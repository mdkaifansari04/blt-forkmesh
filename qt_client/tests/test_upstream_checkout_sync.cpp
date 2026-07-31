#include "UpstreamCheckoutSync.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <cstdio>

// The headless-fleet regression this suite pins down: a node that seals its
// public mirror from the installer-provisioned agent checkout must keep that
// checkout tracking the upstream relay clone. Before UpstreamCheckoutSync,
// the checkout froze at its install-time snapshot (the mirror6/7/8 outage of
// 2026-07-30): new upstream commits, branches, and the pulls metadata branch
// never arrived, and the node served stale data forever while claiming to be
// a healthy mirror.

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

bool git(const QString &path, const QStringList &arguments,
         QByteArray *output = nullptr)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_AUTHOR_NAME"),
                       QStringLiteral("Upstream Sync Test"));
    environment.insert(QStringLiteral("GIT_AUTHOR_EMAIL"),
                       QStringLiteral("upstream-sync@example.invalid"));
    environment.insert(QStringLiteral("GIT_COMMITTER_NAME"),
                       QStringLiteral("Upstream Sync Test"));
    environment.insert(QStringLiteral("GIT_COMMITTER_EMAIL"),
                       QStringLiteral("upstream-sync@example.invalid"));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral("git"),
                  QStringList{QStringLiteral("-C"), path} + arguments);
    if (!process.waitForFinished(30000) || process.exitCode() != 0)
        return false;
    if (output)
        *output = process.readAllStandardOutput();
    return true;
}

QString revParse(const QString &path, const QString &ref)
{
    QByteArray out;
    if (!git(path, {QStringLiteral("rev-parse"), ref}, &out))
        return QString();
    return QString::fromUtf8(out).trimmed();
}

