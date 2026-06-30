#include "IssueStore.h"

#include "ForkMeshIdentity.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QPair>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace {

constexpr int kGitTimeoutMs = 10000;
constexpr int kGitRewriteTimeoutMs = 120000;

// Run git in `dir`, capturing stdout. Returns false (with optional error text)
// on non-zero exit or timeout. Mirrors RepoHost's helper.
bool runGit(const QString &dir, const QStringList &args, QByteArray *output = nullptr,
            QString *errText = nullptr, int timeoutMs = kGitTimeoutMs)
{
    QProcess process;
    if (args.contains(QStringLiteral("filter-branch"))) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("FILTER_BRANCH_SQUELCH_WARNING"), QStringLiteral("1"));
        process.setProcessEnvironment(env);
    }
    process.start("git", QStringList{"-C", dir} + args);
    if (!process.waitForFinished(timeoutMs)) {
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

// Like runGit, but feeds `input` to the process's stdin. Used for batched reads
// (`git cat-file --batch`) so a whole tree of blobs is fetched in one process
// instead of one `git show` per file. waitForFinished services both channels, so
// large input/output won't deadlock the pipes.
bool runGitInput(const QString &dir, const QStringList &args, const QByteArray &input,
                 QByteArray *output, QString *errText = nullptr,
                 int timeoutMs = kGitTimeoutMs)
{
    QProcess process;
    process.start("git", QStringList{"-C", dir} + args);
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs)) {
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
    double dbl(const QString &key) const { return values.value(key).toDouble(); }
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

bool removeOriginalRefs(const QString &workTree, QString *error)
{
    QByteArray refs;
    QString err;
    if (!runGit(workTree, {"for-each-ref", "--format=%(refname)", "refs/original"},
                &refs, &err, kGitRewriteTimeoutMs)) {
        if (error)
            *error = QStringLiteral("git for-each-ref failed: ") + err;
        return false;
    }
    const QList<QByteArray> lines = refs.split('\n');
    for (const QByteArray &line : lines) {
        const QString ref = QString::fromUtf8(line).trimmed();
        if (ref.isEmpty())
            continue;
        if (!runGit(workTree, {"update-ref", "-d", ref}, nullptr, &err,
                    kGitRewriteTimeoutMs)) {
            if (error)
                *error = QStringLiteral("git update-ref failed: ") + err;
            return false;
        }
    }
    return true;
}

bool hasUnrelatedTrackedChanges(const QString &workTree, const QString &relPath,
                                QString *error)
{
    QByteArray status;
    QString err;
    if (!runGit(workTree, {"status", "--porcelain", "--untracked-files=no"}, &status,
                &err)) {
        if (error)
            *error = QStringLiteral("git status failed: ") + err;
        return true;
    }
    const QString prefix = relPath + "/";
    const QList<QByteArray> lines = status.split('\n');
    for (const QByteArray &line : lines) {
        if (line.size() < 4)
            continue;
        const QString path = QString::fromUtf8(line.mid(3)).trimmed();
        if (path != relPath && !path.startsWith(prefix)) {
            if (error) {
                *error = QStringLiteral(
                    "Commit or stash unrelated tracked changes before deleting an issue.");
            }
            return true;
        }
    }
    return false;
}

const QRegularExpression &eventFileRe()
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}-.+\\.md$"));
    return re;
}

QString pendingAttachmentPlaceholder(int index)
{
    return QStringLiteral("forkmesh-pending-image:%1").arg(index);
}

QStringList effectiveAttachmentPlaceholders(const QStringList &srcPaths,
                                            const QStringList &placeholders)
{
    if (!placeholders.isEmpty())
        return placeholders;
    QStringList inferred;
    for (int i = 0; i < srcPaths.size(); ++i)
        inferred << pendingAttachmentPlaceholder(i);
    return inferred;
}

