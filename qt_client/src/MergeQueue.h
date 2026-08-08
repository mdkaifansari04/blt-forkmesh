#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// A repository's merge queue: the ordered list of pull requests the owner has
// handed to ForkMesh to land for them, one after the next. The queue itself is
// pure data — ordering, membership and the last thing each entry did — so it can
// be edited (reordered, pruned) and unit-tested without a working tree. The
// actual git work (update from base, conflict dry-run, merge) is driven by
// MainWindow's queue runner, which walks these entries in order.
//
// The queue is persisted with its repository (RepositoryRecord::mergeQueue) as
// one tab-separated row per entry, so it survives restarts and resumes where it
// left off.
namespace MergeQueueState {
// Waiting its turn; nothing is wrong with it.
inline const QString Queued = QStringLiteral("queued");
// Its head branch is being brought up to date with the base branch.
inline const QString Updating = QStringLiteral("updating");
// The merge itself is running.
inline const QString Merging = QStringLiteral("merging");
// Cannot merge yet for a reason the queue can't fix on its own (conflicts, a
// missing peer approval). It stays queued and is re-evaluated on every pass.
inline const QString Blocked = QStringLiteral("blocked");
// The last attempt failed outright (git refused the update or the merge).
inline const QString Failed = QStringLiteral("failed");
} // namespace MergeQueueState

struct MergeQueueEntry {
    int number = 0;         // pull request number
    qint64 queuedAtMs = 0;  // when it joined the queue (queue order tiebreak)
    QString state = MergeQueueState::Queued;
    QString detail;         // why it is in that state, shown in the queue list
    qint64 stateAtMs = 0;   // when `state` was last set

    bool isValid() const { return number > 0; }
};

class MergeQueue
{
public:
    MergeQueue() = default;

    // Persistence. Rows are "<number>\t<queuedAtMs>\t<state>\t<detail>"; a row
    // carrying only a number is accepted too, so a queue written by an older
    // build (or edited by hand) still loads. Unparsable and duplicate rows are
    // dropped rather than poisoning the queue.
    static MergeQueue fromRows(const QStringList &rows);
    QStringList toRows() const;

    const QList<MergeQueueEntry> &entries() const { return m_entries; }
    int size() const { return m_entries.size(); }
    bool isEmpty() const { return m_entries.isEmpty(); }
    int indexOf(int number) const;
    bool contains(int number) const { return indexOf(number) >= 0; }
    QList<int> numbers() const;

    // Append a pull request. Returns false when it is already queued, so callers
    // can tell "added" from "was already there" without a second lookup.
    bool enqueue(int number, qint64 nowMs);
    bool remove(int number);
    // Move an entry `delta` places towards the front (negative) or the back
    // (positive). Returns false when it is absent or already at that end.
    bool move(int number, int delta);
    void clear() { m_entries.clear(); }

    // Record what the runner just did with an entry. Returns false when the
    // entry is gone (it merged, or the user removed it mid-pass).
    bool setState(int number, const QString &state, const QString &detail,
                  qint64 nowMs);
    // The entry the runner should look at first: the front of the queue. The
    // runner walks every entry in order, though — a blocked one does not stop
    // the ones behind it from landing.
    MergeQueueEntry front() const;
    MergeQueueEntry at(int number) const;

    // One-line summary for the queue panel header, e.g. "3 queued · 1 blocked".
    QString summary() const;

    // Free-text a caller wants to store as an entry's detail, flattened to one
    // line and bounded so a git error can't blow up the settings row it is
    // written to.
    static QString sanitizeDetail(const QString &detail);

private:
    QList<MergeQueueEntry> m_entries;
};
