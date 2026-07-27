#include "PullStore.h"

#include "ForkMeshIdentity.h"
#include "StrictGitReader.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {

constexpr int kGitTimeoutMs = 15000;
constexpr qint64 kMaxJsonSafeInteger = 9007199254740991LL;
// History rewrites (filter-branch) replay every commit, so they need far longer
// than an ordinary git invocation.
constexpr int kGitRewriteTimeoutMs = 120000;

bool strictJsonSafeInteger(const QString &text, qint64 minimum,
                           qint64 *value = nullptr)
{
    bool ok = false;
    const qint64 parsed = text.toLongLong(&ok);
    if (!ok || parsed < minimum || parsed > kMaxJsonSafeInteger)
        return false;
    if (value)
        *value = parsed;
    return true;
}

// Wait for `process` to finish. When `keepGuiAlive` is set the caller is on the
// GUI thread and the command (a `git apply --check` dry-run) can take a second
// or more, so poll in short slices and pump posted events between them — the
// same approach as MainWindow's GitKeepAlive — to keep the window painted
// instead of freezing the event loop. Returns false on timeout, after killing
// the process. User input is excluded so a pump can't re-enter via clicks.
bool waitForFinishedKeepAlive(QProcess &process, int timeoutMs, bool keepGuiAlive)
{
    if (!keepGuiAlive)
        return process.waitForFinished(timeoutMs);
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(40)) {
        if (process.state() == QProcess::NotRunning)
            return true; // exited between polls; caller inspects the exit code
        if (timer.hasExpired(timeoutMs)) {
            process.kill();
            process.waitForFinished(200);
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 12);
    }
    return true;
}

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
    // On the GUI thread pump between polls: applying a pulls-inbox payload runs
    // these commands (materializePullRef's worktree add / am replay in
    // particular) synchronously on the main thread, and a plain blocking wait
    // froze the window for 1s+ per PR (stall log: PullStore::materializePullRef,
    // PullStore::readPull).
    const QCoreApplication *app = QCoreApplication::instance();
    const bool onGuiThread = app && QThread::currentThread() == app->thread();
    if (!waitForFinishedKeepAlive(process, timeoutMs, onGuiThread)) {
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

// Reconstruct a branch-backed PR's diff and commit series from the synced
// base/head refs instead of from committed patch blobs. `dir` may be a working
// tree or a bare mirror — both carry refs/heads/* after a sync. The diff uses
// merge-base (three-dot) so it stays the PR's net change as the base advances;
// the commit series uses two-dot so it is exactly the commits unique to head.
// Returns false (leaving the outputs untouched) when either ref is missing or
// `head` is a cross-node "<node>:<branch>" label that does not resolve locally.
bool deriveFromRefs(const QString &dir, const QString &base, const QString &head,
                    QString *patch, QString *commits)
{
    if (dir.isEmpty() || base.isEmpty() || head.isEmpty() ||
        head.contains(QLatin1Char(':')))
        return false;
    if (!runGit(dir, {"rev-parse", "--verify", "--quiet", base + "^{commit}"}) ||
        !runGit(dir, {"rev-parse", "--verify", "--quiet", head + "^{commit}"}))
        return false;
    if (patch) {
        QByteArray diff;
        // --binary embeds the literal/delta for binary files; without it the diff
        // is only "Binary files differ", which `git apply` (the flat-patch merge
        // and checkMergeable dry-run) cannot replay — so binary changes would
        // silently fail to merge or be misreported as conflicts.
        runGit(dir, {"diff", "--binary", base + "..." + head}, &diff);
        *patch = QString::fromUtf8(diff);
    }
    if (commits) {
        QByteArray mbox;
        runGit(dir, {"format-patch", "--binary", "--stdout", base + ".." + head},
               &mbox);
        *commits = QString::fromUtf8(mbox);
    }
    return true;
}

bool validCommitOid(const QString &oid)
{
    if (oid.size() != 40 && oid.size() != 64)
        return false;
    for (const QChar ch : oid) {
        const ushort code = ch.unicode();
        if (!((code >= '0' && code <= '9') ||
              (code >= 'a' && code <= 'f') ||
              (code >= 'A' && code <= 'F'))) {
            return false;
        }
    }
    return true;
}

QString resolvedCommitOid(const QString &dir, const QString &ref)
{
    if (dir.isEmpty() || ref.isEmpty())
        return {};
    QByteArray output;
    if (!runGit(dir,
                {"rev-parse", "--verify", "--quiet", "--end-of-options",
                 ref + "^{commit}"},
                &output)) {
        return {};
    }
    const QString oid = QString::fromUtf8(output).trimmed().toLower();
    return validCommitOid(oid) ? oid : QString();
}

bool deriveFromImmutableOids(const QString &dir, const QString &baseOid,
                             const QString &headOid, QString *patch,
                             QString *commits,
                             StrictGitReadInternal::Reader *strictReader = nullptr)
{
    if (dir.isEmpty() || !validCommitOid(baseOid) ||
        !validCommitOid(headOid)) {
        return false;
    }

    auto readGit = [&](const QStringList &args, QByteArray *output) {
        return strictReader ? strictReader->run(dir, args, output)
                            : runGit(dir, args, output);
    };
    QByteArray baseOutput;
    QByteArray headOutput;
    if (!readGit({"rev-parse", "--verify", "--quiet", "--end-of-options",
                  baseOid + "^{commit}"},
                 &baseOutput) ||
        !readGit({"rev-parse", "--verify", "--quiet", "--end-of-options",
                  headOid + "^{commit}"},
                 &headOutput)) {
        return false;
    }
    const QString resolvedBase = QString::fromUtf8(baseOutput).trimmed();
    const QString resolvedHead = QString::fromUtf8(headOutput).trimmed();
    if (resolvedBase.compare(baseOid, Qt::CaseInsensitive) != 0 ||
        resolvedHead.compare(headOid, Qt::CaseInsensitive) != 0) {
        return false;
    }

    QByteArray diff;
    QByteArray mbox;
    if (!readGit({"diff", "--no-ext-diff", "--no-textconv", "--binary",
                  baseOid + "..." + headOid},
                 &diff) ||
        !readGit({"format-patch", "--binary", "--stdout",
                  baseOid + ".." + headOid},
                 &mbox)) {
        return false;
    }
    if (patch)
        *patch = QString::fromUtf8(diff);
    if (commits)
        *commits = QString::fromUtf8(mbox);
    return true;
}

// Make a branch-backed PR's head commit reachable in the working tree's object
// store and return its SHA (empty when unavailable). Prefers the local ref when
// it resolves; otherwise fetches just that branch's objects from the mirror
// (which keeps refs/heads/* after a sync) so a real ref-merge can run with no
// patch involved. A cross-node "<node>:<branch>" head never resolves locally and
// returns empty — those PRs are stored-patch and merge from their carried diff.
QString reachableHeadCommit(const QString &workTree, const QString &mirror,
                            const QString &head)
{
    if (workTree.isEmpty() || head.isEmpty() || head.contains(QLatin1Char(':')))
        return QString();
    QByteArray sha;
    if (runGit(workTree, {"rev-parse", "--verify", "--quiet", head + "^{commit}"},
               &sha) &&
        !sha.trimmed().isEmpty())
        return QString::fromUtf8(sha).trimmed();
    if (!mirror.isEmpty() &&
        runGit(workTree, {"fetch", "--no-tags", "--quiet", mirror,
                          "refs/heads/" + head})) {
        if (runGit(workTree,
                   {"rev-parse", "--verify", "--quiet", "FETCH_HEAD^{commit}"},
                   &sha) &&
            !sha.trimmed().isEmpty())
            return QString::fromUtf8(sha).trimmed();
    }
    return QString();
}

// Drop any other worktree currently holding `branch` checked out so the main
// worktree can check it out. An agent session runs in a temp worktree at
// /tmp/forkmesh-worktrees/…; if one is left behind it keeps the branch reserved
// and `git checkout <branch>` here fails with "is already used by worktree at …".
// Removing the worktree frees the branch while keeping its ref intact. Returns
// true if it released something (so the caller should retry the checkout).
bool releaseWorktreeHoldingBranch(const QString &dir, const QString &branch)
{
    if (branch.trimmed().isEmpty())
        return false;
    QByteArray out;
    if (!runGit(dir, {"worktree", "list", "--porcelain"}, &out, nullptr))
        return false;
    const QString want = QStringLiteral("refs/heads/%1").arg(branch);
    QString currentPath;
    QString held;
    const QList<QByteArray> lines = out.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1String("worktree ")))
            currentPath = line.mid(QStringLiteral("worktree ").size()).trimmed();
        else if (line.startsWith(QLatin1String("branch ")) &&
                 line.mid(QStringLiteral("branch ").size()).trimmed() == want &&
                 !currentPath.isEmpty() &&
                 QDir(currentPath).absolutePath() != QDir(dir).absolutePath()) {
            held = currentPath;
            break;
        }
    }
    if (held.isEmpty())
        return false;
    runGit(dir, {"worktree", "remove", "--force", held}, nullptr, nullptr);
    QDir(held).removeRecursively();
    runGit(dir, {"worktree", "prune"}, nullptr, nullptr);
    return true;
}

// Pull the conflicting file paths out of `git apply --check --3way` output,
// which names the offending file in a few shapes depending on whether the
// 3-way fallback ran:
//   "error: patch failed: <path>:<line>"
//   "error: <path>: patch does not apply"
//   "error: <path>: does not exist in index" / "does not match index"
//   "Applied patch to '<path>' with conflicts."
QStringList parseApplyConflicts(const QString &text)
{
    static const QRegularExpression patterns[] = {
        QRegularExpression(QStringLiteral("^error: patch failed: (.+):\\d+$")),
        QRegularExpression(QStringLiteral(
            "^error: (.+): (?:patch does not apply|does not (?:exist|match) in index)$")),
        QRegularExpression(QStringLiteral("^Applied patch to '(.+)' with conflicts\\.$")),
    };
    QStringList files;
    const QStringList lines = text.split('\n');
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        for (const QRegularExpression &re : patterns) {
            const QRegularExpressionMatch m = re.match(trimmed);
            if (m.hasMatch()) {
                if (!files.contains(m.captured(1)))
                    files << m.captured(1);
                break;
            }
        }
    }
    return files;
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
    bool valid = false;
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
    if (i >= lines.size())
        return fm;
    QString body = lines.mid(i + 1).join('\n');
    while (body.startsWith('\n'))
        body.remove(0, 1);
    while (body.endsWith('\n'))
        body.chop(1);
    fm.body = body;
    fm.valid = true;
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

QString encodeFrontMatterBlob(const QString &s)
{
    if (s.isEmpty())
        return QString();
    return QString::fromLatin1(
        s.toUtf8().toBase64(QByteArray::Base64UrlEncoding |
                            QByteArray::OmitTrailingEquals));
}

QString decodeFrontMatterBlob(const QString &s)
{
    if (s.isEmpty())
        return QString();
    return QString::fromUtf8(QByteArray::fromBase64(
        s.toLatin1(), QByteArray::Base64UrlEncoding |
                         QByteArray::OmitTrailingEquals));
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
    ev.threadId = fm.get("threadId");
    ev.parentId = fm.get("parentId");
    ev.lineStart = int(fm.num("lineStart"));
    ev.lineEnd = int(fm.num("lineEnd"));
    ev.suggestionPatch = decodeFrontMatterBlob(fm.get("suggestionPatchB64"));
    ev.targetPath = fm.get("targetPath");
    ev.appliedCommit = fm.get("appliedCommit");
    ev.sig = fm.get("sig");
    ev.body = fm.body;
    return ev;
}

// filter-branch leaves the pre-rewrite tips under refs/original/*; drop them so
// the old (un-purged) history is actually unreachable and can be gc'd.
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

// A history rewrite requires a clean tree. Refuse if anything tracked *other*
// than the pull being deleted is staged/modified, so we never replay or discard
// a collaborator's in-flight work (agents share this working tree).
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
                *error = QStringLiteral("Commit or stash unrelated tracked changes "
                                        "before deleting a pull request.");
            }
            return true;
        }
    }
    return false;
}

