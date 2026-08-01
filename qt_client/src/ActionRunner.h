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



    int stepTimeoutMs = 90 * 60 * 1000;
    int jobTimeoutMs = 4 * 60 * 60 * 1000;
};







class ActionRunner : public QObject
{
    Q_OBJECT
public:
    explicit ActionRunner(ActionStore *store, QObject *parent = nullptr);

    bool busy() const { return m_busy; }

    int currentRunId() const { return m_busy ? m_run.id : -1; }

    void start(const ActionRun &run, const ActionWorkflow &workflow,
               const QString &mirrorPath, const QString &workTreePath,
               const QMap<QString, QString> &variables);



    void stop();



    void setSandboxLimitsForTesting(const ActionSandboxLimits &limits);


    static bool sandboxAvailable(QString *reason = nullptr);

signals:
    void logLine(int runId, const QString &text);
    void statusChanged(int runId, const QString &status);
    void finished(int runId, bool ok);



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






    bool landReleaseMetadata();





    bool landVersionHeader();
    bool landReleaseArtifacts();
    bool landActionArtifacts();
    bool copySandboxTree(const QString &source, const QString &destination,
                         qint64 maxBytes, QString *error);
    bool isReleaseRun() const;
    QString redact(QString text) const;

    ActionStore *m_store;
    bool m_busy = false;
    bool m_stopping = false;
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
    QString m_repoWorkTree;
    QString m_currentStepLabel;
    QString m_currentCommand;
    QMap<QString, QString> m_variables;
    QMap<QString, QString> m_exposedVariables;
    QStringList m_secrets;
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
