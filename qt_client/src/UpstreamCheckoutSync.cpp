#include "UpstreamCheckoutSync.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>

namespace forkmesh::upstream {

namespace {

// Plumbing calls are quick; the fetch itself negotiates and downloads a pack
// over the relay and gets the long budget.
constexpr int kPlumbingTimeoutMs = 30 * 1000;
constexpr int kFetchTimeoutMs = 10 * 60 * 1000;

bool runGit(const QString &dir, const QStringList &args, QString *output,
            int timeoutMs = kPlumbingTimeoutMs)
{
    QProcess process;
    process.setWorkingDirectory(dir);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QStringLiteral("git"), args);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(5000);
        return false;
    }
    if (output)
        *output = QString::fromUtf8(process.readAllStandardOutput());
    return process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

// tip is already part of target's history (moving tip -> target is a pure
// fast-forward that abandons nothing).
bool containedIn(const QString &repo, const QString &tip,
                 const QString &target)
{
    return runGit(repo,
                  {QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"),
                   tip, target},
                  nullptr);
}

// Tracked-file cleanliness only: untracked files neither block a
// fast-forward nor are touched by reset --hard.
bool worktreeClean(const QString &worktreePath)
{
    QString status;
    if (!runGit(worktreePath,
                {QStringLiteral("status"), QStringLiteral("--porcelain"),
                 QStringLiteral("--untracked-files=no")},
                &status))
        return false;
    return status.trimmed().isEmpty();
}

// branch -> worktree directory, for every branch checked out anywhere (the
// primary checkout plus linked worktrees such as the pulls metadata one).
QHash<QString, QString> checkedOutBranches(const QString &repo)
{
    QHash<QString, QString> result;
    QString listing;
    if (!runGit(repo,
                {QStringLiteral("worktree"), QStringLiteral("list"),
                 QStringLiteral("--porcelain")},
                &listing))
        return result;
    QString currentPath;
    const QStringList lines = listing.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("worktree ")))
            currentPath = line.mid(9).trimmed();
        else if (line.startsWith(QLatin1String("branch refs/heads/")) &&
                 !currentPath.isEmpty())
            result.insert(line.mid(18).trimmed(), currentPath);
    }
    return result;
}

// "<name> <oid>" pairs for every ref under prefix.
QHash<QString, QString> refTips(const QString &repo, const QString &prefix,
                                int strip)
{
    QHash<QString, QString> result;
    QString listing;
    if (!runGit(repo,
                {QStringLiteral("for-each-ref"), prefix,
                 QStringLiteral("--format=%(refname:lstrip=") +
                     QString::number(strip) + QStringLiteral(") %(objectname)")},
                &listing))
        return result;
    const QStringList lines = listing.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int space = line.lastIndexOf(QLatin1Char(' '));
        if (space <= 0)
            continue;
        const QString name = line.left(space).trimmed();
        if (name.isEmpty() || name == QLatin1String("HEAD"))
            continue;
        result.insert(name, line.mid(space + 1).trimmed());
    }
    return result;
}

} // namespace

QString RefreshOutcome::summary() const
{
    if (!error.isEmpty())
        return QStringLiteral("upstream refresh failed: %1").arg(error);
    // Deliberately-kept diverged branches alone are steady state (an agent's
    // in-flight work), not news — stay quiet unless a ref actually moved.
    if (!headFastForwarded && branchesUpdated == 0 && branchesPruned == 0)
        return QString();
    QStringList parts;
    if (headFastForwarded)
        parts << QStringLiteral("fast-forwarded the checkout");
    if (branchesUpdated > 0)
        parts << QStringLiteral("%1 branch(es) updated").arg(branchesUpdated);
    if (branchesPruned > 0)
        parts << QStringLiteral("%1 pruned").arg(branchesPruned);
    if (branchesKept > 0)
        parts << QStringLiteral("%1 diverged kept").arg(branchesKept);
    return QStringLiteral("upstream refresh: ") +
           parts.join(QStringLiteral(", "));
}

