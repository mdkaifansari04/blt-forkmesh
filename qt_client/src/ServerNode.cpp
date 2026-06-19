#include "ServerNode.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSettings>
#include <QSslSocket>
#include <QTimer>
#include <QUuid>

namespace {

constexpr int kSeenCacheLimit = 4096;
// How many past members to remember per room so a rejoin can show them as
// offline. Bounded so the stored roster can't grow without limit.
constexpr int kKnownPeerLimit = 256;
const QString kKnownRosterGroup = QStringLiteral("mainnode/knownRoster");
constexpr quint64 kMaxWsPayload = 96ull * 1024 * 1024;
constexpr int kMaxDisplayNameChars = 32;
constexpr int kMaxBchAddressChars = 160;
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
           message.value("bch").toString().size() <= kMaxBchAddressChars &&
           message.value("fileName").toString().size() <= kMaxFileNameChars &&
           message.value("fileMime").toString().size() <= kMaxMimeChars &&
           message.value("file").toString().size() <= kMaxBase64FileChars &&
           message.value("png").toString().size() <= 4 * kMaxAvatarBytes / 3 + 8;
}

} // namespace

ServerNode::ServerNode(const QString &userName, const QUrl &serverUrl,
                       const QString &roomName, const QString &passphrase,
                       const QString &bchAddress,
                       QObject *parent)
    : ChatBackend(parent),
      m_userName(userName),
      m_url(serverUrl),
      m_roomName(roomName.trimmed()),
      m_bchAddress(bchAddress.trimmed().left(kMaxBchAddressChars)),
      m_platform(currentPlatform()),
      m_nodeId(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      m_crypto(m_roomName, passphrase)
{
    const QByteArray material = m_url.toString().toUtf8() + '\n' +
                                m_roomName.toUtf8() + '\n' + passphrase.toUtf8();
    m_rosterStorageKey =
        QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256)
                                .toHex());
    loadKnownPeers();
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

    emit statusChanged("Connecting to " + m_url.host() + "...");
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
            emit statusChanged("Reconnecting to " + m_url.host() + "...");
            openConnection();
        });
    }

    // Exponential backoff: ~1, 2, 4, 8, 16, 30 (capped) seconds, plus jitter so
    // many nodes don't reconnect in lockstep.
    const int base = 1000;
    const int cap = 30000;
    int delay = qMin(cap, base * (1 << qMin(m_reconnectAttempt, 5)));
    delay += int(QRandomGenerator::global()->bounded(750));
    ++m_reconnectAttempt;
    emit statusChanged(
        QString::fromUtf8("Disconnected \xE2\x80\x94 reconnecting in %1s\xE2\x80\xA6")
            .arg((delay + 999) / 1000));
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
        m_wsReady = false;
        for (auto it = m_peers.begin(); it != m_peers.end(); ++it)
            it->online = false;
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
            emit fatalError("Mainnode did not accept the WebSocket upgrade.");
            return;
        }
        m_wsReady = true;
        m_reconnectAttempt = 0; // healthy link: reset backoff
        if (m_pingTimer)
            m_pingTimer->start();
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
    if (!m_bchAddress.isEmpty())
        message.insert("bch", m_bchAddress);
    if (!m_platform.isEmpty())
        message.insert("platform", m_platform);
    return message;
}

