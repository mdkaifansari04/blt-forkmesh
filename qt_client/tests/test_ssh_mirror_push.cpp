#include "SshMirrorPushOutcome.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <cstdio>

// The mirror-fleet regression this suite pins down: the unattended SSH fan-out
// pushes refs/heads/* and refs/tags/* in one non-atomic, non-forcing push, so
// a branch the gateway advanced first (forkmesh/pulls, a live agent branch) is
// rejected while every other ref lands. git still exits non-zero for that
// partial rejection. Judging the push by its exit code alone reported the
// healthy fan-out as "SSH mirror push of forkmesh/forkmesh to
// forkmesh-mirror10-sync failed: … a pushed branch tip is behind its remote
// counterpart", every five-second safety pass, and told a release push that a
// mirror had failed — while main had in fact reached the whole fleet. Only a
// push that reports no ref status at all (unreachable gateway, refused key) or
// a rejection that is not divergence (a hook denial) is a real failure.

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
         QByteArray *output = nullptr, int *exitCode = nullptr)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_AUTHOR_NAME"),
                       QStringLiteral("Mirror Push Test"));
    environment.insert(QStringLiteral("GIT_AUTHOR_EMAIL"),
                       QStringLiteral("mirror-push@example.invalid"));
    environment.insert(QStringLiteral("GIT_COMMITTER_NAME"),
                       QStringLiteral("Mirror Push Test"));
    environment.insert(QStringLiteral("GIT_COMMITTER_EMAIL"),
                       QStringLiteral("mirror-push@example.invalid"));
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"),
                       QStringLiteral("0"));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral("git"),
                  QStringList{QStringLiteral("-C"), path} + arguments);
    if (!process.waitForFinished(30000))
        return false;
    if (output)
        *output = process.readAllStandardOutput();
    if (!exitCode)
        return process.exitCode() == 0;
    // A caller that asks for the exit code is inspecting a push that is
    // *expected* to fail; running to completion is all it needs from here.
    *exitCode = process.exitCode();
    return true;
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

// The exact shape git emits when the gateway is ahead on one branch: main
// advances, the divergent branch is rejected, exit status 1.
void testPartialRejectionIsNotAFailure()
{
    const QString output = QStringLiteral(
        "To ssh://gateway/srv/mirrors/forkmesh-forkmesh.git\n"
        " \trefs/heads/main:refs/heads/main\t349f2db..b0f0298\n"
        "!\trefs/heads/pulls:refs/heads/pulls\t[rejected] (non-fast-forward)\n"
        "Done\n");
    const SshMirrorPushOutcome outcome =
        SshMirrorPush::parsePorcelain(output, 1);
    check(!outcome.failed(), "a divergent branch alone is not a push failure");
    check(!outcome.fatal, "the gateway answered, so nothing is fatal");
    check(outcome.advancedRefs == 1, "main still advanced on the gateway");
    check(outcome.updated(), "an advanced ref counts as an update");
    check(outcome.divergedRefs == QStringList{QStringLiteral("refs/heads/pulls")},
          "the divergent ref is named by its remote-side name");
}

// "fetch first" is the same condition seen from a checkout with no
// remote-tracking ref for the gateway — the mirror-node fan-out's usual case.
void testFetchFirstAndStaleInfoAreDivergence()
{
    const QString output = QStringLiteral(
        "To ssh://gateway/repo.git\n"
        "*\trefs/heads/topic:refs/heads/topic\t[new branch]\n"
        "!\trefs/heads/pulls:refs/heads/pulls\t[rejected] (fetch first)\n"
        "!\trefs/heads/wip:refs/heads/wip\t[rejected] (stale info)\n"
        "!\trefs/tags/v1.0:refs/tags/v1.0\t[rejected] (already exists)\n"
        "Done\n");
    const SshMirrorPushOutcome outcome =
        SshMirrorPush::parsePorcelain(output, 1);
    check(!outcome.failed(), "every rejection here is benign divergence");
    check(outcome.advancedRefs == 1, "the new branch was created");
    check(outcome.divergedRefs.size() == 3, "three refs were left alone");
}

