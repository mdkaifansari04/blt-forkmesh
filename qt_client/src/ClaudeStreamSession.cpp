#include "ClaudeStreamSession.h"

#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>

ClaudeStreamSession::ClaudeStreamSession(QObject *parent) : QObject(parent) {}

ClaudeStreamSession::~ClaudeStreamSession() { stop(); }

void ClaudeStreamSession::start(const QString &cwd, const QStringList &extraEnv,
                                const QString &initialPrompt, bool skipPermissions)
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

    // Run through a login shell so the user's PATH (e.g. ~/.local/bin) resolves
    // `claude`, exactly like the embedded terminal does. `exec` hands stdin/out
    // straight to claude.
    QString cmd = QStringLiteral(
        "exec claude --output-format stream-json --input-format stream-json "
        "--verbose --include-partial-messages");
    if (skipPermissions)
        cmd += QStringLiteral(" --dangerously-skip-permissions");
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
    if (!running())
        return;
    const QJsonObject msg{
        {QStringLiteral("type"), QStringLiteral("user")},
        {QStringLiteral("message"),
         QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                     {QStringLiteral("content"), text}}}};
    m_proc->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
}

void ClaudeStreamSession::stop()
{
    if (!m_proc)
        return;
    m_proc->disconnect(this);
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->closeWriteChannel(); // signal end-of-input first
        m_proc->terminate();
        if (!m_proc->waitForFinished(1500))
            m_proc->kill();
    }
    m_proc->deleteLater();
    m_proc = nullptr;
}

bool ClaudeStreamSession::running() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
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
