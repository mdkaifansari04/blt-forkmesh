#include "OfficeChannelMirror.h"
#include "ServerNode.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

#include <utility>

namespace {

// Rooms this client keeps a receiving socket on. Every office room the account
// can read would otherwise mean a socket (plus its presence beat) per room, and
// the site allows a hundred channels. Rooms past the budget still show their
// startup backlog and still connect when the user sends into them.
constexpr int kMaxLiveRooms = 16;
// Reconnect ramp for a dropped room socket: 2s, 4s, 8s ... capped at 5 minutes.
// The ticket in the socket URL expires in 60s, so every attempt re-fetches room
// access rather than reusing the old endpoint.
constexpr int kReconnectBaseDelayMs = 2000;
constexpr int kReconnectMaxDelayMs = 5 * 60 * 1000;
// Floor between two user-triggered refreshes (opening Chat), so hammering the
// section button cannot turn a user action back into a poll.
constexpr int kMinRefreshIntervalMs = 10000;
constexpr int kMaxChannels = 50;
constexpr int kMaxTextChars = 16000;
constexpr int kMaxDisplayNameChars = 32;
constexpr int kMaxFileNameChars = 180;
constexpr int kMaxMimeChars = 100;
constexpr qsizetype kMaxFileBytes = 8ll * 1024 * 1024;
constexpr int kMaxPendingTextsPerRoom = 32;
// Ids already shown per room. Bounded: an office room retains a few hundred
// messages, and each poll only asks for what landed after the newest stamp
// this client has seen.
constexpr int kSeenIdsPerRoom = 1000;

const QRegularExpression &channelIdPattern()
{
    static const QRegularExpression pattern(QStringLiteral("\\A[0-9a-f]{32}\\z"));
    return pattern;
}

QString boundedText(const QJsonObject &object, const char *key, int maxChars)
{
    return object.value(QLatin1String(key)).toString().left(maxChars);
}

} // namespace

namespace forkmesh::office {

QString conversationForChannel(const QString &channelName)
{
    const QString name = channelName.trimmed().left(kMaxDisplayNameChars);
    if (name.isEmpty())
        return QString();
    return QStringLiteral("#office/") + name;
}

bool isOfficeConversation(const QString &conversation)
{
    return conversation.startsWith(QStringLiteral("#office/"));
}

QByteArray channelListProof(const QString &account, const QString &ts)
{
    return QStringLiteral("forkmesh-chat-channels-v1\n%1\n%2")
        .arg(account, ts)
        .toUtf8();
}

QByteArray channelAccessProof(const QString &account, const QString &channelId,
                              const QString &ts)
{
    return QStringLiteral("forkmesh-chat-channel-access-v1\n%1\n%2\n%3")
        .arg(account, channelId, ts)
        .toUtf8();
}

QByteArray channelHistoryProof(const QString &account, const QString &channelId,
                               const QString &ts)
{
    return QStringLiteral("forkmesh-chat-channel-history-v1\n%1\n%2\n%3")
        .arg(account, channelId, ts)
        .toUtf8();
}

bool chatMessageFromPlain(const QJsonObject &plain, const QString &conversation,
                          const QString &selfId, ChatMessage *out)
{
    if (!out || conversation.isEmpty())
        return false;
    if (plain.value(QStringLiteral("type")).toString() != QLatin1String("chat"))
        return false;
    const QString id = plain.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id.size() > 64)
        return false;
    // The office is an untrusted publisher like any other room member: every
    // field is bounded here, exactly as the mesh room's frames are.
    if (plain.value(QStringLiteral("text")).toString().size() > kMaxTextChars)
        return false;

