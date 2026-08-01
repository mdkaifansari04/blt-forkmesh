#pragma once

#include <QString>















namespace forkmesh {







void installCrashHandler(const QString &crashLogPath = QString(),
                         const QString &mainLogPath = QString());




void setCrashContext(const QString &context);





void setTerminationSignalSurvivalEnabled(bool enabled);



void logDiagnosticEvent(const QString &context, const QString &details);








void logCaughtFault(const QString &context, const QString &what);

}
