#include "AgentRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString shellQuote(QString value)
{
    value.replace('\'', QStringLiteral("'\\''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}

bool runCapture(const QString &dir, const QStringList &args, QByteArray *out,
                QString *error = nullptr)
{
    QProcess process;
    process.start(QStringLiteral("git"), QStringList{QStringLiteral("-C"), dir} + args);
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error)
            *error = QStringLiteral("git timed out");
        return false;
    }
    if (out)
        *out = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}

QString providerTitle(const QString &provider)
{
    if (provider == QLatin1String("claude"))
        return QStringLiteral("Claude Code");
    return QStringLiteral("Codex");
}

} // namespace

AgentRunner::AgentRunner(AgentStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
}

void AgentRunner::start(const AgentSession &session, const Issue &issue,
                        const QString &repoPath, const Config &config)
{
    if (m_busy || !m_store)
        return;

    m_busy = true;
    m_stopping = false;
    m_phase = Phase::Idle;
    m_session = session;
    m_issue = issue;
    m_repoPath = repoPath;
    m_config = config;
    m_prompt = buildPrompt();

    m_session.status = AgentStatus::Running;
    m_session.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_session.contextWindow = m_config.contextWindow;
    m_session.maxOutputTokens = m_config.maxOutputTokens;
    m_session.promptTokens += estimateTokens(m_prompt);
    refreshUsage();
    m_store->saveSession(m_session);
    emit statusChanged(m_session.id, m_session.status);

    emitLog(QStringLiteral("==> Agent: %1").arg(providerTitle(m_session.provider)));
    emitLog(QStringLiteral("==> Issue: #%1 %2")
                .arg(m_session.issueNumber)
                .arg(m_session.issueTitle));
    emitLog(QStringLiteral("==> Budget: context %1 tokens, max output %2 tokens")
                .arg(m_config.contextWindow)
                .arg(m_config.maxOutputTokens));
    if (!m_config.model.trimmed().isEmpty())
        emitLog(QStringLiteral("==> Model: %1").arg(m_config.model.trimmed()));

    if (m_config.command.trimmed().isEmpty()) {
        complete(false, AgentStatus::Waiting,
                 QStringLiteral("No command is configured for this agent."));
        return;
    }
    if (!QDir(m_repoPath).exists(QStringLiteral(".git"))) {
        complete(false, AgentStatus::Failed,
                 QStringLiteral("No writable git checkout is available for this repo."));
        return;
    }

    QByteArray base;
    QString gitError;
    if (!runCapture(m_repoPath, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
                    &base, &gitError)) {
        complete(false, AgentStatus::Failed,
                 QStringLiteral("Could not read the base commit: %1").arg(gitError));
        return;
    }
    m_session.baseRef = QString::fromUtf8(base).trimmed();
    if (m_session.branchName.isEmpty()) {
        m_session.branchName =
            QStringLiteral("agent/issue-%1-%2-%3")
                .arg(m_session.issueNumber)
                .arg(m_session.provider)
                .arg(m_session.id);
    }
    m_store->saveSession(m_session);

    m_worktree = QDir::tempPath() + QStringLiteral("/forkmesh-agent-") +
                 QString::number(m_session.id) + QLatin1Char('-') +
                 QString::number(QDateTime::currentMSecsSinceEpoch());
    emitLog(QStringLiteral("==> Creating temporary worktree %1").arg(m_worktree));
    launch(Phase::Worktree, QStringLiteral("git"),
           {QStringLiteral("-C"), m_repoPath, QStringLiteral("worktree"),
            QStringLiteral("add"), QStringLiteral("-B"), m_session.branchName,
            m_worktree, m_session.baseRef});
}

void AgentRunner::stop()
{
    if (!m_busy)
        return;
    m_stopping = true;
    emitLog(QStringLiteral("==> Stop requested by user."));
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(1500))
            m_process->kill();
        return;
    }
    complete(false, AgentStatus::Stopped, QStringLiteral("Stopped."));
}

void AgentRunner::steer(const QString &prompt)
{
    const QString trimmed = prompt.trimmed();
    if (trimmed.isEmpty() || !m_busy)
        return;
    m_session.promptTokens += estimateTokens(trimmed);
    refreshUsage();
    m_store->saveSession(m_session);
    emitLog(QStringLiteral("\n==> User steering prompt\n%1").arg(trimmed));
    if (m_process && m_process->state() == QProcess::Running) {
        const QString text =
            QStringLiteral("\n\nAdditional user instruction:\n%1\n").arg(trimmed);
        m_process->write(text.toUtf8());
    } else {
        emitLog(QStringLiteral("==> Agent process is not accepting input right now."));
    }
}