// The file a "diff --git a/<path> b/<path>" header names, taken from the b-side
// exactly the way the Files-changed list does (so the path we match against is
// the same one the UI hands back when a row is selected).
QString diffHeaderPath(const QString &diffLine)
{
    return diffLine.section(QStringLiteral(" b/"), 1);
}

// True when any line in a diff body opens a file section.
bool bodyHasFileDiff(const QString &body)
{
    const QStringList lines = body.split('\n');
    for (const QString &line : lines)
        if (line.startsWith(QLatin1String("diff --git ")))
            return true;
    return false;
}

// Drop the unified-diff section(s) for `relPath` from a single diff body (one
// with no email framing — a raw `git diff`, or an mbox message with its trailing
// signature already split off). A section runs from its "diff --git" header up to
// the next header or the end. Sets *removed when at least one section matched.
QString removeFileSectionsFromDiff(const QString &body, const QString &relPath,
                                   bool *removed)
{
    const QStringList lines = body.split('\n');
    QStringList kept;
    kept.reserve(lines.size());
    bool skipping = false;
    bool any = false;
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("diff --git "))) {
            skipping = (diffHeaderPath(line) == relPath);
            if (skipping)
                any = true;
        }
        if (!skipping)
            kept << line;
    }
    if (removed)
        *removed = any;
    return kept.join('\n');
}

// Split a format-patch message at its "-- \n<version>" signature, which is always
// the final such marker (a removed line whose content is "- " would render as
// "-- " inside a hunk, so we match from the end to land on the real one). Returns
// the body before it and the trailer from it onward, so reassembling as
// body + "\n" + trailer reproduces the message verbatim.
void splitMboxSignature(const QString &message, QString *body, QString *trailer)
{
    const QStringList lines = message.split('\n');
    int sig = -1;
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (lines.at(i) == QLatin1String("-- ")) {
            sig = i;
            break;
        }
    }
    if (sig < 0) {
        *body = message;
        trailer->clear();
        return;
    }
    *body = QStringList(lines.mid(0, sig)).join('\n');
    *trailer = QStringList(lines.mid(sig)).join('\n');
}

// Drop the diffstat git format-patch wrote between the "---" cut and the diff.
// Once a file is excised the stat is stale (and would still name the removed
// file), git am ignores it, and the applied commit never includes it anyway — so
// for a message we have already edited we simply remove it. Messages we don't
// touch are left byte-for-byte intact by the caller.
QString stripDiffstat(const QString &message)
{
    const QStringList lines = message.split('\n');
    int firstDiff = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).startsWith(QLatin1String("diff --git "))) {
            firstDiff = i;
            break;
        }
    }
    if (firstDiff < 0)
        return message;
    int cut = -1;
    for (int i = firstDiff - 1; i >= 0; --i) {
        if (lines.at(i) == QLatin1String("---")) {
            cut = i;
            break;
        }
    }
    if (cut < 0)
        return message; // no format-patch cut; nothing recognizable to drop
    QStringList kept = lines.mid(0, cut + 1);
    // Skip the diffstat: its lines (and the trailing blank) are space-prefixed or
    // empty. Stop at the first line that is neither — i.e. the diff itself.
    int i = cut + 1;
    while (i < firstDiff && (lines.at(i).isEmpty() || lines.at(i).startsWith(' ')))
        ++i;
    kept << QString(); // one blank line between the message and the diff
    for (; i < lines.size(); ++i)
        kept << lines.at(i);
    return kept.join('\n');
}

// Excise the diff for `relPath` from every commit in a format-patch mbox. A
// commit left with no remaining file diffs (it only touched that file) is dropped
// whole so `git am` is never handed an empty patch. Survivors keep their headers
// and signatures untouched. Returns an empty string when nothing is left.
QString removeFileFromMbox(const QString &mbox, const QString &relPath)
{
    // Each commit message begins at a "From <sha> Mon Sep 17 00:00:00 2001" line.
    // Diff content never starts a line that way, so it is a safe message delimiter.
    static const QRegularExpression boundary(
        QStringLiteral("^From [0-9a-f]+ Mon Sep 17 00:00:00 2001$"));
    const QStringList lines = mbox.split('\n');
    QList<QStringList> messages;
    for (const QString &line : lines) {
        if (messages.isEmpty() || boundary.match(line).hasMatch())
            messages.append(QStringList{});
        messages.last() << line;
    }

    QStringList out;
    for (const QStringList &msgLines : messages) {
        const QString message = msgLines.join('\n');
        // Anything before the first real header (none, in practice) is passed
        // through so we never silently corrupt an unexpected shape.
        if (msgLines.isEmpty() || !boundary.match(msgLines.first()).hasMatch()) {
            out << message;
            continue;
        }
        QString body, trailer;
        splitMboxSignature(message, &body, &trailer);
        bool removed = false;
        const QString newBody = removeFileSectionsFromDiff(body, relPath, &removed);
        if (!removed) {
            out << message; // this commit doesn't touch the file
            continue;
        }
        if (!bodyHasFileDiff(newBody))
            continue; // the commit only touched the file — drop it entirely
        const QString trimmedBody = stripDiffstat(newBody);
        out << (trailer.isEmpty() ? trimmedBody : trimmedBody + "\n" + trailer);
    }

    const QString result = out.join('\n');
    return result.trimmed().isEmpty() ? QString() : result;
}

} // namespace

// ---- PullEvent -------------------------------------------------------------

QJsonObject PullEvent::toJson() const
{
    return {{"type", type},   {"id", id},     {"author", author},
            {"authorName", authorName}, {"ts", double(ts)}, {"body", body},
            {"state", state}, {"path", path}, {"side", side},
            {"line", line},   {"threadId", threadId},
            {"parentId", parentId}, {"lineStart", lineStart},
            {"lineEnd", lineEnd}, {"suggestionPatch", suggestionPatch},
            {"targetPath", targetPath}, {"appliedCommit", appliedCommit},
            {"sig", sig}};
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
    ev.threadId = obj.value("threadId").toString();
    ev.parentId = obj.value("parentId").toString();
    ev.lineStart = obj.value("lineStart").toInt();
    ev.lineEnd = obj.value("lineEnd").toInt();
    ev.suggestionPatch = obj.value("suggestionPatch").toString();
    ev.targetPath = obj.value("targetPath").toString();
    ev.appliedCommit = obj.value("appliedCommit").toString();
    ev.sig = obj.value("sig").toString();
    return ev;
}

// ---- PullRequest (JSON wire format) ----------------------------------------

QJsonObject PullRequest::toJson() const
{
    return {{"number", number},   {"title", title},   {"description", description},
            {"base", base},       {"head", head},     {"status", status},
            {"ts", double(ts)},   {"author", author}, {"authorName", authorName},
            {"sig", sig},         {"patch", patch},   {"commits", commits},
            {"creationBaseOid", creationBaseOid},
            {"creationHeadOid", creationHeadOid}};
}

