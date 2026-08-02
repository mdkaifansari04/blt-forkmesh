#include "DirectorySizeScan.h"

#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QUrl>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace forkmesh {
namespace {

constexpr int kUnreadableSampleLimit = 6;
constexpr quint32 kScanResultMagic = 0x464d535a; // "FMSZ"
constexpr qint32 kScanResultVersion = 1;
// Live-progress cadence: fast enough to read as motion, slow enough that a
// warm-cache walk spends its time on the filesystem rather than on formatting
// paths nobody could follow at that rate.
constexpr qint64 kProgressIntervalMs = 80;
const char kProgressSentinel[] = "FMSZ-PROGRESS ";

class ProgressEmitter;

// One scanning thread's slice of a running scan, and the unit behind the Size
// map's per-thread lines (adhoc #95). Claimed once per top-level task and then
// threaded down the recursion, so counting a file touches this thread-owned
// object and two atomics — no lock on the hot path, however many threads walk
// at once. Its rate limit is its own, so a thread deep in a slow tree cannot
// silence the lines of the threads beside it.
class ScanWorker
{
public:
    void countFile(qint64 size);
    // Called on entering every directory; most calls are dropped by the clock.
    void enter(const QString &path);
    // Called once the top-level tree this worker was given is fully counted.
    // Never dropped: it is what turns the line from "in this folder" into
    // "idle", so a thread that finished early does not sit on a stale path for
    // the rest of the scan.
    void finished(const QString &path);

private:
    friend class ProgressEmitter;
    ScanWorker(ProgressEmitter &owner, int index) : m_owner(owner), m_index(index)
    {
    }

    ProgressEmitter &m_owner;
    const int m_index;
    qint64 m_lastMs = -1; // -1: this worker has never reported
    qint64 m_bytes = 0;
    int m_files = 0;
};

// Rate limiter around the caller's progress callback, carrying the running
// totals the tree itself only knows once the recursion unwinds, and handing out
// the per-thread slots the updates are attributed to.
class ProgressEmitter
{
public:
    explicit ProgressEmitter(const DirectorySizeScanProgress &callback)
        : m_callback(callback)
    {
        m_clock.start();
    }

    // The slot belonging to the calling thread, created the first time that
    // thread asks. Pool threads are reused across the mapped tasks, so a thread
    // keeps one slot — and therefore one line — for the whole scan.
    ScanWorker &claim()
    {
        QMutexLocker lock(&m_mutex);
        QThread *thread = QThread::currentThread();
        const auto existing = m_byThread.constFind(thread);
        if (existing != m_byThread.constEnd())
            return *existing.value();
        m_workers.push_back(std::unique_ptr<ScanWorker>(
            new ScanWorker(*this, int(m_workers.size()))));
        ScanWorker *worker = m_workers.back().get();
        m_byThread.insert(thread, worker);
        m_workerCount.store(int(m_workers.size()), std::memory_order_relaxed);
        return *worker;
    }

    // A file sitting directly in the scanned folder: it belongs to the totals,
    // but there is no tree to walk and so no line to attribute it to.
    void countLooseFile(qint64 size) { countFile(size); }

private:
    friend class ScanWorker;

    void countFile(qint64 size)
    {
        m_bytes.fetch_add(size, std::memory_order_relaxed);
        m_files.fetch_add(1, std::memory_order_relaxed);
    }

    void report(ScanWorker &worker, const QString &path, bool idle)
    {
        if (!m_callback)
            return;
        const qint64 now = m_clock.elapsed();
        // Only this worker's thread touches its clock, so the decision to skip
        // an update — the overwhelmingly common case — costs no lock at all.
        // Going idle is never skipped: there is at most one of those per
        // top-level tree, and it is the update that stops a line going stale.
        if (!idle && worker.m_lastMs >= 0 &&
            now - worker.m_lastMs < kProgressIntervalMs)
            return;
        worker.m_lastMs = now;
        DirectorySizeScanProgressUpdate update;
        update.worker = worker.m_index;
        update.workers = m_workerCount.load(std::memory_order_relaxed);
        update.path = path;
        update.idle = idle;
        update.workerBytes = worker.m_bytes;
        update.workerFiles = worker.m_files;
        update.bytes = m_bytes.load(std::memory_order_relaxed);
        update.files = m_files.load(std::memory_order_relaxed);
        // Serialized so the callback never has to be re-entrant: the helper
        // writes a line to stderr from here and the GUI hops to its own thread,
        // and neither wants a second worker inside it. Cheap to hold — at a few
        // updates per second per thread this lock is nothing like the per-file
        // contention the counters above deliberately avoid.
        QMutexLocker lock(&m_callbackMutex);
        m_callback(update);
    }