QString replaceAttachmentPlaceholders(QString body, const QStringList &srcPaths,
                                      const QStringList &placeholders,
                                      const QStringList &copiedAttachments)
{
    const QStringList effective =
        effectiveAttachmentPlaceholders(srcPaths, placeholders);
    const int count = std::min(effective.size(), copiedAttachments.size());
    QList<int> order;
    order.reserve(count);
    for (int i = 0; i < count; ++i)
        order.append(i);
    std::sort(order.begin(), order.end(), [&effective](int a, int b) {
        return effective.at(a).size() > effective.at(b).size();
    });
    for (int i : order) {
        if (!effective.at(i).isEmpty())
            body.replace(effective.at(i), copiedAttachments.at(i));
    }
    return body;
}

bool copiedEveryAttachment(const QStringList &srcPaths,
                           const QStringList &copiedAttachments,
                           QString *error)
{
    if (copiedAttachments.size() == srcPaths.size())
        return true;
    if (error) {
        *error = QStringLiteral("Could not copy %1 of %2 attachment(s).")
                     .arg(srcPaths.size() - copiedAttachments.size())
                     .arg(srcPaths.size());
    }
    return false;
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
    if (type == "priority")
        obj.insert("priority", priority);
    if (type == "progress")
        obj.insert("progress", progress);
    if (type == "bounty") {
        obj.insert("bountyUsd", bountyUsd);
        obj.insert("bountyAddress", bountyAddress);
        obj.insert("bountyStatus", bountyStatus);
    }
    if (type == "assignees")
        obj.insert("assignees", fromStringList(assignees));
    if (type == "agent") {
        obj.insert("agentProvider", agentProvider);
        obj.insert("agentSessionId", agentSessionId);
        obj.insert("agentStatus", agentStatus);
        obj.insert("agentCreatePr", agentCreatePr);
    }
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
    ev.priority = obj.value("priority").toInt();
    ev.progress = obj.value("progress").toInt();
    ev.bountyUsd = obj.value("bountyUsd").toDouble();
    ev.bountyAddress = obj.value("bountyAddress").toString();
    ev.bountyStatus = obj.value("bountyStatus").toString();
    ev.assignees = toStringList(obj.value("assignees").toArray());
    ev.agentProvider = obj.value("agentProvider").toString();
    ev.agentSessionId = obj.value("agentSessionId").toInt();
    ev.agentStatus = obj.value("agentStatus").toString();
    ev.agentCreatePr = obj.value("agentCreatePr").toBool();
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
            {"milestone", milestone}, {"priority", priority}, {"progress", progress},
            {"assignees", fromStringList(assignees)},
            {"createdAt", double(createdAt)}, {"author", author},
            {"authorName", authorName}, {"bountyUsd", bountyUsd},
            {"bountyAddress", bountyAddress}, {"bountyStatus", bountyStatus},
            {"events", eventsArray}};
}

