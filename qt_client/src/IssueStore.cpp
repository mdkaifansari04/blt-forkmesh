#include "IssueStore.h"

#include "ForkMeshIdentity.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace {

constexpr int kGitTimeoutMs = 10000;

// Run git in `dir`, capturing stdout. Returns false (with optional error text)
// on non-zero exit or timeout. Mirrors RepoHost's helper.
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
                QString::fromUtf8(process.readAllStandardError()).trimmed().left(200);
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

QStringList toStringList(const QJsonArray &array)
{
    QStringList out;
    for (const QJsonValue &value : array)
        out << value.toString();
    return out;
}

QJsonArray fromStringList(const QStringList &list)
{
    QJsonArray array;
    for (const QString &item : list)
        array.append(item);
    return array;
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// ---- Minimal frontmatter (a constrained YAML subset we fully control) ------
// A file is "---\n<key: value lines>\n---\n\n<markdown body>". Lists are inline
// "[a, b]". This is deliberately simple so the C++ client, the Python seed/tool,
// and hand-editing all agree.
struct FrontMatter {
    QHash<QString, QString> values;
    QString body;
    QString get(const QString &key) const { return values.value(key); }
    qint64 num(const QString &key) const { return values.value(key).toLongLong(); }
    QStringList list(const QString &key) const
    {
        QString v = values.value(key).trimmed();
        if (v.startsWith('[') && v.endsWith(']'))
            v = v.mid(1, v.size() - 2);
        QStringList out;
        for (const QString &part : v.split(',', Qt::SkipEmptyParts))
            out << part.trimmed();
        return out;
    }
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

QString serializeList(const QStringList &list) { return "[" + list.join(", ") + "]"; }

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

const QRegularExpression &eventFileRe()
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}-.+\\.md$"));
    return re;
}

} // namespace

// ---- IssueEvent (JSON is the wire format for the relay inbox) ---------------

QJsonObject IssueEvent::toJson() const
{
    QJsonObject obj{{"type", type}, {"id", id}, {"author", author},
                    {"authorName", authorName},
                    {"ts", double(ts)}};
    if (type == "open" || type == "title")
        obj.insert("title", title);
    if (type == "open" || type == "comment" || type == "edit")
        obj.insert("attachments", fromStringList(attachments));
    if (type == "edit" || type == "delete")
        obj.insert("target", target);
    if (type == "status")
        obj.insert("status", status);
    if (type == "labels")
        obj.insert("labels", fromStringList(labels));
    if (type == "milestone")
        obj.insert("milestone", milestone);
    if (type == "assignees")
        obj.insert("assignees", fromStringList(assignees));
    obj.insert("sig", sig);
    return obj;
}

IssueEvent IssueEvent::fromJson(const QJsonObject &obj)
{
    IssueEvent ev;
    ev.type = obj.value("type").toString();
    ev.id = obj.value("id").toString();
    ev.author = obj.value("author").toString();
    ev.authorName = obj.value("authorName").toString();
    ev.ts = qint64(obj.value("ts").toDouble());
    ev.title = obj.value("title").toString();
    ev.attachments = toStringList(obj.value("attachments").toArray());
    ev.target = obj.value("target").toString();
    ev.status = obj.value("status").toString();
    ev.labels = toStringList(obj.value("labels").toArray());
    ev.milestone = obj.value("milestone").toString();
    ev.assignees = toStringList(obj.value("assignees").toArray());
    ev.sig = obj.value("sig").toString();
    return ev;
}

// ---- Issue -----------------------------------------------------------------

QJsonObject Issue::toJson() const
{
    QJsonArray eventsArray;
    for (const IssueEvent &ev : events)
        eventsArray.append(ev.toJson());
    return {{"schema", "forkmesh-issue-v1"}, {"number", number}, {"title", title},
            {"status", status}, {"labels", fromStringList(labels)},
            {"milestone", milestone}, {"assignees", fromStringList(assignees)},
            {"createdAt", double(createdAt)}, {"author", author},
            {"authorName", authorName}, {"events", eventsArray}};
}

