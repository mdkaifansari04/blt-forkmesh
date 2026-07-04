#pragma once

#include <QString>

// Last-resort crash logging. The app "sometimes exits / crashes" with nothing
// left behind to explain why, so we install handlers for the fatal signals
// (SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL) and for unhandled C++ exceptions. When
// one fires we write a timestamped record — signal, build info and a native
// backtrace — to stderr (joining the main application log) and, if a crash-log
// path was given, to that durable file too, then let the default disposition run
// so the OS still cores/reports the crash as usual. The durable file is what the
// opt-in telemetry upload (issue #354) sends on the next startup.
//
// The write path is async-signal-safe (raw write/backtrace_symbols_fd to a
// pre-opened fd, no malloc/Qt), because a crashing process is in an undefined
// state.

namespace forkmesh {

// Install the crash handlers. Safe to call once, as early in main() as possible
// (before QApplication) so crashes during startup are captured too. No-op on
// platforms without the backtrace/signal facilities. If crashLogPath is given,
// the record is also appended to that file (opened here, ahead of any fault, so
// the handler itself only does async-signal-safe writes); its parent directory
// is created if needed.
void installCrashHandler(const QString &crashLogPath = QString());

// Record a non-fatal fault that was caught rather than allowed to abort the
// process — e.g. a C++ exception thrown out of a slot during Qt event delivery
// (the classic "app vanishes when I click a tab"). Writes a timestamped block to
// stderr (the main application log) and, when a durable crash-log path was given
// to installCrashHandler(), appends it there too so it rides along with real
// crashes for the telemetry upload. Unlike the signal handlers this runs in
// normal context, so it may use Qt/allocation freely.
void logCaughtFault(const QString &context, const QString &what);

} // namespace forkmesh