    const DirectorySizeScanProgress &m_callback;
    QElapsedTimer m_clock;
    std::atomic<qint64> m_bytes{0};
    std::atomic<int> m_files{0};
    std::atomic<int> m_workerCount{0};
    QMutex m_mutex; // guards the slot table below
    QHash<QThread *, ScanWorker *> m_byThread;
    std::vector<std::unique_ptr<ScanWorker>> m_workers;
    QMutex m_callbackMutex;
};

void ScanWorker::countFile(qint64 size)
{
    m_bytes += size;
    ++m_files;
    m_owner.countFile(size);
}

void ScanWorker::enter(const QString &path)
{
    m_owner.report(*this, path, false);
}

void ScanWorker::finished(const QString &path)
{
    m_owner.report(*this, path, true);
}

// A directory needs both read (to list it) and execute (to stat what is in
// it); missing either is what turns a scan of "/" into a handful of slices for
// an unprivileged user.
bool canDescend(const QFileInfo &info)
{
    return info.isReadable() && info.isExecutable();
}

void scanInto(const QString &path, int depth,
              const DirectorySizeScanOptions &options,
              DirectorySizeScanResult &result, SunburstNode &node,
              ScanWorker &worker, const DirectorySizeScanCancel &canceled)
{
    if (canceled && canceled())
        return; // Stop was clicked: unwind without descending further
    worker.enter(path);
    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden |
        QDir::System | QDir::NoSymLinks);
    for (const QFileInfo &info : entries) {
        if (canceled && canceled())
            return;
        const QString absolute = info.absoluteFilePath();
        if (!options.pruned.isEmpty() && options.pruned.contains(absolute))
            continue;
        if (info.isDir()) {
            if (info.fileName() == QLatin1String(".git"))
                continue;
            SunburstNode child;
            child.name = info.fileName();
            if (canDescend(info)) {
                scanInto(absolute, depth + 1, options, result, child, worker,
                         canceled);
            } else {
                // Counted, not descended: the rescan-as-administrator offer is
                // built from exactly these.
                ++result.unreadableDirs;
                if (result.unreadableSample.size() < kUnreadableSampleLimit)
                    result.unreadableSample.append(absolute);
            }
            node.size += child.size;
            node.fileCount += child.fileCount;
            if (depth < options.maxDepth && child.size > 0)
                node.children.append(std::move(child));
        } else {
            node.size += info.size();
            node.fileCount += 1;
            worker.countFile(info.size());
            if (depth < options.maxDepth && info.size() > 0) {
                SunburstNode leaf;
                leaf.name = info.fileName();
                leaf.size = info.size();
                leaf.fileCount = 1;
                node.children.append(std::move(leaf));
            }
        }
    }
    std::sort(node.children.begin(), node.children.end(),
              [](const SunburstNode &a, const SunburstNode &b) {
                  return a.size > b.size;
              });
}

void writeNode(QDataStream &stream, const SunburstNode &node)
{
    stream << node.name << node.size << qint32(node.fileCount)
           << qint32(node.children.size());
    for (const SunburstNode &child : node.children)
        writeNode(stream, child);
}

bool readNode(QDataStream &stream, SunburstNode &node, int depth)
{
    if (depth > kSizeMapMaxDepth + 1)
        return false; // a malformed stream can't be walked into the stack
    qint32 fileCount = 0;
    qint32 childCount = 0;
    stream >> node.name >> node.size >> fileCount >> childCount;
    if (stream.status() != QDataStream::Ok || childCount < 0)
        return false;
    node.fileCount = fileCount;
    node.children.reserve(childCount);
    for (qint32 i = 0; i < childCount; ++i) {
        SunburstNode child;
        if (!readNode(stream, child, depth + 1))
            return false;
        node.children.append(std::move(child));
    }
    return true;
}

// mountinfo fields are escaped octally for the four characters that would
// otherwise break the space-separated layout.
QString unescapeMountField(const QString &field)
{
    QString out;
    out.reserve(field.size());
    for (int i = 0; i < field.size(); ++i) {
        if (field.at(i) == QLatin1Char('\\') && i + 3 < field.size()) {
            bool ok = false;
            const int code = QStringView(field).mid(i + 1, 3).toInt(&ok, 8);
            if (ok) {
                out.append(QChar(code));
                i += 3;
                continue;
            }
        }
        out.append(field.at(i));
    }
    return out;
}

} // namespace

