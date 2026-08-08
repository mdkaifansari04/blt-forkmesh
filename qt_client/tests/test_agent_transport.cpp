// A CLI transport has to end, and has to say when a turn did not get through.
// Both were assumed rather than reported: a `claude` process that never started
// emitted no finished(), so the session that owned it stayed Running behind
// nothing, and a turn written into a closed pipe was reported to the caller as
// sent. Between them, pressing "add" on such a session queued a prompt nobody
// would ever read and restarted into the same silence (adhoc #1618).
#include "../src/ClaudeStreamSession.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition)
        qInfo("PASS: %s", what);
    else {
        qCritical("FAIL: %s", what);
        ++failures;
    }
}

template <typename Predicate> bool pump(Predicate predicate, int timeoutMs = 4000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return true;
}

// A working directory that is not there is the everyday version of this: agent
// worktrees live under /tmp and get swept, and the session that used one is
// picked back up days later. QProcess refuses to start at all, which raises
// errorOccurred and never finished — so this is the only ending the owner of the
// session will ever be told about.
void runLaunchFailureTest()
{
    ClaudeStreamSession session;
    int exits = 0;
    int lastCode = 0;
    QString stderrSeen;
    QObject::connect(&session, &ClaudeStreamSession::finished,
                     [&exits, &lastCode](int code) {
                         ++exits;
                         lastCode = code;
                     });
    QObject::connect(&session, &ClaudeStreamSession::stderrText,
                     [&stderrSeen](const QString &text) { stderrSeen += text; });

    const QString missing =
        QDir::tempPath() + QStringLiteral("/forkmesh-no-such-worktree-1618");
    QDir().rmdir(missing); // make sure of it
    session.start(missing, {}, QStringLiteral("Continue where you left off."));

    check(pump([&exits] { return exits > 0; }),
          "a launch that cannot start reports an exit instead of hanging");
    check(lastCode != 0, "the reported exit code marks it as a failure");
    check(stderrSeen.contains(QStringLiteral("could not be started")),
          "the failure names itself in the session's error stream");
    check(!session.running() && !session.acceptsInput(),
          "a session that never started is neither running nor steerable");
    check(!session.sendUserText(QStringLiteral("try again")),
          "a follow-up sent to a dead transport is refused, not swallowed");

    // Give any late signal a chance to double-report the same ending: the owner
    // treats each exit as "the CLI died mid-turn" and would spend a relaunch
    // attempt on a second one.
    pump([] { return false; }, 150);
    check(exits == 1, "the ending is reported exactly once");
}

// An idle transport that was never started is the same answer from the other
// direction — nothing to steer, and the caller must be told so it can start one.
void runUnstartedSessionTest()
{
    ClaudeStreamSession session;
    check(!session.running() && !session.acceptsInput(),
          "an unstarted session reports no live transport");
    check(!session.sendUserText(QStringLiteral("hello")),
          "an unstarted session refuses a turn");
    check(session.sendUserText(QStringLiteral("   ")),
          "an empty turn is nothing to deliver, so it is not a delivery failure");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    runLaunchFailureTest();
    runUnstartedSessionTest();
    if (failures == 0)
        qInfo("All agent transport tests passed.");
    else
        qCritical("%d agent transport test(s) failed.", failures);
    return failures == 0 ? 0 : 1;
}
