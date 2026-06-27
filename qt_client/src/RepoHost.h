#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

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

    // Supplies a freshly-signed "ts=…&sig=…" query string appended to the /host
    // upgrade so the relay can verify this node may host owner/repo. Called on
    // every (re)connect so the timestamp never goes stale.
    void setTokenProvider(std::function<QString()> provider);

signals:
    void log(const QString &line);
    // Emitted whenever a web request is served for this repo; clone=true for a
    // git clone (upload-pack) so the client can count relays and clones.
    void requestServed(const QString &owner, const QString &name, bool clone);

private:
    void connectSocket();
    void sendHandshake();
    void onTransportReady();
    void onReadyRead();
    void sendText(const QByteArray &payload);
    void sendControlFrame(int opcode, const QByteArray &payload = QByteArray());
    void handleFrame(const QByteArray &payload);
    void handleRequest(const QJsonObject &request);
    QString baseRef() const;
    QJsonObject buildTreeReply(const QString &path) const;
    // Issue/pull/discussion/commit tallies bundled with the root tree so the
    // website can show tab badges without firing a request per counter.
    QJsonObject buildRootCounts(const QString &ref) const;
    QJsonObject buildBlobReply(const QString &path) const;
    QJsonObject buildCommitsReply() const;             // recent commit list
    QJsonObject buildCommitReply(const QString &hash) const; // one commit's diff
    // Run git upload-pack and stream stdout back as git-chunk/git-end messages.
    void runGitStream(const QString &reqId, const QStringList &args,
                      const QByteArray &input);
    void sendGitChunk(const QString &reqId, const QByteArray &data);
    void sendGitEnd(const QString &reqId, bool ok, const QString &error);
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
    QTimer *m_pingTimer = nullptr; // keepalive so the relay holds the host link
    std::function<QString()> m_tokenProvider; // fresh /host auth token per connect
};
