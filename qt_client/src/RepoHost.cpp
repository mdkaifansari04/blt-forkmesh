#include "RepoHost.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QSet>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

namespace {

constexpr quint64 kMaxWsPayload = 8ull * 1024 * 1024;
constexpr int kMaxBlobBytes = 512 * 1024;
constexpr int kMaxImageDiffBytes = 2 * 1024 * 1024;
constexpr int kGitTimeoutMs = 5000;
// A NAT rebind, sleep/wake, or dropped-packet blackhole can leave the local
// QTcpSocket in ConnectedState forever: writes into the OS buffer keep
// "succeeding" with no peer to receive them, so neither `disconnected` nor
// `errorOccurred` ever fires and RepoHost never reconnects, even though the
// relay closed its side and purged host_presence within seconds (adhoc
// #140 — node app shows connected/online while the website shows offline).
// Anything received (handshake bytes, our own ping's pong, a request) proves
// the path is alive, so treat a stretch with no inbound bytes at all as dead.
constexpr qint64 kStaleRxMs = 70000; // ~2.8x the 25s ping interval

QByteArray randomKey()
{
    QByteArray key(16, Qt::Uninitialized);
    for (int i = 0; i < key.size(); ++i)
        key[i] = char(QRandomGenerator::global()->bounded(256));
    return key.toBase64();
}

QString wsPath(const QUrl &url)
{
    QString path = url.path().isEmpty() ? "/" : url.path();
    if (url.hasQuery())
        path += "?" + url.query();
    return path;
}

// Reject paths that try to escape the repository tree.
bool isSafeRepoPath(const QString &path)
{
    if (path.contains(QChar('\0')) || path.startsWith('/'))
        return false;
    for (const QString &segment : path.split('/')) {
        if (segment == "..")
            return false;
    }
    return true;
}

QString displayBranchNameForRef(const QString &ref)
{
    if (ref.startsWith(QLatin1String("refs/heads/")))
        return ref.mid(QStringLiteral("refs/heads/").size());
    if (!ref.startsWith(QLatin1String("refs/remotes/")))
        return QString();
    const QString remotePath = ref.mid(QStringLiteral("refs/remotes/").size());
    const int slash = remotePath.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return QString();
    const QString name = remotePath.mid(slash + 1);
    if (name.isEmpty() || name == QLatin1String("HEAD"))
        return QString();
    return name;
}

// A release asset's content address: exactly 64 lowercase hex characters.
bool isSha256Hex(const QString &value)
{
    if (value.size() != 64)
        return false;
    for (const QChar ch : value) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            return false;
    }
    return true;
}

// Run git in the mirror, returning false on failure. On failure, errText (if
// given) gets a short snippet of git's stderr for surfacing to the browser.
bool runGit(const QString &mirrorPath, const QStringList &args, QByteArray &output,
            QString *errText = nullptr)
{
    QProcess process;
    process.start("git", QStringList{"-C", mirrorPath} + args);
    if (!process.waitForFinished(kGitTimeoutMs)) {
        if (errText)
            *errText = QStringLiteral("git timed out");
        return false;
    }
    output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errText)
            *errText =
                QString::fromUtf8(process.readAllStandardError()).trimmed().left(200);
        return false;
    }
    return true;
}

// Free (non-member) equivalents of RepoHost::baseRef/branchRefCandidates/
// refForBranch/buildBlobReply, parameterized on `mirrorPath` instead of reading
// `this`. A blob fetch chains several of these git spawns back to back (issue:
// StallWatchdog caught it blocking the GUI thread for 500-700ms), so it runs on
// a worker thread (RepoHost::runOffThread) — these must not touch `this`, since
// un-hosting a repo mid-fetch can deleteLater() the RepoHost while that thread
// is still running. The RepoHost member functions below delegate to these.
QString baseRefFor(const QString &mirrorPath)
{
    QByteArray output;
    if (runGit(mirrorPath, {"rev-parse", "--verify", "-q", "HEAD"}, output) &&
        !output.trimmed().isEmpty())
        return QStringLiteral("HEAD");
    for (const QString &refsRoot :
         {QStringLiteral("refs/heads/"), QStringLiteral("refs/remotes/")}) {
        if (runGit(mirrorPath,
                   {"for-each-ref", "--format=%(refname)", "--count=1", refsRoot},
                   output)) {
            const QString ref = QString::fromUtf8(output).trimmed();
            if (!ref.isEmpty())
                return ref;
        }
    }
    return QString();
}

QStringList branchRefCandidatesFor(const QString &mirrorPath, const QString &branch)
{
    const QString raw = branch.trimmed();
    if (raw.isEmpty() || raw.contains(QChar('\0')))
        return {};
    if (raw.startsWith(QLatin1String("refs/heads/")) ||
        raw.startsWith(QLatin1String("refs/remotes/")))
        return {raw};

    QStringList refs{QStringLiteral("refs/heads/%1").arg(raw),
                     QStringLiteral("refs/remotes/%1").arg(raw)};
    QByteArray output;
    if (runGit(mirrorPath, {"for-each-ref", "--format=%(refname)", "refs/remotes/"},
               output)) {
        for (const QByteArray &line : output.split('\n')) {
            const QString ref = QString::fromUtf8(line).trimmed();
            if (!ref.isEmpty() && displayBranchNameForRef(ref) == raw &&
                !refs.contains(ref))
                refs.append(ref);
        }
    }
    return refs;
}

QString refForBranchIn(const QString &mirrorPath, const QString &branch)
{
    if (branch.trimmed().isEmpty())
        return baseRefFor(mirrorPath);
    QByteArray output;
    for (const QString &ref : branchRefCandidatesFor(mirrorPath, branch)) {
        if (runGit(mirrorPath,
                   {"rev-parse", "--verify", "-q", ref + QStringLiteral("^{commit}")},
                   output))
            return QString::fromUtf8(output).trimmed();
    }
    return QString();
}

