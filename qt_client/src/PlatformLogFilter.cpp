#include "PlatformLogFilter.h"

#include <QLatin1String>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>

namespace forkmesh {
namespace {

QtMessageHandler g_previousMessageHandler = nullptr;

void filterPlatformNoise(QtMsgType type, const QMessageLogContext &context,
                         const QString &message)
{
    if (isPlatformSizeHintNoise(message))
        return;
    if (isFontDatabaseNoise(message))
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

} // namespace forkmesh
