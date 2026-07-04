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

#ifndef FORKMESH_VERSION
#define FORKMESH_VERSION "dev"
#endif

namespace {

constexpr int kSeenCacheLimit = 4096;
const QString kKnownRosterGroup = QStringLiteral("mainnode/knownRoster");
// Each connected node rebroadcasts a lightweight presence frame on this cadence
// so peers keep its "last seen" fresh; a peer not heard from for longer than the
// stale window is treated as offline and dropped from the roster. This is what
// reaps nodes that vanished without a clean "bye" (crash, network loss) instead
// of leaving them shown as online indefinitely.
constexpr int kPresenceIntervalMs = 60000;   // 60s broadcast
constexpr qint64 kPeerStaleMs = 180000;       // 3 missed beats -> offline
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

// This node's operating system, advertised to peers.
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

QString boundedText(const QJsonObject &object, const char *key, int maxChars)
{
    QString value = object.value(key).toString();
    if (value.size() > maxChars)
        value = value.left(maxChars);
    return value;
}

constexpr int kMaxMirrorRepos = 500;
constexpr int kMaxRepoNameChars = 160;

// Serialize advertised mirrors into the two wire fields used by `hello`:
//   "mirrors"     — a plain array of "owner/name" (read by every peer, old or new)
//   "mirrorHeads" — an object "owner/name" -> {c:commit, b:branch, t:syncedMs}
// carrying the HEAD each mirror holds. Splitting it this way keeps older peers
// (which only know "mirrors") working while newer peers gain freshness detail.
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
        head.insert("s", m.source); // shared upstream identity for grouping
        head.insert("z", double(m.sizeBytes)); // on-disk mirror size in bytes
        head.insert("i", m.issueCount); // issues this mirror holds (-1 = unknown)
        // More per-node tallies for the Mirror nodes view (-1 = unknown/older peer).
        head.insert("k", m.commitCount);     // commits on the served branch
        head.insert("h", m.branchCount);     // local branches (refs/heads)
        head.insert("p", m.pullCount);       // pull requests
        head.insert("d", m.discussionCount); // discussions
        head.insert("w", m.worktreeCount);   // working-copy worktrees (agent tasks)
        head.insert("a", m.artifactCount);   // release artifacts hosted for download
        heads.insert(m.ownerName, head);
    }
    message.insert("mirrors", names);
    message.insert("mirrorHeads", heads);
}

// Inverse of writeMirrors: rebuild the advertised mirror list from a hello,
// preferring the rich "mirrorHeads" detail and falling back to bare names.
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

} // namespace

ServerNode::ServerNode(const QString &userName, const QString &stableNodeId,
                       const QUrl &serverUrl,
                       const QString &roomName,
                       const QString &solanaAddress,
                       QObject *parent)
    : ChatBackend(parent),
      m_userName(userName),
      m_url(serverUrl),
      m_roomName(roomName.trimmed()),
      m_solanaAddress(solanaAddress.trimmed().left(kMaxSolanaAddressChars)),
      m_platform(currentPlatform()),
      m_version(QStringLiteral(FORKMESH_VERSION).left(kMaxVersionChars)),
      m_nodeId(stableOrRandomNodeId(stableNodeId)),
      m_crypto(m_roomName)
{
    const QByteArray material = m_url.toString().toUtf8() + '\n' +
                                m_roomName.toUtf8();
    m_rosterStorageKey =
        QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256)
                                .toHex());
    m_endpoints = {m_url}; // default to the single URL; setEndpoints() adds failovers
    loadKnownPeers();
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
        // A periodic WebSocket ping keeps the relay (and Cloudflare's edge)
        // from dropping the connection while the node is idle.
        m_pingTimer = new QTimer(this);
        m_pingTimer->setInterval(25000);
        connect(m_pingTimer, &QTimer::timeout, this,
                [this] { sendControlFrame(0x9); });
    }

    if (!m_presenceTimer) {
        // Heartbeat into the room so peers keep our last-seen fresh, and sweep
        // our own roster so peers that went stale (no "bye") stop showing online.
        m_presenceTimer = new QTimer(this);
        m_presenceTimer->setInterval(kPresenceIntervalMs);
        connect(m_presenceTimer, &QTimer::timeout, this, [this] {
            sendPresence();
            updateRosterAndStatus(); // re-evaluate staleness even with no traffic
        });
    }

    // A fresh join starts from the most-preferred mainnode with fast retries.
    if (!m_endpoints.isEmpty()) {
        m_endpointIndex = 0;
        m_url = m_endpoints.first();
    }
    emit statusChanged("Connecting to " + m_url.host() + "...");
    m_reconnectAttempts = 0;
    openConnection();
    return true;
}