// A pre-receive hook refusing the push is a genuine failure: nothing will
// converge on its own, so it must stay loud.
void testHookDenialStaysAFailure()
{
    const QString output = QStringLiteral(
        "To ssh://gateway/repo.git\n"
        " \trefs/heads/main:refs/heads/main\t349f2db..b0f0298\n"
        "!\trefs/heads/main:refs/heads/main\t[remote rejected] "
        "(pre-receive hook declined)\n"
        "Done\n");
    const SshMirrorPushOutcome outcome =
        SshMirrorPush::parsePorcelain(output, 1);
    check(outcome.failed(), "a hook denial is a real failure");
    check(outcome.rejectedRefs.size() == 1, "the denied ref is reported");
    check(outcome.divergedRefs.isEmpty(), "a hook denial is not divergence");
}

// An unreachable gateway or a refused key never reaches ref negotiation: no
// status lines at all, so the failure must stay loud.
void testUnreachableGatewayIsFatal()
{
    const SshMirrorPushOutcome outcome =
        SshMirrorPush::parsePorcelain(QString(), 128);
    check(outcome.fatal, "no ref status with a non-zero exit is fatal");
    check(outcome.failed(), "a fatal push counts as a failed remote");
    check(!outcome.updated(), "nothing moved");
}

// A clean, fully up-to-date push must stay silent: this runs on the
// five-second safety pass and would otherwise spam the network log.
void testUpToDatePushIsSilent()
{
    const QString output = QStringLiteral(
        "To ssh://gateway/repo.git\n"
        "=\trefs/heads/main:refs/heads/main\t[up to date]\n"
        "=\trefs/tags/v1.0:refs/tags/v1.0\t[up to date]\n"
        "Done\n");
    const SshMirrorPushOutcome outcome =
        SshMirrorPush::parsePorcelain(output, 0);
    check(!outcome.failed(), "an up-to-date push is not a failure");
    check(!outcome.updated(), "nothing moved, so nothing is logged");
    check(outcome.divergedRefs.isEmpty(), "up to date is not divergence");
}

// "To <url>" and "Done" carry no tab-delimited ref status and must never be
// mistaken for one — the pre-fix line scan counted "To …" as an update.
void testFramingLinesAreIgnored()
{
    const QString output = QStringLiteral(
        "To ssh://gateway/repo.git\r\n"
        "Done\r\n");
    const SshMirrorPushOutcome outcome =
        SshMirrorPush::parsePorcelain(output, 0);
    check(outcome.advancedRefs == 0, "framing lines are not ref updates");
    check(!outcome.failed(), "a clean exit with no refs is not a failure");
}

void testRefSummaryTruncates()
{
    const QStringList many{QStringLiteral("a"), QStringLiteral("b"),
                           QStringLiteral("c"), QStringLiteral("d"),
                           QStringLiteral("e"), QStringLiteral("f")};
    check(SshMirrorPush::summariseRefs(many) ==
              QStringLiteral("a, b, c, d and 2 more"),
          "a long divergence list is truncated for the log");
    check(SshMirrorPush::summariseRefs({QStringLiteral("refs/heads/x")}) ==
              QStringLiteral("refs/heads/x"),
          "a short list is named in full");
    check(SshMirrorPush::summariseRefs({}).isEmpty(),
          "no divergence summarises to nothing");
}

