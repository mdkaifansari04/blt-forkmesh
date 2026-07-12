#include "ForkMeshVersion.h"
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

// The app's user-facing Log view persists to network_log.txt. A pre-opened fd
// lets the signal handler write crash records directly there so they appear in
// the log view without needing a separate crash file.
int g_mainLogFd = -1;

// Guard against a fault while we're already handling one (a bug in the handler,
// or a second thread crashing) turning into an infinite loop.
std::atomic_flag g_handling = ATOMIC_FLAG_INIT;

// Set only while a local action workflow owns child processes. A failing child
// step may be terminated as part of a wider process group; the UI should record
// that signal and continue so the action can settle into a normal failed run.
std::atomic_bool g_surviveTerminationSignals{false};

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

int safeItoa(long v, char *buf)
{
    if (v < 0) {
        buf[0] = '-';
        const unsigned long magnitude =
            (unsigned long)(-(v + 1)) + 1UL;
        return 1 + safeUtoa(magnitude, buf + 1);
    }
    return safeUtoa((unsigned long)v, buf);
}

void safeWriteFd(int fd, const char *s)
{
    if (fd < 0 || !s || !*s)
        return;
    const size_t len = ::strlen(s);
    ssize_t r = ::write(fd, s, len);
    (void)r;
}

// Write to stderr and also to the network log so crash records appear in the
// user-visible log view without needing a separate crash file.
void safeWrite(const char *s)
{
    safeWriteFd(2, s);
    safeWriteFd(g_mainLogFd, s);
}

void safeWriteNumToFd(int fd, unsigned long v)
{
    char buf[24];
    int n = safeUtoa(v, buf);
    if (fd < 0)
        return;
    ssize_t r = ::write(fd, buf, size_t(n));
    (void)r;
}

void safeWriteNum(unsigned long v)
{
    safeWriteNumToFd(2, v);
    safeWriteNumToFd(g_mainLogFd, v);
}

void safeWriteInt(long v)
{
    char buf[24];
    int n = safeItoa(v, buf);
    ssize_t r = ::write(2, buf, size_t(n));
    (void)r;
    if (g_mainLogFd >= 0)
        r = ::write(g_mainLogFd, buf, size_t(n));
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

void safeWriteSignalInfo(const siginfo_t *info)
{
    if (!info)
        return;
    safeWrite("signal code: ");
    safeWriteInt((long)info->si_code);
    safeWrite("\n");
    safeWrite("sender pid: ");
    safeWriteNum((unsigned long)info->si_pid);
    safeWrite("\n");
    safeWrite("sender uid: ");
    safeWriteNum((unsigned long)info->si_uid);
    safeWrite("\n");
}

// backtrace_symbols_fd targets one fd, so emit the frames to stderr and the
// network log separately.
void safeBacktrace(void *const *frames, int n)
{
    backtrace_symbols_fd(frames, n, 2);
    if (g_mainLogFd >= 0)
        backtrace_symbols_fd(frames, n, g_mainLogFd);
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

bool isTerminationSignal(int sig)
{
    switch (sig) {
    case SIGTERM:
    case SIGINT:
    case SIGHUP:
    case SIGQUIT:
        return true;
    default:
        return false;
    }
}

void safeWriteMainLogSignalRecord(int sig, unsigned long when,
                                  bool survived)
{
    if (g_mainLogFd < 0)
        return;

    safeWriteFd(g_mainLogFd, "ForkMesh signal: ");
    safeWriteFd(g_mainLogFd, signalName(sig));
    safeWriteFd(g_mainLogFd, " at epoch ");
    safeWriteNumToFd(g_mainLogFd, when);
    if (survived) {
        safeWriteFd(g_mainLogFd,
                    "; action workflow survived and will finish/fail normally");
    } else {
        safeWriteFd(g_mainLogFd,
                    "; terminating; full record logged above");
    }
    safeWriteFd(g_mainLogFd, "\n");
}

// The signal handler. Strictly async-signal-safe: only write/time/
// backtrace(_symbols_fd), no allocation, no Qt.
void crashHandler(int sig, siginfo_t *info, void *)
{
    // First faulter wins; anyone re-entering just restores the default and dies.
    if (g_handling.test_and_set()) {
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }

    safeWrite("\n===== ForkMesh crash =====\n");
    const unsigned long when = (unsigned long)::time(nullptr);
    safeWrite("when (epoch): ");
    safeWriteNum(when);
    safeWrite("\nsignal: ");
    safeWrite(signalName(sig));
    safeWrite("\nbuild: ");
    safeWrite(g_buildInfo);
    safeWrite("\n");
    safeWriteSignalInfo(info);
    safeWriteCrashContext();

    const bool terminationSignal = isTerminationSignal(sig);
    const bool surviveTermination =
        terminationSignal &&
        g_surviveTerminationSignals.load(std::memory_order_relaxed);
    safeWriteMainLogSignalRecord(sig, when, surviveTermination);

    if (surviveTermination) {
        safeWrite("termination signal survived: action workflow is still "
                  "running; continuing so the run can fail cleanly\n");
        safeWrite("==========================\n");
        g_handling.clear(std::memory_order_release);
        return;
    }

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
    sa.sa_sigaction = crashHandler;
    sigemptyset(&sa.sa_mask);
    // Do not use SA_RESETHAND here: an action may survive a logged SIGTERM and
    // then need the handler again before the failed child process exits. For
    // real fatal crashes we explicitly restore the default handler below before
    // re-raising.
    sa.sa_flags = SA_ONSTACK | SA_SIGINFO;
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

void installCrashHandler(const QString &crashLogPath,
                         const QString &mainLogPath)
{
#ifdef FORKMESH_CRASH_HANDLER
    const QByteArray build =
        QByteArrayLiteral("ForkMesh v" FORKMESH_VERSION " (src " FORKMESH_SOURCE_DIR ")");
    qstrncpy(g_buildInfo, build.constData(), sizeof(g_buildInfo));

    // crashLogPath is no longer used — crash records go directly to the network
    // log (g_mainLogFd / network_log.txt) so they appear in the log view without
    // needing a separate file. The parameter is kept for API compatibility.
    Q_UNUSED(crashLogPath);
    if (!mainLogPath.isEmpty()) {
        QDir().mkpath(QFileInfo(mainLogPath).absolutePath());
        const QByteArray path = QFile::encodeName(mainLogPath);
        g_mainLogFd = ::open(path.constData(),
                             O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    }

    // Warm up backtrace() so the first (in-handler) call can't try to dlopen
    // libgcc's unwinder — that would malloc, which isn't signal-safe.
    void *warm[4];
    backtrace(warm, 4);

    installSignalHandlers();
    std::set_terminate(terminateHandler);
#else
    Q_UNUSED(crashLogPath); // kept for API compatibility; never used
    Q_UNUSED(mainLogPath);
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

void setTerminationSignalSurvivalEnabled(bool enabled)
{
#ifdef FORKMESH_CRASH_HANDLER
    g_surviveTerminationSignals.store(enabled, std::memory_order_relaxed);
#else
    Q_UNUSED(enabled);
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
}

} // namespace forkmesh
