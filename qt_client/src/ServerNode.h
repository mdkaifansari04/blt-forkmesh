#pragma once

#include "ChatBackend.h"
#include "RoomCrypto.h"

#include <QHash>
#include <QJsonObject>
#include <QQueue>
#include <QSet>
#include <QTcpSocket>
#include <QUrl>

#include <functional>






namespace TelemetrySettings {
inline const QString kReportCpu = QStringLiteral("nodes/reportCpu");
inline const QString kReportMemory = QStringLiteral("nodes/reportMemory");
inline const QString kReportDisk = QStringLiteral("nodes/reportDisk");
}

class QTimer;

class ServerNode : public ChatBackend
{
    Q_OBJECT
public:
    ServerNode(const QString &userName, const QString &nodeName,
               const QString &ownerUser, const QString &stableNodeId,
               const QUrl &serverUrl,
               const QString &roomName,
               const QString &solanaAddress,



               const QString &roomPassphrase = QString(),
               QObject *parent = nullptr);

    bool start();








    void setEndpoints(const QList<QUrl> &endpoints);
    void setConnectionAuthorizer(std::function<bool(const QUrl &)> authorizer);
    void setNetworkAvailable(bool available) override;

#ifdef FORKMESH_SERVER_NODE_TESTS


    void setTransportLimitsForTests(int connectTimeoutMs,
                                    int reconnectBaseDelayMs,
                                    qint64 maxPendingWriteBytes);
#endif

    void sendChat(const QString &channel, const QString &text) override;
    void sendThreadReply(const QString &channel,
                         const QString &rootMessageId,
                         const QString &text) override;
    void setAccountKind(const QString &kind) override;
    void setRoomPassphrase(const QString &passphrase) override;
    void sendBotChat(const QString &channel, const QString &text) override;
    void sendDirect(const QString &targetId, const QString &text) override;
    void sendFile(const QString &conversation, const QString &fileName,
                  const QString &mimeType, const QByteArray &data) override;
    void sendReaction(const QString &conversation, const QString &messageId,
                      const QString &emoji) override;
    void editMessage(const QString &conversation, const QString &messageId,
                     const QString &newText) override;
    void deleteMessage(const QString &conversation, const QString &messageId) override;
    void sendAdminDelete(const QString &conversation, const QString &messageId,
                         qint64 ts, const QString &signature) override;
    void applyAdminDelete(const QString &conversation,
                          const QString &messageId) override;
    void setAvatar(const QByteArray &pngData) override;
    void setUserName(const QString &name) override;
    void setNodeIdentity(const QString &nodeName, const QString &ownerUser) override;
    void forgetMember(const QString &peerId) override;
    void sendTyping(const QString &conversation, bool active) override;
    void addChannel(const QString &channel) override;
    void removeChannel(const QString &channel) override;
    void createPrivateChannel(const QString &channel) override;
    void inviteToChannel(const QString &peerId, const QString &channel) override;
    void setMirroredRepos(const QList<MirrorAdvert> &repos) override;
    void notifyMirrorUpdated(const QString &ownerName,
                             const QString &commit = QString()) override;
    void notifyMirrorSynced(const QString &ownerName,
                            const QString &commit = QString()) override;
    void requestMirrorRefresh(const QString &source,
                              const QString &ownerName) override;
    void advertiseMirrorsNow() override;
    void notifyCoveOpened(const QString &creatorKey, const QString &coveId,
                          const QString &coveName, const QString &openerKey,
                          const QString &openerName, qint64 ts,
                          const QString &signature) override;
    void notifyCoveInvited(const QString &inviteeAccount, const QString &coveId,
                           const QString &coveName, const QString &inviterName,
                           qint64 ts) override;
    QList<QJsonObject> networkDiagnostics() const override;
    void shutdown() override;
    QString modeName() const override { return "Mainnode"; }

signals:


    void connectionChanged(bool connected);

private:
    struct Peer {
        QString name;
        QString nodeName;
        QString ownerUser;
        QString accountKind;
        QString solanaAddress;
        QString platform;
        QString version;
        QStringList mirrors;
        QList<MirrorAdvert> mirrorDetails;


        qint64 memUsedBytes = 0;
        qint64 memTotalBytes = 0;
        qint64 diskUsedBytes = 0;
        qint64 diskTotalBytes = 0;
        double cpuPercent = -1.0;
        qint64 lastSeenMs = 0;
        bool online = true;
    };

    void openConnection();
    void scheduleReconnect();
    void discardCurrentSocket();
    void failCurrentConnection();



    void handleLinkLost();
    void advanceEndpoint();
    void connectSocketSignals();
    void sendHandshake();
    void onSocketReadyRead();
    void onConnectedTransport();
    void processFrame(const QByteArray &payload);
    void sendTextFrame(const QByteArray &payload, const QString &type = QString(),
                       const QString &scope = QString());
    void sendControlFrame(int opcode, const QByteArray &payload = QByteArray());
    void sendEncrypted(const QJsonObject &plain, bool showActivity = false);
    void sendHello(bool force = false, bool showActivity = false);



