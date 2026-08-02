#pragma once

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// Node self-diagnostics: the health checks a node runs on *itself* and pushes to
// the mesh in its heartbeats, so every node list can show what is going wrong on
// a machine nobody is sitting in front of (adhoc #27).
//
// Deliberately NOT the CPU / RAM / disk gauges — those already have their own
// columns and say nothing about trends. These are the slow-burn failures a point
// sample hides: a filesystem that is filling a few GB a day, inodes running out
// before the bytes do, file descriptors leaking, defunct children piling up, a
// relay link that keeps flapping, errors accumulating in the log, a clock that
// has drifted far enough for signed frames to be rejected.
//
// The rule engine (evaluate) is pure: it takes an Inputs snapshot and returns
// findings, so every threshold is testable without a host to break. Collector
// does the messy host sampling (Linux-first, best-effort — an unavailable metric
// is left at its "unknown" sentinel and simply produces no finding).
namespace NodeDiagnostics {

enum Severity {
    Ok = 0,
    Info = 1,      // worth knowing, nothing is failing yet
    Warning = 2,   // will bite if left alone
    Critical = 3,  // failing now or within hours
};

struct Finding {
    QString id;               // stable check id, e.g. "disk-trend"
    int severity = Info;      // Severity
    QString message;          // one-line human summary, already formatted
};

// One reading of the volume that holds the node's data directory.
struct DiskSample {
    qint64 tsMs = 0;
    qint64 freeBytes = 0;
    qint64 totalBytes = 0;
};

// Everything the rules look at. Unknown metrics keep their sentinel (-1 for
// counts that may legitimately be 0, empty history for trends) and are skipped.
struct Inputs {
    qint64 nowMs = 0;
    // Oldest first. A trend needs at least two samples spanning kMinTrendSpanMs.
    QList<DiskSample> diskHistory;
    qint64 inodesTotal = 0;         // 0 == unknown (not Linux/Unix, or stat failed)
    qint64 inodesFree = 0;
    bool dataDirWritable = true;
    int openFileDescriptors = -1;   // -1 == unknown
    int fileDescriptorLimit = -1;
    int zombieProcesses = -1;       // defunct children of this process
    int logErrors = 0;              // error lines seen in the last windowMs
    QString logErrorSample;         // newest of those lines (already trimmed)
    int uiStalls = 0;               // stall reports appended in the last windowMs
    int relayDrops = 0;             // link losses in the last windowMs
    qint64 backpressureDrops = 0;   // frames dropped because the socket backed up
    qint64 clockSkewMs = 0;         // local clock minus the mesh's (peer frames)
    bool clockSkewKnown = false;
    qint64 windowMs = 0;            // period the counters above cover
};

// How far back the rolling counters (log errors, stalls, link drops) look.
constexpr qint64 kWindowMs = 30 * 60 * 1000;
// Ignore a disk trend measured over less than this — a git fetch finishing mid
// sample would otherwise read as "full in 3 hours".
constexpr qint64 kMinTrendSpanMs = 10 * 60 * 1000;
// Most findings carried on the wire; the rest are dropped worst-first-kept.
constexpr int kMaxWireFindings = 6;
// Longest message kept on the wire, so a heartbeat can't grow without bound.
constexpr int kMaxMessageChars = 140;

// Run the rules. Findings come back worst severity first, then by check id, so
// the summary and the wire order are deterministic.
QList<Finding> evaluate(const Inputs &in);

// Severity of the worst finding (Ok for an empty list).
int worstSeverity(const QList<Finding> &findings);
QString severityName(int severity);

// Column text for a node list: "OK" when the node reported and found nothing,
// "2 warnings" / "1 critical" otherwise. `reported` false (never heard from, or
// the node has diagnostics reporting off) gives an em dash.
QString summaryLabel(const QList<Finding> &findings, bool reported);

// Multi-line detail (one "Severity: message" per finding) for tooltips and the
// node detail panel. Empty when there is nothing to say.
QString detailText(const QList<Finding> &findings);

// Wire form carried in a heartbeat's "dg" field: [{"i":id,"s":sev,"m":msg}].
// Bounded by kMaxWireFindings / kMaxMessageChars so telemetry can't inflate a
// frame; fromJson applies the same bounds to anything a peer sends us.
QJsonArray toJson(const QList<Finding> &findings);
QList<Finding> fromJson(const QJsonArray &array);

// Samples this host and evaluates the rules. Process-global state (rolling
// counters, the disk history ring, log read offsets), so — like SystemStats —
// there is one collector per process and the node's heartbeat drives it.
class Collector
{
public:
    // Volume + directory the checks watch (the node's app data dir). Empty
    // means "not configured yet" and host sampling is skipped.
    void setDataDir(const QString &dir);
    // Log files tailed for error lines and stall reports. Defaults to the
    // diagnostics directory next to the stall log.
    void setLogPaths(const QStringList &paths);

    // Feed the app's own log (MainWindow::logSystem) — the errors a headless
    // node would otherwise only ever show to a terminal nobody is watching.
    void noteLogLine(const QString &line, qint64 nowMs = 0);
    void noteRelayDrop(qint64 nowMs = 0);
    // Called with the node's running total; the delta is what's reported.
    void noteBackpressureDrops(qint64 totalSoFar);
    // Clock check: the timestamp a peer stamped on a frame vs when it arrived.
    void notePeerTimestamp(qint64 peerMs, qint64 localMs);

    // Re-run the checks (throttled to kRunIntervalMs) and return the current
    // findings. Cheap between runs — it just hands back the cached list.
    QList<Finding> run(qint64 nowMs = 0);
    QList<Finding> findings() const { return m_findings; }
    qint64 lastRunMs() const { return m_lastRunMs; }

    // Drop every sample and counter (tests, and a node changing data dir).
    void reset();

    // Host sampling is a full /proc walk plus a couple of stats; once a minute
    // is plenty for trends measured in hours.
    static constexpr qint64 kRunIntervalMs = 60 * 1000;

private:
    void pruneCounters(qint64 nowMs);
    void scanLogFiles(qint64 nowMs);

    QString m_dataDir;
    QStringList m_logPaths;
    bool m_logPathsExplicit = false;
    QList<DiskSample> m_diskHistory;
    QList<qint64> m_logErrorTimes;   // rolling event times, oldest first
    QList<qint64> m_relayDropTimes;
    QList<qint64> m_stallTimes;
    QString m_logErrorSample;
    qint64 m_backpressureBaseline = -1;
    qint64 m_backpressureDrops = 0;
    qint64 m_clockSkewMs = 0;
    bool m_clockSkewKnown = false;
    QHash<QString, qint64> m_logOffsets; // path -> bytes already read
    QList<Finding> m_findings;
    qint64 m_lastRunMs = 0;
};

// The one collector for this process.
Collector &hostCollector();

} // namespace NodeDiagnostics
