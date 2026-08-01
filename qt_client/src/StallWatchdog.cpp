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



void btHandler(int)
{
    const int n = backtrace(g_btBuf, 64);
    g_btCount.store(n, std::memory_order_release);
}


QString captureBacktrace()
{
    g_btCount.store(-1, std::memory_order_release);
    if (pthread_kill(g_mainThread, SIGUSR2) != 0)
        return QString();
    for (int i = 0; i < 250 && g_btCount.load(std::memory_order_acquire) < 0; ++i)
        usleep(1000);
    const int n = g_btCount.load(std::memory_order_acquire);
    if (n <= 0)
        return QString();
    char **syms = backtrace_symbols(g_btBuf, n);
    if (!syms)
        return QString();
    QString out;

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



QString firstFrame(const QString &bt)
{
    const int nl = bt.indexOf(QLatin1Char('\n'));
    return nl < 0 ? bt : bt.left(nl);
}





QMutex g_blockingMutex;
QString g_blockingCall;
}

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
}

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


    m_pid = QCoreApplication::applicationPid();
    m_exePath = QCoreApplication::applicationFilePath();
    m_lastBeatMs.store(monoClock().elapsed());

#ifdef STALL_BACKTRACE
    g_mainThread = pthread_self();
    void *warm[4];
    backtrace(warm, 4);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = btHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR2, &sa, nullptr);
#endif



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
    QString bt;
    QString extra;
    QString lastTop;
    QString culprit;




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
                bt = captureBacktrace();



                culprit = stallwatch::blockingCall();
                lastTop = firstFrame(bt);
                lastSampleMs = now;
                sampleCount = 1;
            } else {
                peak = qMax(peak, age);
                if (culprit.isEmpty())
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
            inStall = false;



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
            emit stalled(peak, culprit, full);
            peak = 0;
            sampleCount = 0;
            bt.clear();
            extra.clear();
            lastTop.clear();
            culprit.clear();
        }
    }
}
