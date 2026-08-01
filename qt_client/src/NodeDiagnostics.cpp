#include "NodeDiagnostics.h"

#include "SystemStats.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLocale>
#include <algorithm>
#include <climits>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace NodeDiagnostics {

namespace {

constexpr qint64 kDayMs = 24 * 60 * 60 * 1000;
// Below this the free-space curve is just normal churn, not a leak worth a row.
constexpr qint64 kMinTrendBytesPerDay = 64LL * 1024 * 1024;
// Disk trend severity, in days until the volume is full at the measured rate.
constexpr double kDiskTrendCriticalDays = 2.0;
constexpr double kDiskTrendWarningDays = 14.0;
constexpr double kDiskTrendInfoDays = 60.0;
// Inodes: a full inode table fails writes while `df` still shows free bytes.
constexpr double kInodeCriticalPct = 95.0;
constexpr double kInodeWarningPct = 85.0;
// File descriptors against RLIMIT_NOFILE.
constexpr double kFdCriticalPct = 90.0;
constexpr double kFdWarningPct = 75.0;
// Defunct children: a handful is a race we lost, a pile is a leak.
constexpr int kZombieCritical = 100;
constexpr int kZombieWarning = 25;
constexpr int kLogErrorWarning = 10;
constexpr int kStallWarning = 10;
constexpr int kStallInfo = 3;
constexpr int kRelayDropWarning = 5;
constexpr int kRelayDropInfo = 2;
// Signed relay/worker requests are rejected once a clock drifts this far.
constexpr qint64 kClockSkewCriticalMs = 5 * 60 * 1000;
constexpr qint64 kClockSkewWarningMs = 2 * 60 * 1000;
// Bytes of new log tail read per file per run, and the per-run match cap: a
// runaway logger must not turn a diagnostics pass into a long read.
constexpr qint64 kMaxLogTailBytes = 256 * 1024;
constexpr int kMaxLogMatchesPerScan = 200;

QString formatCount(qint64 n)
{
    static const QLocale locale(QLocale::English, QLocale::UnitedStates);
    return locale.toString(n);
}

// "~4 days" / "~18 hours" / "~40 minutes" — a horizon, never a precise ETA.
QString formatHorizon(double days)
{
    if (days >= 2.0)
        return QStringLiteral("~%1 days").arg(days, 0, 'f', 0);
    const double hours = days * 24.0;
    if (hours >= 2.0)
        return QStringLiteral("~%1 hours").arg(hours, 0, 'f', 0);
    return QStringLiteral("~%1 minutes").arg(qMax(1.0, hours * 60.0), 0, 'f', 0);
}

QString formatDuration(qint64 ms)
{
    const qint64 seconds = qAbs(ms) / 1000;
    if (seconds < 90)
        return QStringLiteral("%1s").arg(seconds);
    if (seconds < 90 * 60)
        return QStringLiteral("%1m").arg(seconds / 60);
    return QStringLiteral("%1h").arg(seconds / 3600);
}

// The window's length as the operator reads it ("in the last 30m").
QString formatWindow(qint64 windowMs)
{
    return formatDuration(windowMs > 0 ? windowMs : kWindowMs);
}

void add(QList<Finding> &out, const QString &id, int severity, const QString &message)
{
    Finding f;
    f.id = id;
    f.severity = severity;
    f.message = message.left(kMaxMessageChars);
    out.append(f);
}

bool lineLooksLikeError(const QString &line)
{
    static const char *kNeedles[] = {"error",  "failed",    "failure", "fatal",
                                     "crash",  "exception", "traceback",
                                     "denied", "refused"};
    for (const char *needle : kNeedles) {
        if (line.contains(QLatin1String(needle), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

qint64 nowOr(qint64 nowMs)
{
    return nowMs > 0 ? nowMs : QDateTime::currentMSecsSinceEpoch();
}

void pruneOlderThan(QList<qint64> &times, qint64 cutoffMs)
{
    while (!times.isEmpty() && times.first() < cutoffMs)
        times.removeFirst();
}

} // namespace

QList<Finding> evaluate(const Inputs &in)
{
    QList<Finding> out;
    const QString window = formatWindow(in.windowMs);

    // Disk trend. The gauge already shows "70% used"; what it can't show is that
    // the node lost 3 GB of headroom since this morning and will wedge on
    // Thursday. Measured from the ends of the history ring, over a span long
    // enough that one git fetch can't fake a slope.
    if (in.diskHistory.size() >= 2) {
        const DiskSample first = in.diskHistory.first();
        const DiskSample last = in.diskHistory.last();
        const qint64 spanMs = last.tsMs - first.tsMs;
        const qint64 lostBytes = first.freeBytes - last.freeBytes;
        if (spanMs >= kMinTrendSpanMs && lostBytes > 0 && last.freeBytes > 0) {
            const double perDay = double(lostBytes) * double(kDayMs) / double(spanMs);
            if (perDay >= double(kMinTrendBytesPerDay)) {
                const double days = double(last.freeBytes) / perDay;
                int severity = Ok;
                if (days < kDiskTrendCriticalDays)
                    severity = Critical;
                else if (days < kDiskTrendWarningDays)
                    severity = Warning;
                else if (days < kDiskTrendInfoDays)
                    severity = Info;
                if (severity != Ok) {
                    add(out, QStringLiteral("disk-trend"), severity,
                        QStringLiteral("Disk filling: %1/day lost, %2 free, full in %3")
                            .arg(SystemStats::formatBytes(qint64(perDay)),
                                 SystemStats::formatBytes(last.freeBytes),
                                 formatHorizon(days)));
                }
            }
        }
    }

    // Inodes run out before bytes do on a node holding many small git objects,
    // and the failure looks like "No space left on device" on a half-empty disk.
    if (in.inodesTotal > 0 && in.inodesFree >= 0 && in.inodesFree <= in.inodesTotal) {
        const double usedPct =
            100.0 * double(in.inodesTotal - in.inodesFree) / double(in.inodesTotal);
        if (usedPct >= kInodeWarningPct) {
            add(out, QStringLiteral("disk-inodes"),
                usedPct >= kInodeCriticalPct ? Critical : Warning,
                QStringLiteral("Filesystem inodes %1% used (%2 of %3 free) — writes "
                               "fail before the disk does")
                    .arg(usedPct, 0, 'f', 0)
                    .arg(formatCount(in.inodesFree), formatCount(in.inodesTotal)));
        }
    }

    if (!in.dataDirWritable) {
        add(out, QStringLiteral("data-dir"), Critical,
            QStringLiteral("Data directory is not writable — serving and mirror "
                           "sync will fail (read-only mount or permissions)"));
    }

    // Descriptor leak: the node keeps serving until the limit, then every clone
    // and relay reconnect fails at once, with no CPU/RAM symptom beforehand.
    if (in.openFileDescriptors >= 0 && in.fileDescriptorLimit > 0) {
        const double usedPct =
            100.0 * double(in.openFileDescriptors) / double(in.fileDescriptorLimit);
        if (usedPct >= kFdWarningPct) {
            add(out, QStringLiteral("file-descriptors"),
                usedPct >= kFdCriticalPct ? Critical : Warning,
                QStringLiteral("Open files %1 of %2 (%3%) — new connections will "
                               "start failing")
                    .arg(formatCount(in.openFileDescriptors),
                         formatCount(in.fileDescriptorLimit))
                    .arg(usedPct, 0, 'f', 0));
        }
    }

    if (in.zombieProcesses >= kZombieWarning) {
        add(out, QStringLiteral("zombie-processes"),
            in.zombieProcesses >= kZombieCritical ? Critical : Warning,
            QStringLiteral("%1 defunct child processes are not being reaped — a "
                           "git or agent helper is leaking")
                .arg(in.zombieProcesses));
    }

    if (in.logErrors > 0) {
        QString message = QStringLiteral("%1 error%2 in the log in the last %3")
                              .arg(in.logErrors)
                              .arg(in.logErrors == 1 ? "" : "s")
                              .arg(window);
        const QString sample = in.logErrorSample.trimmed();
        if (!sample.isEmpty())
            message += QStringLiteral(": ") + sample;
        add(out, QStringLiteral("log-errors"),
            in.logErrors >= kLogErrorWarning ? Warning : Info, message);
    }

    if (in.uiStalls >= kStallInfo) {
        add(out, QStringLiteral("ui-stalls"),
            in.uiStalls >= kStallWarning ? Warning : Info,
            QStringLiteral("%1 UI stalls recorded in the last %2 — the event loop "
                           "is being blocked")
                .arg(in.uiStalls)
                .arg(window));
    }

    if (in.relayDrops >= kRelayDropInfo) {
        add(out, QStringLiteral("relay-flap"),
            in.relayDrops >= kRelayDropWarning ? Warning : Info,
            QStringLiteral("Relay link dropped %1 times in the last %2")
                .arg(in.relayDrops)
                .arg(window));
    }

    if (in.backpressureDrops > 0) {
        add(out, QStringLiteral("relay-backpressure"), Warning,
            QStringLiteral("%1 frames dropped in the last %2 — the relay socket "
                           "is backing up")
                .arg(formatCount(in.backpressureDrops), window));
    }

    // A drifted clock breaks signed heartbeats and mirror leases long before
    // anyone thinks to check `date` on the box.
    if (in.clockSkewKnown && qAbs(in.clockSkewMs) >= kClockSkewWarningMs) {
        add(out, QStringLiteral("clock-skew"),
            qAbs(in.clockSkewMs) >= kClockSkewCriticalMs ? Critical : Warning,
            QStringLiteral("Clock is %1 %2 the rest of the mesh — signed requests "
                           "may be rejected")
                .arg(formatDuration(in.clockSkewMs),
                     in.clockSkewMs > 0 ? QStringLiteral("ahead of")
                                        : QStringLiteral("behind")));
    }

    std::sort(out.begin(), out.end(), [](const Finding &a, const Finding &b) {
        if (a.severity != b.severity)
            return a.severity > b.severity;
        return a.id < b.id;
    });
    return out;
}

int worstSeverity(const QList<Finding> &findings)
{
    int worst = Ok;
    for (const Finding &f : findings)
        worst = qMax(worst, f.severity);
    return worst;
}

QString severityName(int severity)
{
    switch (severity) {
    case Critical:
        return QStringLiteral("Critical");
    case Warning:
        return QStringLiteral("Warning");
    case Info:
        return QStringLiteral("Info");
    default:
        return QStringLiteral("OK");
    }
}

QString summaryLabel(const QList<Finding> &findings, bool reported)
{
    if (!reported)
        return QString::fromUtf8("\xE2\x80\x94"); // em dash: never reported
    if (findings.isEmpty())
        return QStringLiteral("OK");
    int critical = 0;
    int warnings = 0;
    int infos = 0;
    for (const Finding &f : findings) {
        if (f.severity >= Critical)
            ++critical;
        else if (f.severity == Warning)
            ++warnings;
        else
            ++infos;
    }
    QStringList parts;
    if (critical > 0)
        parts << QStringLiteral("%1 critical").arg(critical);
    if (warnings > 0)
        parts << QStringLiteral("%1 warning%2").arg(warnings).arg(warnings == 1 ? "" : "s");
    if (parts.isEmpty() && infos > 0)
        parts << QStringLiteral("%1 note%2").arg(infos).arg(infos == 1 ? "" : "s");
    return parts.join(QString::fromUtf8(" \xC2\xB7 "));
}

QString detailText(const QList<Finding> &findings)
{
    QStringList lines;
    for (const Finding &f : findings)
        lines << severityName(f.severity) + QStringLiteral(": ") + f.message;
    return lines.join(QStringLiteral("\n"));
}

QJsonArray toJson(const QList<Finding> &findings)
{
    QJsonArray array;
    for (const Finding &f : findings) {
        if (array.size() >= kMaxWireFindings)
            break;
        if (f.id.isEmpty() || f.message.isEmpty())
            continue;
        array.append(QJsonObject{{"i", f.id.left(32)},
                                 {"s", qBound(int(Ok), f.severity, int(Critical))},
                                 {"m", f.message.left(kMaxMessageChars)}});
    }
    return array;
}

QList<Finding> fromJson(const QJsonArray &array)
{
    QList<Finding> findings;
    for (const QJsonValue &value : array) {
        if (findings.size() >= kMaxWireFindings)
            break;
        const QJsonObject object = value.toObject();
        Finding f;
        f.id = object.value(QStringLiteral("i")).toString().left(32);
        f.severity = qBound(int(Ok), object.value(QStringLiteral("s")).toInt(Info),
                            int(Critical));
        f.message = object.value(QStringLiteral("m")).toString().left(kMaxMessageChars);
        if (f.id.isEmpty() || f.message.isEmpty())
            continue;
        findings.append(f);
    }
    std::sort(findings.begin(), findings.end(),
              [](const Finding &a, const Finding &b) {
                  if (a.severity != b.severity)
                      return a.severity > b.severity;
                  return a.id < b.id;
              });
    return findings;
}

// ---------------------------------------------------------------------------
// Host sampling
// ---------------------------------------------------------------------------

namespace {

// Inode totals for the volume holding `path` (0/0 when unavailable).
void sampleInodes(const QString &path, qint64 *total, qint64 *free)
{
    *total = 0;
    *free = 0;
#if defined(Q_OS_UNIX)
    struct statvfs st;
    if (!path.isEmpty() && statvfs(path.toLocal8Bit().constData(), &st) == 0) {
        *total = qint64(st.f_files);
        *free = qint64(st.f_favail);
    }
#else
    Q_UNUSED(path);
#endif
}

// Open descriptors for this process and the soft RLIMIT_NOFILE (-1 unknown).
void sampleFileDescriptors(int *open, int *limit)
{
    *open = -1;
    *limit = -1;
#if defined(Q_OS_LINUX)
    QDir fds(QStringLiteral("/proc/self/fd"));
    if (fds.exists())
        *open = int(fds.entryList(QDir::Files | QDir::Dirs | QDir::System |
                                  QDir::NoDotAndDotDot)
                        .size());
#endif
#if defined(Q_OS_UNIX)
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
        *limit = int(qMin<qulonglong>(rl.rlim_cur, INT_MAX));
#endif
}

// Defunct children of this process (-1 where /proc isn't available).
int sampleZombies()
{
#if defined(Q_OS_LINUX)
    const qint64 self = qint64(getpid());
    int zombies = 0;
    const QStringList entries =
        QDir(QStringLiteral("/proc"))
            .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Unsorted);
    for (const QString &entry : entries) {
        bool numeric = false;
        entry.toLongLong(&numeric);
        if (!numeric)
            continue;
        QFile stat(QStringLiteral("/proc/%1/stat").arg(entry));
        if (!stat.open(QIODevice::ReadOnly))
            continue;
        const QByteArray line = stat.readLine(4096);
        // comm (field 2) may hold spaces and parens — split after the last ')'.
        const int close = line.lastIndexOf(')');
        if (close < 0)
            continue;
        const QList<QByteArray> fields = line.mid(close + 2).split(' ');
        if (fields.size() < 2)
            continue;
        if (fields.at(0) != "Z")
            continue;
        if (fields.at(1).toLongLong() == self)
            ++zombies;
    }
    return zombies;
#else
    return -1;
#endif
}

} // namespace

void Collector::setDataDir(const QString &dir)
{
    const QString trimmed = dir.trimmed();
    if (trimmed == m_dataDir)
        return;
    m_dataDir = trimmed;
    m_diskHistory.clear(); // a different volume invalidates the trend
}

void Collector::setLogPaths(const QStringList &paths)
{
    m_logPaths = paths;
    m_logPathsExplicit = true;
    m_logOffsets.clear();
}

void Collector::noteLogLine(const QString &line, qint64 nowMs)
{
    if (!lineLooksLikeError(line))
        return;
    // Our own "self-diagnostics found N problems" log line would otherwise feed
    // itself back in as another error every minute.
    if (line.contains(QLatin1String("Self-diagnostics"), Qt::CaseInsensitive))
        return;
    const qint64 now = nowOr(nowMs);
    m_logErrorTimes.append(now);
    m_logErrorSample = line.trimmed().right(qMin(line.trimmed().size(), 80));
    pruneCounters(now);
}

void Collector::noteRelayDrop(qint64 nowMs)
{
    const qint64 now = nowOr(nowMs);
    m_relayDropTimes.append(now);
    pruneCounters(now);
}

void Collector::noteBackpressureDrops(qint64 totalSoFar)
{
    if (totalSoFar < 0)
        return;
    // A node restart (or a counter reset) must not read as a burst of drops.
    if (m_backpressureBaseline < 0 || totalSoFar < m_backpressureBaseline) {
        m_backpressureBaseline = totalSoFar;
        return;
    }
    m_backpressureDrops = totalSoFar - m_backpressureBaseline;
}

void Collector::notePeerTimestamp(qint64 peerMs, qint64 localMs)
{
    if (peerMs <= 0 || localMs <= 0)
        return;
    // Our clock minus theirs. Smoothed so one peer with a broken clock (or a
    // frame that sat in a queue) doesn't flip the whole node to "skewed".
    const qint64 skew = localMs - peerMs;
    m_clockSkewMs = m_clockSkewKnown ? (m_clockSkewMs * 3 + skew) / 4 : skew;
    m_clockSkewKnown = true;
}

void Collector::pruneCounters(qint64 nowMs)
{
    const qint64 cutoff = nowMs - kWindowMs;
    pruneOlderThan(m_logErrorTimes, cutoff);
    pruneOlderThan(m_relayDropTimes, cutoff);
    pruneOlderThan(m_stallTimes, cutoff);
    if (m_logErrorTimes.isEmpty())
        m_logErrorSample.clear();
    // Keep at most a couple of hours of disk samples: the trend wants a long
    // enough arm to be meaningful, not the node's whole uptime.
    const qint64 diskCutoff = nowMs - 2 * 60 * 60 * 1000;
    while (m_diskHistory.size() > 2 && m_diskHistory.first().tsMs < diskCutoff)
        m_diskHistory.removeFirst();
}

void Collector::scanLogFiles(qint64 nowMs)
{
    QStringList paths = m_logPaths;
    if (!m_logPathsExplicit) {
        // Everything the app writes lives in one diagnostics directory (the
        // stall log and its rotated generation today).
        const QDir dir(QDir::homePath() + QStringLiteral("/.forkmesh/diagnostics"));
        for (const QString &name :
             dir.entryList({QStringLiteral("*.log")}, QDir::Files, QDir::Name))
            paths << dir.filePath(name);
    }
    int matches = 0;
    for (const QString &path : std::as_const(paths)) {
        QFile file(path);
        const qint64 size = QFileInfo(path).size();
        const bool known = m_logOffsets.contains(path);
        qint64 offset = m_logOffsets.value(path, 0);
        if (offset > size) // rotated out from under us
            offset = 0;
        if (!known) {
            // First sight of the file: adopt its end as the baseline so months
            // of old errors don't land in "the last 30 minutes".
            m_logOffsets.insert(path, size);
            continue;
        }
        m_logOffsets.insert(path, size);
        if (size <= offset)
            continue;
        if (size - offset > kMaxLogTailBytes)
            offset = size - kMaxLogTailBytes;
        if (!file.open(QIODevice::ReadOnly))
            continue;
        file.seek(offset);
        while (!file.atEnd() && matches < kMaxLogMatchesPerScan) {
            const QString line = QString::fromUtf8(file.readLine(4096)).trimmed();
            if (line.isEmpty())
                continue;
            // StallWatchdog's report header, the one line that says the event
            // loop froze rather than something merely failing.
            if (line.contains(QLatin1String("UI stalled"))) {
                m_stallTimes.append(nowMs);
                ++matches;
                continue;
            }
            if (lineLooksLikeError(line)) {
                m_logErrorTimes.append(nowMs);
                m_logErrorSample = line.right(qMin(line.size(), 80));
                ++matches;
            }
        }
        file.close();
    }
}

QList<Finding> Collector::run(qint64 nowMs)
{
    const qint64 now = nowOr(nowMs);
    if (m_lastRunMs > 0 && now - m_lastRunMs < kRunIntervalMs)
        return m_findings;
    m_lastRunMs = now;

    Inputs in;
    in.nowMs = now;
    in.windowMs = kWindowMs;

    if (!m_dataDir.isEmpty()) {
        const qint64 total = SystemStats::diskTotalBytes(m_dataDir);
        const qint64 free = SystemStats::diskFreeBytes(m_dataDir);
        if (total > 0) {
            DiskSample sample;
            sample.tsMs = now;
            sample.freeBytes = free;
            sample.totalBytes = total;
            m_diskHistory.append(sample);
        }
        sampleInodes(m_dataDir, &in.inodesTotal, &in.inodesFree);
        const QFileInfo info(m_dataDir);
        in.dataDirWritable = !info.exists() || info.isWritable();
    }
    scanLogFiles(now);
    pruneCounters(now);

    in.diskHistory = m_diskHistory;
    sampleFileDescriptors(&in.openFileDescriptors, &in.fileDescriptorLimit);
    in.zombieProcesses = sampleZombies();
    in.logErrors = int(m_logErrorTimes.size());
    in.logErrorSample = m_logErrorSample;
    in.uiStalls = int(m_stallTimes.size());
    in.relayDrops = int(m_relayDropTimes.size());
    in.backpressureDrops = m_backpressureDrops;
    in.clockSkewMs = m_clockSkewMs;
    in.clockSkewKnown = m_clockSkewKnown;

    m_findings = evaluate(in);
    return m_findings;
}

void Collector::reset()
{
    m_diskHistory.clear();
    m_logErrorTimes.clear();
    m_relayDropTimes.clear();
    m_stallTimes.clear();
    m_logErrorSample.clear();
    m_logOffsets.clear();
    m_backpressureBaseline = -1;
    m_backpressureDrops = 0;
    m_clockSkewMs = 0;
    m_clockSkewKnown = false;
    m_findings.clear();
    m_lastRunMs = 0;
}

Collector &hostCollector()
{
    static Collector collector;
    return collector;
}

} // namespace NodeDiagnostics