// End to end against real git: reproduce the reported outage — the gateway
// advanced a branch this checkout has not consumed — and prove the fan-out's
// own push arguments still deliver main while the outcome reads as healthy.
void testAgainstRealGit()
{
    QTemporaryDir root;
    if (!root.isValid()) {
        check(false, "temporary directory");
        return;
    }
    const QDir dir(root.path());
    const QString gateway = dir.filePath(QStringLiteral("gateway.git"));
    const QString source = dir.filePath(QStringLiteral("source"));
    const QString peer = dir.filePath(QStringLiteral("peer"));

    if (!git(root.path(), {QStringLiteral("init"), QStringLiteral("-q"),
                           QStringLiteral("--bare"), gateway}) ||
        !git(root.path(), {QStringLiteral("init"), QStringLiteral("-q"),
                           QStringLiteral("-b"), QStringLiteral("main"),
                           source})) {
        check(false, "git init");
        return;
    }
    if (!commitFile(source, QStringLiteral("a.txt"), "a", QStringLiteral("one")) ||
        !git(source, {QStringLiteral("branch"), QStringLiteral("pulls")}) ||
        !git(source, {QStringLiteral("push"), QStringLiteral("-q"), gateway,
                      QStringLiteral("refs/heads/*:refs/heads/*")}) ||
        // The bare gateway is born pointing at whatever init.defaultBranch
        // says; aim HEAD at a branch that exists so the peer clone checks out.
        !git(gateway, {QStringLiteral("symbolic-ref"), QStringLiteral("HEAD"),
                       QStringLiteral("refs/heads/main")})) {
        check(false, "seed the gateway");
        return;
    }
    // Another node advances the collaboration branch on the gateway.
    if (!git(root.path(), {QStringLiteral("clone"), QStringLiteral("-q"),
                           gateway, peer}) ||
        !git(peer, {QStringLiteral("checkout"), QStringLiteral("-q"),
                    QStringLiteral("pulls")}) ||
        !commitFile(peer, QStringLiteral("b.txt"), "b", QStringLiteral("two")) ||
        !git(peer, {QStringLiteral("push"), QStringLiteral("-q"),
                    QStringLiteral("origin"), QStringLiteral("pulls")})) {
        check(false, "advance pulls on the gateway");
        return;
    }
    // Meanwhile this checkout commits to main and runs the fan-out push.
    if (!commitFile(source, QStringLiteral("c.txt"), "c",
                    QStringLiteral("three"))) {
        check(false, "commit on the source");
        return;
    }
    QByteArray out;
    int exitCode = 0;
    const bool ran =
        git(source,
            {QStringLiteral("push"), QStringLiteral("--porcelain"),
             QStringLiteral("--no-force"), QStringLiteral("--no-prune"),
             gateway, QStringLiteral("refs/heads/*:refs/heads/*"),
             QStringLiteral("refs/tags/*:refs/tags/*")},
            &out, &exitCode);
    check(ran, "the fan-out push ran");
    check(exitCode != 0, "git reports the partial rejection as a failure");

    const SshMirrorPushOutcome outcome =
        SshMirrorPush::parsePorcelain(QString::fromUtf8(out), exitCode);
    check(!outcome.failed(),
          "a gateway that is merely ahead on pulls is not a failed mirror");
    check(outcome.updated(), "main advanced despite the rejected branch");
    check(outcome.divergedRefs.size() == 1, "only pulls was left alone");

    // The point of tolerating the rejection: main really did reach the mirror.
    QByteArray sourceMain;
    QByteArray gatewayMain;
    git(source, {QStringLiteral("rev-parse"), QStringLiteral("main")},
        &sourceMain);
    git(gateway, {QStringLiteral("rev-parse"), QStringLiteral("main")},
        &gatewayMain);
    check(!sourceMain.trimmed().isEmpty() &&
              sourceMain.trimmed() == gatewayMain.trimmed(),
          "the gateway carries the new main commit");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    testPartialRejectionIsNotAFailure();
    testFetchFirstAndStaleInfoAreDivergence();
    testHookDenialStaysAFailure();
    testUnreachableGatewayIsFatal();
    testUpToDatePushIsSilent();
    testFramingLinesAreIgnored();
    testRefSummaryTruncates();
    testAgainstRealGit();
    if (failures == 0)
        std::fprintf(stderr, "ssh mirror push tests passed\n");
    return failures == 0 ? 0 : 1;
}