PullRequest PullRequest::fromJson(const QJsonObject &obj)
{
    PullRequest pr;
    pr.number = obj.value("number").toInt();
    pr.title = obj.value("title").toString();
    pr.description = obj.contains("description")
                         ? obj.value("description").toString()
                         : obj.value("body").toString();
    pr.base = obj.value("base").toString();
    pr.head = obj.value("head").toString();
    pr.status = obj.value("status").toString("open");
    pr.ts = obj.value("ts").toVariant().toLongLong();
    pr.author = obj.value("author").toString();
    pr.authorName = obj.value("authorName").toString();
    pr.sig = obj.value("sig").toString();
    pr.patch = obj.value("patch").toString();
    pr.commits = obj.value("commits").toString();
    pr.creationBaseOid = obj.value("creationBaseOid").toString();
    pr.creationHeadOid = obj.value("creationHeadOid").toString();
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

QString PullStore::pullsDir() const
{
    const QString meta = metaWorkTree();
    return (meta.isEmpty() ? m_workTree : meta) + "/pulls";
}
QString PullStore::pullDir(int number) const
{
    return pullsDir() + "/" + QString::number(number);
}

QString PullStore::metaWorkTree() const
{
    if (m_workTree.isEmpty())
        return QString();
    // One linked worktree per repo, keyed by a hash of its path, under app
    // data so it persists across restarts (unlike the /tmp agent-session
    // worktrees elsewhere in this file, this one holds the live PR metadata,
    // not scratch state).
    const QByteArray key = QCryptographicHash::hash(
                                QDir(m_workTree).absolutePath().toUtf8(),
                                QCryptographicHash::Sha256)
                                .toHex()
                                .left(16);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                        "/pull-meta/" + QString::fromUtf8(key);
    if (QFileInfo::exists(dir + "/.git"))
        return dir; // already linked from a prior run
    QDir().mkpath(QFileInfo(dir).absolutePath());
    const bool branchExists = runGit(
        m_workTree, {"rev-parse", "--verify", "-q", "refs/heads/forkmesh/pulls^{commit}"});
    auto link = [&] {
        return branchExists
                   ? runGit(m_workTree, {"worktree", "add", dir, "forkmesh/pulls"})
                   : runGit(m_workTree,
                           {"worktree", "add", "-b", "forkmesh/pulls", dir, "HEAD"});
    };
    if (!link()) {
        // A leftover/corrupt directory from a previous failed attempt - clear
        // it and try once more.
        runGit(m_workTree, {"worktree", "prune"}, nullptr, nullptr);
        QDir(dir).removeRecursively();
        if (!link())
            return QString();
    }
    QDir().mkpath(dir + "/pulls");
    return dir;
}

bool PullStore::materializePullRef(const PullRequest &pr, QString *error) const
{
    QString sha = reachableHeadCommit(m_workTree, m_mirror, pr.head);
    if (sha.isEmpty() && !pr.base.trimmed().isEmpty()) {
        QByteArray baseSha;
        if (runGit(m_workTree,
                   {"rev-parse", "--verify", "-q", pr.base.trimmed() + "^{commit}"},
                   &baseSha) &&
            !baseSha.trimmed().isEmpty()) {
            // Replay the PR's commits (or a synthesized single commit from the
            // flat patch, same as beginPullBranch) into a throwaway detached
            // worktree so the user's actual checkout is never disturbed.
            const QString scratchDir = QDir::temp().filePath(
                QStringLiteral("forkmesh-pr-ref-%1-%2")
                    .arg(pr.number)
                    .arg(QDateTime::currentMSecsSinceEpoch()));
            if (runGit(m_workTree, {"worktree", "add", "--detach", scratchDir,
                                    QString::fromUtf8(baseSha).trimmed()})) {
                const QString mboxPath = QDir::temp().filePath(
                    QStringLiteral("forkmesh-pr-ref-%1.mbox").arg(pr.number));
                if (writeTextFile(mboxPath,
                                  pr.commits.isEmpty() ? syntheticMbox(pr) : pr.commits,
                                  nullptr) &&
                    runGit(scratchDir, {"am", "--3way", mboxPath})) {
                    QByteArray tip;
                    if (runGit(scratchDir, {"rev-parse", "HEAD"}, &tip) &&
                        !tip.trimmed().isEmpty())
                        sha = QString::fromUtf8(tip).trimmed();
                }
                QFile::remove(mboxPath);
                runGit(scratchDir, {"am", "--abort"}, nullptr, nullptr);
                runGit(m_workTree, {"worktree", "remove", "--force", scratchDir},
                      nullptr, nullptr);
                QDir(scratchDir).removeRecursively();
                runGit(m_workTree, {"worktree", "prune"}, nullptr, nullptr);
            }
        }
    }
    if (sha.isEmpty())
        return true; // best-effort: readers fall back to the legacy path
    QString err;
    if (!runGit(m_workTree,
               {"update-ref", QStringLiteral("refs/pr/%1/head").arg(pr.number), sha},
               nullptr, &err)) {
        if (error)
            *error = "Could not record the pull request ref: " + err;
        return false;
    }
    return true;
}

void PullStore::computeStats(PullRequest &pr)
{
    int files = 0, add = 0, del = 0;
    // Walk the patch line-by-line over a view instead of pr.patch.split('\n'):
    // a large patch otherwise materialises a QStringList holding one heap-allocated
    // QString per line, and building + destroying that list blocked the UI thread
    // for ~1.6s (the QString destructor was the stall hot spot, issue #152).
    const QStringView patch(pr.patch);
    for (qsizetype start = 0; start <= patch.size();) {
        const qsizetype nl = patch.indexOf(u'\n', start);
        const QStringView line =
            patch.sliced(start, (nl < 0 ? patch.size() : nl) - start);
        if (line.startsWith(QLatin1String("diff --git ")))
            ++files;
        else if (line.startsWith(QLatin1String("+++ ")) ||
                 line.startsWith(QLatin1String("--- "))) {
            // file-header line, not a +/- content line
        } else if (line.startsWith(u'+'))
            ++add;
        else if (line.startsWith(u'-'))
            ++del;
        if (nl < 0)
            break;
        start = nl + 1;
    }
    pr.filesChanged = files;
    pr.additions = add;
    pr.deletions = del;
}

QByteArray PullStore::canonicalString(const PullRequest &pr)
{
    const QChar nul(QChar::Null);
    // The mbox is appended as a 5th NUL-separated field. This changes the hash
    // versus the original 4-field form even when commits is empty, so the worker
    // verifies against both forms during the client rollout.
    const QString content =
        pr.title + nul + pr.base + nul + pr.head + nul + pr.patch + nul + pr.commits;
    const QByteArray contentHash =
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex();
    QByteArray canonical = "forkmesh-pull-event-v1\n";
    canonical += pr.author.toUtf8() + "\n";
    canonical += QByteArray::number(pr.ts) + "\n";
    canonical += contentHash;
    return canonical;
}

QByteArray PullStore::legacyCanonicalString(const PullRequest &pr)
{
    const QChar nul(QChar::Null);
    const QString content =
        pr.title + nul + pr.base + nul + pr.head + nul + pr.patch;
    const QByteArray contentHash =
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256)
            .toHex();
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
    if (ev.type == "thread-comment")
        return ev.threadId + nul + ev.path + nul + ev.side + nul +
               QString::number(ev.lineStart) + nul + QString::number(ev.lineEnd) +
               nul + ev.body + nul + ev.suggestionPatch;
    if (ev.type == "thread-reply")
        return ev.threadId + nul + ev.parentId + nul + ev.body;
    if (ev.type == "thread-state")
        return ev.threadId + nul + ev.state + nul + ev.body;
    if (ev.type == "suggestion-state")
        return ev.threadId + nul + ev.state + nul + ev.appliedCommit + nul + ev.body;
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
    if (ev.type == QLatin1String("thread-comment") && ev.threadId.isEmpty())
        ev.threadId = ev.id;
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
    if (!ev.threadId.isEmpty())
        lines << "threadId: " + ev.threadId;
    if (!ev.parentId.isEmpty())
        lines << "parentId: " + ev.parentId;
    if (ev.type == "thread-comment") {
        lines << "path: " + ev.path;
        lines << "side: " + ev.side;
        lines << "lineStart: " + QString::number(ev.lineStart);
        lines << "lineEnd: " + QString::number(ev.lineEnd);
    }
    if (!ev.suggestionPatch.isEmpty())
        lines << "suggestionPatchB64: " + encodeFrontMatterBlob(ev.suggestionPatch);
    if (!ev.targetPath.isEmpty())
        lines << "targetPath: " + ev.targetPath;
    if (!ev.appliedCommit.isEmpty())
        lines << "appliedCommit: " + ev.appliedCommit;
    if ((ev.type == "thread-state" || ev.type == "suggestion-state") &&
        !ev.state.isEmpty())
        lines << "state: " + ev.state;
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

bool PullStore::addThreadComment(int number, const QString &path,
                                 const QString &side, int lineStart, int lineEnd,
                                 const QString &body,
                                 const QString &suggestionPatch, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    PullEvent ev;
    ev.type = "thread-comment";
    ev.path = path;
    ev.side = side;
    ev.lineStart = lineStart;
    ev.lineEnd = lineEnd > 0 ? lineEnd : lineStart;
    ev.body = stripEdgeNewlines(body);
    ev.suggestionPatch = stripEdgeNewlines(suggestionPatch);
    ev = makeSignedEvent(number, ev);
    return appendEvent(
        number, ev,
        QStringLiteral("pull #%1: thread on %2:%3")
            .arg(number)
            .arg(path)
            .arg(lineStart),
        error);
}

bool PullStore::addThreadReply(int number, const QString &threadId,
                               const QString &parentId, const QString &body,
                               QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    PullEvent ev;
    ev.type = "thread-reply";
    ev.threadId = threadId;
    ev.parentId = parentId;
    ev.body = stripEdgeNewlines(body);
    ev = makeSignedEvent(number, ev);
    return appendEvent(number, ev,
                       QStringLiteral("pull #%1: thread reply").arg(number),
                       error);
}

bool PullStore::setThreadState(int number, const QString &threadId,
                               const QString &state, const QString &body,
                               QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    PullEvent ev;
    ev.type = "thread-state";
    ev.threadId = threadId;
    ev.state = state;
    ev.body = stripEdgeNewlines(body);
    ev = makeSignedEvent(number, ev);
    return appendEvent(
        number, ev,
        QStringLiteral("pull #%1: thread %2").arg(number).arg(state), error);
}

bool PullStore::setSuggestionState(int number, const QString &threadId,
                                   const QString &state,
                                   const QString &appliedCommit,
                                   const QString &body, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This repository is read-only on this node.");
        return false;
    }
    PullEvent ev;
    ev.type = "suggestion-state";
    ev.threadId = threadId;
    ev.state = state;
    ev.appliedCommit = appliedCommit;
    ev.body = stripEdgeNewlines(body);
    ev = makeSignedEvent(number, ev);
    return appendEvent(
        number, ev,
        QStringLiteral("pull #%1: suggestion %2").arg(number).arg(state), error);
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
    if (pr.branchBacked)
        lines << "derive: branch";
    if (!pr.creationBaseOid.isEmpty())
        lines << "creationBaseOid: " + pr.creationBaseOid;
    if (!pr.creationHeadOid.isEmpty())
        lines << "creationHeadOid: " + pr.creationHeadOid;
    if (!pr.mergeBase.isEmpty())
        lines << "mergeBase: " + pr.mergeBase;
    if (!pr.mergeHead.isEmpty())
        lines << "mergeHead: " + pr.mergeHead;
    lines << "ts: " + QString::number(pr.ts);
    lines << "author: " + pr.author;
    lines << "authorName: " + pr.authorName;
    lines << "sig: " + pr.sig;
    lines << "---";
    lines << "";
    if (!writeTextFile(pullDir(pr.number) + "/pull.md",
                       lines.join('\n') + "\n" + pr.description + "\n", error))
        return false;
    if (pr.branchBacked) {
        // Keep the diff out of the repo: it is reconstructed from base..head on
        // read. Drop any stale blobs (e.g. a PR converted to branch-backed) so a
        // reader never picks up an outdated committed copy.
        QFile::remove(pullDir(pr.number) + "/changes.patch");
        QFile::remove(pullDir(pr.number) + "/commits.mbox");
        return true;
    }
    if (!writeTextFile(pullDir(pr.number) + "/changes.patch", pr.patch, error))
        return false;
    // Authored commit series, when present, so merge can replay it with `git am`.
    if (!pr.commits.isEmpty())
        return writeTextFile(pullDir(pr.number) + "/commits.mbox", pr.commits, error);
    return true;
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
    out.branchBacked = fm.get("derive") == QLatin1String("branch");
    out.creationBaseOid = fm.get("creationBaseOid");
    out.creationHeadOid = fm.get("creationHeadOid");
    out.mergeBase = fm.get("mergeBase");
    out.mergeHead = fm.get("mergeHead");
    out.description = fm.body;
    // A branch-backed PR carries no committed diff: reconstruct it from the refs.
    // A merged one resolves against the snapshot taken at merge (the live range
    // would be empty once the base absorbed the commits).
    const bool merged = !out.mergeBase.isEmpty() && !out.mergeHead.isEmpty();
    const QString rbase = merged ? out.mergeBase : out.base;
    const QString rhead = merged ? out.mergeHead : out.head;
    // Reconstruct from real git objects wherever they resolve — the working tree
    // first, then the mirror (it keeps refs/heads/* after a sync, so a PR pushed
    // from another node is reachable even before its branch lands here). Sourcing
    // the diff and commit series straight from the commits means a truncated or
    // otherwise corrupt stored changes.patch / commits.mbox blob (e.g. a binary
    // section damaged in transit through the inbox) can never reach `git am`.
    // Fall through to the on-disk blobs only when nothing resolves anywhere
    // (degraded, but never crashes).
    const bool derived =
        out.branchBacked &&
        (deriveFromRefs(m_workTree, rbase, rhead, &out.patch, &out.commits) ||
         deriveFromRefs(m_mirror, rbase, rhead, &out.patch, &out.commits));
    if (!derived) {
        QFile patch(pullDir(number) + "/changes.patch");
        if (patch.open(QIODevice::ReadOnly))
            out.patch = QString::fromUtf8(patch.readAll());
        QFile mbox(pullDir(number) + "/commits.mbox");
        if (mbox.open(QIODevice::ReadOnly))
            out.commits = QString::fromUtf8(mbox.readAll());
    }
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

QList<PullRequest> PullStore::loadAllStrict(QString *error) const
{
    if (error)
        error->clear();
    if (canWrite()) {
        const PullStore mirrorView(QString(), m_workTree, nullptr);
        return mirrorView.loadFromMirror(error, true);
    }
    return loadFromMirror(error, true);
}

QList<PullRequest> PullStore::loadAllStrictAtRef(const QString &ref,
                                                 QString *error) const
{
    if (error)
        error->clear();
    if (canWrite()) {
        const PullStore mirrorView(QString(), m_workTree, nullptr);
        return mirrorView.loadFromMirror(error, true, ref);
    }
    return loadFromMirror(error, true, ref);
}

int PullStore::createPull(const QString &title, const QString &description,
                          const QString &base, const QString &head,
                          const QString &patch, const QString &commits,
                          bool branchBacked, QString *error)
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
    pr.commits = commits;
    // Branch-backed: the head branch ref carries the change as real commits, so
    // store only the signed pointer and reconstruct the diff from base..head.
    // Recompute patch/commits from the refs now so the signature (and any later
    // cross-node submission) matches exactly what readers will derive.
    if (branchBacked) {
        QString dpatch, dcommits;
        // Only go branch-backed when the committed range actually reproduces the
        // change. If the refs are missing or the range is empty (e.g. the work is
        // still uncommitted in a worktree), keep the passed patch as a stored PR
        // so nothing is dropped.
        const QString baseOid = resolvedCommitOid(m_workTree, base);
        const QString headOid = resolvedCommitOid(m_workTree, head);
        if (!baseOid.isEmpty() && !headOid.isEmpty() &&
            deriveFromImmutableOids(m_workTree, baseOid, headOid, &dpatch,
                                    &dcommits) &&
            !dpatch.trimmed().isEmpty()) {
            pr.branchBacked = true;
            pr.creationBaseOid = baseOid;
            pr.creationHeadOid = headOid;
            pr.patch = dpatch;
            pr.commits = dcommits;
        }
    }
    pr = makeSignedPull(pr);
    pr.number = nextNumber();
    if (!writePull(pr, error))
        return -1;
    if (!commit(QStringLiteral("pull #%1: open").arg(pr.number), error))
        return -1;
    materializePullRef(pr, nullptr);
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

bool PullStore::isBranchBehindBase(int number, bool *behind, QString *error,
                                   int *behindCount) const
{
    if (behind)
        *behind = false;
    if (behindCount)
        *behindCount = 0;
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
    const int count = QString::fromUtf8(output).trimmed().toInt();
    if (behind)
        *behind = count > 0;
    if (behindCount)
        *behindCount = count;
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
        // A leftover agent worktree may still hold this branch ("is already used
        // by worktree at …"). Release it and retry once before giving up.
        const bool recovered = releaseWorktreeHoldingBranch(m_workTree, pr.head) &&
                               runGit(m_workTree, {"checkout", pr.head}, nullptr, &err);
        if (!recovered) {
            if (error)
                *error = QStringLiteral("Could not check out %1: %2").arg(pr.head, err);
            return false;
        }
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
    if (!runGit(m_workTree, {"diff", "--binary", pr.base + ".." + pr.head}, &diff,
                &err)) {
        if (error)
            *error = QStringLiteral("Could not refresh the pull request patch: %1").arg(err);
        return false;
    }
    pr.patch = QString::fromUtf8(diff);
    computeStats(pr);
    if (!writePull(pr, error))
        return false;
    if (!commit(QStringLiteral("pull #%1: update branch from %2")
                    .arg(number)
                    .arg(pr.base),
                error))
        return false;
    // Invalidate rather than refresh: mergePull only trusts a ref that was
    // materialized at creation and never touched since, so a stale one left
    // behind by this update doesn't silently change what a later merge
    // applies. mergePull's pre-#399 fallback (patch/branch-name resolution)
    // takes over correctly once the ref is gone.
    runGit(m_workTree,
          {"update-ref", "-d", QStringLiteral("refs/pr/%1/head").arg(number)},
          nullptr, nullptr);
    return true;
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
    QString err;
    // refs/pr/<n>/head (issue #399) is only ever set at creation time and
    // deleted (never refreshed) by any later edit/conflict-resolution/rebase,
    // so if it resolves here it's guaranteed to still describe exactly the
    // content this PR had when it was opened — trust it directly. A PR that
    // has since been edited has no ref and correctly falls through to the
    // pre-#399 behavior below. The PR's change lives in real commits
    // somewhere reachable — its own materialized ref, its head branch's local
    // ref, or fetched from the mirror — so merge it the robust, patch-free
    // way: bring the commit into reach and `git merge` it into the checked-
    // out base. This replays the exact objects — full authorship, binary
    // files, renames — with none of a patch's context-drift or "corrupt
    // binary patch" fragility. Only a PR with no reachable commit anywhere
    // (pre-#399 stored-patch record, no ref, or an edited PR) falls back to
    // applying the diff below.
    QByteArray prRefSha;
    QString headSha;
    if (runGit(m_workTree,
              {"rev-parse", "--verify", "-q",
               QStringLiteral("refs/pr/%1/head^{commit}").arg(number)},
              &prRefSha) &&
        !prRefSha.trimmed().isEmpty()) {
        headSha = QString::fromUtf8(prRefSha).trimmed();
    } else if (pr.branchBacked) {
        headSha = reachableHeadCommit(m_workTree, m_mirror, pr.head);
    }
    const bool refMerge = !headSha.isEmpty();
    // Snapshot the exact commits the merge is about to apply *before* it runs:
    // `git merge`/am advances the checked-out base, so reading these afterwards
    // would no longer give the PR's pre-merge range. Used below to keep a merged
    // branch-backed PR's diff viewable once the base absorbs the commits.
    QString preBase, preHead;
    if (pr.branchBacked) {
        QByteArray bsha;
        if (runGit(m_workTree, {"rev-parse", "--verify", pr.base + "^{commit}"},
                   &bsha))
            preBase = QString::fromUtf8(bsha).trimmed();
        preHead = headSha; // the resolved head (local ref or fetched from mirror)
        if (preHead.isEmpty()) {
            QByteArray hsha;
            if (runGit(m_workTree, {"rev-parse", "--verify", pr.head + "^{commit}"},
                       &hsha))
                preHead = QString::fromUtf8(hsha).trimmed();
        }
    }
    bool applied = false;
    if (refMerge) {
        // Real merge of the head commit into the current base. A fast-forward when
        // the base is an ancestor (the common linear case), else a merge commit;
        // either way every authored commit stays in history. On conflict, abort so
        // the tree is left clean and the reviewer can resolve it interactively.
        applied = runGit(m_workTree,
                         {"merge", "--no-edit", "-m",
                          QStringLiteral("merge pull #%1: %2").arg(number).arg(pr.title),
                          headSha},
                         nullptr, &err);
        if (!applied) {
            runGit(m_workTree, {"merge", "--abort"}, nullptr, nullptr);
            if (error)
                *error = "Could not merge the pull request's branch cleanly: " + err;
            return false;
        }
    } else {
        // A branch-backed PR has no committed blobs; readPull derived its patch
        // and commit series from the refs. Replay them through a temp file so the
        // rest of the merge is identical whether the diff was stored or rebuilt.
        const QString storedPatch = pullDir(number) + "/changes.patch";
        const QString storedMbox = pullDir(number) + "/commits.mbox";
        QString tempFile;
        auto materialize = [&](const QString &onDisk, const QString &content,
                               const char *suffix) -> QString {
            // For a branch-backed PR the authoritative bytes are the ones readPull
            // reconstructed from the refs; a stale or truncated on-disk blob (e.g.
            // an inbox-drained commits.mbox) must never be preferred over them, or
            // a damaged binary section resurfaces as "corrupt binary patch".
            if (!pr.branchBacked && QFileInfo::exists(onDisk))
                return onDisk;
            tempFile = QDir::temp().filePath(
                QStringLiteral("forkmesh-merge-%1.%2").arg(number).arg(suffix));
            return writeTextFile(tempFile, content, error) ? tempFile : QString();
        };
        if (!pr.commits.isEmpty()) {
            // Replay the author's commits so their name/email/date/message survive.
            // 3-way lets `git am` resolve against the current base; on any failure
            // we abort so the working tree is left clean for the reviewer to retry.
            const QString mbox = materialize(storedMbox, pr.commits, "mbox");
            if (mbox.isEmpty())
                return false;
            applied = runGit(m_workTree, {"am", "--3way", mbox}, nullptr, &err);
            if (!applied) {
                runGit(m_workTree, {"am", "--abort"}, nullptr, nullptr);
                if (error)
                    *error =
                        "Could not replay the pull request's commits cleanly: " + err;
            }
        } else {
            const QString patch = materialize(storedPatch, pr.patch, "patch");
            if (patch.isEmpty())
                return false;
            applied = runGit(m_workTree, {"apply", "--index", "--3way", patch},
                             nullptr, &err);
            if (!applied && error)
                *error = "Could not apply the patch cleanly: " + err;
        }
        if (!tempFile.isEmpty())
            QFile::remove(tempFile);
        if (!applied)
            return false;
    }
    pr.status = "merged";
    // Freeze the snapshot taken above so the merged PR's diff stays viewable.
    if (pr.branchBacked && !preBase.isEmpty() && !preHead.isEmpty()) {
        pr.mergeBase = preBase;
        pr.mergeHead = preHead;
    }
    if (!writePull(pr, error))
        return false;
    // Record the status update on the pulls/ metadata branch (issue #399) -
    // separate from whatever just landed the code change on m_workTree below.
    if (!commit(QStringLiteral("pull #%1: merged").arg(number), error))
        return false;
    // git merge and git am both commit the code change themselves; only the
    // flat-patch `git apply --index --3way` path (no ref, no commit series)
    // left it staged-but-uncommitted, so land that one explicitly.
    if (refMerge || !pr.commits.isEmpty())
        return true;
    if (!runGit(m_workTree, {"add", "-A"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    if (!runGit(m_workTree,
               {"commit", "-m",
                QStringLiteral("merge pull #%1: %2").arg(number).arg(pr.title)},
               nullptr, &err)) {
        if (err.contains("nothing to commit"))
            return true;
        if (error)
            *error = "git commit failed: " + err;
        return false;
    }
    return true;
}

// ---- Interactive conflict resolution ---------------------------------------

QString PullStore::syntheticMbox(const PullRequest &pr)
{
    // A flat patch carries no author/commit metadata; wrap it in a minimal,
    // single-commit mbox so `git am` can apply it and still credit the PR author.
    const QString name = pr.authorName.trimmed().isEmpty()
                             ? QStringLiteral("ForkMesh contributor")
                             : pr.authorName.trimmed();
    // The pubkey is base64url (valid email local-part chars); fall back to a
    // placeholder so the address is always well-formed.
    const QString email = (pr.author.isEmpty() ? QStringLiteral("contributor")
                                               : pr.author) +
                          QStringLiteral("@forkmesh");
    const QDateTime when = pr.ts > 0 ? QDateTime::fromMSecsSinceEpoch(pr.ts)
                                     : QDateTime::currentDateTime();
    QString out;
    out += "From 0000000000000000000000000000000000000000 Mon Sep 17 00:00:00 2001\n";
    out += "From: " + name + " <" + email + ">\n";
    out += "Date: " + when.toString(Qt::RFC2822Date) + "\n";
    out += "Subject: [PATCH] " + (pr.title.isEmpty() ? QStringLiteral("Pull request")
                                                      : pr.title) +
           "\n\n";
    if (!pr.description.trimmed().isEmpty())
        out += pr.description.trimmed() + "\n\n";
    out += "---\n";
    QString patch = pr.patch;
    if (!patch.endsWith('\n'))
        patch += '\n';
    out += patch;
    return out;
}

// Path to the in-progress `git am` state directory, or empty when none.
static QString amStateDir(const QString &workTree)
{
    QByteArray out;
    if (!runGit(workTree, {"rev-parse", "--git-path", "rebase-apply"}, &out))
        return QString();
    QString path = QString::fromUtf8(out).trimmed();
    if (path.isEmpty())
        return QString();
    if (!QDir::isAbsolutePath(path))
        path = workTree + "/" + path;
    return QFileInfo::exists(path) ? path : QString();
}

// Files the merge left unresolved (unmerged index entries).
static QStringList unmergedFiles(const QString &workTree)
{
    QByteArray out;
    runGit(workTree, {"diff", "--name-only", "--diff-filter=U"}, &out);
    return QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
}

bool PullStore::conflictMergeInProgress() const
{
    return (!m_workTree.isEmpty() && !amStateDir(m_workTree).isEmpty()) ||
           (!m_editWorkTree.isEmpty() &&
            !amStateDir(m_editWorkTree).isEmpty());
}

// Check out a fresh work-branch for the PR (its named head, or pull/<N>, started
// from the base) and `git am` the PR onto it, so any changes stay isolated from
// the base branch. Sets the m_am* state. Returns false on a hard failure (already
// torn down); on success sets *cleanApply (true = applied with no conflict and
// committed on the branch; false = conflict markers left in the tree with the am
// session open) and, when conflicting, fills *conflicted with the unmerged paths.
// Shared by startConflictMerge (resolve) and startPullFileEdit (edit a file).
bool PullStore::beginPullBranch(int number, QStringList *conflicted,
                                bool *cleanApply, QString *error)
{
    if (conflicted)
        conflicted->clear();
    if (cleanApply)
        *cleanApply = false;
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("This needs a local working tree.");
        return false;
    }
    if (conflictMergeInProgress()) {
        if (error)
            *error = QStringLiteral("Another pull-request operation is already in "
                                    "progress; finish or cancel it first.");
        return false;
    }
    // `git am` refuses to run on a dirty tree, and the work would entangle the
    // user's own edits — require a clean tree up front.
    QByteArray porcelain;
    runGit(m_workTree, {"status", "--porcelain"}, &porcelain);
    if (!porcelain.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Commit or stash your local changes first.");
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

    // Work on the PR's own branch so changes stay isolated from the base branch —
    // a later mergePull is what lands them. Remember where to return to.
    QString err;
    QByteArray originalBranch;
    QByteArray originalCommit;
    runGit(m_workTree, {"rev-parse", "--abbrev-ref", "HEAD"}, &originalBranch, nullptr);
    runGit(m_workTree, {"rev-parse", "--verify", "HEAD"}, &originalCommit, nullptr);
    const QString current = QString::fromUtf8(originalBranch).trimmed();
    m_amRestoreRef = (current.isEmpty() || current == QLatin1String("HEAD"))
                         ? QString::fromUtf8(originalCommit).trimmed()
                         : current;

    // Branch to commit onto: the PR's named head, or a synthesized pull/<N>.
    // Never the base branch itself (that would defeat the isolation).
    QString branchName = pr.head.trimmed();
    if (branchName.isEmpty() || branchName == m_amRestoreRef ||
        branchName == pr.base.trimmed())
        branchName = QStringLiteral("pull/%1").arg(number);
    m_amBranch = branchName;

    // Start point: the PR's recorded base when it resolves to a commit, else the
    // current checkout. Capture it as a stable SHA so the regenerated diff is
    // unaffected by later ref movement.
    QString startPoint = m_amRestoreRef;
    if (!pr.base.trimmed().isEmpty()) {
        QByteArray baseSha;
        if (runGit(m_workTree,
                   {"rev-parse", "--verify", "--quiet",
                    pr.base.trimmed() + "^{commit}"},
                   &baseSha) &&
            !baseSha.trimmed().isEmpty())
            startPoint = pr.base.trimmed();
    }
    QByteArray startSha;
    runGit(m_workTree, {"rev-parse", "--verify", startPoint}, &startSha, nullptr);
    m_amBase = QString::fromUtf8(startSha).trimmed();
    if (m_amBase.isEmpty())
        m_amBase = startPoint;

    if (!runGit(m_workTree, {"checkout", "-B", m_amBranch, startPoint}, nullptr,
                &err)) {
        const QString branch = m_amBranch;
        m_amBranch.clear();
        m_amBase.clear();
        m_amRestoreRef.clear();
        if (error)
            *error =
                QStringLiteral("Could not create the pull request branch %1: %2")
                    .arg(branch, err);
        return false;
    }

    // Source mbox: the authored series when present (committed blob, or the one
    // readPull reconstructed for a branch-backed PR), else a single synthesized
    // commit from the flat patch. A temp mbox is read once by `git am`, then
    // removed. Preferring pr.commits keeps every authored commit on the branch.
    // Never trust an on-disk commits.mbox for a branch-backed PR: readPull
    // rebuilt pr.commits from the refs, while the stored blob may be a truncated
    // inbox artifact whose damaged binary section trips "corrupt binary patch".
    const QString realMbox = pullDir(number) + "/commits.mbox";
    QString mboxPath;
    QString tempMbox;
    if (!pr.commits.isEmpty() && !pr.branchBacked && QFileInfo::exists(realMbox)) {
        mboxPath = realMbox;
    } else {
        tempMbox = QDir::temp().filePath(
            QStringLiteral("forkmesh-pull-%1.mbox").arg(number));
        if (!writeTextFile(tempMbox, pr.commits.isEmpty() ? syntheticMbox(pr)
                                                          : pr.commits,
                           error)) {
            abortConflictMerge();
            return false;
        }
        mboxPath = tempMbox;
    }

    QProcess git;
    git.start("git", {"-C", m_workTree, "am", "--3way", mboxPath});
    git.waitForFinished(60000);
    const bool ok = git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
    if (!tempMbox.isEmpty())
        QFile::remove(tempMbox); // git am has already copied it into its state dir

    if (ok) {
        if (cleanApply)
            *cleanApply = true;
        return true; // applied with no conflict; committed on the branch
    }
    // Non-zero exit: a content conflict leaves the am session in progress with
    // unmerged files. Anything else is a hard failure we shouldn't leave behind.
    QStringList files = unmergedFiles(m_workTree);
    if (amStateDir(m_workTree).isEmpty() || files.isEmpty()) {
        const QString detail =
            QString::fromUtf8(git.readAllStandardError()).trimmed().left(300);
        abortConflictMerge();
        if (error) {
            // A corrupt/truncated patch (typically a binary section damaged in
            // transit) makes `git am` bail before it can leave any conflict to
            // resolve. The commits are reconstructed from refs whenever those are
            // reachable, so a recurrence means the source objects aren't here yet
            // — point the reviewer at the fix instead of the raw git error.
            if (detail.contains(QLatin1String("corrupt")))
                *error = QStringLiteral(
                             "This pull request's patch is corrupt, so it could "
                             "not be applied. Sync the repository to fetch an "
                             "intact copy of its commits, then try again. (%1)")
                             .arg(detail.isEmpty() ? QStringLiteral("corrupt patch")
                                                   : detail);
            else
                *error =
                    QStringLiteral("The pull request could not be applied: %1")
                        .arg(detail.isEmpty() ? QStringLiteral("patch did not apply")
                                              : detail);
        }
        return false;
    }
    if (conflicted)
        *conflicted = files;
    return true;
}

bool PullStore::startConflictMerge(int number, QStringList *conflicted,
                                   bool *resolvedClean, QString *error)
{
    if (resolvedClean)
        *resolvedClean = false;
    bool clean = false;
    if (!beginPullBranch(number, conflicted, &clean, error))
        return false;
    if (clean) {
        // Applied with no markers to edit — finalize on the branch straight away.
        if (resolvedClean)
            *resolvedClean = true;
        return finishConflictMerge(number, error);
    }
    return true; // conflicts left in the tree for the caller to resolve
}

bool PullStore::startConflictAgentEdit(int number, QStringList *conflicted,
                                       bool *resolvedClean, QString *error)
{
    if (resolvedClean)
        *resolvedClean = false;
    bool clean = false;
    if (!beginPullConflictWorkTree(number, conflicted, &clean, error))
        return false;
    if (clean) {
        if (resolvedClean)
            *resolvedClean = true;
        return finishConflictMerge(number, error);
    }
    return true;
}

bool PullStore::startPullFileEdit(int number, const QString &relPath,
                                  QString *content, QString *error)
{
    QStringList conflicted;
    bool clean = false;
    if (!beginPullBranch(number, &conflicted, &clean, error))
        return false;
    if (!clean) {
        // We can't edit through conflict markers; the PR must be resolved first.
        abortConflictMerge();
        if (error)
            *error = QStringLiteral("This pull request has conflicts - use "
                                    "\"Resolve conflicts\" first, then edit.");
        return false;
    }
    // The PR is now applied on m_amBranch; hand back the file to edit.
    const QString abs = m_workTree + "/" + relPath;
    QFile f(abs);
    if (!QFileInfo::exists(abs) || !f.open(QIODevice::ReadOnly)) {
        abortConflictMerge();
        if (error)
            *error = QStringLiteral("'%1' is not a file in this pull request.")
                         .arg(relPath);
        return false;
    }
    if (content)
        *content = QString::fromUtf8(f.readAll());
    f.close();
    return true; // branch left checked out; finishPullFileEdit commits the edit
}

bool PullStore::finishPullFileEdit(int number, const QString &relPath,
                                   const QString &content, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("Editing needs a local working tree.");
        return false;
    }
    if (!writeTextFile(m_workTree + "/" + relPath, content, error))
        return false;
    QString err;
    if (!runGit(m_workTree, {"add", "--", relPath}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    QByteArray staged;
    runGit(m_workTree, {"diff", "--cached", "--name-only"}, &staged);
    if (staged.trimmed().isEmpty()) {
        // The file is unchanged — nothing to commit; tear the work branch down.
        abortConflictMerge();
        if (error)
            *error = QStringLiteral("No changes to commit.");
        return false;
    }
    if (!runGit(m_workTree,
                {"commit", "-m",
                 QStringLiteral("pull #%1: edit %2").arg(number).arg(relPath)},
                nullptr, &err)) {
        if (error)
            *error = "git commit failed: " + err;
        return false;
    }
    return finalizeOnPullBranch(
        number, QStringLiteral("pull #%1: edit %2").arg(number).arg(relPath),
        error);
}

QString PullStore::agentEditWorkTree() const
{
    return m_editWorkTree;
}

// Check the pull request's branch out in a throwaway linked worktree so an agent
// can edit it without the user's own checkout being involved at all (adhoc
// #437): it may be dirty, mid-rebase, or sitting on another branch. A
// branch-backed PR is edited at its real branch tip (its commits *are* the pull
// request); anything else is replayed onto the base with `git am`, the same way
// the in-tree path does it. The worktree is detached so it never collides with a
// branch that is checked out elsewhere.
bool PullStore::beginPullEditWorkTree(int number, QString *error)
{
    discardPullEditWorkTree();
    if (!canWrite())
        return false; // caller falls back and reports the missing working tree
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

    const QString baseRef = pr.base.trimmed();
    QString baseSha = resolvedCommitOid(m_workTree, baseRef);
    if (baseSha.isEmpty())
        baseSha = resolvedCommitOid(m_workTree, QStringLiteral("HEAD"));
    if (baseSha.isEmpty())
        return false; // nothing to branch from here — let the caller fall back

    // The PR's own branch when it names one that lives here; a cross-node
    // "<node>:<branch>" label or a collision with the base means a synthesized
    // pull/<N> instead (never the base itself — that would defeat the isolation).
    QString branchName = pr.head.trimmed();
    if (branchName.contains(QLatin1Char(':')) || branchName == baseRef)
        branchName.clear();
    QString headSha;
    if (!branchName.isEmpty() && pr.branchBacked)
        headSha = resolvedCommitOid(m_workTree, "refs/heads/" + branchName);
    if (branchName.isEmpty())
        branchName = QStringLiteral("pull/%1").arg(number);

    const QString dir = QDir::temp().filePath(
        QStringLiteral("forkmesh-pr-edit-%1-%2")
            .arg(number)
            .arg(QDateTime::currentMSecsSinceEpoch()));
    if (!runGit(m_workTree, {"worktree", "add", "--detach", dir,
                             headSha.isEmpty() ? baseSha : headSha})) {
        runGit(m_workTree, {"worktree", "prune"}, nullptr, nullptr);
        QDir(dir).removeRecursively();
        return false; // no worktree available — fall back to the in-tree replay
    }
    m_editWorkTree = dir;

    if (headSha.isEmpty()) {
        // Replay the PR onto the base inside the scratch worktree. Conflicts
        // belong to the resolve flow, so anything short of a clean apply is torn
        // down here.
        const QString mboxPath =
            QDir::temp().filePath(QStringLiteral("forkmesh-pr-edit-%1.mbox").arg(number));
        const bool wrote = writeTextFile(
            mboxPath, pr.commits.isEmpty() ? syntheticMbox(pr) : pr.commits, nullptr);
        const bool applied = wrote && runGit(dir, {"am", "--3way", mboxPath});
        QFile::remove(mboxPath);
        if (!applied) {
            const bool conflicted = !unmergedFiles(dir).isEmpty();
            discardPullEditWorkTree();
            if (error)
                *error = conflicted
                             ? QStringLiteral("This pull request has conflicts - use "
                                              "\"Resolve conflicts\" first.")
                             : QStringLiteral("The pull request could not be "
                                              "applied to its base.");
            return false;
        }
    }

    m_amBranch = branchName;
    m_amBase = baseSha;
    m_amRestoreRef.clear(); // nothing was checked out in the user's tree
    return true;
}

// Replay a PR onto its current base in a detached worktree, leaving any
// conflict markers there for an agent. This deliberately has no in-tree
// fallback: a user's unrelated tracked/untracked changes must never block or be
// swept into an automated PR fix.
bool PullStore::beginPullConflictWorkTree(int number, QStringList *conflicted,
                                          bool *cleanApply, QString *error)
{
    if (conflicted)
        conflicted->clear();
    if (cleanApply)
        *cleanApply = false;
    discardPullEditWorkTree();
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("Conflict fixing needs a local working tree.");
        return false;
    }
    if (conflictMergeInProgress()) {
        if (error)
            *error = QStringLiteral("Another pull-request operation is already in "
                                    "progress; finish or cancel it first.");
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

    QString baseSha = resolvedCommitOid(m_workTree, pr.base.trimmed());
    if (baseSha.isEmpty())
        baseSha = resolvedCommitOid(m_workTree, QStringLiteral("HEAD"));
    if (baseSha.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not resolve the pull request base.");
        return false;
    }

    const QString dir = QDir::temp().filePath(
        QStringLiteral("forkmesh-pr-conflict-%1-%2")
            .arg(number)
            .arg(QDateTime::currentMSecsSinceEpoch()));
    QString err;
    if (!runGit(m_workTree,
                {"worktree", "add", "--detach", dir, baseSha},
                nullptr, &err)) {
        runGit(m_workTree, {"worktree", "prune"}, nullptr, nullptr);
        QDir(dir).removeRecursively();
        if (error)
            *error = QStringLiteral(
                         "Could not create an isolated worktree for the agent: %1")
                         .arg(err);
        return false;
    }
    m_editWorkTree = dir;
    // Always land agent conflict fixes on a dedicated branch. This avoids
    // trying to move a PR head branch that may itself be checked out beside
    // unrelated local changes.
    m_amBranch = QStringLiteral("pull/%1-agent-fix").arg(number);
    m_amBase = baseSha;
    m_amRestoreRef.clear();

    const QString mboxPath = QDir::temp().filePath(
        QStringLiteral("forkmesh-pr-conflict-%1.mbox").arg(number));
    if (!writeTextFile(mboxPath,
                       pr.commits.isEmpty() ? syntheticMbox(pr) : pr.commits,
                       error)) {
        abortConflictMerge();
        return false;
    }

    QProcess git;
    git.start("git", {"-C", dir, "am", "--3way", mboxPath});
    git.waitForFinished(60000);
    QFile::remove(mboxPath);
    const bool ok =
        git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
    if (ok) {
        if (cleanApply)
            *cleanApply = true;
        return true;
    }

    const QStringList files = unmergedFiles(dir);
    if (amStateDir(dir).isEmpty() || files.isEmpty()) {
        const QString detail =
            QString::fromUtf8(git.readAllStandardError()).trimmed().left(300);
        abortConflictMerge();
        if (error)
            *error = QStringLiteral("The pull request could not be applied in "
                                    "the agent worktree: %1")
                         .arg(detail.isEmpty()
                                  ? QStringLiteral("patch did not apply")
                                  : detail);
        return false;
    }
    if (conflicted)
        *conflicted = files;
    return true;
}

void PullStore::discardPullEditWorkTree()
{
    if (m_editWorkTree.isEmpty())
        return;
    const QString dir = m_editWorkTree;
    m_editWorkTree.clear();
    if (!amStateDir(dir).isEmpty())
        runGit(dir, {"am", "--abort"}, nullptr, nullptr);
    runGit(m_workTree, {"worktree", "remove", "--force", dir}, nullptr, nullptr);
    QDir(dir).removeRecursively();
    runGit(m_workTree, {"worktree", "prune"}, nullptr, nullptr);
}

bool PullStore::startPullAgentEdit(int number, QString *error)
{
    // Preferred path: edit in a scratch worktree on the PR's own branch, which
    // works whatever state the user's checkout is in (adhoc #437). An empty
    // error there means git could not hand out a worktree, not that the pull
    // request is unfit — so fall through to the in-tree replay below.
    QString worktreeError;
    if (beginPullEditWorkTree(number, &worktreeError))
        return true;
    if (!worktreeError.isEmpty()) {
        if (error)
            *error = worktreeError;
        return false;
    }

    QStringList conflicted;
    bool clean = false;
    if (!beginPullBranch(number, &conflicted, &clean, error))
        return false;
    if (!clean) {
        // Agent edits sit on top of the applied PR; conflicts must go through
        // the resolve flow first.
        abortConflictMerge();
        if (error)
            *error = QStringLiteral("This pull request has conflicts - use "
                                    "\"Resolve conflicts\" first.");
        return false;
    }
    return true; // branch left checked out; finishPullAgentEdit commits the edits
}

bool PullStore::finishPullAgentEdit(int number, const QString &commitMsg,
                                    QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("Editing needs a local working tree.");
        return false;
    }
    // The edits are wherever the agent ran: the scratch worktree, or the main
    // tree when the in-tree fallback was taken.
    const QString dir = m_editWorkTree.isEmpty() ? m_workTree : m_editWorkTree;
    QString err;
    if (!runGit(dir, {"add", "-A"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    QByteArray staged;
    runGit(dir, {"diff", "--cached", "--name-only"}, &staged);
    if (staged.trimmed().isEmpty()) {
        // The agent changed nothing — tear the work branch down.
        abortConflictMerge();
        if (error)
            *error = QStringLiteral("No changes to commit.");
        return false;
    }
    if (!runGit(dir, {"commit", "-m", commitMsg}, nullptr, &err)) {
        if (error)
            *error = "git commit failed: " + err;
        return false;
    }
    if (!m_editWorkTree.isEmpty() && !moveBranchToEditTip(error))
        return false;
    return finalizeOnPullBranch(number, commitMsg, error);
}

// The scratch worktree commits on a detached HEAD; land that commit on the PR's
// branch so the branch (and with it a branch-backed PR) carries the fixes. A
// leftover agent worktree holding the branch is released and retried; the user's
// own checkout sitting on it is fast-forwarded instead.
bool PullStore::moveBranchToEditTip(QString *error)
{
    const QString tip = resolvedCommitOid(m_editWorkTree, QStringLiteral("HEAD"));
    if (tip.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not read the edited commit.");
        return false;
    }
    QString err;
    if (runGit(m_workTree, {"branch", "-f", m_amBranch, tip}, nullptr, &err))
        return true;
    if (releaseWorktreeHoldingBranch(m_workTree, m_amBranch) &&
        runGit(m_workTree, {"branch", "-f", m_amBranch, tip}, nullptr, &err))
        return true;
    // Still refused: the branch is checked out in the user's own tree. A
    // fast-forward moves it there without touching their uncommitted work.
    QByteArray current;
    runGit(m_workTree, {"rev-parse", "--abbrev-ref", "HEAD"}, &current, nullptr);
    if (QString::fromUtf8(current).trimmed() == m_amBranch &&
        runGit(m_workTree, {"merge", "--ff-only", tip}, nullptr, &err))
        return true;
    discardPullEditWorkTree();
    if (error)
        *error = QStringLiteral("The fixes could not be put on %1: %2")
                     .arg(m_amBranch, err);
    return false;
}

bool PullStore::deletePullFile(int number, const QString &relPath, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("Editing needs a local working tree.");
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

    // Edit the stored diff directly rather than rebuilding the PR's branch
    // (issue #258). The old path checked the PR's commits out onto the current
    // base with `git am` and regenerated the patch — but `git am` silently skips
    // commits whose content already exists on the base ("patch already applied"),
    // so deleting a single file could drop most of the PR. Excising just this
    // file's section keeps every other change exactly as the author wrote it.
    bool removed = false;
    const QString newPatch = removeFileSectionsFromDiff(pr.patch, relPath, &removed);
    if (!removed) {
        if (error)
            *error = QStringLiteral("'%1' is not a file in this pull request.")
                         .arg(relPath);
        return false;
    }
    if (!bodyHasFileDiff(newPatch)) {
        if (error)
            *error = QStringLiteral("'%1' is the only file changed by this pull "
                                    "request; close the pull request instead of "
                                    "deleting its last file.")
                         .arg(relPath);
        return false;
    }
    pr.patch = newPatch;
    if (!pr.commits.isEmpty())
        pr.commits = removeFileFromMbox(pr.commits, relPath);
    computeStats(pr); // refresh files/additions/deletions from the trimmed patch

    // The trimmed diff no longer matches base..head, so it can no longer be
    // reconstructed from the branch — persist it as a stored-patch PR (writePull
    // then writes the blobs, which become the source of truth on read).
    pr.branchBacked = false;
    pr.creationBaseOid.clear();
    pr.creationHeadOid.clear();
    pr.mergeBase.clear();
    pr.mergeHead.clear();
    if (!writePull(pr, error))
        return false;
    // writePull only (re)writes commits.mbox when it's non-empty; if every
    // authored commit collapsed away, drop the stale file so merge falls back to
    // the patch instead of replaying an out-of-date series.
    if (pr.commits.isEmpty())
        QFile::remove(pullDir(number) + "/commits.mbox");
    if (!commit(QStringLiteral("pull #%1: delete %2").arg(number).arg(relPath),
               error))
        return false;
    // Invalidate any materialized ref (issue #399): it would still point at
    // the pre-edit commits (including the just-removed file's), and a stale
    // ref would silently override the trimmed patch/mbox this just wrote.
    // See updateBranchFromBase for the same reasoning.
    runGit(m_workTree,
          {"update-ref", "-d", QStringLiteral("refs/pr/%1/head").arg(number)},
          nullptr, nullptr);
    return true;
}

// Shared tail for the on-branch PR operations (resolve, edit): leave the work
// branch, regenerate the PR's patch + commit series against the current base so
// it stays cleanly mergeable, keep it open, and commit the refreshed pulls/
// metadata onto the original branch. Consumes (and clears) the m_am* state.
bool PullStore::finalizeOnPullBranch(int number, const QString &commitMsg,
                                     QString *error)
{
    QString err;
    if (!m_amRestoreRef.isEmpty() && m_amRestoreRef != m_amBranch &&
        !runGit(m_workTree, {"checkout", m_amRestoreRef}, nullptr, &err)) {
        if (error)
            *error = QStringLiteral("Committed on %1, but could not return to %2: %3")
                         .arg(m_amBranch, m_amRestoreRef, err);
        return false;
    }
    PullRequest pr;
    if (!readPull(number, pr)) {
        if (error)
            *error = QStringLiteral("Pull request #%1 not found.").arg(number);
        return false;
    }
    const QString range = m_amBase + ".." + m_amBranch;
    QByteArray diff;
    if (!runGit(m_workTree, {"diff", "--binary", range}, &diff, &err)) {
        if (error)
            *error = "Could not refresh the pull request patch: " + err;
        return false;
    }
    QByteArray mbox;
    if (!runGit(m_workTree, {"format-patch", "--binary", "--stdout", range}, &mbox,
                &err)) {
        if (error)
            *error = "Could not refresh the pull request commits: " + err;
        return false;
    }
    pr.patch = QString::fromUtf8(diff);
    pr.commits = QString::fromUtf8(mbox);
    pr.head = m_amBranch;
    computeStats(pr);
    if (!writePull(pr, error))
        return false;
    discardPullEditWorkTree(); // no-op unless an agent edit ran in a scratch tree
    m_amBranch.clear();
    m_amBase.clear();
    m_amRestoreRef.clear();
    if (!commit(commitMsg, error))
        return false;
    // Invalidate rather than refresh - see updateBranchFromBase for why.
    runGit(m_workTree,
          {"update-ref", "-d", QStringLiteral("refs/pr/%1/head").arg(number)},
          nullptr, nullptr);
    return true;
}

bool PullStore::finishConflictMerge(int number, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("Merging needs a local working tree.");
        return false;
    }
    const QString dir = m_editWorkTree.isEmpty() ? m_workTree : m_editWorkTree;
    // Refuse to commit a tree that still carries conflict markers.
    for (const QString &rel : unmergedFiles(dir)) {
        QFile f(dir + "/" + rel);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(f.readAll());
        if (text.contains(QStringLiteral("\n<<<<<<< ")) ||
            text.startsWith(QStringLiteral("<<<<<<< ")) ||
            text.contains(QStringLiteral("\n>>>>>>> "))) {
            if (error)
                *error = QStringLiteral("Resolve every conflict marker in %1 first.")
                             .arg(rel);
            return false;
        }
    }
    QString err;
    if (!runGit(dir, {"add", "-A"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    // Complete the replay only if an am session is actually open (a clean apply
    // in startConflictMerge already committed it).
    if (!amStateDir(dir).isEmpty() &&
        !runGit(dir, {"am", "--continue"}, nullptr, &err)) {
        if (error)
            *error = "Could not complete the merge: " + err;
        return false;
    }
    if (!m_editWorkTree.isEmpty() && !moveBranchToEditTip(error))
        return false;
    // The resolution now lives on m_amBranch. Return to the original branch,
    // regenerate the PR from it, and leave it open — the base branch only gets
    // the refreshed pulls/ metadata; merging stays the owner's separate step.
    return finalizeOnPullBranch(
        number,
        QStringLiteral("pull #%1: resolve conflicts on %2")
            .arg(number)
            .arg(m_amBranch),
        error);
}

void PullStore::abortConflictMerge()
{
    if (m_workTree.isEmpty())
        return;
    if (!m_editWorkTree.isEmpty()) {
        // Agent edit in a scratch worktree: nothing was checked out in the user's
        // tree and the PR's branch was never moved, so dropping the worktree
        // undoes the whole thing. Never delete m_amBranch here — unlike the
        // in-tree path it is the PR's real branch, not a throwaway.
        discardPullEditWorkTree();
        m_amBranch.clear();
        m_amBase.clear();
        m_amRestoreRef.clear();
        return;
    }
    if (!amStateDir(m_workTree).isEmpty())
        runGit(m_workTree, {"am", "--abort"}, nullptr, nullptr);
    // Return to the branch we started from and drop the throwaway PR branch so a
    // cancelled resolution leaves no trace.
    if (!m_amRestoreRef.isEmpty() && m_amRestoreRef != m_amBranch)
        runGit(m_workTree, {"checkout", m_amRestoreRef}, nullptr, nullptr);
    if (!m_amBranch.isEmpty())
        runGit(m_workTree, {"branch", "-D", m_amBranch}, nullptr, nullptr);
    m_amBranch.clear();
    m_amBase.clear();
    m_amRestoreRef.clear();
}

bool PullStore::checkMergeable(int number, bool *clean,
                               QStringList *conflictFiles, QString *error,
                               bool keepGuiAlive) const
{
    if (clean)
        *clean = false;
    if (conflictFiles)
        conflictFiles->clear();
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("Checking the merge needs a local working tree.");
        return false;
    }
    PullRequest pr;
    if (!readPull(number, pr)) {
        if (error)
            *error = QStringLiteral("Pull request #%1 not found.").arg(number);
        return false;
    }
    // A branch-backed PR merges by real refs (see mergePull), so preview it the
    // same way: `git merge-tree` runs the actual 3-way merge in memory and reports
    // conflicts without touching the index or working tree — with no patch, none
    // of the "missing base blob" / context-drift false conflicts a
    // `git apply --check` produces. Only a stored-patch PR (or a branch-backed one
    // whose commits aren't reachable anywhere) falls back to the patch dry-run.
    if (pr.branchBacked) {
        const QString headSha = reachableHeadCommit(m_workTree, m_mirror, pr.head);
        QByteArray baseSha;
        if (!headSha.isEmpty() &&
            runGit(m_workTree, {"rev-parse", "--verify", "--quiet", "HEAD"},
                   &baseSha) &&
            !baseSha.trimmed().isEmpty()) {
            QProcess git;
            git.start("git", {"-C", m_workTree, "merge-tree", "--write-tree",
                              QString::fromUtf8(baseSha).trimmed(), headSha});
            const bool finished =
                waitForFinishedKeepAlive(git, kGitTimeoutMs, keepGuiAlive);
            if (!finished) {
                if (error)
                    *error = QStringLiteral("git timed out checking the merge.");
                return false;
            }
            const bool normal = git.exitStatus() == QProcess::NormalExit;
            const int code = git.exitCode();
            if (normal && code == 0) {
                if (clean)
                    *clean = true;
                return true;
            }
            if (normal && code == 1) {
                // Conflicts. The conflicted-file-info block lists one line per
                // unmerged stage as "<mode> <object> <stage>\t<path>", ending at a
                // blank line before the informational messages.
                if (conflictFiles) {
                    static const QRegularExpression stageLine(
                        QStringLiteral("^\\d+ [0-9a-f]+ [123]\t(.+)$"));
                    const QStringList lines =
                        QString::fromUtf8(git.readAllStandardOutput()).split('\n');
                    for (int i = 1; i < lines.size(); ++i) {
                        if (lines.at(i).trimmed().isEmpty())
                            break; // reached the informational-messages section
                        const QRegularExpressionMatch m = stageLine.match(lines.at(i));
                        if (m.hasMatch() && !conflictFiles->contains(m.captured(1)))
                            *conflictFiles << m.captured(1);
                    }
                }
                return true; // *clean stays false
            }
            // Exit >1 means merge-tree could not run (e.g. unrelated histories);
            // fall through to the patch dry-run rather than hard-failing.
        }
    }
    // A branch-backed PR's diff is reconstructed (readPull filled pr.patch); dry-
    // run it through a temp file. A stored-patch PR uses its committed blob.
    QString patchPath = pullDir(number) + "/changes.patch";
    QString tempFile;
    if (!QFileInfo::exists(patchPath)) {
        if (pr.patch.trimmed().isEmpty()) {
            if (error)
                *error = QStringLiteral("This pull request has no patch to merge.");
            return false;
        }
        tempFile = QDir::temp().filePath(
            QStringLiteral("forkmesh-check-%1.patch").arg(number));
        if (!writeTextFile(tempFile, pr.patch, error))
            return false;
        patchPath = tempFile;
    }

    // --check dry-runs the same 3-way apply mergePull performs, without
    // touching the index or working tree. Run git directly (not through runGit,
    // which truncates failure output) so we get the full per-file diagnostics.
    QProcess git;
    git.start("git",
              {"-C", m_workTree, "apply", "--check", "--3way", patchPath});
    const bool finished = waitForFinishedKeepAlive(git, kGitTimeoutMs, keepGuiAlive);
    if (!tempFile.isEmpty())
        QFile::remove(tempFile);
    if (!finished) {
        if (error)
            *error = QStringLiteral("git timed out checking the merge.");
        return false;
    }
    const QString output = QString::fromUtf8(git.readAllStandardError()) +
                           QString::fromUtf8(git.readAllStandardOutput());
    const bool exitedClean =
        git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
    // The 3-way fallback can "apply with conflicts" and still exit 0, so treat
    // that message as a conflict too — clean means a non-zero exit AND no
    // "with conflicts" report.
    const bool hasConflicts =
        output.contains(QStringLiteral("with conflicts"));
    if (exitedClean && !hasConflicts) {
        if (clean)
            *clean = true;
        return true;
    }
    if (conflictFiles)
        *conflictFiles = parseApplyConflicts(output);
    return true;
}

QString PullStore::baseTip() const
{
    if (!canWrite())
        return QString();
    QByteArray out;
    if (!runGit(m_workTree, {"rev-parse", "HEAD"}, &out, nullptr))
        return QString();
    return QString::fromUtf8(out).trimmed();
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
    if (!commit(QStringLiteral("pull #%1: opened (from %2)")
                    .arg(pr.number)
                    .arg(pr.authorName.isEmpty() ? pr.author.left(8) : pr.authorName),
                error))
        return false;
    materializePullRef(pr, nullptr);
    return true;
}


bool PullStore::deletePull(int number, bool rewriteHistory, QString *error)
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
    // pulls/ now lives on its own metadata branch (issue #399); fall back to
    // m_workTree only if a working tree somehow isn't available at all.
    const QString meta = metaWorkTree();
    const QString metaDir = meta.isEmpty() ? m_workTree : meta;
    // The plain delete below commits path-scoped (`commit -- pulls/<n>`), so
    // unrelated tracked changes elsewhere in the work tree (e.g. edits on main)
    // never enter it and can't block the deletion. Only the opt-in history
    // rewrite needs a clean tree: git filter-branch refuses to run with unstaged
    // changes, so gate the guard on rewriteHistory rather than rejecting every
    // delete when the working tree happens to be dirty.
    if (rewriteHistory && hasUnrelatedTrackedChanges(metaDir, relPath, error))
        return false;

    // First commit a normal deletion so the work tree is clean for the history
    // rewrite below, which then prunes this commit along with the prior
    // pull-only commits (e.g. "pull #N: open" and its changes.patch).
    runGit(metaDir, {"rm", "-r", "--ignore-unmatch", "--", relPath}, nullptr, nullptr);
    if (QDir(dir).exists() && !QDir(dir).removeRecursively()) {
        if (error)
            *error = QStringLiteral("Could not remove pull request folder.");
        return false;
    }
    if (!runGit(metaDir, {"commit", "-m",
                          QStringLiteral("pull #%1: deleted").arg(number),
                          "--", relPath},
                nullptr, &err)) {
        if (!err.contains(QStringLiteral("nothing to commit")) && !err.isEmpty()) {
            if (error)
                *error = QStringLiteral("git commit failed: ") + err;
            return false;
        }
    }
    // Best-effort: drop the materialized content ref too. Leaving it behind is
    // harmless (an unreferenced object eventually swept by gc), just untidy.
    runGit(m_workTree,
          {"update-ref", "-d", QStringLiteral("refs/pr/%1/head").arg(number)},
          nullptr, nullptr);

    // Default path: the PR folder is gone at the tip, which is all most callers
    // want. Skip the expensive full-history rewrite unless explicitly requested.
    if (!rewriteHistory)
        return true;

    // Purge pulls/<number> from every commit so the diff text it carried
    // (changes.patch / commits.mbox) can no longer be found by searching
    // history. Without this the soft delete above only hides the files at the
    // tip; older commits still contain them. --all rewrites every ref in the
    // repo (shared by every linked worktree, including the pulls/ metadata
    // worktree's checked-out forkmesh/pulls branch) regardless of which
    // worktree directory this runs from - but a branch that's checked out
    // elsewhere while filter-branch rewrites it is an unverified edge case
    // here; a metadata-worktree removal/relink before a rewriteHistory=true
    // call would be the safer sequencing if this proves flaky in practice.
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
bool PullStore::commit(const QString &message, QString *error) const
{
    // Commits pulls/ onto its own dedicated branch (issue #399) rather than
    // whatever the user currently has checked out - which, mid conflict
    // resolution or file edit, may not even be main.
    const QString meta = metaWorkTree();
    const QString dir = meta.isEmpty() ? m_workTree : meta;
    QString err;
    if (!runGit(dir, {"add", "pulls"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    if (!runGit(dir, {"commit", "-m", message, "--", "pulls"}, nullptr, &err)) {
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
    // Prefer the dedicated pull-metadata branch (issue #399) so a mirror-only
    // (read-only) node still sees live PR data even though it no longer lands
    // on the default branch; fall back for mirrors that predate this or
    // haven't synced the new branch yet.
    if (runGit(m_mirror,
               {"rev-parse", "--verify", "-q", "refs/heads/forkmesh/pulls^{commit}"},
               &output) &&
        !output.trimmed().isEmpty())
        return QStringLiteral("refs/heads/forkmesh/pulls");
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

QByteArray PullStore::showFromMirror(const QString &repoRelPath, bool *ok,
                                     const QString &refOverride) const
{
    const QString ref = refOverride.isEmpty() ? mirrorRef() : refOverride;
    QByteArray output;
    const bool good = !ref.isEmpty() &&
                      runGit(m_mirror, {"show", ref + ":" + repoRelPath}, &output);
    if (ok)
        *ok = good;
    return good ? output : QByteArray();
}

QList<PullRequest> PullStore::loadFromMirror(QString *error, bool strict,
                                             const QString &refOverride) const
{
    QList<PullRequest> pulls;
    StrictGitReadInternal::Reader strictReader;
    auto readGit = [&](const QStringList &args, QByteArray *output = nullptr) {
        return strict ? strictReader.run(m_mirror, args, output)
                      : runGit(m_mirror, args, output);
    };
    auto readGitInput = [&](const QStringList &args, const QByteArray &input,
                            QByteArray *output) {
        return strict
                   ? strictReader.runInput(m_mirror, args, input, output)
                   : false;
    };
    auto recordStrictError = [&](const QString &message) {
        if (strict && error && error->isEmpty())
            *error = message;
    };
    if (m_mirror.isEmpty()) {
        recordStrictError(
            QStringLiteral("No local mirror to read pull metadata from."));
        return pulls;
    }
    QString ref = refOverride;
    if (ref.isEmpty() && strict) {
        QByteArray output;
        if (readGit({"rev-parse", "--verify", "-q",
                     "refs/heads/forkmesh/pulls^{commit}"},
                    &output) &&
            !output.trimmed().isEmpty()) {
            ref = QStringLiteral("refs/heads/forkmesh/pulls");
        } else if (readGit({"rev-parse", "--verify", "-q", "HEAD"},
                           &output) &&
                   !output.trimmed().isEmpty()) {
            ref = QStringLiteral("HEAD");
        } else if (readGit({"for-each-ref", "--format=%(refname)",
                            "--count=1", "refs/heads/"},
                           &output)) {
            ref = QString::fromUtf8(output).trimmed();
        }
    } else if (ref.isEmpty()) {
        ref = mirrorRef();
    }
    if (ref.isEmpty()) {
        recordStrictError(QStringLiteral("Could not resolve pull metadata HEAD."));
        return pulls;
    }
    QMap<int, QString> strictPullBases;
    QHash<QString, QStringList> strictEventNames;
    QHash<QString, QString> strictOidByPath;
    QHash<QString, QByteArray> strictContentByOid;
    QByteArray listing;
    const QStringList listingArgs =
        strict ? QStringList{QStringLiteral("ls-tree"), QStringLiteral("-r"),
                             ref, QStringLiteral("pulls/")}
               : QStringList{QStringLiteral("ls-tree"), ref,
                             QStringLiteral("pulls/")};
    if (!readGit(listingArgs, &listing)) {
        recordStrictError(
            QStringLiteral("Could not enumerate pull metadata."));
        return pulls;
    }

    if (strict) {
        QMap<QString, QString> relevantOids;
        const QString prefix = QStringLiteral("pulls/");
        for (const QString &line :
             QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts)) {
            const int tab = line.indexOf('\t');
            if (tab < 0)
                continue;
            const QStringList meta =
                line.left(tab).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (meta.size() < 3 || meta.at(1) != QLatin1String("blob"))
                continue;
            const QString path = line.mid(tab + 1);
            if (!path.startsWith(prefix))
                continue;
            const QString rel = path.mid(prefix.size());
            const int slash = rel.indexOf(QLatin1Char('/'));
            if (slash <= 0)
                continue;
            const QString base = rel.left(slash);
            bool numeric = false;
            const int number = base.toInt(&numeric);
            if (!numeric || number <= 0 ||
                base != QString::number(number)) {
                recordStrictError(
                    QStringLiteral("A pull number is invalid."));
                continue;
            }
            const QString name = rel.mid(slash + 1);
            if (name == QLatin1String("pull.md"))
                strictPullBases.insert(number, base);
            if (name == QLatin1String("pull.md") ||
                name == QLatin1String("changes.patch") ||
                name == QLatin1String("commits.mbox") ||
                eventFileRe().match(name).hasMatch()) {
                relevantOids.insert(path, meta.at(2));
            }
        }

        QSet<QString> wantedOids;
        for (auto it = strictPullBases.constBegin();
             it != strictPullBases.constEnd(); ++it) {
            if (!strictReader.consumePullItem()) {
                recordStrictError(
                    QStringLiteral("The strict pull item limit was exceeded."));
                return pulls;
            }
            const QString base = it.value();
            const QString pullPrefix =
                QStringLiteral("pulls/") + base + QLatin1Char('/');
            for (auto blob = relevantOids.lowerBound(pullPrefix);
                 blob != relevantOids.constEnd() &&
                 blob.key().startsWith(pullPrefix);
                 ++blob) {
                const QString name = blob.key().mid(pullPrefix.size());
                if (eventFileRe().match(name).hasMatch()) {
                    if (!strictReader.consumeEventItem()) {
                        recordStrictError(QStringLiteral(
                            "The strict pull event item limit was exceeded."));
                        return pulls;
                    }
                    strictEventNames[base].append(name);
                }
                strictOidByPath.insert(blob.key(), blob.value());
                wantedOids.insert(blob.value());
            }
            strictEventNames[base].sort();
        }

        if (!wantedOids.isEmpty()) {
            QStringList sortedOids = wantedOids.values();
            sortedOids.sort();
            QByteArray batchInput;
            for (const QString &oid : std::as_const(sortedOids))
                batchInput += oid.toUtf8() + '\n';
            QByteArray batch;
            if (!readGitInput({QStringLiteral("cat-file"),
                               QStringLiteral("--batch")},
                              batchInput, &batch)) {
                recordStrictError(
                    QStringLiteral("Could not read pull metadata blobs."));
                return pulls;
            }
            for (qsizetype pos = 0; pos < batch.size();) {
                const qsizetype nl = batch.indexOf('\n', pos);
                if (nl < 0) {
                    recordStrictError(QStringLiteral(
                        "Pull metadata blob framing is invalid."));
                    break;
                }
                const QList<QByteArray> header =
                    batch.mid(pos, nl - pos).split(' ');
                pos = nl + 1;
                if (header.size() != 3 ||
                    header.at(1) != QByteArrayLiteral("blob")) {
                    recordStrictError(
                        QStringLiteral("A pull metadata blob is missing."));
                    continue;
                }
                bool sizeOk = false;
                const qlonglong size = header.at(2).toLongLong(&sizeOk);
                if (!sizeOk || size < 0 || size > batch.size() - pos ||
                    pos + size >= batch.size() ||
                    batch.at(pos + size) != '\n') {
                    recordStrictError(QStringLiteral(
                        "Pull metadata blob framing is invalid."));
                    break;
                }
                strictContentByOid.insert(QString::fromUtf8(header.at(0)),
                                         batch.mid(pos, size));
                pos += size + 1;
            }
            if (strictContentByOid.size() != wantedOids.size()) {
                recordStrictError(QStringLiteral(
                    "Not all pull metadata blobs could be read."));
            }
        }
    }

    auto readBlob = [&](const QString &repoRelPath, bool *ok) {
        if (strict) {
            const QString oid = strictOidByPath.value(repoRelPath);
            const bool good = !oid.isEmpty() &&
                              strictContentByOid.contains(oid);
            if (ok)
                *ok = good;
            return good ? strictContentByOid.value(oid) : QByteArray();
        }
        QByteArray output;
        const bool good = readGit({"show", ref + ":" + repoRelPath}, &output);
        if (ok)
            *ok = good;
        return good ? output : QByteArray();
    };

    QStringList pullBases;
    if (strict) {
        pullBases = strictPullBases.values();
    } else {
        for (const QString &line :
             QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts)) {
            const int tab = line.indexOf('\t');
            if (tab < 0 || !line.contains(" tree "))
                continue;
            pullBases.append(line.mid(tab + 1).section('/', -1));
        }
    }
    for (const QString &base : std::as_const(pullBases)) {
        bool numeric = false;
        const int number = base.toInt(&numeric);
        if (!numeric)
            continue;
        bool ok = false;
        const QByteArray md = readBlob("pulls/" + base + "/pull.md", &ok);
        if (!ok) {
            recordStrictError(
                QStringLiteral("A pull metadata file could not be read."));
            continue;
        }
        const FrontMatter fm = parseFrontMatter(md);
        qint64 timestamp = 0;
        const bool timestampOk = strictJsonSafeInteger(
            fm.get(QStringLiteral("ts")), 0, &timestamp);
        qint64 declaredNumber = 0;
        const bool declaredNumberOk = strictJsonSafeInteger(
            fm.get(QStringLiteral("number")), 1, &declaredNumber);
        const bool validFrontMatter =
            fm.valid && timestampOk && declaredNumberOk &&
            fm.get(QStringLiteral("schema")) ==
                QLatin1String("forkmesh-pull-v1") &&
            declaredNumber == number &&
            fm.values.contains(QStringLiteral("title")) &&
            fm.values.contains(QStringLiteral("base")) &&
            fm.values.contains(QStringLiteral("head")) &&
            fm.values.contains(QStringLiteral("ts")) &&
            fm.values.contains(QStringLiteral("author")) &&
            !fm.get(QStringLiteral("author")).isEmpty() &&
            fm.values.contains(QStringLiteral("sig")) &&
            !fm.get(QStringLiteral("sig")).isEmpty();
        if (!validFrontMatter) {
            recordStrictError(
                QStringLiteral("A pull metadata file is invalid."));
            if (strict)
                continue;
        }
        PullRequest pr;
        pr.number = number;
        pr.title = fm.get("title");
        pr.base = fm.get("base");
        pr.head = fm.get("head");
        pr.status = fm.values.contains("status") ? fm.get("status")
                                                  : QStringLiteral("open");
        pr.ts = strict ? timestamp : fm.num("ts");
        pr.author = fm.get("author");
        pr.authorName = fm.get("authorName");
        pr.sig = fm.get("sig");
        pr.branchBacked = fm.get("derive") == QLatin1String("branch");
        pr.creationBaseOid = fm.get("creationBaseOid");
        pr.creationHeadOid = fm.get("creationHeadOid");
        pr.mergeBase = fm.get("mergeBase");
        pr.mergeHead = fm.get("mergeHead");
        pr.description = fm.body;
        // Branch-backed PRs carry no committed diff — reconstruct it from the
        // base/head refs the mirror already syncs (the merge snapshot for a
        // merged one). Fall back to committed blobs for legacy/portable PRs.
        bool derived = false;
        if (pr.branchBacked) {
            if (strict) {
                derived = deriveFromImmutableOids(
                    m_mirror, pr.creationBaseOid, pr.creationHeadOid,
                    &pr.patch, &pr.commits, &strictReader);
            } else {
                const bool merged =
                    !pr.mergeBase.isEmpty() && !pr.mergeHead.isEmpty();
                const QString deriveBase = merged ? pr.mergeBase : pr.base;
                const QString deriveHead = merged ? pr.mergeHead : pr.head;
                derived = deriveFromRefs(m_mirror, deriveBase, deriveHead,
                                         &pr.patch, &pr.commits);
            }
            if (strict && !derived) {
                recordStrictError(QStringLiteral(
                    "A branch-backed pull could not be derived from its refs."));
            }
        }
        bool patchOk = false;
        bool mboxOk = false;
        if (!derived) {
            pr.patch = QString::fromUtf8(
                readBlob("pulls/" + base + "/changes.patch", &patchOk));
            const QByteArray mbox =
                readBlob("pulls/" + base + "/commits.mbox", &mboxOk);
            if (mboxOk)
                pr.commits = QString::fromUtf8(mbox);
            if (strict && !pr.branchBacked && !patchOk) {
                recordStrictError(QStringLiteral(
                    "A pull request's signed patch could not be read."));
            }
            if (!patchOk && !mboxOk) {
                recordStrictError(QStringLiteral(
                    "A pull request's signed change data could not be read."));
            }
        }
        computeStats(pr);
        QStringList names;
        bool eventsEnumerated = true;
        if (strict) {
            names = strictEventNames.value(base);
        } else {
            // Keep permissive loading on its existing per-pull path. Strict
            // loading already got every event from the recursive tree batch.
            QByteArray dirListing;
            eventsEnumerated =
                readGit({"ls-tree", ref, "pulls/" + base + "/"},
                        &dirListing);
            if (eventsEnumerated) {
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
            }
        }
        if (!eventsEnumerated) {
            recordStrictError(
                QStringLiteral("Could not enumerate pull event metadata."));
        }
        for (const QString &name : std::as_const(names)) {
            bool eok = false;
            const QByteArray evBytes =
                readBlob("pulls/" + base + "/" + name, &eok);
            if (!eok) {
                recordStrictError(
                    QStringLiteral("A pull event file could not be read."));
                continue;
            }
            const FrontMatter eventFrontMatter = parseFrontMatter(evBytes);
            qint64 eventTimestamp = 0;
            const bool eventTimestampOk = strictJsonSafeInteger(
                eventFrontMatter.get(QStringLiteral("ts")), 0,
                &eventTimestamp);
            const bool validEvent =
                eventFrontMatter.valid && eventTimestampOk &&
                eventFrontMatter.values.contains(QStringLiteral("type")) &&
                !eventFrontMatter.get(QStringLiteral("type")).isEmpty() &&
                eventFrontMatter.values.contains(QStringLiteral("author")) &&
                !eventFrontMatter.get(QStringLiteral("author")).isEmpty() &&
                eventFrontMatter.values.contains(QStringLiteral("ts")) &&
                eventFrontMatter.values.contains(QStringLiteral("sig")) &&
                !eventFrontMatter.get(QStringLiteral("sig")).isEmpty();
            if (!validEvent) {
                recordStrictError(
                    QStringLiteral("A pull event file is invalid."));
                if (strict)
                    continue;
            }
            PullEvent event = eventFromFrontMatter(eventFrontMatter);
            if (strict)
                event.ts = eventTimestamp;
            pr.events.append(event);
        }
        pulls.append(pr);
    }
    std::sort(pulls.begin(), pulls.end(),
              [](const PullRequest &a, const PullRequest &b) { return a.number < b.number; });
    return pulls;
}