Issue Issue::fromJson(const QJsonObject &obj)
{
    Issue issue;
    issue.number = obj.value("number").toInt();
    issue.title = obj.value("title").toString();
    issue.status = obj.value("status").toString("open");
    issue.labels = toStringList(obj.value("labels").toArray());
    issue.milestone = obj.value("milestone").toString();
    issue.priority = obj.value("priority").toInt();
    issue.progress = obj.value("progress").toInt();
    issue.assignees = toStringList(obj.value("assignees").toArray());
    issue.createdAt = qint64(obj.value("createdAt").toDouble());
    issue.author = obj.value("author").toString();
    issue.authorName = obj.value("authorName").toString();
    issue.bountyUsd = obj.value("bountyUsd").toDouble();
    issue.bountyAddress = obj.value("bountyAddress").toString();
    issue.bountyStatus = obj.value("bountyStatus").toString();
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
    if (ev.type == "title")
        return ev.title;
    if (ev.type == "status")
        return ev.status;
    if (ev.type == "labels")
        return ev.labels.join(",");
    if (ev.type == "milestone")
        return ev.milestone;
    if (ev.type == "priority")
        return QString::number(ev.priority);
    if (ev.type == "progress")
        return QString::number(ev.progress);
    if (ev.type == "bounty")
        return QString::number(ev.bountyUsd, 'f', 2) + nul + ev.bountyAddress + nul +
               ev.bountyStatus;
    if (ev.type == "assignees")
        return ev.assignees.join(",");
    if (ev.type == "agent")
        return ev.agentProvider + nul + QString::number(ev.agentSessionId) + nul +
               ev.agentStatus + nul + (ev.agentCreatePr ? "pr" : "no-pr");
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
    if (ev.type == "title")
        lines << "title: " + ev.title;
    if (ev.type == "status")
        lines << "status: " + ev.status;
    if (ev.type == "labels")
        lines << "labels: " + serializeList(ev.labels);
    if (ev.type == "milestone")
        lines << "milestone: " + ev.milestone;
    if (ev.type == "priority")
        lines << "priority: " + QString::number(ev.priority);
    if (ev.type == "progress")
        lines << "progress: " + QString::number(ev.progress);
    if (ev.type == "bounty") {
        lines << "bountyUsd: " + QString::number(ev.bountyUsd, 'f', 2);
        lines << "bountyAddress: " + ev.bountyAddress;
        lines << "bountyStatus: " + ev.bountyStatus;
    }
    if (ev.type == "assignees")
        lines << "assignees: " + serializeList(ev.assignees);
    if (ev.type == "agent") {
        lines << "agentProvider: " + ev.agentProvider;
        lines << "agentSessionId: " + QString::number(ev.agentSessionId);
        lines << "agentStatus: " + ev.agentStatus;
        lines << "agentCreatePr: " + QString(ev.agentCreatePr ? "true" : "false");
    }
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
    ev.priority = int(fm.num("priority"));
    ev.progress = int(fm.num("progress"));
    ev.bountyUsd = fm.dbl("bountyUsd");
    ev.bountyAddress = fm.get("bountyAddress");
    ev.bountyStatus = fm.get("bountyStatus");
    ev.assignees = fm.list("assignees");
    ev.agentProvider = fm.get("agentProvider");
    ev.agentSessionId = fm.num("agentSessionId");
    ev.agentStatus = fm.get("agentStatus");
    ev.agentCreatePr = fm.get("agentCreatePr") == QLatin1String("true");
    ev.attachments = fm.list("attachments");
    ev.sig = fm.get("sig");
    ev.body = fm.body;
    return ev;
}

} // namespace

// ---- Loading ---------------------------------------------------------------

QList<Issue> IssueStore::loadAll(QString *error, const std::function<void()> &tick) const
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
        // readIssueFile opens issue.md plus every event file; across a big repo
        // (hundreds of issues, thousands of files) this loop blocks the GUI long
        // enough to trip the stall watchdog. Let an interactive caller pump.
        if (tick)
            tick();
    }
    std::sort(issues.begin(), issues.end(),
              [](const Issue &a, const Issue &b) { return a.number < b.number; });
    return issues;
}