DirectorySizeScanResult
scanDirectorySizes(const QString &path, const DirectorySizeScanOptions &options,
                   const DirectorySizeScanProgress &progress,
                   const DirectorySizeScanCancel &canceled)
{
    DirectorySizeScanResult result;
    result.root.name = QFileInfo(path).fileName();
    if (!canDescend(QFileInfo(path))) {
        result.unreadableDirs = 1;
        result.unreadableSample.append(QDir::cleanPath(path));
        return result;
    }
    ProgressEmitter emitter(progress);
    // Worker 0 is this thread — the one that reduces the partials below, and
    // the one blockingMapped() also runs tasks on — so the first per-thread line
    // is the scan's own.
    ScanWorker &rootWorker = emitter.claim();
    rootWorker.enter(path);
    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden |
        QDir::System | QDir::NoSymLinks);

    // A scan of / has several independent, very large top-level trees. Walking
    // them serially left most cores idle. Map those immediate children through
    // Qt's bounded global thread pool, then reduce their self-contained results
    // on this worker. Recursion below each child stays serial, avoiding an
    // unbounded task per directory and preserving filesystem locality.
    struct Partial {
        SunburstNode node;
        int unreadableDirs = 0;
        QStringList unreadableSample;
        bool include = false;
    };
    const QList<Partial> partials = QtConcurrent::blockingMapped(
        entries, [&](const QFileInfo &info) {
            Partial partial;
            if (canceled && canceled())
                return partial;
            const QString absolute = info.absoluteFilePath();
            if ((!options.pruned.isEmpty() && options.pruned.contains(absolute)) ||
                (info.isDir() && info.fileName() == QLatin1String(".git")))
                return partial;
            partial.node.name = info.fileName();
            if (info.isDir()) {
                if (canDescend(info)) {
                    // A slot is claimed only where there is a tree to walk, and
                    // per task rather than per directory: the pool hands a thread
                    // one top-level tree at a time and the slot it gets back is
                    // the one it used for the last tree, so its line follows it.
                    // Claiming for the tasks that cannot report — a plain file, a
                    // directory this user may not list — would leave numbered
                    // gaps in the lines that never fill in.
                    ScanWorker &worker = emitter.claim();
                    DirectorySizeScanResult childResult;
                    scanInto(absolute, 1, options, childResult, partial.node,
                             worker, canceled);
                    worker.finished(absolute);
                    partial.unreadableDirs = childResult.unreadableDirs;
                    partial.unreadableSample = childResult.unreadableSample;
                } else {
                    partial.unreadableDirs = 1;
                    partial.unreadableSample.append(absolute);
                }
            } else {
                partial.node.size = info.size();
                partial.node.fileCount = 1;
                emitter.countLooseFile(info.size());
            }
            partial.include = options.maxDepth > 0 && partial.node.size > 0;
            return partial;
        });
    // Nothing left to walk on this thread either: every line the display drew
    // ends on the worker that owns it saying so, none of them on a folder that
    // was finished with long before the scan was.
    rootWorker.finished(path);
    for (const Partial &partial : partials) {
        result.root.size += partial.node.size;
        result.root.fileCount += partial.node.fileCount;
        result.unreadableDirs += partial.unreadableDirs;
        for (const QString &sample : partial.unreadableSample) {
            if (result.unreadableSample.size() >= kUnreadableSampleLimit)
                break;
            result.unreadableSample.append(sample);
        }
        if (partial.include)
            result.root.children.append(partial.node);
    }
    std::sort(result.root.children.begin(), result.root.children.end(),
              [](const SunburstNode &a, const SunburstNode &b) {
                  return a.size > b.size;
              });
    return result;
}

bool scanNeedsElevation(const QString &path, const QSet<QString> &pruned)
{
    if (runningAsRoot())
        return false; // already root's view of the disk
    if (!canDescend(QFileInfo(path)))
        return true;
    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System |
        QDir::NoSymLinks);
    for (const QFileInfo &info : entries) {
        if (info.fileName() == QLatin1String(".git"))
            continue; // excluded from the map either way
        if (pruned.contains(info.absoluteFilePath()))
            continue; // never descended into anyway
        if (!canDescend(info))
            return true;
    }
    return false;
}

QSet<QString> mountPointsFromMountTable(const QByteArray &table)
{
    QSet<QString> mounts;
    for (const QByteArray &rawLine : table.split('\n')) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty())
            continue;
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // mountinfo: "36 25 0:29 / /proc rw,… - proc proc rw" — the mount point
        // is field 5. mtab/proc-mounts: "proc /proc proc rw,… 0 0" — field 2.
        // The separating "-" only exists in mountinfo, so it tells them apart.
        const bool mountInfo = fields.contains(QStringLiteral("-"));
        const int index = mountInfo ? 4 : 1;
        if (fields.size() <= index)
            continue;
        const QString mount = unescapeMountField(fields.at(index));
        if (!mount.startsWith(QLatin1Char('/')))
            continue;
        mounts.insert(QDir::cleanPath(mount));
    }
    return mounts;
}

QSet<QString> systemMountPoints()
{
    for (const QString &source : {QStringLiteral("/proc/self/mountinfo"),
                                  QStringLiteral("/proc/mounts")}) {
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QSet<QString> mounts = mountPointsFromMountTable(file.readAll());
        if (!mounts.isEmpty())
            return mounts;
    }
    return {};
}

