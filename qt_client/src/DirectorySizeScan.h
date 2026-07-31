#pragma once

// Working-tree / on-disk size scan behind the Size map tab (adhoc #189).
// It lives outside MainWindow because two very different callers need the
// exact same walk: the GUI (on a QtConcurrent thread) and the app re-executed
// as root through pkexec/sudo when a folder holds directories this user cannot
// read (adhoc #76). Keeping one implementation means the elevated rescan
// produces a tree the chart can swap in unchanged.

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class QByteArray;

namespace forkmesh {

// One entry in the Size map tab's tree: total bytes of everything beneath it,
// with subdirectories and direct files as children (largest first,
// adhoc #189/#262). A file is a leaf — no children — so the chart offers zoom
// only on directories, and zooming into a files-only directory shows one slice
// per file, matching the website's size map.
struct SunburstNode {
    QString name;
    qint64 size = 0;
    int fileCount = 0;
    QList<SunburstNode> children;
};

// Depth cap: deeper entries still count toward every ancestor's size, they
// just stop producing children of their own.
constexpr int kSizeMapMaxDepth = 8;

struct DirectorySizeScanOptions {
    // Absolute paths never descended into: nested mount points (so the scan
    // stays on one filesystem like `du -x`) and, when the toggle is on, the
    // .gitignored paths git listed.
    QSet<QString> pruned;
    int maxDepth = kSizeMapMaxDepth;
};

struct DirectorySizeScanResult {
    SunburstNode root;
    // Directories skipped because this user may not list them. Non-zero is
    // what offers the "Scan as administrator" rescan.
    int unreadableDirs = 0;
    // First few unreadable paths, for the status line.
    QStringList unreadableSample;
};

// "Where is it right now" callback for a running scan (adhoc #112): the
// absolute directory being walked, plus the bytes and files counted so far.
// Invoked on whichever thread is scanning, rate-limited to a few times a second
// so a label can show it without relaying out on every folder.
using DirectorySizeScanProgress =
    std::function<void(const QString &path, qint64 bytes, int files)>;

// Raw on-disk bytes, .git excluded, symlinks skipped so link cycles can't loop
// or inflate the totals. Files become leaf children alongside subdirectories
// (adhoc #262). Safe to call from a worker thread: touches nothing but the
// filesystem.
DirectorySizeScanResult
scanDirectorySizes(const QString &path, const DirectorySizeScanOptions &options,
                   const DirectorySizeScanProgress &progress = {});

// True when a plain scan of this folder would visibly miss things: this user
// cannot list something directly inside it, so the map would be drawn without
// (say) /root and /var/lib. One level, no recursion, so it is cheap enough to
// gate a password prompt on the click that selected the folder (adhoc #112);
// anything unreadable deeper down still surfaces the after-the-fact rescan
// offer. Always false when this process is already root — there is nothing left
// to ask for. `pruned` is the scan's own prune set: paths it would skip anyway
// never justify a prompt.
bool scanNeedsElevation(const QString &path, const QSet<QString> &pruned);

// Every mount point in a Linux mountinfo/mtab table. Qt's
// QStorageInfo::mountedVolumes() hides the pseudo filesystems (/proc, /sys,
// /dev, …), which is exactly how /proc/kcore's fictional 128 TiB used to eat
// the whole map when sizing "/" (adhoc #76), so the mount table is parsed
// directly instead.
QSet<QString> mountPointsFromMountTable(const QByteArray &table);

// mountPointsFromMountTable() over /proc/self/mountinfo (falling back to
// /proc/mounts). Empty where neither exists.
QSet<QString> systemMountPoints();

// Wire format between the GUI and the elevated helper process: a JSON request
// in the file named on the command line, a compact binary tree on stdout (the
// tree can hold a million nodes when sizing "/", which JSON would not carry
// cheaply).
QByteArray encodeScanRequest(const QString &path,
                             const DirectorySizeScanOptions &options);
bool decodeScanRequest(const QByteArray &payload, QString *path,
                       DirectorySizeScanOptions *options);
QByteArray encodeScanResult(const DirectorySizeScanResult &result);
bool decodeScanResult(const QByteArray &payload, DirectorySizeScanResult *result);

// One live-progress line from the elevated helper (adhoc #112). stdout carries
// the binary tree, so these travel on stderr instead, each on its own line and
// behind a sentinel so pkexec's and sudo's own chatter is never read as
// progress. The path is percent-encoded because a filename may contain a
// newline, which would otherwise split one update into two.
QByteArray encodeScanProgress(const QString &path, qint64 bytes, int files);
bool decodeScanProgress(const QByteArray &line, QString *path, qint64 *bytes,
                        int *files);

// True when this process already has root's view of the filesystem, so there
// is nothing an elevated rescan could add.
bool runningAsRoot();

} // namespace forkmesh
