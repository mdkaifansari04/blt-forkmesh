#pragma once








#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class QByteArray;

namespace forkmesh {






struct SunburstNode {
    QString name;
    qint64 size = 0;
    int fileCount = 0;
    QList<SunburstNode> children;
};



constexpr int kSizeMapMaxDepth = 8;

struct DirectorySizeScanOptions {



    QSet<QString> pruned;
    int maxDepth = kSizeMapMaxDepth;
};

struct DirectorySizeScanResult {
    SunburstNode root;


    int unreadableDirs = 0;

    QStringList unreadableSample;
};





using DirectorySizeScanProgress =
    std::function<void(const QString &path, qint64 bytes, int files)>;





using DirectorySizeScanCancel = std::function<bool()>;





DirectorySizeScanResult
scanDirectorySizes(const QString &path, const DirectorySizeScanOptions &options,
                   const DirectorySizeScanProgress &progress = {},
                   const DirectorySizeScanCancel &canceled = {});









bool scanNeedsElevation(const QString &path, const QSet<QString> &pruned);






QSet<QString> mountPointsFromMountTable(const QByteArray &table);



QSet<QString> systemMountPoints();





QByteArray encodeScanRequest(const QString &path,
                             const DirectorySizeScanOptions &options);
bool decodeScanRequest(const QByteArray &payload, QString *path,
                       DirectorySizeScanOptions *options);
QByteArray encodeScanResult(const DirectorySizeScanResult &result);
bool decodeScanResult(const QByteArray &payload, DirectorySizeScanResult *result);






QByteArray encodeScanProgress(const QString &path, qint64 bytes, int files);
bool decodeScanProgress(const QByteArray &line, QString *path, qint64 *bytes,
                        int *files);



bool runningAsRoot();

}
