#pragma once

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

// Detects Claude Code CLI sessions that are running on this machine outside
// ForkMesh (e.g. in a terminal or another editor) by reading the transcripts the
// CLI writes under ~/.claude/projects/<cwd-slug>/<session-uuid>.jsonl. A session
// whose transcript was written within a recent window is treated as "active".
//
// This is read-only: it never launches or signals those processes. ForkMesh just
// mirrors their progress so you can watch a session you started elsewhere.
struct ExternalClaudeSession {
    QString uuid;           // jsonl file stem == the CLI's session id
    QString path;           // absolute path to the .jsonl transcript
    QString cwd;            // working directory the session runs in
    QString gitBranch;      // git branch recorded in the transcript (may be empty)
    QString title;          // the session's AI-generated title, when present
    qint64 lastActivityMs = 0; // transcript mtime — last time it produced output
};

namespace ClaudeSessionScan {

// Active sessions whose cwd is at (or under) repoLocalPath and whose transcript
// changed within activeWindowMs. excludeCwds are directories ForkMesh itself is
// driving (its own worktrees), so its own runs aren't reported as "external".
QList<ExternalClaudeSession> scan(const QString &repoLocalPath,
                                  const QSet<QString> &excludeCwds,
                                  qint64 activeWindowMs);

// Parse the transcript's JSON-object lines from byte offset fromOffset to the
// last complete line; *newOffset is advanced past what was consumed (a trailing
// partial line is left for the next read). Used both for the initial render and
// for tailing a live session.
QList<QJsonObject> readEvents(const QString &path, qint64 fromOffset,
                              qint64 *newOffset);

// A line-aligned byte offset near the end of the file so an initial render only
// covers the last ~maxBytes (huge transcripts would otherwise build thousands of
// widgets). Returns 0 when the file is smaller than maxBytes.
qint64 tailStartOffset(const QString &path, qint64 maxBytes);

// OS process ids of the running `claude` CLI that belong to a detected session,
// so ForkMesh can stop it (the scan otherwise never signals these processes).
// A resumed session carries `--resume <uuid>` in its argv, giving an exact match;
// a fresh session doesn't expose its uuid, so we fall back to every `claude`
// process whose working directory is cwd. Returns the exact matches when any are
// found, otherwise the cwd matches. Linux-only (reads /proc); empty elsewhere.
QList<qint64> findSessionPids(const QString &uuid, const QString &cwd);

} // namespace ClaudeSessionScan
