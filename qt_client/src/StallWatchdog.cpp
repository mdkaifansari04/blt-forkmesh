#include "StallWatchdog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QTextStream>
#include <QTimer>

#include <chrono>
#include <cstring>

#if defined(__linux__) && defined(__GLIBC__)
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#define STALL_BACKTRACE 1
#endif

namespace {
// One process-wide monotonic clock shared by the heartbeat and the watcher.
QElapsedTimer &monoClock()
{
    static QElapsedTimer c;
    if (!c.isValid())
        c.start();
    return c;
}

#ifdef STALL_BACKTRACE
pthread_t g_mainThread;
void *g_btBuf[64];
std::atomic<int> g_btCount{-1};

// Async-signal context: just sample the stack and stash the count. Symbolising
// (backtrace_symbols, which mallocs) is done later on the watcher thread.
void btHandler(int)
{
    const int n = backtrace(g_btBuf, 64);
    g_btCount.store(n, std::memory_order_release);
}

// Ask the (stalled) main thread for its current stack. Returns symbolised frames.
QString captureBacktrace()
{
    g_btCount.store(-1, std::memory_order_release);
    if (pthread_kill(g_mainThread, SIGUSR2) != 0)
        return QString();
    for (int i = 0; i < 250 && g_btCount.load(std::memory_order_acquire) < 0; ++i)
        usleep(1000); // up to ~250ms for the handler to run
    const int n = g_btCount.load(std::memory_order_acquire);
    if (n <= 0)
        return QString();
    char **syms = backtrace_symbols(g_btBuf, n);
    if (!syms)
        return QString();
    QString out;
    // Skip the first couple of frames (the signal handler / libc trampoline).
    for (int i = 2; i < n; ++i) {
        out += QString::fromUtf8(syms[i]);
        out += QLatin1Char('\n');
    }
    free(syms);
    return out;
}
#else
QString captureBacktrace() { return QString(); }
#endif

// The innermost (deepest) symbol line of a backtrace, used to tell whether a
// dragging stall has moved on to a different blocking spot between samples.
QString firstFrame(const QString &bt)
{
    const int nl = bt.indexOf(QLatin1Char('\n'));
    return nl < 0 ? bt : bt.left(nl);
}

// The current main-thread breadcrumb (see stallwatch::noteBlockingCall). Written
// by the GUI thread, read by the watcher thread, so it's mutex-guarded — the lock
// is only ever held for the assignment/copy below, never across a blocking call,
// so the watcher can always read it even while the GUI thread is frozen.
QMutex g_blockingMutex;
QString g_blockingCall;
} // namespace

namespace stallwatch {
void noteBlockingCall(const QString &what)
{
    QMutexLocker lock(&g_blockingMutex);
    g_blockingCall = what;
}
QString blockingCall()
{
    QMutexLocker lock(&g_blockingMutex);
    return g_blockingCall;
}
} // namespace stallwatch

BlockingCallScope::BlockingCallScope(const QString &what)
    : m_prev(stallwatch::blockingCall())
{
    stallwatch::noteBlockingCall(what);
}
BlockingCallScope::~BlockingCallScope() { stallwatch::noteBlockingCall(m_prev); }

StallWatchdog::StallWatchdog(QObject *parent) : QObject(parent)
{
    monoClock();
}

StallWatchdog::~StallWatchdog()
{
    m_running.store(false);
    if (m_worker.joinable())
        m_worker.join();
}

void StallWatchdog::start(int stallThresholdMs, const QString &logPath,
                          const QString &buildInfo)
{
    if (m_running.load())
        return;
    m_thresholdMs = stallThresholdMs;
    m_logPath = logPath;
    m_buildInfo = buildInfo;
    // Captured here on the main thread so the watcher thread doesn't touch Qt
    // app state; both feed the per-stall context header (see watchLoop()).
    m_pid = QCoreApplication::applicationPid();
    m_exePath = QCoreApplication::applicationFilePath();
    m_lastBeatMs.store(monoClock().elapsed());

#ifdef STALL_BACKTRACE
    g_mainThread = pthread_self();
    void *warm[4];
    backtrace(warm, 4); // page in libgcc so the in-handler call can't dlopen
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = btHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // let interrupted syscalls resume
    sigaction(SIGUSR2, &sa, nullptr);
#endif

    // Heartbeat on the main thread: while the event loop is healthy this fires
    // every 200ms; while it's blocked, it can't, and the gap is the stall.
    m_beatTimer = new QTimer(this);
    m_beatTimer->setInterval(200);
    connect(m_beatTimer, &QTimer::timeout, this, [this] { beat(); });
    m_beatTimer->start();

    m_running.store(true);
    m_worker = std::thread([this] { watchLoop(); });
}