void ServerNode::openConnection()
{
    // Tear down any previous socket (e.g. a failed attempt) before reconnecting.
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_wsReady = false;
    m_readBuffer.clear();

    m_socket = m_url.scheme() == "wss" ? new QSslSocket(this) : new QTcpSocket(this);
    connectSocketSignals();
    const int port = m_url.port(m_url.scheme() == "wss" ? 443 : 80);
    if (auto *ssl = qobject_cast<QSslSocket *>(m_socket))
        ssl->connectToHostEncrypted(m_url.host(), port);
    else
        m_socket->connectToHost(m_url.host(), port);
}

void ServerNode::scheduleReconnect()
{
    if (m_userStopped)
        return; // the user left the node; don't keep retrying
    if (m_reconnectTimer && m_reconnectTimer->isActive())
        return; // a retry is already pending

    if (!m_reconnectTimer) {
        m_reconnectTimer = new QTimer(this);
        m_reconnectTimer->setSingleShot(true);
        connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
            if (m_userStopped)
                return;
            // Fail over to the next configured mainnode before each retry so a
            // dead/quota-limited endpoint is skipped instead of hammered forever
            // (issue #364). With a single endpoint this is a no-op.
            advanceEndpoint();
            emit statusChanged("Reconnecting to " + m_url.host() + "...");
            openConnection();
        });
    }

    // Retry quickly at first so the node reconnects promptly after a brief
    // relay redeploy (adhoc #192), then back off exponentially — 1s, 2s, 4s …
    // capped at 5 minutes — while the relay keeps refusing. A cap of 60s
    // (the original adhoc #66 value) is reached after only ~6 attempts
    // (about a minute), so a Cloudflare daily-quota 429 that lasts hours
    // spent the rest of the outage hammering the relay every ~60-75s —
    // barely different from no backoff at all, and the jitter on a fixed
    // base made consecutive waits look random rather than growing (adhoc
    // #68). Stretching the ramp out and raising the ceiling keeps genuine
    // exponential growth visible for longer and cuts the steady-state
    // request rate once it does plateau. Jitter keeps many nodes from
    // reconnecting in lockstep; the counter resets once an upgrade succeeds.
    // With multiple mainnodes, probe them all at the fast rate before the delay
    // grows: the backoff shift advances once per full cycle through the endpoint
    // list (issue #364), so failover to a healthy peer stays quick while a
    // network-wide outage still ramps down to the 5-minute ceiling.
    const int endpointCount = qMax(1, int(m_endpoints.size()));
    const int shift = qMin(m_reconnectAttempts / endpointCount, 9);
    ++m_reconnectAttempts;
    int delay = qMin(1000 << shift, 300000);
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
    connect(m_socket, &QTcpSocket::readyRead, this, &ServerNode::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::connected, this, [this] {
        if (!qobject_cast<QSslSocket *>(m_socket))
            onConnectedTransport();
    });
    if (auto *ssl = qobject_cast<QSslSocket *>(m_socket))
        connect(ssl, &QSslSocket::encrypted, this, &ServerNode::onConnectedTransport);
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        if (m_pingTimer)
            m_pingTimer->stop();
        if (m_presenceTimer)
            m_presenceTimer->stop();
        m_wsReady = false;
        m_peers.clear();
        updateRosterAndStatus();
        scheduleReconnect();
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] {
        emit systemMessage("Mainnode socket error: " + m_socket->errorString());
        // A connect failure may not emit disconnected, so retry from here too.
        if (!m_wsReady)
            scheduleReconnect();
    });
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
            // A non-101 here is almost always transient — the mainnode is
            // redeploying, quota-limited (Cloudflare 429 page) or briefly
            // returning an error/HTML page. Treat it like any other dropped
            // link and reconnect with backoff instead of a fatal error, which
            // would kick the user all the way back to the onboarding screen
            // every time the relay is deployed. Include the status line so a
            // quota 429 is distinguishable from a genuine routing bug.
            const int lineEnd = header.indexOf("\r\n");
            const QString statusLine = QString::fromLatin1(
                (lineEnd < 0 ? header : header.left(lineEnd)).left(80)).trimmed();
            emit systemMessage(
                "Mainnode did not accept the WebSocket upgrade ("
                + (statusLine.isEmpty() ? QStringLiteral("no status") : statusLine)
                + "); reconnecting\xE2\x80\xA6");
            m_wsReady = false;
            if (m_socket)
                m_socket->abort();
            scheduleReconnect();
            return;
        }
        m_wsReady = true;
        m_reconnectAttempts = 0; // link is healthy again; retry fast next drop
        if (m_pingTimer)
            m_pingTimer->start();
        if (m_presenceTimer)
            m_presenceTimer->start();
        emit channelsChanged(m_channels);
        updateRosterAndStatus();
        emit statusChanged("Connected to encrypted mainnode room " + m_roomName);
        sendHello();
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
            m_socket->disconnectFromHost();
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
        if (opcode == 0x9) { // ping from server -> reply with pong
            sendControlFrame(0xA, payload);
            continue;
        }
        if (opcode == 0xA) // pong: keepalive acknowledged, nothing to do
            continue;
        if (opcode == 0x8) {
            m_socket->disconnectFromHost();
            return;
        }
        if (opcode == 0x1)
            processFrame(payload);
    }
}

