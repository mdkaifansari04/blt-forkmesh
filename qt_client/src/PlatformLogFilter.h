#pragma once

#include <QString>

namespace forkmesh {

// True for the harmless "This plugin does not support propagateSizeHints()"
// qWarning that the offscreen/minimal QPA plugins emit whenever a top-level
// window pushes its size constraints (including the initial show()). Headless
// ForkMesh forces the offscreen platform, so that noise would otherwise land
// straight in the interactive `forkmesh>` console (issue #300).
bool isPlatformSizeHintNoise(const QString &message);

// Install a qInstallMessageHandler that drops isPlatformSizeHintNoise() messages
// and forwards everything else to the previously installed handler (or Qt's
// default behaviour: stderr, abort on fatal). Call once, after the QPA platform
// is chosen and before any window is shown.
void installPlatformLogFilter();

} // namespace forkmesh
