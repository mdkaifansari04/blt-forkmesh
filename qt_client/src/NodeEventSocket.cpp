#include "NodeEventSocket.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QTimer>

namespace {

// Event frames are a topic plus a repo path; anything bigger is not ours.
constexpr quint64 kMaxEventPayload = 64 * 1024;
// A server that accepts TCP/TLS but never answers the upgrade would otherwise
// hang the attempt forever: no other timer runs before the 101.
constexpr int kConnectTimeoutMs = 30000;
// Protocol pings ride the edge connection without waking the hibernated
// Durable Object; the app-level {"type":"ping"} beat refreshes the DO's
// staleness clock (NODE_SOCKET_STALE_MS = 15 min server-side) far less often.
constexpr int kPingIntervalMs = 25000;
constexpr qint64 kAppPingIntervalMs = 4 * 60 * 1000;
// Half-open-link watchdog, same guard as ServerNode: if not even a pong has
// arrived for ~2.8x the ping interval the TCP socket is a zombie (NAT timeout,
// silent relay drop) and Qt may never emit disconnected() on its own.
constexpr qint64 kStaleRxMs = 70000;
// Reconnect ramp: 2s, 4s, 8s ... capped at 10 minutes. A dropped event only
// delays a sync until the fallback poll, so this channel backs off harder
// than chat rather than hammering a quota-limited relay.
constexpr int kReconnectBaseDelayMs = 2000;
constexpr qint64 kReconnectMaxDelayMs = 10 * 60 * 1000;

QByteArray randomWsKey()
{
    QByteArray key(16, Qt::Uninitialized);
    for (int i = 0; i < key.size(); ++i)
        key[i] = char(QRandomGenerator::global()->bounded(256));
    return key.toBase64();
}

QString wsRequestPath(const QUrl &url)
{
    QString path = url.path().isEmpty() ? QStringLiteral("/") : url.path();
    if (url.hasQuery())
        path += "?" + url.query();
    return path;
}

} // namespace

NodeEventSocket::NodeEventSocket(QObject *parent) : QObject(parent) {}

void NodeEventSocket::setUrlFactory(std::function<QUrl()> factory)
{
    m_urlFactory = std::move(factory);
}

void NodeEventSocket::setConnectionAuthorizer(
    std::function<bool(const QUrl &)> authorizer)
{
    m_connectionAuthorizer = std::move(authorizer);
}

void NodeEventSocket::start()
{
    m_userStopped = false;
    m_reconnectAttempts = 0;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    openConnection();
}

void NodeEventSocket::stop()
{
    m_userStopped = true;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    if (m_connectTimeoutTimer)
        m_connectTimeoutTimer->stop();
    if (m_pingTimer)
        m_pingTimer->stop();
    const bool wasConnected = m_wsReady;
    m_wsReady = false;
    m_attemptActive = false;
    discardCurrentSocket();
    if (wasConnected)
        emit connectedChanged(false);
}

void NodeEventSocket::discardCurrentSocket()
{
    QTcpSocket *socket = m_socket;
    m_socket = nullptr;
    if (!socket)
        return;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}

void NodeEventSocket::failCurrentConnection()
{
    discardCurrentSocket();
    handleLinkLost();
}

void NodeEventSocket::openConnection()
{
    discardCurrentSocket();
    m_attemptActive = false;
    m_wsReady = false;
    m_readBuffer.clear();

    if (m_userStopped)
        return;

    // A fresh signed URL for every attempt: the ts in the drain token must be
    // within the relay's clock-skew window when the upgrade lands.
    m_url = m_urlFactory ? m_urlFactory() : QUrl();
    if (!m_url.isValid() || m_url.host().isEmpty()) {
        scheduleReconnect(); // e.g. not signed in yet; retry later
        return;
    }
    if (m_connectionAuthorizer && !m_connectionAuthorizer(m_url)) {
        emit systemMessage(
            QStringLiteral("Firewall blocked the relay event socket to %1.")
                .arg(m_url.host()));
        scheduleReconnect();
        return;
    }

    if (!m_connectTimeoutTimer) {
        m_connectTimeoutTimer = new QTimer(this);
        m_connectTimeoutTimer->setSingleShot(true);
        connect(m_connectTimeoutTimer, &QTimer::timeout, this, [this] {
            if (m_wsReady || m_userStopped || !m_attemptActive)
                return;
            failCurrentConnection();
        });
    }
    m_connectTimeoutTimer->start(kConnectTimeoutMs);

    m_socket = m_url.scheme() == QLatin1String("wss") ? new QSslSocket(this)
                                                      : new QTcpSocket(this);
    m_attemptActive = true;
    connectSocketSignals();
    const int port =
        m_url.port(m_url.scheme() == QLatin1String("wss") ? 443 : 80);
    if (auto *ssl = qobject_cast<QSslSocket *>(m_socket))
        ssl->connectToHostEncrypted(m_url.host(), port);
    else
        m_socket->connectToHost(m_url.host(), port);
}