QJsonObject blobReplyFor(const QString &mirrorPath, const QString &path,
                         const QString &branch)
{
    if (path.isEmpty())
        return {{"ok", false}, {"error", "not_found"}};
    const QString ref = refForBranchIn(mirrorPath, branch);
    if (ref.isEmpty())
        return {{"ok", false}, {"error", "not_found"}};
    QByteArray output;
    QString gitErr;
    if (!runGit(mirrorPath, {"cat-file", "-p", ref + ":" + path}, output, &gitErr))
        return {{"ok", false}, {"error", gitErr.isEmpty() ? "not_found" : gitErr}};

    bool truncated = false;
    if (output.size() > kMaxBlobBytes) {
        output = output.left(kMaxBlobBytes);
        truncated = true;
    }

    QJsonObject reply{{"ok", true}, {"size", double(output.size())},
                      {"truncated", truncated}};
    if (output.contains('\0')) {
        reply.insert("encoding", "base64");
        reply.insert("content", QString::fromLatin1(output.toBase64()));
    } else {
        reply.insert("encoding", "utf8");
        reply.insert("content", QString::fromUtf8(output));
    }
    return reply;
}

// Repo-scoped search over a bare mirror (issue #360). One `git grep` at the tip
// ref classifies every hit by path: issues/<N>/… and pulls/<N>/… fold into that
// issue/PR (deduped by number, titled from the record's frontmatter); everything
// else is a code match. Fixed-string, case-insensitive; caps every bucket hard
// so the reply can't balloon into a huge tunnel payload. Runs off the GUI thread
// (git grep can be slow on a big tree), so it must not touch `this`.
QString frontMatterTitle(const QString &mirrorPath, const QString &ref,
                         const QString &relPath)
{
    QByteArray output;
    if (!runGit(mirrorPath, {"cat-file", "-p", ref + ":" + relPath}, output))
        return QString();
    for (const QByteArray &line : output.left(4096).split('\n')) {
        const QString text = QString::fromUtf8(line);
        if (text.startsWith(QLatin1String("title:")))
            return text.mid(6).trimmed();
    }
    return QString();
}

QJsonObject searchReplyFor(const QString &mirrorPath, const QString &rawQuery)
{
    constexpr int kMaxCode = 60;
    constexpr int kMaxIssues = 30;
    constexpr int kMaxPulls = 30;

    const QString query = rawQuery.trimmed();
    if (query.size() < 2 || query.contains(QChar('\0')))
        return {{"ok", false}, {"error", "bad_query"}};
    const QString ref = baseRefFor(mirrorPath);
    if (ref.isEmpty())
        return {{"ok", true}, {"issues", QJsonArray()}, {"pulls", QJsonArray()},
                {"code", QJsonArray()}};

    QByteArray output;
    // Exit 1 (no matches) makes runGit "fail" with empty stderr; treat that as an
    // empty result rather than an error by running the process directly.
    {
        QProcess process;
        process.start("git", {"-C", mirrorPath, "grep", "-n", "-I", "-i", "-F",
                              "-e", query, ref});
        if (!process.waitForFinished(8000)) {
            process.kill();
            process.waitForFinished(1000);
            return {{"ok", false}, {"error", "search_timeout"}};
        }
        output = process.readAllStandardOutput();
    }

    QJsonArray code;
    QJsonArray issues;
    QJsonArray pulls;
    QSet<int> seenIssue;
    QSet<int> seenPull;
    static const QRegularExpression rowRe(QStringLiteral("^(.+?):(\\d+):(.*)$"));
    static const QRegularExpression numberedRe(
        QStringLiteral("^(issues|pulls)/(\\d+)/"));
    const QString prefix = ref + QLatin1Char(':');

    for (const QByteArray &raw : output.split('\n')) {
        if (code.size() >= kMaxCode && issues.size() >= kMaxIssues &&
            pulls.size() >= kMaxPulls)
            break;
        QString line = QString::fromUtf8(raw);
        if (line.startsWith(prefix))
            line = line.mid(prefix.size());
        const QRegularExpressionMatch m = rowRe.match(line);
        if (!m.hasMatch())
            continue;
        const QString path = m.captured(1);
        const int lineNo = m.captured(2).toInt();
        const QString text = m.captured(3).trimmed().left(200);

        const QRegularExpressionMatch nm = numberedRe.match(path);
        if (nm.hasMatch()) {
            const int number = nm.captured(2).toInt();
            const bool isIssue = nm.captured(1) == QLatin1String("issues");
            QSet<int> &seen = isIssue ? seenIssue : seenPull;
            if (seen.contains(number))
                continue;
            QJsonArray &bucket = isIssue ? issues : pulls;
            if (bucket.size() >= (isIssue ? kMaxIssues : kMaxPulls))
                continue;
            seen.insert(number);
            const QString titleFile =
                isIssue ? QStringLiteral("issues/%1/issue.md").arg(number)
                        : QStringLiteral("pulls/%1/pull.md").arg(number);
            bucket.append(QJsonObject{
                {"number", number},
                {"title", frontMatterTitle(mirrorPath, ref, titleFile)},
                {"snippet", text}});
        } else if (code.size() < kMaxCode) {
            code.append(QJsonObject{
                {"path", path}, {"line", lineNo}, {"text", text}});
        }
    }
    return {{"ok", true}, {"issues", issues}, {"pulls", pulls}, {"code", code}};
}

// Count the numbered sub-directories (1/, 2/, …) under a top-level folder such
// as issues/, pulls/ or discussions/. Each maps to one filed item, so this is
// the tally the website shows in its tab badges. A missing folder counts as 0.
int countNumberedDirs(const QString &mirrorPath, const QString &ref,
                      const QString &dir)
{
    QByteArray output;
    if (!runGit(mirrorPath, {"ls-tree", "-z", ref + ":" + dir}, output))
        return 0; // folder absent -> nothing filed yet
    static const QRegularExpression numericName(QStringLiteral("^[0-9]+$"));
    int count = 0;
    for (const QByteArray &record : output.split('\0')) {
        if (record.isEmpty())
            continue;
        const int tab = record.indexOf('\t');
        if (tab < 0)
            continue;
        const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
        if (meta.size() < 2 || meta.at(1) != "tree")
            continue;
        if (numericName.match(QString::fromUtf8(record.mid(tab + 1))).hasMatch())
            ++count;
    }
    return count;
}