    ChatMessage message;
    message.id = id;
    message.conversation = conversation;
    message.senderId =
        plain.value(QStringLiteral("senderId")).toString().left(64);
    message.senderName = boundedText(plain, "sender", kMaxDisplayNameChars);
    if (message.senderName.isEmpty())
        message.senderName = QStringLiteral("Office visitor");
    message.text = boundedText(plain, "text", kMaxTextChars);
    message.timestampMs = qint64(plain.value(QStringLiteral("ts")).toDouble());
    message.fileName = boundedText(plain, "fileName", kMaxFileNameChars);
    message.fileMime = boundedText(plain, "fileMime", kMaxMimeChars);
    if (!message.fileName.isEmpty()) {
        const QByteArray decoded = QByteArray::fromBase64(
            plain.value(QStringLiteral("file")).toString().toLatin1(),
            QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.isEmpty() || decoded.size() > kMaxFileBytes) {
            // Keep the message, drop an unreadable/oversized attachment: the
            // row still shows who said what.
            message.fileName.clear();
            message.fileMime.clear();
        } else {
            message.fileData = decoded;
        }
    }
    message.self = !selfId.isEmpty() && message.senderId == selfId;
    if (message.text.isEmpty() && !message.hasFile())
        return false;
    *out = message;
    return true;
}

} // namespace forkmesh::office

OfficeChannelMirror::OfficeChannelMirror(QNetworkAccessManager *network,
                                         QObject *parent)
    : QObject(parent), m_network(network)
{
}

void OfficeChannelMirror::setSigner(std::function<QString(const QByteArray &)> signer)
{
    m_signer = std::move(signer);
}

void OfficeChannelMirror::setIdentity(const QString &account, const QString &selfId,
                                      const QString &displayName)
{
    const QString normalized = account.trimmed().toLower();
    const bool switched = normalized != m_account;
    if (switched) {
        // A different account sees a different set of office rooms.
        for (auto it = m_rooms.begin(); it != m_rooms.end(); ++it) {
            failPending(it.value(), QStringLiteral("chat account changed"));
            discardSender(it.value());
        }
        m_rooms.clear();
        m_socketBudgetNoted = false;
        if (!m_conversations.isEmpty()) {
            m_conversations.clear();
            emit conversationsChanged(m_conversations);
        }
    }
    m_account = normalized;
    m_selfId = selfId;
    m_displayName = displayName.trimmed().left(kMaxDisplayNameChars);
    // Nothing re-lists on a timer any more, so the account switch that just
    // emptied the sidebar has to fill it again itself.
    if (switched && m_active && ready())
        fetchChannels();
}

void OfficeChannelMirror::setConnectionAuthorizer(
    std::function<bool(const QUrl &)> authorizer)
{
    m_connectionAuthorizer = std::move(authorizer);
}

void OfficeChannelMirror::setApiBase(const QUrl &apiBase)
{
    QUrl base = apiBase;
    base.setPath(QString());
    base.setQuery(QString());
    base.setFragment(QString());
    m_apiBase = base;
}

bool OfficeChannelMirror::ready() const
{
    return m_network && m_signer && !m_account.isEmpty() &&
           m_apiBase.isValid() && !m_apiBase.host().isEmpty();
}

void OfficeChannelMirror::start()
{
    // Called from the 60s account heartbeat: one pass per run, never a beat.
    if (!ready() || m_active)
        return;
    m_active = true;
    fetchChannels();
}

void OfficeChannelMirror::stop()
{
    m_active = false;
    for (auto it = m_rooms.begin(); it != m_rooms.end(); ++it)
        discardSender(it.value());
    m_rooms.clear();
    if (m_conversations.isEmpty())
        return;
    m_conversations.clear();
    emit conversationsChanged(m_conversations);
}

bool OfficeChannelMirror::isActive() const
{
    return m_active;
}

void OfficeChannelMirror::refresh()
{
    if (!ready() || !m_active)
        return;
    // Clicking Chat is a user action, but clicking it four times a second is
    // not four requests' worth of intent.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastListMs > 0 && now - m_lastListMs < kMinRefreshIntervalMs)
        return;
    fetchChannels();
}

QStringList OfficeChannelMirror::conversations() const
{
    return m_conversations;
}

QStringList OfficeChannelMirror::membersForConversation(
    const QString &conversation) const
{
    for (auto it = m_rooms.constBegin(); it != m_rooms.constEnd(); ++it) {
        if (it->conversation == conversation)
            return it->members;
    }
    return {};
}

bool OfficeChannelMirror::isPrivateConversation(
    const QString &conversation) const
{
    for (auto it = m_rooms.constBegin(); it != m_rooms.constEnd(); ++it) {
        if (it->conversation == conversation)
            return it->privateRoom;
    }
    return false;
}

