#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include "ActionFile.h"
#include "ActionStore.h"

class QProcess;

// Executes a single approved workflow run. Checks the pushed commit out into a
// detached worktree of the bare mirror, then runs each step's `run:` through a
// shell with the global variables injected into the environment. Output is
// streamed live (signal) and persisted to the run's log.txt, with secret values
// redacted. One run at a time; MainWindow serializes a queue through it.
class ActionRunner : public QObject
{
    Q_OBJECT
public:
    explicit ActionRunner(ActionStore *store, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    // The id of the run currently executing, or -1 when idle.
    int currentRunId() const { return m_busy ? m_run.id : -1; }

    void start(const ActionRun &run, const ActionWorkflow &workflow,
               const QString &mirrorPath, const QString &workTreePath,
               const QMap<QString, QString> &variables);

    // Abort the in-flight run: signal the step's process group, clean up the
    // throwaway worktree and record the run as Cancelled. No-op when idle.
    void stop();

signals:
    void logLine(int runId, const QString &text);
    void statusChanged(int runId, const QString &status);
    void finished(int runId, bool ok);
    // Emitted after a successful release run lands new release metadata into the
    // working copy, so MainWindow can publish it (sync the served mirror) and the
    // installer / Releases panel can see the attached artifact.
    void releaseMetadataLanded(int runId);

private:
    enum class Phase { Idle, Checkout, Step };

    void launch(Phase phase, const QString &program, const QStringList &args,
                const QString &workingDir);
    void onProcessFinished(int exitCode);
    void runNextStep();
    void emitLog(const QString &text);
    void complete(bool ok, const QString &finalMessage);
    void cleanupWorktree();
    // For a release run (ref under refs/tags/), copy the release metadata the
    // workflow produced in the throwaway worktree (releases/<channel>/*) into the
    // owner's working copy and commit it, so it survives worktree cleanup and can
    // be published. The artifact bytes are already in the served CAS (the run's
    // FORKMESH_RELEASE_CAS points at <mirror>/forkmesh-releases). Returns true if
    // a new metadata commit was made.
    bool landReleaseMetadata();
    // Propagate the version header (project(ForkMesh VERSION X.Y.Z ...) the
    // release workflow rewrote in the worktree's qt_client/CMakeLists.txt) into
    // the owner's working copy, so the committed source version tracks the
    // release tag. Surgically rewrites only the version token on that one line —
    // never any unrelated edits — and returns true when the working copy changed.
    bool landVersionHeader();
    bool isReleaseRun() const;
    QString redact(QString text) const;

    ActionStore *m_store;
    bool m_busy = false;
    bool m_stopping = false; // a stop() was requested; complete() records Cancelled
    Phase m_phase = Phase::Idle;
    ActionRun m_run;
    ActionWorkflow m_workflow;
    QString m_mirror;
    QString m_worktree;
    QString m_repoWorkTree; // owner's working copy (may be empty on a mirror-only node)
    QMap<QString, QString> m_variables;
    QStringList m_secrets; // values to redact from logs
    int m_stepIndex = 0;
    QProcess *m_process = nullptr;
};
