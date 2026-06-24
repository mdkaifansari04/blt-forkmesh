#include "CommitCommentStore.h"

#include "ForkMeshIdentity.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace {

constexpr int kGitTimeoutMs = 15000;

bool runGit(const QString &dir, const QStringList &args, QByteArray *output = nullptr,
            QString *errText = nullptr)
{
    QProcess process;
    process.start("git", QStringList{"-C", dir} + args);
    if (!process.waitForFinished(kGitTimeoutMs)) {
        if (errText)
            *errText = QStringLiteral("git timed out");
        return false;
    }
    if (output)
        *output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errText)
            *errText =
                QString::fromUtf8(process.readAllStandardError()).trimmed().left(300);
        return false;
    }
    return true;
}

bool writeTextFile(const QString &path, const QString &text, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("Could not write %1").arg(path);
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

// Minimal "---\nkey: value\n---\n\nbody" frontmatter (same subset as PullStore).
struct FrontMatter {
    QHash<QString, QString> values;
    QString body;
    QString get(const QString &key) const { return values.value(key); }
    qint64 num(const QString &key) const { return values.value(key).toLongLong(); }
};

FrontMatter parseFrontMatter(const QByteArray &bytes)
{
    FrontMatter fm;
    const QString text = QString::fromUtf8(bytes);
    const QStringList lines = text.split('\n');
    if (lines.isEmpty() || lines.first().trimmed() != "---") {
        fm.body = text;
        return fm;
    }
    int i = 1;
    for (; i < lines.size() && lines.at(i) != "---"; ++i) {
        const int sep = lines.at(i).indexOf(": ");
        if (sep >= 0)
            fm.values.insert(lines.at(i).left(sep), lines.at(i).mid(sep + 2));
    }
    QString body = lines.mid(i + 1).join('\n');
    while (body.startsWith('\n'))
        body.remove(0, 1);
    while (body.endsWith('\n'))
        body.chop(1);
    fm.body = body;
    return fm;
}

const QRegularExpression &commentFileRe()
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}-comment\\.md$"));
    return re;
}

// Commit hashes name folders; constrain to hex so a stray path can't escape.
bool isValidSha(const QString &sha)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{7,40}$"));
    return re.match(sha).hasMatch();
}

CommitComment commentFromFrontMatter(const FrontMatter &fm)
{
    CommitComment c;
    c.id = fm.get("id");
    c.author = fm.get("author");
    c.authorName = fm.get("authorName");
    c.ts = fm.num("ts");
    c.sig = fm.get("sig");
    c.body = fm.body;
    return c;
}

QString stripEdgeNewlines(QString s)
{
    while (s.startsWith('\n'))
        s.remove(0, 1);
    while (s.endsWith('\n'))
        s.chop(1);
    return s;
}

} // namespace

// ---- CommitComment (JSON wire format) --------------------------------------

QJsonObject CommitComment::toJson() const
{
    return {{"id", id},       {"author", author}, {"authorName", authorName},
            {"ts", double(ts)}, {"body", body},   {"sig", sig}};
}

CommitComment CommitComment::fromJson(const QJsonObject &obj)
{
    CommitComment c;
    c.id = obj.value("id").toString();
    c.author = obj.value("author").toString();
    c.authorName = obj.value("authorName").toString();
    c.ts = obj.value("ts").toVariant().toLongLong();
    c.body = obj.value("body").toString();
    c.sig = obj.value("sig").toString();
    return c;
}

// ---- CommitCommentStore ----------------------------------------------------

CommitCommentStore::CommitCommentStore(QString workTreePath, QString mirrorPath,
                                       const ForkMeshIdentity *identity,
                                       QString authorName)
    : m_workTree(std::move(workTreePath)), m_mirror(std::move(mirrorPath)),
      m_identity(identity), m_authorName(std::move(authorName))
{
}

bool CommitCommentStore::canWrite() const
{
    if (m_workTree.isEmpty() || !m_identity || !m_identity->isValid())
        return false;
    return QFileInfo::exists(m_workTree + "/.git");
}

QString CommitCommentStore::commitsDir() const { return m_workTree + "/commits"; }
QString CommitCommentStore::commitDir(const QString &sha) const
{
    return commitsDir() + "/" + sha;
}

QByteArray CommitCommentStore::canonicalString(const QString &sha,
                                               const CommitComment &c)
{
    const QByteArray contentHash =
        QCryptographicHash::hash(c.body.toUtf8(), QCryptographicHash::Sha256).toHex();
    QByteArray canonical = "forkmesh-commit-comment-v1\n";
    canonical += sha.toUtf8() + "\n";
    canonical += c.author.toUtf8() + "\n";
    canonical += QByteArray::number(c.ts) + "\n";
    canonical += contentHash;
    return canonical;
}

CommitComment CommitCommentStore::makeSignedComment(const QString &sha,
                                                    CommitComment c) const
{
    if (c.id.isEmpty())
        c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.author = m_identity ? m_identity->publicKey() : QString();
    if (c.authorName.isEmpty())
        c.authorName = m_authorName;
    if (c.ts == 0)
        c.ts = QDateTime::currentMSecsSinceEpoch();
    c.sig = m_identity ? m_identity->signData(canonicalString(sha, c)) : QString();
    return c;
}

int CommitCommentStore::nextIndex(const QString &sha) const
{
    int max = 0;
    for (const QString &name : QDir(commitDir(sha)).entryList(QDir::Files)) {
        if (!commentFileRe().match(name).hasMatch())
            continue;
        const int n = name.left(4).toInt();
        if (n > max)
            max = n;
    }
    return max + 1;
}

