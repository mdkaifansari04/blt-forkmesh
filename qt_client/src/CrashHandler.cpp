#include "CrashHandler.h"

#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cstdlib>
#include <atomic>
#include <cstring>
#include <exception>
#include <initializer_list>

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#define FORKMESH_CRASH_HANDLER 1
#endif

namespace forkmesh {

#ifdef FORKMESH_CRASH_HANDLER
namespace {

// Everything a signal handler touches is prepared here, in normal context, so
// the handler itself only calls async-signal-safe functions. Fixed C buffers,
// no QString/std::string, so there's no allocation on the crash path.
char g_buildInfo[512] = {0};
char g_crashContext[4096] = {0};

// A dedicated stack so the SIGSEGV handler still runs when the crash *is* a
// stack overflow (the normal stack is then unusable). A fixed 64 KiB: modern
// glibc makes SIGSTKSZ a runtime sysconf() call, so it can't size a static
// array — 64 KiB comfortably exceeds the classic 8 KiB MINSIGSTKSZ.
char g_altStack[64 * 1024];

// Durable crash-log file descriptor, opened ahead of any fault in
// installCrashHandler() so the handler only ever does signal-safe writes to it.
// -1 = no durable log (write to stderr only). The opt-in telemetry upload reads
// this file on the next startup (issue #354).
int g_crashFd = -1;

// Guard against a fault while we're already handling one (a bug in the handler,
// or a second thread crashing) turning into an infinite loop.
std::atomic_flag g_handling = ATOMIC_FLAG_INIT;

// Async-signal-safe unsigned-to-decimal. Writes into buf, returns length.
int safeUtoa(unsigned long v, char *buf)
{
    char tmp[24];
    int n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v > 0) {
        tmp[n++] = char('0' + (v % 10));
        v /= 10;
    }
    for (int i = 0; i < n; ++i)
        buf[i] = tmp[n - 1 - i];
    return n;
}

// Write to stderr and, when open, mirror the same bytes into the durable crash
// log so the record survives the process for the next-startup telemetry upload.
void safeWrite(const char *s)
{
    if (s && *s) {
        const size_t len = ::strlen(s);
        ssize_t r = ::write(2, s, len);
        (void)r;
        if (g_crashFd >= 0)
            r = ::write(g_crashFd, s, len);
        (void)r;
    }
}

void safeWriteNum(unsigned long v)
{
    char buf[24];
    int n = safeUtoa(v, buf);
    ssize_t r = ::write(2, buf, size_t(n));
    (void)r;
    if (g_crashFd >= 0)
        r = ::write(g_crashFd, buf, size_t(n));
    (void)r;
}

void safeWriteCrashContext()
{
    if (!g_crashContext[0])
        return;
    safeWrite("context:\n");
    safeWrite(g_crashContext);
    safeWrite("\n");
}

// backtrace_symbols_fd targets one fd, so emit the frames to stderr and the
// durable log separately.
void safeBacktrace(void *const *frames, int n)
{
    backtrace_symbols_fd(frames, n, 2);
    if (g_crashFd >= 0)
        backtrace_symbols_fd(frames, n, g_crashFd);
}

const char *signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGABRT: return "SIGABRT (abort / assertion / unhandled exception)";
    case SIGBUS:  return "SIGBUS (bus error)";
    case SIGFPE:  return "SIGFPE (floating-point / integer error)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    case SIGTERM: return "SIGTERM (terminated)";
    case SIGINT:  return "SIGINT (interrupt)";
    case SIGHUP:  return "SIGHUP (hangup)";
    case SIGQUIT: return "SIGQUIT (quit)";
#ifdef SIGTRAP
    case SIGTRAP: return "SIGTRAP (trace/breakpoint trap)";
#endif
#ifdef SIGSYS
    case SIGSYS:  return "SIGSYS (bad system call)";
#endif
#ifdef SIGXCPU
    case SIGXCPU: return "SIGXCPU (CPU time limit exceeded)";
#endif
#ifdef SIGXFSZ
    case SIGXFSZ: return "SIGXFSZ (file size limit exceeded)";
#endif
    default:      return "signal";
    }
}

// The signal handler. Strictly async-signal-safe: only write/time/
// backtrace(_symbols_fd), no allocation, no Qt.
void crashHandler(int sig)
{
    // First faulter wins; anyone re-entering just restores the default and dies.
    if (g_handling.test_and_set()) {
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }

    safeWrite("\n===== ForkMesh crash =====\n");
    safeWrite("when (epoch): ");
    safeWriteNum((unsigned long)::time(nullptr));
    safeWrite("\nsignal: ");
    safeWrite(signalName(sig));
    safeWrite("\nbuild: ");
    safeWrite(g_buildInfo);
    safeWrite("\n");
    safeWriteCrashContext();
    safeWrite("\nbacktrace:\n");
    void *frames[64];
    int n = backtrace(frames, 64);
    safeBacktrace(frames, n);
    safeWrite("==========================\n");

    // Chain to the default handler so the OS still terminates the process (and
    // writes a core dump if enabled) exactly as it would have without us.
    signal(sig, SIG_DFL);
    raise(sig);
}

// std::terminate fires for an uncaught C++ exception. It runs in normal context
// (not a signal), so we can grab the exception message before abort() — which
// raises SIGABRT and thus logs the backtrace through crashHandler().
void terminateHandler()
{
    const char *what = nullptr;
    if (std::current_exception()) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception &e) {
            what = e.what();
        } catch (...) {
            what = "(non-std exception)";
        }
    }
    safeWrite("\n===== ForkMesh unhandled exception =====\n");
    safeWrite("what: ");
    safeWrite(what ? what : "(unknown / no active exception)");
    safeWrite("\n");
    safeWriteCrashContext();
    std::abort(); // -> SIGABRT -> crashHandler() logs the backtrace
}

