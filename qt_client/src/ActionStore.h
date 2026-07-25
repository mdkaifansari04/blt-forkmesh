#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

// Status values for a run. `AwaitingApproval` means the pushed workflow content
// is new or changed and must be human-approved before anything executes.
namespace ActionStatus {
inline const QString AwaitingApproval = QStringLiteral("awaiting-approval");
inline const QString Queued = QStringLiteral("queued");
inline const QString Running = QStringLiteral("running");
inline const QString Success = QStringLiteral("success");
inline const QString Failed = QStringLiteral("failed");
inline const QString Rejected = QStringLiteral("rejected");
// User stopped the run while it was queued or executing.
inline const QString Cancelled = QStringLiteral("cancelled");
// User skipped the run before it started (queued or awaiting approval).
inline const QString Skipped = QStringLiteral("skipped");
} // namespace ActionStatus

// One workflow run, persisted as <root>/runs/<owner>-<name>/<id>/meta.json with a
// sibling log.txt. Runs are local CI artifacts and are never committed to git.
struct ActionRun {
    int id = 0;
    QString owner;
    QString name;            // repository name
    QString workflowPath;    // ".forkmesh/deploy.yml"
    QString workflowName;    // display name from the YAML
    QString workflowContent; // raw YAML at the pushed commit (for diff/approval)
    QString commit;          // pushed newrev SHA
    QString ref;             // pushed refname
    // Approval is bound to the complete repository snapshot, not just the
    // workflow YAML. repositoryTree is Git's tree object id; executionDigest is
    // a SHA-256 over both the recursive tree manifest and the archived file
    // bytes. Changing a helper script, Makefile, package hook, submodule pin, or
    // any other tracked input therefore invalidates approval.
    QString repositoryTree;
    QString executionDigest;
    QString status = ActionStatus::Queued;
    qint64 createdAtMs = 0;
    qint64 startedAtMs = 0;
    qint64 finishedAtMs = 0;

    QString repoKey() const; // sanitized "<owner>-<name>"
    QJsonObject toJson() const;
    static ActionRun fromJson(const QJsonObject &obj);
};

// Persists run history and logs on disk, and owns the global variables store and
// the per-repo approved-workflow store (both in QSettings).
class ActionStore
{
public:
    explicit ActionStore(QString rootDir); // rootDir = <AppData>/actions

    QString spoolDir() const; // <root>/spool (push events land here)
    QString artifactsDir() const; // <root>/artifacts (validated run outputs)

    // Runs
    QList<ActionRun> loadAllRuns() const;    // newest first
    ActionRun createRun(ActionRun run);      // assigns id + timestamps, persists
    bool saveRun(const ActionRun &run) const; // rewrite meta.json
    bool deleteRun(const ActionRun &run) const; // remove the run's dir (meta+log)
    void appendLog(const ActionRun &run, const QString &text) const;
    QString readLog(const ActionRun &run) const;

    // Global variables (shared by all repos): QSettings key actions/variables.
    static QMap<QString, QString> variables();
    static void setVariables(const QMap<QString, QString> &vars);

    // Approved-workflow store, keyed per repo by file path. Trust is by exact
    // workflow content AND the complete repository state that can execute.
    static bool isApproved(const QString &repoKey, const QString &path,
                           const QString &content,
                           const QString &repositoryTree,
                           const QString &executionDigest);
    static void approve(const QString &repoKey, const QString &path,
                        const QString &content,
                        const QString &repositoryTree,
                        const QString &executionDigest);
    static QString lastApprovedContent(const QString &repoKey, const QString &path);

    // Resolve and hash an immutable repository snapshot without checking it
    // out. The digest includes the recursive Git tree record (including modes,
    // paths, object ids, and submodule pins) and a deterministic tar stream of
    // every archived file, then hashes that material with SHA-256. A bounded
    // timeout prevents a damaged repository from hanging the approval UI.
    static bool repositoryStateDigest(const QString &repository,
                                      const QString &commit,
                                      QString *repositoryTree,
                                      QString *executionDigest,
                                      QString *error = nullptr);

private:
    QString runsDir() const; // <root>/runs
    QString runDir(const ActionRun &run) const;
    int nextId() const;

    QString m_root;
};
