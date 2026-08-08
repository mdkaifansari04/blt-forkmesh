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
// This bridge lists the channels the account may read, fetches each room's
// retained (still-encrypted) backlog once, and then holds that room's ticketed
// encrypted WebSocket open so later messages — the ones that light the chat
// button's unread badge — arrive as pushes instead of being discovered by a
// repeating history fetch (no-polling policy). All HTTP/socket work remains
// asynchronous.
//
// The only history reads left are the one-per-room startup fetch, the catch-up
// fetch each (re)connect fires to drain whatever landed while the socket was
// down, and whatever a user action asks for (refresh()). Nothing is on a timer.
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

    // Idempotent: start()/stop() may be called on every heartbeat. start()
    // performs exactly one channel-list + history pass; nothing repeats it.
    void start();
    void stop();
    bool isActive() const;
    // Re-list the channels and drain each room's backlog once. For user
    // actions only (opening Chat) — a channel created after launch has no push
    // to announce itself, and this is the moment the sidebar has to be right.
    void refresh();

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
        // Position in the channel list, so the live-socket budget is spent on a
        // stable set of rooms rather than whichever one answered first.
        int order = 0;
        // A room this client keeps a receiving socket on. Rooms past the budget
        // only connect when the user sends into them.
        bool wantsSocket = false;
        int reconnectAttempts = 0;
        bool reconnectPending = false;
    };

    void fetchChannels();
    // Open (or keep) this room's receiving socket, if it is inside the budget.
    void ensureReceiver(const QString &channelId);
    // Re-ticket and reopen a dropped room socket with bounded backoff. The
    // socket is this room's only live message path, so it is never left down.
    void scheduleReconnect(const QString &channelId);
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
    bool m_active = false;
    // One notice per run when the channel list outgrows the live-socket budget,
    // so a silently unwatched room is visible in the log.
    bool m_socketBudgetNoted = false;
    // When the channel list was last asked for, so refresh() stays a user
    // action rather than becoming a click-driven poll.
    qint64 m_lastListMs = 0;
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