void ServerNode::processFrame(const QByteArray &payload)
{
    if (quint64(payload.size()) > kMaxWsPayload)
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return;
    const QJsonObject plain = m_crypto.decryptObject(doc.object());
    if (plain.isEmpty() || plain.value("senderId").toString() == m_nodeId)
        return;
    handlePlain(plain);
}

void ServerNode::sendTextFrame(const QByteArray &payload)
{
    if (!m_wsReady || !m_socket)
        return;
    if (quint64(payload.size()) > kMaxWsPayload) {
        emit systemMessage("Message exceeded the safe mainnode frame limit and was not sent.");
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
}

void ServerNode::sendControlFrame(int opcode, const QByteArray &payload)
{
    // Ping/pong control frames (payload <= 125 bytes), client-masked per RFC 6455.
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

QJsonObject ServerNode::makeMessage(const QString &type) const
{
    QJsonObject message{{"type", type},
                        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                        {"senderId", m_nodeId},
                        {"sender", m_userName.left(kMaxDisplayNameChars)},
                        {"ts", double(QDateTime::currentMSecsSinceEpoch())}};
    if (!m_solanaAddress.isEmpty())
        message.insert("solana", m_solanaAddress);
    if (!m_platform.isEmpty())
        message.insert("platform", m_platform);
    if (!m_version.isEmpty())
        message.insert("version", m_version);
    // Host resource telemetry for the Mirror nodes view's CPU/RAM/disk bars. Sent
    // on every frame so peers refresh it on any heartbeat; unknown fields are
    // omitted. Compact keys keep the per-frame overhead tiny (~60 bytes).
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
    // Tag durable conversation messages so the relay can retain them (still
    // encrypted) and replay them to nodes that join later — giving new users
    // some recent history even when no other node is online. Ephemeral frames
    // (typing/presence/hello/history/avatar) and private DMs are never retained.
    static const QSet<QString> kDurableTypes = {
        QStringLiteral("chat"), QStringLiteral("edit"),
        QStringLiteral("delete"), QStringLiteral("reaction"),
        QStringLiteral("admin-delete")};
    if (kDurableTypes.contains(plain.value("type").toString()))
        envelope.insert("persist", true);
    sendTextFrame(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
    if (showActivity)
        emit systemMessage("Network: sent encrypted " + plain.value("type").toString() +
                           " through mainnode room " + m_roomName + ".");
}

void ServerNode::sampleSystemStats()
{
    // Re-sample at most once per ~10s so a burst of chat sends (each of which
    // builds a frame via makeMessage) doesn't repeatedly stat the filesystem.
    // The heartbeats (hello/presence) call this; chat sends reuse the cache.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastStatsSampleMs != 0 && now - m_lastStatsSampleMs < 10000)
        return;
    m_lastStatsSampleMs = now;

    // Each metric is sampled (and thus advertised + shown on our self row) only
    // when its display toggle is on. Disabled metrics reset to the "unknown"
    // sentinel so makeMessage() omits them and the nodes view draws no bar
    // (adhoc #23). Re-read every sample so a runtime toggle takes effect.
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
        // Measure the volume that holds our app data (where mirrors live); fall
        // back to the home directory so a meaningful figure shows even without
        // app data.
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

void ServerNode::sendHello()
{
    sampleSystemStats();
    QJsonArray channels;
    for (const QString &channel : std::as_const(m_channels))
        if (!m_privateChannels.contains(channel))
            channels.append(channel);
    QJsonObject hello = makeMessage("hello");
    hello.insert("channels", channels);
    writeMirrors(hello, m_mirroredRepos);
    sendEncrypted(hello, true);
}

void ServerNode::sendPresence()
{
    if (!m_wsReady)
        return;
    sampleSystemStats();
    // A bare keep-alive: makeMessage already carries senderId/name/platform, so
    // peers refresh our last-seen on receipt. Ephemeral (not persisted) and not
    // logged as activity, unlike hello, so it stays quiet on the network log.
    sendEncrypted(makeMessage("presence"), false);
}

void ServerNode::setMirroredRepos(const QList<MirrorAdvert> &repos)
{
    // Re-advertise when the set of repos OR any HEAD changed (a fresh sync moves
    // a commit/timestamp without changing the name list), so peers see freshness.
    bool changed = repos.size() != m_mirroredRepos.size();
    for (int i = 0; !changed && i < repos.size(); ++i)
        changed = repos.at(i).ownerName != m_mirroredRepos.at(i).ownerName ||
                  repos.at(i).commit != m_mirroredRepos.at(i).commit;
    if (!changed)
        return;
    m_mirroredRepos = repos;
    if (m_wsReady)
        sendHello(); // re-advertise so peers see the updated mirror set
}

void ServerNode::notifyMirrorUpdated(const QString &ownerName)
{
    if (ownerName.trimmed().isEmpty() || !m_wsReady)
        return;
    QJsonObject message = makeMessage("mirror-update");
    message.insert("repo", ownerName.trimmed().left(160));
    // Pre-mark our own id so the relay's echo back to us isn't surfaced as a
    // self-notification. This is an ephemeral frame (not in kDurableTypes), so
    // the relay won't retain or replay it.
    markSeen(message.value("id").toString());
    sendEncrypted(message, false);
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
    // Pre-mark our own id so the relay's echo back to us isn't re-processed. This
    // is an ephemeral frame (not in kDurableTypes), so the relay won't retain it.
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
    // The UI signed a canonical string that binds this exact ts/messageId/
    // conversation to the admin's key; carry the same ts (overriding the one
    // makeMessage stamps) so every receiver reconstructs the identical string.
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
    // Called by the UI only after it verified the signature and admin status.
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

void ServerNode::addChannel(const QString &channel)
{
    QString name = channel.trimmed();
    if (name.isEmpty())
        return;
    if (!name.startsWith('#'))
        name.prepend('#');
    if (!m_channels.contains(name)) {
        m_channels.append(name);
        emit channelsChanged(m_channels);
    }
    QJsonObject message = makeMessage("channel");
    message.insert("name", name);
    markSeen(message.value("id").toString());
    sendEncrypted(message, true);
}

void ServerNode::createPrivateChannel(const QString &channel)
{
    QString name = channel.trimmed();
    if (name.isEmpty())
        return;
    if (!name.startsWith('#'))
        name.prepend('#');
    m_privateChannels.insert(name);
    if (!m_channels.contains(name)) {
        m_channels.append(name);
        emit channelsChanged(m_channels);
    }
    emit privateChannelJoined(name);
    // Deliberately NOT broadcast: a private room only reaches peers we invite.
}

void ServerNode::inviteToChannel(const QString &peerId, const QString &channel)
{
    QString name = channel.trimmed();
    if (peerId.isEmpty() || name.isEmpty())
        return;
    if (!name.startsWith('#'))
        name.prepend('#');
    // Only private rooms are invite-based; a public channel is already visible
    // to everyone, so an "invite" to one is meaningless.
    if (!m_privateChannels.contains(name))
        return;
    QJsonObject message = makeMessage("invite");
    message.insert("to", peerId);
    message.insert("channel", name);
    markSeen(message.value("id").toString());
    // Persist so an invitee who is offline right now still receives it when they
    // reconnect (non-target members ignore it via the "to" check).
    sendEncrypted(message, true);
    // Bring the invitee up to speed with the room's existing history.
    sendChannelHistoryTo(peerId, name);
}

void ServerNode::shutdown()
{
    m_userStopped = true; // intentional leave: stop the auto-reconnect loop
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    if (m_pingTimer)
        m_pingTimer->stop();
    if (m_presenceTimer)
        m_presenceTimer->stop();
    QJsonObject bye = makeMessage("bye");
    sendEncrypted(bye, true);
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
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
    const QString solanaAddress = boundedText(message, "solana", kMaxSolanaAddressChars);
    const QString platform = message.value("platform").toString().left(16);
    const QString version = boundedText(message, "version", kMaxVersionChars);
    if (!senderId.isEmpty())
        rememberPeer(senderId, sender, solanaAddress, platform, version);

    // Host resource telemetry (CPU/RAM/disk) the sender advertised; refresh the
    // peer's cached figures so the Mirror nodes view's bars track live load.
    // rememberPeer just (re)created the peer, so it's safe to look up by id.
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
            if (!channel.isEmpty() && !m_channels.contains(channel)) {
                m_channels.append(channel);
                changed = true;
            }
        }
        if (changed)
            emit channelsChanged(m_channels);
        // Record which repos this peer advertises mirroring, plus the HEAD each
        // mirror currently holds (for the network's mirror-freshness view).
        if (m_peers.contains(senderId)) {
            const QList<MirrorAdvert> mirrors = readMirrors(message);
            m_peers[senderId].mirrorDetails = mirrors;
            m_peers[senderId].mirrors = mirrorNames(mirrors);
            updateRosterAndStatus();
        }
        // A broadcast hello (no "to") is a peer announcing themselves on a
        // fresh connect or reconnect. Existing peers must answer with their own
        // presence so the newcomer rebuilds its roster — otherwise it would not
        // see anyone already in the room until they next spoke. The reply is
        // directed at the newcomer and carries "to"; directed hellos are not
        // answered again, so two peers can't ping-pong hellos forever.
        if (message.value("to").toString().isEmpty()) {
            sendHistoryTo(senderId);
            QJsonArray channels;
            for (const QString &channel : std::as_const(m_channels))
                if (!m_privateChannels.contains(channel))
                    channels.append(channel);
            QJsonObject reply = makeMessage("hello");
            reply.insert("channels", channels);
            writeMirrors(reply, m_mirroredRepos); // so the newcomer sees our mirrors too
            reply.insert("to", senderId);
            sendEncrypted(reply, false);
        }
    } else if (type == "chat") {
        const QString channel = message.value("channel").toString();
        // A private-room message from a room we weren't invited to is ignored,
        // the same honour-model as a direct message addressed to someone else.
        if (message.value("private").toBool() && !m_channels.contains(channel))
            return;
        if (!m_channels.contains(channel))
            m_channels.append(channel);
        storeHistory(message);
        emitChat(message);
    } else if (type == "invite") {
        // Someone added us to a private room. Join it locally (invite-only, so
        // we keep it out of our own hello/channel broadcasts too) and surface it.
        if (message.value("to").toString() == m_nodeId) {
            QString channel = message.value("channel").toString().left(120);
            if (!channel.isEmpty()) {
                if (!channel.startsWith('#'))
                    channel.prepend('#');
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
        if (!m_channels.contains(name)) {
            m_channels.append(name);
            emit channelsChanged(m_channels);
        }
    } else if (type == "mirror-update") {
        const QString repo = message.value("repo").toString().left(160);
        if (!repo.isEmpty())
            emit mirrorUpdated(repo, sender);
    } else if (type == "cove-open") {
        const QString creator = message.value("creator").toString().left(120);
        if (!creator.isEmpty())
            emit coveOpened(creator, message.value("coveId").toString().left(80),
                            message.value("coveName").toString().left(160),
                            message.value("openerKey").toString().left(120),
                            message.value("openerName").toString().left(80),
                            qint64(message.value("coveTs").toDouble()),
                            message.value("sig").toString().left(200));
    } else if (type == "reaction") {
        const QString target = message.value("target").toString();
        const QString emoji = message.value("emoji").toString();
        const QString reactorId = message.value("reactorId").toString();
        const QString reactorName = message.value("reactorName").toString();
        if (message.value("added").toBool())
            m_reactions[target][emoji].insert(reactorId, reactorName);
        else
            m_reactions[target][emoji].remove(reactorId);
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
        // A signed moderation delete. We can't authorise it here (admin status
        // lives server-side); hand it to the UI to verify the signature against
        // senderId and confirm that node is an admin, then apply.
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
            // Never surface replayed private-room history for a room we aren't a
            // member of (mirrors the live "chat" guard above).
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
        // A blanket hello-reply must not leak private rooms: only the members
        // we've explicitly invited (via inviteToChannel) receive their history.
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
    m_messageConversation.insert(out.id, out.conversation);
    m_messageSender.insert(out.id, out.senderId);
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
    m_messageConversation.insert(out.id, out.conversation);
    m_messageSender.insert(out.id, out.senderId);
    emit messageArrived(out);
}

void ServerNode::rememberPeer(const QString &peerId, const QString &name,
                              const QString &solanaAddress, const QString &platform,
                              const QString &version, bool online)
{
    if (peerId.isEmpty() || peerId == m_nodeId)
        return;
    Peer &peer = m_peers[peerId];
    peer.name = name.isEmpty() ? peer.name : name;
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
    // Coalesce: incoming frames (typing/presence/hello/chat) each touch the
    // roster, but the membership rarely actually changes. Emit at most once per
    // short window so the UI status/roster don't thrash on every frame.
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
    // Refresh our own telemetry so the self row's CPU/RAM/disk bars are populated
    // even before the first heartbeat fires (throttled internally to ~10s).
    sampleSystemStats();
    MemberInfo self;
    self.id = m_nodeId;
    self.name = m_userName;
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
        // Drop peers that stopped sending frames (presence/hello/chat) long ago:
        // a disconnect without a "bye" otherwise leaves them stuck online forever.
        if (now - it->lastSeenMs > kPeerStaleMs)
            continue;
        MemberInfo member;
        member.id = it.key();
        member.name = it->name;
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
    emit statusChanged(QString::number(onlineCount) +
                       " peer(s) online · encrypted room " + m_roomName);
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
    if (!channel.isEmpty())
        m_channelHistory[channel].append(message);
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