    void sampleSystemStats();



    void sendPresence();
    void sendHistoryTo(const QString &peerId);


    void sendChannelHistoryTo(const QString &peerId, const QString &channel);
    void loadKnownPeers();
    void persistKnownPeers() const;


    QString hiddenChannelsSettingKey() const;
    void loadHiddenChannels();
    void saveHiddenChannels();
    void handlePlain(const QJsonObject &message);
    void emitChat(const QJsonObject &message);
    void emitDm(const QJsonObject &message, const QString &conversationPeer);
    void rememberPeer(const QString &peerId, const QString &name,
                      const QString &nodeName = QString(),
                      const QString &ownerUser = QString(),
                      const QString &accountKind = QString(),
                      const QString &solanaAddress = QString(),
                      const QString &platform = QString(),
                      const QString &version = QString(), bool online = true);


    void updateRosterAndStatus();
    void flushRosterAndStatus();
    void storeHistory(const QJsonObject &message);



    void indexMessage(const QString &id, const QString &conversation,
                      const QString &senderId);


    void dropMessageIndex(const QString &id);



    void reapStalePeers();
    void updateStoredMessage(const QString &messageId, const QString &text, bool deleted);
    bool messageIsAuthoredBy(const QString &messageId, const QString &senderId) const;
    void applyEdit(const QString &conversation, const QString &target,
                   const QString &senderId, const QString &text);
    void applyDelete(const QString &conversation, const QString &target,
                     const QString &senderId);


    void applyDeleteUnchecked(const QString &conversation, const QString &target);
    bool markSeen(const QString &messageId);
    QJsonObject makeMessage(const QString &type) const;

    QString m_userName;

    QString m_accountKind = QStringLiteral("node");
    QString m_nodeName;
    QString m_ownerUser;
    QUrl m_url;


    QList<QUrl> m_endpoints;
    int m_endpointIndex = 0;
    QString m_roomName;
    QString m_solanaAddress;
    QString m_platform;
    QString m_version;
    QList<MirrorAdvert> m_mirroredRepos;


    qint64 m_memUsedBytes = 0;
    qint64 m_memTotalBytes = 0;
    qint64 m_diskUsedBytes = 0;
    qint64 m_diskTotalBytes = 0;
    double m_cpuPercent = -1.0;
    qint64 m_lastStatsSampleMs = 0;
    QString m_nodeId;


    QString m_rosterStorageKey;
    RoomCrypto m_crypto;
    QTcpSocket *m_socket = nullptr;
    std::function<bool(const QUrl &)> m_connectionAuthorizer;
    QByteArray m_readBuffer;
    QByteArray m_wsKey;
    bool m_wsReady = false;
    QTimer *m_pingTimer = nullptr;
    qint64 m_pingSentMs = 0;
    QTimer *m_presenceTimer = nullptr;
    QTimer *m_helloAdvertiseTimer = nullptr;



    QTimer *m_reconnectTimer = nullptr;
    int m_reconnectAttempts = 0;


    QTimer *m_connectTimeoutTimer = nullptr;
    bool m_userStopped = false;
    bool m_networkAvailable = true;



    bool m_attemptActive = false;
    int m_connectTimeoutMs = 30000;
    int m_reconnectBaseDelayMs = 1000;
    qint64 m_maxPendingWriteBytes = 96ll * 1024 * 1024 + 14;
    bool m_reconnectJitter = true;
    qint64 m_backpressureDrops = 0;
    qint64 m_lastBackpressureNoticeMs = 0;
    QTimer *m_rosterEmitTimer = nullptr;
    QHash<QString, qint64> m_lastHelloReplyMs;
    qint64 m_lastHelloSentMs = 0;
    QString m_lastStatusText;
    qint64 m_lastStatusEmitMs = 0;
    qint64 m_wsConnectedAtMs = 0;
    qint64 m_lastRxMs = 0;
    qint64 m_lastTxMs = 0;
    qint64 m_rxFrames = 0;
    qint64 m_txFrames = 0;
    qint64 m_rxBytes = 0;
    qint64 m_txBytes = 0;
    qint64 m_rxControlFrames = 0;
    qint64 m_txControlFrames = 0;
    int m_lastRxBytes = 0;
    int m_lastTxBytes = 0;
    QString m_lastRxType;
    QString m_lastRxScope;
    QString m_lastTxType;
    QString m_lastTxScope;



    QStringList m_channels{"#general", "#welcome", "#random"};



    QSet<QString> m_privateChannels;




    QSet<QString> m_hiddenChannels;
    QHash<QString, Peer> m_peers;
    QHash<QString, QList<QJsonObject>> m_channelHistory;



    QHash<QString, qsizetype> m_channelHistoryChars;
    QHash<QString, QString> m_messageConversation;
    QHash<QString, QString> m_messageSender;


    QQueue<QString> m_messageIndexOrder;
    QHash<QString, QHash<QString, QHash<QString, QString>>> m_reactions;
    QByteArray m_avatarPng;
    QSet<QString> m_seenIds;
    QQueue<QString> m_seenOrder;
};