Issue Issue::fromJson(const QJsonObject &obj)
{
    Issue issue;
    issue.number = obj.value("number").toInt();
    issue.title = obj.value("title").toString();
    issue.status = obj.value("status").toString("open");
    issue.labels = toStringList(obj.value("labels").toArray());
    issue.milestone = obj.value("milestone").toString();
    issue.assignees = toStringList(obj.value("assignees").toArray());
    issue.createdAt = qint64(obj.value("createdAt").toDouble());
    issue.author = obj.value("author").toString();
    issue.authorName = obj.value("authorName").toString();
    for (const QJsonValue &value : obj.value("events").toArray())
        issue.events.append(IssueEvent::fromJson(value.toObject()));
    return issue;
}

bool Issue::isDeleted() const
{
    for (const IssueEvent &ev : events)
        if (ev.type == "delete" && ev.target == "self")
            return true;
    return false;
}

// ---- IssueStore ------------------------------------------------------------

IssueStore::IssueStore(QString workTreePath, QString mirrorPath,
                       const ForkMeshIdentity *identity, QString authorName)
    : m_workTree(std::move(workTreePath)), m_mirror(std::move(mirrorPath)),
      m_identity(identity), m_authorName(std::move(authorName))
{
}

bool IssueStore::canWrite() const
{
    if (m_workTree.isEmpty() || !m_identity || !m_identity->isValid())
        return false;
    return QFileInfo::exists(m_workTree + "/.git");
}

QString IssueStore::issuesDir() const { return m_workTree + "/issues"; }

QString IssueStore::issueDir(int number) const
{
    return issuesDir() + "/" + QString::number(number);
}

// ---- Signing ---------------------------------------------------------------

QString IssueStore::contentForSigning(const IssueEvent &ev)
{
    const QChar nul(QChar::Null);
    if (ev.type == "open")
        return ev.title + nul + ev.body + nul + ev.attachments.join(",");
    if (ev.type == "comment" || ev.type == "edit")
        return ev.body + nul + ev.attachments.join(",");
    if (ev.type == "status")
        return ev.status;
    if (ev.type == "labels")
        return ev.labels.join(",");
    if (ev.type == "milestone")
        return ev.milestone;
    if (ev.type == "assignees")
        return ev.assignees.join(",");
    if (ev.type == "delete")
        return ev.target;
    return QString();
}

