#include "MirrorGatewayHealth.h"
#include "MirrorPushOutcome.h"

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
// mirror had failed — while main had in fact reached the whole fleet.
//
// Only the rejections that converge on their own are tolerated. A hook denial,
// a clobbered tag, or a gateway that never answered stay hard failures.

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

// The rule pushToSshMirrorRemotes applies to a finished push. Mirrored here so
// the tests exercise the decision the fan-out actually makes, not just the
// classifier in isolation: the exit code alone is meaningless without the
// per-ref status, and the status alone cannot see an unreachable gateway.
bool pushFailed(const MirrorPushOutcome &outcome, int exitCode)
{
    return exitCode != 0 && !outcome.onlyHeldBack;
}

// The gateways pushToSshMirrorRemotes would actually spawn a `git push` for on
// one pass. Mirrored here for the same reason as pushFailed: the rotation rule
// is a property of the fan-out, not of the health tracker alone — a release
// rollout deliberately ignores every cooldown.
QStringList rotationTargets(const MirrorGatewayHealth &health,
                            const QStringList &hosts, qint64 nowMs,
                            bool userInitiated = false)
{
    QStringList targets;
    for (const QString &host : hosts) {
        if (userInitiated || health.readyToPush(host, nowMs))
            targets.append(host);
    }
    return targets;
}

// The reported outage, verbatim: mirror13 was decommissioned, its remote stayed
// configured, and every five-second safety pass paid a full TCP connect timeout
// to it and logged the same red line.
const QString kRetiredGatewayStderr = QStringLiteral(
    "ssh: connect to host 149.28.230.243 port 22: Connection timed out\r\n"
    "fatal: Could not read from remote repository.\n"
    "Please make sure you have the correct access rights\n"
    "and the repository exists.");

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
    const MirrorPushOutcome outcome = classifyMirrorPush(output);
    check(!pushFailed(outcome, 1),
          "a divergent branch alone is not a push failure");
    check(outcome.onlyHeldBack, "the only rejection was rewind protection");
    check(outcome.updated == QStringList{QStringLiteral("main")},
          "main still advanced on the gateway");
    check(outcome.heldBack == QStringList{QStringLiteral("pulls")},
          "the held-back ref is named by its short branch name");
}

// "fetch first" is the same condition seen from a checkout with no
// remote-tracking ref for the gateway — the mirror-node fan-out's usual case.
void testFetchFirstAndStaleInfoAreHeldBack()
{
    const QString output = QStringLiteral(
        "To ssh://gateway/repo.git\n"
        "*\trefs/heads/topic:refs/heads/topic\t[new branch]\n"
        "!\trefs/heads/pulls:refs/heads/pulls\t[rejected] (fetch first)\n"
        "!\trefs/heads/wip:refs/heads/wip\t[rejected] (stale info)\n"
        "Done\n");
    const MirrorPushOutcome outcome = classifyMirrorPush(output);
    check(!pushFailed(outcome, 1), "both rejections resolve themselves");
    check(outcome.updated == QStringList{QStringLiteral("topic")},
          "the new branch was created");
    check(outcome.heldBack.size() == 2, "two refs were left alone");
}

// A pre-receive hook refusing the push is a genuine failure: nothing will
// converge on its own, so it must stay loud.
void testHookDenialStaysAFailure()
{
    const QString output = QStringLiteral(
        "To ssh://gateway/repo.git\n"
        " \trefs/heads/main:refs/heads/main\t349f2db..b0f0298\n"
        "!\trefs/heads/topic:refs/heads/topic\t[remote rejected] "
        "(pre-receive hook declined)\n"
        "Done\n");
    const MirrorPushOutcome outcome = classifyMirrorPush(output);
    check(pushFailed(outcome, 1), "a hook denial is a real failure");
    check(!outcome.onlyHeldBack, "a hook denial is not rewind protection");
    check(outcome.heldBack.isEmpty(), "nothing was merely held back");
}

// A tag the gateway already carries at a different commit will never converge
// on its own — unlike a branch, nothing here fast-forwards later.
void testClobberedTagStaysAFailure()
{
    const QString output = QStringLiteral(
        "To ssh://gateway/repo.git\n"
        "!\trefs/tags/v1.0:refs/tags/v1.0\t[rejected] (already exists)\n"
        "Done\n");
    const MirrorPushOutcome outcome = classifyMirrorPush(output);
    check(pushFailed(outcome, 1), "a clobbered tag is a real failure");
    check(outcome.heldBack.isEmpty(), "a tag clash is not rewind protection");
}

