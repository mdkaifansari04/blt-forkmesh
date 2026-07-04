#include "AgentRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

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

bool runWithInput(const QString &dir, const QStringList &args,
                  const QByteArray &input, QString *error = nullptr)
{
    QProcess process;
    process.start(QStringLiteral("git"), QStringList{QStringLiteral("-C"), dir} + args);
    if (!process.waitForStarted(10000)) {
        if (error)
            *error = process.errorString();
        return false;
    }
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error)
            *error = QStringLiteral("git timed out");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}

bool branchExists(const QString &dir, const QString &branchName)
{
    if (branchName.trimmed().isEmpty())
        return false;
    QByteArray ignored;
    return runCapture(dir,
                      {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                       QStringLiteral("--quiet"),
                       QStringLiteral("refs/heads/%1").arg(branchName)},
                      &ignored, nullptr);
}

// Returns the filesystem path of the worktree currently checked out to `branch`
// (other than the main checkout), or an empty string if none. Used to find a
// worktree a previous, interrupted run left behind.
QString worktreePathForBranch(const QString &repoPath, const QString &branch)
{
    if (branch.trimmed().isEmpty())
        return QString();
    QByteArray out;
    if (!runCapture(repoPath,
                    {QStringLiteral("worktree"), QStringLiteral("list"),
                     QStringLiteral("--porcelain")},
                    &out, nullptr))
        return QString();
    const QString want = QStringLiteral("refs/heads/%1").arg(branch);
    const int pathPrefix = QStringLiteral("worktree ").size();
    const int branchPrefix = QStringLiteral("branch ").size();
    QString currentPath;
    const QList<QByteArray> lines = out.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1String("worktree ")))
            currentPath = line.mid(pathPrefix).trimmed();
        else if (line.startsWith(QLatin1String("branch "))) {
            if (line.mid(branchPrefix).trimmed() == want &&
                !currentPath.isEmpty() && currentPath != repoPath)
                return currentPath;
        }
    }
    return QString();
}

// Drop any worktree a previous run left checked out to `branch` and prune stale
// registrations, so a fresh `git worktree add` for this branch can succeed. A
// run interrupted by an app restart never reaches cleanupWorktree(), so without
// this the branch stays "already checked out" at the old (often deleted) path
// and the resumed run would fail to create its worktree (issue #242).
void releaseBranchWorktree(const QString &repoPath, const QString &branch)
{
    const QString leaked = worktreePathForBranch(repoPath, branch);
    if (!leaked.isEmpty()) {
        QProcess::execute(QStringLiteral("git"),
                          {QStringLiteral("-C"), repoPath, QStringLiteral("worktree"),
                           QStringLiteral("remove"), QStringLiteral("--force"), leaked});
        QDir(leaked).removeRecursively();
    }
    QProcess::execute(QStringLiteral("git"),
                      {QStringLiteral("-C"), repoPath, QStringLiteral("worktree"),
                       QStringLiteral("prune")});
}

QString providerTitle(const QString &provider)
{
    // "claude-code" runs the real CLI; other "claude*" sessions are the Claude
    // API script; everything else (incl. legacy "codex") is OpenAI API.
    if (provider == QLatin1String("claude-code"))
        return QStringLiteral("Claude Code");
    if (provider.startsWith(QLatin1String("claude")))
        return QStringLiteral("Claude API");
    return QStringLiteral("OpenAI API");
}

// Approximate USD price per 1,000,000 tokens, used only to surface a rough
// "how much did this task cost" figure. Claude numbers are the published
// Anthropic per-million-token rates; the OpenAI fallback is a single rough
// estimate since those models aren't priced here.
struct TokenPrice {
    double inputPerMillion;
    double outputPerMillion;
};

