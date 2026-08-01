#pragma once

#include <QObject>
#include <QUrl>

#include <functional>

class QTcpSocket;
class QTimer;

















class NodeEventSocket : public QObject
{
    Q_OBJECT
public:
    explicit NodeEventSocket(QObject *parent = nullptr);





    void setUrlFactory(std::function<QUrl()> factory);

    void setConnectionAuthorizer(std::function<bool(const QUrl &)> authorizer);

    void start();
    void stop();
    bool isConnected() const { return m_wsReady; }

signals:



    void eventReceived(const QString &topic, const QString &repo);
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
    QUrl m_url;
    QTcpSocket *m_socket = nullptr;
    QByteArray m_readBuffer;
    QByteArray m_wsKey;
    bool m_wsReady = false;
    bool m_attemptActive = false;
    bool m_userStopped = true;
    QTimer *m_pingTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_connectTimeoutTimer = nullptr;
    int m_reconnectAttempts = 0;
    qint64 m_lastRxMs = 0;
    qint64 m_lastAppPingMs = 0;
};
