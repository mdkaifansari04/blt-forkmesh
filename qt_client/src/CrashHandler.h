#pragma once

// Last-resort crash logging. The app "sometimes exits / crashes" with nothing
// left behind to explain why, so we install handlers for the fatal signals
// (SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL) and for unhandled C++ exceptions. When
// one fires we write a timestamped record — signal, build info and a native
// backtrace — to stderr (joining the main application log), then let the default
// disposition run so the OS still cores/reports the crash as usual.
//
// The write path is async-signal-safe (raw write/backtrace_symbols_fd to stderr,
// no malloc/Qt), because a crashing process is in an undefined state.

namespace forkmesh {

// Install the crash handlers. Safe to call once, as early in main() as possible
// (before QApplication) so crashes during startup are captured too. No-op on
// platforms without the backtrace/signal facilities.
void installCrashHandler();

} // namespace forkmesh
