#include "ForkMeshVersion.h"
#include "ChatHistoryLimits.h"
#include "ChatVisitorPresence.h"
#include "ServerNode.h"
#include "SystemStats.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSet>
#include <QSettings>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <utility>

#ifndef FORKMESH_VERSION
#define FORKMESH_VERSION "dev"
#endif

namespace {

constexpr int kSeenCacheLimit = 4096;




constexpr int kMessageIndexLimit = 4096;



constexpr qint64 kPeerReapMs = 3600000;
const QString kKnownRosterGroup = QStringLiteral("mainnode/knownRoster");





constexpr int kPresenceIntervalMs = 60000;
constexpr qint64 kPeerStaleMs = 180000;






constexpr qint64 kStaleRxMs = 70000;


constexpr qint64 kHelloAdvertiseMinIntervalMs = 30000;
constexpr qint64 kHelloReplyMinIntervalMs = 120000;
constexpr qint64 kStatusRepeatMinIntervalMs = 60000;
constexpr quint64 kMaxWsPayload = 96ull * 1024 * 1024;
constexpr int kMaxDisplayNameChars = 32;
constexpr int kMaxSolanaAddressChars = 64;
constexpr int kMaxVersionChars = 32;
constexpr int kMaxTextChars = 16000;
constexpr int kMaxFileNameChars = 180;
constexpr int kMaxMimeChars = 100;
constexpr qsizetype kMaxFileBytes = 64ll * 1024 * 1024;
constexpr qsizetype kMaxBase64FileChars = 90ll * 1024 * 1024;
constexpr qsizetype kMaxAvatarBytes = 256 * 1024;


QString currentPlatform()
{
#if defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_IOS)
    return QStringLiteral("ios");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("unknown");
#endif
}

QByteArray randomKey()
{
    QByteArray key(16, Qt::Uninitialized);
    for (int i = 0; i < key.size(); ++i)
        key[i] = char(QRandomGenerator::global()->bounded(256));
    return key.toBase64();
}

QString wsPath(const QUrl &url)
{
    QString path = url.path().isEmpty() ? "/" : url.path();
    if (url.hasQuery())
        path += "?" + url.query();
    return path;
}

QString endpointDisplay(const QUrl &url)
{
    return url.toString(QUrl::RemoveUserInfo);
}

bool isDurableMainnodeType(const QString &type)
{
    static const QSet<QString> kDurableTypes = {
        QStringLiteral("chat"), QStringLiteral("thread-reply"),
        QStringLiteral("edit"),
        QStringLiteral("delete"), QStringLiteral("reaction"),
        QStringLiteral("admin-delete")};
    return kDurableTypes.contains(type);
}

QString mainnodeScopeFor(const QJsonObject &plain, bool durable)
{
    if (durable)
        return QStringLiteral("durable");
    const QString type = plain.value("type").toString();
    if (type == QLatin1String("dm") || plain.contains(QStringLiteral("to")) ||
        plain.value("private").toBool())
        return QStringLiteral("targeted");
    return QStringLiteral("ephemeral");
}

QString boundedText(const QJsonObject &object, const char *key, int maxChars)
{
    QString value = object.value(key).toString();
    if (value.size() > maxChars)
        value = value.left(maxChars);
    return value;
}

constexpr int kMaxMirrorRepos = 500;
constexpr int kMaxRepoNameChars = 160;






void writeMirrors(QJsonObject &message, const QList<MirrorAdvert> &mirrors)
{
    QJsonArray names;
    QJsonObject heads;
    for (const MirrorAdvert &m : mirrors) {
        if (m.ownerName.isEmpty() || names.size() >= kMaxMirrorRepos)
            continue;
        names.append(m.ownerName);
        QJsonObject head;
        head.insert("c", m.commit);
        head.insert("b", m.branch);
        head.insert("t", m.updatedMs);
        head.insert("s", m.source);
        head.insert("z", double(m.sizeBytes));
        head.insert("i", m.issueCount);

        head.insert("k", m.commitCount);
        head.insert("h", m.branchCount);
        head.insert("p", m.pullCount);
        head.insert("d", m.discussionCount);
        head.insert("w", m.worktreeCount);
        head.insert("a", m.artifactCount);


        head.insert("m", m.commitIdentity.subject.left(kMaxCommitSubjectChars));
        head.insert("n", m.commitIdentity.author.left(kMaxCommitAuthorChars));
        head.insert("e", double(m.commitIdentity.committedAtMs));
        heads.insert(m.ownerName, head);
    }
    message.insert("mirrors", names);
    message.insert("mirrorHeads", heads);
}



QList<MirrorAdvert> readMirrors(const QJsonObject &message)
{
    const QJsonObject heads = message.value("mirrorHeads").toObject();
    QList<MirrorAdvert> mirrors;
    for (const auto &value : message.value("mirrors").toArray()) {
        const QString repo = value.toString().left(kMaxRepoNameChars);
        if (repo.isEmpty() || mirrors.size() >= kMaxMirrorRepos)
            continue;
        MirrorAdvert advert;
        advert.ownerName = repo;
        const QJsonObject head = heads.value(repo).toObject();
        advert.commit = head.value("c").toString().left(64);
        advert.branch = head.value("b").toString().left(kMaxRepoNameChars);
        advert.updatedMs = qint64(head.value("t").toDouble());
        advert.source = head.value("s").toString().left(kMaxRepoNameChars);
        advert.sizeBytes = qMax(qint64(0), qint64(head.value("z").toDouble()));
        advert.issueCount = head.contains("i") ? head.value("i").toInt(-1) : -1;
        advert.commitCount = head.contains("k") ? head.value("k").toInt(-1) : -1;
        advert.branchCount = head.contains("h") ? head.value("h").toInt(-1) : -1;
        advert.pullCount = head.contains("p") ? head.value("p").toInt(-1) : -1;
        advert.discussionCount =
            head.contains("d") ? head.value("d").toInt(-1) : -1;
        advert.worktreeCount = head.contains("w") ? head.value("w").toInt(-1) : -1;
        advert.artifactCount = head.contains("a") ? head.value("a").toInt(-1) : -1;
        advert.commitIdentity.subject =
            head.value("m").toString().left(kMaxCommitSubjectChars);
        advert.commitIdentity.author =
            head.value("n").toString().left(kMaxCommitAuthorChars);
        advert.commitIdentity.committedAtMs =
            qMax(qint64(0), qint64(head.value("e").toDouble()));
        mirrors.append(advert);
    }
    return mirrors;
}

QStringList mirrorNames(const QList<MirrorAdvert> &mirrors)
{
    QStringList names;
    names.reserve(mirrors.size());
    for (const MirrorAdvert &m : mirrors)
        names.append(m.ownerName);
    return names;
}

QString safeFileName(const QString &name)
{
    QString cleaned = name;
    cleaned.replace('\\', '/');
    cleaned = cleaned.section('/', -1).trimmed();
    cleaned.remove('\0');
    if (cleaned.isEmpty())
        cleaned = "file";
    return cleaned.left(kMaxFileNameChars);
}

QByteArray boundedBase64(const QJsonObject &object, const char *key,
                         qsizetype maxBase64Chars, qsizetype maxDecodedBytes)
{
    const QString encoded = object.value(key).toString();
    if (encoded.isEmpty() || encoded.size() > maxBase64Chars)
        return {};
    const QByteArray decoded = QByteArray::fromBase64(encoded.toLatin1());
    if (decoded.size() > maxDecodedBytes)
        return {};
    return decoded;
}

bool messageHasSafePayload(const QJsonObject &message)
{
    return message.value("text").toString().size() <= kMaxTextChars &&
           message.value("sender").toString().size() <= kMaxDisplayNameChars &&
           message.value("solana").toString().size() <= kMaxSolanaAddressChars &&
           message.value("version").toString().size() <= kMaxVersionChars &&
           message.value("fileName").toString().size() <= kMaxFileNameChars &&
           message.value("fileMime").toString().size() <= kMaxMimeChars &&
           message.value("rootId").toString().size() <= 96 &&
           message.value("file").toString().size() <= kMaxBase64FileChars &&
           message.value("png").toString().size() <= 4 * kMaxAvatarBytes / 3 + 8;
}

QString stableOrRandomNodeId(const QString &stableNodeId)
{
    const QString trimmed = stableNodeId.trimmed();
    if (!trimmed.isEmpty())
        return trimmed.left(160);
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

}

ServerNode::ServerNode(const QString &userName, const QString &nodeName,
                       const QString &ownerUser, const QString &stableNodeId,
                       const QUrl &serverUrl,
                       const QString &roomName,
                       const QString &solanaAddress,
                       const QString &roomPassphrase,
                       QObject *parent)
    : ChatBackend(parent),
      m_userName(userName),
      m_nodeName(nodeName.trimmed().left(kMaxDisplayNameChars)),
      m_ownerUser(ownerUser.trimmed().left(kMaxDisplayNameChars)),
      m_url(serverUrl),
      m_roomName(roomName.trimmed()),
      m_solanaAddress(solanaAddress.trimmed().left(kMaxSolanaAddressChars)),
      m_platform(currentPlatform()),
      m_version(QStringLiteral(FORKMESH_VERSION).left(kMaxVersionChars)),
      m_nodeId(stableOrRandomNodeId(stableNodeId)),
      m_crypto(m_roomName)
{





    if (!roomPassphrase.isEmpty())
        m_crypto = RoomCrypto(m_roomName, roomPassphrase);

    const QByteArray material = m_url.toString().toUtf8() + '\n' +
                                m_roomName.toUtf8();
    m_rosterStorageKey =
        QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256)
                                .toHex());
    m_endpoints = {m_url};
    loadKnownPeers();
    loadHiddenChannels();
}

