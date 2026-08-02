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
    // Reads tracked files only, records at most one sample per local day and
    // retains the latest 30 days in .forkmesh/stats/repository.json. A repo
    // with no stored history is backfilled from git so the charts start full.
    static QVector<RepoStatsSample> captureDaily(const QString &repoDir,
                                                 QString *error = nullptr);
    static QVector<RepoStatsSample> load(const QString &repoDir,
                                         QString *error = nullptr);
    // Ratchet state lives outside the work tree (in the shared git dir): every
    // worktree sees the same switch, and no checkout, merge or branch swap can
    // resurrect a setting the user turned off.
    static bool ratchetEnabled(const QString &repoDir);
    static bool setRatchetEnabled(const QString &repoDir, bool enabled,
                                  QString *error = nullptr);
    // Measures repository size: a commit may not push the tracked byte count
    // above the ceiling recorded at the start of the current day.
    static bool stagedCommitAllowed(const QString &repoDir, QString *reason);
    static QString agentGuidance(const QString &repoDir);

private:
    static RepoStatsSample measure(const QString &repoDir, QString *error);
    static QVector<RepoStatsSample> backfill(const QString &repoDir,
                                             const RepoStatsSample &today);
    static qint64 ceilingBytes(const QString &repoDir, const RepoStatsSample &now,
                               bool refreshStaleDay);
    static QString statsPath(const QString &repoDir);
    static QString statePath(const QString &repoDir);
};
