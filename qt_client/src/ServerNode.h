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
    ServerNode(const QString &userName, const QString &nodeName,
               const QString &ownerUser, const QString &stableNodeId,
               const QUrl &serverUrl,
               const QString &roomName,
               const QString &solanaAddress,
               // Shared room key fetched from the relay (server-derived from
               // DATA_KEY). Empty keeps the legacy baked-in app key as a fallback
               // so chat still works before/if the key fetch fails.
               const QString &roomPassphrase = QString(),
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
    void setConnectionAuthorizer(std::function<bool(const QUrl &)> authorizer);
    void setNetworkAvailable(bool available) override;

#ifdef FORKMESH_SERVER_NODE_TESTS
    // Keeps transport integration tests fast and makes backpressure
    // deterministic without changing production limits.
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
                              const QString &ownerName,
                              const QString &toNodeId = QString(),
                              bool sync = false) override;
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
    // Transport state for on-demand users of ServerNode. statusChanged is
    // human-readable and must not be parsed to decide when a frame can be sent.
    void connectionChanged(bool connected);

private:
    struct Peer {
        QString name;
        QString nodeName;
        QString ownerUser;
        QString accountKind; // "node" or "user", from the peer's advertised accountKind
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
    void discardCurrentSocket();
    void failCurrentConnection();
    // Shared teardown when the link drops — from the socket's disconnected()
    // signal or from the ping timer's stale-rx watchdog, which catches
    // half-open sockets that never emit disconnected() at all.
    void handleLinkLost();
    void advanceEndpoint();     // rotate m_url to the next configured mainnode
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
    // Rooms the user deleted, persisted per room/relay so a delete sticks across
    // restarts and can't be resurrected by peer traffic.
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
    // Coalesced: schedules a roster/status emit shortly after the last change so
    // a burst of frames (typing, presence, hellos) collapses into one update.
    void updateRosterAndStatus();
    void flushRosterAndStatus(); // does the actual roster build + emit
    void storeHistory(const QJsonObject &message);
    // Record a message's conversation + sender for later edit/delete lookups,
    // bounded FIFO-style so months of traffic can't grow the maps without
    // limit on a long-running node (issue #428).
    void indexMessage(const QString &id, const QString &conversation,
                      const QString &senderId);
    // Drop every per-message record (conversation, sender, reactions) for an
    // id that no longer needs them (evicted from history or the index FIFO).
    void dropMessageIndex(const QString &id);
    // Erase peers not heard from in kPeerReapMs — they've long been hidden
    // from the roster, but their Peer entries (mirror adverts included) would
    // otherwise accumulate in RAM forever on a long-running node (issue #428).
    void reapStalePeers();
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
    // Stamped on every outgoing frame; web surfaces only display "user" frames.
    QString m_accountKind = QStringLiteral("node");
    QString m_nodeName;
    QString m_ownerUser;
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
    std::function<bool(const QUrl &)> m_connectionAuthorizer;
    QByteArray m_readBuffer;
    QByteArray m_wsKey;
    bool m_wsReady = false;
    QTimer *m_pingTimer = nullptr; // keeps the relay connection from idling out
    qint64 m_pingSentMs = 0; // when the last keepalive ping left (RTT sample)
    QTimer *m_presenceTimer = nullptr; // periodic presence beat + stale-peer sweep
    QTimer *m_helloAdvertiseTimer = nullptr; // coalesces mirror hello updates
    // Auto-reconnect: the node stays online across drops, retrying quickly at
    // first and backing off exponentially while the relay keeps refusing, until
    // it returns or the user explicitly leaves (adhoc #192, adhoc #66).
    QTimer *m_reconnectTimer = nullptr;
    int m_reconnectAttempts = 0; // consecutive failures since the last upgrade
    // Aborts a connect/TLS/upgrade attempt that stalls silently — before the
    // 101 no other timer is running, so a wedged attempt would hang forever.
    QTimer *m_connectTimeoutTimer = nullptr;
    bool m_userStopped = false;
    bool m_networkAvailable = true;
    // True for exactly one current TCP/TLS/WebSocket attempt (including an
    // upgraded live link). It makes duplicate error/disconnect callbacks
    // idempotent and prevents two reconnect timers from one failure.
    bool m_attemptActive = false;
    int m_connectTimeoutMs = 30000;
    int m_reconnectBaseDelayMs = 1000;
    qint64 m_maxPendingWriteBytes = 96ll * 1024 * 1024 + 14;
    bool m_reconnectJitter = true;
    qint64 m_backpressureDrops = 0;
    qint64 m_lastBackpressureNoticeMs = 0;
    QTimer *m_rosterEmitTimer = nullptr; // coalesces roster/status emissions
    QHash<QString, qint64> m_lastHelloReplyMs; // peer id -> last directed hello reply
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

    // Shared network rooms every node starts in. #welcome is the one-time
    // "just joined" room (only verified users actually post there).
    QStringList m_channels{"#general", "#welcome", "#random"};
    // Invite-only channels (subset of m_channels). Never advertised in hello or
    // "channel" broadcasts, and messages in them carry "private":true so a peer
    // who wasn't invited drops them instead of auto-joining. See createPrivateChannel.
    QSet<QString> m_privateChannels;
    // Rooms the user explicitly deleted. Kept out of m_channels and re-checked
    // whenever a peer hello / channel broadcast / chat would otherwise re-add a
    // room, so a deleted room stays gone. Persisted in QSettings so it survives
    // restarts (loadHiddenChannels/saveHiddenChannels).
    QSet<QString> m_hiddenChannels;
    QHash<QString, Peer> m_peers;
    QHash<QString, QList<QJsonObject>> m_channelHistory;
    // Running ChatHistoryLimits::entryCost total per channel, so appendBounded
    // doesn't re-walk (and re-decode) the whole history on every message.
    // Kept in lockstep by storeHistory/updateStoredMessage/channel removal.
    QHash<QString, qsizetype> m_channelHistoryChars;
    QHash<QString, QString> m_messageConversation;
    QHash<QString, QString> m_messageSender;
    // Insertion order of the ids in the two maps above; oldest are dropped
    // once the index outgrows its cap (see indexMessage).
    QQueue<QString> m_messageIndexOrder;
    QHash<QString, QHash<QString, QHash<QString, QString>>> m_reactions;
    QByteArray m_avatarPng;
    QSet<QString> m_seenIds;
    QQueue<QString> m_seenOrder;
};
