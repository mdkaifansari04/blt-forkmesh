#pragma once

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

struct ExternalClaudeSession {
    QString uuid;           // jsonl file stem == the CLI's session id
    QString path;           // absolute path to the .jsonl transcript
    QString cwd;            // working directory the session runs in
    QString gitBranch;      // git branch recorded in the transcript (may be empty)
    QString title;          // the session's AI-generated title, when present
    qint64 lastActivityMs = 0; // transcript mtime — last time it produced output
};

namespace ClaudeSessionScan {

QList<ExternalClaudeSession> scan(const QString &repoLocalPath,
                                  const QSet<QString> &excludeCwds,
                                  qint64 activeWindowMs);

QList<QJsonObject> readEvents(const QString &path, qint64 fromOffset,
                              qint64 *newOffset);

qint64 tailStartOffset(const QString &path, qint64 maxBytes);

QList<qint64> findSessionPids(const QString &uuid, const QString &cwd);

} // namespace ClaudeSessionScan