void ServerNode::setEndpoints(const QList<QUrl> &endpoints)
{
    QList<QUrl> valid;
    for (const QUrl &u : endpoints) {
        if (u.isValid() && (u.scheme() == "ws" || u.scheme() == "wss") &&
            !valid.contains(u))
            valid.append(u);
    }
    if (valid.isEmpty())
        return;
    m_endpoints = valid;
    m_endpointIndex = 0;
    m_url = m_endpoints.first();
}

void ServerNode::setConnectionAuthorizer(std::function<bool(const QUrl &)> authorizer)
{
    m_connectionAuthorizer = std::move(authorizer);
}

#ifdef FORKMESH_SERVER_NODE_TESTS
void ServerNode::setTransportLimitsForTests(int connectTimeoutMs,
                                            int reconnectBaseDelayMs,
                                            qint64 maxPendingWriteBytes)
{
    m_connectTimeoutMs = qMax(1, connectTimeoutMs);
    m_reconnectBaseDelayMs = qMax(1, reconnectBaseDelayMs);
    m_maxPendingWriteBytes = qMax<qint64>(64, maxPendingWriteBytes);
    m_reconnectJitter = false;
}
#endif

void ServerNode::setNetworkAvailable(bool available)
{
    if (m_networkAvailable == available)
        return;
    m_networkAvailable = available;

    if (!available) {
        if (m_reconnectTimer)
            m_reconnectTimer->stop();
        if (m_connectTimeoutTimer)
            m_connectTimeoutTimer->stop();
        if (m_socket || m_attemptActive || m_wsReady) {
            discardCurrentSocket();
            handleLinkLost();
        }
        emit statusChanged(QStringLiteral("Offline \xE2\x80\x94 waiting for network\xE2\x80\xA6"));
        return;
    }

    if (m_userStopped || m_wsReady || m_attemptActive)
        return;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    m_reconnectAttempts = 0;
    emit statusChanged(QStringLiteral("Network restored \xE2\x80\x94 reconnecting\xE2\x80\xA6"));
    openConnection();
}

void ServerNode::advanceEndpoint()
{
    if (m_endpoints.size() <= 1)
        return;
    m_endpointIndex = (m_endpointIndex + 1) % m_endpoints.size();
    m_url = m_endpoints.at(m_endpointIndex);
}

bool ServerNode::start()
{
    if (!m_crypto.isValid()) {
        emit fatalError(m_crypto.errorString());
        return false;
    }
    if (!m_url.isValid() || (m_url.scheme() != "ws" && m_url.scheme() != "wss")) {
        emit fatalError("Enter a ws:// or wss:// mainnode URL.");
        return false;
    }
    if (m_roomName.isEmpty()) {
        emit fatalError("Enter a repository room name.");
        return false;
    }

    if (!m_pingTimer) {




        m_pingTimer = new QTimer(this);
        m_pingTimer->setInterval(25000);
        connect(m_pingTimer, &QTimer::timeout, this, [this] {




            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const qint64 lastAlive = qMax(m_lastRxMs, m_wsConnectedAtMs);
            if (m_wsReady && lastAlive > 0 && now - lastAlive > kStaleRxMs) {
                emit systemMessage(
                    QString::fromUtf8("Mainnode link went stale (nothing "
                                      "received for %1s); reconnecting\xE2\x80\xA6")
                        .arg((now - lastAlive) / 1000));
                failCurrentConnection();
                return;
            }
            m_pingSentMs = now;
            sendControlFrame(0x9);
        });
    }

    if (!m_presenceTimer) {


        m_presenceTimer = new QTimer(this);
        m_presenceTimer->setInterval(kPresenceIntervalMs);
        connect(m_presenceTimer, &QTimer::timeout, this, [this] {
            sendPresence();
            reapStalePeers();
            updateRosterAndStatus();
        });
    }

    m_userStopped = false;

    if (!m_endpoints.isEmpty()) {
        m_endpointIndex = 0;
        m_url = m_endpoints.first();
    }
    emit statusChanged("Connecting to " + m_url.host() + "...");
    m_reconnectAttempts = 0;
    if (m_networkAvailable)
        openConnection();
    return true;
}

void ServerNode::discardCurrentSocket()
{
    QTcpSocket *socket = m_socket;
    m_socket = nullptr;
    if (!socket)
        return;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}

void ServerNode::failCurrentConnection()
{
    discardCurrentSocket();
    handleLinkLost();
}

void ServerNode::openConnection()
{

    discardCurrentSocket();
    m_attemptActive = false;
    m_wsReady = false;
    m_wsConnectedAtMs = 0;
    m_readBuffer.clear();
    emit networkDiagnosticsChanged();

    if (m_userStopped || !m_networkAvailable)
        return;

    if (m_connectionAuthorizer && !m_connectionAuthorizer(m_url)) {
        emit systemMessage(QStringLiteral("Firewall blocked mainnode socket to %1.")
                               .arg(m_url.host()));
        scheduleReconnect();
        return;
    }





    if (!m_connectTimeoutTimer) {
        m_connectTimeoutTimer = new QTimer(this);
        m_connectTimeoutTimer->setSingleShot(true);
        connect(m_connectTimeoutTimer, &QTimer::timeout, this, [this] {
            if (m_wsReady || m_userStopped || !m_networkAvailable ||
                !m_attemptActive)
                return;
            emit systemMessage(
                QString::fromUtf8("Mainnode connect to %1 timed out; "
                                  "retrying\xE2\x80\xA6")
                    .arg(m_url.host()));
            failCurrentConnection();
        });
    }
    m_connectTimeoutTimer->start(m_connectTimeoutMs);

    m_socket = m_url.scheme() == "wss" ? new QSslSocket(this) : new QTcpSocket(this);
    m_attemptActive = true;
    connectSocketSignals();
    const int port = m_url.port(m_url.scheme() == "wss" ? 443 : 80);
    if (auto *ssl = qobject_cast<QSslSocket *>(m_socket))
        ssl->connectToHostEncrypted(m_url.host(), port);
    else
        m_socket->connectToHost(m_url.host(), port);
}

void ServerNode::scheduleReconnect()
{



    if (m_connectTimeoutTimer)
        m_connectTimeoutTimer->stop();
    if (m_userStopped || !m_networkAvailable)
        return;
    if (m_reconnectTimer && m_reconnectTimer->isActive())
        return;

    if (!m_reconnectTimer) {
        m_reconnectTimer = new QTimer(this);
        m_reconnectTimer->setSingleShot(true);
        connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
            if (m_userStopped || !m_networkAvailable)
                return;



            advanceEndpoint();
            emit statusChanged("Reconnecting to " + m_url.host() + "...");
            openConnection();
        });
    }

















    const int endpointCount = qMax(1, int(m_endpoints.size()));
    const int shift = qMin(m_reconnectAttempts / endpointCount, 9);
    ++m_reconnectAttempts;
    int delay = int(qMin<qint64>(
        qint64(m_reconnectBaseDelayMs) << shift, 300000));
    if (m_reconnectJitter)
        delay += int(QRandomGenerator::global()->bounded(delay / 4 + 250));
    if (delay >= 5000)
        emit statusChanged(
            QString::fromUtf8("Disconnected \xE2\x80\x94 reconnecting in %1s\xE2\x80\xA6")
                .arg((delay + 500) / 1000));
    else
        emit statusChanged(
            QString::fromUtf8("Disconnected \xE2\x80\x94 reconnecting\xE2\x80\xA6"));
    m_reconnectTimer->start(delay);
}

