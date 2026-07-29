#pragma once

#include "ChatBackend.h"
#include "RoomCrypto.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QTimer;
class ServerNode;

// Desktop view of the chats people hold in the World's virtual office.
//
// The office opens two kinds of room. Its #general room IS the shared mainnode
// room this client already joins, so that conversation has always landed in
// #general here. Its other rooms are the site's chat channels: separate
// encrypted rooms, each with its own key and a 60-second WebSocket admission
// ticket issued to a browser session. The desktop authenticates with the
// account's Ed25519 key and holds no session, so those office conversations
// were invisible in the app (adhoc #412).
//
// This bridge lists the channels the account may read, polls each room's
// retained (still-encrypted) backlog, and opens an on-demand ticketed encrypted
// WebSocket when the desktop sends. All HTTP/socket work remains asynchronous.
namespace forkmesh::office {

// Conversation key for an office room in the desktop sidebar. Prefixed so an
// office room can never silently merge with a same-named mesh room (a mesh
// "#random" and an office "#random" are different rooms with different keys).
QString conversationForChannel(const QString &channelName);
bool isOfficeConversation(const QString &conversation);

// Canonical strings the account's identity key signs for the channel endpoints.
// These must stay byte-identical to the corresponding constants in the Worker.
QByteArray channelListProof(const QString &account, const QString &ts);
QByteArray channelAccessProof(const QString &account, const QString &channelId,
                              const QString &ts);
QByteArray channelHistoryProof(const QString &account, const QString &channelId,
                               const QString &ts);

// Convert one decrypted office frame into a desktop chat message. Returns false
// for anything that isn't a displayable chat frame (presence/typing/oversized).
bool chatMessageFromPlain(const QJsonObject &plain, const QString &conversation,
                          const QString &selfId, ChatMessage *out);

} // namespace forkmesh::office

class OfficeChannelMirror : public QObject
{
    Q_OBJECT
public:
    explicit OfficeChannelMirror(QNetworkAccessManager *network,
                                 QObject *parent = nullptr);

    // Signs a canonical string with this node's account identity key, in the
    // same base64url form the relay's signed endpoints expect.
    void setSigner(std::function<QString(const QByteArray &)> signer);
    // `account` is the registered account name the relay knows the key by;
    // `selfId` is this client's chat id, used to mark our own messages.
    void setIdentity(const QString &account, const QString &selfId,
                     const QString &displayName = QString());
    void setConnectionAuthorizer(std::function<bool(const QUrl &)> authorizer);
    // Any absolute https:// (or http:// for a local relay) URL on the relay;
    // only its scheme/authority is used.
    void setApiBase(const QUrl &apiBase);

    // Idempotent: start()/stop() may be called on every heartbeat.
    void start();
    void stop();
    bool isActive() const;

    QStringList conversations() const;
    QStringList membersForConversation(const QString &conversation) const;
    bool isPrivateConversation(const QString &conversation) const;
    bool canSend(const QString &conversation) const;
    // Queues a text send without blocking the GUI. Returns false only when the
    // conversation/identity is not currently eligible for office chat.
    bool sendMessage(const QString &conversation, const QString &text);

signals:
    void conversationsChanged(const QStringList &conversations);
    void roomMembersChanged(const QString &conversation,
                            const QStringList &members);
    void messageArrived(const ChatMessage &message);
    void sendActivity(const QString &text);
    void messageSendFailed(const QString &conversation, const QString &text,
                           const QString &reason);

private:
    struct Room {
        QString id;
        QString name;
        QString conversation;
        bool privateRoom = false;
        QStringList members;
        qint64 lastTs = 0;
        bool fetching = false;
        // Room key material, re-derived whenever the relay rotates the
        // channel's key version (removing a member re-keys the room).
        QString room;
        RoomCrypto crypto;
        QSet<QString> seenIds;
        QQueue<QString> seenOrder;
        QQueue<QString> pendingTexts;
        QPointer<ServerNode> sender;
        bool accessFetching = false;
        bool senderConnected = false;
    };

    void poll();
    void fetchChannels();
    void fetchHistory(const QString &channelId);
    void fetchRoomAccess(const QString &channelId);
    void openSender(const QString &channelId, const QJsonObject &payload);
    void flushPending(Room &room);
    void failPending(Room &room, const QString &reason);
    void rememberSeen(Room &room, const QString &messageId);
    void discardSender(Room &room);
    void applyHistory(Room &room, const QJsonObject &payload);
    QUrl signedUrl(const QString &path, const QString &ts,
                   const QByteArray &canonical) const;
    bool ready() const;

    QNetworkAccessManager *m_network = nullptr;
    QTimer *m_timer = nullptr;
    std::function<QString(const QByteArray &)> m_signer;
    std::function<bool(const QUrl &)> m_connectionAuthorizer;
    QString m_account;
    QString m_selfId;
    QString m_displayName;
    QUrl m_apiBase;
    bool m_listing = false;
    QHash<QString, Room> m_rooms; // channel id -> room
    QStringList m_conversations;  // stable display order
};