void AgentRunner::launch(Phase phase, const QString &program,
                         const QStringList &args, const QString &workingDir)
{
    m_phase = phase;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    if (!workingDir.isEmpty())
        m_process->setWorkingDirectory(workingDir);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (m_config.preferApiKeyAuth && !m_config.isolatedHome.isEmpty()) {
        QDir().mkpath(m_config.isolatedHome);
        env.insert(QStringLiteral("CODEX_HOME"), m_config.isolatedHome);
    }
    if (!m_config.apiKeyName.isEmpty() && !m_config.apiKey.isEmpty()) {
        const QString key = m_config.apiKey.trimmed();
        env.insert(m_config.apiKeyName, key);
        if (m_session.provider == QLatin1String("codex"))
            env.insert(QStringLiteral("OPENAI_API_KEY"), key);
    }
    if (!m_config.model.trimmed().isEmpty())
        env.insert(QStringLiteral("FORKMESH_AGENT_MODEL"), m_config.model.trimmed());
    env.insert(QStringLiteral("FORKMESH_AGENT_CONTEXT_WINDOW"),
               QString::number(m_config.contextWindow));
    env.insert(QStringLiteral("FORKMESH_AGENT_MAX_OUTPUT_TOKENS"),
               QString::number(m_config.maxOutputTokens));
    const QString home = QDir::homePath();
    const QString extraPath = home + QStringLiteral("/.local/bin:") + home +
                              QStringLiteral("/.cargo/bin:") + home +
                              QStringLiteral("/.npm-global/bin");
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
            [this](QProcess::ProcessError error) {
                if (!m_process)
                    return;
                emitLog(QStringLiteral("!! ") + m_process->errorString());
                if (error == QProcess::FailedToStart)
                    complete(false, AgentStatus::Failed, m_process->errorString());
            });

    m_process->setProgram(program);
    m_process->setArguments(args);
    m_process->start();
}

void AgentRunner::onProcessFinished(int exitCode)
{
    if (m_process) {
        const QByteArray tail = m_process->readAllStandardOutput();
        if (!tail.isEmpty())
            emitLog(QString::fromUtf8(tail));
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_stopping) {
        complete(false, AgentStatus::Stopped, QStringLiteral("Stopped."));
        return;
    }

    if (m_phase == Phase::Worktree) {
        if (exitCode != 0) {
            complete(false, AgentStatus::Failed,
                     QStringLiteral("Could not create the worktree."));
            return;
        }
        runAgentProcess();
        return;
    }

    if (exitCode != 0) {
        QString message =
            QStringLiteral("Agent command exited with code %1.").arg(exitCode);
        const QString log = m_store ? m_store->readLog(m_session) : QString();
        if (m_session.provider == QLatin1String("codex") &&
            log.contains(QStringLiteral("401 Unauthorized"), Qt::CaseInsensitive)) {
            message += QStringLiteral(
                " OpenAI rejected the API key; rotate it and re-enter it in Settings.");
        }
        complete(false, AgentStatus::Failed, message);
        return;
    }

    QByteArray diff;
    QString diffError;
    if (runCapture(m_worktree,
                   {QStringLiteral("diff"), QStringLiteral("--binary"),
                    m_session.baseRef},
                   &diff, &diffError)) {
        const QString patch = QString::fromUtf8(diff);
        m_store->writePatch(m_session, patch);
        emitLog(patch.trimmed().isEmpty()
                    ? QStringLiteral("==> No code changes were produced.")
                    : QStringLiteral("==> Captured patch for PR creation."));
    } else {
        emitLog(QStringLiteral("!! Could not capture patch: %1").arg(diffError));
    }

    complete(true, AgentStatus::Success, QStringLiteral("Agent session completed."));
}

void AgentRunner::runAgentProcess()
{
    const QString promptPath = m_worktree + QStringLiteral("/.forkmesh-agent-prompt.md");
    QFile promptFile(promptPath);
    if (!promptFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        complete(false, AgentStatus::Failed,
                 QStringLiteral("Could not write the prompt file."));
        return;
    }
    promptFile.write(m_prompt.toUtf8());
    promptFile.close();

    if (!m_config.apiKey.isEmpty() && m_config.preferApiKeyAuth &&
        !m_config.isolatedHome.isEmpty()) {
        emitLog(QStringLiteral("==> Using %1 with isolated CLI home for API-key auth.")
                    .arg(m_config.apiKeyName));
    } else if (m_config.apiKey.isEmpty()) {
        emitLog(QStringLiteral("==> No API key set; relying on the local CLI auth if available."));
    }

    const QString command = expandCommand(promptPath);
    emitLog(QStringLiteral("==> Running configured command."));
#ifdef Q_OS_WIN
    launch(Phase::Agent, QStringLiteral("cmd"), {QStringLiteral("/c"), command},
           m_worktree);
#else
    const QString shell =
        QFile::exists(QStringLiteral("/bin/bash")) ? QStringLiteral("/bin/bash")
                                                   : QStringLiteral("/bin/sh");
    launch(Phase::Agent, shell, {QStringLiteral("-lc"), command}, m_worktree);
#endif
}

void AgentRunner::complete(bool ok, const QString &status, const QString &message)
{
    if (!m_busy && m_phase == Phase::Idle)
        return;
    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    emitLog((ok ? QStringLiteral("==> SUCCESS: ") : QStringLiteral("==> ")) + message);
    refreshUsage();
    cleanupWorktree();
    m_session.status = status;
    m_session.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    if (!ok)
        m_session.lastError = message;
    m_store->saveSession(m_session);
    m_phase = Phase::Idle;
    m_busy = false;
    emit statusChanged(m_session.id, m_session.status);
    emit finished(m_session.id, ok);
}

