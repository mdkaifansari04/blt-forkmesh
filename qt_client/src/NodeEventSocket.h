#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>

class QTcpSocket;
class QTimer;

// Live event channel to the relay's per-owner ForkMeshNodes Durable Object
// (wss://<relay>/api/nodes/events). The relay pushes payload-free
// {"type":"event","topic","repo"} frames the moment a web submission lands
// (issue/PR/discussion/commit/agent prompt/About edit), and MainWindow answers
// each one with its single debounced GET /api/sync — so the source of truth
// updates instantly while the 5-minute HTTPS poll shrinks to a safety net.
//
// Unlike the retired per-repo RepoHost tunnel, this socket never carries
// repository bytes or inbox payloads in either direction: inbound frames are
// advisory only (worst case: one extra signed sync), and all repository data
// stays on the existing signed HTTPS routes.
//
// Outbound it carries the {"type":"ping"} keepalive plus the small key-signed
// control-plane writes the relay advertises in its hello frame — today only
// the batched agent-status report, which would otherwise be an HTTPS POST the
// node makes while already holding this connection (adhoc #1618). Those frames
// carry their own node/ts/sig proof and the relay checks it exactly as it does
// on HTTPS, so the socket is transport and never authority.
//
// The transport is the same hand-rolled RFC 6455 client over QSslSocket as
// ServerNode (deliberately no qt6-websockets dependency), minus the room
// crypto: event frames are relay-originated plaintext control frames.
class NodeEventSocket : public QObject
{
    Q_OBJECT
public:
    explicit NodeEventSocket(QObject *parent = nullptr);

    // Called before every connection attempt so each upgrade carries a
    // freshly-signed forkmesh-issues-pull-v1 token (owner/ts/sig query params
    // — the same auth as GET /api/sync). Returning an invalid URL skips the
    // attempt; the reconnect timer retries later (e.g. after login).
    void setUrlFactory(std::function<QUrl()> factory);
    // Same firewall gate as the mainnode room socket.
    void setConnectionAuthorizer(std::function<bool(const QUrl &)> authorizer);

    void start();
    void stop();
    bool isConnected() const { return m_wsReady; }
    // True once the connected relay has advertised that it accepts `kind` off
    // the socket. False against an older relay (and while disconnected), which
    // is what keeps the HTTPS fallback honest instead of dropping writes into
    // a relay that would ignore the frame.
    bool accepts(const QString &kind) const
    {
        return m_wsReady && m_accepts.contains(kind);
    }
    // Send one signed write. `frame` needs "type" plus whatever that frame
    // kind proves itself with; returns false when the socket cannot take it,
    // so the caller can fall back to the signed HTTPS route.
    bool sendSignedFrame(const QJsonObject &frame);

signals:
    // A payload-free relay push: something changed for this owner, run one
    // consolidated /api/sync soon. `repo` is "owner/name" context (may be
    // empty); the sync itself covers every owned repo either way.
    void eventReceived(const QString &topic, const QString &repo);
    // The relay's verdict on a batched agent-status frame, mirroring what the
    // HTTPS reply carried: `ok` false means nothing was written, otherwise
    // `results` reports each task individually.
    void agentStatusResult(bool ok, const QJsonArray &results);
    void connectedChanged(bool connected);
    void systemMessage(const QString &text);

private:
    void openConnection();
    void scheduleReconnect();
    void discardCurrentSocket();
    void failCurrentConnection();
    void handleLinkLost();
    void connectSocketSignals();
    void sendHandshake();
    void onSocketReadyRead();
    void processFrame(const QByteArray &payload);
    void sendTextFrame(const QByteArray &payload);
    void sendControlFrame(int opcode, const QByteArray &payload = QByteArray());
    void onPingTick();

    std::function<QUrl()> m_urlFactory;
    std::function<bool(const QUrl &)> m_connectionAuthorizer;
    QUrl m_url; // the endpoint of the attempt in flight (for messages)
    QTcpSocket *m_socket = nullptr;
    QByteArray m_readBuffer;
    QByteArray m_wsKey;
    // Frame kinds the connected relay said it accepts, from its hello frame.
    // Cleared on every (re)connect: capability belongs to the live link.
    QSet<QString> m_accepts;
    bool m_wsReady = false;
    bool m_attemptActive = false;
    bool m_userStopped = true; // inert until start()
    QTimer *m_pingTimer = nullptr;      // keepalive + stale-rx watchdog
    QTimer *m_reconnectTimer = nullptr; // exponential backoff between attempts
    QTimer *m_connectTimeoutTimer = nullptr; // bounds connect+TLS+upgrade
    int m_reconnectAttempts = 0;
    qint64 m_lastRxMs = 0;
    qint64 m_lastAppPingMs = 0;
};