bool OfficeChannelMirror::canSend(const QString &conversation) const
{
    if (!ready() || !forkmesh::office::isOfficeConversation(conversation))
        return false;
    for (auto it = m_rooms.constBegin(); it != m_rooms.constEnd(); ++it)
        if (it->conversation == conversation)
            return true;
    return false;
}

bool OfficeChannelMirror::sendMessage(const QString &conversation,
                                      const QString &text)
{
    const QString message = text.trimmed().left(kMaxTextChars);
    if (message.isEmpty() || !canSend(conversation))
        return false;
    for (auto it = m_rooms.begin(); it != m_rooms.end(); ++it) {
        Room &room = it.value();
        if (room.conversation != conversation)
            continue;
        if (room.pendingTexts.size() >= kMaxPendingTextsPerRoom) {
            emit sendActivity(QStringLiteral("Office chat: send queue for %1 is full.")
                                  .arg(conversation));
            return false;
        }
        room.pendingTexts.enqueue(message);
        emit sendActivity(QStringLiteral("Office chat: queued a message for %1.")
                              .arg(conversation));
        if (room.sender && room.senderConnected)
            flushPending(room);
        else if (!room.sender && !room.accessFetching)
            fetchRoomAccess(room.id);
        return true;
    }
    return false;
}

QUrl OfficeChannelMirror::signedUrl(const QString &path, const QString &ts,
                                    const QByteArray &canonical) const
{
    QUrl url = m_apiBase;
    url.setPath(path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("node"), m_account);
    query.addQueryItem(QStringLiteral("ts"), ts);
    query.addQueryItem(QStringLiteral("sig"), m_signer(canonical));
    url.setQuery(query);
    return url;
}

void OfficeChannelMirror::fetchChannels()
{
    if (m_listing)
        return;
    m_lastListMs = QDateTime::currentMSecsSinceEpoch();
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QUrl url = signedUrl(QStringLiteral("/api/chat/channels"), ts,
                               forkmesh::office::channelListProof(m_account, ts));
    m_listing = true;
    QNetworkReply *reply = m_network->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_listing = false;
        const QByteArray body = reply->readAll();
        const bool failed = reply->error() != QNetworkReply::NoError;
        reply->deleteLater();
        if (failed)
            return;
        const QJsonArray channels =
            QJsonDocument::fromJson(body).object()
                .value(QStringLiteral("channels")).toArray();
        QStringList conversations;
        QSet<QString> live;
        for (const QJsonValue &value : channels) {
            if (conversations.size() >= kMaxChannels)
                break;
            const QJsonObject channel = value.toObject();
            const QString id = channel.value(QStringLiteral("id")).toString();
            if (!channelIdPattern().match(id).hasMatch())
                continue;
            const QString conversation = forkmesh::office::conversationForChannel(
                channel.value(QStringLiteral("name")).toString());
            if (conversation.isEmpty() || conversations.contains(conversation))
                continue;
            Room &room = m_rooms[id];
            room.id = id;
            room.name = channel.value(QStringLiteral("name")).toString()
                            .left(kMaxDisplayNameChars);
            room.conversation = conversation;
            const bool privateRoom =
                channel.value(QStringLiteral("visibility")).toString() ==
                QStringLiteral("private");
            QStringList members;
            if (privateRoom) {
                QSet<QString> seenMembers;
                for (const QJsonValue &memberValue :
                     channel.value(QStringLiteral("members")).toArray()) {
                    const QString member =
                        memberValue.toString().trimmed().toLower()
                            .left(kMaxDisplayNameChars);
                    if (!member.isEmpty() && !seenMembers.contains(member)) {
                        seenMembers.insert(member);
                        members.append(member);
                    }
                }
            }
            const bool membersChanged =
                room.privateRoom != privateRoom || room.members != members;
            room.privateRoom = privateRoom;
            room.members = members;
            room.order = int(conversations.size());
            room.wantsSocket = room.order < kMaxLiveRooms;
            conversations.append(conversation);
            live.insert(id);
            if (membersChanged)
                emit roomMembersChanged(conversation, members);
        }
        if (conversations.size() > kMaxLiveRooms && !m_socketBudgetNoted) {
            m_socketBudgetNoted = true;
            emit sendActivity(
                QStringLiteral("Office chat: watching the first %1 of %2 rooms "
                               "live; the rest load when you open or send to "
                               "them.")
                    .arg(kMaxLiveRooms)
                    .arg(conversations.size()));
        }
        // Rooms the account can no longer read (removed from a private channel)
        // stop being mirrored; their already-shown messages stay in the view.
        const QStringList known = m_rooms.keys();
        for (const QString &id : known) {
            if (!live.contains(id)) {
                failPending(m_rooms[id],
                            QStringLiteral("office room is no longer available"));
                discardSender(m_rooms[id]);
                m_rooms.remove(id);
            }
        }
        const bool changed = conversations != m_conversations;
        if (changed) {
            m_conversations = conversations;
            emit conversationsChanged(m_conversations);
        }
        // The pass that launched this list request could not yet know the room
        // ids: this is where each room's one backlog fetch starts, and where a
        // room whose socket is down (or never opened) gets it back.
        for (const QString &id : std::as_const(live)) {
            fetchHistory(id);
            ensureReceiver(id);
        }
    });
}