// An unreachable gateway or a refused key never reaches ref negotiation: no
// status lines at all, so the failure must stay loud.
void testUnreachableGatewayIsAFailure()
{
    const MirrorPushOutcome outcome = classifyMirrorPush(QString());
    check(pushFailed(outcome, 128),
          "no ref status with a non-zero exit is a failure");
    check(!outcome.onlyHeldBack, "nothing was held back; nothing was reached");
    check(outcome.updated.isEmpty(), "nothing moved");
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
    const MirrorPushOutcome outcome = classifyMirrorPush(output);
    check(!pushFailed(outcome, 0), "an up-to-date push is not a failure");
    check(outcome.updated.isEmpty(), "nothing moved, so nothing is logged");
    check(outcome.heldBack.isEmpty(), "up to date is not rewind protection");
}

// "To <url>" and "Done" carry no ref status and must never be mistaken for
// one — an early line scan counted "To …" as an update.
void testFramingLinesAreIgnored()
{
    const QString output = QStringLiteral(
        "To ssh://gateway/repo.git\n"
        "Done\n");
    const MirrorPushOutcome outcome = classifyMirrorPush(output);
    check(outcome.updated.isEmpty(), "framing lines are not ref updates");
    check(!pushFailed(outcome, 0),
          "a clean exit with no refs is not a failure");
}

