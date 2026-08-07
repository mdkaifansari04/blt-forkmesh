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
    // When model is non-empty it's passed to the CLI as `--model` (an alias like
    // "opus"/"sonnet"/"haiku" or a full model id), letting the user pick which
    // Claude model runs the agent (adhoc #261); empty keeps the CLI default.
    // effort ("low"/"medium"/"high"/"xhigh"/"max") and fallbackModels (a
    // comma-separated model list) pass through as `--effort`/`--fallback-model`
    // when non-empty — the footer slash-actions menu sets them (adhoc #116).
    // memoryLimitMb > 0 jails the launch (adhoc #236): the shell caps the
    // process tree's data memory at that many MB before exec'ing claude; the
    // caller pairs it with AgentJail::envEntries in extraEnv for a private
    // scratch environment. 0 launches unjailed.
    // resumeFallbackPrompt is the turn to send when `--resume` cannot open the
    // saved conversation at all: the CLI keeps its conversations inside one
    // config root and prunes them, so a session the user comes back to days
    // later can have nothing left to resume. Rather than exiting on the spot —
    // which read as "the agent won't start" and, because the session's history
    // still named a conversation id, had the queue relaunch the same doomed
    // `--resume` forever — the run is retried once as a fresh conversation with
    // this prompt. Empty keeps the initial prompt. Mirrors the Codex
    // app-server's thread/resume fallback.
    void start(const QString &cwd, const QStringList &extraEnv,
               const QString &initialPrompt, bool skipPermissions = true,
               const QString &resumeSessionId = QString(),
               const QString &model = QString(),
               const QString &effort = QString(),
               const QString &fallbackModels = QString(),
               int memoryLimitMb = 0,
               const QString &resumeFallbackPrompt = QString());
    // Send another user turn to a running session (steering). Returns false when
    // the turn could not be handed to the CLI — the process is gone, or its stdin
    // is closed even though the state machine has not caught up yet. The caller
    // treats that as "this transport is dead" and restarts the session rather than
    // leaving the user's message written into a broken pipe (adhoc #1618).
    bool sendUserText(const QString &text);
    // Answer a pending tool call (e.g. the AskUserQuestion clarifying-question
    // tool) by writing a `tool_result` block for that tool_use id. When the
    // agent's turn ended with a tool_use the conversation is structurally
    // required to continue with a matching tool_result — a plain user turn would
    // be rejected — so multiple-choice answers go back through this path.
    void sendToolResult(const QString &toolUseId, const QString &content);
    void stop();
    bool running() const;
    // Running *and* still able to take a turn. A process whose stdin has gone
    // (it is exiting, the pipe broke) is running by the state machine and useless
    // to steer, so the restart gate asks this rather than running() alone
    // (adhoc #1618).
    bool acceptsInput() const;
    // PID of the running CLI (0 when not running). Everything the agent shells
    // out to — builds included — lands under this process, so the UI can count
    // its descendants (adhoc #57).
    qint64 processId() const;

signals:
    void started();
    void finished(int exitCode);
    void event(const QJsonObject &ev); // one parsed stream-json event
    void rawLine(const QString &line); // raw stdout line (for the debug view)
    void stderrText(const QString &text);

private:
    // Spawn the CLI from the launch parameters below. start() records them and
    // calls this; the resume fallback calls it again with the resume id dropped.
    void launch();
    void onStdout();
    void onStderr();
    bool writeUserTurn(const QString &text);
    bool writeLine(const QJsonObject &msg);
    // Report an exit exactly once. QProcess emits finished() for a process that
    // ran, and errorOccurred(FailedToStart) for one that never did — a launch
    // that cannot start at all (its working directory is gone, no shell on PATH)
    // emits only the latter, so without this the session had no ending: the
    // caller kept it Running with nothing behind it, and every "add" relaunched
    // into the same silence (adhoc #1618).
    void reportExit(int exitCode);

    QProcess *m_proc = nullptr;
    QByteArray m_buf; // accumulates partial stdout lines
    // The last start()'s arguments, kept so the resume fallback can relaunch
    // without them having to be threaded back in from the caller.
    QString m_cwd;
    QStringList m_extraEnv;
    QString m_initialPrompt;
    bool m_skipPermissions = true;
    QString m_resumeSessionId;
    QString m_model;
    QString m_effort;
    QString m_fallbackModels;
    int m_memoryLimitMb = 0;
    QString m_resumeFallbackPrompt;
    // Whether this launch got as far as the CLI's `system`/init line, which is
    // what proves the conversation actually opened.
    bool m_conversationOpened = false;
    bool m_resumeFallbackUsed = false; // one retry per start(), never a loop
    bool m_exitReported = false;       // one finished() per launch, never two
};