void OfficeChannelMirror::ensureReceiver(const QString &channelId)
{
    if (!m_active || !m_rooms.contains(channelId))
        return;
    Room &room = m_rooms[channelId];
    if (!room.wantsSocket || room.sender || room.accessFetching ||
        room.reconnectPending)
        return;
    fetchRoomAccess(channelId);
}

void OfficeChannelMirror::scheduleReconnect(const QString &channelId)
{
    if (!m_active || !m_rooms.contains(channelId))
        return;
    Room &room = m_rooms[channelId];
    if (room.reconnectPending || room.sender || room.accessFetching)
        return;
    if (!room.wantsSocket && room.pendingTexts.isEmpty())
        return; // an out-of-budget room only connects to send
    const int shift = qMin(room.reconnectAttempts, 8);
    ++room.reconnectAttempts;
    int delay = int(qMin<qint64>(qint64(kReconnectBaseDelayMs) << shift,
                                 kReconnectMaxDelayMs));
    // Jitter so a fleet of desktops does not re-ticket in lockstep after a
    // relay deploy drops every room socket at once.
    delay += int(QRandomGenerator::global()->bounded(delay / 4 + 250));
    room.reconnectPending = true;
    QTimer::singleShot(delay, this, [this, channelId] {
        if (!m_rooms.contains(channelId))
            return;
        Room &liveRoom = m_rooms[channelId];
        liveRoom.reconnectPending = false;
        if (!m_active || liveRoom.sender)
            return;
        if (!liveRoom.wantsSocket && liveRoom.pendingTexts.isEmpty())
            return; // an out-of-budget room only connects to send
        fetchRoomAccess(channelId);
    });
}

void OfficeChannelMirror::fetchRoomAccess(const QString &channelId)
{
    if (!m_rooms.contains(channelId))
        return;
    Room &room = m_rooms[channelId];
    if (room.accessFetching || room.sender)
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QUrl url = signedUrl(
        QStringLiteral("/api/chat/channels/%1/room-access").arg(channelId), ts,
        forkmesh::office::channelAccessProof(m_account, channelId, ts));
    room.accessFetching = true;
    // Only narrate the ticket the user is waiting on. Every mirrored room now
    // takes one of these on connect, and that is background work.
    if (!room.pendingTexts.isEmpty())
        emit sendActivity(QStringLiteral("Office chat: requesting encrypted room access "
                                         "for %1.")
                              .arg(room.conversation));
    QNetworkReply *reply = m_network->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, channelId]() {
                const QByteArray body = reply->readAll();
                const bool failed = reply->error() != QNetworkReply::NoError;
                const QString networkError = reply->errorString();
                reply->deleteLater();
                if (!m_rooms.contains(channelId))
                    return;
                Room &room = m_rooms[channelId];
                room.accessFetching = false;
                const QJsonObject payload =
                    QJsonDocument::fromJson(body).object();
                if (failed || payload.value(QStringLiteral("room")).toString().isEmpty()) {
                    failPending(
                        room,
                        failed ? networkError
                               : payload.value(QStringLiteral("error"))
                                     .toString(QStringLiteral("room access denied")));
                    // A refused ticket is usually transient (relay redeploy,
                    // quota). Nothing polls this room any more, so the retry is
                    // what keeps its messages arriving at all.
                    scheduleReconnect(channelId);
                    return;
                }
                openSender(channelId, payload);
            });
}

