#include "RepoHost.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

namespace {

constexpr quint64 kMaxWsPayload = 8ull * 1024 * 1024;
constexpr int kMaxBlobBytes = 512 * 1024;
constexpr int kMaxImageDiffBytes = 2 * 1024 * 1024;
constexpr int kGitTimeoutMs = 5000;

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
    if (doc.isObject())
        handleRequest(doc.object());
}

void RepoHost::handleRequest(const QJsonObject &request)
{
    if (request.value("type").toString() != "request")
        return;
    const QString reqId = request.value("reqId").toString();
    const QString op = request.value("op").toString();
    const QString path = request.value("path").toString();
    if (reqId.isEmpty())
        return;

    emit requestServed(m_owner, m_name, op == "git-upload-pack");

    // Git smart-HTTP clone: stream the packfile/advertisement back in chunks.
    if (op == "git-info-refs") {
        runGitStream(reqId,
                     {"upload-pack", "--stateless-rpc", "--advertise-refs",
                      m_mirrorPath},
                     QByteArray());
        return;
    }
    if (op == "git-upload-pack") {
        const QByteArray body =
            QByteArray::fromBase64(request.value("body").toString().toLatin1());
        runGitStream(reqId, {"upload-pack", "--stateless-rpc", m_mirrorPath}, body);
        return;
    }

    QJsonObject reply;
    if (!isSafeRepoPath(path)) {
        reply = QJsonObject{{"ok", false}, {"error", "bad_path"}};
    } else if (op == "tree") {
        reply = buildTreeReply(path);
    } else if (op == "blob") {
        reply = buildBlobReply(path);
    } else if (op == "commits") {
        reply = buildCommitsReply();
    } else if (op == "commit") {
        reply = buildCommitReply(path); // path carries the commit hash
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

QString RepoHost::baseRef() const
{
    // A bare mirror's HEAD can point at a branch that doesn't resolve (e.g. the
    // source's default branch differs from the refs actually present), so fall
    // back to the first available branch when HEAD can't be verified.
    QByteArray output;
    if (runGit(m_mirrorPath, {"rev-parse", "--verify", "-q", "HEAD"}, output) &&
        !output.trimmed().isEmpty())
        return QStringLiteral("HEAD");
    if (runGit(m_mirrorPath,
               {"for-each-ref", "--format=%(refname)", "--count=1",
                "refs/heads/"},
               output)) {
        const QString ref = QString::fromUtf8(output).trimmed();
        if (!ref.isEmpty())
            return ref;
    }
    return QString();
}

QJsonObject RepoHost::buildTreeReply(const QString &path) const
{
    const QString ref = baseRef();
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
    return {{"ok", true}, {"entries", entries}};
}

QJsonObject RepoHost::buildBlobReply(const QString &path) const
{
    if (path.isEmpty())
        return {{"ok", false}, {"error", "not_found"}};
    const QString ref = baseRef();
    if (ref.isEmpty())
        return {{"ok", false}, {"error", "not_found"}};
    QByteArray output;
    QString gitErr;
    if (!runGit(m_mirrorPath, {"cat-file", "-p", ref + ":" + path}, output, &gitErr))
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

QJsonObject RepoHost::buildCommitsReply() const
{
    const QString ref = baseRef();
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
