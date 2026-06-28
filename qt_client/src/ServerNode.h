#pragma once

#include "ChatBackend.h"
#include "RoomCrypto.h"

#include <QHash>
#include <QJsonObject>
#include <QQueue>
#include <QSet>
#include <QTcpSocket>
#include <QUrl>

class QTimer;

class ServerNode : public ChatBackend
{
    Q_OBJECT
public:
    ServerNode(const QString &userName, const QString &stableNodeId,
               const QUrl &serverUrl,
               const QString &roomName,
               const QString &solanaAddress,
               QObject *parent = nullptr);

    bool start();

    void sendChat(const QString &channel, const QString &text) override;
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
    void forgetMember(const QString &peerId) override;
    void sendTyping(const QString &conversation, bool active) override;
    void addChannel(const QString &channel) override;
    void setMirroredRepos(const QList<MirrorAdvert> &repos) override;
    void notifyMirrorUpdated(const QString &ownerName) override;
    void notifyCoveOpened(const QString &creatorKey, const QString &coveId,
                          const QString &coveName, const QString &openerKey,
                          const QString &openerName, qint64 ts,
                          const QString &signature) override;
    void shutdown() override;
    QString modeName() const override { return "Mainnode"; }

private:
    struct Peer {
        QString name;
        QString solanaAddress;
        QString platform;
        QString version;
        QStringList mirrors;
        QList<MirrorAdvert> mirrorDetails; // per-repo HEAD info advertised by the peer
        qint64 lastSeenMs = 0;
        bool online = true;
    };

    void openConnection();      // (re)create the socket and start connecting
    void scheduleReconnect();   // progressive backoff after a drop/failure
    void connectSocketSignals();
    void sendHandshake();
    void onSocketReadyRead();
    void onConnectedTransport();
    void processFrame(const QByteArray &payload);
    void sendTextFrame(const QByteArray &payload);
    void sendControlFrame(int opcode, const QByteArray &payload = QByteArray());
    void sendEncrypted(const QJsonObject &plain, bool showActivity = false);
    void sendHello();
    // Lightweight broadcast so peers refresh this node's "last seen"; sent on a
    // timer while connected. Unknown to older peers, but they still record it as
    // activity (every frame with a senderId refreshes the peer's last-seen time).
    void sendPresence();
    void sendHistoryTo(const QString &peerId);
    void loadKnownPeers();
    void persistKnownPeers() const;
    void handlePlain(const QJsonObject &message);
    void emitChat(const QJsonObject &message);
    void emitDm(const QJsonObject &message, const QString &conversationPeer);
    void rememberPeer(const QString &peerId, const QString &name,
                      const QString &solanaAddress = QString(),
                      const QString &platform = QString(),
                      const QString &version = QString(), bool online = true);
    // Coalesced: schedules a roster/status emit shortly after the last change so
    // a burst of frames (typing, presence, hellos) collapses into one update.
    void updateRosterAndStatus();
    void flushRosterAndStatus(); // does the actual roster build + emit
    void storeHistory(const QJsonObject &message);
    void updateStoredMessage(const QString &messageId, const QString &text, bool deleted);
    bool messageIsAuthoredBy(const QString &messageId, const QString &senderId) const;
    void applyEdit(const QString &conversation, const QString &target,
                   const QString &senderId, const QString &text);
    void applyDelete(const QString &conversation, const QString &target,
                     const QString &senderId);
    // Mark a message deleted without the author check (used after an admin
    // moderation delete has been verified by the UI).
    void applyDeleteUnchecked(const QString &conversation, const QString &target);
    bool markSeen(const QString &messageId);
    QJsonObject makeMessage(const QString &type) const;

    QString m_userName;
    QUrl m_url;
    QString m_roomName;
    QString m_solanaAddress;
    QString m_platform;
    QString m_version;
    QList<MirrorAdvert> m_mirroredRepos; // repos + HEAD advertised to other nodes
    QString m_nodeId;
    // SHA-256 of mainnode URL + room; scopes the persisted roster so members are
    // only recalled for the exact same room.
    QString m_rosterStorageKey;
    RoomCrypto m_crypto;
    QTcpSocket *m_socket = nullptr;
    QByteArray m_readBuffer;
    QByteArray m_wsKey;
    bool m_wsReady = false;
    QTimer *m_pingTimer = nullptr; // keeps the relay connection from idling out
    QTimer *m_presenceTimer = nullptr; // periodic presence beat + stale-peer sweep
    // Auto-reconnect: the node stays online across drops, retrying about once a
    // second until the relay returns or the user explicitly leaves (adhoc #192).
    QTimer *m_reconnectTimer = nullptr;
    bool m_userStopped = false;
    QTimer *m_rosterEmitTimer = nullptr; // coalesces roster/status emissions

    QStringList m_channels{"#general", "#random"};
    QHash<QString, Peer> m_peers;
    QHash<QString, QList<QJsonObject>> m_channelHistory;
    QHash<QString, QString> m_messageConversation;
    QHash<QString, QString> m_messageSender;
    QHash<QString, QHash<QString, QHash<QString, QString>>> m_reactions;
    QByteArray m_avatarPng;
    QSet<QString> m_seenIds;
    QQueue<QString> m_seenOrder;
};