bool commitFile(const QString &repo, const QString &name,
                const QByteArray &bytes, const QString &message)
{
    QFile file(QDir(repo).filePath(name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(bytes) != bytes.size())
        return false;
    file.close();
    return git(repo, {QStringLiteral("add"), name}) &&
           git(repo, {QStringLiteral("commit"), QStringLiteral("-q"),
                      QStringLiteral("-m"), message});
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::fprintf(stderr, "FAIL: no temporary directory\n");
        return 1;
    }
    const QDir root(temp.path());

    // Upstream: the relay-served source of truth, with main, a side branch,
    // the pulls metadata branch, and a branch that will later disappear.
    const QString upstream = root.filePath(QStringLiteral("upstream"));
    QDir().mkpath(upstream);
    check(git(upstream, {QStringLiteral("init"), QStringLiteral("-q"),
                         QStringLiteral("-b"), QStringLiteral("main")}),
          "init upstream");
    check(commitFile(upstream, QStringLiteral("README.md"),
                     QByteArrayLiteral("hello\n"), QStringLiteral("first")),
          "seed upstream");
    check(git(upstream, {QStringLiteral("branch"),
                         QStringLiteral("agent/adhoc-1-stale-claim")}),
          "seed stale branch");
    check(git(upstream, {QStringLiteral("checkout"), QStringLiteral("-q"),
                         QStringLiteral("-b"), QStringLiteral("feature/live")}),
          "branch feature");
    check(commitFile(upstream, QStringLiteral("feature.txt"),
                     QByteArrayLiteral("f1\n"), QStringLiteral("feature 1")),
          "commit feature");
    check(git(upstream, {QStringLiteral("checkout"), QStringLiteral("-q"),
                         QStringLiteral("--orphan"),
                         QStringLiteral("forkmesh/pulls")}),
          "orphan pulls");
    check(git(upstream, {QStringLiteral("rm"), QStringLiteral("-rfq"),
                         QStringLiteral("--ignore-unmatch"),
                         QStringLiteral(".")}),
          "clear pulls tree");
    check(commitFile(upstream, QStringLiteral("pulls.json"),
                     QByteArrayLiteral("{\"pulls\":[]}\n"),
                     QStringLiteral("pull metadata")),
          "commit pulls metadata");
    check(git(upstream, {QStringLiteral("checkout"), QStringLiteral("-q"),
                         QStringLiteral("main")}),
          "back to main");

    // Node checkout: the installer's single-branch clone, exactly like
    // launch_root_headless_service seeds it.
    const QString checkout = root.filePath(QStringLiteral("checkout"));
    check(git(temp.path(), {QStringLiteral("clone"), QStringLiteral("-q"),
                            QStringLiteral("--branch"), QStringLiteral("main"),
                            QStringLiteral("--single-branch"), upstream,
                            checkout}),
          "single-branch clone");
    // Local junk pulls branch (the pre-fix nodes grew one) plus a stale agent
    // claim branch pointing at the clone-time tip, and one branch with real
    // local-only work that must survive every refresh.
    check(git(checkout, {QStringLiteral("branch"),
                         QStringLiteral("forkmesh/pulls")}),
          "local junk pulls branch");
    check(git(checkout, {QStringLiteral("branch"),
                         QStringLiteral("agent/adhoc-1-stale-claim")}),
          "local stale claim");
    check(git(checkout, {QStringLiteral("checkout"), QStringLiteral("-q"),
                         QStringLiteral("-b"),
                         QStringLiteral("agent/adhoc-9-wip")}),
          "branch wip");
    check(commitFile(checkout, QStringLiteral("wip.txt"),
                     QByteArrayLiteral("wip\n"), QStringLiteral("agent wip")),
          "commit wip");
    check(git(checkout, {QStringLiteral("checkout"), QStringLiteral("-q"),
                         QStringLiteral("main")}),
          "back on main");
    // A linked worktree keeps the pulls branch checked out, mirroring the
    // app's pull-meta materialization (the case that broke a naive refresh).
    const QString pullsWorktree = root.filePath(QStringLiteral("pull-meta"));
    check(git(checkout, {QStringLiteral("worktree"), QStringLiteral("add"),
                         QStringLiteral("-q"), pullsWorktree,
                         QStringLiteral("forkmesh/pulls")}),
          "pulls worktree");

    // Upstream moves on: main advances, the stale claim branch is deleted.
    check(commitFile(upstream, QStringLiteral("README.md"),
                     QByteArrayLiteral("hello v2\n"), QStringLiteral("second")),
          "advance upstream main");
    check(git(upstream, {QStringLiteral("branch"), QStringLiteral("-q"),
                         QStringLiteral("-D"),
                         QStringLiteral("agent/adhoc-1-stale-claim")}),
          "delete upstream stale claim");

    // --- The refresh under test -------------------------------------------
    const auto outcome =
        forkmesh::upstream::refreshManagedCheckoutFromUpstream(checkout,
                                                               upstream);
    check(outcome.error.isEmpty(), "refresh reports no error");
    check(outcome.fetchOk, "refresh fetched");
    check(outcome.headFastForwarded, "checked-out main fast-forwarded");
    check(!outcome.summary().isEmpty(), "changes produce a summary");

    check(revParse(checkout, QStringLiteral("main")) ==
              revParse(upstream, QStringLiteral("main")),
          "main matches upstream");
    check(revParse(checkout, QStringLiteral("feature/live")) ==
              revParse(upstream, QStringLiteral("feature/live")),
          "new upstream branch materialized locally");
    check(revParse(checkout, QStringLiteral("forkmesh/pulls")) ==
              revParse(upstream, QStringLiteral("forkmesh/pulls")),
          "pulls metadata force-tracked across divergence");
    check(revParse(checkout,
                   QStringLiteral("agent/adhoc-1-stale-claim")).isEmpty(),
          "stale merged claim branch pruned");
    check(!revParse(checkout, QStringLiteral("agent/adhoc-9-wip")).isEmpty(),
          "diverged agent work preserved");
    QByteArray wipList;
    check(git(checkout, {QStringLiteral("log"), QStringLiteral("-1"),
                         QStringLiteral("--format=%s"),
                         QStringLiteral("agent/adhoc-9-wip")},
              &wipList) &&
              QString::fromUtf8(wipList).trimmed() ==
                  QStringLiteral("agent wip"),
          "agent wip tip untouched");

    // A second refresh with nothing new is a quiet no-op.
    const auto again =
        forkmesh::upstream::refreshManagedCheckoutFromUpstream(checkout,
                                                               upstream);
    check(again.error.isEmpty(), "no-op refresh reports no error");
    check(again.summary().isEmpty(), "no-op refresh has an empty summary");

    // A dirty primary checkout must never be fast-forwarded under an agent.
    check(commitFile(upstream, QStringLiteral("README.md"),
                     QByteArrayLiteral("hello v3\n"), QStringLiteral("third")),
          "advance upstream again");
    QFile dirty(QDir(checkout).filePath(QStringLiteral("README.md")));
    check(dirty.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
              dirty.write(QByteArrayLiteral("local edit\n")) > 0,
          "dirty the checkout");
    dirty.close();
    const auto guarded =
        forkmesh::upstream::refreshManagedCheckoutFromUpstream(checkout,
                                                               upstream);
    check(guarded.error.isEmpty(), "guarded refresh reports no error");
    check(!guarded.headFastForwarded, "dirty checkout left alone");
    check(revParse(checkout, QStringLiteral("main")) !=
              revParse(upstream, QStringLiteral("main")),
          "dirty main did not move");

    // An unreachable upstream fails closed without touching anything.
    const auto offline = forkmesh::upstream::refreshManagedCheckoutFromUpstream(
        checkout, root.filePath(QStringLiteral("missing")));
    check(!offline.error.isEmpty(), "unreachable upstream reports an error");
    check(!offline.summary().isEmpty(), "failure summary names the problem");

    // A path that is not a checkout is rejected outright.
    const auto notRepo = forkmesh::upstream::refreshManagedCheckoutFromUpstream(
        root.filePath(QStringLiteral("nowhere")), upstream);
    check(!notRepo.error.isEmpty(), "missing checkout rejected");

    if (failures == 0)
        std::printf("test_upstream_checkout_sync: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
