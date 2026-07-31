#pragma once

#include <QString>

class QImage;

// Where pasted/captured screenshots attached to an agent prompt live on disk.
//
// The prompt text carries them as "Attached image: <path>" lines, so the file
// has to outlive the run: the transcript re-renders those lines as thumbnails
// every time an old session is opened. They used to be written to the system
// temp dir, which is a tmpfs on Linux — a reboot (or a tmp cleaner) wiped them
// and the picture degraded into a raw path on the next restart (adhoc #66).
// They are kept under the app's data directory instead.
namespace AgentPromptImages {

// Absolute path of the persistent attachment directory (created on demand).
QString directory();

// Write `image` as a PNG in directory() and return its absolute path, or an
// empty string if it couldn't be saved.
QString save(const QImage &image);

// Resolve an "Attached image:" path for display: `path` itself when it still
// exists, otherwise the same file name inside directory() (which is where a
// legacy temp-dir attachment lands after migrateLegacy()). Empty when neither
// is readable.
QString resolve(const QString &path);

// Copy any attachments left in the old temp directory into directory(), so
// transcripts written before this change keep their thumbnails across the next
// reboot. Runs at most once per process; cheap when there is nothing to do.
void migrateLegacy();

} // namespace AgentPromptImages
