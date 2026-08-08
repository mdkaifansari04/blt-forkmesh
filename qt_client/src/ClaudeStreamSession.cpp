#include "ClaudeStreamSession.h"

#include "AgentJail.h"
#include "VirtualMachineRuntime.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

ClaudeStreamSession::ClaudeStreamSession(QObject *parent) : QObject(parent) {}

ClaudeStreamSession::~ClaudeStreamSession() { stop(); }

void ClaudeStreamSession::start(const QString &cwd, const QStringList &extraEnv,
                                const QString &initialPrompt, bool skipPermissions,
                                const QString &resumeSessionId, const QString &model,
                                const QString &effort, const QString &fallbackModels,
                                int memoryLimitMb,
                                const QString &resumeFallbackPrompt)
{
    m_cwd = cwd;
    m_extraEnv = extraEnv;
    m_initialPrompt = initialPrompt;
    m_skipPermissions = skipPermissions;
    m_resumeSessionId = resumeSessionId;
    m_model = model;
    m_effort = effort;
    m_fallbackModels = fallbackModels;
    m_memoryLimitMb = memoryLimitMb;
    m_resumeFallbackPrompt = resumeFallbackPrompt.trimmed();
    m_resumeFallbackUsed = false;
    launch();
}

void ClaudeStreamSession::launch()
{
    stop();
    m_buf.clear();
    m_conversationOpened = false;
    m_exitReported = false;

    const QString cwd = m_cwd;
    const QString initialPrompt = m_initialPrompt;

    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(cwd);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &kv : m_extraEnv) {
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq > 0)
            env.insert(kv.left(eq), kv.mid(eq + 1));
        else if (eq < 0 && !kv.isEmpty())
            env.remove(kv);
    }
    forkmesh::vm::applyGuestEnvironmentPolicy(env);
    m_proc->setProcessEnvironment(env);

    connect(m_proc, &QProcess::readyReadStandardOutput, this,
            &ClaudeStreamSession::onStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this,
            &ClaudeStreamSession::onStderr);
    connect(m_proc, &QProcess::started, this, &ClaudeStreamSession::started);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
                // Exiting before the `system`/init line means the CLI never got
                // a conversation going. With `--resume` in play that is almost
                // always the saved conversation being gone (pruned, or written
                // under a config root this login no longer uses), so try once
                // more as a fresh one rather than reporting a dead session the
                // caller would only relaunch the same way.
                if (!m_conversationOpened && !m_resumeSessionId.isEmpty() &&
                    !m_resumeFallbackUsed) {
                    m_resumeFallbackUsed = true;
                    m_resumeSessionId.clear();
                    if (!m_resumeFallbackPrompt.isEmpty())
                        m_initialPrompt = m_resumeFallbackPrompt;
                    emit event(QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("_local_notice")},
                        {QStringLiteral("level"), QStringLiteral("warning")},
                        {QStringLiteral("text"),
                         QStringLiteral(
                             "The saved Claude Code conversation could not be "
                             "resumed, so ForkMesh started a new one with the "
                             "saved task and latest instruction. The branch and "
                             "its work are untouched.")}});
                    launch();
                    return;
                }
                reportExit(code);
            });
    // A process that never starts emits errorOccurred(FailedToStart) and no
    // finished() at all, so this is the only ending the caller will ever see for
    // a launch whose working directory has been removed or whose shell is
    // missing. Without it the session stayed Running behind a dead transport:
    // every following "add" restarted it, the restart failed the same silent way,
    // and the prompts piled up in the transcript with nothing answering them
    // (adhoc #1618). A start failure is not the resume fallback's problem — the
    // conversation was never reached — so report it straight through.
    connect(m_proc, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                emit stderrText(
                    QStringLiteral(
                        "Claude Code could not be started in %1 (the working "
                        "directory may be gone, or `claude` is not on PATH).\n")
                        .arg(m_cwd));
                reportExit(-1);
            });

    // Run through a login shell so the user's PATH (e.g. ~/.local/bin) resolves
    // `claude`, exactly like the embedded terminal does. `exec` hands stdin/out
    // straight to claude.
    QString cmd = QStringLiteral(
        "exec claude --output-format stream-json --input-format stream-json "
        "--verbose --include-partial-messages");
    if (m_skipPermissions)
        cmd += QStringLiteral(" --dangerously-skip-permissions");
    // Run as the model the user picked in the quick-add bar (adhoc #261). The
    // value is a CLI alias ("opus"/"sonnet"/…) or a full model id; single-quote
    // it defensively like the resume id below.
    if (!m_model.trimmed().isEmpty())
        cmd += QStringLiteral(" --model '%1'").arg(m_model.trimmed());
    // Effort level and fallback models from the footer slash-actions menu
    // (adhoc #116). Values are fixed CLI keywords / model aliases, but
    // single-quote them defensively like the model above.
    if (!m_effort.trimmed().isEmpty())
        cmd += QStringLiteral(" --effort '%1'").arg(m_effort.trimmed());
    if (!m_fallbackModels.trimmed().isEmpty())
        cmd += QStringLiteral(" --fallback-model '%1'").arg(m_fallbackModels.trimmed());
    // Resume a prior conversation so the agent picks up its full context (the
    // files it touched, what it had figured out, what's left). The id is a UUID
    // from the CLI's own stream, but single-quote it defensively all the same.
    if (!m_resumeSessionId.isEmpty())
        cmd += QStringLiteral(" --resume '%1'").arg(m_resumeSessionId);
    // Jail (adhoc #236): cap the agent's memory before handing the shell to
    // claude. The rlimit survives the exec and is inherited by subprocesses.
    cmd = AgentJail::wrapCommand(cmd, m_memoryLimitMb);
    const forkmesh::vm::LaunchCommand launchCmd =
        forkmesh::vm::isolateCommand(
            QStringLiteral("bash"), {QStringLiteral("-lc"), cmd}, cwd);
    if (!launchCmd.error.isEmpty()) {
        QProcess *failed = m_proc;
        m_proc = nullptr;
        failed->deleteLater();
        emit stderrText(QStringLiteral("KVM launch failed: %1").arg(launchCmd.error));
        // Reported straight to the caller, never through the resume fallback:
        // nothing was wrong with the conversation, the host could not spawn a
        // process at all, and a second attempt would fail the same way.
        QTimer::singleShot(0, this, [this] { reportExit(-1); });
        return;
    }
    m_proc->start(launchCmd.program, launchCmd.arguments);

    if (!initialPrompt.isEmpty())
        writeUserTurn(initialPrompt);
}