QByteArray encodeScanRequest(const QString &path,
                             const DirectorySizeScanOptions &options)
{
    QJsonArray pruned;
    for (const QString &entry : options.pruned)
        pruned.append(entry);
    const QJsonObject request{{QStringLiteral("path"), path},
                              {QStringLiteral("maxDepth"), options.maxDepth},
                              {QStringLiteral("pruned"), pruned}};
    return QJsonDocument(request).toJson(QJsonDocument::Compact);
}

bool decodeScanRequest(const QByteArray &payload, QString *path,
                       DirectorySizeScanOptions *options)
{
    const QJsonObject request = QJsonDocument::fromJson(payload).object();
    const QString requested = request.value(QStringLiteral("path")).toString();
    if (requested.isEmpty())
        return false;
    if (path)
        *path = requested;
    if (options) {
        const int depth =
            request.value(QStringLiteral("maxDepth")).toInt(kSizeMapMaxDepth);
        options->maxDepth = qBound(0, depth, kSizeMapMaxDepth);
        options->pruned.clear();
        const QJsonArray pruned = request.value(QStringLiteral("pruned")).toArray();
        for (const QJsonValue &value : pruned) {
            const QString entry = value.toString();
            if (!entry.isEmpty())
                options->pruned.insert(entry);
        }
    }
    return true;
}

QByteArray encodeScanResult(const DirectorySizeScanResult &result)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kScanResultMagic << kScanResultVersion
           << qint32(result.unreadableDirs) << result.unreadableSample;
    writeNode(stream, result.root);
    return payload;
}

bool decodeScanResult(const QByteArray &payload, DirectorySizeScanResult *result)
{
    QDataStream stream(payload);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    qint32 version = 0;
    qint32 unreadable = 0;
    QStringList sample;
    stream >> magic >> version >> unreadable >> sample;
    if (stream.status() != QDataStream::Ok || magic != kScanResultMagic ||
        version != kScanResultVersion)
        return false;
    DirectorySizeScanResult decoded;
    decoded.unreadableDirs = unreadable;
    decoded.unreadableSample = sample;
    if (!readNode(stream, decoded.root, 0))
        return false;
    if (result)
        *result = std::move(decoded);
    return true;
}

QByteArray encodeScanProgress(const DirectorySizeScanProgressUpdate &update)
{
    // "<bytes> <files> <worker> <workers> <workerBytes> <workerFiles> <idle>
    // <path>". Slashes stay literal so the line is still readable when the
    // helper is run by hand from a terminal.
    QByteArray line(kProgressSentinel);
    for (qint64 number : {update.bytes, qint64(update.files), qint64(update.worker),
                          qint64(update.workers), update.workerBytes,
                          qint64(update.workerFiles), qint64(update.idle ? 1 : 0)})
        line += QByteArray::number(qMax<qint64>(0, number)) + ' ';
    return line + QUrl::toPercentEncoding(update.path, QByteArrayLiteral("/")) +
           '\n';
}

bool decodeScanProgress(const QByteArray &line,
                        DirectorySizeScanProgressUpdate *update)
{
    QByteArray body = line.trimmed();
    if (!body.startsWith(kProgressSentinel))
        return false;
    body.remove(0, int(qstrlen(kProgressSentinel)));
    // The path is percent-encoded, so it holds no spaces: every field before the
    // last one is a counter. Reading however many arrived — rather than exactly
    // seven — keeps the two-counter lines of an older helper (one left on disk
    // by a half-applied update, re-executed for the elevated scan) readable as
    // single-worker progress instead of dropping them as chatter.
    const QList<QByteArray> fields = body.split(' ');
    if (fields.size() < 3)
        return false;
    QList<qint64> counters;
    for (int i = 0; i < fields.size() - 1; ++i) {
        bool ok = false;
        const qint64 value = fields.at(i).toLongLong(&ok);
        if (!ok || value < 0)
            return false;
        counters.append(value);
    }
    const QString path =
        QString::fromUtf8(QByteArray::fromPercentEncoding(fields.last()));
    if (path.isEmpty())
        return false;
    if (update) {
        const auto counter = [&counters](int index, qint64 fallback) {
            return index < counters.size() ? counters.at(index) : fallback;
        };
        update->bytes = counters.at(0);
        update->files = int(counters.at(1));
        update->worker = int(counter(2, 0));
        update->workers = int(counter(3, 1));
        update->workerBytes = counter(4, counters.at(0));
        update->workerFiles = int(counter(5, counters.at(1)));
        update->idle = counter(6, 0) != 0;
        update->path = path;
    }
    return true;
}

bool runningAsRoot()
{
#ifdef Q_OS_UNIX
    return ::geteuid() == 0;
#else
    return false;
#endif
}

} // namespace forkmesh
