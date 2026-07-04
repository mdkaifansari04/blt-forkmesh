// Nightly end-to-end mesh-loop test (issue #352).
//
// Exercises one continuous full-product loop the way a real ForkMesh mesh does:
//
//   publish -> browse -> clone -> file issue -> drain -> agent PR -> merge
//
// Every past reliability outage (relay OOM, stale presence, stalled releases)
// would have tripped one of these steps. The test wires the *real* client-side
// building blocks — RepoHost (the live file/git tunnel a desktop node runs),
// IssueStore + ForkMeshIdentity (signed issue events) — against an in-process
// RelayStub that speaks the same tunnel + smart-HTTP + issue-inbox protocol the
// Cloudflare Worker relay does. Nothing here depends on the network, an API
// quota, or a model: the agent step shells out to a deterministic stub binary
// (FORKMESH_E2E_STUB_AGENT) so we assert the PR plumbing, not the agent.
//
// The RelayStub deliberately re-implements the *observable* relay contract
// (mirrored from cloudflare_worker/src/entry.py): the /host WebSocket tunnel,
// the git smart-HTTP wrapping (info/refs advertisement + git-upload-pack POST),
// a /api/repositories catalog, a blob-browse endpoint, and a signed-issue
// inbox POST verified with the exact same canonical string the worker uses
// (verify_issue_event <-> IssueStore::canonicalString). The matching worker
// side of that contract is asserted in cloudflare_worker/tests.

#include "../src/ForkMeshIdentity.h"
#include "../src/IssueStore.h"
#include "../src/RepoHost.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const QString &what)
{
    ++g_checks;
    if (ok) {
        out << "  ok   " << what << '\n';
    } else {
        ++g_failures;
        out << "  FAIL " << what << '\n';
    }
    out.flush();
}

// Pump the shared event loop until `done` is true or the timeout elapses. Every
// actor here — RelayStub, RepoHost, the driver's QNetworkAccessManager and its
// git subprocesses — lives on this one thread, so the driver must never block:
// it cooperatively pumps so the tunnel keeps answering while it waits.
bool pumpUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return true;
}

QByteArray pktLine(const QByteArray &payload)
{
    return QByteArray(QByteArray::number(payload.size() + 4, 16).rightJustified(4, '0')) +
           payload;
}

// ---------------------------------------------------------------------------
// RelayStub: the minimal relay contract the client half of the loop touches.
// ---------------------------------------------------------------------------
class RelayStub : public QObject
{
public:
    explicit RelayStub(QObject *parent = nullptr) : QObject(parent)
    {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &RelayStub::onNewConnection);
    }

    bool listen()
    {
        return m_server->listen(QHostAddress::LocalHost);
    }
    quint16 port() const { return m_server->serverPort(); }
    QString base() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(port());
    }
    QString hostWsUrl(const QString &owner, const QString &repo) const
    {
        return QStringLiteral("ws://127.0.0.1:%1/api/repo/%2/%3/host")
            .arg(port())
            .arg(owner, repo);
    }

    void publish(const QString &owner, const QString &repo)
    {
        m_owner = owner;
        m_repo = repo;
    }
    bool hostLive() const { return m_host && m_hostReady; }
    const QList<QJsonObject> &inbox() const { return m_inbox; }

