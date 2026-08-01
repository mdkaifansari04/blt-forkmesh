#include "ActionTelemetry.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

namespace forkmesh {
namespace {

QString executionName(ActionTelemetry::Execution execution)
{
    switch (execution) {
    case ActionTelemetry::Execution::Async:
        return QStringLiteral("async");
    case ActionTelemetry::Execution::Worker:
        return QStringLiteral("worker");
    case ActionTelemetry::Execution::UiBlocking:
        return QStringLiteral("ui-blocking");
    }
    return QStringLiteral("unknown");
}

QByteArray recordBytes(const char *event, quint64 id, const QString &kind,
                       const QString &detail,
                       ActionTelemetry::Execution execution, qint64 timestampMs,
                       qint64 durationMs, const QString &outcome)
{
    QJsonObject object{
        {QStringLiteral("ts"),
         QDateTime::fromMSecsSinceEpoch(timestampMs).toString(Qt::ISODateWithMs)},
        {QStringLiteral("event"), QString::fromLatin1(event)},
        {QStringLiteral("id"), QString::number(id)},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("detail"), detail.left(2048)},
        {QStringLiteral("execution"), executionName(execution)},
        {QStringLiteral("pid"),
         QString::number(QCoreApplication::applicationPid())},
    };
    if (durationMs >= 0)
        object.insert(QStringLiteral("duration_ms"), double(durationMs));
    if (!outcome.isEmpty())
        object.insert(QStringLiteral("outcome"), outcome);
    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    return bytes;
}

}

struct ActionTelemetry::Writer {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<QByteArray> pending;
    QString path;
    bool stopping = false;
    std::thread thread;

    ~Writer() { stop(); }

    void start(const QString &newPath)
    {
        if (newPath.isEmpty())
            return;
        std::lock_guard<std::mutex> lock(mutex);
        if (thread.joinable() || stopping)
            return;
        path = newPath;
        thread = std::thread([this] { writeLoop(); });
    }

    void enqueue(QByteArray bytes)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!thread.joinable() || stopping)
                return;


            if (pending.size() >= 4096)
                pending.pop_front();
            pending.emplace_back(std::move(bytes));
        }
        ready.notify_one();
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!thread.joinable())
                return;
            stopping = true;
        }
        ready.notify_one();
        thread.join();
    }

    void writeLoop()
    {
        const QByteArray nativePath = QFile::encodeName(path);
        std::ofstream output;
        while (true) {
            std::deque<QByteArray> batch;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait(lock, [this] { return stopping || !pending.empty(); });
                batch.swap(pending);
                if (batch.empty() && stopping)
                    break;
            }
            if (!output.is_open())
                output.open(nativePath.constData(),
                            std::ios::out | std::ios::app | std::ios::binary);
            if (output.is_open()) {
                for (const QByteArray &record : batch)
                    output.write(record.constData(), record.size());
                output.flush();
            }



        }
    }
};

std::shared_ptr<ActionTelemetry::Writer> ActionTelemetry::writer()
{
    static const std::shared_ptr<Writer> instance = std::make_shared<Writer>();
    return instance;
}

void ActionTelemetry::initialize(const QString &path)
{
    if (path.isEmpty())
        return;


    QDir().mkpath(QFileInfo(path).absolutePath());
    constexpr qint64 kMaxBytes = 8 * 1024 * 1024;
    if (QFileInfo(path).size() > kMaxBytes) {
        const QString previous = path + QStringLiteral(".1");
        QFile::remove(previous);
        QFile::rename(path, previous);
    }
    writer()->start(path);
}

void ActionTelemetry::started(quint64 id, const QString &kind,
                              const QString &detail, Execution execution,
                              qint64 startedAtMs)
{
    writer()->enqueue(recordBytes("started", id, kind, detail, execution,
                                  startedAtMs, -1, QString()));
}

void ActionTelemetry::finished(quint64 id, const QString &kind,
                               const QString &detail, Execution execution,
                               qint64 startedAtMs, qint64 finishedAtMs,
                               const QString &outcome)
{
    writer()->enqueue(recordBytes("finished", id, kind, detail, execution,
                                  finishedAtMs,
                                  qMax<qint64>(0, finishedAtMs - startedAtMs),
                                  outcome));
}

void ActionTelemetry::instant(const QString &kind, const QString &detail,
                              const QString &outcome)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    writer()->enqueue(recordBytes("event", 0, kind, detail, Execution::Async, now,
                                  0, outcome));
}

void ActionTelemetry::shutdown()
{
    writer()->stop();
}

}