void NodeEventSocket::scheduleReconnect()
{
    if (m_connectTimeoutTimer)
        m_connectTimeoutTimer->stop();
    if (m_userStopped)
        return;
    if (m_reconnectTimer && m_reconnectTimer->isActive())
        return;
    if (!m_reconnectTimer) {
        m_reconnectTimer = new QTimer(this);
        m_reconnectTimer->setSingleShot(true);
        connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
            if (!m_userStopped)
                openConnection();
        });
    }
    const int shift = qMin(m_reconnectAttempts, 9);
    ++m_reconnectAttempts;
    int delay = int(qMin<qint64>(qint64(kReconnectBaseDelayMs) << shift,
                                 kReconnectMaxDelayMs));
    // Jitter keeps a fleet from reconnecting in lockstep after a relay deploy.
    delay += int(QRandomGenerator::global()->bounded(delay / 4 + 250));
    m_reconnectTimer->start(delay);
}

void NodeEventSocket::connectSocketSignals()
{
    QTcpSocket *socket = m_socket;
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        if (socket == m_socket)
            onSocketReadyRead();
    });
    connect(socket, &QTcpSocket::connected, this, [this, socket] {
        if (socket == m_socket && !qobject_cast<QSslSocket *>(socket))
            sendHandshake();
    });
    if (auto *ssl = qobject_cast<QSslSocket *>(socket))
        connect(ssl, &QSslSocket::encrypted, this, [this, socket] {
            if (socket == m_socket)
                sendHandshake();
        });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        if (socket != m_socket)
            return;
        discardCurrentSocket();
        handleLinkLost();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket] {
        if (socket != m_socket)
            return;
        // Some errors do not emit disconnected, while others emit both;
        // detach this exact socket first so either path converges on one
        // reconnect.
        failCurrentConnection();
    });
}

void NodeEventSocket::handleLinkLost()
{
    if (!m_attemptActive && !m_wsReady)
        return;
    m_attemptActive = false;
    if (m_pingTimer)
        m_pingTimer->stop();
    if (m_connectTimeoutTimer)
        m_connectTimeoutTimer->stop();
    const bool wasConnected = m_wsReady;
    m_wsReady = false;
    if (wasConnected)
        emit connectedChanged(false);
    scheduleReconnect();
}

void NodeEventSocket::sendHandshake()
{
    m_wsKey = randomWsKey();
    QByteArray host = m_url.host().toUtf8();
    if (m_url.port() > 0)
        host += ":" + QByteArray::number(m_url.port());
    const QByteArray request =
        "GET " + wsRequestPath(m_url).toUtf8() + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: " + m_wsKey + "\r\n\r\n";
    m_socket->write(request);
}

void NodeEventSocket::onPingTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastRxMs > 0 && now - m_lastRxMs > kStaleRxMs) {
        // Zombie link: writes still "succeed" but nothing (not even pongs)
        // comes back. Tear it down and reconnect.
        failCurrentConnection();
        return;
    }
    sendControlFrame(0x9);
    if (now - m_lastAppPingMs >= kAppPingIntervalMs) {
        m_lastAppPingMs = now;
        sendTextFrame(QJsonDocument(QJsonObject{{QStringLiteral("type"),
                                                 QStringLiteral("ping")}})
                          .toJson(QJsonDocument::Compact));
    }
}