// A retired node still listed as a push remote must not define the fan-out's
// cadence: it rotates out, the rest of the fleet keeps syncing on the pass it
// always used.
void testRetiredGatewayRotatesOutOfTheFanOut()
{
    const QStringList fleet = {QStringLiteral("forkmesh-mirror10-sync"),
                               QStringLiteral("forkmesh-mirror13-sync"),
                               QStringLiteral("forkmesh-mirror14-sync")};
    check(mirrorGatewayUnreachable(kRetiredGatewayStderr),
          "a connect timeout is the gateway never answering");

    MirrorGatewayHealth health;
    const qint64 start = 1'700'000'000'000LL;
    check(rotationTargets(health, fleet, start) == fleet,
          "every gateway is tried before anything has failed");

    health.noteUnreachable(QStringLiteral("forkmesh-mirror13-sync"), start);
    const QStringList next = rotationTargets(health, fleet, start + 5'000);
    check(next == QStringList{QStringLiteral("forkmesh-mirror10-sync"),
                              QStringLiteral("forkmesh-mirror14-sync")},
          "the retired gateway sits out the next pass");
    check(rotationTargets(health, fleet, start + 5'000,
                          /*userInitiated=*/true) == fleet,
          "a release rollout still tries every configured gateway");
}

// The cooldown grows so a dead node is probed less and less, but stays bounded
// so a gateway that comes back is picked up within the quarter hour.
void testUnreachableCooldownGrowsAndCaps()
{
    MirrorGatewayHealth health;
    const QString host = QStringLiteral("forkmesh-mirror13-sync");
    qint64 now = 1'700'000'000'000LL;
    const MirrorGatewayHealth::Notice first =
        health.noteUnreachable(host, now);
    check(first.cooldownMs >= MirrorGatewayHealth::kBaseCooldownMs,
          "the first failure holds the gateway off for at least a pass");

    qint64 previous = first.cooldownMs;
    bool grew = false;
    for (int i = 0; i < 3; ++i) {
        now += previous + 1;
        const MirrorGatewayHealth::Notice notice =
            health.noteUnreachable(host, now);
        grew = grew || notice.cooldownMs > previous;
        previous = notice.cooldownMs;
    }
    check(grew, "repeated silence widens the retry interval");

    for (int i = 0; i < 10; ++i) {
        now += previous + 1;
        previous = health.noteUnreachable(host, now).cooldownMs;
    }
    // NetworkBackoff adds up to an eighth of the delay as jitter so separately
    // failing gateways do not all retry on the same tick.
    check(previous <= MirrorGatewayHealth::kMaxCooldownMs * 9 / 8,
          "the cooldown never grows past the ceiling");
}

// Rotating out is not giving up: the gateway is tried again when its cooldown
// expires, and one success puts it straight back on the normal pass.
void testGatewayReturnsToRotationAfterItsCooldown()
{
    MirrorGatewayHealth health;
    const QString host = QStringLiteral("forkmesh-mirror13-sync");
    const qint64 start = 1'700'000'000'000LL;
    const qint64 cooldown = health.noteUnreachable(host, start).cooldownMs;
    check(!health.readyToPush(host, start + cooldown / 2),
          "the gateway is skipped while it is cooling down");
    check(health.cooldownRemainingMs(host, start + cooldown / 2) > 0,
          "the caller can say how long it is held off");
    check(health.readyToPush(host, start + cooldown + 1),
          "it is retried once the cooldown expires");

    check(health.noteReachable(host),
          "the recovery is worth reporting after an outage was reported");
    check(health.readyToPush(host, start + cooldown + 2),
          "one success restores the normal cadence");
    check(!health.noteReachable(host),
          "a gateway that never failed reports no recovery");
}

// A gateway that answers and then refuses is a different failure: someone has
// to fix it, so it must never be quietly rotated out.
void testAnsweredRefusalsAreNotRotatedOut()
{
    check(!mirrorGatewayUnreachable(QStringLiteral(
              "git@gateway: Permission denied (publickey).\n"
              "fatal: Could not read from remote repository.")),
          "a refused key answered; it is not an unreachable gateway");
    check(!mirrorGatewayUnreachable(QStringLiteral(
              "remote: pre-receive hook declined\n"
              "! [remote rejected] main -> main (pre-receive hook declined)")),
          "a hook denial answered; it is not an unreachable gateway");
    check(!mirrorGatewayUnreachable(QString()),
          "no stderr at all is not evidence of an unreachable gateway");
    check(mirrorGatewayUnreachable(QStringLiteral(
              "ssh: Could not resolve hostname forkmesh-mirror13-sync: "
              "Name or service not known")),
          "a hostname that no longer resolves is a removed node");
}

// The log line is the point of the whole exercise: say it when it happens, then
// once an hour, and after a full day say what to do about it — once.
void testUnreachableIsAnnouncedOnceAnHourThenNamedRetired()
{
    MirrorGatewayHealth health;
    const QString host = QStringLiteral("forkmesh-mirror13-sync");
    const qint64 start = 1'700'000'000'000LL;
    check(health.noteUnreachable(host, start).announce,
          "the first failure is reported");
    check(!health.noteUnreachable(host, start + 60'000).announce,
          "the next pass does not repeat it");
    const MirrorGatewayHealth::Notice hourly = health.noteUnreachable(
        host, start + MirrorGatewayHealth::kRepeatNoticeMs + 1);
    check(hourly.announce, "a still-dead gateway is repeated once an hour");
    check(!hourly.retired, "an hour of silence is not yet a retired node");

    const MirrorGatewayHealth::Notice day = health.noteUnreachable(
        host, start + MirrorGatewayHealth::kRetiredAfterMs + 1);
    check(day.retired && day.downForMs >= MirrorGatewayHealth::kRetiredAfterMs,
          "a day of silence suggests dropping the remote");
    check(!health
               .noteUnreachable(
                   host, start + MirrorGatewayHealth::kRetiredAfterMs +
                             MirrorGatewayHealth::kRepeatNoticeMs + 2)
               .retired,
          "the removal hint is given once per outage, not every hour");

    health.noteReachable(host);
    const MirrorGatewayHealth::Notice fresh = health.noteUnreachable(
        host, start + MirrorGatewayHealth::kRetiredAfterMs + 3);
    check(fresh.announce && !fresh.retired,
          "a new outage starts its own streak");
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

    const MirrorPushOutcome outcome =
        classifyMirrorPush(QString::fromUtf8(out));
    check(!pushFailed(outcome, exitCode),
          "a gateway that is merely ahead on pulls is not a failed mirror");
    check(outcome.updated.contains(QStringLiteral("main")),
          "main advanced despite the rejected branch");
    check(outcome.heldBack == QStringList{QStringLiteral("pulls")},
          "only pulls was left alone");

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
    testFetchFirstAndStaleInfoAreHeldBack();
    testHookDenialStaysAFailure();
    testClobberedTagStaysAFailure();
    testUnreachableGatewayIsAFailure();
    testUpToDatePushIsSilent();
    testFramingLinesAreIgnored();
    testRetiredGatewayRotatesOutOfTheFanOut();
    testUnreachableCooldownGrowsAndCaps();
    testGatewayReturnsToRotationAfterItsCooldown();
    testAnsweredRefusalsAreNotRotatedOut();
    testUnreachableIsAnnouncedOnceAnHourThenNamedRetired();
    testAgainstRealGit();
    if (failures == 0)
        std::fprintf(stderr, "ssh mirror push tests passed\n");
    return failures == 0 ? 0 : 1;
}
