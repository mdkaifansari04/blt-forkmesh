#pragma once

#include <QString>

#include <memory>

namespace forkmesh {







class ActionTelemetry
{
public:
    enum class Execution {
        Async,
        Worker,
        UiBlocking,
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

}
