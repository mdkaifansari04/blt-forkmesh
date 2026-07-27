#pragma once

#include <QAtomicInteger>
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
    using Listener = std::function<void(quint64 id, const QString &kind,
                                        const QString &detail, bool started)>;

    static quint64 begin(const QString &kind, const QString &detail = QString())
    {
        const quint64 id = state().nextId.fetchAndAddOrdered(1);
        notify(id, kind, detail, true);
        return id;
    }

    static void end(quint64 id)
    {
        if (id == 0)
            return;
        notify(id, QString(), QString(), false);
    }

    // Only the window installs a listener; passing a default-constructed
    // std::function (as the window's destructor does) detaches again.
    static void setListener(Listener listener)
    {
        QMutexLocker lock(&state().mutex);
        state().listener = std::move(listener);
    }

private:
    struct State {
        QMutex mutex;
        Listener listener;
        QAtomicInteger<quint64> nextId = 1;
    };

    static State &state()
    {
        static State s;
        return s;
    }

    static void notify(quint64 id, const QString &kind, const QString &detail,
                       bool started)
    {
        QMutexLocker lock(&state().mutex);
        if (state().listener)
            state().listener(id, kind, detail, started);
    }
};

// RAII ticket for synchronous work (a blocking git wait, a scan on a worker
// thread). Non-copyable; retires the ticket when the scope unwinds, including
// on an early return.
class BackgroundScope
{
public:
    explicit BackgroundScope(const QString &kind,
                             const QString &detail = QString())
        : m_id(BackgroundActivity::begin(kind, detail))
    {
    }
    ~BackgroundScope() { BackgroundActivity::end(m_id); }
    BackgroundScope(const BackgroundScope &) = delete;
    BackgroundScope &operator=(const BackgroundScope &) = delete;

private:
    quint64 m_id = 0;
};

} // namespace forkmesh
