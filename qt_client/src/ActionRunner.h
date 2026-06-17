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

    void start(const ActionRun &run, const ActionWorkflow &workflow,
               const QString &mirrorPath,
               const QMap<QString, QString> &variables);

signals:
    void logLine(int runId, const QString &text);
    void statusChanged(int runId, const QString &status);
    void finished(int runId, bool ok);

private:
    enum class Phase { Idle, Checkout, Step };

    void launch(Phase phase, const QString &program, const QStringList &args,
                const QString &workingDir);
    void onProcessFinished(int exitCode);
    void runNextStep();
    void emitLog(const QString &text);
    void complete(bool ok, const QString &finalMessage);
    void cleanupWorktree();
    QString redact(QString text) const;

    ActionStore *m_store;
    bool m_busy = false;
    Phase m_phase = Phase::Idle;
    ActionRun m_run;
    ActionWorkflow m_workflow;
    QString m_mirror;
    QString m_worktree;
    QMap<QString, QString> m_variables;
    QStringList m_secrets; // values to redact from logs
    int m_stepIndex = 0;
    QProcess *m_process = nullptr;
};