void installSignalHandlers()
{
    // Run SIGSEGV etc. on a dedicated stack so a stack-overflow crash can still
    // be logged.
    stack_t ss;
    ss.ss_sp = g_altStack;
    ss.ss_size = sizeof(g_altStack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
    // Catch every normal fatal/termination signal we can reasonably handle. The
    // kernel does not allow SIGKILL or SIGSTOP to be caught, blocked, or logged.
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGTERM, SIGINT,
                    SIGHUP, SIGQUIT
#ifdef SIGTRAP
                    , SIGTRAP
#endif
#ifdef SIGSYS
                    , SIGSYS
#endif
#ifdef SIGXCPU
                    , SIGXCPU
#endif
#ifdef SIGXFSZ
                    , SIGXFSZ
#endif
         })
        sigaction(sig, &sa, nullptr);
}

} // namespace
#endif // FORKMESH_CRASH_HANDLER

namespace {

QString compactDiagnosticForMainLog(QString text)
{
    text.replace(QChar(0x2014), QLatin1Char('-'));
    text.replace(QChar(0x2026), QStringLiteral("..."));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QStringLiteral(" | "));
    text = text.simplified();
    constexpr qsizetype kMaxMainLogDiagnosticChars = 6000;
    if (text.size() > kMaxMainLogDiagnosticChars) {
        text = QStringLiteral("...(diagnostic truncated; showing tail)... ") +
               text.right(kMaxMainLogDiagnosticChars);
    }
    return text;
}

void appendDiagnosticToMainLog(const QString &kind, const QString &context,
                               const QString &details)
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QFile f(dir + QStringLiteral("/network_log.txt"));
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    const QString line =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
        QStringLiteral("  %1: %2").arg(kind, compactDiagnosticForMainLog(context));
    f.write(line.toUtf8());
    if (!details.trimmed().isEmpty()) {
        f.write(" | ");
        f.write(compactDiagnosticForMainLog(details).toUtf8());
    }
    f.write("\n");
}

} // namespace

void installCrashHandler(const QString &crashLogPath)
{
#ifdef FORKMESH_CRASH_HANDLER
    const QByteArray build =
        QByteArrayLiteral("ForkMesh v" FORKMESH_VERSION " (src " FORKMESH_SOURCE_DIR ")");
    qstrncpy(g_buildInfo, build.constData(), sizeof(g_buildInfo));

    // Open the durable crash log now, in normal context, so the signal handler
    // only writes to an already-open fd. Best-effort: on any failure we simply
    // fall back to stderr-only logging (g_crashFd stays -1).
    if (!crashLogPath.isEmpty()) {
        QDir().mkpath(QFileInfo(crashLogPath).absolutePath());
        const QByteArray path = QFile::encodeName(crashLogPath);
        g_crashFd = ::open(path.constData(),
                           O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    }

    // Warm up backtrace() so the first (in-handler) call can't try to dlopen
    // libgcc's unwinder — that would malloc, which isn't signal-safe.
    void *warm[4];
    backtrace(warm, 4);

    installSignalHandlers();
    std::set_terminate(terminateHandler);
#else
    Q_UNUSED(crashLogPath);
#endif
}

void setCrashContext(const QString &context)
{
#ifdef FORKMESH_CRASH_HANDLER
    const QByteArray utf8 = context.toUtf8();
    const int n = qMin<int>(utf8.size(), int(sizeof(g_crashContext)) - 1);
    if (n > 0)
        memcpy(g_crashContext, utf8.constData() + utf8.size() - n, size_t(n));
    g_crashContext[n] = '\0';
#else
    Q_UNUSED(context);
#endif
}

void logDiagnosticEvent(const QString &context, const QString &details)
{
    const QString block =
        QStringLiteral("\n===== ForkMesh diagnostic =====\n")
        + QStringLiteral("when (epoch): ")
        + QString::number(QDateTime::currentSecsSinceEpoch()) + QLatin1Char('\n')
        + QStringLiteral("context: ") + context + QLatin1Char('\n')
        + details + QLatin1Char('\n')
        + QStringLiteral("===============================\n");

    // Main application log (stderr). qCritical keeps it visible at default log
    // levels and alongside the startup timing / node log.
    qCritical().noquote() << block;
    appendDiagnosticToMainLog(QStringLiteral("ForkMesh diagnostic"), context,
                              details);

#ifdef FORKMESH_CRASH_HANDLER
    // Mirror into the durable crash log, when one was opened, so the breadcrumb
    // survives if the UI dies while handling the failure.
    if (g_crashFd >= 0) {
        const QByteArray utf8 = block.toUtf8();
        ssize_t r = ::write(g_crashFd, utf8.constData(), size_t(utf8.size()));
        (void)r;
    }
#endif
}

void logCaughtFault(const QString &context, const QString &what)
{
    const QString block =
        QStringLiteral("\n===== ForkMesh caught fault =====\n")
        + QStringLiteral("when (epoch): ")
        + QString::number(QDateTime::currentSecsSinceEpoch()) + QLatin1Char('\n')
        + QStringLiteral("context: ") + context + QLatin1Char('\n')
        + QStringLiteral("what: ") + what + QLatin1Char('\n')
        + QStringLiteral("=================================\n");

    qCritical().noquote() << block;
    appendDiagnosticToMainLog(QStringLiteral("ForkMesh caught fault"), context,
                              what);

#ifdef FORKMESH_CRASH_HANDLER
    if (g_crashFd >= 0) {
        const QByteArray utf8 = block.toUtf8();
        ssize_t r = ::write(g_crashFd, utf8.constData(), size_t(utf8.size()));
        (void)r;
    }
#endif
}

} // namespace forkmesh
