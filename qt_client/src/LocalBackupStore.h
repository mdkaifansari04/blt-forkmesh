#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// Hourly, on-disk snapshots of the live ForkMesh database.
//
// "The database" here is the same thing Settings -> Data exports: the
// application-data directory (identity key, account, chat history, agents,
// issues, pull requests and every other local store) plus the QSettings
// preferences file. The bulky, re-downloadable repository mirrors and browse
// caches are deliberately left out, so a snapshot stays small enough to take
// every hour and keep a full day of history.
//
// Snapshots are plain `tar czf` archives named forkmesh-backup-<stamp>.tar.gz,
// which means a user can recover one by hand (tar xzf) even without the app —
// and the app itself restores them through the existing import path.
//
// These helpers are Qt Core only (no widgets, no MainWindow) so the naming,
// listing, retention and due-time rules are covered by forkmesh-tests.

namespace forkmesh {

// One snapshot sitting in the backup folder.
struct BackupSnapshot {
    QString path;     // absolute path to the .tar.gz
    QString fileName; // base name
    QDateTime taken;  // parsed from the file name (local time)
    qint64 bytes = 0;
};

// How often an automatic backup is taken, and how many are kept by default
// (24 hourly snapshots = a rolling day of history).
constexpr qint64 kBackupIntervalMs = 60 * 60 * 1000;
constexpr int kBackupKeepDefault = 24;
constexpr int kBackupKeepMin = 1;
constexpr int kBackupKeepMax = 240;

// The default for backup/hourlyEnabled when the user has never chosen: on only
// for control nodes — the installs holding a Cloudflare API token, and with it
// the deploy credentials and worker state that nothing else in the mesh can
// hand back. Everywhere else backups start off and are opted into from
// Settings -> Data: a rolling day of hourly ~1GB tarballs filled several small
// VPS disks outright, and that cost isn't worth paying on every desktop and
// mirror by default. `cloudflareApiToken` is this node's stored token (see
// control::cloudflareApiTokenFromVariables); blank/whitespace means no token.
bool autoBackupDefault(const QString &cloudflareApiToken);

// Where snapshots live: <app data>/backups. Kept inside the app-data dir so it
// travels with the rest of ForkMesh's storage and shows up in the Data tab —
// callers must therefore exclude it when packing (see configArchiveTarArgs).
QString defaultBackupRoot();

// forkmesh-backup-yyyyMMdd-HHmmss.tar.gz
QString backupFileName(const QDateTime &when);
// The inverse: an invalid QDateTime for anything that isn't one of ours.
QDateTime backupTimestampFromName(const QString &fileName);

// Every snapshot in `root`, newest first. Files that don't match our naming are
// ignored, so an unrelated archive dropped in the folder can't be restored or
// pruned by mistake.
QList<BackupSnapshot> listBackups(const QString &root);

// The snapshots that must be deleted to keep at most `keep` newest ones.
// Expects the newest-first order listBackups() returns.
QList<BackupSnapshot> backupsToPrune(const QList<BackupSnapshot> &snapshots,
                                     int keep);

// True when the next hourly snapshot is due. An invalid/absent `last` (no
// snapshot yet) is always due, and a timestamp in the future (clock moved
// backwards) is treated as "just taken" rather than firing a burst.
bool backupIsDue(const QDateTime &last, const QDateTime &now,
                 qint64 intervalMs = kBackupIntervalMs);

// The `tar` argv that packs the configuration into `archivePath`.
// `excludeRoots` are absolute paths (mirror root, browse cache, the backup
// folder itself) that are skipped when they live inside the app-data tree;
// tar requires every --exclude to precede the members, which this guarantees.
// Returns an empty list when neither the app-data dir nor the settings file
// exists, i.e. there is nothing worth archiving.
QStringList configArchiveTarArgs(const QString &archivePath,
                                 const QString &appDataDir,
                                 const QString &configFile,
                                 const QStringList &excludeRoots);

} // namespace forkmesh
