#include "PullStore.h"

#include "ForkMeshIdentity.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
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

// Minimal "---\nkey: value\n---\n\nbody" frontmatter (same subset as IssueStore).
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
        else if (lines.at(i).endsWith(':'))
            fm.values.insert(lines.at(i).left(lines.at(i).size() - 1), QString());
    }
    QString body = lines.mid(i + 1).join('\n');
    while (body.startsWith('\n'))
        body.remove(0, 1);
    while (body.endsWith('\n'))
        body.chop(1);
    fm.body = body;
    return fm;
}

// Subsequent-event filenames: NNNN-<type>.md (same convention as issues/).
// The type may contain hyphens (e.g. "line-comment").
const QRegularExpression &eventFileRe()
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}-[a-z-]+\\.md$"));
    return re;
}

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString stripEdgeNewlines(QString s)
{
    while (s.startsWith('\n'))
        s.remove(0, 1);
    while (s.endsWith('\n'))
        s.chop(1);
    return s;
}

PullEvent eventFromFrontMatter(const FrontMatter &fm)
{
    PullEvent ev;
    ev.type = fm.get("type");
    ev.id = fm.get("id");
    ev.author = fm.get("author");
    ev.authorName = fm.get("authorName");
    ev.ts = fm.num("ts");
    ev.state = fm.get("state");
    ev.path = fm.get("path");
    ev.side = fm.get("side");
    ev.line = int(fm.num("line"));
    ev.sig = fm.get("sig");
    ev.body = fm.body;
    return ev;
}

} // namespace

// ---- PullEvent -------------------------------------------------------------

QJsonObject PullEvent::toJson() const
{
    return {{"type", type},   {"id", id},     {"author", author},
            {"authorName", authorName}, {"ts", double(ts)}, {"body", body},
            {"state", state}, {"path", path}, {"side", side},
            {"line", line},   {"sig", sig}};
}

PullEvent PullEvent::fromJson(const QJsonObject &obj)
{
    PullEvent ev;
    ev.type = obj.value("type").toString();
    ev.id = obj.value("id").toString();
    ev.author = obj.value("author").toString();
    ev.authorName = obj.value("authorName").toString();
    ev.ts = obj.value("ts").toVariant().toLongLong();
    ev.body = obj.value("body").toString();
    ev.state = obj.value("state").toString();
    ev.path = obj.value("path").toString();
    ev.side = obj.value("side").toString();
    ev.line = obj.value("line").toInt();
    ev.sig = obj.value("sig").toString();
    return ev;
}

// ---- PullRequest (JSON wire format) ----------------------------------------

QJsonObject PullRequest::toJson() const
{
    return {{"number", number},   {"title", title},   {"description", description},
            {"base", base},       {"head", head},     {"status", status},
            {"ts", double(ts)},   {"author", author}, {"authorName", authorName},
            {"sig", sig},         {"patch", patch}};
}

PullRequest PullRequest::fromJson(const QJsonObject &obj)
{
    PullRequest pr;
    pr.number = obj.value("number").toInt();
    pr.title = obj.value("title").toString();
    pr.description = obj.value("description").toString();
    pr.base = obj.value("base").toString();
    pr.head = obj.value("head").toString();
    pr.status = obj.value("status").toString("open");
    pr.ts = obj.value("ts").toVariant().toLongLong();
    pr.author = obj.value("author").toString();
    pr.authorName = obj.value("authorName").toString();
    pr.sig = obj.value("sig").toString();
    pr.patch = obj.value("patch").toString();
    PullStore::computeStats(pr);
    return pr;
}

QString PullRequest::reviewSummary() const
{
    // Fold reviews into the latest state per author; a later review supersedes an
    // earlier one from the same node. "commented" reviews don't set a state.
    QHash<QString, QString> latest;
    for (const PullEvent &ev : events) {
        if (ev.type != QLatin1String("review"))
            continue;
        if (ev.state == QLatin1String("approved") ||
            ev.state == QLatin1String("changes_requested"))
            latest.insert(ev.author, ev.state);
        else
            latest.remove(ev.author); // a plain comment-review clears prior state
    }
    bool approved = false;
    for (auto it = latest.constBegin(); it != latest.constEnd(); ++it) {
        if (it.value() == QLatin1String("changes_requested"))
            return QStringLiteral("changes_requested");
        if (it.value() == QLatin1String("approved"))
            approved = true;
    }
    return approved ? QStringLiteral("approved") : QString();
}

