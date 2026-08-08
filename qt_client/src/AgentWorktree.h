#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

// Where an agent run's isolated worktree lives, and what it is called.
//
// Every agent gets its own worktree + branch so concurrent runs never share a
// working tree (adhoc #2). Those worktrees used to be carved out of the system
// temp directory as /tmp/forkmesh-worktrees/issue-<n>-s<id>; they now live
// inside the project they belong to, as <checkout>/.worktrees/agent-<id>-<desc>
// (adhoc #1624). Keeping them in the project means the tree an agent is editing
// sits beside the code it forked from — openable from the same file manager
// window, on the same filesystem as the checkout, and out of reach of the tmp
// reapers that delete "old" files under a long-running session.
namespace forkmesh::agentwt {

// Short slug from a session/issue title: keep only the significant words and
// cap the result at a few of them, so "occasionally the codex sessions bleed
// into each other" becomes "codex-sessions-bleed" rather than a 48-character
// transliteration of the whole sentence. Used for both the agent branch name
// and its worktree directory, so the two always read the same. The result is
// [a-z0-9-] only, which is what makes it safe to single-quote into the shell
// command that creates the worktree.
inline QString titleSlug(const QString &title, const QString &fallback)
{
    // Filler that makes a poor branch name; mirrors the commit drafter's
    // generic-name filter, but for prose.
    static const QSet<QString> kFiller = {
        QStringLiteral("a"),     QStringLiteral("an"),    QStringLiteral("and"),
        QStringLiteral("are"),   QStringLiteral("as"),    QStringLiteral("at"),
        QStringLiteral("be"),    QStringLiteral("but"),   QStringLiteral("can"),
        QStringLiteral("could"), QStringLiteral("do"),    QStringLiteral("dont"),
        QStringLiteral("each"),  QStringLiteral("for"),   QStringLiteral("from"),
        QStringLiteral("get"),   QStringLiteral("has"),   QStringLiteral("have"),
        QStringLiteral("i"),     QStringLiteral("id"),    QStringLiteral("if"),
        QStringLiteral("in"),    QStringLiteral("into"),  QStringLiteral("is"),
        QStringLiteral("it"),    QStringLiteral("its"),   QStringLiteral("just"),
        QStringLiteral("let"),   QStringLiteral("lets"),  QStringLiteral("like"),
        QStringLiteral("make"),  QStringLiteral("my"),    QStringLiteral("of"),
        QStringLiteral("on"),    QStringLiteral("or"),    QStringLiteral("other"),
        QStringLiteral("our"),   QStringLiteral("out"),   QStringLiteral("please"),
        QStringLiteral("should"),QStringLiteral("so"),    QStringLiteral("some"),
        QStringLiteral("that"),  QStringLiteral("the"),   QStringLiteral("their"),
        QStringLiteral("them"),  QStringLiteral("then"),  QStringLiteral("there"),
        QStringLiteral("these"), QStringLiteral("they"),  QStringLiteral("this"),
        QStringLiteral("to"),    QStringLiteral("too"),   QStringLiteral("up"),
        QStringLiteral("use"),   QStringLiteral("we"),    QStringLiteral("when"),
        QStringLiteral("with"),  QStringLiteral("would"), QStringLiteral("you"),
        QStringLiteral("your")};
    QStringList words{QString()};
    for (QChar ch : title.toLower()) {
        const char a = ch.toLatin1();
        if ((a >= 'a' && a <= 'z') || (a >= '0' && a <= '9'))
            words.last().append(ch);
        else if (!words.last().isEmpty())
            words.append(QString());
    }
    QString slug;
    int kept = 0;
    for (const QString &word : words) {
        if (word.isEmpty() || kFiller.contains(word))
            continue;
        if (!slug.isEmpty() &&
            (kept >= 4 || slug.size() + 1 + word.size() > 30))
            break;
        if (!slug.isEmpty())
            slug.append(QLatin1Char('-'));
        slug.append(word);
        ++kept;
    }
    if (slug.isEmpty()) {
        // All filler (or non-latin): fall back to the first raw words.
        for (const QString &word : words) {
            if (word.isEmpty())
                continue;
            if (!slug.isEmpty() &&
                (kept >= 4 || slug.size() + 1 + word.size() > 30))
                break;
            if (!slug.isEmpty())
                slug.append(QLatin1Char('-'));
            slug.append(word);
            ++kept;
        }
    }
    return slug.isEmpty() ? fallback : slug.left(30);
}

// Directory name for a session's worktree: agent-<id>-<short-desc>. The session
// id keeps it unique — a rerun of one issue must never land in an earlier run's
// tree — and the slug is the same summary the branch carries, so `git worktree
// list` and a shell prompt read as a list of tasks rather than of numbers.
inline QString dirName(int sessionId, const QString &title)
{
    const QString slug = titleSlug(title, QString());
    return slug.isEmpty()
               ? QStringLiteral("agent-%1").arg(sessionId)
               : QStringLiteral("agent-%1-%2").arg(QString::number(sessionId), slug);
}

// The directory agent worktrees are created under for `repoPath`.
//
// A checkout gets <checkout>/.worktrees. A bare network mirror does not: a node
// that only mirrors a repo can still run agents off it (adhoc #191), but its
// directory *is* the git dir, so those runs stay in the system temp location
// rather than scattering working trees through git's own object store. The
// caller handles the KVM case ahead of this, where worktrees have to land on
// the host directory that is mounted into the jail.
inline QString root(const QString &repoPath)
{
    const QString repo = repoPath.trimmed();
    if (!repo.isEmpty() &&
        QFileInfo::exists(QDir(repo).filePath(QStringLiteral(".git"))))
        return QDir(repo).filePath(QStringLiteral(".worktrees"));
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
           QStringLiteral("/forkmesh-worktrees");
}

// Create the worktree root and, inside a project, hide it from git. The trees
// underneath are full working copies; without this every `git status` in the
// project (and the SCM tab, and the commit drafter) would list thousands of
// untracked files. The ignore rule lives in the directory itself rather than in
// the project's .gitignore so ForkMesh never edits a repository it is only
// borrowing, and so it covers every project on the node. It cannot leak into
// the worktrees themselves: git reads ignore files from a working tree's own
// root downwards, and each worktree's root is the agent-… directory below this
// one.
inline void ensureRoot(const QString &root)
{
    QDir().mkpath(root);
    const QString marker = QDir(root).filePath(QStringLiteral(".gitignore"));
    if (QFileInfo::exists(marker))
        return;
    QFile file(marker);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write("# ForkMesh agent worktrees. Never part of the project.\n*\n");
}

// Single-quote a path for the `bash -lc` script that creates the worktree. The
// slug and session id are sanitised by construction, but the project path in
// front of them is whatever the user cloned into and may contain a quote.
inline QString shellQuote(const QString &path)
{
    QString escaped = path;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

} // namespace forkmesh::agentwt
