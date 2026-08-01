#pragma once

#include <QString>
#include <QtGlobal>






namespace SystemStats {


qint64 residentBytes();


qint64 totalMemoryBytes();





qint64 availableMemoryBytes();



qint64 diskTotalBytes(const QString &path);
qint64 diskFreeBytes(const QString &path);







double hostCpuPercent();







double cpuPercent();


QString formatBytes(qint64 bytes);


struct DescendantLoad {
    int count = 0;
    qint64 residentBytes = 0;
};





DescendantLoad descendantsNamed(qint64 rootPid, const QString &comm);

}