QByteArray IssueStore::canonicalString(int number, const IssueEvent &ev)
{
    const QByteArray contentHash =
        QCryptographicHash::hash(contentForSigning(ev).toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex();
    QByteArray canonical = "forkmesh-issue-event-v1\n";
    canonical += ev.type.toUtf8() + "\n";
    canonical += QByteArray::number(number) + "\n";
    canonical += ev.author.toUtf8() + "\n";
    canonical += QByteArray::number(ev.ts) + "\n";
    canonical += contentHash;
    return canonical;
}

IssueEvent IssueStore::makeSignedEvent(int number, IssueEvent ev) const
{
    if (ev.id.isEmpty())
        ev.id = newId();
    ev.author = m_identity ? m_identity->publicKey() : QString();
    if (ev.authorName.isEmpty())
        ev.authorName = m_authorName;
    if (ev.ts == 0)
        ev.ts = QDateTime::currentMSecsSinceEpoch();
    ev.sig = m_identity ? m_identity->signData(canonicalString(number, ev)) : QString();
    return ev;
}

// ---- Frontmatter (de)serialization for one event ---------------------------

namespace {

// Serialize the event-specific frontmatter lines (without the issue-level
// metadata, which the open event shares with issue.md).
void appendEventFields(QStringList &lines, const IssueEvent &ev)
{
    lines << "type: " + ev.type;
    lines << "id: " + ev.id;
    lines << "author: " + ev.author;
    lines << "authorName: " + ev.authorName;
    lines << "ts: " + QString::number(ev.ts);
    if (ev.type == "edit" || ev.type == "delete")
        lines << "target: " + ev.target;
    if (ev.type == "status")
        lines << "status: " + ev.status;
    if (ev.type == "labels")
        lines << "labels: " + serializeList(ev.labels);
    if (ev.type == "milestone")
        lines << "milestone: " + ev.milestone;
    if (ev.type == "assignees")
        lines << "assignees: " + serializeList(ev.assignees);
    if (ev.type == "open" || ev.type == "comment" || ev.type == "edit")
        lines << "attachments: " + serializeList(ev.attachments);
    lines << "sig: " + ev.sig;
}

IssueEvent eventFromFrontMatter(const FrontMatter &fm)
{
    IssueEvent ev;
    ev.type = fm.get("type");
    ev.id = fm.get("id");
    ev.author = fm.get("author");
    ev.authorName = fm.get("authorName");
    ev.ts = fm.num("ts");
    ev.title = fm.get("title"); // open + title events
    ev.target = fm.get("target");
    ev.status = fm.get("status");
    ev.labels = fm.list("labels");
    ev.milestone = fm.get("milestone");
    ev.assignees = fm.list("assignees");
    ev.attachments = fm.list("attachments");
    ev.sig = fm.get("sig");
    ev.body = fm.body;
    return ev;
}

} // namespace

// ---- Loading ---------------------------------------------------------------

QList<Issue> IssueStore::loadAll(QString *error) const
{
    if (!canWrite())
        return loadFromMirror(error);

    QList<Issue> issues;
    const QStringList entries =
        QDir(issuesDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        bool numeric = false;
        const int number = entry.toInt(&numeric);
        if (!numeric)
            continue;
        Issue issue;
        if (readIssueFile(number, issue) && !issue.isDeleted())
            issues.append(issue);
    }
    std::sort(issues.begin(), issues.end(),
              [](const Issue &a, const Issue &b) { return a.number < b.number; });
    return issues;
}

bool IssueStore::readIssueFile(int number, Issue &out) const
{
    QFile file(issueDir(number) + "/issue.md");
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const FrontMatter fm = parseFrontMatter(file.readAll());

    out = Issue();
    out.number = number;
    out.title = fm.get("title");
    out.status = fm.values.contains("status") ? fm.get("status") : QStringLiteral("open");
    out.labels = fm.list("labels");
    out.milestone = fm.get("milestone");
    out.assignees = fm.list("assignees");
    out.createdAt = fm.num("createdAt");
    out.author = fm.get("author");
    out.authorName = fm.get("authorName");

    IssueEvent open = eventFromFrontMatter(fm);
    open.type = "open";
    open.title = out.title;
    out.events.append(open);

    // Subsequent events: NNNN-<type>.md, in filename (chronological) order.
    QStringList eventFiles =
        QDir(issueDir(number)).entryList(QDir::Files, QDir::Name);
    for (const QString &name : eventFiles) {
        if (!eventFileRe().match(name).hasMatch())
            continue;
        QFile ef(issueDir(number) + "/" + name);
        if (ef.open(QIODevice::ReadOnly))
            out.events.append(eventFromFrontMatter(parseFrontMatter(ef.readAll())));
    }
    recomputeMetadata(out); // also tallies votes
    return true;
}

// ---- Mirror (read-only) ----------------------------------------------------

QString IssueStore::mirrorRef() const
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

QByteArray IssueStore::showFromMirror(const QString &repoRelPath, bool *ok) const
{
    const QString ref = mirrorRef();
    QByteArray output;
    const bool good = !ref.isEmpty() &&
                      runGit(m_mirror, {"show", ref + ":" + repoRelPath}, &output);
    if (ok)
        *ok = good;
    return good ? output : QByteArray();
}

QList<Issue> IssueStore::loadFromMirror(QString *error) const
{
    QList<Issue> issues;
    if (m_mirror.isEmpty()) {
        if (error)
            *error = QStringLiteral("No local mirror to read issues from.");
        return issues;
    }
    const QString ref = mirrorRef();
    if (ref.isEmpty())
        return issues;

    QByteArray listing;
    if (!runGit(m_mirror, {"ls-tree", ref, "issues/"}, &listing))
        return issues;
    for (const QString &line :
         QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts)) {
        const int tab = line.indexOf('\t');
        if (tab < 0 || !line.contains(" tree "))
            continue;
        const QString base = line.mid(tab + 1).section('/', -1); // <n>
        bool numeric = false;
        const int number = base.toInt(&numeric);
        if (!numeric)
            continue;

        bool ok = false;
        const QByteArray issueMd =
            showFromMirror("issues/" + base + "/issue.md", &ok);
        if (!ok)
            continue;
        const FrontMatter fm = parseFrontMatter(issueMd);
        Issue issue;
        issue.number = number;
        issue.title = fm.get("title");
        issue.status = fm.values.contains("status") ? fm.get("status")
                                                    : QStringLiteral("open");
        issue.labels = fm.list("labels");
        issue.milestone = fm.get("milestone");
        issue.assignees = fm.list("assignees");
        issue.createdAt = fm.num("createdAt");
        issue.author = fm.get("author");
        issue.authorName = fm.get("authorName");
        IssueEvent open = eventFromFrontMatter(fm);
        open.type = "open";
        open.title = issue.title;
        issue.events.append(open);

        // Enumerate this issue's event files from the mirror.
        QByteArray dirListing;
        if (runGit(m_mirror, {"ls-tree", ref, "issues/" + base + "/"}, &dirListing)) {
            QStringList names;
            for (const QString &l :
                 QString::fromUtf8(dirListing).split('\n', Qt::SkipEmptyParts)) {
                const int t = l.indexOf('\t');
                if (t < 0 || !l.contains(" blob "))
                    continue;
                const QString fname = l.mid(t + 1).section('/', -1);
                if (eventFileRe().match(fname).hasMatch())
                    names << fname;
            }
            names.sort();
            for (const QString &name : names) {
                bool eok = false;
                const QByteArray ev =
                    showFromMirror("issues/" + base + "/" + name, &eok);
                if (eok)
                    issue.events.append(eventFromFrontMatter(parseFrontMatter(ev)));
            }
        }
        recomputeMetadata(issue); // tally votes (and fold event metadata)
        if (!issue.isDeleted())
            issues.append(issue);
    }
    std::sort(issues.begin(), issues.end(),
              [](const Issue &a, const Issue &b) { return a.number < b.number; });
    return issues;
}

