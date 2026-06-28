#pragma once

// Shared "keep the GUI responsive across blocking git reads" mechanism.
//
// Synchronous git subprocesses run on the GUI thread: the stores (IssueStore,
// PullStore, …) and the repo loaders all shell out to `git` and block on
// QProcess::waitForFinished(). On a large repo a single read can take seconds,
// and a repo load or the periodic detail refresh fires a dozen of them — which
// freezes the window, trips the StallWatchdog ("UI stalled / event loop
// blocked"), and gets the app flagged "Not Responding" by the window manager.
//
// When a GitKeepAlive scope is active (depth > 0), gitkeepalive::waitForFinished
// polls in short slices and services the GUI event loop between them — excluding
// user input so a stray click can't re-enter a load mid-flight — so the window
// keeps painting and spinners animate while git runs. With no scope active it
// blocks exactly as a plain waitForFinished() would. The depth is process-wide
// so the stores (separate translation units) participate in a scope opened by
// the MainWindow loader that invoked them.

class QProcess;

namespace gitkeepalive {
bool active();
void push();
void pop();
// Wait up to timeoutMs for `process` to finish. Pumps the GUI event loop in
// slices while a keep-alive scope is active; otherwise blocks identically to
// QProcess::waitForFinished(). Returns false on timeout (the caller still owns
// killing/cleanup, exactly as before).
bool waitForFinished(QProcess &process, int timeoutMs);
} // namespace gitkeepalive

// RAII scope: keep the GUI responsive across the synchronous git reads run while
// it is alive (a node switch, opening a repo, a periodic detail refresh, a
// branch op). Nestable; the innermost git wait sees the active depth.
struct GitKeepAlive {
    GitKeepAlive() { gitkeepalive::push(); }
    ~GitKeepAlive() { gitkeepalive::pop(); }
    GitKeepAlive(const GitKeepAlive &) = delete;
    GitKeepAlive &operator=(const GitKeepAlive &) = delete;
};