void ServerNode::sendEncrypted(const QJsonObject &plain, bool showActivity)
{
    QJsonObject envelope = m_crypto.encryptObject(plain);
    if (envelope.isEmpty()) {
        emit systemMessage("Mainnode encryption failed; message was not sent.");
        return;
    }
    sendTextFrame(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
    if (showActivity)
        emit systemMessage("Network: sent encrypted " + plain.value("type").toString() +
                           " through mainnode room " + m_roomName + ".");
}

void ServerNode::sendHello()
{
    QJsonArray channels;
    for (const QString &channel : std::as_const(m_channels))
        channels.append(channel);
    QJsonArray mirrors;
    for (const QString &repo : std::as_const(m_mirroredRepos))
        mirrors.append(repo);
    QJsonObject hello = makeMessage("hello");
    hello.insert("channels", channels);
    hello.insert("mirrors", mirrors);
    sendEncrypted(hello, true);
}

void ServerNode::setMirroredRepos(const QStringList &ownerNames)
{
    if (m_mirroredRepos == ownerNames)
        return;
    m_mirroredRepos = ownerNames;
    if (m_wsReady)
        sendHello(); // re-advertise so peers see the updated mirror set
}

void ServerNode::sendChat(const QString &channel, const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    QJsonObject message = makeMessage("chat");
    message.insert("channel", channel);
    message.insert("text", text.left(kMaxTextChars));
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
    emit systemMessage("Removed stale member from the remembered roster.");
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

void ServerNode::shutdown()
{
    m_userStopped = true; // intentional leave: stop the auto-reconnect loop
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    if (m_pingTimer)
        m_pingTimer->stop();
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
    const QString bchAddress = boundedText(message, "bch", kMaxBchAddressChars);
    const QString platform = message.value("platform").toString().left(16);
    if (!senderId.isEmpty())
        rememberPeer(senderId, sender, bchAddress, platform);

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
        // Record which repos this peer advertises mirroring.
        if (m_peers.contains(senderId)) {
            QStringList mirrors;
            for (const auto &value : message.value("mirrors").toArray()) {
                const QString repo = value.toString().left(160);
                if (!repo.isEmpty() && mirrors.size() < 500)
                    mirrors.append(repo);
            }
            m_peers[senderId].mirrors = mirrors;
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
                channels.append(channel);
            QJsonObject reply = makeMessage("hello");
            reply.insert("channels", channels);
            reply.insert("to", senderId);
            sendEncrypted(reply, false);
        }
    } else if (type == "chat") {
        if (!m_channels.contains(message.value("channel").toString()))
            m_channels.append(message.value("channel").toString());
        storeHistory(message);
        emitChat(message);
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
            if (markSeen(entry.value("id").toString())) {
                storeHistory(entry);
                emitChat(entry);
            }
        }
    } else if (type == "bye") {
        if (m_peers.contains(senderId)) {
            m_peers[senderId].online = false;
            m_peers[senderId].lastSeenMs = QDateTime::currentMSecsSinceEpoch();
            persistKnownPeers();
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
    for (const QList<QJsonObject> &history : std::as_const(m_channelHistory)) {
        for (const QJsonObject &entry : history)
            entries.append(entry);
    }
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
                              const QString &bchAddress, const QString &platform,
                              bool online)
{
    if (peerId.isEmpty() || peerId == m_nodeId)
        return;
    const bool isNew = !m_peers.contains(peerId);
    Peer &peer = m_peers[peerId];
    const QString previousName = peer.name;
    const QString previousBch = peer.bchAddress;
    peer.name = name.isEmpty() ? peer.name : name;
    if (!bchAddress.trimmed().isEmpty())
        peer.bchAddress = bchAddress.trimmed().left(kMaxBchAddressChars);
    if (!platform.isEmpty())
        peer.platform = platform;
    peer.online = online;
    peer.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
    // Only touch persistent storage when the durable identity changes, not on
    // every incoming frame.
    if (isNew || peer.name != previousName || peer.bchAddress != previousBch)
        persistKnownPeers();
    updateRosterAndStatus();
}

void ServerNode::updateRosterAndStatus()
{
    MemberInfo self{m_nodeId,    m_userName, QString(), true, m_wsReady, m_bchAddress,
                    QString(),   m_platform, m_mirroredRepos};
    QList<MemberInfo> members{self};
    int onlineCount = 0;
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        MemberInfo member;
        member.id = it.key();
        member.name = it->name;
        member.note = it->online ? QString() : QStringLiteral("(offline)");
        member.online = it->online;
        member.bchAddress = it->bchAddress;
        member.platform = it->platform;
        member.mirrors = it->mirrors;
        members.append(member);
        if (it->online)
            ++onlineCount;
    }
    emit rosterChanged(members);
    emit statusChanged(QString::number(onlineCount) + " of " +
                       QString::number(m_peers.size()) +
                       " known peer(s) online · encrypted room " + m_roomName);
}

void ServerNode::loadKnownPeers()
{
    QSettings settings;
    const QByteArray stored =
        settings.value(kKnownRosterGroup + "/" + m_rosterStorageKey).toByteArray();
    const QJsonDocument doc = QJsonDocument::fromJson(stored);
    if (!doc.isArray())
        return;
    for (const auto &value : doc.array()) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value("id").toString();
        if (id.isEmpty() || id == m_nodeId)
            continue;
        Peer &peer = m_peers[id];
        peer.name = entry.value("name").toString().left(kMaxDisplayNameChars);
        peer.bchAddress = entry.value("bch").toString().left(kMaxBchAddressChars);
        peer.lastSeenMs = qint64(entry.value("lastSeen").toDouble());
        peer.online = false; // Recalled members start offline until they speak.
    }
}

void ServerNode::persistKnownPeers() const
{
    // Keep the most recently seen members within the cap.
    QList<QPair<QString, Peer>> ordered;
    ordered.reserve(m_peers.size());
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it)
        ordered.append({it.key(), it.value()});
    std::sort(ordered.begin(), ordered.end(),
              [](const auto &a, const auto &b) {
                  return a.second.lastSeenMs > b.second.lastSeenMs;
              });
    if (ordered.size() > kKnownPeerLimit)
        ordered.erase(ordered.begin() + kKnownPeerLimit, ordered.end());

    QJsonArray entries;
    for (const auto &pair : std::as_const(ordered)) {
        entries.append(QJsonObject{{"id", pair.first},
                                   {"name", pair.second.name},
                                   {"bch", pair.second.bchAddress},
                                   {"lastSeen", double(pair.second.lastSeenMs)}});
    }
    QSettings settings;
    settings.setValue(kKnownRosterGroup + "/" + m_rosterStorageKey,
                      QJsonDocument(entries).toJson(QJsonDocument::Compact));
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