void OfficeChannelMirror::openSender(const QString &channelId,
                                     const QJsonObject &payload)
{
    if (!m_rooms.contains(channelId))
        return;
    Room &room = m_rooms[channelId];
    const QString roomName = payload.value(QStringLiteral("room")).toString();
    const QString passphrase =
        payload.value(QStringLiteral("passphrase")).toString();
    QUrl endpoint =
        m_apiBase.resolved(QUrl(payload.value(QStringLiteral("webSocketUrl")).toString()));
    if (endpoint.scheme() == QLatin1String("https"))
        endpoint.setScheme(QStringLiteral("wss"));
    else if (endpoint.scheme() == QLatin1String("http"))
        endpoint.setScheme(QStringLiteral("ws"));
    if (roomName.isEmpty() || passphrase.isEmpty() || !endpoint.isValid() ||
        (endpoint.scheme() != QLatin1String("ws") &&
         endpoint.scheme() != QLatin1String("wss"))) {
        failPending(room, QStringLiteral("relay returned invalid room access"));
        scheduleReconnect(channelId);
        return;
    }

    discardSender(room);
    auto *sender = new ServerNode(
        m_displayName.isEmpty() ? m_account : m_displayName, m_account, m_account,
        m_selfId, endpoint, roomName, QString(), passphrase, this);
    sender->setAccountKind(QStringLiteral("user"));
    sender->addChannel(QLatin1Char('#') + room.name);
    if (m_connectionAuthorizer)
        sender->setConnectionAuthorizer(m_connectionAuthorizer);
    room.sender = sender;
    room.senderConnected = false;

    connect(sender, &ChatBackend::messageArrived, this,
            [this, channelId, sender](ChatMessage message) {
                if (!m_rooms.contains(channelId) ||
                    m_rooms[channelId].sender != sender)
                    return;
                Room &liveRoom = m_rooms[channelId];
                message.conversation = liveRoom.conversation;
                if (liveRoom.seenIds.contains(message.id))
                    return;
                rememberSeen(liveRoom, message.id);
                emit messageArrived(message);
            });
    connect(sender, &ServerNode::connectionChanged, this,
            [this, channelId, sender](bool connected) {
                if (!m_rooms.contains(channelId) ||
                    m_rooms[channelId].sender != sender)
                    return;
                Room &liveRoom = m_rooms[channelId];
                liveRoom.senderConnected = connected;
                if (connected) {
                    liveRoom.reconnectAttempts = 0;
                    if (!liveRoom.pendingTexts.isEmpty())
                        emit sendActivity(
                            QStringLiteral("Office chat: encrypted socket ready "
                                           "for %1.")
                                .arg(liveRoom.conversation));
                    flushPending(liveRoom);
                    // Catch-up, not a poll: one history read per (re)connect
                    // drains whatever the room retained while this socket was
                    // down (and re-seeds the key if the room was re-keyed).
                    fetchHistory(channelId);
                    return;
                }
                liveRoom.sender = nullptr;
                sender->shutdown();
                sender->deleteLater();
                scheduleReconnect(channelId);
            });
    connect(sender, &ChatBackend::systemMessage, this,
            [this, channelId](const QString &message) {
                if (message.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
                    message.contains(QStringLiteral("failed"), Qt::CaseInsensitive) ||
                    message.contains(QStringLiteral("blocked"), Qt::CaseInsensitive))
                    emit sendActivity(QStringLiteral("Office chat: %1").arg(message));
            });

    if (!sender->start()) {
        room.sender = nullptr;
        sender->deleteLater();
        failPending(room, QStringLiteral("could not start encrypted room socket"));
        scheduleReconnect(channelId);
        return;
    }

    // A ticket or edge failure must not leave queued text stuck forever, nor
    // leave the room without a live path. A later attempt gets a fresh ticket
    // instead of reusing a stale reconnect URL.
    QTimer::singleShot(20000, this, [this, channelId, sender] {
        if (!m_rooms.contains(channelId))
            return;
        Room &liveRoom = m_rooms[channelId];
        if (liveRoom.sender != sender || liveRoom.senderConnected)
            return;
        liveRoom.sender = nullptr;
        sender->shutdown();
        sender->deleteLater();
        failPending(liveRoom, QStringLiteral("encrypted room connection timed out"));
        scheduleReconnect(channelId);
    });
}

