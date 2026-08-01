#include "ClaudeStreamSession.h"

#include "AgentJail.h"

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
                                int memoryLimitMb)
{
    stop();
    m_buf.clear();

    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(cwd);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &kv : extraEnv) {
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq > 0)
            env.insert(kv.left(eq), kv.mid(eq + 1));
        else if (eq < 0 && !kv.isEmpty())
            env.remove(kv);
    }
    m_proc->setProcessEnvironment(env);

    connect(m_proc, &QProcess::readyReadStandardOutput, this,
            &ClaudeStreamSession::onStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this,
            &ClaudeStreamSession::onStderr);
    connect(m_proc, &QProcess::started, this, &ClaudeStreamSession::started);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { emit finished(code); });




    QString cmd = QStringLiteral(
        "exec claude --output-format stream-json --input-format stream-json "
        "--verbose --include-partial-messages");
    if (skipPermissions)
        cmd += QStringLiteral(" --dangerously-skip-permissions");



    if (!model.trimmed().isEmpty())
        cmd += QStringLiteral(" --model '%1'").arg(model.trimmed());



    if (!effort.trimmed().isEmpty())
        cmd += QStringLiteral(" --effort '%1'").arg(effort.trimmed());
    if (!fallbackModels.trimmed().isEmpty())
        cmd += QStringLiteral(" --fallback-model '%1'").arg(fallbackModels.trimmed());



    if (!resumeSessionId.isEmpty())
        cmd += QStringLiteral(" --resume '%1'").arg(resumeSessionId);


    cmd = AgentJail::wrapCommand(cmd, memoryLimitMb);
    m_proc->start(QStringLiteral("bash"), {QStringLiteral("-lc"), cmd});

    if (!initialPrompt.isEmpty())
        writeUserTurn(initialPrompt);
}

void ClaudeStreamSession::sendUserText(const QString &text)
{
    if (!text.trimmed().isEmpty())
        writeUserTurn(text);
}

void ClaudeStreamSession::writeUserTurn(const QString &text)
{
    writeLine(QJsonObject{
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

void ClaudeStreamSession::writeLine(const QJsonObject &msg)
{
    if (!running())
        return;
    m_proc->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
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
    proc->closeWriteChannel();
    proc->terminate();
    QTimer::singleShot(1500, proc, [proc] {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
}

bool ClaudeStreamSession::running() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
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
        if (doc.isObject())
            emit event(doc.object());
    }
}

void ClaudeStreamSession::onStderr()
{
    if (m_proc)
        emit stderrText(QString::fromUtf8(m_proc->readAllStandardError()));
}
