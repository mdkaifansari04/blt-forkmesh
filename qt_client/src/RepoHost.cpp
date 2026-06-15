#include "RepoHost.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

namespace {

constexpr quint64 kMaxWsPayload = 8ull * 1024 * 1024;
constexpr int kMaxBlobBytes = 512 * 1024;
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

} // namespace

RepoHost::RepoHost(const QString &owner, const QString &name,
                   const QString &mirrorPath, const QUrl &url, QObject *parent)
    : QObject(parent), m_url(url), m_owner(owner), m_name(name),
      m_mirrorPath(mirrorPath)
{
    m_reconnect = new QTimer(this);
    m_reconnect->setSingleShot(true);
    connect(m_reconnect, &QTimer::timeout, this, [this] { connectSocket(); });
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
    const QByteArray request =
        "GET " + wsPath(m_url).toUtf8() + " HTTP/1.1\r\n"
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

    QJsonObject reply;
    if (!isSafeRepoPath(path)) {
        reply = QJsonObject{{"ok", false}, {"error", "bad_path"}};
    } else if (op == "tree") {
        reply = buildTreeReply(path);
    } else if (op == "blob") {
        reply = buildBlobReply(path);
    } else {
        reply = QJsonObject{{"ok", false}, {"error", "bad_op"}};
    }

    reply.insert("type", "response");
    reply.insert("reqId", reqId);
    reply.insert("op", op);
    reply.insert("path", path);
    sendText(QJsonDocument(reply).toJson(QJsonDocument::Compact));
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

void RepoHost::scheduleReconnect()
{
    if (m_stopping)
        return;
    m_wsReady = false;
    if (!m_reconnect->isActive())
        m_reconnect->start(5000);
}
