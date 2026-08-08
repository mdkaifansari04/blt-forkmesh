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

// RFC 6455 control channel for payload-free sync notifications and signed
// node writes. It never carries repository or inbox data; every write retains
// its node/ts/sig proof, so the socket is transport rather than authority.
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
    void setConnectionAuthorizer(std::function<bool(const QUrl &)> authorizer);

    void start();
    void stop();
    bool isConnected() const { return m_wsReady; }
    bool accepts(const QString &kind) const
    {
        return m_wsReady && m_accepts.contains(kind);
    }
    // Send one signed write. `frame` needs "type" plus whatever that frame
    // kind proves itself with; returns false when the socket cannot take it,
    // so the caller can fall back to the signed HTTPS route.
    bool sendSignedFrame(const QJsonObject &frame);

signals:
    void eventReceived(const QString &topic, const QString &repo);
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
