#include "PullReviewModel.h"

#include <QHash>

namespace {

QString legacyThreadId(const PullEvent &ev, int index)
{
    if (!ev.id.isEmpty())
        return ev.id;
    return QStringLiteral("legacy:%1:%2:%3:%4")
        .arg(ev.path, ev.side)
        .arg(ev.line)
        .arg(index);
}

int ensureThread(QList<PullReviewThread> &threads, QHash<QString, int> &indexById,
                 const QString &id)
{
    const QString key = id.isEmpty()
                            ? QStringLiteral("thread:%1").arg(threads.size() + 1)
                            : id;
    const auto it = indexById.constFind(key);
    if (it != indexById.constEnd())
        return it.value();
    PullReviewThread thread;
    thread.id = key;
    threads.append(thread);
    const int index = threads.size() - 1;
    indexById.insert(key, index);
    return index;
}

void copyAnchor(PullReviewThread &thread, const PullEvent &ev)
{
    if (!ev.path.isEmpty())
        thread.path = ev.path;
    if (!ev.side.isEmpty())
        thread.side = ev.side;
    if (ev.lineStart > 0)
        thread.lineStart = ev.lineStart;
    if (ev.lineEnd > 0)
        thread.lineEnd = ev.lineEnd;
    if (thread.lineStart == 0 && ev.line > 0)
        thread.lineStart = ev.line;
    if (thread.lineEnd == 0 && ev.line > 0)
        thread.lineEnd = ev.line;
}

} // namespace

PullReviewSnapshot buildPullReviewSnapshot(const PullRequest &pr)
{
    PullReviewSnapshot snapshot;
    snapshot.reviewSummary = pr.reviewSummary();

    QHash<QString, int> indexById;
    for (int i = 0; i < pr.events.size(); ++i) {
        const PullEvent &ev = pr.events.at(i);
        if (ev.type == QLatin1String("comment") ||
            ev.type == QLatin1String("review")) {
            ++snapshot.topLevelItems;
            continue;
        }

        if (ev.type == QLatin1String("line-comment")) {
            const int index =
                ensureThread(snapshot.threads, indexById, legacyThreadId(ev, i));
            PullReviewThread &thread = snapshot.threads[index];
            copyAnchor(thread, ev);
            thread.events.append(ev);
            continue;
        }

        if (ev.type == QLatin1String("thread-comment")) {
            const int index =
                ensureThread(snapshot.threads, indexById, ev.threadId);
            PullReviewThread &thread = snapshot.threads[index];
            copyAnchor(thread, ev);
            if (!ev.suggestionPatch.isEmpty())
                thread.hasSuggestion = true;
            thread.events.append(ev);
            continue;
        }

        if (ev.type == QLatin1String("thread-reply")) {
            const int index =
                ensureThread(snapshot.threads, indexById, ev.threadId);
            snapshot.threads[index].events.append(ev);
            continue;
        }

        if (ev.type == QLatin1String("thread-state")) {
            const int index =
                ensureThread(snapshot.threads, indexById, ev.threadId);
            PullReviewThread &thread = snapshot.threads[index];
            thread.resolved = ev.state == QLatin1String("resolved");
            thread.events.append(ev);
            continue;
        }

        if (ev.type == QLatin1String("suggestion-state")) {
            const int index =
                ensureThread(snapshot.threads, indexById, ev.threadId);
            PullReviewThread &thread = snapshot.threads[index];
            thread.suggestionState = ev.state;
            thread.appliedCommit = ev.appliedCommit;
            thread.events.append(ev);
            continue;
        }
    }

    snapshot.totalThreads = snapshot.threads.size();
    for (const PullReviewThread &thread : std::as_const(snapshot.threads)) {
        if (thread.resolved)
            ++snapshot.resolvedThreads;
        else
            ++snapshot.unresolvedThreads;
        if (thread.hasSuggestion)
            ++snapshot.suggestions;
        if (thread.path.isEmpty())
            continue;
        PullReviewFileSummary summary = snapshot.files.value(thread.path);
        summary.path = thread.path;
        ++summary.totalThreads;
        if (thread.resolved)
            ++summary.resolvedThreads;
        else
            ++summary.unresolvedThreads;
        if (thread.hasSuggestion)
            ++summary.suggestions;
        snapshot.files.insert(thread.path, summary);
    }

    return snapshot;
}
