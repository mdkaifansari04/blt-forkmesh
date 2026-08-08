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

    QString cmd = QStringLiteral(
        "exec claude --output-format stream-json --input-format stream-json "
        "--verbose --include-partial-messages");
    if (m_skipPermissions)
        cmd += QStringLiteral(" --dangerously-skip-permissions");
    if (!m_model.trimmed().isEmpty())
        cmd += QStringLiteral(" --model '%1'").arg(m_model.trimmed());
    if (!m_effort.trimmed().isEmpty())
        cmd += QStringLiteral(" --effort '%1'").arg(m_effort.trimmed());
    if (!m_fallbackModels.trimmed().isEmpty())
        cmd += QStringLiteral(" --fallback-model '%1'").arg(m_fallbackModels.trimmed());
    if (!m_resumeSessionId.isEmpty())
        cmd += QStringLiteral(" --resume '%1'").arg(m_resumeSessionId);
    cmd = AgentJail::wrapCommand(cmd, m_memoryLimitMb);
    const forkmesh::vm::LaunchCommand launchCmd =
        forkmesh::vm::isolateCommand(
            QStringLiteral("bash"), {QStringLiteral("-lc"), cmd}, cwd);
    if (!launchCmd.error.isEmpty()) {
        QProcess *failed = m_proc;
        m_proc = nullptr;
        failed->deleteLater();
        emit stderrText(QStringLiteral("KVM launch failed: %1").arg(launchCmd.error));
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
    proc->setParent(nullptr);
    if (proc->state() == QProcess::NotRunning) {
        proc->deleteLater();
        return;
    }
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