void StallWatchdog::beat()
{
    m_lastBeatMs.store(monoClock().elapsed(), std::memory_order_relaxed);
}

void StallWatchdog::watchLoop()
{
    bool inStall = false;
    qint64 peak = 0;
    qint64 lastSampleMs = 0;
    int sampleCount = 0;
    QString bt;       // first sample (also handed to the stalled() signal)
    QString extra;    // later samples taken while a long stall keeps dragging on
    QString lastTop;  // deepest frame of the last sample, to skip duplicate spots
    QString culprit;  // breadcrumb of the operation blocking when the stall began
    // A single sample taken at the 1.5s mark mislabels multi-phase stalls — e.g. a
    // panel rebuild that runs a dozen back-to-back git calls, or layout thrash,
    // gets blamed on whatever frame the one sample happened to catch. Re-sampling a
    // dragging stall captures each distinct blocking spot so it's actually fixable.
    constexpr int kMaxSamples = 6;
    constexpr qint64 kResampleMs = 1500;
    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const qint64 now = monoClock().elapsed();
        const qint64 age = now - m_lastBeatMs.load(std::memory_order_relaxed);
        if (age > m_thresholdMs) {
            if (!inStall) {
                inStall = true;
                peak = age;
                bt = captureBacktrace(); // sample the stack at first detection
                // What the GUI thread said it was doing — usually the git command
                // in flight — is far more actionable than a raw backtrace and is
                // often the only readable clue when frames don't symbolise.
                culprit = stallwatch::blockingCall();
                lastTop = firstFrame(bt);
                lastSampleMs = now;
                sampleCount = 1;
            } else {
                peak = qMax(peak, age);
                if (culprit.isEmpty()) // set slightly after the stall began
                    culprit = stallwatch::blockingCall();
                if (sampleCount < kMaxSamples && now - lastSampleMs >= kResampleMs) {
                    lastSampleMs = now;
                    const QString s = captureBacktrace();
                    const QString top = firstFrame(s);
                    if (!s.isEmpty() && top != lastTop) {
                        extra += QStringLiteral("-- still blocked ~%1 ms in --\n").arg(age);
                        extra += s;
                        lastTop = top;
                        ++sampleCount;
                    }
                }
            }
        } else if (inStall) {
            inStall = false; // the event loop resumed
            // Context header so a stall report is actionable on its own: which
            // build (and where its source lives), the process, how long/how badly
            // it blocked, and how to turn the raw frames below into file:line.
            QString header =
                QStringLiteral("context: %1 | pid %2 | main thread blocked ~%3 ms "
                               "(threshold %4 ms) | %5 stack sample(s)\n")
                    .arg(m_buildInfo.isEmpty() ? QStringLiteral("ForkMesh (build unknown)")
                                               : m_buildInfo)
                    .arg(m_pid)
                    .arg(peak)
                    .arg(m_thresholdMs)
                    .arg(sampleCount);
            if (!culprit.isEmpty())
                header += QStringLiteral("blocking call: %1\n").arg(culprit);
            if (!m_exePath.isEmpty())
                header += QStringLiteral(
                              "symbolize unresolved frames: addr2line -fpe %1 <hex-addr>\n")
                              .arg(m_exePath);
            const QString full = header + bt + extra;
            // Durable record first, so it survives a later hang/crash.
            if (!m_logPath.isEmpty()) {
                QDir().mkpath(QFileInfo(m_logPath).absolutePath());
                QFile f(m_logPath);
                if (f.open(QIODevice::Append | QIODevice::Text)) {
                    QTextStream ts(&f);
                    ts << QDateTime::currentDateTime().toString(Qt::ISODate)
                       << "  UI stalled ~" << peak << " ms\n";
                    if (!full.isEmpty())
                        ts << full;
                    ts << "----\n";
                }
            }
            emit stalled(peak, culprit, full); // queued to the main thread for live display
            peak = 0;
            sampleCount = 0;
            bt.clear();
            extra.clear();
            lastTop.clear();
            culprit.clear();
        }
    }
}