void AgentRunner::cleanupWorktree()
{
    if (m_worktree.isEmpty())
        return;
    QProcess::execute(QStringLiteral("git"),
                      {QStringLiteral("-C"), m_repoPath, QStringLiteral("worktree"),
                       QStringLiteral("remove"), QStringLiteral("--force"),
                       m_worktree});
    QDir(m_worktree).removeRecursively();
    m_worktree.clear();
}

QString AgentRunner::buildPrompt() const
{
    QStringList prompt;
    prompt << QStringLiteral("You are running inside ForkMesh as a coding agent.");
    prompt << QStringLiteral("Use the minimum context and output needed. Inspect only files relevant to the issue.");
    prompt << QStringLiteral("Do not spend extra tokens on broad refactors or unrelated cleanup.");
    prompt << QStringLiteral("When possible, make the smallest patch that satisfies the issue.");
    prompt << QStringLiteral("Do not commit, push, or open network resources unless the issue explicitly requires it.");
    prompt << QStringLiteral("");
    prompt << QStringLiteral("Repository: %1/%2").arg(m_session.owner, m_session.name);
    prompt << QStringLiteral("Issue #%1: %2")
                  .arg(m_session.issueNumber)
                  .arg(m_session.issueTitle);
    prompt << QStringLiteral("");
    prompt << QStringLiteral("Issue thread:");
    for (const IssueEvent &ev : m_issue.events) {
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        if (ev.type == QLatin1String("open") || ev.type == QLatin1String("comment")) {
            prompt << QStringLiteral("--- %1 by %2 ---").arg(ev.type, who);
            prompt << ev.body.left(6000);
        } else if (ev.type == QLatin1String("labels")) {
            prompt << QStringLiteral("--- labels: %1 ---").arg(ev.labels.join(", "));
        } else if (ev.type == QLatin1String("assignees")) {
            prompt << QStringLiteral("--- assignees: %1 ---").arg(ev.assignees.join(", "));
        }
    }
    if (m_store && m_session.startedAtMs > 0) {
        const QString transcript = m_store->readLog(m_session).trimmed();
        if (!transcript.isEmpty()) {
            prompt << QStringLiteral(
                "\nPrevious session transcript (most recent part):");
            prompt << transcript.right(12000);
        }
    }
    if (m_session.createPr)
        prompt << QStringLiteral("\nA pull request should be created from your patch after this run.");
    prompt << QStringLiteral("\nReturn concise progress and final notes.");
    return prompt.join(QLatin1Char('\n'));
}

QString AgentRunner::expandCommand(const QString &promptPath) const
{
    QString command = m_config.command.trimmed();
    const bool hadPromptFile = command.contains(QStringLiteral("{promptFile}"));
    const QString model = m_config.model.trimmed();
    command.replace(QStringLiteral("{modelArg}"),
                    model.isEmpty()
                        ? QString()
                        : QStringLiteral("-m %1").arg(shellQuote(model)));
    command.replace(QStringLiteral("{model}"), shellQuote(model));
    command.replace(QStringLiteral("{promptFile}"), shellQuote(promptPath));
    command.replace(QStringLiteral("{issueNumber}"),
                    QString::number(m_session.issueNumber));
    command.replace(QStringLiteral("{provider}"), m_session.provider);
    command.replace(QStringLiteral("{contextWindow}"),
                    QString::number(m_config.contextWindow));
    command.replace(QStringLiteral("{maxOutputTokens}"),
                    QString::number(m_config.maxOutputTokens));
    if (!hadPromptFile)
        command += QStringLiteral(" < ") + shellQuote(promptPath);
    return command;
}

QString AgentRunner::redact(QString text) const
{
    const QString key = m_config.apiKey.trimmed();
    if (!m_config.apiKey.isEmpty())
        text.replace(m_config.apiKey, QStringLiteral("***"));
    if (!key.isEmpty())
        text.replace(key, QStringLiteral("***"));
    text.replace(QRegularExpression(QStringLiteral("\\bsk-[A-Za-z0-9_-]{20,}")),
                 QStringLiteral("sk-***"));
    return text;
}

void AgentRunner::emitLog(const QString &text)
{
    const QString safe = redact(text);
    m_store->appendLog(m_session, safe);
    emit logLine(m_session.id, safe);
}

void AgentRunner::refreshUsage()
{
    const QString transcript = m_store ? m_store->readLog(m_session) : QString();
    m_session.completionTokens = estimateTokens(transcript);
    m_session.totalTokens = m_session.promptTokens + m_session.completionTokens;
    m_session.contextTokens = m_session.totalTokens;
    m_session.estimatedCredits = (m_session.totalTokens + 999) / 1000;
}

int AgentRunner::estimateTokens(const QString &text)
{
    if (text.isEmpty())
        return 0;
    return qMax(1, text.size() / 4);
}
