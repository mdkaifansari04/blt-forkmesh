#pragma once

#include <QString>
#include <QtGlobal>

#include <functional>

namespace forkmesh {

// True for the harmless "This plugin does not support propagateSizeHints()"
// qWarning that the offscreen/minimal QPA plugins emit whenever a top-level
// window pushes its size constraints (including the initial show()). Headless
// ForkMesh forces the offscreen platform, so that noise would otherwise land
// straight in the interactive `forkmesh>` console (issue #300).
bool isPlatformSizeHintNoise(const QString &message);

// True for the "OpenType support missing for <family>, script N" warnings that
// QFontDatabase (category qt.text.font.db) emits while walking the fallback
// list for a codepoint in a script the fonts can't shape. Nothing in the app
// picks those families — Qt re-walks *every* installed family on each such
// draw, so a single unshapeable character (common in chat text or a repo file)
// buries the console under one line per installed font, repeatedly. The glyph
// still falls back and renders; only the log line is new information, and only
// the first time.
bool isFontDatabaseNoise(const QString &message);

// Install a qInstallMessageHandler that drops isPlatformSizeHintNoise() and
// isFontDatabaseNoise() messages and forwards everything else to the previously
// installed handler (or Qt's default behaviour: stderr, abort on fatal). Call
// once, after the QPA platform is chosen and before any window is shown.
void installPlatformLogFilter();

// Destination for the messages the filter keeps: called with the message type
// and text, from whichever thread emitted them, plus the file and line the
// message was emitted from. Those two are what Qt's QMessageLogContext carried
// — populated only in builds compiled with QT_MESSAGELOGCONTEXT, so the sink
// must cope with an empty file and a zero line.
using AppLogSink =
    std::function<void(QtMsgType, const QString &, const QString &, int)>;

// Send every surviving message to `sink` — the app's Log view — instead of the
// terminal. This is where the app's own qInfo() progress lines (catalog
// publishes, mirror syncs, restart phases) and Qt's own warnings ("QProcess:
// Destroyed while process ("git") is still running.") belong: the desktop is
// launched from a terminal nobody watches, while the Log view is on screen and
// persisted to disk. keepConsoleEcho leaves the terminal copy in place as well,
// which is what a headless node wants — its operator reads the service journal,
// not a GUI panel it cannot see.
//
// Fatal messages always reach the console (and Qt's abort) regardless: they are
// the last thing the process says and the app log never gets flushed after one.
// A sink that logs re-entrantly is forwarded to the console instead, so it
// cannot loop.
void setAppLogSink(AppLogSink sink, bool keepConsoleEcho);

// Stop routing to the sink; messages go back to the console. Blocks until any
// in-flight sink call has returned, so the callee can be destroyed right after.
void clearAppLogSink();

} // namespace forkmesh
