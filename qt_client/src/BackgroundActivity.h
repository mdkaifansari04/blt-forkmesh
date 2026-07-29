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

// Process-wide "something is working" bus (adhoc #421).
//
// The strip between the live log and the agent prompt shows one spinner row per
// *kind* of background work. Most of that work is started by free functions
// (waitForGit), by the network transport, or off the GUI thread — none of which
// hold a MainWindow pointer — so instead of threading a back pointer through
// every call site, callers announce a one-word kind here and MainWindow installs
// a single listener that marshals the notification onto the GUI thread.
//
// Tickets are refcounted per kind: begin() hands one out, end() retires it, and
// the strip keeps a kind visible while at least one of its tickets is open.
// Nothing is drawn for work that finishes quickly (see MainWindow's sweep), so
// announcing even the hottest git read here stays free in the common case.
namespace forkmesh {

class BackgroundActivity
{
public:
    // started == false means the ticket is being retired; kind/detail are empty.
    // Execution is retained on both edges so the UI can distinguish genuinely
    // asynchronous work from a GUI-thread blocking scope without guessing from
    // how quickly it happened to finish.
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

    // Only the window installs a listener; passing a default-constructed
    // std::function (as the window's destructor does) detaches again.
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

// Work that retires inside this window never gets a row in the strip. This
// delay is presentation-only; execution metadata, not elapsed time, determines
// whether the work was actually backgrounded.
constexpr qint64 kBackgroundShowAfterMs = 200;

inline QString backgroundOkGlyph() { return QString::fromUtf8("\xE2\x9C\x93"); }
inline QString backgroundNotGlyph() { return QString::fromUtf8("\xE2\x9C\x95"); }

// "48ms" under a second, "1.4s" past it — short enough to sit in a log line.
inline QString backgroundElapsedText(qint64 ms)
{
    const qint64 clamped = ms < 0 ? 0 : ms;
    if (clamped < 1000)
        return QStringLiteral("%1ms").arg(clamped);
    return QStringLiteral("%1s").arg(clamped / 1000.0, 0, 'f', 1);
}

// The log entry for one finished run of a kind of work. `runs` collapses a burst
// of same-kind tickets that all came and went too fast to be backgrounded, in
// which case `elapsedMs` is the longest of them.
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

// Both log renderers paint the message body in one flat colour; the outcome
// marker has to read as pass/fail on its own, so lift the first ✓ / ✕ out of the
// (already HTML-escaped) message into a green / red span.
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

// RAII ticket for synchronous work (a blocking git wait, a scan on a worker
// thread). Non-copyable; retires the ticket when the scope unwinds, including
// on an early return.
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

} // namespace forkmesh