QString imageMimeForPath(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".png")))
        return QStringLiteral("image/png");
    if (lower.endsWith(QStringLiteral(".jpg")) || lower.endsWith(QStringLiteral(".jpeg")))
        return QStringLiteral("image/jpeg");
    if (lower.endsWith(QStringLiteral(".gif")))
        return QStringLiteral("image/gif");
    if (lower.endsWith(QStringLiteral(".webp")))
        return QStringLiteral("image/webp");
    if (lower.endsWith(QStringLiteral(".bmp")))
        return QStringLiteral("image/bmp");
    if (lower.endsWith(QStringLiteral(".ico")))
        return QStringLiteral("image/x-icon");
    return QString();
}

} // namespace

RepoHost::RepoHost(const QString &owner, const QString &name,
                   const QString &mirrorPath, const QUrl &url, QObject *parent)
    : QObject(parent), m_url(url), m_owner(owner), m_name(name),
      m_mirrorPath(mirrorPath)
{
    m_reconnect = new QTimer(this);
    m_reconnect->setSingleShot(true);
    connect(m_reconnect, &QTimer::timeout, this, [this] { connectSocket(); });

    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(25000);
    connect(m_pingTimer, &QTimer::timeout, this, [this] {
        // If nothing at all has come back since before the last ping went out,
        // the socket is a zombie (see kStaleRxMs) — the relay already dropped
        // us, so reconnect instead of writing another ping into the void.
        if (m_lastRx && QDateTime::currentMSecsSinceEpoch() - m_lastRx > kStaleRxMs) {
            emit log("Host: connection to relay went stale for " + m_owner + "/" +
                      m_name + ", reconnecting.");
            if (m_socket)
                m_socket->abort();
            scheduleReconnect();
            return;
        }
        // A WebSocket ping keeps the transport alive, while the application
        // heartbeat lets the hibernating Worker refresh this repository's D1
        // presence row. Without the text heartbeat an idle-but-connected host
        // fell out of the online catalog after ten minutes.
        sendControlFrame(0x9);
        sendText(QByteArrayLiteral("{\"type\":\"heartbeat\"}"));
    });
}

void RepoHost::setTokenProvider(std::function<QString()> provider)
{
    m_tokenProvider = std::move(provider);
}

void RepoHost::start()
{
    m_stopping = false;
    connectSocket();
}

void RepoHost::stop()
{
    m_stopping = true;
    m_reconnect->stop();
    m_pingTimer->stop();
    // Tear down any in-flight receive-pack pushes: kill the process (its
    // deleteLater still fires) and free the tracking struct so nothing leaks
    // when hosting stops mid-push.
    for (ReceiveJob *job : std::as_const(m_receiveJobs)) {
        if (job->process)
            job->process->kill();
        delete job;
    }
    m_receiveJobs.clear();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_wsReady = false;
}

void RepoHost::connectSocket()
{
    if (m_stopping)
        return;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_wsReady = false;
    m_readBuffer.clear();
    m_lastRx = QDateTime::currentMSecsSinceEpoch();

    const bool secure = m_url.scheme() == "wss";
    m_socket = secure ? new QSslSocket(this) : new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &RepoHost::onReadyRead);
    connect(m_socket, &QTcpSocket::connected, this, [this] {
        if (!qobject_cast<QSslSocket *>(m_socket))
            onTransportReady();
    });
    if (auto *ssl = qobject_cast<QSslSocket *>(m_socket))
        connect(ssl, &QSslSocket::encrypted, this, &RepoHost::onTransportReady);
    connect(m_socket, &QTcpSocket::disconnected, this, [this] { scheduleReconnect(); });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] { scheduleReconnect(); });

    const int port = m_url.port(secure ? 443 : 80);
    if (auto *ssl = qobject_cast<QSslSocket *>(m_socket))
        ssl->connectToHostEncrypted(m_url.host(), port);
    else
        m_socket->connectToHost(m_url.host(), port);
}

void RepoHost::onTransportReady()
{
    sendHandshake();
}

void RepoHost::sendHandshake()
{
    m_wsKey = randomKey();
    QByteArray host = m_url.host().toUtf8();
    if (m_url.port() > 0)
        host += ":" + QByteArray::number(m_url.port());
    // Append a freshly-signed host-auth token so the relay accepts the upgrade.
    QString path = wsPath(m_url);
    if (m_tokenProvider) {
        const QString token = m_tokenProvider();
        if (!token.isEmpty())
            path += (m_url.hasQuery() ? QLatin1Char('&') : QLatin1Char('?')) + token;
    }
    const QByteArray request =
        "GET " + path.toUtf8() + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: " + m_wsKey + "\r\n\r\n";
    m_socket->write(request);
}