QString IssueStore::contentSignature() const
{
    // Resolve the issues/ subtree to its git object id. The oid is a content hash
    // of the whole subtree, so it moves iff some issue/event/label/milestone file
    // changed — exactly when a reload would surface something new. A writable store
    // commits every mutation (createIssue / applyRemoteEvent / set*), so HEAD:issues
    // matches the on-disk issues/ that loadAll() reads there; the mirror case reads
    // the same ref it loads blobs from.
    const QString dir = canWrite() ? m_workTree : m_mirror;
    if (dir.isEmpty())
        return QString();
    const QString ref = canWrite() ? QStringLiteral("HEAD") : mirrorRef();
    if (ref.isEmpty())
        return QString();
    // -q + non-zero exit when issues/ doesn't exist yet (no issues): we ignore the
    // bool and fold the empty oid into a stable "no issues" signature.
    QByteArray oid;
    runGit(dir, {"rev-parse", "--verify", "-q", ref + QStringLiteral(":issues")}, &oid);
    // Prefix with the source path so two repos with no issues (both empty oid) — or
    // a coincidental oid match — can't be mistaken for "unchanged" across a switch.
    return dir + QLatin1Char('\n') + QString::fromUtf8(oid).trimmed();
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
    out.priority = int(fm.num("priority"));
    out.progress = int(fm.num("progress"));
    out.assignees = fm.list("assignees");
    out.createdAt = fm.num("createdAt");
    out.author = fm.get("author");
    out.authorName = fm.get("authorName");
    out.bountyUsd = fm.dbl("bountyUsd");
    out.bountyAddress = fm.get("bountyAddress");
    out.bountyStatus = fm.get("bountyStatus");

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
    // The mirror doesn't change while a single store reads it, but mirrorRef() is
    // hit once per blob fetched from the mirror. Resolving it via git every time
    // spawns hundreds of subprocesses on the GUI thread; memoize for our lifetime.
    if (m_mirrorRefResolved)
        return m_cachedMirrorRef;
    m_mirrorRefResolved = true;

    QByteArray output;
    if (runGit(m_mirror, {"rev-parse", "--verify", "-q", "HEAD"}, &output) &&
        !output.trimmed().isEmpty()) {
        m_cachedMirrorRef = QStringLiteral("HEAD");
    } else if (runGit(m_mirror,
                      {"for-each-ref", "--format=%(refname)", "--count=1",
                       "refs/heads/"},
                      &output)) {
        m_cachedMirrorRef = QString::fromUtf8(output).trimmed();
    }
    return m_cachedMirrorRef;
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

    // Read the whole issues/ subtree in one recursive listing, then fetch every
    // needed blob in a single `git cat-file --batch`. The old code spawned a
    // `git show` per file (plus a per-issue `ls-tree`), which stalled the GUI
    // thread for seconds on repos with many issues/events.
    QByteArray listing;
    if (!runGit(m_mirror, {"ls-tree", "-r", ref, "issues/"}, &listing))
        return issues;

    struct MirrorIssue {
        QString issueOid;                          // blob oid of issue.md
        QList<QPair<QString, QString>> eventBlobs; // (filename, oid)
    };
    QMap<int, MirrorIssue> byNumber; // keyed (and thus sorted) by issue number
    QSet<QString> wantedOids;
    for (const QString &line :
         QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts)) {
        const int tab = line.indexOf('\t');
        if (tab < 0)
            continue;
        const QStringList meta = line.left(tab).split(' ', Qt::SkipEmptyParts);
        if (meta.size() < 3 || meta.at(1) != QStringLiteral("blob"))
            continue;
        const QString oid = meta.at(2);
        // path is "issues/<n>/<file>"; ignore top-level files (labels.json …)
        // and anything nested deeper (attachments/…).
        const QString rel = line.mid(tab + 1).section('/', 1); // strip "issues/"
        const int slash = rel.indexOf('/');
        if (slash < 0)
            continue;
        bool numeric = false;
        const int number = rel.left(slash).toInt(&numeric);
        if (!numeric)
            continue;
        const QString fname = rel.mid(slash + 1);
        if (fname.contains('/'))
            continue;
        if (fname == QStringLiteral("issue.md")) {
            byNumber[number].issueOid = oid;
            wantedOids.insert(oid);
        } else if (eventFileRe().match(fname).hasMatch()) {
            byNumber[number].eventBlobs.append({fname, oid});
            wantedOids.insert(oid);
        }
    }
    if (byNumber.isEmpty())
        return issues;

    // Batch-fetch every blob in one process. Output framing per object is
    // "<oid> <type> <size>\n<size bytes>\n".
    QByteArray batchInput;
    for (const QString &oid : wantedOids)
        batchInput += oid.toUtf8() + '\n';
    QByteArray batch;
    if (!runGitInput(m_mirror, {"cat-file", "--batch"}, batchInput, &batch))
        return issues;

    QHash<QString, QByteArray> contentByOid;
    contentByOid.reserve(wantedOids.size());
    for (int pos = 0; pos < batch.size();) {
        const int nl = batch.indexOf('\n', pos);
        if (nl < 0)
            break;
        const QList<QByteArray> header = batch.mid(pos, nl - pos).split(' ');
        pos = nl + 1;
        if (header.size() < 3) // "<oid> missing" or malformed — no body follows
            continue;
        bool sizeOk = false;
        const int size = header.at(2).toInt(&sizeOk);
        if (!sizeOk || pos + size > batch.size())
            break;
        contentByOid.insert(QString::fromUtf8(header.at(0)), batch.mid(pos, size));
        pos += size + 1; // skip body and its trailing newline
    }

    for (auto it = byNumber.constBegin(); it != byNumber.constEnd(); ++it) {
        const MirrorIssue &files = it.value();
        if (files.issueOid.isEmpty() || !contentByOid.contains(files.issueOid))
            continue;
        const FrontMatter fm = parseFrontMatter(contentByOid.value(files.issueOid));
        Issue issue;
        issue.number = it.key();
        issue.title = fm.get("title");
        issue.status = fm.values.contains("status") ? fm.get("status")
                                                    : QStringLiteral("open");
        issue.labels = fm.list("labels");
        issue.milestone = fm.get("milestone");
        issue.priority = int(fm.num("priority"));
        issue.progress = int(fm.num("progress"));
        issue.assignees = fm.list("assignees");
        issue.createdAt = fm.num("createdAt");
        issue.author = fm.get("author");
        issue.authorName = fm.get("authorName");
        issue.bountyUsd = fm.dbl("bountyUsd");
        issue.bountyAddress = fm.get("bountyAddress");
        issue.bountyStatus = fm.get("bountyStatus");
        IssueEvent open = eventFromFrontMatter(fm);
        open.type = "open";
        open.title = issue.title;
        issue.events.append(open);

        // Subsequent events in filename (chronological) order.
        QList<QPair<QString, QString>> events = files.eventBlobs;
        std::sort(events.begin(), events.end(),
                  [](const QPair<QString, QString> &a,
                     const QPair<QString, QString> &b) { return a.first < b.first; });
        for (const QPair<QString, QString> &ev : events) {
            if (contentByOid.contains(ev.second))
                issue.events.append(
                    eventFromFrontMatter(parseFrontMatter(contentByOid.value(ev.second))));
        }
        recomputeMetadata(issue); // tally votes (and fold event metadata)
        if (!issue.isDeleted())
            issues.append(issue);
    }
    // byNumber iterates in ascending key order, so issues is already sorted.
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
    lines << "priority: " + QString::number(issue.priority);
    lines << "progress: " + QString::number(issue.progress);
    lines << "assignees: " + serializeList(issue.assignees);
    lines << "createdAt: " + QString::number(issue.createdAt);
    lines << "author: " + issue.author;
    lines << "authorName: " + issue.authorName;
    lines << "bountyUsd: " + QString::number(issue.bountyUsd, 'f', 2);
    lines << "bountyAddress: " + issue.bountyAddress;
    lines << "bountyStatus: " + issue.bountyStatus;
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
    int voteCount = 0;
    for (const IssueEvent &ev : issue.events) {
        if (ev.type == "open")
            issue.title = ev.title;
        else if (ev.type == "title" && !ev.title.isEmpty())
            issue.title = ev.title;  // later title events rename the issue
        else if (ev.type == "status")
            issue.status = ev.status;
        else if (ev.type == "labels")
            issue.labels = ev.labels;
        else if (ev.type == "milestone")
            issue.milestone = ev.milestone;
        else if (ev.type == "priority")
            issue.priority = ev.priority;
        else if (ev.type == "progress")
            issue.progress = ev.progress;
        else if (ev.type == "bounty") {
            issue.bountyUsd = ev.bountyUsd;
            issue.bountyAddress = ev.bountyAddress;
            issue.bountyStatus = ev.bountyStatus;
        } else if (ev.type == "assignees")
            issue.assignees = ev.assignees;
        else if (ev.type == "vote" && !ev.author.isEmpty())
            // Voters may now spend multiple credits on the same issue, so every
            // signed vote event counts (no longer one-per-author).
            ++voteCount;
    }
    issue.votes = voteCount;
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
                            int priority,
                            const QStringList &assignees,
                            const QStringList &attachmentSrcPaths, QString *error)
{
    return createIssue(title, body, labels, milestone, priority, assignees,
                       attachmentSrcPaths, {}, error);
}

