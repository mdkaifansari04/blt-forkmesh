#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

namespace ActionStatus {
inline const QString AwaitingApproval = QStringLiteral("awaiting-approval");
inline const QString Queued = QStringLiteral("queued");
inline const QString Running = QStringLiteral("running");
inline const QString Success = QStringLiteral("success");
inline const QString Failed = QStringLiteral("failed");
inline const QString Rejected = QStringLiteral("rejected");
inline const QString Cancelled = QStringLiteral("cancelled");
inline const QString Skipped = QStringLiteral("skipped");
} // namespace ActionStatus

struct ActionRun {
    int id = 0;
    QString owner;
    QString name;            // repository name
    QString workflowPath;    // ".forkmesh/deploy.yml"
    QString workflowName;    // display name from the YAML
    QString workflowContent; // raw YAML at the pushed commit (for diff/approval)
    QString commit;          // pushed newrev SHA
    QString ref;             // pushed refname
    QString repositoryTree;
    QString executionDigest;
    QString status = ActionStatus::Queued;
    qint64 createdAtMs = 0;
    qint64 startedAtMs = 0;
    qint64 finishedAtMs = 0;

    QString repoKey() const;
    QString legacyRepoKey() const;
    QJsonObject toJson() const;
    static ActionRun fromJson(const QJsonObject &obj);
};

namespace ActionNeeds {

enum class State {
    Ready,   // every dependency succeeded, or isn't scheduled on this node
    Waiting, // a dependency is still awaiting approval, queued, or running
    Blocked, // a dependency finished without succeeding
};

bool matches(const QString &token, const QString &workflowName,
             const QString &workflowPath);

State resolve(const ActionRun &run, const QStringList &needs,
              const QList<ActionRun> &history, QString *detail = nullptr);

} // namespace ActionNeeds

class ActionStore
{
public:
    explicit ActionStore(QString rootDir); // rootDir = <AppData>/actions

    QString spoolDir() const; // <root>/spool (push events land here)
    QString artifactsDir() const; // <root>/artifacts (validated run outputs)
    QString sandboxDir() const; // <root>/sandbox (disposable per-run trees)

    QList<ActionRun> loadAllRuns() const;    // newest first
    ActionRun createRun(ActionRun run);      // assigns id + timestamps, persists
    bool saveRun(const ActionRun &run) const; // rewrite meta.json
    bool deleteRun(const ActionRun &run) const; // remove the run's dir (meta+log)
    void appendLog(const ActionRun &run, const QString &text) const;
    QString readLog(const ActionRun &run) const;

    static QMap<QString, QString> variables();
    static void setVariables(const QMap<QString, QString> &vars);

    static bool isApproved(const QString &repoKey, const QString &path,
                           const QString &content,
                           const QString &repositoryTree,
                           const QString &executionDigest);
    static void approve(const QString &repoKey, const QString &path,
                        const QString &content,
                        const QString &repositoryTree,
                        const QString &executionDigest);
    static QString lastApprovedContent(const QString &repoKey, const QString &path);

    static bool repositoryStateDigest(const QString &repository,
                                      const QString &commit,
                                      QString *repositoryTree,
                                      QString *executionDigest,
                                      QString *error = nullptr);

private:
    QString runsDir() const; // <root>/runs
    QString runDir(const ActionRun &run) const;
    QString legacyRunDir(const ActionRun &run) const;
    QString existingRunDir(const ActionRun &run) const;
    void migrateLegacyRecords();
    int nextId() const;

    QString m_root;
};
