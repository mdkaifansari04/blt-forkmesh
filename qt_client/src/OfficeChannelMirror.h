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














namespace forkmesh::office {




QString conversationForChannel(const QString &channelName);
bool isOfficeConversation(const QString &conversation);



QByteArray channelListProof(const QString &account, const QString &ts);
QByteArray channelAccessProof(const QString &account, const QString &channelId,
                              const QString &ts);
QByteArray channelHistoryProof(const QString &account, const QString &channelId,
                               const QString &ts);



bool chatMessageFromPlain(const QJsonObject &plain, const QString &conversation,
                          const QString &selfId, ChatMessage *out);

}

class OfficeChannelMirror : public QObject
{
    Q_OBJECT
public:
    explicit OfficeChannelMirror(QNetworkAccessManager *network,
                                 QObject *parent = nullptr);



    void setSigner(std::function<QString(const QByteArray &)> signer);


    void setIdentity(const QString &account, const QString &selfId,
                     const QString &displayName = QString());
    void setConnectionAuthorizer(std::function<bool(const QUrl &)> authorizer);


    void setApiBase(const QUrl &apiBase);


    void start();
    void stop();
    bool isActive() const;

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
    QHash<QString, Room> m_rooms;
    QStringList m_conversations;
};