void NodeEventSocket::onSocketReadyRead()
{
    m_readBuffer += m_socket->readAll();
    if (!m_wsReady) {
        const int headerEnd = m_readBuffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray header = m_readBuffer.left(headerEnd);
        m_readBuffer.remove(0, headerEnd + 4);
        if (!header.startsWith("HTTP/1.1 101") &&
            !header.startsWith("HTTP/1.0 101")) {
            // Non-101 is almost always transient (relay redeploy, quota 429)
            // — or a 401 because the signed token aged past the skew window
            // in a slow connect. Reconnect with backoff either way; the
            // fallback /api/sync poll covers the gap.
            const int lineEnd = header.indexOf("\r\n");
            const QString statusLine = QString::fromLatin1(
                (lineEnd < 0 ? header : header.left(lineEnd)).left(80))
                                           .trimmed();
            emit systemMessage(
                QStringLiteral("Relay event socket upgrade refused (%1); "
                               "falling back to the sync poll until it "
                               "reconnects.")
                    .arg(statusLine.isEmpty() ? QStringLiteral("no status")
                                              : statusLine));
            failCurrentConnection();
            return;
        }
        m_wsReady = true;
        m_attemptActive = true;
        m_reconnectAttempts = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        m_lastRxMs = now;
        m_lastAppPingMs = now;
        if (m_reconnectTimer)
            m_reconnectTimer->stop();
        if (m_connectTimeoutTimer)
            m_connectTimeoutTimer->stop();
        if (!m_pingTimer) {
            m_pingTimer = new QTimer(this);
            m_pingTimer->setInterval(kPingIntervalMs);
            connect(m_pingTimer, &QTimer::timeout, this,
                    &NodeEventSocket::onPingTick);
        }
        m_pingTimer->start();
        emit connectedChanged(true);
    }

    while (m_readBuffer.size() >= 2) {
        const uchar b0 = uchar(m_readBuffer.at(0));
        const uchar b1 = uchar(m_readBuffer.at(1));
        const int opcode = b0 & 0x0f;
        quint64 len = b1 & 0x7f;
        int pos = 2;
        if (len == 126) {
            if (m_readBuffer.size() < pos + 2)
                return;
            len = (uchar(m_readBuffer.at(pos)) << 8) |
                  uchar(m_readBuffer.at(pos + 1));
            pos += 2;
        } else if (len == 127) {
            if (m_readBuffer.size() < pos + 8)
                return;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | uchar(m_readBuffer.at(pos + i));
            pos += 8;
        }
        if (len > kMaxEventPayload) {
            failCurrentConnection();
            return;
        }
        const bool masked = b1 & 0x80;
        QByteArray mask;
        if (masked) {
            if (m_readBuffer.size() < pos + 4)
                return;
            mask = m_readBuffer.mid(pos, 4);
            pos += 4;
        }
        if (m_readBuffer.size() < pos + int(len))
            return;
        QByteArray payload = m_readBuffer.mid(pos, int(len));
        m_readBuffer.remove(0, pos + int(len));
        if (masked) {
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = payload.at(i) ^ mask.at(i % 4);
        }
        m_lastRxMs = QDateTime::currentMSecsSinceEpoch();
        if (opcode == 0x9) { // server ping -> pong
            sendControlFrame(0xA, payload);
            continue;
        }
        if (opcode == 0xA) // pong: keepalive acknowledged
            continue;
        if (opcode == 0x8) {
            m_socket->disconnectFromHost();
            return;
        }
        if (opcode == 0x1)
            processFrame(payload);
    }
}

void NodeEventSocket::processFrame(const QByteArray &payload)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return;
    const QJsonObject frame = doc.object();
    // Advisory-only by design: whatever arrives here, the strongest reaction
    // is one signed /api/sync. There is nothing else to parse or trust.
    if (frame.value(QStringLiteral("type")).toString() ==
        QLatin1String("event"))
        emit eventReceived(frame.value(QStringLiteral("topic")).toString(),
                           frame.value(QStringLiteral("repo")).toString());
}

void NodeEventSocket::sendTextFrame(const QByteArray &payload)
{
    if (!m_wsReady || !m_socket || payload.size() > 4096)
        return;
    QByteArray frame;
    frame.append(char(0x81));
    if (payload.size() < 126) {
        frame.append(char(0x80 | payload.size()));
    } else {
        frame.append(char(0x80 | 126));
        frame.append(char((payload.size() >> 8) & 0xff));
        frame.append(char(payload.size() & 0xff));
    }
    QByteArray mask(4, Qt::Uninitialized);
    for (int i = 0; i < 4; ++i)
        mask[i] = char(QRandomGenerator::global()->bounded(256));
    frame.append(mask);
    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i)
        masked[i] = masked.at(i) ^ mask.at(i % 4);
    frame.append(masked);
    m_socket->write(frame);
}

void NodeEventSocket::sendControlFrame(int opcode, const QByteArray &payload)
{
    // Ping/pong control frames (payload <= 125 bytes), client-masked per RFC
    // 6455.
    if (!m_socket || !m_wsReady || payload.size() > 125)
        return;
    QByteArray frame;
    frame.append(char(0x80 | (opcode & 0x0f)));
    frame.append(char(0x80 | payload.size()));
    QByteArray mask(4, Qt::Uninitialized);
    for (int i = 0; i < 4; ++i)
        mask[i] = char(QRandomGenerator::global()->bounded(256));
    frame.append(mask);
    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i)
        masked[i] = masked.at(i) ^ mask.at(i % 4);
    frame.append(masked);
    m_socket->write(frame);
}