private:
    struct PendingGit {
        QByteArray buf;
        std::function<void(bool, QByteArray, QString)> done;
    };

    void onNewConnection()
    {
        while (QTcpSocket *sock = m_server->nextPendingConnection()) {
            connect(sock, &QTcpSocket::readyRead, this, [this, sock] { onReadyRead(sock); });
            connect(sock, &QTcpSocket::disconnected, this, [this, sock] {
                if (sock == m_host) {
                    m_host = nullptr;
                    m_hostReady = false;
                }
                m_httpBuf.remove(sock);
                sock->deleteLater();
            });
        }
    }

    void onReadyRead(QTcpSocket *sock)
    {
        if (sock == m_host) {
            m_hostBuf += sock->readAll();
            parseHostFrames();
            return;
        }
        QByteArray &buf = m_httpBuf[sock];
        buf += sock->readAll();
        const int headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray header = buf.left(headerEnd);
        // WebSocket upgrade for the /host tunnel?
        if (header.contains("Upgrade: websocket") || header.contains("upgrade: websocket")) {
            upgradeHost(sock, header);
            m_httpBuf.remove(sock);
            return;
        }
        // Plain HTTP: for POST we must have the whole body before dispatching.
        const int contentLength = headerValue(header, "content-length").toInt();
        const int total = headerEnd + 4 + contentLength;
        if (buf.size() < total)
            return;
        const QByteArray body = buf.mid(headerEnd + 4, contentLength);
        const QByteArray requestLine = header.left(header.indexOf("\r\n"));
        m_httpBuf.remove(sock);
        handleHttp(sock, requestLine, header, body);
    }

    static QByteArray headerValue(const QByteArray &header, const QByteArray &name)
    {
        for (const QByteArray &line : header.split('\n')) {
            const int colon = line.indexOf(':');
            if (colon < 0)
                continue;
            if (line.left(colon).trimmed().toLower() == name)
                return line.mid(colon + 1).trimmed();
        }
        return {};
    }

    void upgradeHost(QTcpSocket *sock, const QByteArray &header)
    {
        const QByteArray key = headerValue(header, "sec-websocket-key");
        const QByteArray accept =
            QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                     QCryptographicHash::Sha1)
                .toBase64();
        sock->write("HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: " +
                    accept + "\r\n\r\n");
        m_host = sock;
        m_hostReady = true;
        m_hostBuf.clear();
    }

    // Server->client text frame (never masked, per RFC 6455).
    void sendHostText(const QByteArray &payload)
    {
        if (!m_host)
            return;
        QByteArray frame;
        frame.append(char(0x81));
        if (payload.size() < 126) {
            frame.append(char(payload.size()));
        } else if (payload.size() <= 0xffff) {
            frame.append(char(126));
            frame.append(char((payload.size() >> 8) & 0xff));
            frame.append(char(payload.size() & 0xff));
        } else {
            frame.append(char(127));
            quint64 len = payload.size();
            for (int i = 7; i >= 0; --i)
                frame.append(char((len >> (8 * i)) & 0xff));
        }
        frame.append(payload);
        m_host->write(frame);
    }

    void parseHostFrames()
    {
        while (m_hostBuf.size() >= 2) {
            const uchar b0 = uchar(m_hostBuf.at(0));
            const uchar b1 = uchar(m_hostBuf.at(1));
            const int opcode = b0 & 0x0f;
            quint64 len = b1 & 0x7f;
            int pos = 2;
            if (len == 126) {
                if (m_hostBuf.size() < pos + 2)
                    return;
                len = (uchar(m_hostBuf.at(pos)) << 8) | uchar(m_hostBuf.at(pos + 1));
                pos += 2;
            } else if (len == 127) {
                if (m_hostBuf.size() < pos + 8)
                    return;
                len = 0;
                for (int i = 0; i < 8; ++i)
                    len = (len << 8) | uchar(m_hostBuf.at(pos + i));
                pos += 8;
            }
            const bool masked = b1 & 0x80;
            QByteArray mask;
            if (masked) {
                if (m_hostBuf.size() < pos + 4)
                    return;
                mask = m_hostBuf.mid(pos, 4);
                pos += 4;
            }
            if (quint64(m_hostBuf.size()) < quint64(pos) + len)
                return;
            QByteArray payload = m_hostBuf.mid(pos, int(len));
            m_hostBuf.remove(0, pos + int(len));
            if (masked)
                for (int i = 0; i < payload.size(); ++i)
                    payload[i] = payload.at(i) ^ mask.at(i % 4);
            if (opcode == 0x8) { // close
                if (m_host)
                    m_host->disconnectFromHost();
                return;
            }
            if (opcode == 0x9) // ping -> ignore (RepoHost only pings us)
                continue;
            if (opcode == 0x1)
                dispatchHostMessage(payload);
        }
    }

    void dispatchHostMessage(const QByteArray &payload)
    {
        const QJsonObject msg = QJsonDocument::fromJson(payload).object();
        const QString type = msg.value("type").toString();
        const QString reqId = msg.value("reqId").toString();
        if (type == "git-chunk") {
            auto it = m_git.find(reqId);
            if (it != m_git.end())
                it->buf += QByteArray::fromBase64(msg.value("data").toString().toLatin1());
        } else if (type == "git-end") {
            auto it = m_git.find(reqId);
            if (it != m_git.end()) {
                const auto pending = *it;
                m_git.erase(it);
                pending.done(msg.value("ok").toBool(), pending.buf,
                             msg.value("error").toString());
            }
        } else if (type == "response") {
            auto it = m_json.find(reqId);
            if (it != m_json.end()) {
                const auto done = *it;
                m_json.erase(it);
                done(msg);
            }
        }
    }

    void sendGitRequest(const QString &op, const QByteArray &body,
                        std::function<void(bool, QByteArray, QString)> done)
    {
        const QString reqId = QStringLiteral("g%1").arg(++m_counter);
        m_git.insert(reqId, {QByteArray(), std::move(done)});
        QJsonObject msg{{"type", "request"}, {"reqId", reqId}, {"op", op}};
        if (!body.isEmpty())
            msg.insert("body", QString::fromLatin1(body.toBase64()));
        sendHostText(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }

    void sendJsonRequest(const QString &op, const QString &path, const QString &ref,
                         std::function<void(QJsonObject)> done)
    {
        const QString reqId = QStringLiteral("r%1").arg(++m_counter);
        m_json.insert(reqId, std::move(done));
        const QJsonObject msg{{"type", "request"},
                              {"reqId", reqId},
                              {"op", op},
                              {"path", path},
                              {"ref", ref}};
        sendHostText(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }

    void writeHttp(QTcpSocket *sock, int status, const QByteArray &contentType,
                   const QByteArray &body)
    {
        if (!sock)
            return;
        QByteArray head =
            "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n"
            "Content-Type: " + contentType + "\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
            "Connection: close\r\n\r\n";
        sock->write(head);
        sock->write(body);
        sock->flush();
        sock->disconnectFromHost();
    }

    void handleHttp(QTcpSocket *sock, const QByteArray &requestLine,
                    const QByteArray &header, const QByteArray &body)
    {
        const QList<QByteArray> parts = requestLine.split(' ');
        const QString method = parts.value(0);
        const QUrl url = QUrl::fromEncoded("http://x" + parts.value(1));
        const QString path = url.path();
        QPointer<QTcpSocket> guard(sock);

        if (path == "/api/repositories") {
            QJsonArray repos;
            if (!m_owner.isEmpty()) {
                repos.append(QJsonObject{{"owner", m_owner},
                                         {"name", m_repo},
                                         {"live", hostLive()}});
            }
            writeHttp(sock, 200, "application/json",
                      QJsonDocument(repos).toJson(QJsonDocument::Compact));
            return;
        }

        // Git smart-HTTP: /<owner>/<repo>/info/refs and /<owner>/<repo>/git-upload-pack
        if (path.endsWith("/info/refs")) {
            sendGitRequest("git-info-refs", QByteArray(),
                           [this, guard](bool ok, QByteArray data, QString) {
                               if (!ok) {
                                   writeHttp(guard, 502, "text/plain", "host error");
                                   return;
                               }
                               const QByteArray out =
                                   pktLine("# service=git-upload-pack\n") + "0000" + data;
                               writeHttp(guard, 200,
                                         "application/x-git-upload-pack-advertisement", out);
                           });
            return;
        }
        if (path.endsWith("/git-upload-pack") && method == "POST") {
            if (!headerValue(header, "content-encoding").isEmpty()) {
                writeHttp(sock, 400, "text/plain", "unexpected content-encoding");
                return;
            }
            sendGitRequest("git-upload-pack", body,
                           [this, guard](bool ok, QByteArray data, QString) {
                               if (!ok) {
                                   writeHttp(guard, 502, "text/plain", "host error");
                                   return;
                               }
                               writeHttp(guard, 200,
                                         "application/x-git-upload-pack-result", data);
                           });
            return;
        }

        // Blob browse: /api/repo/<owner>/<repo>/blob?path=...
        if (path.endsWith("/blob")) {
            const QUrlQuery query(url);
            const QString blobPath = query.queryItemValue("path");
            sendJsonRequest("blob", blobPath, query.queryItemValue("ref"),
                            [this, guard](QJsonObject reply) {
                                writeHttp(guard, reply.value("ok").toBool() ? 200 : 404,
                                          "application/json",
                                          QJsonDocument(reply).toJson(QJsonDocument::Compact));
                            });
            return;
        }

        // Signed-issue inbox POST: verify with the same canonical the worker uses.
        if (path.endsWith("/issues") && method == "POST") {
            const QJsonObject payload = QJsonDocument::fromJson(body).object();
            const QJsonObject eventJson = payload.value("event").toObject();
            const int number = payload.value("number").toInt();
            IssueEvent ev = IssueEvent::fromJson(eventJson);
            ev.body = eventJson.value("body").toString(); // sent for the sig, not stored
            const bool sigOk = ForkMeshIdentity::verifySignature(
                ev.author, ev.sig, IssueStore::canonicalString(number, ev));
            if (!sigOk) {
                writeHttp(sock, 400, "application/json",
                          "{\"ok\":false,\"error\":\"bad_signature\"}");
                return;
            }
            m_inbox.append(payload);
            writeHttp(sock, 200, "application/json", "{\"ok\":true}");
            return;
        }

        writeHttp(sock, 404, "text/plain", "not found");
    }

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_host = nullptr;
    bool m_hostReady = false;
    QByteArray m_hostBuf;
    QHash<QTcpSocket *, QByteArray> m_httpBuf;
    QHash<QString, PendingGit> m_git;
    QHash<QString, std::function<void(QJsonObject)>> m_json;
    int m_counter = 0;
    QString m_owner;
    QString m_repo;
    QList<QJsonObject> m_inbox;
};

// ---------------------------------------------------------------------------
// Driver helpers
// ---------------------------------------------------------------------------
bool runGit(const QString &dir, const QStringList &args, QByteArray *out = nullptr,
            QString *errText = nullptr)
{
    QProcess proc;
    if (!dir.isEmpty())
        proc.setWorkingDirectory(dir);
    proc.start(QStringLiteral("git"), args);
    bool finished = false;
    QObject::connect(&proc,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     [&finished] { finished = true; });
    if (!proc.waitForStarted(5000)) {
        if (errText)
            *errText = QStringLiteral("git failed to start");
        return false;
    }
    // Pump so any in-flight tunnel traffic (RepoHost answering this clone) keeps
    // flowing while git runs — waitForFinished would starve the event loop.
    if (!pumpUntil([&finished] { return finished; }, 30000)) {
        proc.kill();
        if (errText)
            *errText = QStringLiteral("git timed out: ") + args.join(' ');
        return false;
    }
    if (out)
        *out = proc.readAllStandardOutput();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (errText)
            *errText = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}

struct HttpResult {
    int status = 0;
    QByteArray body;
    bool ok = false;
};

HttpResult httpGet(QNetworkAccessManager &nam, const QString &url)
{
    QNetworkReply *reply = nam.get(QNetworkRequest(QUrl(url)));
    HttpResult result;
    bool done = false;
    QObject::connect(reply, &QNetworkReply::finished, [&] { done = true; });
    pumpUntil([&done] { return done; }, 15000);
    result.ok = reply->error() == QNetworkReply::NoError;
    result.status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    reply->deleteLater();
    return result;
}

HttpResult httpPostJson(QNetworkAccessManager &nam, const QString &url,
                        const QByteArray &body)
{
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = nam.post(request, body);
    HttpResult result;
    bool done = false;
    QObject::connect(reply, &QNetworkReply::finished, [&] { done = true; });
    pumpUntil([&done] { return done; }, 15000);
    result.ok = reply->error() == QNetworkReply::NoError;
    result.status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    reply->deleteLater();
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForkMeshTest"));
    QCoreApplication::setApplicationName(QStringLiteral("forkmesh-e2e"));
    QStandardPaths::setTestModeEnabled(true);

    const QString owner = QStringLiteral("e2e");
    const QString repo = QStringLiteral("scratch");

    out << "ForkMesh end-to-end mesh-loop test (publish -> browse -> clone -> "
           "issue -> agent PR -> merge)\n";
    out.flush();

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        err << "cannot create temp dir\n";
        return 1;
    }
    const QString workDir = tmp.filePath(QStringLiteral("work"));
    const QString mirrorDir = tmp.filePath(QStringLiteral("mirror.git"));
    const QString cloneDir = tmp.filePath(QStringLiteral("clone"));

    // --- Scratch repo (the thing we publish) --------------------------------
    QDir().mkpath(workDir);
    QString gitErr;
    bool setup = runGit(QString(), {"init", "-q", "-b", "main", workDir}) &&
                 runGit(workDir, {"config", "user.email", "e2e@forkmesh.test"}) &&
                 runGit(workDir, {"config", "user.name", "E2E"});
    {
        QFile readme(QDir(workDir).filePath(QStringLiteral("README.md")));
        readme.open(QIODevice::WriteOnly);
        readme.write("# scratch\nHello mesh.\n");
        readme.close();
    }
    setup = setup && runGit(workDir, {"add", "README.md"}) &&
            runGit(workDir, {"commit", "-q", "-m", "initial"}) &&
            runGit(QString(),
                   {"clone", "-q", "--bare", workDir, mirrorDir}, nullptr, &gitErr);
    check(setup, QStringLiteral("scratch repo + bare mirror created"));
    if (!setup) {
        err << gitErr << '\n';
        return 1;
    }

    // --- Identity (signs the issue) -----------------------------------------
    ForkMeshIdentity identity;
    check(identity.load() && identity.isValid(),
          QStringLiteral("node identity generated"));

    // --- Relay + host (publish: the node serves its mirror live) ------------
    RelayStub relay;
    check(relay.listen(), QStringLiteral("relay stub listening on loopback"));
    relay.publish(owner, repo);

    RepoHost host(owner, repo, mirrorDir, QUrl(relay.hostWsUrl(owner, repo)));
    QObject::connect(&host, &RepoHost::log, [](const QString &) {});
    host.start();
    check(pumpUntil([&relay] { return relay.hostLive(); }, 8000),
          QStringLiteral("node host tunnel connected to relay"));

    QNetworkAccessManager nam;

    // --- Step 1: publish shows up in /api/repositories ----------------------
    bool cataloged = pumpUntil(
        [&] {
            HttpResult r = httpGet(nam, relay.base() + "/api/repositories");
            if (!r.ok)
                return false;
            const QJsonArray repos = QJsonDocument::fromJson(r.body).array();
            for (const QJsonValue &v : repos) {
                const QJsonObject o = v.toObject();
                if (o.value("owner").toString() == owner &&
                    o.value("name").toString() == repo && o.value("live").toBool())
                    return true;
            }
            return false;
        },
        8000);
    check(cataloged, QStringLiteral("published repo appears live in /api/repositories"));

    // --- Step 2: browse a file over HTTP (relay -> host tunnel) -------------
    {
        HttpResult r = httpGet(
            nam, relay.base() + "/api/repo/" + owner + "/" + repo + "/blob?path=README.md");
        const QJsonObject o = QJsonDocument::fromJson(r.body).object();
        const QString content = o.value("content").toString();
        check(r.status == 200 && content.contains("Hello mesh."),
              QStringLiteral("browse README.md over the relay returns file contents"));
    }

    // --- Step 3: git clone through the relay (info/refs + upload-pack POST) --
    {
        const QString cloneUrl = relay.base() + "/" + owner + "/" + repo;
        QString cloneErr;
        // -c protocol.version=0 keeps the negotiation on the v0 advertisement the
        // host streams from `upload-pack --advertise-refs`.
        const bool cloned = runGit(
            QString(),
            {"-c", "protocol.version=0", "clone", "-q", cloneUrl, cloneDir}, nullptr,
            &cloneErr);
        bool fileOk = false;
        if (cloned) {
            QFile f(QDir(cloneDir).filePath(QStringLiteral("README.md")));
            fileOk = f.open(QIODevice::ReadOnly) && f.readAll().contains("Hello mesh.");
        }
        check(cloned && fileOk,
              QStringLiteral("git clone through relay (info/refs + upload-pack) checks out"));
        if (!cloned)
            err << "  clone error: " << cloneErr << '\n';
    }

    // --- Step 4: file a signed issue via POST .../issues --------------------
    const int issueNumber = 1;
    {
        IssueStore store(workDir, mirrorDir, &identity, QStringLiteral("e2e"));
        IssueEvent ev;
        ev.type = QStringLiteral("open");
        ev.id = QStringLiteral("open-%1").arg(issueNumber);
        ev.title = QStringLiteral("Nightly loop found a bug");
        ev.body = QStringLiteral("The mesh loop should be green.");
        ev = store.makeSignedEvent(issueNumber, ev);

        QJsonObject eventJson = ev.toJson();
        eventJson.insert("body", ev.body);
        const QJsonObject payload{{"owner", owner},
                                  {"repo", repo},
                                  {"number", issueNumber},
                                  {"titleIfNew", ev.title},
                                  {"event", eventJson}};
        HttpResult r = httpPostJson(nam,
                                    relay.base() + "/api/repo/" + owner + "/" + repo +
                                        "/issues",
                                    QJsonDocument(payload).toJson(QJsonDocument::Compact));
        check(r.status == 200 && relay.inbox().size() == 1,
              QStringLiteral("signed issue accepted into the relay inbox"));
    }

    // --- Step 5: owner drains the inbox and commits the issue ---------------
    {
        bool drained = false;
        if (relay.inbox().size() == 1) {
            const QJsonObject item = relay.inbox().first();
            const QJsonObject eventJson = item.value("event").toObject();
            const QString issueDir =
                QDir(workDir).filePath(QStringLiteral("issues/%1").arg(issueNumber));
            QDir().mkpath(issueDir);
            QFile f(issueDir + "/issue.md");
            if (f.open(QIODevice::WriteOnly)) {
                f.write(("# " + eventJson.value("title").toString() + "\n\n" +
                         eventJson.value("body").toString() + "\n")
                            .toUtf8());
                f.close();
                drained = runGit(workDir, {"add", "-A"}) &&
                          runGit(workDir, {"commit", "-q", "-m",
                                           QStringLiteral("Drain issue #%1 from inbox")
                                               .arg(issueNumber)});
            }
        }
        check(drained, QStringLiteral("owner drains inbox and commits the issue"));
    }

    // --- Step 6: run the stub agent -> it produces a PR branch --------------
    const QString branch = QStringLiteral("agent/issue-%1").arg(issueNumber);
    {
        const QString stubAgent = qEnvironmentVariable("FORKMESH_E2E_STUB_AGENT");
        QProcess agent;
        bool finished = false;
        QObject::connect(&agent,
                         QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         [&finished] { finished = true; });
        agent.start(stubAgent,
                    {cloneDir, branch, QString::number(issueNumber)});
        agent.waitForStarted(5000);
        pumpUntil([&finished] { return finished; }, 30000);
        const bool agentOk =
            finished && agent.exitStatus() == QProcess::NormalExit &&
            agent.exitCode() == 0;
        bool branchExists =
            agentOk &&
            runGit(cloneDir, {"rev-parse", "--verify", "-q", "refs/heads/" + branch});
        check(agentOk && branchExists,
              QStringLiteral("stub agent produced a PR branch with a change"));
    }

    // --- Step 7: merge the resulting PR -------------------------------------
    {
        // The PR head lives in the clone; fetch it into the source and merge —
        // exactly the plumbing a maintainer's "merge PR" runs (branch-backed PR).
        QString mergeErr;
        bool merged =
            runGit(workDir, {"fetch", "-q", cloneDir, branch + ":" + branch},
                   nullptr, &mergeErr) &&
            runGit(workDir, {"merge", "-q", "--no-ff", "-m",
                             QStringLiteral("Merge PR for issue #%1").arg(issueNumber),
                             branch},
                   nullptr, &mergeErr);
        bool fixLanded =
            merged && QFile::exists(QDir(workDir).filePath(QStringLiteral("AGENT_FIX.md")));
        check(merged && fixLanded,
              QStringLiteral("PR merges into main and the agent's change lands"));
        if (!merged)
            err << "  merge error: " << mergeErr << '\n';
    }

    host.stop();

    out << "\n" << (g_failures == 0 ? "PASS" : "FAIL") << ": " << (g_checks - g_failures)
        << "/" << g_checks << " checks passed\n";
    out.flush();
    return g_failures == 0 ? 0 : 1;
}