QList<IssueLabel> IssueStore::loadLabels() const
{
    QByteArray bytes;
    if (canWrite()) {
        QFile file(issuesDir() + "/labels.json");
        if (file.open(QIODevice::ReadOnly))
            bytes = file.readAll();
    } else {
        bool ok = false;
        bytes = showFromMirror("issues/labels.json", &ok);
    }
    QList<IssueLabel> labels;
    for (const QJsonValue &value : QJsonDocument::fromJson(bytes).array()) {
        const QJsonObject obj = value.toObject();
        labels.append({obj.value("name").toString(), obj.value("color").toString()});
    }
    return labels;
}

QList<IssueMilestone> IssueStore::loadMilestones() const
{
    QByteArray bytes;
    if (canWrite()) {
        QFile file(issuesDir() + "/milestones.json");
        if (file.open(QIODevice::ReadOnly))
            bytes = file.readAll();
    } else {
        bool ok = false;
        bytes = showFromMirror("issues/milestones.json", &ok);
    }
    QList<IssueMilestone> milestones;
    for (const QJsonValue &value : QJsonDocument::fromJson(bytes).array()) {
        const QJsonObject obj = value.toObject();
        milestones.append({obj.value("title").toString(),
                           qint64(obj.value("due").toDouble()),
                           obj.value("status").toString("open"),
                           obj.value("description").toString()});
    }
    return milestones;
}

