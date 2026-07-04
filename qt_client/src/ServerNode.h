#pragma once

#include "ChatBackend.h"
#include "RoomCrypto.h"

#include <QHash>
#include <QJsonObject>
#include <QQueue>
#include <QSet>
#include <QTcpSocket>
#include <QUrl>

// QSettings keys for the per-metric "advertise this node's host stats" toggles
// (adhoc #23). Each controls whether this node samples and broadcasts that metric
// for the Mirror nodes view's CPU/RAM/disk bars. Headful nodes default these OFF
// (don't broadcast a personal machine's load); headless installs done from the
// Hosts tab are seeded ON at startup so the operator can monitor them remotely.
namespace TelemetrySettings {
inline const QString kReportCpu = QStringLiteral("nodes/reportCpu");
inline const QString kReportMemory = QStringLiteral("nodes/reportMemory");
inline const QString kReportDisk = QStringLiteral("nodes/reportDisk");
} // namespace TelemetrySettings

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

    // Set the ordered list of mainnodes to try, most preferred first (issue
    // #364). Any endpoint that isn't a valid ws://\/wss:// URL is dropped; the
    // first surviving entry becomes the active endpoint. The reconnect loop
    // rotates through the list on each failed attempt so a dead or
    // quota-limited mainnode no longer strands the node — a client heartbeats
    // to whichever mainnode answers. No-op (keeps the constructor URL) if the
    // filtered list is empty. Call before start().
    void setEndpoints(const QList<QUrl> &endpoints);

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
    void createPrivateChannel(const QString &channel) override;
    void inviteToChannel(const QString &peerId, const QString &channel) override;
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
        // Host resource telemetry the peer advertises in its hello/presence frames,
        // surfaced as the Mirror nodes view's CPU/RAM/disk bars.
        qint64 memUsedBytes = 0;
        qint64 memTotalBytes = 0;
        qint64 diskUsedBytes = 0;
        qint64 diskTotalBytes = 0;
        double cpuPercent = -1.0;
        qint64 lastSeenMs = 0;
        bool online = true;
    };

    void openConnection();      // (re)create the socket and start connecting
    void scheduleReconnect();   // progressive backoff after a drop/failure
    void advanceEndpoint();     // rotate m_url to the next configured mainnode
    void connectSocketSignals();
    void sendHandshake();
    void onSocketReadyRead();
    void onConnectedTransport();
    void processFrame(const QByteArray &payload);
    void sendTextFrame(const QByteArray &payload);
    void sendControlFrame(int opcode, const QByteArray &payload = QByteArray());
    void sendEncrypted(const QJsonObject &plain, bool showActivity = false);
    void sendHello();
    // Refresh the cached host resource telemetry (CPU/RAM/disk) advertised in the
    // "sys" field of every frame. Throttled internally so it only re-samples on
    // the heartbeats, not on each chat send. Cheap on Linux (/proc + statvfs).
    void sampleSystemStats();
    // Lightweight broadcast so peers refresh this node's "last seen"; sent on a
    // timer while connected. Unknown to older peers, but they still record it as
    // activity (every frame with a senderId refreshes the peer's last-seen time).
    void sendPresence();
    void sendHistoryTo(const QString &peerId);
    // Replay just one channel's history to a peer (used when inviting them into a
    // private room, whose history the general hello-reply deliberately withholds).
    void sendChannelHistoryTo(const QString &peerId, const QString &channel);
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
    QUrl m_url; // the mainnode we're currently pointed at (m_endpoints[m_endpointIndex])
    // All mainnodes to try, in preference order (issue #364). Seeded to just the
    // constructor URL; setEndpoints() replaces it with the full failover list.
    QList<QUrl> m_endpoints;
    int m_endpointIndex = 0; // which endpoint m_url currently points at
    QString m_roomName;
    QString m_solanaAddress;
    QString m_platform;
    QString m_version;
    QList<MirrorAdvert> m_mirroredRepos; // repos + HEAD advertised to other nodes
    // Cached host telemetry for our own node, refreshed by sampleSystemStats() and
    // copied into the "sys" field of outgoing frames + our own roster row.
    qint64 m_memUsedBytes = 0;
    qint64 m_memTotalBytes = 0;
    qint64 m_diskUsedBytes = 0;
    qint64 m_diskTotalBytes = 0;
    double m_cpuPercent = -1.0;
    qint64 m_lastStatsSampleMs = 0; // throttle so chat sends don't re-sample
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
    // Auto-reconnect: the node stays online across drops, retrying quickly at
    // first and backing off exponentially while the relay keeps refusing, until
    // it returns or the user explicitly leaves (adhoc #192, adhoc #66).
    QTimer *m_reconnectTimer = nullptr;
    int m_reconnectAttempts = 0; // consecutive failures since the last upgrade
    bool m_userStopped = false;
    QTimer *m_rosterEmitTimer = nullptr; // coalesces roster/status emissions

    // #welcome is the shared greeting room every node joins; a brand-new node
    // posts a one-time hello there so the network sees who joined (issue #192).
    QStringList m_channels{"#general", "#welcome", "#random"};
    // Invite-only channels (subset of m_channels). Never advertised in hello or
    // "channel" broadcasts, and messages in them carry "private":true so a peer
    // who wasn't invited drops them instead of auto-joining. See createPrivateChannel.
    QSet<QString> m_privateChannels;
    QHash<QString, Peer> m_peers;
    QHash<QString, QList<QJsonObject>> m_channelHistory;
    QHash<QString, QString> m_messageConversation;
    QHash<QString, QString> m_messageSender;
    QHash<QString, QHash<QString, QHash<QString, QString>>> m_reactions;
    QByteArray m_avatarPng;
    QSet<QString> m_seenIds;
    QQueue<QString> m_seenOrder;
};