// ---- PullStore -------------------------------------------------------------

PullStore::PullStore(QString workTreePath, QString mirrorPath,
                     const ForkMeshIdentity *identity, QString authorName)
    : m_workTree(std::move(workTreePath)), m_mirror(std::move(mirrorPath)),
      m_identity(identity), m_authorName(std::move(authorName))
{
}

bool PullStore::canWrite() const
{
    if (m_workTree.isEmpty() || !m_identity || !m_identity->isValid())
        return false;
    return QFileInfo::exists(m_workTree + "/.git");
}

QString PullStore::pullsDir() const { return m_workTree + "/pulls"; }
QString PullStore::pullDir(int number) const
{
    return pullsDir() + "/" + QString::number(number);
}

void PullStore::computeStats(PullRequest &pr)
{
    int files = 0, add = 0, del = 0;
    for (const QString &line : pr.patch.split('\n')) {
        if (line.startsWith("diff --git "))
            ++files;
        else if (line.startsWith("+++") || line.startsWith("---"))
            continue;
        else if (line.startsWith('+'))
            ++add;
        else if (line.startsWith('-'))
            ++del;
    }
    pr.filesChanged = files;
    pr.additions = add;
    pr.deletions = del;
}

QByteArray PullStore::canonicalString(const PullRequest &pr)
{
    const QChar nul(QChar::Null);
    const QString content = pr.title + nul + pr.base + nul + pr.head + nul + pr.patch;
    const QByteArray contentHash =
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex();
    QByteArray canonical = "forkmesh-pull-event-v1\n";
    canonical += pr.author.toUtf8() + "\n";
    canonical += QByteArray::number(pr.ts) + "\n";
    canonical += contentHash;
    return canonical;
}

PullRequest PullStore::makeSignedPull(PullRequest pr) const
{
    pr.author = m_identity ? m_identity->publicKey() : QString();
    if (pr.authorName.isEmpty())
        pr.authorName = m_authorName;
    if (pr.ts == 0)
        pr.ts = QDateTime::currentMSecsSinceEpoch();
    pr.sig = m_identity ? m_identity->signData(canonicalString(pr)) : QString();
    return pr;
}

// ---- Conversation events (comments + reviews) ------------------------------

QString PullStore::contentForSigning(const PullEvent &ev)
{
    const QChar nul(QChar::Null);
    if (ev.type == "comment")
        return ev.body;
    if (ev.type == "review")
        return ev.state + nul + ev.body;
    if (ev.type == "line-comment")
        return ev.path + nul + ev.side + nul + QString::number(ev.line) + nul + ev.body;
    return QString();
}

