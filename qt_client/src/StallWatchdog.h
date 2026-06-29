#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <thread>

class QTimer;

// Watches the GUI (main) thread for "not responding" stalls — the kind that make
// the window manager pop the Wait / Force-Quit prompt, or just feel sluggish. A
// main-thread heartbeat timer bumps a timestamp; a background thread notices when
// it stops advancing and, on Linux, snapshots the main thread's call stack so the
// blocking spot is identifiable. Detected stalls are reported (queued, on the
// main thread) and appended to a durable log so they survive a later hang/crash.
class StallWatchdog : public QObject
{
    Q_OBJECT
public:
    explicit StallWatchdog(QObject *parent = nullptr);
    ~StallWatchdog() override;

    // Begin watching. A gap longer than stallThresholdMs between heartbeats counts
    // as a stall. logPath, if set, receives one appended record per stall.
    // buildInfo (e.g. "ForkMesh v0.5.1 (src /path/to/qt_client)") is recorded with
    // each stall so a report handed to an agent points at the exact code to fix.
    void start(int stallThresholdMs = 1500, const QString &logPath = QString(),
               const QString &buildInfo = QString());

signals:
    // Fired once a stall ends: how long the UI was unresponsive (peak observed)
    // and the captured backtrace (may be empty if it couldn't be sampled).
    void stalled(qint64 peakMs, const QString &backtrace);

private:
    void beat();      // runs on the main thread (heartbeat timer)
    void watchLoop(); // runs on the background thread

    std::atomic<qint64> m_lastBeatMs{0};
    std::atomic<bool> m_running{false};
    int m_thresholdMs = 1500;
    QString m_logPath;
    QString m_buildInfo;  // version + source dir, recorded with each stall
    QString m_exePath;    // running binary, for the addr2line symbolize hint
    qint64 m_pid = 0;
    QTimer *m_beatTimer = nullptr;
    std::thread m_worker;
};
