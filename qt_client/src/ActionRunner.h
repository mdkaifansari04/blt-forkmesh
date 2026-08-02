#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QtGlobal>
#include <QString>
#include <QStringList>

#include "ActionFile.h"
#include "ActionStore.h"

class QProcess;
class QTimer;

struct ActionSandboxLimits {
    qint64 maxMemoryBytes = 4LL * 1024 * 1024 * 1024;
    qint64 maxFileBytes = 1024LL * 1024 * 1024;
    qint64 maxWorkspaceBytes = 2LL * 1024 * 1024 * 1024;
    int maxProcesses = 128;
    int maxOpenFiles = 256;
    int maxCpuSeconds = 20 * 60;
    // A release step compiles the whole desktop client from scratch inside the
    // sandbox's CPU quota, which took longer than the old 30-minute deadline —
    // every release since v0.7.0 was SIGTERMed mid-build (adhoc #329).
    int stepTimeoutMs = 90 * 60 * 1000;
    int jobTimeoutMs = 4 * 60 * 60 * 1000;
};

// Executes a single approved workflow run. On Linux, every step runs fail-closed
// inside a bubblewrap user/PID/mount/IPC/UTS namespace as uid 65534, with the
// source snapshot mounted read-only, a disposable writable clone, no host home
// or environment, network disabled by default, and hard resource/deadline
// limits. Output is streamed live (signal) and persisted to the run's log.txt,
// with explicit workflow variables redacted.
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

    // Tests use short deadlines and smaller quotas without weakening the fixed
    // production defaults above.
    void setSandboxLimitsForTesting(const ActionSandboxLimits &limits);

    // A node must never silently fall back to unsandboxed execution.
    static bool sandboxAvailable(QString *reason = nullptr);

signals:
    void logLine(int runId, const QString &text);
    void statusChanged(int runId, const QString &status);
    void finished(int runId, bool ok);
    // Emitted after a successful release run lands new release metadata into the
    // working copy, so MainWindow can publish it (sync the served mirror) and the
    // installer / Releases panel can see the attached artifact.
    void releaseMetadataLanded(int runId);

private:
    enum class Phase {
        Idle,
        Checkout,
        WorkspaceClone,
        WorkspaceCheckout,
        Step
    };

    void launch(Phase phase, const QString &program, const QStringList &args,
                const QString &workingDir);
    void launchSandboxedStep(const QString &command);
    QStringList sandboxArguments(const QString &shell,
                                 const QString &command) const;
    QMap<QString, QString> explicitWorkflowVariables() const;
    bool verifyWorkspaceSnapshot(QString *reason = nullptr) const;
    bool workspaceWithinQuota(QString *reason = nullptr) const;
    void terminateCurrentProcess(bool timedOut);
    void onProcessFinished(int exitCode);
    void runNextStep();
    void emitLog(const QString &text);
    void emitProcessOutput(const QByteArray &bytes);
    void emitProcessOutputTruncationNotice();
    void rememberSuppressedProcessOutput(const QByteArray &bytes);
    void emitSuppressedProcessOutputTail();
    QString normalizeProcessOutputForLog(const QString &text);
    void rememberCrashProcessOutput(const QByteArray &bytes);
    QString phaseName() const;
    QString crashContext() const;
    void updateCrashContext() const;
    void logFailureDiagnostic(const QString &finalMessage) const;
    void complete(bool ok, const QString &finalMessage);
    void cleanupWorktree();
    // For a release run (ref under refs/tags/), copy the release metadata the
    // workflow produced in the throwaway worktree (.forkmesh/releases/<channel>/*) into the
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
    bool landReleaseArtifacts();
    bool landActionArtifacts();
    bool copySandboxTree(const QString &source, const QString &destination,
                         qint64 maxBytes, QString *error);
    bool isReleaseRun() const;
    QString redact(QString text) const;

    ActionStore *m_store;
    bool m_busy = false;
    bool m_stopping = false; // a stop() was requested; complete() records Cancelled
    Phase m_phase = Phase::Idle;
    ActionRun m_run;
    ActionWorkflow m_workflow;
    QString m_mirror;
    QString m_sandboxRoot;
    QString m_worktree;
    QString m_workspace;
    QString m_sandboxHome;
    QString m_sandboxTmp;
    QString m_releaseStaging;
    QString m_repoWorkTree; // owner's working copy (may be empty on a mirror-only node)
    QString m_currentStepLabel;
    QString m_currentCommand;
    QMap<QString, QString> m_variables;
    QMap<QString, QString> m_exposedVariables;
    QStringList m_secrets; // values to redact from logs
    bool m_networkAllowed = false;
    ActionSandboxLimits m_limits;
    int m_stepIndex = 0;
    qsizetype m_processOutputBytes = 0;
    qsizetype m_processOutputSuppressedBytes = 0;
    qsizetype m_processOutputLineChars = 0;
    QByteArray m_processOutputTail;
    QByteArray m_crashOutputTail;
    bool m_processOutputTruncated = false;
    bool m_processTimedOut = false;
    bool m_jobTimedOut = false;
    QByteArray m_processStdin;
    QString m_systemdUnit;
    QTimer *m_stepTimer = nullptr;
    QTimer *m_jobTimer = nullptr;
    QProcess *m_process = nullptr;
};
