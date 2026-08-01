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

    fprintf(stderr, "%s\n",
            qFormatLogMessage(type, context, message).toLocal8Bit().constData());
    fflush(stderr);
    if (type == QtFatalMsg)
        abort();
}

}

bool isPlatformSizeHintNoise(const QString &message)
{



    return message.contains(QLatin1String("propagateSizeHints"));
}

bool isFontDatabaseNoise(const QString &message)
{




    return message.startsWith(QLatin1String("OpenType support missing for"));
}

void installPlatformLogFilter()
{
    g_previousMessageHandler = qInstallMessageHandler(filterPlatformNoise);
}

}