// ---- Writing ---------------------------------------------------------------

bool IssueStore::writeIssueFile(const Issue &issue, QString *error) const
{
    QDir().mkpath(issueDir(issue.number));
    if (issue.events.isEmpty())
        return false;
    const IssueEvent &open = issue.events.first();

    // issue.md: issue-level metadata + the open event + the description body.
    QStringList lines;
    lines << "---";
    lines << "schema: forkmesh-issue-v1";
    lines << "number: " + QString::number(issue.number);
    lines << "title: " + issue.title;
    lines << "status: " + issue.status;
    lines << "labels: " + serializeList(issue.labels);
    lines << "milestone: " + issue.milestone;
    lines << "assignees: " + serializeList(issue.assignees);
    lines << "createdAt: " + QString::number(issue.createdAt);
    lines << "author: " + issue.author;
    lines << "authorName: " + issue.authorName;
    lines << "type: open";
    lines << "id: " + open.id;
    lines << "ts: " + QString::number(open.ts);
    lines << "attachments: " + serializeList(open.attachments);
    lines << "sig: " + open.sig;
    lines << "---";
    lines << "";
    const QString issueText = lines.join('\n') + "\n" + open.body + "\n";
    if (!writeTextFile(issueDir(issue.number) + "/issue.md", issueText, error))
        return false;

    // Each subsequent event is its own NNNN-<type>.md file.
    for (int i = 1; i < issue.events.size(); ++i) {
        const IssueEvent &ev = issue.events.at(i);
        QStringList evLines;
        evLines << "---";
        appendEventFields(evLines, ev);
        evLines << "---";
        evLines << "";
        const QString body = (ev.type == "comment" || ev.type == "edit") ? ev.body
                                                                          : QString();
        const QString text = evLines.join('\n') + "\n" + body + "\n";
        const QString name =
            QStringLiteral("%1-%2.md").arg(i + 1, 4, 10, QChar('0')).arg(ev.type);
        if (!writeTextFile(issueDir(issue.number) + "/" + name, text, error))
            return false;
    }
    return true;
}

void IssueStore::recomputeMetadata(Issue &issue) const
{
    QStringList voters;
    for (const IssueEvent &ev : issue.events) {
        if (ev.type == "open")
            issue.title = ev.title;
        else if (ev.type == "status")
            issue.status = ev.status;
        else if (ev.type == "labels")
            issue.labels = ev.labels;
        else if (ev.type == "milestone")
            issue.milestone = ev.milestone;
        else if (ev.type == "assignees")
            issue.assignees = ev.assignees;
        else if (ev.type == "vote" && !ev.author.isEmpty() &&
                 !voters.contains(ev.author))
            voters.append(ev.author);
    }
    issue.votes = voters.size();
}

QStringList IssueStore::copyAttachments(int number, const QStringList &srcPaths) const
{
    QStringList rel;
    QDir().mkpath(issueDir(number));
    for (const QString &src : srcPaths) {
        QFile in(src);
        if (!in.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = in.readAll();
        const QString sha8 = QString::fromLatin1(
            QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().left(8));
        QString ext = QFileInfo(src).suffix().toLower();
        if (ext.isEmpty())
            ext = "bin";
        // Images live directly in the issue folder (no attachments/ subdir).
        const QString name = sha8 + "." + ext;
        QFile out(issueDir(number) + "/" + name);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(data);
            rel << name;
        }
    }
    return rel;
}

