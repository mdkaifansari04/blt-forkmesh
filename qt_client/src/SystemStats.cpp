#include "SystemStats.h"

#include <QByteArray>
#include <QFile>
#include <QList>

#include <chrono>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

// Sample cache for the CPU delta. A single process, so plain file-statics are
// enough; cpuPercent() compares each reading against the previous one.
std::chrono::steady_clock::time_point g_lastWall{};
double g_lastCpuSecs = -1.0;  // < 0 until the first sample establishes a baseline
double g_lastPercent = 0.0;

// Whole-host CPU accounting from /proc/stat's aggregate line, kept separate from
// the process-CPU cache above so the two readings don't disturb each other.
double g_lastHostBusy = -1.0;   // busy jiffies at the previous host sample
double g_lastHostTotal = -1.0;  // total jiffies at the previous host sample
double g_lastHostPercent = 0.0;

// Total CPU seconds (user + system) this process has consumed, or -1 if unknown.
double processCpuSeconds()
{
#if defined(Q_OS_LINUX)
    // /proc/self/stat is the cheapest accurate source. The comm field (2nd) is
    // wrapped in parens and may itself contain spaces, so parse after the last
    // ')': in that tail, index 11 == utime and index 12 == stime (clock ticks).
    QFile stat(QStringLiteral("/proc/self/stat"));
    if (stat.open(QIODevice::ReadOnly)) {
        const QByteArray data = stat.readAll();
        const int rp = data.lastIndexOf(')');
        if (rp > 0) {
            const QList<QByteArray> fields = data.mid(rp + 2).split(' ');
            if (fields.size() > 12) {
                bool okU = false, okS = false;
                const qulonglong utime = fields.at(11).toULongLong(&okU);
                const qulonglong stime = fields.at(12).toULongLong(&okS);
                const long hz = sysconf(_SC_CLK_TCK);
                if (okU && okS && hz > 0)
                    return double(utime + stime) / double(hz);
            }
        }
    }
#endif
#if defined(Q_OS_UNIX)
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        return double(ru.ru_utime.tv_sec) + double(ru.ru_utime.tv_usec) / 1e6 +
               double(ru.ru_stime.tv_sec) + double(ru.ru_stime.tv_usec) / 1e6;
#endif
    return -1.0;
}

} // namespace

qint64 SystemStats::residentBytes()
{
#if defined(Q_OS_LINUX)
    // /proc/self/statm: "size resident shared ..." in pages.
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (statm.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> parts = statm.readAll().trimmed().split(' ');
        if (parts.size() >= 2) {
            bool ok = false;
            const qulonglong pages = parts.at(1).toULongLong(&ok);
            const long pageSize = sysconf(_SC_PAGESIZE);
            if (ok && pageSize > 0)
                return qint64(pages) * qint64(pageSize);
        }
    }
#endif
#if defined(Q_OS_UNIX)
    // Fallback: peak RSS from getrusage (ru_maxrss is bytes on macOS, KiB else).
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(Q_OS_MACOS)
        return qint64(ru.ru_maxrss);
#else
        return qint64(ru.ru_maxrss) * 1024;
#endif
    }
#endif
    return 0;
}

qint64 SystemStats::totalMemoryBytes()
{
#if defined(Q_OS_UNIX) && defined(_SC_PHYS_PAGES)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0)
        return qint64(pages) * qint64(pageSize);
#endif
    return 0;
}

qint64 SystemStats::availableMemoryBytes()
{
#if defined(Q_OS_LINUX)
    // MemAvailable is the kernel's own estimate of how much can be allocated
    // without swapping (reclaimable cache included), reported in KiB.
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (meminfo.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = meminfo.readAll().split('\n');
        for (const QByteArray &line : lines) {
            if (!line.startsWith("MemAvailable:"))
                continue;
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() >= 2) {
                bool ok = false;
                const qulonglong kib = parts.at(1).toULongLong(&ok);
                if (ok)
                    return qint64(kib) * 1024;
            }
        }
    }
