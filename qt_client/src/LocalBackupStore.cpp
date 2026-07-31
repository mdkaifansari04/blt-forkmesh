#include "LocalBackupStore.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

namespace {

const QString kBackupPrefix = QStringLiteral("forkmesh-backup-");
const QString kBackupSuffix = QStringLiteral(".tar.gz");
const QString kBackupStampFormat = QStringLiteral("yyyyMMdd-HHmmss");

} // namespace

namespace forkmesh {

bool autoBackupDefault(const QString &cloudflareApiToken)
{
    return !cloudflareApiToken.trimmed().isEmpty();
}

QString defaultBackupRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/backups");
}

QString backupFileName(const QDateTime &when)
{
    return kBackupPrefix + when.toString(kBackupStampFormat) + kBackupSuffix;
}

QDateTime backupTimestampFromName(const QString &fileName)
{
    if (!fileName.startsWith(kBackupPrefix) || !fileName.endsWith(kBackupSuffix))
        return QDateTime();
    const QString stamp = fileName.mid(kBackupPrefix.size(),
                                       fileName.size() - kBackupPrefix.size() -
                                           kBackupSuffix.size());
    // Pin the length as well as the format: QDateTime::fromString accepts a
    // prefix match, so without this a "…-20260728-101500-copy.tar.gz" would
    // parse and become restorable/prunable as if it were ours.
    if (stamp.size() != kBackupStampFormat.size())
        return QDateTime();
    return QDateTime::fromString(stamp, kBackupStampFormat);
}

QList<BackupSnapshot> listBackups(const QString &root)
{
    QList<BackupSnapshot> snapshots;
    const QFileInfoList entries =
        QDir(root).entryInfoList(QDir::Files, QDir::NoSort);
    for (const QFileInfo &fi : entries) {
        const QDateTime taken = backupTimestampFromName(fi.fileName());
        if (!taken.isValid())
            continue;
        snapshots.append(
            {fi.absoluteFilePath(), fi.fileName(), taken, fi.size()});
    }
    std::sort(snapshots.begin(), snapshots.end(),
              [](const BackupSnapshot &a, const BackupSnapshot &b) {
                  if (a.taken == b.taken)
                      return a.fileName > b.fileName;
                  return a.taken > b.taken; // newest first
              });
    return snapshots;
}

QList<BackupSnapshot> backupsToPrune(const QList<BackupSnapshot> &snapshots,
                                     int keep)
{
    // Never prune down to nothing, however the setting was tampered with: the
    // newest snapshot is the one thing recovery depends on.
    const int floor = std::max(keep, kBackupKeepMin);
    if (snapshots.size() <= floor)
        return {};
    return snapshots.mid(floor);
}

bool backupIsDue(const QDateTime &last, const QDateTime &now, qint64 intervalMs)
{
    if (!last.isValid())
        return true; // nothing on disk yet
    const qint64 elapsed = last.msecsTo(now);
    if (elapsed < 0)
        return false; // clock went backwards; wait for the hourly timer instead
    return elapsed >= intervalMs;
}

QStringList configArchiveTarArgs(const QString &archivePath,
                                 const QString &appDataDir,
                                 const QString &configFile,
                                 const QStringList &excludeRoots)
{
    const QString appData = QDir(appDataDir).absolutePath();
    const QString appParent = QFileInfo(appData).absolutePath();
    const bool haveApp = QFileInfo::exists(appData);
    const bool haveConfig = QFileInfo::exists(configFile);
    if (!haveApp && !haveConfig)
        return {};

    QStringList args;
    args << QStringLiteral("-czf") << archivePath;
    // Every --exclude has to precede the members it applies to, and tar matches
    // the pattern against the archived (relative) path, so translate each root
    // into a path relative to the -C directory. Roots outside the app-data tree
    // are never archived in the first place and need no exclusion.
    for (const QString &root : excludeRoots) {
        if (root.isEmpty())
            continue;
        const QString rel =
            QDir(appParent).relativeFilePath(QDir(root).absolutePath());
        if (rel.isEmpty() || rel.startsWith(QStringLiteral("..")))
            continue;
        args << (QStringLiteral("--exclude=") + rel);
    }
    if (haveApp)
        args << QStringLiteral("-C") << appParent << QFileInfo(appData).fileName();
    if (haveConfig)
        args << QStringLiteral("-C") << QFileInfo(configFile).absolutePath()
             << QFileInfo(configFile).fileName();
    return args;
}

} // namespace forkmesh
