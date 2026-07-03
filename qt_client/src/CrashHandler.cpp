#include "CrashHandler.h"

#include <QByteArray>

#include <cstdlib>
#include <atomic>
#include <cstring>
#include <exception>
#include <initializer_list>

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
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

// A dedicated stack so the SIGSEGV handler still runs when the crash *is* a
// stack overflow (the normal stack is then unusable). A fixed 64 KiB: modern
// glibc makes SIGSTKSZ a runtime sysconf() call, so it can't size a static
// array — 64 KiB comfortably exceeds the classic 8 KiB MINSIGSTKSZ.
char g_altStack[64 * 1024];

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

void safeWrite(int fd, const char *s)
{
    if (s && *s) {
        ssize_t r = ::write(fd, s, ::strlen(s));
        (void)r;
    }
}

void safeWriteNum(int fd, unsigned long v)
{
    char buf[24];
    int n = safeUtoa(v, buf);
    ssize_t r = ::write(fd, buf, size_t(n));
    (void)r;
}

const char *signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGABRT: return "SIGABRT (abort / assertion / unhandled exception)";
    case SIGBUS:  return "SIGBUS (bus error)";
    case SIGFPE:  return "SIGFPE (floating-point / integer error)";
    case SIGILL:  return "SIGILL (illegal instruction)";
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

    const int fd = 2;  // stderr
    safeWrite(fd, "\n===== ForkMesh crash =====\n");
    safeWrite(fd, "when (epoch): ");
    safeWriteNum(fd, (unsigned long)::time(nullptr));
    safeWrite(fd, "\nsignal: ");
    safeWrite(fd, signalName(sig));
    safeWrite(fd, "\nbuild: ");
    safeWrite(fd, g_buildInfo);
    safeWrite(fd, "\nbacktrace:\n");
    void *frames[64];
    int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, fd);
    safeWrite(fd, "==========================\n");

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
    const int fd = 2;  // stderr
    safeWrite(fd, "\n===== ForkMesh unhandled exception =====\n");
    safeWrite(fd, "what: ");
    safeWrite(fd, what ? what : "(unknown / no active exception)");
    safeWrite(fd, "\n");
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
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
        sigaction(sig, &sa, nullptr);
}

} // namespace
#endif // FORKMESH_CRASH_HANDLER

void installCrashHandler()
{
#ifdef FORKMESH_CRASH_HANDLER
    const QByteArray build =
        QByteArrayLiteral("ForkMesh v" FORKMESH_VERSION " (src " FORKMESH_SOURCE_DIR ")");
    qstrncpy(g_buildInfo, build.constData(), sizeof(g_buildInfo));

    // Warm up backtrace() so the first (in-handler) call can't try to dlopen
    // libgcc's unwinder — that would malloc, which isn't signal-safe.
    void *warm[4];
    backtrace(warm, 4);

    installSignalHandlers();
    std::set_terminate(terminateHandler);
#endif
}

} // namespace forkmesh