QByteArray PullStore::canonicalString(int number, const PullEvent &ev)
{
    const QByteArray contentHash =
        QCryptographicHash::hash(contentForSigning(ev).toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex();
    QByteArray canonical = "forkmesh-pull-comment-v1\n";
    canonical += ev.type.toUtf8() + "\n";
    canonical += QByteArray::number(number) + "\n";
    canonical += ev.author.toUtf8() + "\n";
    canonical += QByteArray::number(ev.ts) + "\n";
    canonical += contentHash;
    return canonical;
}

PullEvent PullStore::makeSignedEvent(int number, PullEvent ev) const
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

QList<PullEvent> PullStore::readEvents(int number) const
{
    QList<PullEvent> events;
    QStringList names = QDir(pullDir(number)).entryList(QDir::Files, QDir::Name);
    names.sort();
    for (const QString &name : names) {
        if (!eventFileRe().match(name).hasMatch())
            continue;
        QFile ef(pullDir(number) + "/" + name);
        if (ef.open(QIODevice::ReadOnly))
            events.append(eventFromFrontMatter(parseFrontMatter(ef.readAll())));
    }
    return events;
}

int PullStore::nextEventIndex(int number) const
{
    int max = 1; // pull.md is conceptually event 1
    for (const QString &name : QDir(pullDir(number)).entryList(QDir::Files)) {
        if (!eventFileRe().match(name).hasMatch())
            continue;
        const int n = name.left(4).toInt();
        if (n > max)
            max = n;
    }
    return max + 1;
}

bool PullStore::writeEventFile(int number, int index, const PullEvent &ev,
                               QString *error) const
{
    QStringList lines;
    lines << "---";
    lines << "type: " + ev.type;
    lines << "id: " + ev.id;
    lines << "author: " + ev.author;
    lines << "authorName: " + ev.authorName;
    lines << "ts: " + QString::number(ev.ts);
    if (ev.type == "review")
        lines << "state: " + ev.state;
    if (ev.type == "line-comment") {
        lines << "path: " + ev.path;
        lines << "side: " + ev.side;
        lines << "line: " + QString::number(ev.line);
    }
    lines << "sig: " + ev.sig;
    lines << "---";
    lines << "";
    const QString name =
        QStringLiteral("%1-%2.md").arg(index, 4, 10, QChar('0')).arg(ev.type);
    return writeTextFile(pullDir(number) + "/" + name,
                         lines.join('\n') + "\n" + ev.body + "\n", error);
}

bool PullStore::appendEvent(int number, const PullEvent &ev,
                            const QString &commitMsg, QString *error)
{
    if (!QFileInfo::exists(pullDir(number) + "/pull.md")) {
        if (error)
            *error = QStringLiteral("Pull request #%1 not found.").arg(number);
        return false;
    }
    if (!writeEventFile(number, nextEventIndex(number), ev, error))
        return false;
    return commit(commitMsg, error);
}

bool PullStore::addComment(int number, const QString &body, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    PullEvent ev;
    ev.type = "comment";
    ev.body = stripEdgeNewlines(body);
    ev = makeSignedEvent(number, ev);
    return appendEvent(number, ev,
                       QStringLiteral("pull #%1: comment").arg(number), error);
}

bool PullStore::addReview(int number, const QString &state, const QString &body,
                          QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    PullEvent ev;
    ev.type = "review";
    ev.state = state;
    ev.body = stripEdgeNewlines(body);
    ev = makeSignedEvent(number, ev);
    return appendEvent(number, ev,
                       QStringLiteral("pull #%1: review (%2)").arg(number).arg(state),
                       error);
}

bool PullStore::addLineComment(int number, const QString &path, const QString &side,
                               int line, const QString &body, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    PullEvent ev;
    ev.type = "line-comment";
    ev.path = path;
    ev.side = side;
    ev.line = line;
    ev.body = stripEdgeNewlines(body);
    ev = makeSignedEvent(number, ev);
    return appendEvent(
        number, ev,
        QStringLiteral("pull #%1: comment on %2:%3").arg(number).arg(path).arg(line),
        error);
}

bool PullStore::applyRemoteEvent(int number, const PullEvent &ev, QString *error)
{
    if (!canWrite())
        return false;
    return appendEvent(
        number, ev,
        QStringLiteral("pull #%1: %2 (from %3)")
            .arg(number)
            .arg(ev.type == "review" ? QStringLiteral("review") : QStringLiteral("comment"),
                 ev.authorName.isEmpty() ? ev.author.left(8) : ev.authorName),
        error);
}

int PullStore::nextNumber() const
{
    int max = 0;
    for (const QString &entry :
         QDir(pullsDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numeric = false;
        const int n = entry.toInt(&numeric);
        if (numeric && n > max)
            max = n;
    }
    return max + 1;
}

bool PullStore::writePull(const PullRequest &pr, QString *error) const
{
    QDir().mkpath(pullDir(pr.number));
    QStringList lines;
    lines << "---";
    lines << "schema: forkmesh-pull-v1";
    lines << "number: " + QString::number(pr.number);
    lines << "title: " + pr.title;
    lines << "base: " + pr.base;
    lines << "head: " + pr.head;
    lines << "status: " + pr.status;
    lines << "ts: " + QString::number(pr.ts);
    lines << "author: " + pr.author;
    lines << "authorName: " + pr.authorName;
    lines << "sig: " + pr.sig;
    lines << "---";
    lines << "";
    if (!writeTextFile(pullDir(pr.number) + "/pull.md",
                       lines.join('\n') + "\n" + pr.description + "\n", error))
        return false;
    return writeTextFile(pullDir(pr.number) + "/changes.patch", pr.patch, error);
}

bool PullStore::readPull(int number, PullRequest &out) const
{
    QFile file(pullDir(number) + "/pull.md");
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const FrontMatter fm = parseFrontMatter(file.readAll());
    out.number = number;
    out.title = fm.get("title");
    out.base = fm.get("base");
    out.head = fm.get("head");
    out.status = fm.values.contains("status") ? fm.get("status")
                                              : QStringLiteral("open");
    out.ts = fm.num("ts");
    out.author = fm.get("author");
    out.authorName = fm.get("authorName");
    out.sig = fm.get("sig");
    out.description = fm.body;
    QFile patch(pullDir(number) + "/changes.patch");
    if (patch.open(QIODevice::ReadOnly))
        out.patch = QString::fromUtf8(patch.readAll());
    computeStats(out);
    out.events = readEvents(number);
    return true;
}

QList<PullRequest> PullStore::loadAll(QString *error) const
{
    if (!canWrite())
        return loadFromMirror(error);
    QList<PullRequest> pulls;
    for (const QString &entry :
         QDir(pullsDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        bool numeric = false;
        const int number = entry.toInt(&numeric);
        if (!numeric)
            continue;
        PullRequest pr;
        if (readPull(number, pr))
            pulls.append(pr);
    }
    std::sort(pulls.begin(), pulls.end(),
              [](const PullRequest &a, const PullRequest &b) { return a.number < b.number; });
    return pulls;
}

int PullStore::createPull(const QString &title, const QString &description,
                          const QString &base, const QString &head,
                          const QString &patch, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return -1;
    }
    // Don't open a second PR for a head branch that already has an open one.
    // Agents derive a unique branch per session, so a match means this exact
    // change was already submitted — return the existing number instead.
    if (!head.isEmpty()) {
        for (const PullRequest &existing : loadAll()) {
            if (existing.status == QLatin1String("open") && existing.head == head)
                return existing.number;
        }
    }
    PullRequest pr;
    pr.title = title;
    pr.description = description;
    pr.base = base;
    pr.head = head;
    pr.patch = patch;
    pr = makeSignedPull(pr);
    pr.number = nextNumber();
    if (!writePull(pr, error))
        return -1;
    if (!commit(QStringLiteral("pull #%1: open").arg(pr.number), error))
        return -1;
    return pr.number;
}

bool PullStore::setStatus(int number, const QString &status, QString *error)
{
    if (!canWrite())
        return false;
    PullRequest pr;
    if (!readPull(number, pr)) {
        if (error)
            *error = QStringLiteral("Pull request #%1 not found.").arg(number);
        return false;
    }
    pr.status = status;
    if (!writePull(pr, error))
        return false;
    return commit(QStringLiteral("pull #%1: %2").arg(number).arg(status), error);
}

bool PullStore::isBranchBehindBase(int number, bool *behind, QString *error) const
{
    if (behind)
        *behind = false;
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    PullRequest pr;
    if (!readPull(number, pr)) {
        if (error)
            *error = QStringLiteral("Pull request #%1 not found.").arg(number);
        return false;
    }
    if (pr.base.isEmpty() || pr.head.isEmpty()) {
        if (error)
            *error = QStringLiteral("This pull request does not name a base and head branch.");
        return false;
    }
    QByteArray output;
    QString err;
    if (!runGit(m_workTree, {"rev-list", "--count", pr.head + ".." + pr.base},
                &output, &err)) {
        if (error)
            *error = QStringLiteral("Could not compare branches: %1").arg(err);
        return false;
    }
    if (behind)
        *behind = QString::fromUtf8(output).trimmed().toInt() > 0;
    return true;
}

bool PullStore::updateBranchFromBase(int number, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("Updating needs a local working tree.");
        return false;
    }
    PullRequest pr;
    if (!readPull(number, pr)) {
        if (error)
            *error = QStringLiteral("Pull request #%1 not found.").arg(number);
        return false;
    }
    if (pr.status != "open") {
        if (error)
            *error = QStringLiteral("This pull request is already %1.").arg(pr.status);
        return false;
    }

    bool behind = false;
    if (!isBranchBehindBase(number, &behind, error))
        return false;
    if (!behind)
        return true;

    QString err;
    QByteArray status;
    if (!runGit(m_workTree, {"status", "--porcelain"}, &status, &err) ||
        !status.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Commit or stash local changes before updating the branch.");
        return false;
    }

    QByteArray originalBranch;
    QByteArray originalCommit;
    runGit(m_workTree, {"rev-parse", "--abbrev-ref", "HEAD"}, &originalBranch, nullptr);
    runGit(m_workTree, {"rev-parse", "--verify", "HEAD"}, &originalCommit, nullptr);
    const QString current = QString::fromUtf8(originalBranch).trimmed();
    const QString restoreRef =
        current.isEmpty() || current == QLatin1String("HEAD")
            ? QString::fromUtf8(originalCommit).trimmed()
            : current;

    if (!runGit(m_workTree, {"checkout", pr.head}, nullptr, &err)) {
        if (error)
            *error = QStringLiteral("Could not check out %1: %2").arg(pr.head, err);
        return false;
    }
    if (!runGit(m_workTree, {"merge", "--no-edit", pr.base}, nullptr, &err)) {
        runGit(m_workTree, {"merge", "--abort"}, nullptr, nullptr);
        if (!restoreRef.isEmpty() && restoreRef != pr.head)
            runGit(m_workTree, {"checkout", restoreRef}, nullptr, nullptr);
        if (error)
            *error = QStringLiteral("Could not merge %1 into %2: %3")
                         .arg(pr.base, pr.head, err);
        return false;
    }
    if (!restoreRef.isEmpty() && restoreRef != pr.head &&
        !runGit(m_workTree, {"checkout", restoreRef}, nullptr, &err)) {
        if (error)
            *error = QStringLiteral("Updated %1, but could not return to %2: %3")
                         .arg(pr.head, restoreRef, err);
        return false;
    }

    QByteArray diff;
    if (!runGit(m_workTree, {"diff", pr.base + ".." + pr.head}, &diff, &err)) {
        if (error)
            *error = QStringLiteral("Could not refresh the pull request patch: %1").arg(err);
        return false;
    }
    pr.patch = QString::fromUtf8(diff);
    computeStats(pr);
    if (!writePull(pr, error))
        return false;
    return commit(QStringLiteral("pull #%1: update branch from %2")
                      .arg(number)
                      .arg(pr.base),
                  error);
}

bool PullStore::mergePull(int number, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("Merging needs a local working tree.");
        return false;
    }
    PullRequest pr;
    if (!readPull(number, pr)) {
        if (error)
            *error = QStringLiteral("Pull request #%1 not found.").arg(number);
        return false;
    }
    if (pr.status != "open") {
        if (error)
            *error = QStringLiteral("This pull request is already %1.").arg(pr.status);
        return false;
    }
    const QString patchPath = pullDir(number) + "/changes.patch";
    QString err;
    if (!runGit(m_workTree, {"apply", "--index", "--3way", patchPath}, nullptr, &err)) {
        if (error)
            *error = "Could not apply the patch cleanly: " + err;
        return false;
    }
    pr.status = "merged";
    if (!writePull(pr, error))
        return false;
    // Commit both the applied changes and the status update.
    if (!runGit(m_workTree, {"add", "-A"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    if (!runGit(m_workTree,
                {"commit", "-m", QStringLiteral("merge pull #%1: %2")
                                     .arg(number)
                                     .arg(pr.title)},
                nullptr, &err)) {
        if (err.contains("nothing to commit"))
            return true;
        if (error)
            *error = "git commit failed: " + err;
        return false;
    }
    return true;
}

bool PullStore::applyRemotePull(const PullRequest &incoming, QString *error)
{
    if (!canWrite())
        return false;
    PullRequest pr = incoming;
    pr.number = nextNumber();
    pr.status = "open";
    if (!writePull(pr, error))
        return false;
    return commit(QStringLiteral("pull #%1: opened (from %2)")
                      .arg(pr.number)
                      .arg(pr.authorName.isEmpty() ? pr.author.left(8) : pr.authorName),
                  error);
}


bool PullStore::deletePull(int number, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    const QString dir = pullDir(number);
    if (!QFileInfo::exists(dir)) {
        if (error)
            *error = QStringLiteral("Pull request #%1 not found.").arg(number);
        return false;
    }
    const QString relPath = QStringLiteral("pulls/%1").arg(number);
    QString err;
    runGit(m_workTree, {"rm", "-r", "--ignore-unmatch", "--", relPath}, nullptr, nullptr);
    if (QDir(dir).exists() && !QDir(dir).removeRecursively()) {
        if (error)
            *error = QStringLiteral("Could not remove pull request folder.");
        return false;
    }
    if (!runGit(m_workTree, {"commit", "-m",
                             QStringLiteral("pull #%1: deleted").arg(number),
                             "--", relPath},
                nullptr, &err)) {
        if (!err.contains(QStringLiteral("nothing to commit")) && !err.isEmpty()) {
            if (error)
                *error = QStringLiteral("git commit failed: ") + err;
            return false;
        }
    }
    return true;
}
bool PullStore::commit(const QString &message, QString *error) const
{
    QString err;
    if (!runGit(m_workTree, {"add", "pulls"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    if (!runGit(m_workTree, {"commit", "-m", message, "--", "pulls"}, nullptr, &err)) {
        if (err.contains("nothing to commit") || err.isEmpty())
            return true;
        if (error)
            *error = "git commit failed: " + err;
        return false;
    }
    return true;
}

// ---- Read-only access from a bare mirror -----------------------------------

QString PullStore::mirrorRef() const
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

QByteArray PullStore::showFromMirror(const QString &repoRelPath, bool *ok) const
{
    const QString ref = mirrorRef();
    QByteArray output;
    const bool good = !ref.isEmpty() &&
                      runGit(m_mirror, {"show", ref + ":" + repoRelPath}, &output);
    if (ok)
        *ok = good;
    return good ? output : QByteArray();
}

QList<PullRequest> PullStore::loadFromMirror(QString *error) const
{
    QList<PullRequest> pulls;
    if (m_mirror.isEmpty())
        return pulls;
    const QString ref = mirrorRef();
    if (ref.isEmpty())
        return pulls;
    QByteArray listing;
    if (!runGit(m_mirror, {"ls-tree", ref, "pulls/"}, &listing))
        return pulls;
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
        const QByteArray md = showFromMirror("pulls/" + base + "/pull.md", &ok);
        if (!ok)
            continue;
        const FrontMatter fm = parseFrontMatter(md);
        PullRequest pr;
        pr.number = number;
        pr.title = fm.get("title");
        pr.base = fm.get("base");
        pr.head = fm.get("head");
        pr.status = fm.values.contains("status") ? fm.get("status")
                                                  : QStringLiteral("open");
        pr.ts = fm.num("ts");
        pr.author = fm.get("author");
        pr.authorName = fm.get("authorName");
        pr.sig = fm.get("sig");
        pr.description = fm.body;
        bool pok = false;
        pr.patch = QString::fromUtf8(showFromMirror("pulls/" + base + "/changes.patch", &pok));
        computeStats(pr);
        // Enumerate this PR's conversation event files from the mirror.
        QByteArray dirListing;
        if (runGit(m_mirror, {"ls-tree", ref, "pulls/" + base + "/"}, &dirListing)) {
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
                const QByteArray evBytes =
                    showFromMirror("pulls/" + base + "/" + name, &eok);
                if (eok)
                    pr.events.append(eventFromFrontMatter(parseFrontMatter(evBytes)));
            }
        }
        pulls.append(pr);
    }
    std::sort(pulls.begin(), pulls.end(),
              [](const PullRequest &a, const PullRequest &b) { return a.number < b.number; });
    Q_UNUSED(error);
    return pulls;
}