void RepoHost::onReadyRead()
{
    m_lastRx = QDateTime::currentMSecsSinceEpoch();
    m_readBuffer += m_socket->readAll();
    if (!m_wsReady) {
        const int headerEnd = m_readBuffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray header = m_readBuffer.left(headerEnd);
        m_readBuffer.remove(0, headerEnd + 4);
        if (!header.startsWith("HTTP/1.1 101") && !header.startsWith("HTTP/1.0 101")) {
            scheduleReconnect();
            return;
        }
        m_wsReady = true;
        m_pingTimer->start();
        emit log("Host: serving " + m_owner + "/" + m_name + " live to the web.");
    }

    while (m_readBuffer.size() >= 2) {
        const uchar b0 = uchar(m_readBuffer.at(0));
        const uchar b1 = uchar(m_readBuffer.at(1));
        const int opcode = b0 & 0x0f;
        quint64 len = b1 & 0x7f;
        int pos = 2;
        if (len == 126) {
            if (m_readBuffer.size() < pos + 2)
                return;
            len = (uchar(m_readBuffer.at(pos)) << 8) | uchar(m_readBuffer.at(pos + 1));
            pos += 2;
        } else if (len == 127) {
            if (m_readBuffer.size() < pos + 8)
                return;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | uchar(m_readBuffer.at(pos + i));
            pos += 8;
        }
        if (len > kMaxWsPayload) {
            m_socket->disconnectFromHost();
            return;
        }
        const bool masked = b1 & 0x80;
        QByteArray mask;
        if (masked) {
            if (m_readBuffer.size() < pos + 4)
                return;
            mask = m_readBuffer.mid(pos, 4);
            pos += 4;
        }
        if (m_readBuffer.size() < pos + int(len))
            return;
        QByteArray payload = m_readBuffer.mid(pos, int(len));
        m_readBuffer.remove(0, pos + int(len));
        if (masked) {
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = payload.at(i) ^ mask.at(i % 4);
        }
        if (opcode == 0x9) { // ping -> pong
            sendControlFrame(0xA, payload);
            continue;
        }
        if (opcode == 0xA) // pong
            continue;
        if (opcode == 0x8) {
            m_socket->disconnectFromHost();
            return;
        }
        if (opcode == 0x1)
            handleFrame(payload);
    }
}

void RepoHost::sendText(const QByteArray &payload)
{
    if (!m_wsReady || !m_socket)
        return;
    QByteArray frame;
    frame.append(char(0x81));
    if (payload.size() < 126) {
        frame.append(char(0x80 | payload.size()));
    } else if (payload.size() <= 0xffff) {
        frame.append(char(0x80 | 126));
        frame.append(char((payload.size() >> 8) & 0xff));
        frame.append(char(payload.size() & 0xff));
    } else {
        frame.append(char(0x80 | 127));
        quint64 length = payload.size();
        for (int i = 7; i >= 0; --i)
            frame.append(char((length >> (8 * i)) & 0xff));
    }
    QByteArray mask(4, Qt::Uninitialized);
    for (int i = 0; i < 4; ++i)
        mask[i] = char(QRandomGenerator::global()->bounded(256));
    frame.append(mask);
    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i)
        masked[i] = masked.at(i) ^ mask.at(i % 4);
    frame.append(masked);
    m_socket->write(frame);
}

void RepoHost::sendControlFrame(int opcode, const QByteArray &payload)
{
    if (!m_socket || !m_wsReady || payload.size() > 125)
        return;
    QByteArray frame;
    frame.append(char(0x80 | (opcode & 0x0f)));
    frame.append(char(0x80 | payload.size()));
    QByteArray mask(4, Qt::Uninitialized);
    for (int i = 0; i < 4; ++i)
        mask[i] = char(QRandomGenerator::global()->bounded(256));
    frame.append(mask);
    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i)
        masked[i] = masked.at(i) ^ mask.at(i % 4);
    frame.append(masked);
    m_socket->write(frame);
}

void RepoHost::handleFrame(const QByteArray &payload)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return;
    const QJsonObject obj = doc.object();
    // git push (issue #358): the pushed pack streams to us as git-req-chunk
    // messages that follow the initial "request", then git-req-end. These are
    // relayed to the running receive-pack's stdin (never buffered whole).
    const QString type = obj.value("type").toString();
    if (type == "git-req-chunk") {
        feedReceivePack(
            obj.value("reqId").toString(),
            QByteArray::fromBase64(obj.value("data").toString().toLatin1()));
        return;
    }
    if (type == "git-req-end") {
        finishReceiveInput(obj.value("reqId").toString());
        return;
    }
    handleRequest(obj);
}