void ServerNode::connectSocketSignals()
{
    QTcpSocket *socket = m_socket;
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        if (socket == m_socket)
            onSocketReadyRead();
    });
    connect(socket, &QTcpSocket::connected, this, [this, socket] {
        if (socket == m_socket && !qobject_cast<QSslSocket *>(socket))
            onConnectedTransport();
    });
    if (auto *ssl = qobject_cast<QSslSocket *>(socket))
        connect(ssl, &QSslSocket::encrypted, this, [this, socket] {
            if (socket == m_socket)
                onConnectedTransport();
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
        emit systemMessage("Mainnode socket error: " + socket->errorString());



        failCurrentConnection();
    });
}

void ServerNode::handleLinkLost()
{



    if (!m_attemptActive && !m_wsReady)
        return;
    m_attemptActive = false;
    if (m_pingTimer)
        m_pingTimer->stop();
    if (m_presenceTimer)
        m_presenceTimer->stop();
    if (m_helloAdvertiseTimer)
        m_helloAdvertiseTimer->stop();
    if (m_connectTimeoutTimer)
        m_connectTimeoutTimer->stop();
    const bool wasConnected = m_wsReady;
    m_wsReady = false;
    m_wsConnectedAtMs = 0;
    m_peers.clear();
    m_lastHelloReplyMs.clear();
    emit networkDiagnosticsChanged();
    if (wasConnected)
        emit connectionChanged(false);
    updateRosterAndStatus();
    scheduleReconnect();
}

void ServerNode::onConnectedTransport()
{
    sendHandshake();
}

void ServerNode::sendHandshake()
{
    m_wsKey = randomKey();
    QByteArray host = m_url.host().toUtf8();
    if (m_url.port() > 0)
        host += ":" + QByteArray::number(m_url.port());
    const QByteArray request =
        "GET " + wsPath(m_url).toUtf8() + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: " + m_wsKey + "\r\n\r\n";
    m_socket->write(request);
}

void ServerNode::onSocketReadyRead()
{
    m_readBuffer += m_socket->readAll();
    if (!m_wsReady) {
        const int headerEnd = m_readBuffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray header = m_readBuffer.left(headerEnd);
        m_readBuffer.remove(0, headerEnd + 4);
        if (!header.startsWith("HTTP/1.1 101") && !header.startsWith("HTTP/1.0 101")) {







            const int lineEnd = header.indexOf("\r\n");
            const QString statusLine = QString::fromLatin1(
                (lineEnd < 0 ? header : header.left(lineEnd)).left(80)).trimmed();
            emit systemMessage(
                "Mainnode did not accept the WebSocket upgrade ("
                + (statusLine.isEmpty() ? QStringLiteral("no status") : statusLine)
                + "); reconnecting\xE2\x80\xA6");
            m_wsReady = false;
            failCurrentConnection();
            return;
        }
        m_wsReady = true;
        m_attemptActive = true;
        m_wsConnectedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_reconnectAttempts = 0;
        if (m_reconnectTimer)
            m_reconnectTimer->stop();
        if (m_connectTimeoutTimer)
            m_connectTimeoutTimer->stop();
        if (m_pingTimer)
            m_pingTimer->start();
        if (m_presenceTimer)
            m_presenceTimer->start();
        emit channelsChanged(m_channels);
        updateRosterAndStatus();
        emit statusChanged("Connected to encrypted mainnode room " + m_roomName);
        emit connectionChanged(true);
        emit networkDiagnosticsChanged();
        sendHello(true, true);
        if (!m_avatarPng.isEmpty())
            setAvatar(m_avatarPng);
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
            len = (uchar(m_readBuffer.at(pos)) << 8) | uchar(m_readBuffer.at(pos + 1));
            pos += 2;
        } else if (len == 127) {
            if (m_readBuffer.size() < pos + 8)
                return;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | uchar(m_readBuffer.at(pos + i));
            pos += 8;
        }
        if (len > kMaxWsPayload) {
            emit systemMessage("Mainnode frame exceeded the safe size limit; disconnecting.");
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
        if (opcode == 0x9) {
            ++m_rxControlFrames;
            m_lastRxMs = QDateTime::currentMSecsSinceEpoch();
            emit networkDiagnosticsChanged();
            sendControlFrame(0xA, payload);
            continue;
        }
        if (opcode == 0xA) {
            ++m_rxControlFrames;
            m_lastRxMs = QDateTime::currentMSecsSinceEpoch();
            if (m_pingSentMs > 0) {
                emit latencySampled(int(qMin<qint64>(
                    m_lastRxMs - m_pingSentMs, 60 * 1000)));
                m_pingSentMs = 0;
            }
            emit networkDiagnosticsChanged();
            continue;
        }
        if (opcode == 0x8) {
            ++m_rxControlFrames;
            m_lastRxMs = QDateTime::currentMSecsSinceEpoch();
            emit networkDiagnosticsChanged();
            m_socket->disconnectFromHost();
            return;
        }
        if (opcode == 0x1) {
            ++m_rxFrames;
            m_rxBytes += payload.size();
            m_lastRxBytes = payload.size();
            m_lastRxMs = QDateTime::currentMSecsSinceEpoch();
            m_lastRxType = QStringLiteral("text");
            m_lastRxScope.clear();
            processFrame(payload);
            emit networkDiagnosticsChanged();
        }
    }
}

void ServerNode::processFrame(const QByteArray &payload)
{
    if (quint64(payload.size()) > kMaxWsPayload)
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return;
    const QJsonObject envelope = doc.object();
    const QJsonObject plain = m_crypto.decryptObject(envelope);
    if (plain.isEmpty())
        return;
    const QString type = plain.value("type").toString();
    if (!type.isEmpty()) {
        m_lastRxType = type;
        m_lastRxScope = mainnodeScopeFor(plain, envelope.value("persist").toBool());
    }
    if (plain.value("senderId").toString() == m_nodeId)
        return;
    handlePlain(plain);
}

void ServerNode::sendTextFrame(const QByteArray &payload, const QString &type,
                               const QString &scope)
{
    if (!m_wsReady || !m_socket)
        return;
    if (quint64(payload.size()) > kMaxWsPayload) {
        emit systemMessage("Message exceeded the safe mainnode frame limit and was not sent.");
        return;
    }
    const qint64 frameBytes = qint64(payload.size()) + 14;
    const qint64 buffered = m_socket->bytesToWrite();
    if (frameBytes > m_maxPendingWriteBytes ||
        buffered > m_maxPendingWriteBytes - frameBytes) {
        ++m_backpressureDrops;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastBackpressureNoticeMs >= 10000) {
            m_lastBackpressureNoticeMs = now;
            emit systemMessage(
                QStringLiteral("Mainnode send queue is full; dropped one "
                               "outgoing frame instead of growing memory."));
        }
        emit networkDiagnosticsChanged();
        return;
    }
    QByteArray frame;
    frame.append(char(0x81));
    if (payload.size() < 126) {
        frame.append(char(0x80 | payload.size()));
    } else if (payload.size() <= 0xffff) {
        frame.append(char(0x80 | 126));
        frame.append(char((payload.size() >> 8) & 0xff));
        frame.append(char(payload.size() & 0xff));
    } else {
        frame.append(char(0x80 | 127));
        quint64 len = payload.size();
        for (int i = 7; i >= 0; --i)
            frame.append(char((len >> (8 * i)) & 0xff));
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
    ++m_txFrames;
    m_txBytes += payload.size();
    m_lastTxBytes = payload.size();
    m_lastTxMs = QDateTime::currentMSecsSinceEpoch();
    m_lastTxType = type.isEmpty() ? QStringLiteral("text") : type;
    m_lastTxScope = scope;
    emit networkDiagnosticsChanged();
}

void ServerNode::sendControlFrame(int opcode, const QByteArray &payload)
{

    if (!m_socket || !m_wsReady || payload.size() > 125)
        return;
    if (m_socket->bytesToWrite() >
        m_maxPendingWriteBytes - (qint64(payload.size()) + 6)) {
        ++m_backpressureDrops;
        emit networkDiagnosticsChanged();
        return;
    }
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
    ++m_txControlFrames;
    m_lastTxMs = QDateTime::currentMSecsSinceEpoch();
    emit networkDiagnosticsChanged();
}

