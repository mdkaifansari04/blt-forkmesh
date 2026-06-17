#include "ActionRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>

ActionRunner::ActionRunner(ActionStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
}

void ActionRunner::start(const ActionRun &run, const ActionWorkflow &workflow,
                         const QString &mirrorPath,
                         const QMap<QString, QString> &variables)
{
    m_busy = true;
    m_run = run;
    m_workflow = workflow;
    m_mirror = mirrorPath;
    m_variables = variables;
    m_stepIndex = 0;

    m_secrets.clear();
    for (const QString &value : variables.values())
        if (!value.isEmpty())
            m_secrets.append(value);

    m_run.status = ActionStatus::Running;
    m_run.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_store->saveRun(m_run);
    emit statusChanged(m_run.id, m_run.status);

    emitLog(QStringLiteral("==> Workflow: %1 (%2)")
                .arg(m_workflow.name, m_workflow.path));
    emitLog(QStringLiteral("==> Commit:   %1 on %2")
                .arg(m_run.commit.left(12), m_run.ref));
    emitLog(QStringLiteral("==> Checking out into a temporary worktree..."));

    m_worktree =
        QDir::tempPath() + QStringLiteral("/forkmesh-run-") +
        QString::number(m_run.id) + QStringLiteral("-") +
        QString::number(QDateTime::currentMSecsSinceEpoch());

    launch(Phase::Checkout, QStringLiteral("git"),
           {QStringLiteral("-C"), m_mirror, QStringLiteral("worktree"),
            QStringLiteral("add"), QStringLiteral("--detach"), m_worktree,
            m_run.commit},
           QString());
}

void ActionRunner::launch(Phase phase, const QString &program,
                          const QStringList &args, const QString &workingDir)
{
    m_phase = phase;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    if (!workingDir.isEmpty())
        m_process->setWorkingDirectory(workingDir);

    // Inherit the system environment, add the global variables (so wrangler sees
    // CLOUDFLARE_API_TOKEN), and make sure user-local tool dirs are on PATH so
    // uvx/cargo-installed tools resolve.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = m_variables.constBegin(); it != m_variables.constEnd(); ++it)
        env.insert(it.key(), it.value());
    const QString home = QDir::homePath();
    const QString extraPath = home + QStringLiteral("/.local/bin:") + home +
                              QStringLiteral("/.cargo/bin");
    env.insert(QStringLiteral("PATH"),
               extraPath + QLatin1Char(':') +
                   env.value(QStringLiteral("PATH")));
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        emitLog(QString::fromUtf8(m_process->readAllStandardOutput()));
    });
    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus) {
                onProcessFinished(exitCode);
            });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                if (m_process)
                    emitLog(QStringLiteral("!! ") + m_process->errorString());
            });

    m_process->setProgram(program);
    m_process->setArguments(args);
    m_process->start();
}

void ActionRunner::onProcessFinished(int exitCode)
{
    // Drain any tail output before tearing the process down.
    if (m_process) {
        const QByteArray tail = m_process->readAllStandardOutput();
        if (!tail.isEmpty())
            emitLog(QString::fromUtf8(tail));
        m_process->deleteLater();
        m_process = nullptr;
    }

    if (m_phase == Phase::Checkout) {
        if (exitCode != 0) {
            complete(false, QStringLiteral("Checkout failed."));
            return;
        }
        runNextStep();
        return;
    }

    // A step finished.
    if (exitCode != 0) {
        complete(false, QStringLiteral("Step failed with exit code %1.")
                            .arg(exitCode));
        return;
    }
    ++m_stepIndex;
    runNextStep();
}

void ActionRunner::runNextStep()
{
    if (m_stepIndex >= m_workflow.steps.size()) {
        complete(true, QStringLiteral("All steps completed."));
        return;
    }

    const ActionStep &step = m_workflow.steps.at(m_stepIndex);
    const QString command = ActionFile::substitute(step.run, m_variables);
    const QString label =
        step.name.isEmpty() ? QStringLiteral("Step %1").arg(m_stepIndex + 1)
                            : step.name;
    emitLog(QString());
    emitLog(QStringLiteral("==> %1").arg(label));

#ifdef Q_OS_WIN
    launch(Phase::Step, QStringLiteral("cmd"),
           {QStringLiteral("/c"), command}, m_worktree);
#else
    const QString shell =
        QFile::exists(QStringLiteral("/bin/bash")) ? QStringLiteral("/bin/bash")
                                                   : QStringLiteral("/bin/sh");
    launch(Phase::Step, shell, {QStringLiteral("-c"), command}, m_worktree);
#endif
}

void ActionRunner::complete(bool ok, const QString &finalMessage)
{
    emitLog(QString());
    emitLog((ok ? QStringLiteral("==> SUCCESS: ")
                : QStringLiteral("==> FAILED: ")) +
            finalMessage);

    cleanupWorktree();

    m_run.status = ok ? ActionStatus::Success : ActionStatus::Failed;
    m_run.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_store->saveRun(m_run);
    m_phase = Phase::Idle;
    m_busy = false;
    emit statusChanged(m_run.id, m_run.status);
    emit finished(m_run.id, ok);
}

void ActionRunner::cleanupWorktree()
{
    if (m_worktree.isEmpty())
        return;
    // Best-effort: detach the worktree from the mirror, then remove the dir.
    QProcess::execute(QStringLiteral("git"),
                      {QStringLiteral("-C"), m_mirror, QStringLiteral("worktree"),
                       QStringLiteral("remove"), QStringLiteral("--force"),
                       m_worktree});
    QDir(m_worktree).removeRecursively();
    m_worktree.clear();
}

void ActionRunner::emitLog(const QString &text)
{
    const QString safe = redact(text);
    m_store->appendLog(m_run, safe);
    emit logLine(m_run.id, safe);
}

QString ActionRunner::redact(QString text) const
{
    for (const QString &secret : m_secrets)
        if (!secret.isEmpty())
            text.replace(secret, QStringLiteral("***"));
    return text;
}
