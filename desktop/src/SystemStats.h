#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

namespace SystemStats {

qint64 residentBytes();

qint64 totalMemoryBytes();

qint64 availableMemoryBytes();

qint64 totalSwapBytes();
qint64 freeSwapBytes();

qint64 diskTotalBytes(const QString &path);
qint64 diskFreeBytes(const QString &path);

double hostCpuPercent();

double cpuPercent();

QString formatBytes(qint64 bytes);

struct DescendantLoad {
    int count = 0;             // matching processes below the root
    qint64 residentBytes = 0;  // their combined RSS
};

struct DescendantProcess {
    qint64 pid = 0;
    qint64 parentPid = 0;
    QString command;
};

QList<DescendantProcess> descendantProcesses(qint64 rootPid);

DescendantLoad descendantsNamed(qint64 rootPid, const QString &comm);


int openFileCount();

int openFileSoftLimit();
int openFileHardLimit();

int threadCount();

int raiseOpenFileLimit();

} // namespace SystemStats
