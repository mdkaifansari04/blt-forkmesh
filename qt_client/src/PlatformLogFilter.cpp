#include "PlatformLogFilter.h"

#include <QLatin1String>
#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>

namespace forkmesh {
namespace {

QtMessageHandler g_previousMessageHandler = nullptr;

QMutex g_sinkMutex;
AppLogSink g_sink;
bool g_keepConsoleEcho = false;
// A sink lands in the app's log, and logging can itself warn. Sinking that
// warning would re-enter the sink from inside it, so the nested message takes
// the console path instead.
thread_local bool g_insideSink = false;

// Hands the message to the registered sink. Returns true once the sink has it
// and the console copy should be dropped.
bool routeToAppLog(QtMsgType type, const QMessageLogContext &context,
                   const QString &message)
{
    if (type == QtFatalMsg || g_insideSink)
        return false;
    // Held across the call so clearAppLogSink() cannot return — and the sink's
    // callee cannot be destroyed — while a worker thread is inside the sink.
    QMutexLocker lock(&g_sinkMutex);
    if (!g_sink)
        return false;
    struct ReentryGuard {
        ReentryGuard() { g_insideSink = true; }
        ~ReentryGuard() { g_insideSink = false; }
    } guard;
    // context.file points at a string literal in the emitting translation unit
    // (or is null in a build without QT_MESSAGELOGCONTEXT). Copied here rather
    // than passed on as a pointer: the sink may queue the message to another
    // thread, and a literal from a plugin that later unloads would outlive it.
    g_sink(type, message,
           context.file ? QString::fromUtf8(context.file) : QString(),
           context.line);
    return !g_keepConsoleEcho;
}

void filterPlatformNoise(QtMsgType type, const QMessageLogContext &context,
                         const QString &message)
{
    if (isPlatformSizeHintNoise(message))
        return;
    if (isFontDatabaseNoise(message))
        return;
    if (routeToAppLog(type, context, message))
        return;
    if (g_previousMessageHandler) {
        g_previousMessageHandler(type, context, message);
        return;
    }
    // No prior handler installed: replicate Qt's default (stderr, abort on fatal).
    fprintf(stderr, "%s\n",
            qFormatLogMessage(type, context, message).toLocal8Bit().constData());
    fflush(stderr);
    if (type == QtFatalMsg)
        abort();
}

} // namespace

bool isPlatformSizeHintNoise(const QString &message)
{
    // The text comes straight from QPlatformWindow::propagateSizeHints()'s base
    // implementation; the function name in it is stable across Qt versions and
    // is what we key on.
    return message.contains(QLatin1String("propagateSizeHints"));
}

bool isFontDatabaseNoise(const QString &message)
{
    // Text straight from QFontDatabase's loadSingleEngine(); the family name and
    // script number vary, the prefix does not. Matching the prefix rather than
    // the qt.text.font.db category keeps this testable with a plain qWarning()
    // and still can't swallow an app message: nothing here logs that phrase.
    return message.startsWith(QLatin1String("OpenType support missing for"));
}

void installPlatformLogFilter()
{
    g_previousMessageHandler = qInstallMessageHandler(filterPlatformNoise);
}

void setAppLogSink(AppLogSink sink, bool keepConsoleEcho)
{
    QMutexLocker lock(&g_sinkMutex);
    g_sink = std::move(sink);
    g_keepConsoleEcho = keepConsoleEcho;
}

void clearAppLogSink()
{
    QMutexLocker lock(&g_sinkMutex);
    g_sink = {};
    g_keepConsoleEcho = false;
}

} // namespace forkmesh
