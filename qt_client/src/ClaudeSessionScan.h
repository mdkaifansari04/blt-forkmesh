#pragma once

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>








struct ExternalClaudeSession {
    QString uuid;
    QString path;
    QString cwd;
    QString gitBranch;
    QString title;
    qint64 lastActivityMs = 0;
};

namespace ClaudeSessionScan {




QList<ExternalClaudeSession> scan(const QString &repoLocalPath,
                                  const QSet<QString> &excludeCwds,
                                  qint64 activeWindowMs);





QList<QJsonObject> readEvents(const QString &path, qint64 fromOffset,
                              qint64 *newOffset);




qint64 tailStartOffset(const QString &path, qint64 maxBytes);







QList<qint64> findSessionPids(const QString &uuid, const QString &cwd);

}
