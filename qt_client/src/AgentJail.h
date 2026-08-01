#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QStringList>







namespace AgentJail {










inline QString wrapCommand(const QString &command, int memoryMb)
{
    if (memoryMb <= 0 || command.trimmed().isEmpty())
        return command;
    const qint64 kb = qint64(memoryMb) * 1024;
    return QStringLiteral("ulimit -d %1 2>/dev/null; %2")
        .arg(QString::number(kb), command);
}




inline QStringList envEntries(const QString &jailDir)
{
    const QString tmp = jailDir + QStringLiteral("/tmp");
    const QString cache = jailDir + QStringLiteral("/cache");
    QDir().mkpath(tmp);
    QDir().mkpath(cache);
    return {QStringLiteral("TMPDIR=") + tmp, QStringLiteral("TMP=") + tmp,
            QStringLiteral("TEMP=") + tmp,
            QStringLiteral("XDG_CACHE_HOME=") + cache};
}



inline QString sessionJailDir(int sessionId)
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
           QStringLiteral("/forkmesh-agent-jails/s%1").arg(sessionId);
}

}
