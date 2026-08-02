#pragma once

#include <QString>

#include <memory>

namespace forkmesh {

// Durable, non-blocking action journal used by the desktop diagnostics.
//
// Callers only enqueue already-formatted records; a dedicated writer thread owns
// the file and flushes every record.  This is intentionally separate from
// qInfo()/the live system log: logging a hot action must never perform filesystem
// I/O, rotate a file, or contend with QTextDocument layout on the GUI thread.
class ActionTelemetry
{
public:
    enum class Execution {
        Async,      // event-driven network/process work
        Worker,     // blocking work running on a worker thread
        UiBlocking, // synchronous work still running on the GUI thread
    };

    static void initialize(const QString &path);
    static void started(quint64 id, const QString &kind, const QString &detail,
                        Execution execution, qint64 startedAtMs);
    static void finished(quint64 id, const QString &kind, const QString &detail,
                         Execution execution, qint64 startedAtMs,
                         qint64 finishedAtMs, const QString &outcome);
    static void instant(const QString &kind, const QString &detail,
                        const QString &outcome = QStringLiteral("observed"));
    static void shutdown();

private:
    struct Writer;
    static std::shared_ptr<Writer> writer();
};

} // namespace forkmesh