void OfficeChannelMirror::flushPending(Room &room)
{
    if (!room.sender || !room.senderConnected)
        return;
    while (!room.pendingTexts.isEmpty()) {
        const QString text = room.pendingTexts.dequeue();
        room.sender->sendChat(QLatin1Char('#') + room.name, text);
        emit sendActivity(QStringLiteral("Office chat: sent an encrypted message "
                                         "to %1.")
                              .arg(room.conversation));
    }
}

void OfficeChannelMirror::failPending(Room &room, const QString &reason)
{
    if (room.pendingTexts.isEmpty())
        return;
    const QString failure =
        reason.trimmed().isEmpty() ? QStringLiteral("send failed") : reason.trimmed();
    while (!room.pendingTexts.isEmpty())
        emit messageSendFailed(room.conversation, room.pendingTexts.dequeue(), failure);
    emit sendActivity(QStringLiteral("Office chat: send to %1 failed (%2).")
                          .arg(room.conversation, failure));
}

void OfficeChannelMirror::rememberSeen(Room &room, const QString &messageId)
{
    if (messageId.isEmpty() || room.seenIds.contains(messageId))
        return;
    room.seenIds.insert(messageId);
    room.seenOrder.enqueue(messageId);
    while (room.seenOrder.size() > kSeenIdsPerRoom)
        room.seenIds.remove(room.seenOrder.dequeue());
}

void OfficeChannelMirror::discardSender(Room &room)
{
    ServerNode *sender = room.sender.data();
    room.sender = nullptr;
    room.senderConnected = false;
    if (!sender)
        return;
    sender->shutdown();
    sender->deleteLater();
}

void OfficeChannelMirror::fetchHistory(const QString &channelId)
{
    if (!m_rooms.contains(channelId))
        return;
    Room &room = m_rooms[channelId];
    if (room.fetching)
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    QUrl url = signedUrl(
        QStringLiteral("/api/chat/channels/%1/history").arg(channelId), ts,
        forkmesh::office::channelHistoryProof(m_account, channelId, ts));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("since"), QString::number(room.lastTs));
    url.setQuery(query);
    room.fetching = true;
    QNetworkReply *reply = m_network->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, channelId]() {
        const QByteArray body = reply->readAll();
        const bool failed = reply->error() != QNetworkReply::NoError;
        reply->deleteLater();
        if (!m_rooms.contains(channelId))
            return;
        m_rooms[channelId].fetching = false;
        if (failed)
            return;
        applyHistory(m_rooms[channelId], QJsonDocument::fromJson(body).object());
    });
}

void OfficeChannelMirror::applyHistory(Room &room, const QJsonObject &payload)
{
    const QString roomName = payload.value(QStringLiteral("room")).toString();
    const QString passphrase =
        payload.value(QStringLiteral("passphrase")).toString();
    if (roomName.isEmpty() || passphrase.isEmpty())
        return;
    if (roomName != room.room || !room.crypto.isValid()) {
        room.room = roomName;
        room.crypto = RoomCrypto(roomName, passphrase);
        if (!room.crypto.isValid())
            return;
    }

    const QJsonArray messages =
        payload.value(QStringLiteral("messages")).toArray();
    for (const QJsonValue &value : messages) {
        const QJsonObject entry = value.toObject();
        const qint64 ts = qint64(entry.value(QStringLiteral("ts")).toDouble());
        room.lastTs = qMax(room.lastTs, ts);
        const QJsonObject envelope =
            QJsonDocument::fromJson(
                entry.value(QStringLiteral("body")).toString().toUtf8())
                .object();
        if (envelope.isEmpty())
            continue;
        const QJsonObject plain = room.crypto.decryptObject(envelope);
        ChatMessage message;
        if (!forkmesh::office::chatMessageFromPlain(plain, room.conversation,
                                                    m_selfId, &message))
            continue;
        if (room.seenIds.contains(message.id))
            continue;
        rememberSeen(room, message.id);
        emit messageArrived(message);
    }
}
