#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

class QTcpSocket;
class QTimer;
class QJsonObject;
class QProcess;

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
    QStringList branchRefCandidates(const QString &branch) const;
    QString refForBranch(const QString &branch) const;
    // Issue/pull/discussion/commit tallies bundled with the root tree so the
    // website can show tab badges without firing a request per counter.
    QJsonObject buildRootCounts(const QString &ref) const;
    QJsonObject buildBranchesReply() const;
    QJsonObject buildTreeReply(const QString &path, const QString &branch) const;
    QJsonObject buildBlobReply(const QString &path, const QString &branch) const;
    QJsonObject buildCommitsReply(const QString &branch) const; // recent commit list
    QJsonObject buildCommitReply(const QString &hash) const; // one commit's diff
    // Run git upload-pack and stream stdout back as git-chunk/git-end messages.
    void runGitStream(const QString &reqId, const QStringList &args,
                      const QByteArray &input);
    // git push (issue #358): receive-pack whose stdin (the pushed pack) arrives
    // as a stream of git-req-chunk messages rather than one buffered body, so a
    // large push is never held whole. startReceivePack spawns the process;
    // feedReceivePack/finishReceiveInput relay stdin as the relay pumps it.
    void startReceivePack(const QString &reqId);
    void feedReceivePack(const QString &reqId, const QByteArray &data);
    void finishReceiveInput(const QString &reqId);
    // Stream a content-addressed release binary (kept outside git) back over the
    // same git-chunk/git-end protocol. `sha256` is the asset's content address.
    void streamReleaseBlob(const QString &reqId, const QString &sha256);
    void streamRawBlob(const QString &reqId, const QString &path, const QString &branch);
    void sendGitChunk(const QString &reqId, const QByteArray &data);
    void sendGitEnd(const QString &reqId, bool ok, const QString &error);
    void scheduleReconnect();
    // Run `work` (must not touch `this` — see blobReplyFor in RepoHost.cpp) on a
    // detached worker thread, then deliver `apply(result)` back on this object's
    // thread. Keeps a request's chain of git spawns from blocking the socket's
    // event loop (see StallWatchdog reports naming buildBlobReply/refForBranch).
    void runOffThread(std::function<QJsonObject()> work,
                      std::function<void(QJsonObject)> apply);

    QTcpSocket *m_socket = nullptr;
    QUrl m_url;
    QString m_owner;
    QString m_name;
    QString m_mirrorPath;
    QByteArray m_wsKey;
    QByteArray m_readBuffer;
    bool m_wsReady = false;
    bool m_stopping = false;
    qint64 m_lastRx = 0; // ms epoch of the last bytes received; detects a half-open socket
    QTimer *m_reconnect = nullptr;
    QTimer *m_pingTimer = nullptr; // keepalive so the relay holds the host link
    std::function<QString()> m_tokenProvider; // fresh /host auth token per connect

    // In-flight receive-pack pushes keyed by reqId. Input can arrive before the
    // process is running (QProcess is still Starting), so it is buffered and
    // flushed on the started() signal — mirroring runGitStream's own guard.
    struct ReceiveJob {
        QProcess *process = nullptr;
        QByteArray pendingInput; // stdin buffered until the process starts
        bool started = false;
        bool endReceived = false; // git-req-end seen before started()
    };
    QHash<QString, ReceiveJob *> m_receiveJobs;
};