TokenPrice priceFor(const QString &provider, const QString &model)
{
    const QString m = model.toLower();
    if (provider.startsWith(QLatin1String("claude"))) {
        if (m.contains(QLatin1String("haiku")))
            return {1.0, 5.0};
        if (m.contains(QLatin1String("sonnet")))
            return {3.0, 15.0};
        // Opus (the default Claude model) and anything unrecognised.
        return {5.0, 25.0};
    }
    // OpenAI and any other provider.
    return {1.25, 10.0};
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
    m_attentionRaised = false;
    m_processStartedAtMs = 0;
    m_lastOutputAtMs = 0;
    m_currentProgram.clear();
    m_phase = Phase::Idle;
    m_session = session;
    m_restorePreviousPatch = m_session.startedAtMs > 0 || !m_session.baseRef.isEmpty();
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
    // Record the spend at the start of this run so the run's incremental cost
    // can be reported as a diff when it finishes.
    m_session.spendBeforeUsd = m_session.costUsd;
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
    // Accept either a working-tree checkout (has .git) or a bare mirror (has a
    // HEAD at its root): a node that only mirrors a repo runs agents straight off
    // the mirror, building the branch in a throwaway worktree (adhoc #191).
    if (!QDir(m_repoPath).exists(QStringLiteral(".git")) &&
        !QDir(m_repoPath).exists(QStringLiteral("HEAD"))) {
        complete(false, AgentStatus::Failed,
                 QStringLiteral("No git checkout is available for this repo."));
        return;
    }

    if (m_session.branchName.isEmpty()) {
        m_session.branchName =
            QStringLiteral("agent/issue-%1-%2-%3")
                .arg(m_session.issueNumber)
                .arg(m_session.provider)
                .arg(m_session.id);
    }

    const bool existingSessionBranch = branchExists(m_repoPath, m_session.branchName);
    if (m_session.baseRef.isEmpty()) {
        QByteArray base;
        QString gitError;
        if (!runCapture(m_repoPath, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
                        &base, &gitError)) {
            complete(false, AgentStatus::Failed,
                     QStringLiteral("Could not read the base commit: %1").arg(gitError));
            return;
        }
        m_session.baseRef = QString::fromUtf8(base).trimmed();
        // The PR targets the branch the agent forked from (typically main), not the
        // frozen commit SHA. Fall back to the SHA only when HEAD is detached.
        QByteArray branch;
        if (runCapture(m_repoPath,
                       {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                        QStringLiteral("HEAD")},
                       &branch, nullptr)) {
            const QString name = QString::fromUtf8(branch).trimmed();
            if (!name.isEmpty() && name != QLatin1String("HEAD"))
                m_session.baseBranch = name;
        }
        if (m_session.baseBranch.isEmpty())
            m_session.baseBranch = m_session.baseRef;
    } else if (m_session.baseBranch.isEmpty()) {
        m_session.baseBranch = m_session.baseRef;
    }
    m_store->saveSession(m_session);

    // A run interrupted by an app restart can leave a worktree behind that still
    // holds this session's branch (and in-flight edits it never saved as a
    // patch). Recover those edits into the session patch — but only when nothing
    // is saved yet, so a real prior patch is never clobbered — then release the
    // stale worktree so this run can re-create one cleanly (issue #242).
    if (existingSessionBranch && !m_session.baseRef.isEmpty()) {
        const QString leaked = worktreePathForBranch(m_repoPath, m_session.branchName);
        if (!leaked.isEmpty() && QDir(leaked).exists() &&
            m_store->readPatch(m_session).trimmed().isEmpty()) {
            QByteArray wip;
            if (runCapture(leaked,
                           {QStringLiteral("diff"), QStringLiteral("--binary"),
                            m_session.baseRef},
                           &wip, nullptr) &&
                !QString::fromUtf8(wip).trimmed().isEmpty()) {
                m_store->writePatch(m_session, QString::fromUtf8(wip));
                emitLog(QStringLiteral(
                    "==> Recovered in-flight changes from the interrupted run."));
            }
        }
    }
    releaseBranchWorktree(m_repoPath, m_session.branchName);

    m_worktree = QDir::tempPath() + QStringLiteral("/forkmesh-agent-") +
                 QString::number(m_session.id) + QLatin1Char('-') +
                 QString::number(QDateTime::currentMSecsSinceEpoch());
    emitLog(QStringLiteral("==> Creating temporary worktree %1").arg(m_worktree));
    QStringList args{QStringLiteral("-C"), m_repoPath, QStringLiteral("worktree"),
                     QStringLiteral("add")};
    if (existingSessionBranch) {
        args << m_worktree << m_session.branchName;
    } else {
        args << QStringLiteral("-B") << m_session.branchName << m_worktree
             << m_session.baseRef;
    }
    launch(Phase::Worktree, QStringLiteral("git"), args);
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
    m_processStartedAtMs = 0;
    m_lastOutputAtMs = 0;
    m_currentProgram = program;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    if (!workingDir.isEmpty())
        m_process->setWorkingDirectory(workingDir);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (m_config.preferApiKeyAuth && !m_config.isolatedHome.isEmpty()) {
        QDir().mkpath(m_config.isolatedHome);
        env.insert(QStringLiteral("CODEX_HOME"), m_config.isolatedHome);
    }
    // Both remaining providers (OpenAI API and Claude API) authenticate with an
    // API key supplied in Settings. We inherit the user's shell environment, which
    // may already carry an ANTHROPIC_API_KEY/OPENAI_API_KEY that's stale or belongs
    // to a different account. Unset those first so the agent never silently uses an
    // inherited key, then reset only the one ForkMesh configured.
    const QString provider = m_session.provider;
    env.remove(QStringLiteral("ANTHROPIC_API_KEY"));
    env.remove(QStringLiteral("OPENAI_API_KEY"));
    if (!m_config.apiKeyName.isEmpty())
        env.remove(m_config.apiKeyName);
    const bool usingConfiguredKey = !m_config.apiKeyName.isEmpty() &&
                                    !m_config.apiKey.trimmed().isEmpty();
    if (usingConfiguredKey) {
        const QString key = m_config.apiKey.trimmed();
        env.insert(m_config.apiKeyName, key);
        if (provider == QLatin1String("openai"))
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
        m_lastOutputAtMs = QDateTime::currentMSecsSinceEpoch();
        const QString chunk = QString::fromUtf8(m_process->readAllStandardOutput());
        emitLog(chunk);
        const QString hint = detectAuthIssue(chunk);
        if (!hint.isEmpty()) {
            emitLog(QStringLiteral("==> ") + hint);
            emit needsAttention(m_session.id, hint);
        }
    });
    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus) {
                if (m_noOutputTimer)
                    m_noOutputTimer->stop();
                onProcessFinished(exitCode);
            });
    connect(m_process, &QProcess::started, this, [this] {
        m_processStartedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_lastOutputAtMs = m_processStartedAtMs;
        emitLog(QStringLiteral("==> Started process pid %1: %2")
                    .arg(m_process ? m_process->processId() : 0)
                    .arg(m_currentProgram));
    });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (m_noOutputTimer)
                    m_noOutputTimer->stop();
                if (!m_process)
                    return;
                emitLog(QStringLiteral("!! ") + m_process->errorString());
                if (error == QProcess::FailedToStart)
                    complete(false, AgentStatus::Failed, m_process->errorString());
            });

    m_process->setProgram(program);
    m_process->setArguments(args);
    m_process->start();
    if (phase == Phase::Agent) {
        if (!m_noOutputTimer) {
            m_noOutputTimer = new QTimer(this);
            m_noOutputTimer->setInterval(15000);
            connect(m_noOutputTimer, &QTimer::timeout, this,
                    &AgentRunner::onNoOutputTimeout);
        }
        emitLog(QStringLiteral("==> Waiting for agent output..."));
        m_noOutputTimer->start();
    }
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
        if (!restorePreviousPatch())
            return;
        runAgentProcess();
        return;
    }

    if (exitCode != 0) {
        QString message =
            QStringLiteral("Agent command exited with code %1.").arg(exitCode);
        const QString log = m_store ? m_store->readLog(m_session) : QString();
        if (!m_session.provider.startsWith(QLatin1String("claude")) &&
            log.contains(QStringLiteral("401 Unauthorized"), Qt::CaseInsensitive)) {
            message += QStringLiteral(
                " OpenAI rejected the API key; rotate it and re-enter it in Settings.");
        }
        complete(false, AgentStatus::Failed, message);
        return;
    }

    // Attribute the commits this run produced to the agent before capturing the
    // patch, so the provenance trailer is carried by the signed commit series.
    stampAgentProvenance();

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

