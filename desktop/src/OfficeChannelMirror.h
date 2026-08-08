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

// Office channels are independently encrypted rooms admitted with 60-second
// WebSocket tickets. The desktop authenticates with its Ed25519 identity,
// fetches retained ciphertext at startup/reconnect, and otherwise receives
// messages only through asynchronous socket pushes.
namespace forkmesh::office {

QString conversationForChannel(const QString &channelName);
bool isOfficeConversation(const QString &conversation);

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
    void setIdentity(const QString &account, const QString &selfId,
                     const QString &displayName = QString());
    void setConnectionAuthorizer(std::function<bool(const QUrl &)> authorizer);
    void setApiBase(const QUrl &apiBase);

    void start();
    void stop();
    bool isActive() const;
    void refresh();

    QStringList conversations() const;
    QStringList membersForConversation(const QString &conversation) const;
    bool isPrivateConversation(const QString &conversation) const;
    bool canSend(const QString &conversation) const;
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
        QString room;
        RoomCrypto crypto;
        QSet<QString> seenIds;
        QQueue<QString> seenOrder;
        QQueue<QString> pendingTexts;
        QPointer<ServerNode> sender;
        bool accessFetching = false;
        bool senderConnected = false;
        int order = 0;
        bool wantsSocket = false;
        int reconnectAttempts = 0;
        bool reconnectPending = false;
    };

    void fetchChannels();
    void ensureReceiver(const QString &channelId);
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
    bool m_socketBudgetNoted = false;
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
