#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QTcpSocket;
class QTimer;
class QJsonObject;

// Live file host for one mirrored repository. Connects out to the relay's
// per-repo /host WebSocket and answers tree/blob requests by reading the local
// bare mirror with git. The repository's data never leaves this machine except
// as on-demand replies to a browsing visitor; nothing is uploaded or stored on
// the relay. Uses a minimal hand-rolled WebSocket client (like ServerNode) so
// no extra Qt module is required.
class RepoHost : public QObject
{
    Q_OBJECT
public:
    RepoHost(const QString &owner, const QString &name, const QString &mirrorPath,
             const QUrl &url, QObject *parent = nullptr);

    void start();
    void stop();

signals:
    void log(const QString &line);

private:
    void connectSocket();
    void sendHandshake();
    void onTransportReady();
    void onReadyRead();
    void sendText(const QByteArray &payload);
    void handleFrame(const QByteArray &payload);
    void handleRequest(const QJsonObject &request);
    QString baseRef() const;
    QJsonObject buildTreeReply(const QString &path) const;
    QJsonObject buildBlobReply(const QString &path) const;
    void scheduleReconnect();

    QTcpSocket *m_socket = nullptr;
    QUrl m_url;
    QString m_owner;
    QString m_name;
    QString m_mirrorPath;
    QByteArray m_wsKey;
    QByteArray m_readBuffer;
    bool m_wsReady = false;
    bool m_stopping = false;
    QTimer *m_reconnect = nullptr;
};
