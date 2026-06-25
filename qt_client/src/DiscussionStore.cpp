#include "DiscussionStore.h"

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
#include <utility>

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

QString stripEdgeNewlines(QString text)
{
    while (text.startsWith('\n') || text.startsWith('\r'))
        text.remove(0, 1);
    while (text.endsWith('\n') || text.endsWith('\r'))
        text.chop(1);
    return text;
}

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
    for (; i < lines.size() && lines.at(i).trimmed() != "---"; ++i) {
        const QString &line = lines.at(i);
        const int sep = line.indexOf(": ");
        if (sep >= 0)
            fm.values.insert(line.left(sep), line.mid(sep + 2));
        else if (line.endsWith(':'))
            fm.values.insert(line.left(line.size() - 1), QString());
    }
    fm.body = stripEdgeNewlines(lines.mid(i + 1).join('\n'));
    return fm;
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

const QRegularExpression &commentFileRe()
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}-comment\\.md$"));
    return re;
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void appendEventFields(QStringList &lines, const DiscussionEvent &ev)
{
    lines << "type: " + ev.type;
    lines << "id: " + ev.id;
    lines << "author: " + ev.author;
    lines << "authorName: " + ev.authorName;
    lines << "ts: " + QString::number(ev.ts);
    if (ev.type == "open") {
        lines << "title: " + ev.title;
        lines << "category: " + ev.category;
    }
    lines << "sig: " + ev.sig;
}

DiscussionEvent eventFromFrontMatter(const FrontMatter &fm)
{
    DiscussionEvent ev;
    ev.type = fm.get("type");
    ev.id = fm.get("id");
    ev.author = fm.get("author");
    ev.authorName = fm.get("authorName");
    ev.ts = fm.num("ts");
    ev.title = fm.get("title");
    ev.category = fm.get("category");
    ev.sig = fm.get("sig");
    ev.body = fm.body;
    return ev;
}

} // namespace

QJsonObject DiscussionEvent::toJson() const
{
    QJsonObject obj{{"type", type},       {"id", id},
                    {"author", author},   {"authorName", authorName},
                    {"ts", double(ts)},   {"body", body},
                    {"sig", sig}};
    if (type == "open") {
        obj.insert("title", title);
        obj.insert("category", category);
    }
    return obj;
}

DiscussionEvent DiscussionEvent::fromJson(const QJsonObject &obj)
{
    DiscussionEvent ev;
    ev.type = obj.value("type").toString();
    ev.id = obj.value("id").toString();
    ev.author = obj.value("author").toString();
    ev.authorName = obj.value("authorName").toString();
    ev.ts = qint64(obj.value("ts").toDouble());
    ev.title = obj.value("title").toString();
    ev.category = obj.value("category").toString();
    ev.body = obj.value("body").toString();
    ev.sig = obj.value("sig").toString();
    return ev;
}

qint64 Discussion::updatedAt() const
{
    qint64 latest = createdAt;
    for (const DiscussionEvent &ev : events)
        latest = std::max(latest, ev.ts);
    return latest;
}

int Discussion::commentCount() const
{
    int count = 0;
    for (const DiscussionEvent &ev : events)
        if (ev.type == "comment")
            ++count;
    return count;
}

DiscussionStore::DiscussionStore(QString workTreePath, QString mirrorPath,
                                 const ForkMeshIdentity *identity,
                                 QString authorName)
    : m_workTree(std::move(workTreePath)), m_mirror(std::move(mirrorPath)),
      m_identity(identity), m_authorName(std::move(authorName))
{
}

bool DiscussionStore::canWrite() const
{
    if (m_workTree.isEmpty() || !m_identity || !m_identity->isValid())
        return false;
    return QFileInfo::exists(m_workTree + "/.git");
}

QString DiscussionStore::discussionsDir() const
{
    return m_workTree + "/discussions";
}

QString DiscussionStore::discussionDir(int number) const
{
    return discussionsDir() + "/" + QString::number(number);
}

QStringList DiscussionStore::categories()
{
    return {"Announcements", "Ideas", "Q&A", "Show and tell", "Maintainer notes"};
}

