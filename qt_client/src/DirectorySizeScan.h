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

// One "where is it right now" update from a running scan (adhoc #112). The walk
// is parallel — the scanned folder's immediate children are mapped across the
// thread pool — so each update names the worker that produced it, which is what
// lets the tab draw one live line per scanning thread (adhoc #95) instead of a
// single line flickering between unrelated trees.
struct DirectorySizeScanProgressUpdate {
    // 0-based slot, stable for one thread for the length of one scan: worker 0
    // is the thread that called scanDirectorySizes. A pool thread that finishes
    // one top-level tree and picks up the next keeps its slot, so the number of
    // lines is the number of threads, not the number of folders.
    int worker = 0;
    // Slots handed out so far — the line count the display should hold.
    int workers = 1;
    // Absolute directory this worker is inside right now — or, when idle, the
    // last one it walked.
    QString path;
    // Set on the update a worker sends as it finishes a top-level tree: it has
    // no work in hand until the pool gives it the next one. Without it a thread
    // that finished early would leave its line frozen on a folder it left long
    // ago, reading as a stall. Always delivered, never dropped by the rate limit.
    bool idle = false;
    // Counted by this worker alone, so each line carries its own progress.
    qint64 workerBytes = 0;
    int workerFiles = 0;
    // Counted by the whole scan, for the summary line above the per-thread ones.
    qint64 bytes = 0;
    int files = 0;
};

// Progress callback for a running scan. Invoked on whichever thread is
// scanning, but never by two of them at once (the scan serializes the calls),
// so an implementation only has to be safe against being entered from an
// arbitrary thread — not against itself. Rate-limited per worker to a few times
// a second, so a label can show it without relaying out on every folder.
using DirectorySizeScanProgress =
    std::function<void(const DirectorySizeScanProgressUpdate &update)>;

// "Give up now" poll for the Size map tab's Stop button: checked once per
// directory, the same cadence as the progress callback, so a click unwinds
// the recursion in about one progress tick rather than waiting for the rest
// of the tree. Empty means never cancel.
using DirectorySizeScanCancel = std::function<bool()>;

// Raw on-disk bytes, .git excluded, symlinks skipped so link cycles can't loop
// or inflate the totals. Files become leaf children alongside subdirectories
// (adhoc #262). Safe to call from a worker thread: touches nothing but the
// filesystem.
DirectorySizeScanResult
scanDirectorySizes(const QString &path, const DirectorySizeScanOptions &options,
                   const DirectorySizeScanProgress &progress = {},
                   const DirectorySizeScanCancel &canceled = {});

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
// progress. The path goes last and percent-encoded: a filename may contain a
// newline, which would otherwise split one update into two, or a space, which
// would be read as another counter. Everything before it is a counter, so a
// line from an older helper — one left on disk by a half-applied update and
// re-executed for the elevated scan — still reads as single-worker progress.
QByteArray encodeScanProgress(const DirectorySizeScanProgressUpdate &update);
bool decodeScanProgress(const QByteArray &line,
                        DirectorySizeScanProgressUpdate *update);

// True when this process already has root's view of the filesystem, so there
// is nothing an elevated rescan could add.
bool runningAsRoot();

} // namespace forkmesh