int IssueStore::createIssue(const QString &title, const QString &body,
                            const QStringList &labels, const QString &milestone,
                            int priority,
                            const QStringList &assignees,
                            const QStringList &attachmentSrcPaths,
                            const QStringList &attachmentPlaceholders, QString *error)
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
    ev.attachments = copyAttachments(number, attachmentSrcPaths);
    if (!copiedEveryAttachment(attachmentSrcPaths, ev.attachments, error))
        return -1;
    ev.body = stripEdgeNewlines(replaceAttachmentPlaceholders(
        body, attachmentSrcPaths, attachmentPlaceholders, ev.attachments));
    ev = makeSignedEvent(number, ev);

    Issue issue;
    issue.number = number;
    issue.title = title;
    issue.status = "open";
    issue.labels = labels;
    issue.milestone = milestone;
    issue.priority = qBound(0, priority, 99);
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
    return addComment(number, body, attachmentSrcPaths, {}, error);
}

bool IssueStore::addComment(int number, const QString &body,
                            const QStringList &attachmentSrcPaths,
                            const QStringList &attachmentPlaceholders, QString *error)
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
    ev.attachments = copyAttachments(number, attachmentSrcPaths);
    if (!copiedEveryAttachment(attachmentSrcPaths, ev.attachments, error))
        return false;
    ev.body = stripEdgeNewlines(replaceAttachmentPlaceholders(
        body, attachmentSrcPaths, attachmentPlaceholders, ev.attachments));
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
    // Multiple votes per author are allowed now (each spends a voting credit on
    // the client), so we no longer reject a repeat vote from the same node.
    IssueEvent ev;
    ev.type = "vote";
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: vote").arg(number), error);
}