QList<QJsonObject> ServerNode::networkDiagnostics() const
{
    QJsonObject row;
    row.insert(QStringLiteral("connection"), QStringLiteral("Mainnode room"));
    row.insert(QStringLiteral("kind"), QStringLiteral("mainnode"));
    row.insert(QStringLiteral("durableObject"),
               QStringLiteral("Room DO: %1").arg(m_roomName));
    row.insert(QStringLiteral("endpoint"), endpointDisplay(m_url));
    row.insert(QStringLiteral("state"),
               m_wsReady ? QStringLiteral("Connected")
                         : (m_socket ? QStringLiteral("Connecting")
                                     : QStringLiteral("Disconnected")));
    row.insert(QStringLiteral("connected"), m_wsReady);
    row.insert(QStringLiteral("connectedAtMs"), double(m_wsConnectedAtMs));
    row.insert(QStringLiteral("lastRxMs"), double(m_lastRxMs));
    row.insert(QStringLiteral("lastTxMs"), double(m_lastTxMs));
    row.insert(QStringLiteral("rxFrames"), double(m_rxFrames));
    row.insert(QStringLiteral("txFrames"), double(m_txFrames));
    row.insert(QStringLiteral("rxBytes"), double(m_rxBytes));
    row.insert(QStringLiteral("txBytes"), double(m_txBytes));
    row.insert(QStringLiteral("rxControlFrames"), double(m_rxControlFrames));
    row.insert(QStringLiteral("txControlFrames"), double(m_txControlFrames));
    row.insert(QStringLiteral("bufferedBytes"),
               double(m_socket ? m_socket->bytesToWrite() : 0));
    row.insert(QStringLiteral("backpressureDrops"),
               double(m_backpressureDrops));
    row.insert(QStringLiteral("networkAvailable"), m_networkAvailable);
    row.insert(QStringLiteral("lastRxBytes"), m_lastRxBytes);
    row.insert(QStringLiteral("lastTxBytes"), m_lastTxBytes);
    row.insert(QStringLiteral("lastRxType"), m_lastRxType);
    row.insert(QStringLiteral("lastRxScope"), m_lastRxScope);
    row.insert(QStringLiteral("lastTxType"), m_lastTxType);
    row.insert(QStringLiteral("lastTxScope"), m_lastTxScope);
    row.insert(QStringLiteral("peers"), m_peers.size());
    row.insert(QStringLiteral("channels"), m_channels.size());
    row.insert(QStringLiteral("mirrors"), m_mirroredRepos.size());
    row.insert(QStringLiteral("data"),
               QStringLiteral("Encrypted room frames: chat/edit/delete/reaction/admin-delete "
                              "are durable history; hello/presence/typing/avatar/history/"
                              "mirror/mirror-refresh/cove frames are ephemeral; direct/"
                              "private frames are targeted."));
    return {row};
}

QJsonObject ServerNode::makeMessage(const QString &type) const
{
    QJsonObject message{{"type", type},
                        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                        {"senderId", m_nodeId},
                        {"sender", m_userName.left(kMaxDisplayNameChars)},
                        {"ts", double(QDateTime::currentMSecsSinceEpoch())}};



    if (!m_accountKind.isEmpty())
        message.insert("accountKind", m_accountKind);
    if (!m_nodeName.isEmpty())
        message.insert("nodeName", m_nodeName);
    if (!m_ownerUser.isEmpty())
        message.insert("ownerUser", m_ownerUser);
    if (!m_solanaAddress.isEmpty())
        message.insert("solana", m_solanaAddress);
    if (!m_platform.isEmpty())
        message.insert("platform", m_platform);
    if (!m_version.isEmpty())
        message.insert("version", m_version);



    if (m_memTotalBytes > 0 || m_diskTotalBytes > 0 || m_cpuPercent >= 0.0) {
        QJsonObject sys;
        if (m_memTotalBytes > 0) {
            sys.insert("mu", double(m_memUsedBytes));
            sys.insert("mt", double(m_memTotalBytes));
        }
        if (m_diskTotalBytes > 0) {
            sys.insert("du", double(m_diskUsedBytes));
            sys.insert("dt", double(m_diskTotalBytes));
        }
        if (m_cpuPercent >= 0.0)
            sys.insert("cpu", m_cpuPercent);
        message.insert("sys", sys);
    }
    return message;
}

void ServerNode::sendEncrypted(const QJsonObject &plain, bool showActivity)
{
    QJsonObject envelope = m_crypto.encryptObject(plain);
    if (envelope.isEmpty()) {
        emit systemMessage("Mainnode encryption failed; message was not sent.");
        return;
    }




    const QString type = plain.value("type").toString();
    const bool durable = isDurableMainnodeType(type);
    if (durable)
        envelope.insert("persist", true);
    sendTextFrame(QJsonDocument(envelope).toJson(QJsonDocument::Compact),
                  type, mainnodeScopeFor(plain, durable));
    if (showActivity)
        emit systemMessage("Network: sent encrypted " + type +
                           " through mainnode room " + m_roomName + ".");
}

void ServerNode::sampleSystemStats()
{



    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastStatsSampleMs != 0 && now - m_lastStatsSampleMs < 10000)
        return;
    m_lastStatsSampleMs = now;





    QSettings settings;
    const bool showCpu = settings.value(TelemetrySettings::kReportCpu, false).toBool();
    const bool showMem = settings.value(TelemetrySettings::kReportMemory, false).toBool();
    const bool showDisk = settings.value(TelemetrySettings::kReportDisk, false).toBool();

    if (showMem) {
        const qint64 total = SystemStats::totalMemoryBytes();
        const qint64 avail = SystemStats::availableMemoryBytes();
        m_memTotalBytes = total;
        m_memUsedBytes = (total > 0 && avail > 0) ? qMax(qint64(0), total - avail) : 0;
    } else {
        m_memTotalBytes = 0;
        m_memUsedBytes = 0;
    }

    if (showDisk) {



        QString diskPath =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (diskPath.isEmpty() || !QDir(diskPath).exists())
            diskPath = QDir::homePath();
        const qint64 diskTotal = SystemStats::diskTotalBytes(diskPath);
        const qint64 diskFree = SystemStats::diskFreeBytes(diskPath);
        m_diskTotalBytes = diskTotal;
        m_diskUsedBytes =
            (diskTotal > 0 && diskFree >= 0) ? qMax(qint64(0), diskTotal - diskFree) : 0;
    } else {
        m_diskTotalBytes = 0;
        m_diskUsedBytes = 0;
    }

    m_cpuPercent = showCpu ? SystemStats::hostCpuPercent() : -1.0;
}

void ServerNode::sendHello(bool force, bool showActivity)
{
    if (!m_wsReady)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && m_lastHelloSentMs > 0) {
        const qint64 remaining =
            kHelloAdvertiseMinIntervalMs - (now - m_lastHelloSentMs);
        if (remaining > 0) {
            if (!m_helloAdvertiseTimer) {
                m_helloAdvertiseTimer = new QTimer(this);
                m_helloAdvertiseTimer->setSingleShot(true);
                connect(m_helloAdvertiseTimer, &QTimer::timeout, this,
                        [this] { sendHello(true, false); });
            }
            if (!m_helloAdvertiseTimer->isActive())
                m_helloAdvertiseTimer->start(int(qMin<qint64>(
                    remaining, kHelloAdvertiseMinIntervalMs)));
            return;
        }
    }
    m_lastHelloSentMs = now;
    sampleSystemStats();
    QJsonArray channels;
    for (const QString &channel : std::as_const(m_channels))
        if (!m_privateChannels.contains(channel))
            channels.append(channel);
    QJsonObject hello = makeMessage("hello");
    hello.insert("channels", channels);
    writeMirrors(hello, m_mirroredRepos);
    sendEncrypted(hello, showActivity);
}

void ServerNode::sendPresence()
{
    if (!m_wsReady)
        return;
    sampleSystemStats();



    sendEncrypted(makeMessage("presence"), false);
}

void ServerNode::setMirroredRepos(const QList<MirrorAdvert> &repos)
{




    bool changed = repos.size() != m_mirroredRepos.size();
    for (int i = 0; !changed && i < repos.size(); ++i)
        changed = repos.at(i).ownerName != m_mirroredRepos.at(i).ownerName ||
                  repos.at(i).source != m_mirroredRepos.at(i).source ||
                  repos.at(i).commit != m_mirroredRepos.at(i).commit ||
                  repos.at(i).branch != m_mirroredRepos.at(i).branch ||
                  repos.at(i).updatedMs != m_mirroredRepos.at(i).updatedMs ||
                  repos.at(i).sizeBytes != m_mirroredRepos.at(i).sizeBytes ||
                  repos.at(i).issueCount != m_mirroredRepos.at(i).issueCount ||
                  repos.at(i).commitCount != m_mirroredRepos.at(i).commitCount ||
                  repos.at(i).branchCount != m_mirroredRepos.at(i).branchCount ||
                  repos.at(i).pullCount != m_mirroredRepos.at(i).pullCount ||
                  repos.at(i).discussionCount !=
                      m_mirroredRepos.at(i).discussionCount ||
                  repos.at(i).worktreeCount !=
                      m_mirroredRepos.at(i).worktreeCount ||
                  repos.at(i).artifactCount !=
                      m_mirroredRepos.at(i).artifactCount ||
                  repos.at(i).commitIdentity.subject !=
                      m_mirroredRepos.at(i).commitIdentity.subject ||
                  repos.at(i).commitIdentity.author !=
                      m_mirroredRepos.at(i).commitIdentity.author;
    if (!changed)
        return;
    m_mirroredRepos = repos;
    if (m_wsReady)
        sendHello();
}