bool ClaudeStreamSession::sendUserText(const QString &text)
{
    if (text.trimmed().isEmpty())
        return true; // nothing to deliver, so nothing failed to arrive
    return writeUserTurn(text);
}

bool ClaudeStreamSession::writeUserTurn(const QString &text)
{
    return writeLine(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("user")},
        {QStringLiteral("message"),
         QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                     {QStringLiteral("content"), text}}}});
}

void ClaudeStreamSession::sendToolResult(const QString &toolUseId,
                                         const QString &content)
{
    if (toolUseId.isEmpty())
        return;
    // A tool_result is delivered as a user turn whose content is a single
    // tool_result block referencing the pending tool_use id (Messages API
    // shape). This is what lets the CLI resume a turn that stopped on a tool
    // call awaiting user input, e.g. AskUserQuestion.
    const QJsonObject block{
        {QStringLiteral("type"), QStringLiteral("tool_result")},
        {QStringLiteral("tool_use_id"), toolUseId},
        {QStringLiteral("content"), content}};
    writeLine(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("user")},
        {QStringLiteral("message"),
         QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                     {QStringLiteral("content"), QJsonArray{block}}}}});
}

bool ClaudeStreamSession::writeLine(const QJsonObject &msg)
{
    if (!running())
        return false;
    // A process can read as running while its stdin is already closed (it is
    // exiting, or the pipe broke). write() reports that, and the caller needs to
    // know: a turn that goes nowhere used to be indistinguishable from one the
    // agent simply had not answered yet (adhoc #1618).
    if (!m_proc->isWritable())
        return false;
    return m_proc->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n') >= 0;
}

void ClaudeStreamSession::reportExit(int exitCode)
{
    if (m_exitReported)
        return;
    m_exitReported = true;
    emit finished(exitCode);
}

void ClaudeStreamSession::stop()
{
    if (!m_proc)
        return;
    QProcess *proc = m_proc;
    m_proc = nullptr;
    proc->disconnect(this);
    // Detach the process from this session so it survives our own destruction and
    // tears itself down on its own time.
    proc->setParent(nullptr);
    if (proc->state() == QProcess::NotRunning) {
        proc->deleteLater();
        return;
    }
    // Don't block the UI thread waiting for Claude to exit. This used to call
    // waitForFinished(1500), freezing the window for up to 1.5s every time a
    // running session was stopped or deleted. Instead ask it to terminate and let
    // it clean itself up: kill it if it's still alive after a grace period, and
    // delete the QProcess once it has actually exited.
    connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    proc->closeWriteChannel(); // signal end-of-input first
    proc->terminate();
    QTimer::singleShot(1500, proc, [proc] {
        if (proc->state() != QProcess::NotRunning)
            proc->kill(); // finished() → deleteLater() then frees it
    });
}

bool ClaudeStreamSession::running() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

bool ClaudeStreamSession::acceptsInput() const
{
    return running() && m_proc->isWritable();
}

qint64 ClaudeStreamSession::processId() const
{
    return running() ? m_proc->processId() : 0;
}

void ClaudeStreamSession::onStdout()
{
    if (!m_proc)
        return;
    m_buf += m_proc->readAllStandardOutput();
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        if (line.trimmed().isEmpty())
            continue;
        emit rawLine(QString::fromUtf8(line));
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject()) {
            const QJsonObject ev = doc.object();
            // The CLI opens every run — fresh or resumed — with a system/init
            // line. Seeing one is what tells the finished handler this launch
            // had a real conversation, so an exit after it is a genuine end and
            // not a `--resume` that had nothing to open.
            if (ev.value(QStringLiteral("type")).toString() == QLatin1String("system"))
                m_conversationOpened = true;
            emit event(ev);
        }
    }
}

void ClaudeStreamSession::onStderr()
{
    if (m_proc)
        emit stderrText(QString::fromUtf8(m_proc->readAllStandardError()));
}
