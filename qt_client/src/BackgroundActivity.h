#pragma once

#include "ActionTelemetry.h"

#include <QAtomicInteger>
#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QString>

#include <functional>
#include <utility>














namespace forkmesh {

class BackgroundActivity
{
public:




    using Listener = std::function<void(quint64 id, const QString &kind,
                                        const QString &detail,
                                        ActionTelemetry::Execution execution,
                                        bool started)>;

    static quint64 begin(
        const QString &kind, const QString &detail = QString(),
        ActionTelemetry::Execution execution = ActionTelemetry::Execution::Async)
    {
        const quint64 id = state().nextId.fetchAndAddOrdered(1);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        {
            QMutexLocker lock(&state().mutex);
            state().tickets.insert(id, Ticket{kind, detail, execution, now});
        }
        ActionTelemetry::started(id, kind, detail, execution, now);
        notify(id, kind, detail, execution, true);
        return id;
    }

    static void end(quint64 id,
                    const QString &outcome = QStringLiteral("completed"))
    {
        if (id == 0)
            return;
        Ticket ticket;
        bool found = false;
        {
            QMutexLocker lock(&state().mutex);
            const auto it = state().tickets.find(id);
            if (it != state().tickets.end()) {
                ticket = *it;
                state().tickets.erase(it);
                found = true;
            }
        }
        if (found) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            ActionTelemetry::finished(id, ticket.kind, ticket.detail,
                                      ticket.execution, ticket.startedAtMs, now,
                                      outcome);
        }
        if (found)
            notify(id, QString(), QString(), ticket.execution, false);
    }



    static void setListener(Listener listener)
    {
        QMutexLocker lock(&state().mutex);
        state().listener = std::move(listener);
    }

private:
    struct Ticket {
        QString kind;
        QString detail;
        ActionTelemetry::Execution execution =
            ActionTelemetry::Execution::Async;
        qint64 startedAtMs = 0;
    };

    struct State {
        QMutex mutex;
        Listener listener;
        QHash<quint64, Ticket> tickets;
        QAtomicInteger<quint64> nextId = 1;
    };

    static State &state()
    {
        static State s;
        return s;
    }

    static void notify(quint64 id, const QString &kind, const QString &detail,
                       ActionTelemetry::Execution execution, bool started)
    {
        QMutexLocker lock(&state().mutex);
        if (state().listener)
            state().listener(id, kind, detail, execution, started);
    }
};




constexpr qint64 kBackgroundShowAfterMs = 200;

inline QString backgroundOkGlyph() { return QString::fromUtf8("\xE2\x9C\x93"); }
inline QString backgroundNotGlyph() { return QString::fromUtf8("\xE2\x9C\x95"); }


inline QString backgroundElapsedText(qint64 ms)
{
    const qint64 clamped = ms < 0 ? 0 : ms;
    if (clamped < 1000)
        return QStringLiteral("%1ms").arg(clamped);
    return QStringLiteral("%1s").arg(clamped / 1000.0, 0, 'f', 1);
}




inline QString backgroundOutcomeLine(const QString &word, int runs,
                                     qint64 elapsedMs, const QString &detail,
                                     bool backgrounded)
{
    QString line = QStringLiteral("Background %1 %2")
                       .arg(backgrounded ? backgroundOkGlyph()
                                         : backgroundNotGlyph(),
                            word.isEmpty() ? QStringLiteral("work") : word);
    if (runs > 1)
        line += QStringLiteral(" %1%2").arg(QChar(0x00D7)).arg(runs);
    line += backgrounded ? QStringLiteral(" backgrounded")
                         : QStringLiteral(" not backgrounded");
    line += QStringLiteral(" (%1%2)")
                .arg(runs > 1 ? QStringLiteral("longest ") : QString(),
                     backgroundElapsedText(elapsedMs));
    const QString note = detail.trimmed();
    if (!note.isEmpty())
        line += QStringLiteral(" - ") + note;
    return line;
}




inline QString colorizeBackgroundMarker(const QString &escapedMessage)
{
    struct Mark {
        QString glyph;
        QLatin1String color;
    };
    const Mark marks[] = {
        {backgroundOkGlyph(), QLatin1String("#3fb950")},
        {backgroundNotGlyph(), QLatin1String("#f85149")},
    };
    QString html = escapedMessage;
    for (const Mark &mark : marks) {
        const int at = html.indexOf(mark.glyph);
        if (at < 0)
            continue;
        html.replace(at, mark.glyph.size(),
                     QStringLiteral("<span style='color:%1; font-weight:700'>%2"
                                    "</span>")
                         .arg(QString(mark.color), mark.glyph));
    }
    return html;
}




class BackgroundScope
{
public:
    explicit BackgroundScope(const QString &kind,
                             const QString &detail = QString(),
                             ActionTelemetry::Execution execution =
                                 ActionTelemetry::Execution::Async)
        : m_id(BackgroundActivity::begin(kind, detail, execution))
    {
    }
    ~BackgroundScope() { BackgroundActivity::end(m_id); }
    BackgroundScope(const BackgroundScope &) = delete;
    BackgroundScope &operator=(const BackgroundScope &) = delete;

private:
    quint64 m_id = 0;
};

}