bool IssueStore::editEvent(int number, const QString &eventId, const QString &newBody,
                           const QStringList &keepAttachments,
                           const QStringList &newAttachmentSrcPaths, QString *error)
{
    return editEvent(number, eventId, newBody, keepAttachments,
                     newAttachmentSrcPaths, {}, error);
}

bool IssueStore::editEvent(int number, const QString &eventId, const QString &newBody,
                           const QStringList &keepAttachments,
                           const QStringList &newAttachmentSrcPaths,
                           const QStringList &newAttachmentPlaceholders, QString *error)
{
    if (!canWrite())
        return false;
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "edit";
    ev.target = eventId;
    // An edit overwrites the target's attachment set, so carry forward the ones
    // being kept (already in the issue folder) and copy in any newly added.
    ev.attachments = keepAttachments;
    const QStringList copiedAttachments = copyAttachments(number, newAttachmentSrcPaths);
    if (!copiedEveryAttachment(newAttachmentSrcPaths, copiedAttachments, error))
        return false;
    ev.attachments += copiedAttachments;
    ev.body = stripEdgeNewlines(replaceAttachmentPlaceholders(
        newBody, newAttachmentSrcPaths, newAttachmentPlaceholders, copiedAttachments));
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: edit").arg(number), error);
}

bool IssueStore::setTitle(int number, const QString &newTitle, QString *error)
{
    if (!canWrite())
        return false;
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "title";
    ev.title = newTitle.trimmed();
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    recomputeMetadata(issue);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: retitle").arg(number), error);
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

bool IssueStore::setPriority(int number, int priority, QString *error)
{
    if (!canWrite())
        return false;
    if (priority < 0 || priority > 99) {
        if (error)
            *error = QStringLiteral(
                "Priority must be between 1 and 99, or 0 to clear it.");
        return false;
    }
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "priority";
    ev.priority = priority;
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    recomputeMetadata(issue);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: priority %2")
                      .arg(number)
                      .arg(priority == 0 ? QStringLiteral("cleared")
                                         : QString::number(priority)),
                  error);
}

