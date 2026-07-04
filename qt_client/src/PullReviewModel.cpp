#include "PullReviewModel.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

namespace {

// The provenance trailer AgentRunner stamps on agent commits (issue #365).
const QLatin1String kAgentTrailer("ForkMesh-Agent:");

// Pull the trailer value out of a single commit message / patch body. Returns an
// empty string when the commit has no ForkMesh-Agent trailer.
QString agentTrailerIn(QStringView text)
{
    for (const auto &line : text.split(u'\n')) {
        const QStringView t = line.trimmed();
        if (t.startsWith(kAgentTrailer))
            return t.mid(kAgentTrailer.size()).trimmed().toString();
    }
    return QString();
}

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

// Split a format-patch mbox into its per-commit chunks. Each entry starts with a
// mbox "From <sha> <date>" line at column 0.
QStringList splitMboxPatches(const QString &mbox)
{
    static const QRegularExpression re(
        QStringLiteral("(?m)^From [0-9a-fA-F]{7,40} "));
    QStringList out;
    QList<qsizetype> starts;
    auto it = re.globalMatch(mbox);
    while (it.hasNext())
        starts.append(it.next().capturedStart());
    for (int i = 0; i < starts.size(); ++i) {
        const qsizetype begin = starts.at(i);
        const qsizetype end = (i + 1 < starts.size()) ? starts.at(i + 1) : mbox.size();
        out.append(mbox.mid(begin, end - begin));
    }
    return out;
}

} // namespace

PullAgentProvenance pullAgentProvenance(const PullRequest &pr)
{
    PullAgentProvenance prov;
    // The trailer rides inside the signed commit series; the first commit that
    // carries it settles the tool/model for the whole PR.
    const QString value = agentTrailerIn(pr.commits);
    if (value.isEmpty())
        return prov;
    prov.isAgent = true;
    prov.value = value;
    const int slash = value.indexOf(u'/');
    if (slash >= 0) {
        prov.tool = value.left(slash);
        prov.model = value.mid(slash + 1);
    } else {
        prov.tool = value;
    }
    return prov;
}

QHash<QString, bool> pullFileAuthorship(const PullRequest &pr)
{
    QHash<QString, bool> authorship;
    for (const QString &patch : splitMboxPatches(pr.commits)) {
        // The commit message (and its trailers) sit above the first diff header.
        const qsizetype diffStart = patch.indexOf(QLatin1String("\ndiff --git "));
        const QString message = diffStart < 0 ? patch : patch.left(diffStart);
        const bool agent = !agentTrailerIn(message).isEmpty();
        for (const QString &line :
             patch.mid(diffStart < 0 ? patch.size() : diffStart).split(u'\n')) {
            if (!line.startsWith(QLatin1String("diff --git ")))
                continue;
            const QString path = line.section(QLatin1String(" b/"), 1);
            if (path.isEmpty())
                continue;
            // Any agent-stamped commit that touches a file marks it agent-authored.
            authorship[path] = authorship.value(path, false) || agent;
        }
    }
    return authorship;
}

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
