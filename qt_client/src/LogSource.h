#pragma once

// Source attribution for the app log (adhoc #1587). Every line logSystem()
// records carries the file and line of the code that logged it, both in the
// Log view and in the plain-text network_log.txt on disk, so an entry can be
// traced back to the call that wrote it without grepping for its wording.
//
// The capture side is these two macros: as *default arguments* the compiler
// builtins expand at the call site, not here, so ~430 existing logSystem()
// calls keep working unchanged and each still reports its own location.

#include <QString>
#include <QStringList>

#if defined(__GNUC__) || defined(__clang__) || \
    (defined(_MSC_VER) && _MSC_VER >= 1926)
#define FORKMESH_LOG_SOURCE_FILE __builtin_FILE()
#define FORKMESH_LOG_SOURCE_LINE __builtin_LINE()
#else
// No builtins: entries simply carry no origin rather than failing to build.
#define FORKMESH_LOG_SOURCE_FILE nullptr
#define FORKMESH_LOG_SOURCE_LINE 0
#endif

namespace forkmesh {

// "qt_client/src/MainWindowRepos.cpp" for the absolute path a compiler builtin
// reports. The build machine's checkout prefix is stripped (it means nothing on
// another machine, and a full path would dwarf the log line it annotates); a
// path from outside the checkout keeps its last two components.
QString logSourceRelativePath(const char *absolutePath);

// The "  [qt_client/src/Foo.cpp:42]" tail appended to a stored log line, or an
// empty string when there is no usable location.
QString logSourceSuffix(const QString &relativePath, int line);

// Splits the tail back off a stored message. Returns false — leaving `body`
// as the whole message — for lines that carry none, so history written before
// this existed (and anything a message happens to end in brackets with) still
// renders and classifies exactly as it did.
bool splitLogSource(const QString &message, QString *body, QString *path,
                    int *line);

// The message without its source tail. Used everywhere a line is classified or
// matched by its text, so the appended location cannot leak into a badge.
QString logMessageBody(const QString &message);

// "Foo.cpp:42" — what the Log view shows for a location, the full path being
// the anchor's business rather than the reader's.
QString logSourceLabel(const QString &relativePath, int line);

// Where `relativePath` actually lives on this machine: the build-time checkout
// if it is still there, else `extraRoots` in order (the open repository, most
// usefully). Empty when nothing matches, which is the honest answer on a
// machine that only has the binary.
QString resolveLogSourcePath(const QString &relativePath,
                             const QStringList &extraRoots = {});

// The checkout this binary was built from, empty if the compiler reported
// relative paths. Exposed for tests.
QString logSourceBuildRoot();

} // namespace forkmesh