void ServerNode::notifyMirrorUpdated(const QString &ownerName,
                                     const QString &commit)
{
    if (ownerName.trimmed().isEmpty() || !m_wsReady)
        return;
    QJsonObject message = makeMessage("mirror-update");
    message.insert("repo", ownerName.trimmed().left(160));


    if (!commit.trimmed().isEmpty())
        message.insert("commit", commit.trimmed().left(64));



    markSeen(message.value("id").toString());
    sendEncrypted(message, false);
}

void ServerNode::notifyMirrorSynced(const QString &ownerName,
                                    const QString &commit)
{
    if (ownerName.trimmed().isEmpty() || !m_wsReady)
        return;




    QJsonObject message = makeMessage("mirror-synced");
    message.insert("repo", ownerName.trimmed().left(160));
    if (!commit.trimmed().isEmpty())
        message.insert("commit", commit.trimmed().left(64));
    markSeen(message.value("id").toString());
    sendEncrypted(message, false);
}

void ServerNode::requestMirrorRefresh(const QString &source,
                                      const QString &ownerName)
{
    if (!m_wsReady)
        return;
    const QString cleanSource = source.trimmed().left(kMaxRepoNameChars);
    const QString cleanOwnerName = ownerName.trimmed().left(kMaxRepoNameChars);
    if (cleanSource.isEmpty() && cleanOwnerName.isEmpty())
        return;
    QJsonObject message = makeMessage("mirror-refresh");
    if (!cleanSource.isEmpty())
        message.insert("source", cleanSource);
    if (!cleanOwnerName.isEmpty())
        message.insert("repo", cleanOwnerName);


    markSeen(message.value("id").toString());
    sendEncrypted(message, false);
}

void ServerNode::advertiseMirrorsNow()
{




    m_lastStatsSampleMs = 0;
    sampleSystemStats();
    updateRosterAndStatus();
    sendHello(true, false);
}

void ServerNode::notifyCoveOpened(const QString &creatorKey, const QString &coveId,
                                  const QString &coveName, const QString &openerKey,
                                  const QString &openerName, qint64 ts,
                                  const QString &signature)
{
    if (creatorKey.trimmed().isEmpty() || !m_wsReady)
        return;
    QJsonObject message = makeMessage("cove-open");
    message.insert("creator", creatorKey.left(120));
    message.insert("coveId", coveId.left(80));
    message.insert("coveName", coveName.left(160));
    message.insert("openerKey", openerKey.left(120));
    message.insert("openerName", openerName.left(80));
    message.insert("coveTs", double(ts));
    message.insert("sig", signature.left(200));


    markSeen(message.value("id").toString());
    sendEncrypted(message, false);
}

void ServerNode::notifyCoveInvited(const QString &inviteeAccount, const QString &coveId,
                                   const QString &coveName, const QString &inviterName,
                                   qint64 ts)
{
    if (inviteeAccount.trimmed().isEmpty() || !m_wsReady)
        return;
    QJsonObject message = makeMessage("cove-invite");
    message.insert("invitee", inviteeAccount.left(120));
    message.insert("coveId", coveId.left(80));
    message.insert("coveName", coveName.left(160));
    message.insert("inviterName", inviterName.left(80));
    message.insert("coveTs", double(ts));



    markSeen(message.value("id").toString());
    sendEncrypted(message, false);
}

void ServerNode::sendChat(const QString &channel, const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    QJsonObject message = makeMessage("chat");
    message.insert("channel", channel);
    message.insert("text", text.left(kMaxTextChars));
    if (m_privateChannels.contains(channel))
        message.insert("private", true);
    markSeen(message.value("id").toString());
    storeHistory(message);
    sendEncrypted(message, true);
    emitChat(message);
}

void ServerNode::sendThreadReply(const QString &channel,
                                 const QString &rootMessageId,
                                 const QString &text)
{
    const QString rootId = rootMessageId.trimmed().left(96);
    if (channel.trimmed().isEmpty() || rootId.isEmpty() ||
        text.trimmed().isEmpty())
        return;
    QJsonObject message = makeMessage("thread-reply");
    message.insert("channel", channel);
    message.insert("rootId", rootId);
    message.insert("text", text.left(kMaxTextChars));
    if (m_privateChannels.contains(channel))
        message.insert("private", true);
    markSeen(message.value("id").toString());
    storeHistory(message);
    sendEncrypted(message, true);
    emitChat(message);
}

void ServerNode::setAccountKind(const QString &kind)
{
    m_accountKind = kind.trimmed().left(16);
}

void ServerNode::setRoomPassphrase(const QString &passphrase)
{







    if (passphrase.isEmpty())
        return;
    m_crypto = RoomCrypto(m_roomName, passphrase);
}

void ServerNode::sendBotChat(const QString &channel, const QString &text)
{







    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || m_privateChannels.contains(channel))
        return;
    QJsonObject message{{"type", QStringLiteral("chat")},
                        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                        {"senderId", QStringLiteral("forkbot")},
                        {"sender", QStringLiteral("forkbot")},
                        {"accountKind", QStringLiteral("user")},
                        {"ts", double(QDateTime::currentMSecsSinceEpoch())},
                        {"channel", channel},
                        {"text", trimmed.left(kMaxTextChars)}};
    markSeen(message.value("id").toString());
    storeHistory(message);
    sendEncrypted(message, false);
    emitChat(message);
}

void ServerNode::sendDirect(const QString &targetId, const QString &text)
{
    if (text.trimmed().isEmpty() || targetId == m_nodeId)
        return;
    QJsonObject message = makeMessage("dm");
    message.insert("to", targetId);
    message.insert("text", text.left(kMaxTextChars));
    markSeen(message.value("id").toString());
    sendEncrypted(message, true);
    emitDm(message, targetId);
}

void ServerNode::sendFile(const QString &conversation, const QString &fileName,
                          const QString &mimeType, const QByteArray &data)
{
    if (fileName.isEmpty() || data.isEmpty() || data.size() > kMaxFileBytes)
        return;
    if (conversation.startsWith('@')) {
        QJsonObject message = makeMessage("dm");
        message.insert("to", conversation.mid(1));
        message.insert("fileName", safeFileName(fileName));
        message.insert("fileMime", mimeType.left(kMaxMimeChars));
        message.insert("file", QString::fromLatin1(data.toBase64()));
        markSeen(message.value("id").toString());
        sendEncrypted(message, true);
        emitDm(message, conversation.mid(1));
    } else {
        QJsonObject message = makeMessage("chat");
        message.insert("channel", conversation);
        message.insert("fileName", safeFileName(fileName));
        message.insert("fileMime", mimeType.left(kMaxMimeChars));
        message.insert("file", QString::fromLatin1(data.toBase64()));
        if (m_privateChannels.contains(conversation))
            message.insert("private", true);
        markSeen(message.value("id").toString());
        storeHistory(message);
        sendEncrypted(message, true);
        emitChat(message);
    }
}

void ServerNode::sendReaction(const QString &conversation, const QString &messageId,
                              const QString &emoji)
{
    if (messageId.isEmpty() || emoji.isEmpty())
        return;
    const bool added =
        !m_reactions.value(messageId).value(emoji).contains(m_nodeId);
    QJsonObject message = makeMessage("reaction");
    message.insert("conversation", conversation);
    message.insert("target", messageId);
    message.insert("emoji", emoji);
    message.insert("reactorId", m_nodeId);
    message.insert("reactorName", m_userName);
    message.insert("added", added);
    handlePlain(message);
    sendEncrypted(message, false);
}

void ServerNode::editMessage(const QString &conversation, const QString &messageId,
                             const QString &newText)
{
    const QString text = newText.trimmed().left(kMaxTextChars);
    if (messageId.isEmpty() || text.isEmpty() ||
        !messageIsAuthoredBy(messageId, m_nodeId))
        return;
    QJsonObject message = makeMessage("edit");
    message.insert("conversation", conversation);
    message.insert("target", messageId);
    message.insert("text", text);
    handlePlain(message);
    sendEncrypted(message, false);
}

void ServerNode::deleteMessage(const QString &conversation, const QString &messageId)
{
    if (messageId.isEmpty() || !messageIsAuthoredBy(messageId, m_nodeId))
        return;
    QJsonObject message = makeMessage("delete");
    message.insert("conversation", conversation);
    message.insert("target", messageId);
    handlePlain(message);
    sendEncrypted(message, false);
}

void ServerNode::sendAdminDelete(const QString &conversation, const QString &messageId,
                                 qint64 ts, const QString &signature)
{
    if (messageId.isEmpty() || signature.isEmpty())
        return;



    QJsonObject message = makeMessage("admin-delete");
    message.insert("ts", double(ts));
    message.insert("conversation", conversation);
    message.insert("target", messageId);
    message.insert("sig", signature);
    sendEncrypted(message, false);
}