bool AgentRunner::restorePreviousPatch()
{
    if (!m_store || !m_restorePreviousPatch)
        return true;

    const QString patch = m_store->readPatch(m_session);
    if (patch.trimmed().isEmpty())
        return true;

    QByteArray currentDiff;
    if (runCapture(m_worktree,
                   {QStringLiteral("diff"), QStringLiteral("--binary"),
                    m_session.baseRef},
                   &currentDiff, nullptr) &&
        !currentDiff.trimmed().isEmpty()) {
        emitLog(QStringLiteral("==> Existing session branch already has changes; skipping saved patch restore."));
        return true;
    }

    const QByteArray patchBytes = patch.toUtf8();
    QString error;
    if (!runWithInput(m_worktree,
                      {QStringLiteral("apply"), QStringLiteral("--check"),
                       QStringLiteral("--binary")},
                      patchBytes, &error)) {
        complete(false, AgentStatus::Failed,
                 QStringLiteral("Could not restore the previous session patch: %1")
                     .arg(error));
        return false;
    }
    if (!runWithInput(m_worktree,
                      {QStringLiteral("apply"), QStringLiteral("--binary")},
                      patchBytes, &error)) {
        complete(false, AgentStatus::Failed,
                 QStringLiteral("Could not restore the previous session patch: %1")
                     .arg(error));
        return false;
    }
    emitLog(QStringLiteral("==> Restored previous session patch."));
    return true;
}