static RefreshOutcome refreshCore(const QString &checkoutPath,
                                  const QString &upstreamUrl,
                                  const QStringList &forcedBranches,
                                  bool repointOrigin, bool pruneGone)
{
    RefreshOutcome outcome;
    const QString path = checkoutPath.trimmed();
    const QString url = upstreamUrl.trimmed();
    if (path.isEmpty() || url.isEmpty() ||
        !QFileInfo(QDir(path).filePath(QStringLiteral(".git"))).exists()) {
        outcome.error = QStringLiteral("no managed checkout to refresh");
        return outcome;
    }

    const QString remoteNs = repointOrigin
                                 ? QStringLiteral("refs/remotes/origin")
                                 : QStringLiteral("refs/remotes/forkmesh-mesh");
    if (repointOrigin) {
        // Track the live relay route and the full branch namespace:
        // provisioned checkouts are single-branch clones whose refspec only
        // covered main.
        if (!runGit(path,
                    {QStringLiteral("remote"), QStringLiteral("set-url"),
                     QStringLiteral("origin"), url},
                    nullptr))
            runGit(path,
                   {QStringLiteral("remote"), QStringLiteral("add"),
                    QStringLiteral("origin"), url},
                   nullptr);
        runGit(path,
               {QStringLiteral("config"), QStringLiteral("remote.origin.fetch"),
                QStringLiteral("+refs/heads/*:refs/remotes/origin/*")},
               nullptr);
        if (!runGit(path,
                    {QStringLiteral("fetch"), QStringLiteral("--prune"),
                     QStringLiteral("--prune-tags"), QStringLiteral("--tags"),
                     QStringLiteral("--force"), QStringLiteral("--quiet"),
                     QStringLiteral("origin")},
                    nullptr, kFetchTimeoutMs)) {
            outcome.error = QStringLiteral("fetch from the relay failed");
            return outcome;
        }
    } else {
        // A personal working copy: never touch the user's remote
        // configuration. One-shot fetch by URL into a scratch remote-tracking
        // namespace; the forced refspec only ever moves that scratch view,
        // and the per-branch logic below still refuses every non-fast-forward
        // local update.
        if (!runGit(path,
                    {QStringLiteral("fetch"), QStringLiteral("--prune"),
                     QStringLiteral("--quiet"), url,
                     QStringLiteral("+refs/heads/*:") + remoteNs +
                         QStringLiteral("/*")},
                    nullptr, kFetchTimeoutMs)) {
            outcome.error = QStringLiteral("fetch from the mesh failed");
            return outcome;
        }
    }
    outcome.fetchOk = true;

    const QHash<QString, QString> upstream = refTips(path, remoteNs, 3);
    if (upstream.isEmpty()) {
        outcome.error = QStringLiteral("upstream advertised no branches");
        return outcome;
    }
    const QHash<QString, QString> local =
        refTips(path, QStringLiteral("refs/heads"), 2);
    const QHash<QString, QString> checkedOut = checkedOutBranches(path);
    const QSet<QString> forced(forcedBranches.begin(), forcedBranches.end());
    QString headBranch;
    runGit(path,
           {QStringLiteral("symbolic-ref"), QStringLiteral("--short"),
            QStringLiteral("HEAD")},
           &headBranch);
    headBranch = headBranch.trimmed();

    for (auto it = upstream.constBegin(); it != upstream.constEnd(); ++it) {
        const QString &branch = it.key();
        const QString &target = it.value();
        const QString have = local.value(branch);
        if (have == target)
            continue;
        const QString worktree = checkedOut.value(branch);
        const bool fastForward =
            !have.isEmpty() && containedIn(path, have, target);
        if (!worktree.isEmpty()) {
            // The branch is checked out somewhere; move it through its own
            // worktree, and only when that worktree carries no local edits.
            if (!worktreeClean(worktree))
                continue;
            bool moved = false;
            if (fastForward) {
                moved = runGit(worktree,
                               {QStringLiteral("merge"),
                                QStringLiteral("--ff-only"), target},
                               nullptr);
            } else if (forced.contains(branch)) {
                moved = runGit(worktree,
                               {QStringLiteral("reset"), QStringLiteral("--hard"),
                                target},
                               nullptr);
            }
            if (moved) {
                ++outcome.branchesUpdated;
                if (branch == headBranch)
                    outcome.headFastForwarded = true;
            } else if (!fastForward && !forced.contains(branch)) {
                ++outcome.branchesKept;
            }
            continue;
        }
        if (have.isEmpty() || fastForward || forced.contains(branch)) {
            if (runGit(path,
                       {QStringLiteral("branch"), QStringLiteral("--force"),
                        branch, target},
                       nullptr))
                ++outcome.branchesUpdated;
        } else {
            ++outcome.branchesKept; // local-only commits: never clobbered
        }
    }

    if (pruneGone) {
        const QString upstreamMain = upstream.value(QStringLiteral("main"));
        for (auto it = local.constBegin(); it != local.constEnd(); ++it) {
            const QString &branch = it.key();
            if (upstream.contains(branch) || checkedOut.contains(branch))
                continue;
            // Gone upstream. Drop it only when everything it points at already
            // lives in upstream main — stale claim markers, merged work.
            if (!upstreamMain.isEmpty() &&
                containedIn(path, it.value(), upstreamMain)) {
                if (runGit(path,
                           {QStringLiteral("branch"), QStringLiteral("-D"),
                            branch},
                           nullptr))
                    ++outcome.branchesPruned;
            } else {
                ++outcome.branchesKept;
            }
        }
    }

    return outcome;
}

RefreshOutcome refreshManagedCheckoutFromUpstream(
    const QString &checkoutPath, const QString &upstreamUrl,
    const QStringList &forcedBranches)
{
    return refreshCore(checkoutPath, upstreamUrl, forcedBranches,
                       /*repointOrigin=*/true, /*pruneGone=*/true);
}

RefreshOutcome convergeSourceCheckoutFromMesh(const QString &checkoutPath,
                                              const QString &meshUrl)
{
    // The source of truth converging on submissions an online mirror merged
    // while this node was away. Strictly additive: no remote reconfiguration,
    // no branch pruning, no forced branches — every local ref moves only by
    // fast-forward through a clean worktree, so local-only work always wins
    // and simply supersedes the mesh on the next publish.
    return refreshCore(checkoutPath, meshUrl, /*forcedBranches=*/{},
                       /*repointOrigin=*/false, /*pruneGone=*/false);
}

} // namespace forkmesh::upstream