bool IssueStore::setProgress(int number, int progress, QString *error)
{
    if (!canWrite())
        return false;
    if (progress < 0 || progress > 100) {
        if (error)
            *error = QStringLiteral("Progress must be between 0 and 100.");
        return false;
    }
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "progress";
    ev.progress = progress;
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    recomputeMetadata(issue);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: progress %2%").arg(number).arg(progress),
                  error);
}

bool IssueStore::setBounty(int number, double amountUsd, const QString &address,
                           const QString &status, QString *error)
{
    if (!canWrite())
        return false;
    if (amountUsd < 0) {
        if (error)
            *error = QStringLiteral("Bounty amount must not be negative.");
        return false;
    }
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "bounty";
    ev.bountyUsd = amountUsd;
    ev.bountyAddress = address;
    ev.bountyStatus = status.isEmpty() ? QStringLiteral("open") : status;
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    recomputeMetadata(issue);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: bounty $%2 (%3)")
                      .arg(number)
                      .arg(QString::number(amountUsd, 'f', 2), ev.bountyStatus),
                  error);
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

bool IssueStore::assignAgent(int number, const QString &provider, int sessionId,
                             bool createPr, const QString &status, QString *error)
{
    if (!canWrite())
        return false;
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    IssueEvent ev;
    ev.type = "agent";
    ev.agentProvider = provider;
    ev.agentSessionId = sessionId;
    ev.agentCreatePr = createPr;
    ev.agentStatus = status;
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    if (!writeIssueFile(issue, error))
        return false;
    const QString action = (sessionId <= 0 || status == QLatin1String("cleared"))
                               ? QStringLiteral("clear agent")
                               : QStringLiteral("assign agent");
    return commit(QStringLiteral("issue #%1: %2").arg(number).arg(action), error);
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

bool IssueStore::tombstoneIssue(int number, QString *error)
{
    if (!canWrite())
        return false;
    Issue issue;
    if (!readIssueFile(number, issue))
        return false;
    if (issue.isDeleted())
        return true; // already tombstoned; nothing to do
    IssueEvent ev;
    ev.type = "delete";
    ev.target = "self";
    ev = makeSignedEvent(number, ev);
    issue.events.append(ev);
    if (!writeIssueFile(issue, error))
        return false;
    return commit(QStringLiteral("issue #%1: delete").arg(number), error);
}

bool IssueStore::deleteIssue(int number, QString *error)
{
    if (!canWrite())
        return false;
    const QString relPath = QStringLiteral("issues/%1").arg(number);
    QString err;
    if (hasUnrelatedTrackedChanges(m_workTree, relPath, error))
        return false;

    // First commit a normal deletion so the work tree is clean for history
    // rewriting. The rewrite below prunes this commit along with prior
    // issue-only commits.
    runGit(m_workTree, {"rm", "-r", "--ignore-unmatch", "--", relPath}, nullptr,
           nullptr);
    if (QDir(issueDir(number)).exists() &&
        !QDir(issueDir(number)).removeRecursively()) {
        if (error)
            *error = QStringLiteral("Could not remove issue folder.");
        return false;
    }
    if (!runGit(m_workTree, {"commit", "-m", QStringLiteral("delete issue"),
                             "--", relPath},
                nullptr, &err)) {
        if (!err.contains(QStringLiteral("nothing to commit")) && !err.isEmpty()) {
            if (error)
                *error = QStringLiteral("git commit failed: ") + err;
            return false;
        }
    }

    QByteArray refs;
    if (!runGit(m_workTree, {"rev-list", "--all", "--max-count=1"}, &refs, &err,
                kGitRewriteTimeoutMs)) {
        if (error)
            *error = QStringLiteral("git rev-list failed: ") + err;
        return false;
    }
    if (refs.trimmed().isEmpty())
        return true;

    const QString indexFilter =
        QStringLiteral("git rm -r --cached --ignore-unmatch -- %1").arg(relPath);
    QByteArray out;
    if (!runGit(m_workTree,
                {"filter-branch", "--force", "--index-filter", indexFilter,
                 "--prune-empty", "--tag-name-filter", "cat", "--", "--all"},
                &out, &err, kGitRewriteTimeoutMs)) {
        const QString stdoutText = QString::fromUtf8(out);
        if (!err.contains(QStringLiteral("Not a valid object name HEAD")) &&
            !stdoutText.contains(QStringLiteral("was deleted"))) {
            if (error)
                *error = QStringLiteral("git history rewrite failed: ") + err;
            return false;
        }
    }

    if (!runGit(m_workTree, {"rev-parse", "--verify", "HEAD"}, nullptr, nullptr) &&
        !runGit(m_workTree, {"commit", "--allow-empty", "-m",
                             QStringLiteral("Initial commit")},
                nullptr, &err)) {
        if (error)
            *error = QStringLiteral("git commit failed: ") + err;
        return false;
    }
    if (!removeOriginalRefs(m_workTree, error))
        return false;
    if (!runGit(m_workTree, {"reflog", "expire", "--expire=now",
                             "--expire-unreachable=now", "--all"},
                nullptr, &err, kGitRewriteTimeoutMs)) {
        if (error)
            *error = QStringLiteral("git reflog expire failed: ") + err;
        return false;
    }
    if (!runGit(m_workTree, {"gc", "--prune=now"}, nullptr, &err,
                kGitRewriteTimeoutMs)) {
        if (error)
            *error = QStringLiteral("git gc failed: ") + err;
        return false;
    }
    return true;
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
                                  const QString &titleIfNew, QString *error,
                                  const RemoteIssueMeta &meta)
{
    if (!canWrite())
        return false;

    // A brand-new issue ("open") authored elsewhere (e.g. filed from a mirror).
    // The proposed number is only a hint: the open event's id is number-derived
    // ("open-N"), so it isn't a stable identity. We dedup on the signature (which
    // binds content+number+author+ts) across ALL issues so a re-synced submission
    // never duplicates, and we always assign a free number so a collision can't
    // staple a second open event onto an existing issue.
    if (ev.type == "open") {
        const QList<Issue> all = loadAll();
        for (const Issue &i : all)
            for (const IssueEvent &e : i.events) {
                if (e.type != "open")
                    continue;
                const bool sameSig = !ev.sig.isEmpty() && e.sig == ev.sig;
                const bool sameAuthorTs = ev.sig.isEmpty() && e.author == ev.author &&
                                          e.ts == ev.ts && e.title == ev.title;
                if (sameSig || sameAuthorTs)
                    return true; // already merged this submission
            }

        Issue probe;
        if (number <= 0 || readIssueFile(number, probe))
            number = nextNumber(); // proposed slot taken (or unset) -> reassign

        Issue issue;
        issue.number = number;
        issue.title = ev.title;
        issue.status = "open";
        issue.createdAt = ev.ts;
        issue.author = ev.author;
        issue.authorName = ev.authorName;
        // Issue-level metadata isn't part of the open-event signature; apply it
        // as vouched data that rode along with the submission.
        issue.labels = meta.labels;
        issue.milestone = meta.milestone;
        issue.priority = qBound(0, meta.priority, 99);
        issue.assignees = meta.assignees;
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

    Issue issue;
    const bool exists = readIssueFile(number, issue);
    if (!exists) {
        issue = Issue();
        issue.number = number;
        issue.title = titleIfNew;
        issue.status = "open";
        issue.createdAt = ev.ts;
        issue.author = ev.author;
        issue.authorName = ev.authorName;
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
    // Voters may cast several votes (one per credit), so we no longer collapse
    // to one-per-author. We still ignore an event we've already merged (same id)
    // so re-syncing the inbox doesn't double-count the same vote.
    if (!ev.id.isEmpty()) {
        for (const IssueEvent &existing : std::as_const(issue.events))
            if (existing.id == ev.id)
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
