#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

struct RepoStatsSample {
    QString day;
    qint64 bytes = 0;
    qint64 lines = 0;
    qint64 files = 0;
};

class RepoStatsStore
{
public:
    static QVector<RepoStatsSample> captureDaily(const QString &repoDir,
                                                 QString *error = nullptr);
    static QVector<RepoStatsSample> load(const QString &repoDir,
                                         QString *error = nullptr);
    static bool ratchetEnabled(const QString &repoDir);
    static bool setRatchetEnabled(const QString &repoDir, bool enabled,
                                  QString *error = nullptr);
    static bool stagedCommitAllowed(const QString &repoDir, QString *reason);
    static QString agentGuidance(const QString &repoDir);

private:
    static RepoStatsSample measure(const QString &repoDir, QString *error);
    static QVector<RepoStatsSample> backfill(const QString &repoDir,
                                             const RepoStatsSample &today);
    static qint64 ceilingBytes(const QString &repoDir, const RepoStatsSample &now,
                               bool refreshStaleDay);
    static QString statsPath(const QString &repoDir);
};