#endif
#if defined(Q_OS_UNIX) && defined(_SC_AVPHYS_PAGES)
    const long pages = sysconf(_SC_AVPHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0)
        return qint64(pages) * qint64(pageSize);
#endif
    return 0;
}

qint64 SystemStats::diskTotalBytes(const QString &path)
{
#if defined(Q_OS_UNIX)
    struct statvfs vfs{};
    if (!path.isEmpty() && statvfs(path.toLocal8Bit().constData(), &vfs) == 0)
        return qint64(vfs.f_blocks) * qint64(vfs.f_frsize);
#else
    Q_UNUSED(path);
#endif
    return 0;
}

qint64 SystemStats::diskFreeBytes(const QString &path)
{
#if defined(Q_OS_UNIX)
    struct statvfs vfs{};
    if (!path.isEmpty() && statvfs(path.toLocal8Bit().constData(), &vfs) == 0)
        // f_bavail is the space available to a non-root process — the figure a
        // user actually has to spend, which is what the bar should reflect.
        return qint64(vfs.f_bavail) * qint64(vfs.f_frsize);
#else
    Q_UNUSED(path);
#endif
    return 0;
}

double SystemStats::hostCpuPercent()
{
#if defined(Q_OS_LINUX)
    QFile stat(QStringLiteral("/proc/stat"));
    if (stat.open(QIODevice::ReadOnly)) {
        // The first line "cpu  user nice system idle iowait irq softirq steal..."
        // aggregates every core. Busy == total minus the idle+iowait fields.
        const QByteArray first = stat.readLine();
        if (first.startsWith("cpu ")) {
            const QList<QByteArray> fields = first.simplified().split(' ');
            double total = 0.0, idle = 0.0;
            for (int i = 1; i < fields.size(); ++i) {
                bool ok = false;
                const double v = double(fields.at(i).toLongLong(&ok));
                if (!ok)
                    continue;
                total += v;
                if (i == 4 || i == 5) // idle + iowait
                    idle += v;
            }
            if (total > 0.0) {
                const double busy = total - idle;
                if (g_lastHostTotal < 0.0) {
                    g_lastHostBusy = busy;
                    g_lastHostTotal = total;
                    g_lastHostPercent = 0.0;
                    return g_lastHostPercent;
                }
                const double dTotal = total - g_lastHostTotal;
                const double dBusy = busy - g_lastHostBusy;
                g_lastHostBusy = busy;
                g_lastHostTotal = total;
                if (dTotal > 0.0) {
                    const double pct = dBusy / dTotal * 100.0;
                    g_lastHostPercent = pct < 0.0 ? 0.0 : (pct > 100.0 ? 100.0 : pct);
                }
                return g_lastHostPercent;
            }
        }
    }
#endif
    return -1.0;
}

double SystemStats::cpuPercent()
{
    const double cpu = processCpuSeconds();
    if (cpu < 0.0)
        return -1.0;
    const auto now = std::chrono::steady_clock::now();
    if (g_lastCpuSecs < 0.0) {
        // First sample: no interval yet, so report 0 and record the baseline.
        g_lastCpuSecs = cpu;
        g_lastWall = now;
        g_lastPercent = 0.0;
        return g_lastPercent;
    }
    const double wallSecs =
        std::chrono::duration<double>(now - g_lastWall).count();
    if (wallSecs < 0.05)
        return g_lastPercent; // too soon to resample without amplifying noise
    const double pct = (cpu - g_lastCpuSecs) / wallSecs * 100.0;
    g_lastCpuSecs = cpu;
    g_lastWall = now;
    g_lastPercent = pct < 0.0 ? 0.0 : pct;
    return g_lastPercent;
}

QString SystemStats::formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("0 B");
    const double kib = double(bytes) / 1024.0;
    if (kib < 1024.0)
        return QStringLiteral("%1 KB").arg(kib, 0, 'f', 0);
    const double mib = kib / 1024.0;
    if (mib < 1024.0)
        return QStringLiteral("%1 MB").arg(mib, 0, 'f', 1);
    const double gib = mib / 1024.0;
    return QStringLiteral("%1 GB").arg(gib, 0, 'f', 2);
}