void RepoHost::handleRequest(const QJsonObject &request)
{
    if (request.value("type").toString() != "request")
        return;
    const QString reqId = request.value("reqId").toString();
    const QString op = request.value("op").toString();
    const QString path = request.value("path").toString();
    const QString branch = request.value("ref").toString();
    if (reqId.isEmpty())
        return;

    const bool clone = op == "git-upload-pack";
    emit requestServed(m_owner, m_name, clone);

    // Surface every served request in the node log so the operator can see their
    // node working (issue #297). Describe the operation in plain terms; clone and
    // ref-advertisement requests are the git smart-HTTP clone/fetch handshake.
    QString action;
    if (clone)
        action = QStringLiteral("clone/fetch (upload-pack)");
    else if (op == "git-info-refs")
        action = QStringLiteral("clone handshake (ref advertisement)");
    else if (op == "git-receive-info-refs")
        action = QStringLiteral("push handshake (ref advertisement)");
    else if (op == "git-receive-pack")
        action = QStringLiteral("push (receive-pack)");
    else if (op == "tree")
        action = path.isEmpty() ? QStringLiteral("browse tree (root)")
                                : QStringLiteral("browse tree '%1'").arg(path);
    else if (op == "blob")
        action = QStringLiteral("view file '%1'").arg(path);
    else if (op == "raw-blob")
        action = QStringLiteral("stream raw file '%1'").arg(path);
    else if (op == "commits")
        action = QStringLiteral("commit history");
    else if (op == "commit")
        action = QStringLiteral("view commit %1").arg(path);
    else if (op == "branches")
        action = QStringLiteral("list branches");
    else if (op == "search")
        action = QStringLiteral("search '%1'").arg(path);
    else if (op == "release-blob")
        action = QStringLiteral("download release asset %1").arg(path);
    else
        action = op.isEmpty() ? QStringLiteral("request") : op;
    emit log(QStringLiteral("Host: served %1 for %2/%3.")
                 .arg(action, m_owner, m_name));

    // A clone is two requests: info/refs (ref advertisement) then the
    // git-upload-pack POST that negotiates `want <oid>` against those refs. On a
    // live mirror a `fetch --prune` can advance a ref between the two, so by the
    // time the POST lands the wanted OID is no longer an advertised tip and
    // upload-pack aborts with "fatal: git upload-pack: not our ref <oid>",
    // failing the clone. Allow wants for OIDs that were a tip or are still
    // reachable from a ref (the moved commit is an ancestor of the new tip after
    // a fast-forward fetch) so the concurrent-fetch race resolves instead of
    // 500-ing the client.
    static const QStringList kUploadPackConfig = {
        "-c", "uploadpack.allowTipSHA1InWant=true",
        "-c", "uploadpack.allowReachableSHA1InWant=true"};

    // Git smart-HTTP clone: stream the packfile/advertisement back in chunks.
    if (op == "git-info-refs") {
        runGitStream(reqId,
                     kUploadPackConfig +
                         QStringList{"upload-pack", "--stateless-rpc",
                                     "--advertise-refs", m_mirrorPath},
                     QByteArray());
        return;
    }
    if (op == "git-upload-pack") {
        const QByteArray body =
            QByteArray::fromBase64(request.value("body").toString().toLatin1());
        runGitStream(reqId,
                     kUploadPackConfig +
                         QStringList{"upload-pack", "--stateless-rpc",
                                     m_mirrorPath},
                     body);
        return;
    }
    // git push (issue #358): advertise refs for receive-pack, then accept the
    // streamed pack. A successful receive-pack fires the mirror's post-receive
    // hook, which spools a .push event; MainWindow::scanActionSpool then
    // re-attests the integrity pin so the refs we now serve stay verifiable.
    if (op == "git-receive-info-refs") {
        runGitStream(reqId,
                     {"receive-pack", "--stateless-rpc", "--advertise-refs",
                      m_mirrorPath},
                     QByteArray());
        return;
    }
    if (op == "git-receive-pack") {
        startReceivePack(reqId);
        return;
    }
    // Release asset download: stream the bytes of a content-addressed blob from
    // the node's release store (co-located with the bare mirror, never in git).
    // Reuses the git-chunk/git-end streaming protocol so arbitrarily large
    // binaries flow without buffering the whole file. `path` carries the sha256.
    if (op == "release-blob") {
        streamReleaseBlob(reqId, path);
        return;
    }
    if (op == "raw-blob") {
        streamRawBlob(reqId, path, branch);
        return;
    }
    // "blob" is the op the StallWatchdog caught freezing the GUI thread: refForBranch
    // (baseRef + a rev-parse per candidate ref) plus a cat-file -p is several
    // synchronous git spawns deep for one request. Run that chain on a worker thread
    // and reply once it lands back here; the other ops below are a single fast git
    // call each and stay synchronous. blobReplyFor takes mirrorPath by value instead
    // of reading `this` so it stays safe even if the repo is un-hosted (RepoHost
    // deleteLater()'d) mid-fetch.
    if (op == "blob") {
        if (!isSafeRepoPath(path)) {
            QJsonObject reply{{"ok", false}, {"error", "bad_path"}};
            reply.insert("type", "response");
            reply.insert("reqId", reqId);
            reply.insert("op", op);
            reply.insert("path", path);
            sendText(QJsonDocument(reply).toJson(QJsonDocument::Compact));
            return;
        }
        const QString mirrorPath = m_mirrorPath;
        runOffThread(
            [mirrorPath, path, branch] { return blobReplyFor(mirrorPath, path, branch); },
            [this, reqId, op, path](QJsonObject reply) {
                reply.insert("type", "response");
                reply.insert("reqId", reqId);
                reply.insert("op", op);
                reply.insert("path", path);
                sendText(QJsonDocument(reply).toJson(QJsonDocument::Compact));
            });
        return;
    }

    // Repo-scoped search (issue #360): `path` carries the query, not a repo path,
    // so it bypasses the isSafeRepoPath gate below and runs off-thread (git grep
    // can be slow) like "blob" does.
    if (op == "search") {
        const QString mirrorPath = m_mirrorPath;
        const QString query = path;
        runOffThread(
            [mirrorPath, query] { return searchReplyFor(mirrorPath, query); },
            [this, reqId, op, path](QJsonObject reply) {
                reply.insert("type", "response");
                reply.insert("reqId", reqId);
                reply.insert("op", op);
                reply.insert("path", path);
                sendText(QJsonDocument(reply).toJson(QJsonDocument::Compact));
            });
        return;
    }

    QJsonObject reply;
    if (!isSafeRepoPath(path)) {
        reply = QJsonObject{{"ok", false}, {"error", "bad_path"}};
    } else if (op == "tree") {
        reply = buildTreeReply(path, branch);
    } else if (op == "commits") {
        reply = buildCommitsReply(branch);
    } else if (op == "commit") {
        reply = buildCommitReply(path); // path carries the commit hash
    } else if (op == "branches") {
        reply = buildBranchesReply();
    } else {
        reply = QJsonObject{{"ok", false}, {"error", "bad_op"}};
    }

    reply.insert("type", "response");
    reply.insert("reqId", reqId);
    reply.insert("op", op);
    reply.insert("path", path);
    sendText(QJsonDocument(reply).toJson(QJsonDocument::Compact));
}

