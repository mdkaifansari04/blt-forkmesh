#include "GitKeepAlive.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcess>

namespace gitkeepalive {
namespace {
// GUI-thread only; the synchronous git reads this guards all run on the main
// thread, so a plain int needs no synchronization.
int g_depth = 0;
} // namespace

bool active()
{
    return g_depth > 0;
}

void push()
{
    ++g_depth;
}

void pop()
{
    --g_depth;
}

bool waitForFinished(QProcess &process, int timeoutMs)
{
    if (g_depth <= 0)
        return process.waitForFinished(timeoutMs);
    QElapsedTimer timer;
    timer.start();
    // Poll in short slices, servicing the event loop between them. Excluding user
    // input keeps a stray click from re-entering a load mid-flight; the loaders'
    // own re-entrancy guards backstop anything that still slips through.
    while (!process.waitForFinished(40)) {
        if (process.state() == QProcess::NotRunning)
            return true; // exited between polls; caller inspects the exit code
        if (timeoutMs >= 0 && timer.hasExpired(timeoutMs))
            return false;
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 12);
    }
    return true;
}
} // namespace gitkeepalive