void ServerNode::applyAdminDelete(const QString &conversation,
                                  const QString &messageId)
{

    applyDeleteUnchecked(conversation, messageId);
}

void ServerNode::setAvatar(const QByteArray &pngData)
{
    if (pngData.size() > kMaxAvatarBytes)
        return;
    m_avatarPng = pngData;
    QJsonObject message = makeMessage("avatar");
    message.insert("png", QString::fromLatin1(pngData.toBase64()));
    markSeen(message.value("id").toString());
    emit avatarChanged(m_nodeId, pngData);
    sendEncrypted(message, false);
}

void ServerNode::setUserName(const QString &name)
{
    const QString trimmed = name.trimmed().left(kMaxDisplayNameChars);
    if (trimmed.isEmpty() || trimmed == m_userName)
        return;
    m_userName = trimmed;
    updateRosterAndStatus();
    if (m_wsReady)
        sendHello();
}

void ServerNode::setNodeIdentity(const QString &nodeName, const QString &ownerUser)
{
    const QString node = nodeName.trimmed().left(kMaxDisplayNameChars);
    const QString owner = ownerUser.trimmed().left(kMaxDisplayNameChars);
    if (node == m_nodeName && owner == m_ownerUser)
        return;
    m_nodeName = node;
    m_ownerUser = owner;
    updateRosterAndStatus();
    if (m_wsReady)
        sendHello();
}

void ServerNode::forgetMember(const QString &peerId)
{
    if (peerId.isEmpty() || peerId == m_nodeId)
        return;
    if (m_peers.remove(peerId) <= 0)
        return;
    persistKnownPeers();
    updateRosterAndStatus();
    emit systemMessage("Removed member from the live roster.");
}

void ServerNode::sendTyping(const QString &conversation, bool active)
{
    QJsonObject message = makeMessage("typing");
    message.insert("conversation", conversation);
    message.insert("active", active);
    sendEncrypted(message, false);
}

QString ServerNode::hiddenChannelsSettingKey() const
{


    return QStringLiteral("chat/hiddenChannels/") + m_rosterStorageKey;
}

void ServerNode::loadHiddenChannels()
{
    const QStringList hidden =
        QSettings().value(hiddenChannelsSettingKey()).toStringList();
    m_hiddenChannels = QSet<QString>(hidden.begin(), hidden.end());


    for (const QString &name : hidden)
        m_channels.removeAll(name);
}

void ServerNode::saveHiddenChannels()
{
    QSettings().setValue(hiddenChannelsSettingKey(),
                         QStringList(m_hiddenChannels.begin(),
                                     m_hiddenChannels.end()));
}

void ServerNode::addChannel(const QString &channel)
{
    QString name = channel.trimmed();
    if (name.isEmpty())
        return;
    if (!name.startsWith('#'))
        name.prepend('#');


    if (m_hiddenChannels.contains(name))
        return;
    if (!m_channels.contains(name)) {
        m_channels.append(name);
        emit channelsChanged(m_channels);
    }
    QJsonObject message = makeMessage("channel");
    message.insert("name", name);
    markSeen(message.value("id").toString());
    sendEncrypted(message, true);
}

void ServerNode::removeChannel(const QString &channel)
{
    QString name = channel.trimmed();
    if (name.isEmpty())
        return;
    if (!name.startsWith('#'))
        name.prepend('#');
    m_hiddenChannels.insert(name);
    saveHiddenChannels();
    m_privateChannels.remove(name);
    if (m_channels.removeAll(name) > 0)
        emit channelsChanged(m_channels);

    m_channelHistory.remove(name);
    m_channelHistoryChars.remove(name);
}

void ServerNode::createPrivateChannel(const QString &channel)
{
    QString name = channel.trimmed();
    if (name.isEmpty())
        return;
    if (!name.startsWith('#'))
        name.prepend('#');

    if (m_hiddenChannels.remove(name))
        saveHiddenChannels();
    m_privateChannels.insert(name);
    if (!m_channels.contains(name)) {
        m_channels.append(name);
        emit channelsChanged(m_channels);
    }
    emit privateChannelJoined(name);

}

void ServerNode::inviteToChannel(const QString &peerId, const QString &channel)
{
    QString name = channel.trimmed();
    if (peerId.isEmpty() || name.isEmpty())
        return;
    if (!name.startsWith('#'))
        name.prepend('#');


    if (!m_privateChannels.contains(name))
        return;
    QJsonObject message = makeMessage("invite");
    message.insert("to", peerId);
    message.insert("channel", name);
    markSeen(message.value("id").toString());


    sendEncrypted(message, true);

    sendChannelHistoryTo(peerId, name);
}

void ServerNode::shutdown()
{
    m_userStopped = true;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    if (m_pingTimer)
        m_pingTimer->stop();
    if (m_presenceTimer)
        m_presenceTimer->stop();
    if (m_connectTimeoutTimer)
        m_connectTimeoutTimer->stop();
    QJsonObject bye = makeMessage("bye");
    sendEncrypted(bye, true);
    const bool wasConnected = m_wsReady;
    discardCurrentSocket();
    m_attemptActive = false;
    m_wsReady = false;
    m_wsConnectedAtMs = 0;
    if (wasConnected)
        emit connectionChanged(false);
    emit networkDiagnosticsChanged();
}