void RepoHost::runGitStream(const QString &reqId, const QStringList &args,
                            const QByteArray &input)
{
    auto *process = new QProcess(this);
    process->setProgram("git");
    process->setArguments(args);
    // Stream stdout as it arrives so large packfiles don't block the UI or
    // exceed the relay's per-message size limit.
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process, reqId] {
                sendGitChunk(reqId, process->readAllStandardOutput());
            });
    connect(process, &QProcess::finished, this,
            [this, process, reqId](int exitCode, QProcess::ExitStatus status) {
                sendGitChunk(reqId, process->readAllStandardOutput());
                const bool ok =
                    exitCode == 0 && status == QProcess::NormalExit;
                QString error;
                if (!ok) {
                    // Surface the real git diagnostic (e.g. "fatal: bad object"),
                    // not a generic code, so a broken mirror is debuggable.
                    error = QString::fromUtf8(
                                process->readAllStandardError()).trimmed();
                    if (error.isEmpty())
                        error = QStringLiteral("git_failed");
                }
                sendGitEnd(reqId, ok, error);
                process->deleteLater();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, reqId] {
                sendGitEnd(reqId, false,
                           QStringLiteral("git_error: ") + process->errorString());
                process->deleteLater();
            });
    // Feed the request body on stdin only AFTER the process is running. Writing
    // (and closing the write channel) while QProcess is still in Starting state
    // can race the deferred startup flush: the pipe gets closed before the
    // buffered request reaches git, so `upload-pack --stateless-rpc` reads an
    // empty request and exits non-zero immediately — surfacing to clients as a
    // 502 "host temporarily unavailable" even though the mirror is healthy.
    connect(process, &QProcess::started, this, [process, input] {
        if (!input.isEmpty())
            process->write(input);
        process->closeWriteChannel();
    });
    process->start();
}

void RepoHost::startReceivePack(const QString &reqId)
{
    // Spawn receive-pack for a push whose stdin (the pack) is streamed in
    // separately via git-req-chunk. stdout (the small report-status reply)
    // streams back over the same git-chunk/git-end path a clone uses. Unlike
    // runGitStream we do NOT close stdin here — finishReceiveInput does that
    // once git-req-end arrives, so the pack can be arbitrarily large without
    // ever being buffered whole on either side.
    auto *job = new ReceiveJob;
    auto *process = new QProcess(this);
    job->process = process;
    m_receiveJobs.insert(reqId, job);
    process->setProgram("git");
    process->setArguments({"receive-pack", "--stateless-rpc", m_mirrorPath});
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process, reqId] {
                sendGitChunk(reqId, process->readAllStandardOutput());
            });
    connect(process, &QProcess::finished, this,
            [this, process, reqId](int exitCode, QProcess::ExitStatus status) {
                sendGitChunk(reqId, process->readAllStandardOutput());
                const bool ok =
                    exitCode == 0 && status == QProcess::NormalExit;
                QString error;
                if (!ok) {
                    error = QString::fromUtf8(
                                process->readAllStandardError()).trimmed();
                    if (error.isEmpty())
                        error = QStringLiteral("git_failed");
                }
                sendGitEnd(reqId, ok, error);
                delete m_receiveJobs.take(reqId);
                process->deleteLater();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, reqId] {
                sendGitEnd(reqId, false,
                           QStringLiteral("git_error: ") + process->errorString());
                delete m_receiveJobs.take(reqId);
                process->deleteLater();
            });
    // Flush any stdin buffered before the process was running (see runGitStream
    // for why writing while Starting is unsafe), then close it if git-req-end
    // already arrived.
    connect(process, &QProcess::started, this, [this, reqId] {
        ReceiveJob *j = m_receiveJobs.value(reqId);
        if (!j)
            return;
        j->started = true;
        if (!j->pendingInput.isEmpty()) {
            j->process->write(j->pendingInput);
            j->pendingInput.clear();
        }
        if (j->endReceived)
            j->process->closeWriteChannel();
    });
    process->start();
}

void RepoHost::feedReceivePack(const QString &reqId, const QByteArray &data)
{
    ReceiveJob *job = m_receiveJobs.value(reqId);
    if (!job || data.isEmpty())
        return;
    if (job->started)
        job->process->write(data);
    else
        job->pendingInput.append(data);
}

void RepoHost::finishReceiveInput(const QString &reqId)
{
    ReceiveJob *job = m_receiveJobs.value(reqId);
    if (!job)
        return;
    if (job->started)
        job->process->closeWriteChannel();
    else
        job->endReceived = true;
}

void RepoHost::streamReleaseBlob(const QString &reqId, const QString &sha256)
{
    // Release binaries are NOT committed to git (issue #304). They live in a
    // content-addressed store co-located with the bare mirror — the same hash
    // the signed release manifest records — so a blob is self-verifying and
    // shared once across every release/asset that references it.
    const QString hash = sha256.trimmed().toLower();
    if (!isSha256Hex(hash)) {
        sendGitEnd(reqId, false, QStringLiteral("bad_hash"));
        return;
    }
    const QString blobPath =
        QDir(m_mirrorPath)
            .filePath(QStringLiteral("forkmesh-releases/sha256/%1/%2/data")
                          .arg(hash.left(2), hash));
    QFile file(blobPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        sendGitEnd(reqId, false, QStringLiteral("not_found"));
        return;
    }
    // Stream in chunks (sendGitChunk re-splits to stay under the relay's frame
    // cap) so even a multi-hundred-MB binary never loads fully into memory.
    constexpr qint64 kRead = 256 * 1024;
    while (!file.atEnd()) {
        const QByteArray piece = file.read(kRead);
        if (piece.isEmpty())
            break;
        sendGitChunk(reqId, piece);
    }
    sendGitEnd(reqId, true, QString());
}

void RepoHost::streamRawBlob(const QString &reqId, const QString &path, const QString &branch)
{
    if (path.isEmpty() || !isSafeRepoPath(path)) {
        sendGitEnd(reqId, false, QStringLiteral("bad_path"));
        return;
    }
    const QString ref = refForBranch(branch);
    if (ref.isEmpty()) {
        sendGitEnd(reqId, false, QStringLiteral("not_found"));
        return;
    }
    const QString object = ref + ":" + path;
    QByteArray typeOutput;
    if (!runGit(m_mirrorPath, {"cat-file", "-t", object}, typeOutput) ||
        QString::fromUtf8(typeOutput).trimmed() != QStringLiteral("blob")) {
        sendGitEnd(reqId, false, QStringLiteral("not_found"));
        return;
    }
    runGitStream(reqId, {"-C", m_mirrorPath, "cat-file", "-p", ref + ":" + path}, QByteArray());
}

