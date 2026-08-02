#include "GlobalSearchMatch.h"

#include <QRegularExpression>

namespace forkmesh::search {

namespace {

const Category kCategories[] = {
    {"repo", "Repositories", "repo", "#d29922"},
    {"issue", "Issues", "issue-opened", "#3fb950"},
    {"pull", "Pull requests", "git-pull-request", "#a371f7"},
    {"discussion", "Discussions", "comment", "#58a6ff"},
    {"project", "Projects", "workflow", "#a371f7"},
    {"milestone", "Milestones", "graph", "#3fb950"},
    {"agent", "Agent sessions", "sparkle", "#e3b341"},
    {"branch", "Branches", "git-branch", "#58a6ff"},
    {"worktree", "Worktrees", "file-directory", "#d29922"},
    {"tag", "Tags", "tag", "#8b949e"},
    {"action", "Workflow runs", "workflow", "#3fb950"},
    {"commit", "Commits", "git-commit", "#8b949e"},
    {"file", "Files", "file", "#58a6ff"},
    {"code", "Code", "code", "#58a6ff"},
};
constexpr int kCategoryCount = int(sizeof(kCategories) / sizeof(kCategories[0]));

// Comment bodies are quoted with the "comment: " prefix so a hit in a reply
// reads differently from a hit in the record's own description.
QString commentNote(const QString &body, const QString &query)
{
    return QStringLiteral("comment: ") + snippetAround(body, query);
}

} // namespace

int categoryCount()
{
    return kCategoryCount;
}

const Category &categoryAt(int index)
{
    static const Category unknown{"", "Other", "search", "#8b949e"};
    if (index < 0 || index >= kCategoryCount)
        return unknown;
    return kCategories[index];
}

int categoryIndexOf(const QString &kind)
{
    for (int i = 0; i < kCategoryCount; ++i)
        if (kind == QLatin1String(kCategories[i].kind))
            return i;
    return kCategoryCount;
}

bool containsFold(const QString &haystack, const QString &needle)
{
    return !haystack.isEmpty() && !needle.isEmpty() &&
           haystack.contains(needle, Qt::CaseInsensitive);
}

QString snippetAround(const QString &haystack, const QString &needle)
{
    QString flat = haystack;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    flat = flat.simplified();
    const int at = flat.indexOf(needle, 0, Qt::CaseInsensitive);
    if (at < 0)
        return flat.left(120);
    const int start = qMax(0, at - 30);
    QString out = flat.mid(start, 120);
    if (start > 0)
        out.prepend(QStringLiteral("…"));
    return out;
}

bool matchTitleBody(const QString &query, const QString &title,
                    const QString &body, QString *where)
{
    if (containsFold(title, query)) {
        *where = QStringLiteral("title");
        return true;
    }
    if (containsFold(body, query)) {
        *where = snippetAround(body, query);
        return true;
    }
    return false;
}

bool matchIssue(const Issue &issue, const QString &query, QString *where)
{
    if (containsFold(issue.title, query)) {
        *where = QStringLiteral("title");
        return true;
    }
    for (const IssueEvent &event : issue.events) {
        if (!containsFold(event.body, query))
            continue;
        *where = event.type == QLatin1String("open")
                     ? snippetAround(event.body, query)
                     : commentNote(event.body, query);
        return true;
    }
    return false;
}

bool matchPull(const PullRequest &pull, const QString &query, QString *where)
{
    if (matchTitleBody(query, pull.title, pull.description, where))
        return true;
    for (const PullEvent &event : pull.events) {
        if (!containsFold(event.body, query))
            continue;
        *where = commentNote(event.body, query);
        return true;
    }
    return false;
}

bool matchDiscussion(const Discussion &discussion, const QString &query,
                     QString *where)
{
    if (containsFold(discussion.title, query)) {
        *where = QStringLiteral("title");
        return true;
    }
    for (const DiscussionEvent &event : discussion.events) {
        if (!containsFold(event.body, query))
            continue;
        *where = event.type == QLatin1String("open")
                     ? snippetAround(event.body, query)
                     : commentNote(event.body, query);
        return true;
    }
    return false;
}

bool matchProject(const Project &project, const QString &query, QString *where)
{
    if (matchTitleBody(query, project.title, project.body, where))
        return true;
    for (const ProjectEvent &event : project.events) {
        if (!containsFold(event.body, query))
            continue;
        *where = commentNote(event.body, query);
        return true;
    }
    return false;
}

QVector<WorktreeRecord> parseWorktreePorcelain(const QStringList &lines)
{
    QVector<WorktreeRecord> records;
    WorktreeRecord current;
    bool open = false;
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("worktree "))) {
            if (open)
                records.append(current); // a new record starts; emit the last
            current = WorktreeRecord();
            current.path = line.mid(9);
            open = true;
        } else if (!open) {
            continue; // stray attribute before any "worktree " line
        } else if (line.startsWith(QLatin1String("HEAD "))) {
            current.head = line.mid(5);
        } else if (line.startsWith(QLatin1String("branch "))) {
            current.branch = line.mid(7);
            if (current.branch.startsWith(QLatin1String("refs/heads/")))
                current.branch = current.branch.mid(11);
        } else if (line == QLatin1String("detached")) {
            current.detached = true;
        }
    }
    if (open)
        records.append(current);
    return records;
}

CodeRow parseGrepRow(const QString &row, const QString &ref)
{
    CodeRow out;
    QString line = row;
    const QString prefix = ref + QLatin1Char(':');
    if (!ref.isEmpty() && line.startsWith(prefix))
        line = line.mid(prefix.size());
    static const QRegularExpression rowRe(QStringLiteral("^(.+?):(\\d+):(.*)$"));
    const QRegularExpressionMatch match = rowRe.match(line);
    if (!match.hasMatch())
        return out;
    out.path = match.captured(1);
    out.line = match.captured(2).toInt();
    out.text = match.captured(3).trimmed();
    out.valid = true;
    return out;
}

} // namespace forkmesh::search