QString DiscussionStore::normalizedCategory(const QString &category)
{
    const QString trimmed = category.simplified();
    for (const QString &known : categories()) {
        if (QString::compare(trimmed, known, Qt::CaseInsensitive) == 0)
            return known;
    }

    const QString folded = trimmed.toCaseFolded();
    if (folded == "announcement")
        return QStringLiteral("Announcements");
    if (folded == "idea")
        return QStringLiteral("Ideas");
    if (folded == "qa" || folded == "q and a" || folded == "q-and-a")
        return QStringLiteral("Q&A");
    if (folded == "show-and-tell" || folded == "show & tell")
        return QStringLiteral("Show and tell");
    if (folded == "maintainer-notes" || folded == "maintainer note")
        return QStringLiteral("Maintainer notes");
    return QStringLiteral("Ideas");
}

QString DiscussionStore::contentForSigning(const DiscussionEvent &ev)
{
    const QChar nul(QChar::Null);
    if (ev.type == "open")
        return ev.title + nul + ev.body + nul + ev.category;
    if (ev.type == "comment")
        return ev.body;
    return QString();
}

QByteArray DiscussionStore::canonicalString(int number, const DiscussionEvent &ev)
{
    const QByteArray contentHash =
        QCryptographicHash::hash(contentForSigning(ev).toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex();
    QByteArray canonical = "forkmesh-discussion-event-v1\n";
    canonical += ev.type.toUtf8() + "\n";
    canonical += QByteArray::number(number) + "\n";
    canonical += ev.author.toUtf8() + "\n";
    canonical += QByteArray::number(ev.ts) + "\n";
    canonical += contentHash;
    return canonical;
}

DiscussionEvent DiscussionStore::makeSignedEvent(int number, DiscussionEvent ev) const
{
    if (ev.id.isEmpty())
        ev.id = ev.type == "open" && number > 0 ? QStringLiteral("open-%1").arg(number)
                                                 : newId();
    if (ev.type == "open")
        ev.category = normalizedCategory(ev.category);
    ev.author = m_identity ? m_identity->publicKey() : QString();
    if (ev.authorName.isEmpty())
        ev.authorName = m_authorName;
    if (ev.ts == 0)
        ev.ts = QDateTime::currentMSecsSinceEpoch();
    ev.sig = m_identity ? m_identity->signData(canonicalString(number, ev)) : QString();
    return ev;
}

QList<Discussion> DiscussionStore::loadAll(QString *error) const
{
    if (!canWrite())
        return loadFromMirror(error);

    QList<Discussion> discussions;
    const QStringList entries =
        QDir(discussionsDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                         QDir::Name);
    for (const QString &entry : entries) {
        bool numeric = false;
        const int number = entry.toInt(&numeric);
        if (!numeric)
            continue;
        Discussion discussion;
        if (readDiscussionFile(number, discussion))
            discussions.append(discussion);
    }
    std::sort(discussions.begin(), discussions.end(),
              [](const Discussion &a, const Discussion &b) {
                  return a.number < b.number;
              });
    return discussions;
}

bool DiscussionStore::readDiscussionFile(int number, Discussion &out) const
{
    QFile file(discussionDir(number) + "/discussion.md");
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const FrontMatter fm = parseFrontMatter(file.readAll());

    out = Discussion();
    out.number = number;
    out.title = fm.get("title");
    out.category = fm.get("category");
    out.createdAt = fm.num("createdAt");
    out.author = fm.get("author");
    out.authorName = fm.get("authorName");

    DiscussionEvent open = eventFromFrontMatter(fm);
    open.type = "open";
    open.title = out.title;
    open.category = out.category;
    out.events.append(open);

    QStringList eventFiles =
        QDir(discussionDir(number)).entryList(QDir::Files, QDir::Name);
    eventFiles.sort();
    for (const QString &name : eventFiles) {
        if (!commentFileRe().match(name).hasMatch())
            continue;
        QFile ef(discussionDir(number) + "/" + name);
        if (ef.open(QIODevice::ReadOnly))
            out.events.append(eventFromFrontMatter(parseFrontMatter(ef.readAll())));
    }
    return true;
}

bool DiscussionStore::writeDiscussionFile(const Discussion &discussion,
                                          QString *error) const
{
    QDir().mkpath(discussionDir(discussion.number));
    if (discussion.events.isEmpty())
        return false;
    const DiscussionEvent &open = discussion.events.first();

    QStringList lines;
    lines << "---";
    lines << "schema: forkmesh-discussion-v1";
    lines << "number: " + QString::number(discussion.number);
    lines << "title: " + open.title;
    lines << "category: " + open.category;
    lines << "createdAt: " + QString::number(discussion.createdAt);
    lines << "author: " + open.author;
    lines << "authorName: " + open.authorName;
    lines << "type: open";
    lines << "id: " + open.id;
    lines << "ts: " + QString::number(open.ts);
    lines << "sig: " + open.sig;
    lines << "---";
    lines << "";
    if (!writeTextFile(discussionDir(discussion.number) + "/discussion.md",
                       lines.join('\n') + "\n" + open.body + "\n", error))
        return false;

    int index = 2;
    for (int i = 1; i < discussion.events.size(); ++i) {
        const DiscussionEvent &ev = discussion.events.at(i);
        if (ev.type != "comment")
            continue;
        QStringList evLines;
        evLines << "---";
        appendEventFields(evLines, ev);
        evLines << "---";
        evLines << "";
        const QString name = QStringLiteral("%1-comment.md")
                                 .arg(index, 4, 10, QChar('0'));
        if (!writeTextFile(discussionDir(discussion.number) + "/" + name,
                           evLines.join('\n') + "\n" + ev.body + "\n", error))
            return false;
        ++index;
    }
    return true;
}

int DiscussionStore::nextNumber() const
{
    int max = 0;
    for (const QString &entry :
         QDir(discussionsDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numeric = false;
        const int n = entry.toInt(&numeric);
        if (numeric && n > max)
            max = n;
    }
    return max + 1;
}

bool DiscussionStore::commit(const QString &message, QString *error) const
{
    QString err;
    if (!runGit(m_workTree, {"add", "discussions"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    if (!runGit(m_workTree, {"commit", "-m", message, "--", "discussions"},
                nullptr, &err)) {
        if (err.contains("nothing to commit") || err.isEmpty())
            return true;
        if (error)
            *error = "git commit failed: " + err;
        return false;
    }
    return true;
}

int DiscussionStore::createDiscussion(const QString &title, const QString &body,
                                      const QString &category, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return -1;
    }
    const QString cleanTitle = title.trimmed();
    if (cleanTitle.isEmpty()) {
        if (error)
            *error = QStringLiteral("Discussion title is required.");
        return -1;
    }

    const int number = nextNumber();
    DiscussionEvent ev;
    ev.type = "open";
    ev.title = cleanTitle;
    ev.category = normalizedCategory(category);
    ev.body = stripEdgeNewlines(body);
    ev = makeSignedEvent(number, ev);

    Discussion discussion;
    discussion.number = number;
    discussion.title = ev.title;
    discussion.category = ev.category;
    discussion.createdAt = ev.ts;
    discussion.author = ev.author;
    discussion.authorName = ev.authorName;
    discussion.events.append(ev);

    if (!writeDiscussionFile(discussion, error))
        return -1;
    if (!commit(QStringLiteral("discussion #%1: %2").arg(number).arg(ev.title), error))
        return -1;
    return number;
}

bool DiscussionStore::addComment(int number, const QString &body, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }

    Discussion discussion;
    if (!readDiscussionFile(number, discussion)) {
        if (error)
            *error = QStringLiteral("Discussion #%1 not found.").arg(number);
        return false;
    }

    DiscussionEvent ev;
    ev.type = "comment";
    ev.body = stripEdgeNewlines(body);
    ev = makeSignedEvent(number, ev);
    discussion.events.append(ev);
    if (!writeDiscussionFile(discussion, error))
        return false;
    return commit(QStringLiteral("discussion #%1: comment").arg(number), error);
}

bool DiscussionStore::applyRemoteEvent(int number, const DiscussionEvent &ev,
                                       const QString &titleIfNew, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }

    if (ev.type == "open") {
        const QList<Discussion> all = loadAll();
        for (const Discussion &discussion : all) {
            for (const DiscussionEvent &existing : discussion.events) {
                if (existing.type != "open")
                    continue;
                const bool sameSig = !ev.sig.isEmpty() && existing.sig == ev.sig;
                const bool sameAuthorTs = ev.sig.isEmpty() &&
                                          existing.author == ev.author &&
                                          existing.ts == ev.ts &&
                                          existing.title == ev.title;
                if (sameSig || sameAuthorTs)
                    return true;
            }
        }

        Discussion probe;
        if (number <= 0 || readDiscussionFile(number, probe))
            number = nextNumber();

        Discussion discussion;
        discussion.number = number;
        discussion.title = ev.title;
        discussion.category = ev.category;
        discussion.createdAt = ev.ts;
        discussion.author = ev.author;
        discussion.authorName = ev.authorName;
        discussion.events.append(ev);
        if (!writeDiscussionFile(discussion, error))
            return false;
        return commit(QStringLiteral("discussion #%1: opened (from %2)")
                          .arg(number)
                          .arg(ev.authorName.isEmpty() ? ev.author.left(8)
                                                       : ev.authorName),
                      error);
    }

    if (ev.type != "comment") {
        if (error)
            *error = QStringLiteral("Unsupported discussion event type.");
        return false;
    }

    Discussion discussion;
    const bool exists = readDiscussionFile(number, discussion);
    if (!exists) {
        discussion.number = number;
        discussion.title = titleIfNew;
        discussion.category = QStringLiteral("Ideas");
        discussion.createdAt = ev.ts;
        discussion.author = ev.author;
        discussion.authorName = ev.authorName;

        DiscussionEvent placeholder;
        placeholder.type = "open";
        placeholder.id = QStringLiteral("open-%1").arg(number);
        placeholder.title = titleIfNew;
        placeholder.category = discussion.category;
        placeholder.author = ev.author;
        placeholder.authorName = ev.authorName;
        placeholder.ts = ev.ts;
        discussion.events.append(placeholder);
    }

    if (!ev.id.isEmpty()) {
        for (const DiscussionEvent &existing : std::as_const(discussion.events))
            if (existing.id == ev.id)
                return true;
    }
    discussion.events.append(ev);
    if (!writeDiscussionFile(discussion, error))
        return false;
    return commit(QStringLiteral("discussion #%1: comment (from %2)")
                      .arg(number)
                      .arg(ev.authorName.isEmpty() ? ev.author.left(8)
                                                   : ev.authorName),
                  error);
}

QString DiscussionStore::mirrorRef() const
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

QByteArray DiscussionStore::showFromMirror(const QString &repoRelPath, bool *ok) const
{
    const QString ref = mirrorRef();
    QByteArray output;
    const bool good = !ref.isEmpty() &&
                      runGit(m_mirror, {"show", ref + ":" + repoRelPath}, &output);
    if (ok)
        *ok = good;
    return good ? output : QByteArray();
}

QList<Discussion> DiscussionStore::loadFromMirror(QString *error) const
{
    QList<Discussion> discussions;
    if (m_mirror.isEmpty()) {
        if (error)
            *error = QStringLiteral("No local mirror to read discussions from.");
        return discussions;
    }
    const QString ref = mirrorRef();
    if (ref.isEmpty())
        return discussions;

    QByteArray listing;
    if (!runGit(m_mirror, {"ls-tree", ref, "discussions/"}, &listing))
        return discussions;
    for (const QString &line :
         QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts)) {
        const int tab = line.indexOf('\t');
        if (tab < 0 || !line.contains(" tree "))
            continue;
        const QString base = line.mid(tab + 1).section('/', -1);
        bool numeric = false;
        const int number = base.toInt(&numeric);
        if (!numeric)
            continue;

        bool ok = false;
        const QByteArray md =
            showFromMirror("discussions/" + base + "/discussion.md", &ok);
        if (!ok)
            continue;
        const FrontMatter fm = parseFrontMatter(md);
        Discussion discussion;
        discussion.number = number;
        discussion.title = fm.get("title");
        discussion.category = fm.get("category");
        discussion.createdAt = fm.num("createdAt");
        discussion.author = fm.get("author");
        discussion.authorName = fm.get("authorName");
        DiscussionEvent open = eventFromFrontMatter(fm);
        open.type = "open";
        open.title = discussion.title;
        open.category = discussion.category;
        discussion.events.append(open);

        QByteArray dirListing;
        if (runGit(m_mirror, {"ls-tree", ref, "discussions/" + base + "/"},
                   &dirListing)) {
            QStringList names;
            for (const QString &l :
                 QString::fromUtf8(dirListing).split('\n', Qt::SkipEmptyParts)) {
                const int t = l.indexOf('\t');
                if (t < 0 || !l.contains(" blob "))
                    continue;
                const QString fname = l.mid(t + 1).section('/', -1);
                if (commentFileRe().match(fname).hasMatch())
                    names << fname;
            }
            names.sort();
            for (const QString &name : names) {
                bool eok = false;
                const QByteArray evBytes =
                    showFromMirror("discussions/" + base + "/" + name, &eok);
                if (eok)
                    discussion.events.append(
                        eventFromFrontMatter(parseFrontMatter(evBytes)));
            }
        }
        discussions.append(discussion);
    }
    std::sort(discussions.begin(), discussions.end(),
              [](const Discussion &a, const Discussion &b) {
                  return a.number < b.number;
              });
    return discussions;
}
