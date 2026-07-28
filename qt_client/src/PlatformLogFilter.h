#pragma once

#include <QString>

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

} // namespace forkmesh
