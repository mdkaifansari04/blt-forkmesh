#pragma once

// Re-entrancy state for the GUI-thread keep-alive pump.
//
// Every synchronous git read on the GUI thread goes through waitForGit(), which
// calls QCoreApplication::processEvents() between polls so the window keeps
// painting instead of freezing (see MainWindowInternal.h). That pump also
// delivers *queued* work — the results of background threads and detached git
// processes — so a worker that happens to finish while the GUI thread is parked
// in a git wait lands its (often heavy) apply handler nested inside whatever
// render was already running.
//
// That nesting is how a 500ms pull-list rebuild ended up stacked on top of a
// commit reload on top of a spool sweep for a single 14.6s freeze in the stall
// log: none of the individual passes was pathological, they just ran inside one
// another. The counter below lets those landing points recognise "I am being
// delivered inside a pump" and re-post themselves for the next top-level turn of
// the event loop, which costs one extra event-loop hop and keeps each pass on
// its own stack.
//
// Lives in its own header (rather than MainWindowInternal.h) so MainWindow.h's
// runOffThread template can consult it without pulling in the whole internal UI
// layer.
namespace forkmesh::ui {

// Depth of nested pumpKeepAlive() calls. inline so every translation unit shares
// one instance. Only ever touched on the GUI thread.
inline int g_keepAlivePumpDepth = 0;

// True while the GUI thread is inside a keep-alive pump, i.e. servicing events
// from within a blocking git wait rather than from the main event loop.
inline bool inKeepAlivePump() { return g_keepAlivePumpDepth > 0; }

// RAII depth marker for pumpKeepAlive(). Exception-safe and re-entrant.
struct KeepAlivePumpScope {
    KeepAlivePumpScope() { ++g_keepAlivePumpDepth; }
    ~KeepAlivePumpScope() { --g_keepAlivePumpDepth; }
    KeepAlivePumpScope(const KeepAlivePumpScope &) = delete;
    KeepAlivePumpScope &operator=(const KeepAlivePumpScope &) = delete;
};

} // namespace forkmesh::ui