void ServerNode::handlePlain(const QJsonObject &message)
{
    if (!messageHasSafePayload(message)) {
        emit systemMessage("Dropped oversized or unsafe mainnode payload.");
        return;
    }
    const QString type = message.value("type").toString();
    if (type != "typing" && !markSeen(message.value("id").toString()))
        return;
    const QString senderId = message.value("senderId").toString();
    const QString sender = message.value("sender").toString();
    const QString nodeName = boundedText(message, "nodeName", kMaxDisplayNameChars);
    const QString ownerUser = boundedText(message, "ownerUser", kMaxDisplayNameChars);
    const QString accountKind = message.value("accountKind").toString().left(16);
    const QString solanaAddress = boundedText(message, "solana", kMaxSolanaAddressChars);
    const QString platform = message.value("platform").toString().left(16);
    const QString version = boundedText(message, "version", kMaxVersionChars);
    if (!senderId.isEmpty())
        rememberPeer(senderId, sender, nodeName, ownerUser, accountKind,
                     solanaAddress, platform, version);




    if (!senderId.isEmpty() && message.contains("sys") &&
        m_peers.contains(senderId)) {
        const QJsonObject sys = message.value("sys").toObject();
        Peer &peer = m_peers[senderId];
        if (sys.contains("mt")) {
            peer.memTotalBytes = qMax(qint64(0), qint64(sys.value("mt").toDouble()));
            peer.memUsedBytes = qMax(qint64(0), qint64(sys.value("mu").toDouble()));
        }
        if (sys.contains("dt")) {
            peer.diskTotalBytes = qMax(qint64(0), qint64(sys.value("dt").toDouble()));
            peer.diskUsedBytes = qMax(qint64(0), qint64(sys.value("du").toDouble()));
        }
        if (sys.contains("cpu"))
            peer.cpuPercent = qBound(0.0, sys.value("cpu").toDouble(), 100.0);
    }

    if (type == "hello") {
        bool changed = false;
        for (const auto &value : message.value("channels").toArray()) {
            const QString channel = value.toString();
            if (!channel.isEmpty() && !m_channels.contains(channel) &&
                !m_hiddenChannels.contains(channel)) {
                m_channels.append(channel);
                changed = true;
            }
        }
        if (changed)
            emit channelsChanged(m_channels);


        if (m_peers.contains(senderId)) {
            const QList<MirrorAdvert> mirrors = readMirrors(message);
            m_peers[senderId].mirrorDetails = mirrors;
            m_peers[senderId].mirrors = mirrorNames(mirrors);
            updateRosterAndStatus();
        }






        if (message.value("to").toString().isEmpty()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const qint64 last = m_lastHelloReplyMs.value(senderId, 0);
            if (last <= 0 || now - last >= kHelloReplyMinIntervalMs) {
                m_lastHelloReplyMs.insert(senderId, now);
                sendHistoryTo(senderId);
                QJsonArray channels;
                for (const QString &channel : std::as_const(m_channels))
                    if (!m_privateChannels.contains(channel))
                        channels.append(channel);
                QJsonObject reply = makeMessage("hello");
                reply.insert("channels", channels);



                writeMirrors(reply, m_mirroredRepos);
                reply.insert("to", senderId);
                sendEncrypted(reply, false);
            }
        }
    } else if (type == "chat" || type == "thread-reply") {
        const QString channel = message.value("channel").toString();
        if (type == "thread-reply" &&
            message.value("rootId").toString().trimmed().isEmpty())
            return;
        // A private-room message from a room we weren't invited to is ignored,
        // the same honour-model as a direct message addressed to someone else.
        if (message.value("private").toBool() && !m_channels.contains(channel))
            return;

        if (m_hiddenChannels.contains(channel))
            return;
        if (!m_channels.contains(channel))
            m_channels.append(channel);
        storeHistory(message);
        emitChat(message);
    } else if (type == "invite") {


        if (message.value("to").toString() == m_nodeId) {
            QString channel = message.value("channel").toString().left(120);
            if (!channel.isEmpty()) {
                if (!channel.startsWith('#'))
                    channel.prepend('#');
                if (m_hiddenChannels.contains(channel))
                    return;
                m_privateChannels.insert(channel);
                if (!m_channels.contains(channel)) {
                    m_channels.append(channel);
                    emit channelsChanged(m_channels);
                }
                emit privateChannelJoined(channel);
                emit systemMessage(sender + " invited you to private room " +
                                   channel + ".");
            }
        }
    } else if (type == "dm") {
        const QString to = message.value("to").toString();
        if (to == m_nodeId)
            emitDm(message, senderId);
    } else if (type == "channel") {
        const QString name = message.value("name").toString();
        if (!m_channels.contains(name) && !m_hiddenChannels.contains(name)) {
            m_channels.append(name);
            emit channelsChanged(m_channels);
        }
    } else if (type == "mirror-update") {
        const QString repo = message.value("repo").toString().left(160);
        if (!repo.isEmpty())
            emit mirrorUpdated(repo, sender,
                               message.value("commit").toString().left(64));
    } else if (type == "mirror-synced") {
        const QString repo = message.value("repo").toString().left(160);
        if (!repo.isEmpty())
            emit mirrorSynced(repo, sender,
                              message.value("commit").toString().left(64));
    } else if (type == "mirror-refresh") {
        if (!message.value("to").toString().isEmpty() &&
            message.value("to").toString() != m_nodeId)
            return;
        const QString source =
            message.value("source").toString().left(kMaxRepoNameChars);
        const QString repo =
            message.value("repo").toString().left(kMaxRepoNameChars);
        bool relevant = source.isEmpty() && repo.isEmpty();
        for (const MirrorAdvert &m : std::as_const(m_mirroredRepos)) {
            if ((!source.isEmpty() &&
                 m.source.compare(source, Qt::CaseInsensitive) == 0) ||
                (!repo.isEmpty() &&
                 m.ownerName.compare(repo, Qt::CaseInsensitive) == 0)) {
                relevant = true;
                break;
            }
        }
        if (relevant)
            emit mirrorRefreshRequested(source.isEmpty() ? repo : source, sender);
    } else if (type == "cove-open") {
        const QString creator = message.value("creator").toString().left(120);
        if (!creator.isEmpty())
            emit coveOpened(creator, message.value("coveId").toString().left(80),
                            message.value("coveName").toString().left(160),
                            message.value("openerKey").toString().left(120),
                            message.value("openerName").toString().left(80),
                            qint64(message.value("coveTs").toDouble()),
                            message.value("sig").toString().left(200));
    } else if (type == "cove-invite") {
        const QString invitee = message.value("invitee").toString().left(120);
        if (!invitee.isEmpty())
            emit coveInvited(invitee, message.value("coveId").toString().left(80),
                             message.value("coveName").toString().left(160),
                             message.value("inviterName").toString().left(80),
                             qint64(message.value("coveTs").toDouble()));
    } else if (type == "reaction") {
        const QString target = message.value("target").toString();
        const QString emoji = message.value("emoji").toString();
        const QString reactorId = message.value("reactorId").toString();
        const QString reactorName = message.value("reactorName").toString();



        if (m_messageConversation.contains(target)) {
            if (message.value("added").toBool())
                m_reactions[target][emoji].insert(reactorId, reactorName);
            else
                m_reactions[target][emoji].remove(reactorId);
        }
        emit reactionChanged(message.value("conversation").toString(), target,
                             emoji, reactorName, message.value("added").toBool());
    } else if (type == "edit") {
        const QString target = message.value("target").toString();
        if (messageIsAuthoredBy(target, senderId))
            applyEdit(message.value("conversation").toString(), target, senderId,
                      boundedText(message, "text", kMaxTextChars));
    } else if (type == "delete") {
        const QString target = message.value("target").toString();
        if (messageIsAuthoredBy(target, senderId))
            applyDelete(message.value("conversation").toString(), target, senderId);
    } else if (type == "admin-delete") {



        const QString target = message.value("target").toString();
        const QString sig = message.value("sig").toString();
        const qint64 ts = qint64(message.value("ts").toDouble());
        if (!target.isEmpty() && !sig.isEmpty())
            emit adminDeleteRequested(message.value("conversation").toString(),
                                      target, senderId, sender, ts, sig);
    } else if (type == "avatar") {
        const QByteArray avatar =
            boundedBase64(message, "png", 4 * kMaxAvatarBytes / 3 + 8,
                          kMaxAvatarBytes);
        if (!avatar.isEmpty())
            emit avatarChanged(senderId, avatar);
    } else if (type == "typing") {
        emit typingChanged(message.value("conversation").toString(), senderId,
                           sender, message.value("active").toBool());
    } else if (type == "history") {
        if (!message.value("to").toString().isEmpty() &&
            message.value("to").toString() != m_nodeId)
            return;
        for (const auto &value : message.value("entries").toArray()) {
            const QJsonObject entry = value.toObject();
            if (!messageHasSafePayload(entry))
                continue;


            if (entry.value("private").toBool() &&
                !m_channels.contains(entry.value("channel").toString()))
                continue;
            if (markSeen(entry.value("id").toString())) {
                storeHistory(entry);
                emitChat(entry);
            }
        }
    } else if (type == "bye") {
        if (m_peers.contains(senderId)) {
            m_peers.remove(senderId);
            emit systemMessage(sender + " left");
            updateRosterAndStatus();
        }
    }
}

void ServerNode::sendHistoryTo(const QString &peerId)
{
    if (peerId.isEmpty() || m_channelHistory.isEmpty())
        return;
    QJsonArray entries;
    for (auto it = m_channelHistory.constBegin(); it != m_channelHistory.constEnd();
         ++it) {


        if (m_privateChannels.contains(it.key()))
            continue;
        for (const QJsonObject &entry : it.value())
            entries.append(entry);
    }
    QJsonObject message = makeMessage("history");
    message.insert("to", peerId);
    message.insert("entries", entries);
    sendEncrypted(message, false);
}

void ServerNode::sendChannelHistoryTo(const QString &peerId, const QString &channel)
{
    if (peerId.isEmpty() || !m_channelHistory.contains(channel))
        return;
    QJsonArray entries;
    for (const QJsonObject &entry : std::as_const(m_channelHistory[channel]))
        entries.append(entry);
    if (entries.isEmpty())
        return;
    QJsonObject message = makeMessage("history");
    message.insert("to", peerId);
    message.insert("entries", entries);
    sendEncrypted(message, false);
}

void ServerNode::emitChat(const QJsonObject &message)
{
    ChatMessage out;
    out.id = message.value("id").toString();
    out.conversation = message.value("channel").toString();
    out.threadRootId = message.value("rootId").toString().left(96);
    out.senderId = message.value("senderId").toString();
    out.senderName = boundedText(message, "sender", kMaxDisplayNameChars);
    out.text = boundedText(message, "text", kMaxTextChars);
    out.timestampMs = qint64(message.value("ts").toDouble());
    out.fileName = safeFileName(message.value("fileName").toString());
    if (out.fileName == "file" && !message.contains("fileName"))
        out.fileName.clear();
    out.fileMime = boundedText(message, "fileMime", kMaxMimeChars);
    if (message.contains("file"))
        out.fileData = boundedBase64(message, "file", kMaxBase64FileChars, kMaxFileBytes);
    out.self = out.senderId == m_nodeId;
    out.edited = message.value("edited").toBool();
    out.deleted = message.value("deleted").toBool();
    indexMessage(out.id, out.conversation, out.senderId);
    emit messageArrived(out);
}

void ServerNode::emitDm(const QJsonObject &message, const QString &conversationPeer)
{
    ChatMessage out;
    out.id = message.value("id").toString();
    out.conversation = "@" + conversationPeer;
    out.senderId = message.value("senderId").toString();
    out.senderName = boundedText(message, "sender", kMaxDisplayNameChars);
    out.text = boundedText(message, "text", kMaxTextChars);
    out.timestampMs = qint64(message.value("ts").toDouble());
    out.fileName = safeFileName(message.value("fileName").toString());
    if (out.fileName == "file" && !message.contains("fileName"))
        out.fileName.clear();
    out.fileMime = boundedText(message, "fileMime", kMaxMimeChars);
    if (message.contains("file"))
        out.fileData = boundedBase64(message, "file", kMaxBase64FileChars, kMaxFileBytes);
    out.self = out.senderId == m_nodeId;
    out.edited = message.value("edited").toBool();
    out.deleted = message.value("deleted").toBool();
    indexMessage(out.id, out.conversation, out.senderId);
    emit messageArrived(out);
}

