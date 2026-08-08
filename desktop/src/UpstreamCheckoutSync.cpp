#include "UpstreamCheckoutSync.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>

namespace forkmesh::upstream {

namespace {

constexpr int kPlumbingTimeoutMs = 30 * 1000;
constexpr int kFetchTimeoutMs = 10 * 60 * 1000;
const QString kGeneratedStatsPath =
    QStringLiteral(".forkmesh/stats/repository.json");

bool runGit(const QString &dir, const QStringList &args, QString *output,
            int timeoutMs = kPlumbingTimeoutMs)
{
    QProcess process;
    process.setWorkingDirectory(dir);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    QStringList boundedArgs{
        QStringLiteral("-c"), QStringLiteral("pack.threads=1"),
        QStringLiteral("-c"), QStringLiteral("core.deltaBaseCacheLimit=16m"),
        QStringLiteral("-c"), QStringLiteral("pack.deltaCacheSize=16m"),
        QStringLiteral("-c"), QStringLiteral("pack.windowMemory=16m")};
    boundedArgs.append(args);
    process.start(QStringLiteral("git"), boundedArgs);
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

bool containedIn(const QString &repo, const QString &tip,
                 const QString &target)
{
    return runGit(repo,
                  {QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"),
                   tip, target},
                  nullptr);
}

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

bool onlyGeneratedStatsDirty(const QString &worktreePath)
{
    QString status;
    if (!runGit(worktreePath,
                {QStringLiteral("status"), QStringLiteral("--porcelain"),
                 QStringLiteral("--untracked-files=no")},
                &status))
        return false;
    const QStringList lines =
        status.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty())
        return false;
    for (const QString &line : lines) {
        if (line.size() < 4 || line.mid(3) != kGeneratedStatsPath)
            return false;
    }
    return true;
}

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

bool configureForkCheckoutRemote(const QString &checkoutPath,
                                 const QString &sourceUrl,
                                 const QString &forkMirrorPath,
                                 QString *error)
{
    if (error)
        error->clear();
    const QString path = checkoutPath.trimmed();
    const QString source = sourceUrl.trimmed();
    const QString mirror = forkMirrorPath.trimmed();
    if (path.isEmpty() || source.isEmpty() || mirror.isEmpty() ||
        !QFileInfo(QDir(path).filePath(QStringLiteral(".git"))).exists()) {
        if (error)
            *error = QStringLiteral(
                "the fork checkout or one of its remotes is missing");
        return false;
    }
    if (!runGit(path,
                {QStringLiteral("remote"), QStringLiteral("set-url"),
                 QStringLiteral("origin"), source},
                nullptr)) {
        if (error)
            *error = QStringLiteral("could not set origin's fetch URL");
        return false;
    }
    if (!runGit(path,
                {QStringLiteral("remote"), QStringLiteral("set-url"),
                 QStringLiteral("--push"), QStringLiteral("origin"), mirror},
                nullptr)) {
        if (error)
            *error = QStringLiteral("could not set origin's push URL");
        return false;
    }
    return true;
}

QString RefreshOutcome::summary() const
{
    if (!error.isEmpty())
        return QStringLiteral("upstream refresh failed: %1").arg(error);
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
                                  bool repointOrigin, bool pruneGone,
                                  bool recoverGeneratedStats)
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
            bool moved = false;
            if (fastForward) {
                moved = runGit(worktree,
                               {QStringLiteral("merge"),
                                QStringLiteral("--ff-only"), target},
                               nullptr);
                if (!moved && recoverGeneratedStats &&
                    onlyGeneratedStatsDirty(worktree) &&
                    runGit(worktree,
                           {QStringLiteral("restore"),
                            QStringLiteral("--source=HEAD"),
                            QStringLiteral("--staged"),
                            QStringLiteral("--worktree"),
                            QStringLiteral("--"), kGeneratedStatsPath},
                           nullptr)) {
                    moved = runGit(worktree,
                                   {QStringLiteral("merge"),
                                    QStringLiteral("--ff-only"), target},
                                   nullptr);
                }
            } else if (forced.contains(branch) && worktreeClean(worktree)) {
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
                       /*repointOrigin=*/true, /*pruneGone=*/true,
                       /*recoverGeneratedStats=*/true);
}

RefreshOutcome convergeSourceCheckoutFromMesh(const QString &checkoutPath,
                                              const QString &meshUrl)
{
    return refreshCore(checkoutPath, meshUrl, /*forcedBranches=*/{},
                       /*repointOrigin=*/false, /*pruneGone=*/false,
                       /*recoverGeneratedStats=*/false);
}

} // namespace forkmesh::upstream