void RepoHost::sendGitChunk(const QString &reqId, const QByteArray &data)
{
    if (data.isEmpty())
        return;
    // Keep each WebSocket message comfortably under the relay's ~1 MiB cap.
    constexpr int kChunk = 256 * 1024;
    for (int i = 0; i < data.size(); i += kChunk) {
        const QByteArray piece = data.mid(i, kChunk);
        sendText(QJsonDocument(QJsonObject{
                     {"type", "git-chunk"},
                     {"reqId", reqId},
                     {"data", QString::fromLatin1(piece.toBase64())}})
                     .toJson(QJsonDocument::Compact));
    }
}

void RepoHost::sendGitEnd(const QString &reqId, bool ok, const QString &error)
{
    QJsonObject message{{"type", "git-end"}, {"reqId", reqId}, {"ok", ok}};
    if (!error.isEmpty())
        message.insert("error", error);
    sendText(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

void RepoHost::runOffThread(std::function<QJsonObject()> work,
                            std::function<void(QJsonObject)> apply)
{
    // Guard is constructed here (GUI thread) and only ever read back on the GUI
    // thread inside the queued lambda below, so QPointer's auto-null-on-destroy
    // never races: the worker thread never touches it, and never touches `this`.
    QPointer<RepoHost> guard(this);
    QThread *worker = QThread::create(
        [work = std::move(work), apply = std::move(apply), guard]() mutable {
            QJsonObject result = work();
            // qApp, not `this`: un-hosting can deleteLater() this RepoHost while
            // this thread is still running, and invokeMethod's context must be
            // alive at the moment it's called (not just when the event is later
            // delivered). qApp always is; `guard` gates the actual delivery.
            QMetaObject::invokeMethod(
                qApp,
                [apply = std::move(apply), result = std::move(result),
                 guard]() mutable {
                    if (guard)
                        apply(std::move(result));
                },
                Qt::QueuedConnection);
        });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

QString RepoHost::baseRef() const
{
    // A bare mirror's HEAD can point at a branch that doesn't resolve (e.g. the
    // source's default branch differs from the refs actually present), so fall
    // back to the first available branch when HEAD can't be verified.
    return baseRefFor(m_mirrorPath);
}

QStringList RepoHost::branchRefCandidates(const QString &branch) const
{
    return branchRefCandidatesFor(m_mirrorPath, branch);
}

QString RepoHost::refForBranch(const QString &branch) const
{
    return refForBranchIn(m_mirrorPath, branch);
}

QJsonObject RepoHost::buildBranchesReply() const
{
    QByteArray output;
    QString gitErr;
    if (!runGit(m_mirrorPath,
                {"for-each-ref", "--sort=refname",
                 "--format=%(refname)%x1f%(objectname)%x1f%(committerdate:iso8601)",
                 "refs/heads/", "refs/remotes/"},
                output, &gitErr)) {
        return {{"ok", false}, {"error", gitErr.isEmpty() ? "branches_failed" : gitErr}};
    }

    QJsonArray branches;
    QSet<QString> seen;
    for (const QByteArray &record : output.split('\n')) {
        if (record.trimmed().isEmpty())
            continue;
        const QList<QByteArray> fields = record.split('\x1f');
        if (fields.isEmpty())
            continue;
        const QString ref = QString::fromUtf8(fields.value(0)).trimmed();
        const QString name = displayBranchNameForRef(ref);
        if (name.isEmpty())
            continue;
        if (seen.contains(name))
            continue;
        seen.insert(name);
        branches.append(QJsonObject{
            {"name", name},
            {"commit", QString::fromUtf8(fields.value(1)).trimmed()},
            {"updatedAt", QString::fromUtf8(fields.value(2)).trimmed()}});
    }
    return {{"ok", true}, {"branches", branches}};
}

QJsonObject RepoHost::buildTreeReply(const QString &path, const QString &branch) const
{
    const QString ref = refForBranch(branch);
    if (ref.isEmpty())
        return {{"ok", false}, {"error", "empty_repo"}};
    const QString treeish = path.isEmpty() ? ref : ref + ":" + path;
    QByteArray output;
    QString gitErr;
    if (!runGit(m_mirrorPath, {"ls-tree", "-l", "-z", treeish}, output, &gitErr))
        return {{"ok", false}, {"error", gitErr.isEmpty() ? "not_found" : gitErr}};

    QJsonArray entries;
    for (const QByteArray &record : output.split('\0')) {
        if (record.isEmpty())
            continue;
        const int tab = record.indexOf('\t');
        if (tab < 0)
            continue;
        const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
        if (meta.size() < 4)
            continue;
        const QByteArray type = meta.at(1); // "tree" or "blob"
        bool ok = false;
        const qlonglong size = meta.at(3).toLongLong(&ok);
        entries.append(QJsonObject{
            {"name", QString::fromUtf8(record.mid(tab + 1))},
            {"type", QString::fromUtf8(type)},
            {"size", double(ok ? size : 0)}});
    }
    QJsonObject reply{{"ok", true}, {"entries", entries}};
    // The root listing carries the repo's issue/pull/discussion/commit tallies so
    // the website updates every tab badge from this one reply (issue #93).
    if (path.isEmpty())
        reply.insert("counts", buildRootCounts(ref));
    return reply;
}

QJsonObject RepoHost::buildRootCounts(const QString &ref) const
{
    int commits = 0;
    QByteArray output;
    if (runGit(m_mirrorPath, {"rev-list", "--count", ref}, output))
        commits = QString::fromUtf8(output).trimmed().toInt();
    return QJsonObject{
        {"issues", countNumberedDirs(m_mirrorPath, ref, QStringLiteral("issues"))},
        {"pulls", countNumberedDirs(m_mirrorPath, ref, QStringLiteral("pulls"))},
        {"discussions",
         countNumberedDirs(m_mirrorPath, ref, QStringLiteral("discussions"))},
        {"commits", commits}};
}

QJsonObject RepoHost::buildBlobReply(const QString &path, const QString &branch) const
{
    return blobReplyFor(m_mirrorPath, path, branch);
}

QJsonObject RepoHost::buildCommitsReply(const QString &branch) const
{
    const QString ref = refForBranch(branch);
    if (ref.isEmpty())
        return {{"ok", false}, {"error", "empty_repo"}};
    QByteArray output;
    QString gitErr;
    if (!runGit(m_mirrorPath,
                {"log", "--date=format:%Y-%m-%d", "-n", "60",
                 "--format=%H%x1f%an%x1f%ad%x1f%s", ref},
                output, &gitErr))
        return {{"ok", false}, {"error", gitErr.isEmpty() ? "log_failed" : gitErr}};

    QJsonArray commits;
    for (const QByteArray &record : output.split('\n')) {
        if (record.trimmed().isEmpty())
            continue;
        const QList<QByteArray> f = record.split('\x1f');
        if (f.size() < 4)
            continue;
        commits.append(QJsonObject{{"hash", QString::fromUtf8(f.at(0))},
                                   {"author", QString::fromUtf8(f.at(1))},
                                   {"date", QString::fromUtf8(f.at(2))},
                                   {"subject", QString::fromUtf8(f.at(3))}});
    }
    return {{"ok", true}, {"commits", commits}};
}

QJsonObject RepoHost::buildCommitReply(const QString &hash) const
{
    // Strictly validate the hash so it can never be read as a git flag/path.
    static const QRegularExpression hashRe(QStringLiteral("^[0-9a-fA-F]{4,40}$"));
    if (!hashRe.match(hash).hasMatch())
        return {{"ok", false}, {"error", "bad_hash"}};

    QByteArray meta;
    QString gitErr;
    if (!runGit(m_mirrorPath,
                {"show", "-s", "--date=format:%Y-%m-%d %H:%M",
                 "--format=%H%x1f%an%x1f%ad%x1f%P%x1f%s%x1f%b", hash},
                meta, &gitErr))
        return {{"ok", false}, {"error", gitErr.isEmpty() ? "not_found" : gitErr}};
    const QList<QByteArray> mf = meta.split('\x1f');
    if (mf.size() < 5)
        return {{"ok", false}, {"error", "bad_commit"}};

    const QString full = QString::fromUtf8(mf.at(0)).trimmed();
    const QString parents = QString::fromUtf8(mf.value(3)).trimmed();
    const QString base =
        parents.isEmpty()
            ? QStringLiteral("4b825dc642cb6eb9a060e54bf8d69288fbee4904") // empty tree
            : parents.split(QLatin1Char(' ')).first();

    QByteArray diff;
    runGit(m_mirrorPath, {"diff", "-M", "--no-color", base, full}, diff, nullptr);
    QByteArray numstat;
    runGit(m_mirrorPath, {"diff", "--numstat", base, full}, numstat, nullptr);

    QJsonArray files;
    QJsonArray imageDiffs;
    int imageDiffBytes = 0;
    for (const QByteArray &record : numstat.split('\n')) {
        if (record.trimmed().isEmpty())
            continue;
        const QList<QByteArray> p = record.split('\t');
        if (p.size() < 3)
            continue;
        const QString path = QString::fromUtf8(p.at(2));
        const QString adds = QString::fromUtf8(p.at(0));
        const QString dels = QString::fromUtf8(p.at(1));
        files.append(QJsonObject{{"path", path}, {"adds", adds}, {"dels", dels}});

        const QString mime = imageMimeForPath(path);
        if (adds != QStringLiteral("-") || dels != QStringLiteral("-") ||
            mime.isEmpty())
            continue;

        QJsonObject image{{"path", path}, {"mime", mime}};
        QByteArray oldBytes;
        if (runGit(m_mirrorPath, {"show", base + ":" + path}, oldBytes, nullptr) &&
            oldBytes.size() <= kMaxBlobBytes) {
            const QByteArray encoded = oldBytes.toBase64();
            if (imageDiffBytes + encoded.size() <= kMaxImageDiffBytes) {
                image.insert("old", QString::fromLatin1(encoded));
                imageDiffBytes += encoded.size();
            }
        }
        QByteArray newBytes;
        if (runGit(m_mirrorPath, {"show", full + ":" + path}, newBytes, nullptr) &&
            newBytes.size() <= kMaxBlobBytes) {
            const QByteArray encoded = newBytes.toBase64();
            if (imageDiffBytes + encoded.size() <= kMaxImageDiffBytes) {
                image.insert("new", QString::fromLatin1(encoded));
                imageDiffBytes += encoded.size();
            }
        }
        if (image.contains("old") || image.contains("new"))
            imageDiffs.append(image);
    }

    // Keep the single tunnel frame bounded; the web view notes truncation.
    constexpr int kMaxDiffBytes = 600 * 1024;
    bool truncated = false;
    if (diff.size() > kMaxDiffBytes) {
        diff = diff.left(kMaxDiffBytes);
        truncated = true;
    }

    QJsonObject commit{{"hash", full},
                       {"author", QString::fromUtf8(mf.value(1))},
                       {"date", QString::fromUtf8(mf.value(2))},
                       {"subject", QString::fromUtf8(mf.value(4))},
                       {"body", QString::fromUtf8(mf.value(5)).trimmed()},
                       {"parents", parents}};
    return {{"ok", true},
            {"commit", commit},
            {"files", files},
            {"imageDiffs", imageDiffs},
            {"truncated", truncated},
            {"diff", QString::fromUtf8(diff)}};
}

void RepoHost::scheduleReconnect()
{
    if (m_pingTimer)
        m_pingTimer->stop();
    if (m_stopping)
        return;
    m_wsReady = false;
    if (!m_reconnect->isActive())
        m_reconnect->start(5000);
}