void AgentRunner::complete(bool ok, const QString &status, const QString &message)
{
    if (!m_busy && m_phase == Phase::Idle)
        return;
    if (m_process) {
        if (m_noOutputTimer)
            m_noOutputTimer->stop();
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    emitLog((ok ? QStringLiteral("==> SUCCESS: ") : QStringLiteral("==> ")) + message);
    refreshUsage();
    // Snapshot the spend after this run and log the difference so the cost of
    // this particular run is visible in the agent log.
    m_session.spendAfterUsd = m_session.costUsd;
    const double runCost = m_session.spendAfterUsd - m_session.spendBeforeUsd;
    emitLog(QStringLiteral(
                "==> Task cost: $%1 (spend before $%2 -> after $%3, this run +$%4)")
                .arg(m_session.costUsd, 0, 'f', 4)
                .arg(m_session.spendBeforeUsd, 0, 'f', 4)
                .arg(m_session.spendAfterUsd, 0, 'f', 4)
                .arg(runCost, 0, 'f', 4));
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

QString AgentRunner::agentProvenanceValue() const
{
    QString tool = m_session.provider.trimmed();
    if (tool.isEmpty())
        tool = QStringLiteral("agent");
    const QString model = m_config.model.trimmed();
    QString value = model.isEmpty() ? tool : tool + QLatin1Char('/') + model;
    // Keep the trailer value shell-safe (it is embedded in a single-quoted
    // `git commit --trailer` command below) and free of newlines.
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._+/ -]")),
                  QString());
    return value;
}

void AgentRunner::stampAgentProvenance()
{
    if (m_worktree.isEmpty() || m_session.baseRef.isEmpty())
        return;
    // Only stamp when the run actually committed something. Uncommitted changes
    // would make `git rebase` refuse, so skip cleanly in that case too — the diff
    // is still captured from the working tree.
    QByteArray dirty;
    if (runCapture(m_worktree, {QStringLiteral("status"), QStringLiteral("--porcelain")},
                   &dirty, nullptr) &&
        !QString::fromUtf8(dirty).trimmed().isEmpty()) {
        emitLog(QStringLiteral(
            "==> Skipping agent provenance trailer: worktree has uncommitted changes."));
        return;
    }
    QByteArray count;
    if (!runCapture(m_worktree,
                    {QStringLiteral("rev-list"), QStringLiteral("--count"),
                     m_session.baseRef + QStringLiteral("..HEAD")},
                    &count, nullptr) ||
        QString::fromUtf8(count).trimmed().toInt() <= 0)
        return;

    const QString value = agentProvenanceValue();
    // Rewrite each new commit's message to append the trailer. `--trailer`'s
    // default addIfDifferentNeighbor policy makes a re-run (resumed session)
    // idempotent, so already-stamped commits are left untouched.
    const QString exec =
        QStringLiteral("git commit --amend --no-edit --trailer 'ForkMesh-Agent: %1'")
            .arg(value);
    QProcess process;
    process.start(QStringLiteral("git"),
                  {QStringLiteral("-C"), m_worktree, QStringLiteral("rebase"),
                   m_session.baseRef, QStringLiteral("--exec"), exec});
    if (!process.waitForFinished(60000) || process.exitCode() != 0) {
        // Leave history untouched rather than a half-finished rebase.
        QProcess::execute(QStringLiteral("git"),
                          {QStringLiteral("-C"), m_worktree,
                           QStringLiteral("rebase"), QStringLiteral("--abort")});
        emitLog(QStringLiteral("==> Could not stamp agent provenance trailer (%1).")
                    .arg(QString::fromUtf8(process.readAllStandardError()).trimmed()));
        return;
    }
    emitLog(QStringLiteral("==> Stamped ForkMesh-Agent provenance trailer: %1").arg(value));
}