bool IssueStore::commit(const QString &message, QString *error) const
{
    QString err;
    if (!runGit(m_workTree, {"add", "issues"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    QByteArray out;
    if (!runGit(m_workTree, {"commit", "-m", message, "--", "issues"}, &out, &err)) {
        if (err.contains("nothing to commit") || err.isEmpty())
            return true;
        if (error)
            *error = "git commit failed: " + err;
        return false;
    }
    return true;
}

int IssueStore::nextNumber() const
{
    int max = 0;
    const QStringList entries =
        QDir(issuesDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool numeric = false;
        const int n = entry.toInt(&numeric);
        if (numeric && n > max)
            max = n;
    }
    return max + 1;
}

// ---- Mutations -------------------------------------------------------------

int IssueStore::createIssue(const QString &title, const QString &body,
                            const QStringList &labels, const QString &milestone,
                            const QStringList &assignees,
                            const QStringList &attachmentSrcPaths, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return -1;
    }
    const int number = nextNumber();
    QDir().mkpath(issueDir(number));

    IssueEvent ev;
    ev.type = "open";
    ev.id = QStringLiteral("open-%1").arg(number);
    ev.title = title;
    ev.body = stripEdgeNewlines(body);
    ev.attachments = copyAttachments(number, attachmentSrcPaths);
    ev = makeSignedEvent(number, ev);

    Issue issue;
    issue.number = number;
    issue.title = title;
    issue.status = "open";
    issue.labels = labels;
    issue.milestone = milestone;
    issue.assignees = assignees;
    issue.createdAt = ev.ts;
    issue.author = ev.author;
    issue.authorName = ev.authorName;
    issue.events.append(ev);

    if (!writeIssueFile(issue, error))
        return -1;
    if (!commit(QStringLiteral("issue #%1: %2").arg(number).arg(title), error))
        return -1;
    return number;
}

bool IssueStore::addComment(int number, const QString &body,
                            const QStringList &attachmentSrcPaths, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    Issue issue;
    if (!readIssueFile(number, issue)) {
        if (error)
            *error = QStringLiteral("Issue #%1 not found.").arg(number);
        return false;
    }
    IssueEvent ev;
    ev.type = "comment";
    ev.body = stripEdgeNewlines(body);
    ev.attachments = copyAttachments(number, attachmentSrcPaths);
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: comment").arg(number), error);
}

bool IssueStore::addVote(int number, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    Issue issue;
    if (!readIssueFile(number, issue)) {
        if (error)
            *error = QStringLiteral("Issue #%1 not found.").arg(number);
        return false;
    }
    const QString me = m_identity ? m_identity->publicKey() : QString();
    for (const IssueEvent &existing : std::as_const(issue.events))
        if (existing.type == "vote" && existing.author == me) {
            if (error)
                *error = QStringLiteral("You have already voted on this issue.");
            return false;
        }
    IssueEvent ev;
    ev.type = "vote";
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: vote").arg(number), error);
}

bool IssueStore::editEvent(int number, const QString &eventId, const QString &newBody,
                           QString *error)
{
    if (!canWrite())
        return false;
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "edit";
    ev.target = eventId;
    ev.body = stripEdgeNewlines(newBody);
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: edit").arg(number), error);
}

bool IssueStore::setStatus(int number, const QString &status, QString *error)
{
    if (!canWrite())
        return false;
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "status";
    ev.status = status;
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    recomputeMetadata(issue);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: %2").arg(number).arg(status), error);
}

bool IssueStore::setLabels(int number, const QStringList &labels, QString *error)
{
    if (!canWrite())
        return false;
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "labels";
    ev.labels = labels;
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    recomputeMetadata(issue);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: labels").arg(number), error);
}

bool IssueStore::setMilestone(int number, const QString &milestone, QString *error)
{
    if (!canWrite())
        return false;
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "milestone";
    ev.milestone = milestone;
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    recomputeMetadata(issue);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: milestone").arg(number), error);
}

bool IssueStore::setAssignees(int number, const QStringList &assignees, QString *error)
{
    if (!canWrite())
        return false;
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "assignees";
    ev.assignees = assignees;
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    recomputeMetadata(issue);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: assignees").arg(number), error);
}

bool IssueStore::deleteEvent(int number, const QString &eventId, QString *error)
{
    if (!canWrite())
        return false;
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "delete";
    ev.target = eventId;
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: delete comment").arg(number), error);
}

bool IssueStore::deleteIssue(int number, QString *error)
{
    if (!canWrite())
        return false;
    if (!QDir(issueDir(number)).removeRecursively()) {
        if (error)
            *error = QStringLiteral("Could not remove issue folder.");
        return false;
    }
    return commit(QStringLiteral("issue #%1: deleted").arg(number), error);
}

// ---- Label / milestone definition lists ------------------------------------

bool IssueStore::saveLabels(const QList<IssueLabel> &labels, QString *error)
{
    if (!canWrite())
        return false;
    QJsonArray array;
    for (const IssueLabel &label : labels)
        array.append(QJsonObject{{"name", label.name}, {"color", label.color}});
    QDir().mkpath(issuesDir());
    if (!writeTextFile(issuesDir() + "/labels.json",
                       QString::fromUtf8(QJsonDocument(array).toJson()), error))
        return false;
    return commit(QStringLiteral("issues: update labels"), error);
}

bool IssueStore::saveMilestones(const QList<IssueMilestone> &milestones, QString *error)
{
    if (!canWrite())
        return false;
    QJsonArray array;
    for (const IssueMilestone &ms : milestones)
        array.append(QJsonObject{{"title", ms.title}, {"due", double(ms.due)},
                                 {"status", ms.status}, {"description", ms.description}});
    QDir().mkpath(issuesDir());
    if (!writeTextFile(issuesDir() + "/milestones.json",
                       QString::fromUtf8(QJsonDocument(array).toJson()), error))
        return false;
    return commit(QStringLiteral("issues: update milestones"), error);
}

// ---- Cross-user sync (merge an inbound signed event) -----------------------

bool IssueStore::applyRemoteEvent(int number, const IssueEvent &ev,
                                  const QString &titleIfNew, QString *error)
{
    if (!canWrite())
        return false;

    Issue issue;
    const bool exists = readIssueFile(number, issue);
    if (!exists) {
        issue = Issue();
        issue.number = number;
        issue.title = ev.type == "open" ? ev.title : titleIfNew;
        issue.status = "open";
        issue.createdAt = ev.ts;
        issue.author = ev.author;
        issue.authorName = ev.authorName;
        if (ev.type == "open") {
            issue.events.append(ev);
            recomputeMetadata(issue);
            if (!writeIssueFile(issue, error))
                return false;
            return commit(QStringLiteral("issue #%1: opened (from %2)")
                              .arg(number)
                              .arg(ev.authorName.isEmpty() ? ev.author.left(8)
                                                           : ev.authorName),
                          error);
        }
        // A non-open event for a missing issue: synthesize a placeholder open.
        IssueEvent placeholder;
        placeholder.type = "open";
        placeholder.id = QStringLiteral("open-%1").arg(number);
        placeholder.title = titleIfNew;
        placeholder.author = ev.author;
        placeholder.authorName = ev.authorName;
        placeholder.ts = ev.ts;
        issue.events.append(placeholder);
    }
    // One vote per author: ignore a repeat vote from someone we already counted.
    if (ev.type == "vote") {
        for (const IssueEvent &existing : std::as_const(issue.events))
            if (existing.type == "vote" && existing.author == ev.author)
                return true;
    }
    issue.events.append(ev);
    recomputeMetadata(issue);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(
        QStringLiteral("issue #%1: %2 (from %3)")
            .arg(number)
            .arg(ev.type, ev.authorName.isEmpty() ? ev.author.left(8) : ev.authorName),
        error);
}