void ServerNode::rememberPeer(const QString &peerId, const QString &name,
                              const QString &nodeName, const QString &ownerUser,
                              const QString &accountKind,
                              const QString &solanaAddress,
                              const QString &platform,
                              const QString &version, bool online)
{
    if (peerId.isEmpty() || peerId == m_nodeId)
        return;
    Peer &peer = m_peers[peerId];
    peer.name = name.isEmpty() ? peer.name : name;
    if (!nodeName.trimmed().isEmpty())
        peer.nodeName = nodeName.trimmed().left(kMaxDisplayNameChars);
    if (!ownerUser.trimmed().isEmpty())
        peer.ownerUser = ownerUser.trimmed().left(kMaxDisplayNameChars);
    if (!accountKind.trimmed().isEmpty())
        peer.accountKind = accountKind.trimmed().left(16);
    if (!solanaAddress.trimmed().isEmpty())
        peer.solanaAddress = solanaAddress.trimmed().left(kMaxSolanaAddressChars);
    if (!platform.isEmpty())
        peer.platform = platform;
    if (!version.isEmpty())
        peer.version = version;
    peer.online = online;
    peer.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
    updateRosterAndStatus();
}

void ServerNode::updateRosterAndStatus()
{



    if (!m_rosterEmitTimer) {
        m_rosterEmitTimer = new QTimer(this);
        m_rosterEmitTimer->setSingleShot(true);
        m_rosterEmitTimer->setInterval(200);
        connect(m_rosterEmitTimer, &QTimer::timeout, this,
                &ServerNode::flushRosterAndStatus);
    }
    if (!m_rosterEmitTimer->isActive())
        m_rosterEmitTimer->start();
}

void ServerNode::flushRosterAndStatus()
{


    sampleSystemStats();
    MemberInfo self;
    self.id = m_nodeId;
    self.name = m_userName;
    self.nodeName = m_nodeName.isEmpty() ? m_userName : m_nodeName;
    self.ownerUser = m_ownerUser;
    self.accountKind = m_accountKind;
    self.self = true;
    self.online = m_wsReady;
    self.solanaAddress = m_solanaAddress;
    self.platform = m_platform;
    self.version = m_version;
    self.mirrors = mirrorNames(m_mirroredRepos);
    self.mirrorDetails = m_mirroredRepos;
    self.memUsedBytes = m_memUsedBytes;
    self.memTotalBytes = m_memTotalBytes;
    self.diskUsedBytes = m_diskUsedBytes;
    self.diskTotalBytes = m_diskTotalBytes;
    self.cpuPercent = m_cpuPercent;
    QList<MemberInfo> members{self};
    int onlineCount = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        if (!it->online)
            continue;


        if (now - it->lastSeenMs > kPeerStaleMs)
            continue;
        MemberInfo member;
        member.id = it.key();
        member.name = it->name;
        member.nodeName = it->nodeName;
        member.ownerUser = it->ownerUser;
        member.accountKind = it->accountKind;
        member.note = QString();
        member.online = it->online;
        member.solanaAddress = it->solanaAddress;
        member.platform = it->platform;
        member.version = it->version;
        member.mirrors = it->mirrors;
        member.mirrorDetails = it->mirrorDetails;
        member.memUsedBytes = it->memUsedBytes;
        member.memTotalBytes = it->memTotalBytes;
        member.diskUsedBytes = it->diskUsedBytes;
        member.diskTotalBytes = it->diskTotalBytes;
        member.cpuPercent = it->cpuPercent;
        members.append(member);
        ++onlineCount;
    }
    emit rosterChanged(members);
    const QString status = QString::number(onlineCount) +
                           " peer(s) online · encrypted room " + m_roomName;
    if (status != m_lastStatusText ||
        now - m_lastStatusEmitMs >= kStatusRepeatMinIntervalMs) {
        m_lastStatusText = status;
        m_lastStatusEmitMs = now;
        emit statusChanged(status);
    }
}

void ServerNode::loadKnownPeers()
{
    QSettings settings;
    settings.remove(kKnownRosterGroup + "/" + m_rosterStorageKey);
}

void ServerNode::persistKnownPeers() const
{
    QSettings settings;
    settings.remove(kKnownRosterGroup + "/" + m_rosterStorageKey);
}

void ServerNode::storeHistory(const QJsonObject &message)
{
    const QString channel = message.value("channel").toString();
    if (channel.isEmpty())
        return;
    const QStringList evicted = ChatHistoryLimits::appendBounded(
        m_channelHistory[channel], message, &m_channelHistoryChars[channel]);
    for (const QString &id : evicted)
        dropMessageIndex(id);
}

void ServerNode::indexMessage(const QString &id, const QString &conversation,
                              const QString &senderId)
{
    if (id.isEmpty())
        return;
    if (!m_messageConversation.contains(id))
        m_messageIndexOrder.enqueue(id);
    m_messageConversation.insert(id, conversation);
    m_messageSender.insert(id, senderId);



    while (m_messageIndexOrder.size() > kMessageIndexLimit)
        dropMessageIndex(m_messageIndexOrder.dequeue());
}

void ServerNode::dropMessageIndex(const QString &id)
{
    m_messageConversation.remove(id);
    m_messageSender.remove(id);
    m_reactions.remove(id);
}

void ServerNode::reapStalePeers()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_peers.begin(); it != m_peers.end();) {



        const qint64 reapAfter =
            ChatVisitorPresence::isTransientVisitor(it->accountKind, it->name)
                ? ChatVisitorPresence::kVisitorIdleMs
                : kPeerReapMs;
        if (now - it->lastSeenMs > reapAfter) {
            m_lastHelloReplyMs.remove(it.key());
            it = m_peers.erase(it);
        } else {
            ++it;
        }
    }
}

bool ServerNode::messageIsAuthoredBy(const QString &messageId, const QString &senderId) const
{
    return !messageId.isEmpty() && !senderId.isEmpty() &&
           m_messageSender.value(messageId) == senderId;
}

void ServerNode::updateStoredMessage(const QString &messageId, const QString &text,
                                     bool deleted)
{
    for (auto it = m_channelHistory.begin(); it != m_channelHistory.end(); ++it) {
        QList<QJsonObject> &messages = it.value();
        for (QJsonObject &message : messages) {
            if (message.value("id").toString() != messageId)
                continue;
            const qsizetype costBefore = ChatHistoryLimits::entryCost(message);
            if (deleted) {
                message.insert("deleted", true);
                message.insert("edited", false);
                message.insert("text", QString());
                message.remove("fileName");
                message.remove("fileMime");
                message.remove("file");
            } else {
                message.insert("text", text.left(kMaxTextChars));
                message.insert("edited", true);
            }



            const auto total = m_channelHistoryChars.find(it.key());
            if (total != m_channelHistoryChars.end())
                *total += ChatHistoryLimits::entryCost(message) - costBefore;
            return;
        }
    }
}

void ServerNode::applyEdit(const QString &conversation, const QString &target,
                           const QString &senderId, const QString &text)
{
    if (!messageIsAuthoredBy(target, senderId) || text.trimmed().isEmpty())
        return;
    const QString conv =
        conversation.isEmpty() ? m_messageConversation.value(target) : conversation;
    updateStoredMessage(target, text, false);
    emit messageEdited(conv, target, text.left(kMaxTextChars));
}

void ServerNode::applyDelete(const QString &conversation, const QString &target,
                             const QString &senderId)
{
    if (!messageIsAuthoredBy(target, senderId))
        return;
    applyDeleteUnchecked(conversation, target);
}

void ServerNode::applyDeleteUnchecked(const QString &conversation,
                                      const QString &target)
{
    if (target.isEmpty())
        return;
    const QString conv =
        conversation.isEmpty() ? m_messageConversation.value(target) : conversation;
    updateStoredMessage(target, QString(), true);
    m_reactions.remove(target);
    emit messageDeleted(conv, target);
}

bool ServerNode::markSeen(const QString &messageId)
{
    if (messageId.isEmpty() || m_seenIds.contains(messageId))
        return false;
    m_seenIds.insert(messageId);
    m_seenOrder.enqueue(messageId);
    while (m_seenOrder.size() > kSeenCacheLimit)
        m_seenIds.remove(m_seenOrder.dequeue());
    return true;
}