QString AgentRunner::defaultPromptPreamble()
{
    return QStringLiteral(
        "You are running inside ForkMesh as a coding agent.\n"
        "Use the minimum context and output needed. Inspect only files relevant to the issue.\n"
        "Do not spend extra tokens on broad refactors or unrelated cleanup.\n"
        "When possible, make the smallest patch that satisfies the issue.\n"
        "Do not commit, push, or open network resources unless the issue explicitly requires it.");
}

QString AgentRunner::buildPrompt() const
{
    QStringList prompt;
    // Instruction preamble: the user-editable prompt saved in Settings → Agents,
    // falling back to the built-in default when left blank.
    QString preamble = m_config.promptPreamble.trimmed();
    if (preamble.isEmpty())
        preamble = defaultPromptPreamble();
    prompt << preamble;
    prompt << QStringLiteral("");
    prompt << QStringLiteral("Repository: %1/%2").arg(m_session.owner, m_session.name);
    const QString task = m_config.taskOverride.trimmed();
    if (!task.isEmpty()) {
        // Ad-hoc composer run: no issue thread, just the typed task.
        prompt << QStringLiteral("Task: %1").arg(m_session.issueTitle);
        prompt << QStringLiteral("");
        prompt << task;
    } else {
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
    command.replace(QStringLiteral("{provider}"), shellQuote(m_session.provider));
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

QString AgentRunner::detectAuthIssue(const QString &chunk)
{
    if (m_attentionRaised)
        return QString();
    const QString low = chunk.toLower();
    const bool claude = m_session.provider.startsWith(QLatin1String("claude"));
    // Strong, specific markers so normal agent output doesn't trip this.
    const auto has = [&](const char *needle) { return low.contains(QLatin1String(needle)); };
    if (claude &&
        (has("invalid api key") || has("authentication_error") ||
         has("401 unauthorized"))) {
        m_attentionRaised = true;
        return QStringLiteral(
            "Claude rejected the Anthropic API key. Set a valid ANTHROPIC_API_KEY "
            "in Settings, then click Continue.");
    }
    if (claude && has("credit balance is too low")) {
        m_attentionRaised = true;
        return QStringLiteral(
            "Claude reports the credit balance is too low. Top up the Anthropic "
            "account for this API key, then click Continue.");
    }
    if (!claude && (has("401 unauthorized") || has("invalid api key"))) {
        m_attentionRaised = true;
        return QStringLiteral(
            "OpenAI rejected the API key. Set a valid OpenAI API key in Settings, "
            "then click Continue.");
    }
    return QString();
}

void AgentRunner::emitLog(const QString &text)
{
    const QString safe = redact(text);
    m_store->appendLog(m_session, safe);
    emit logLine(m_session.id, safe);
}

void AgentRunner::onNoOutputTimeout()
{
    if (!m_process || m_phase != Phase::Agent ||
        m_process->state() != QProcess::Running)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 started = m_processStartedAtMs > 0 ? m_processStartedAtMs : now;
    const qint64 last = m_lastOutputAtMs > 0 ? m_lastOutputAtMs : started;
    const qint64 runningSecs = qMax<qint64>(0, (now - started) / 1000);
    const qint64 quietSecs = qMax<qint64>(0, (now - last) / 1000);
    emitLog(QStringLiteral(
                "==> Agent process still running: %1s elapsed, no output for %2s (pid %3, program %4).")
                .arg(runningSecs)
                .arg(quietSecs)
                .arg(m_process->processId())
                .arg(m_currentProgram));
}

void AgentRunner::refreshUsage()
{
    const QString transcript = m_store ? m_store->readLog(m_session) : QString();
    m_session.completionTokens = estimateTokens(transcript);
    m_session.totalTokens = m_session.promptTokens + m_session.completionTokens;
    m_session.contextTokens = m_session.totalTokens;
    m_session.estimatedCredits = (m_session.totalTokens + 999) / 1000;
    m_session.costUsd = estimateCostUsd();
}

double AgentRunner::estimateCostUsd() const
{
    const TokenPrice price = priceFor(m_session.provider, m_config.model);
    return (m_session.promptTokens * price.inputPerMillion +
            m_session.completionTokens * price.outputPerMillion) /
           1000000.0;
}

int AgentRunner::estimateTokens(const QString &text)
{
    if (text.isEmpty())
        return 0;
    return qMax(1, text.size() / 4);
}
