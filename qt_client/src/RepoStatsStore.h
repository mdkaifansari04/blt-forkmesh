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
    // retains the latest 30 days in .forkmesh/stats/repository.json.
    static QVector<RepoStatsSample> captureDaily(const QString &repoDir,
                                                 QString *error = nullptr);
    static QVector<RepoStatsSample> load(const QString &repoDir,
                                         QString *error = nullptr);
    // Ratchet enforcement is a local repository preference stored in
    // .git/config. Toggling it never modifies tracked repository files.
    static bool ratchetEnabled(const QString &repoDir);
    static bool setRatchetEnabled(const QString &repoDir, bool enabled,
                                  QString *error = nullptr);
    // Enforces both parts of Ratchet Mode for a staged commit: added LOC may
    // not exceed removed LOC, and today's tracked-byte ceiling may not grow.
    static bool stagedCommitAllowed(const QString &repoDir, QString *reason);
    static QString agentGuidance(const QString &repoDir);

private:
    static RepoStatsSample measure(const QString &repoDir, QString *error);
    static QString statsPath(const QString &repoDir);
};
