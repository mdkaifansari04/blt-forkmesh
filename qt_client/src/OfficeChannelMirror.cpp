#include "OfficeChannelMirror.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

namespace {

// Cadence of the read-only office poll. Office rooms are conversational, not
// operational, so this is deliberately slow next to the mesh room's live
// socket: it costs one small request per room and rides the same network
// backoff manager as every other relay call.
constexpr int kPollIntervalMs = 30000;
constexpr int kMaxChannels = 50;
constexpr int kMaxTextChars = 16000;
constexpr int kMaxDisplayNameChars = 32;
constexpr int kMaxFileNameChars = 180;
constexpr int kMaxMimeChars = 100;
constexpr qsizetype kMaxFileBytes = 8ll * 1024 * 1024;
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
    m_timer = new QTimer(this);
    m_timer->setInterval(kPollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &OfficeChannelMirror::poll);
}

void OfficeChannelMirror::setSigner(std::function<QString(const QByteArray &)> signer)
{
    m_signer = std::move(signer);
}

void OfficeChannelMirror::setIdentity(const QString &account, const QString &selfId)
{
    const QString normalized = account.trimmed().toLower();
    if (normalized != m_account && !m_conversations.isEmpty()) {
        // A different account sees a different set of office rooms.
        m_rooms.clear();
        m_conversations.clear();
        emit conversationsChanged(m_conversations);
    }
    m_account = normalized;
    m_selfId = selfId;
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
    if (!ready() || m_timer->isActive())
        return;
    m_timer->start();
    poll();
}

void OfficeChannelMirror::stop()
{
    m_timer->stop();
    m_rooms.clear();
    if (m_conversations.isEmpty())
        return;
    m_conversations.clear();
    emit conversationsChanged(m_conversations);
}

bool OfficeChannelMirror::isActive() const
{
    return m_timer && m_timer->isActive();
}

QStringList OfficeChannelMirror::conversations() const
{
    return m_conversations;
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

void OfficeChannelMirror::poll()
{
    if (!ready())
        return;
    fetchChannels();
    const QStringList ids = m_rooms.keys();
    for (const QString &id : ids)
        fetchHistory(id);
}

void OfficeChannelMirror::fetchChannels()
{
    if (m_listing)
        return;
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
            conversations.append(conversation);
            live.insert(id);
        }
        // Rooms the account can no longer read (removed from a private channel)
        // stop being polled; their already-shown messages stay in the view.
        const QStringList known = m_rooms.keys();
        for (const QString &id : known)
            if (!live.contains(id))
                m_rooms.remove(id);
        if (conversations == m_conversations)
            return;
        m_conversations = conversations;
        emit conversationsChanged(m_conversations);
    });
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
        room.seenIds.insert(message.id);
        room.seenOrder.enqueue(message.id);
        while (room.seenOrder.size() > kSeenIdsPerRoom)
            room.seenIds.remove(room.seenOrder.dequeue());
        emit messageArrived(message);
    }
}
