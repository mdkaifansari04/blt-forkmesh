#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

// Drives the `claude` CLI in stream-json mode (the same transport the VS Code
// extension uses: `--output-format stream-json --input-format stream-json`) so
// the app can render a rich native transcript instead of the raw TUI. Each line
// claude writes is one JSON event; we parse and re-emit it. The process stays
// alive between turns so the user can steer it with more prompts.
class ClaudeStreamSession : public QObject
{
    Q_OBJECT
public:
    explicit ClaudeStreamSession(QObject *parent = nullptr);
    ~ClaudeStreamSession() override;

    // Launch claude in `cwd`. extraEnv holds "KEY=VALUE" entries; "KEY" with no
    // '=' unsets that variable in the child (matches TerminalWidget semantics).
    // The initial prompt is sent as the first user turn. When resumeSessionId is
    // non-empty the CLI is launched with `--resume <id>`, so it picks up that
    // prior conversation with full context instead of starting fresh — the
    // initial prompt then becomes the next steering turn (adhoc #182).
    void start(const QString &cwd, const QStringList &extraEnv,
               const QString &initialPrompt, bool skipPermissions = true,
               const QString &resumeSessionId = QString());
    // Send another user turn to a running session (steering).
    void sendUserText(const QString &text);
    void stop();
    bool running() const;

signals:
    void started();
    void finished(int exitCode);
    void event(const QJsonObject &ev); // one parsed stream-json event
    void rawLine(const QString &line); // raw stdout line (for the debug view)
    void stderrText(const QString &text);

private:
    void onStdout();
    void onStderr();
    void writeUserTurn(const QString &text);

    QProcess *m_proc = nullptr;
    QByteArray m_buf; // accumulates partial stdout lines
};
