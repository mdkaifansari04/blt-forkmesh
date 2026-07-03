#pragma once

#include <functional>

// Enforces at most one ForkMesh process per user. Every launch trigger we don't
// control — a desktop-environment session restore, a login-autostart entry
// racing a manually-opened window, a double-click while a slow cold start is
// still loading — used to hand back a brand new, fully independent MainWindow:
// its own mesh backend, its own repo-hosting server, its own tray icon, all
// reading/writing the same ~/.forkmesh data out from under each other. Rather
// than chase down every OS-specific trigger, this makes a duplicate launch a
// no-op: the second process pings the first to raise its window and exits
// before doing any real work.
namespace forkmesh {

// Call once, immediately after constructing QApplication. Returns true if this
// process becomes (or already is) the primary instance and startup should
// continue normally. Returns false if another instance is already running —
// it has been pinged to raise its window, and this process must exit at once
// without constructing MainWindow.
bool acquireSingleInstance();

// Registers the callback invoked (on the primary instance) when a later launch
// attempt pings it, so it can raise/focus its window. Call after MainWindow is
// constructed and before app.exec().
void onSingleInstanceActivation(std::function<void()> handler);

// Releases the instance lock so a process this instance is about to spawn (a
// self-update rebuild, or a relaunch after wiping data) can immediately become
// the new primary, instead of bouncing off the still-held lock and quitting
// into nothing. Call this right before every intentional
// QProcess::startDetached() relaunch of the ForkMesh binary itself.
void releaseSingleInstance();

} // namespace forkmesh