bool CommitCommentStore::writeComment(const QString &sha, int index,
                                      const CommitComment &c, QString *error) const
{
    QDir().mkpath(commitDir(sha));
    QStringList lines;
    lines << "---";
    lines << "type: comment";
    lines << "id: " + c.id;
    lines << "commit: " + sha;
    lines << "author: " + c.author;
    lines << "authorName: " + c.authorName;
    lines << "ts: " + QString::number(c.ts);
    lines << "sig: " + c.sig;
    lines << "---";
    lines << "";
    const QString name = QStringLiteral("%1-comment.md").arg(index, 4, 10, QChar('0'));
    return writeTextFile(commitDir(sha) + "/" + name,
                         lines.join('\n') + "\n" + c.body + "\n", error);
}

bool CommitCommentStore::commit(const QString &message, QString *error) const
{
    QString err;
    if (!runGit(m_workTree, {"add", "commits"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    if (!runGit(m_workTree, {"commit", "-m", message, "--", "commits"}, nullptr, &err)) {
        if (err.contains("nothing to commit") || err.isEmpty())
            return true;
        if (error)
            *error = "git commit failed: " + err;
        return false;
    }
    return true;
}

bool CommitCommentStore::addComment(const QString &sha, const QString &body,
                                    QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    if (!isValidSha(sha)) {
        if (error)
            *error = QStringLiteral("Invalid commit hash.");
        return false;
    }
    CommitComment c;
    c.body = stripEdgeNewlines(body);
    c = makeSignedComment(sha, c);
    if (!writeComment(sha, nextIndex(sha), c, error))
        return false;
    return commit(QStringLiteral("commit %1: comment").arg(sha.left(8)), error);
}

bool CommitCommentStore::applyRemoteComment(const QString &sha,
                                            const CommitComment &c, QString *error)
{
    if (!canWrite())
        return false;
    if (!isValidSha(sha)) {
        if (error)
            *error = QStringLiteral("Invalid commit hash.");
        return false;
    }
    if (!writeComment(sha, nextIndex(sha), c, error))
        return false;
    return commit(QStringLiteral("commit %1: comment (from %2)")
                      .arg(sha.left(8),
                           c.authorName.isEmpty() ? c.author.left(8) : c.authorName),
                  error);
}

QList<CommitComment> CommitCommentStore::loadFor(const QString &sha) const
{
    if (!isValidSha(sha))
        return {};
    if (!canWrite())
        return loadFromMirror(sha);
    QList<CommitComment> out;
    QStringList names = QDir(commitDir(sha)).entryList(QDir::Files, QDir::Name);
    names.sort();
    for (const QString &name : names) {
        if (!commentFileRe().match(name).hasMatch())
            continue;
        QFile f(commitDir(sha) + "/" + name);
        if (f.open(QIODevice::ReadOnly))
            out.append(commentFromFrontMatter(parseFrontMatter(f.readAll())));
    }
    return out;
}

QList<QPair<QString, QList<CommitComment>>> CommitCommentStore::loadAll() const
{
    QList<QPair<QString, QList<CommitComment>>> out;
    QStringList shas;
    if (canWrite()) {
        for (const QString &name :
             QDir(commitsDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (isValidSha(name))
                shas << name;
    } else if (!m_mirror.isEmpty()) {
        // List the commits/ subtree directly so each entry name is a bare SHA.
        const QString ref = mirrorRef();
        QByteArray listing;
        if (!ref.isEmpty() &&
            runGit(m_mirror, {"ls-tree", ref + ":commits"}, &listing)) {
            for (const QString &line :
                 QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts)) {
                const int tab = line.indexOf('\t');
                if (tab < 0 || !line.contains(" tree "))
                    continue;
                const QString name = line.mid(tab + 1).section('/', 0, 0);
                if (isValidSha(name))
                    shas << name;
            }
        }
    }
    for (const QString &sha : shas) {
        const QList<CommitComment> thread = loadFor(sha);
        if (!thread.isEmpty())
            out.append({sha, thread});
    }
    return out;
}

// ---- Read-only access from a bare mirror -----------------------------------

QString CommitCommentStore::mirrorRef() const
{
    QByteArray output;
    if (runGit(m_mirror, {"rev-parse", "--verify", "-q", "HEAD"}, &output) &&
        !output.trimmed().isEmpty())
        return QStringLiteral("HEAD");
    if (runGit(m_mirror,
               {"for-each-ref", "--format=%(refname)", "--count=1", "refs/heads/"},
               &output)) {
        const QString ref = QString::fromUtf8(output).trimmed();
        if (!ref.isEmpty())
            return ref;
    }
    return QString();
}

QList<CommitComment> CommitCommentStore::loadFromMirror(const QString &sha) const
{
    QList<CommitComment> out;
    if (m_mirror.isEmpty())
        return out;
    const QString ref = mirrorRef();
    if (ref.isEmpty())
        return out;
    QByteArray listing;
    if (!runGit(m_mirror, {"ls-tree", ref, "commits/" + sha + "/"}, &listing))
        return out;
    QStringList names;
    for (const QString &line :
         QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts)) {
        const int tab = line.indexOf('\t');
        if (tab < 0 || !line.contains(" blob "))
            continue;
        const QString fname = line.mid(tab + 1).section('/', -1);
        if (commentFileRe().match(fname).hasMatch())
            names << fname;
    }
    names.sort();
    for (const QString &name : names) {
        QByteArray bytes;
        if (runGit(m_mirror, {"show", ref + ":commits/" + sha + "/" + name}, &bytes))
            out.append(commentFromFrontMatter(parseFrontMatter(bytes)));
    }
    return out;
}
