#include "PullStore.h"

#include "ForkMeshIdentity.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace {

constexpr int kGitTimeoutMs = 15000;
// History rewrites (filter-branch) replay every commit, so they need far longer
// than an ordinary git invocation.
constexpr int kGitRewriteTimeoutMs = 120000;

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
            {"sig", sig},         {"patch", patch},   {"commits", commits}};
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
    pr.commits = obj.value("commits").toString();
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
        else if (line.startsWith(QLatin1String("+++")) ||
                 line.startsWith(QLatin1String("---"))) {
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
        if (deriveFromRefs(m_workTree, base, head, &dpatch, &dcommits) &&
            !dpatch.trimmed().isEmpty()) {
            pr.branchBacked = true;
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
    QString err;
    // A branch-backed PR's change lives in its head branch's real commits, so
    // merge it the robust, patch-free way: bring the head commit into reach (the
    // local ref, or fetched from the mirror) and `git merge` it into the checked-
    // out base. This replays the exact objects — full authorship, binary files,
    // renames — with none of a patch's context-drift or "corrupt binary patch"
    // fragility. Only a stored-patch PR (imported patch / cross-node submission),
    // or a branch-backed PR whose commits aren't reachable anywhere, falls back
    // to applying the diff below.
    const QString headSha =
        pr.branchBacked ? reachableHeadCommit(m_workTree, m_mirror, pr.head)
                        : QString();
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
    // Commit the status update. In the ref-merge path `git merge` already
    // committed the change, so this records only the pulls/ metadata ("pull #N:
    // merged"); in the patch path this same commit carries the applied files too
    // ("merge pull #N: <title>").
    if (!runGit(m_workTree, {"add", "-A"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    const QString commitMsg =
        refMerge ? QStringLiteral("pull #%1: merged").arg(number)
                 : QStringLiteral("merge pull #%1: %2").arg(number).arg(pr.title);
    if (!runGit(m_workTree, {"commit", "-m", commitMsg}, nullptr, &err)) {
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
    return !m_workTree.isEmpty() && !amStateDir(m_workTree).isEmpty();
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

bool PullStore::startPullAgentEdit(int number, QString *error)
{
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
    QString err;
    if (!runGit(m_workTree, {"add", "-A"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    QByteArray staged;
    runGit(m_workTree, {"diff", "--cached", "--name-only"}, &staged);
    if (staged.trimmed().isEmpty()) {
        // The agent changed nothing — tear the work branch down.
        abortConflictMerge();
        if (error)
            *error = QStringLiteral("No changes to commit.");
        return false;
    }
    if (!runGit(m_workTree, {"commit", "-m", commitMsg}, nullptr, &err)) {
        if (error)
            *error = "git commit failed: " + err;
        return false;
    }
    return finalizeOnPullBranch(number, commitMsg, error);
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
    pr.mergeBase.clear();
    pr.mergeHead.clear();
    if (!writePull(pr, error))
        return false;
    // writePull only (re)writes commits.mbox when it's non-empty; if every
    // authored commit collapsed away, drop the stale file so merge falls back to
    // the patch instead of replaying an out-of-date series.
    if (pr.commits.isEmpty())
        QFile::remove(pullDir(number) + "/commits.mbox");
    return commit(QStringLiteral("pull #%1: delete %2").arg(number).arg(relPath),
                  error);
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
    m_amBranch.clear();
    m_amBase.clear();
    m_amRestoreRef.clear();
    return commit(commitMsg, error);
}

bool PullStore::finishConflictMerge(int number, QString *error)
{
    if (!canWrite()) {
        if (error)
            *error = QStringLiteral("Merging needs a local working tree.");
        return false;
    }
    // Refuse to commit a tree that still carries conflict markers.
    for (const QString &rel : unmergedFiles(m_workTree)) {
        QFile f(m_workTree + "/" + rel);
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
    if (!runGit(m_workTree, {"add", "-A"}, nullptr, &err)) {
        if (error)
            *error = "git add failed: " + err;
        return false;
    }
    // Complete the replay only if an am session is actually open (a clean apply
    // in startConflictMerge already committed it).
    if (!amStateDir(m_workTree).isEmpty() &&
        !runGit(m_workTree, {"am", "--continue"}, nullptr, &err)) {
        if (error)
            *error = "Could not complete the merge: " + err;
        return false;
    }
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
    return commit(QStringLiteral("pull #%1: opened (from %2)")
                      .arg(pr.number)
                      .arg(pr.authorName.isEmpty() ? pr.author.left(8) : pr.authorName),
                  error);
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
    if (hasUnrelatedTrackedChanges(m_workTree, relPath, error))
        return false;

    // First commit a normal deletion so the work tree is clean for the history
    // rewrite below, which then prunes this commit along with the prior
    // pull-only commits (e.g. "pull #N: open" and its changes.patch).
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

    // Default path: the PR folder is gone at the tip, which is all most callers
    // want. Skip the expensive full-history rewrite unless explicitly requested.
    if (!rewriteHistory)
        return true;

    // Purge pulls/<number> from every commit so the diff text it carried
    // (changes.patch / commits.mbox) can no longer be found by searching
    // history. Without this the soft delete above only hides the files at the
    // tip; older commits still contain them.
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
        pr.branchBacked = fm.get("derive") == QLatin1String("branch");
        pr.mergeBase = fm.get("mergeBase");
        pr.mergeHead = fm.get("mergeHead");
        pr.description = fm.body;
        // Branch-backed PRs carry no committed diff — reconstruct it from the
        // base/head refs the mirror already syncs (the merge snapshot for a
        // merged one). Fall back to committed blobs for legacy/portable PRs.
        const bool derived =
            pr.branchBacked &&
            (!pr.mergeBase.isEmpty() && !pr.mergeHead.isEmpty()
                 ? deriveFromRefs(m_mirror, pr.mergeBase, pr.mergeHead, &pr.patch,
                                  &pr.commits)
                 : deriveFromRefs(m_mirror, pr.base, pr.head, &pr.patch,
                                  &pr.commits));
        if (!derived) {
            bool pok = false;
            pr.patch = QString::fromUtf8(
                showFromMirror("pulls/" + base + "/changes.patch", &pok));
            bool mok = false;
            const QByteArray mbox =
                showFromMirror("pulls/" + base + "/commits.mbox", &mok);
            if (mok)
                pr.commits = QString::fromUtf8(mbox);
        }
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
