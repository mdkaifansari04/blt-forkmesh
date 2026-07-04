#pragma once

#include "AgentStore.h"
#include "IssueStore.h"

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

class AgentRunner : public QObject
{
    Q_OBJECT
public:
    struct Config {
        QString command;
        QString apiKeyName;
        QString apiKey;
        QString isolatedHome;
        QString model;
        // Instruction preamble prepended to the issue prompt. Empty falls back to
        // defaultPromptPreamble(); editable and saved via Settings → Agents.
        QString promptPreamble;
        // Ad-hoc runs (the Agents-tab composer, issue #273) have no issue to
        // anchor to: when set, this free-form task becomes the agent's prompt in
        // place of the issue thread.
        QString taskOverride;
        bool preferApiKeyAuth = false;
        int contextWindow = 32000;
        int maxOutputTokens = 2000;
    };

    explicit AgentRunner(AgentStore *store, QObject *parent = nullptr);

    // The built-in instruction preamble used when no custom prompt is configured.
    static QString defaultPromptPreamble();

    bool busy() const { return m_busy; }
    int currentSessionId() const { return m_session.id; }

    void start(const AgentSession &session, const Issue &issue,
               const QString &repoPath, const Config &config);
    void stop();
    void steer(const QString &prompt);

signals:
    void logLine(int sessionId, const QString &text);
    void statusChanged(int sessionId, const QString &status);
    void finished(int sessionId, bool ok);
    // The agent CLI needs the user to act (e.g. Claude Code isn't logged in).
    // Carries an actionable message for the UI to surface instead of appearing
    // stuck.
    void needsAttention(int sessionId, const QString &message);

private:
    enum class Phase { Idle, Worktree, Agent };

    void launch(Phase phase, const QString &program, const QStringList &args,
                const QString &workingDir = QString());
    void onProcessFinished(int exitCode);
    void runAgentProcess();
    void complete(bool ok, const QString &status, const QString &message);
    void cleanupWorktree();
    // Stamp a `ForkMesh-Agent: <tool>/<model>` trailer onto the commits this run
    // produced so machine authorship is attributable and signed in review (issue
    // #365). The trailer travels inside the commit series and thus the pull sig.
    void stampAgentProvenance();
    // The trailer value for this session, "<provider>/<model>" (sanitised).
    QString agentProvenanceValue() const;
    bool restorePreviousPatch();
    QString buildPrompt() const;
    QString expandCommand(const QString &promptPath) const;
    QString redact(QString text) const;
    void emitLog(const QString &text);
    void onNoOutputTimeout();
    // Scan agent output for "needs sign-in / out of credit" markers; returns an
    // actionable message the first time one is seen (else empty).
    QString detectAuthIssue(const QString &chunk);
    void refreshUsage();
    double estimateCostUsd() const;
    static int estimateTokens(const QString &text);

    AgentStore *m_store;
    bool m_busy = false;
    bool m_stopping = false;
    Phase m_phase = Phase::Idle;
    AgentSession m_session;
    Issue m_issue;
    Config m_config;
    QString m_repoPath;
    QString m_worktree;
    QString m_prompt;
    QProcess *m_process = nullptr;
    QTimer *m_noOutputTimer = nullptr;
    qint64 m_processStartedAtMs = 0;
    qint64 m_lastOutputAtMs = 0;
    QString m_currentProgram;
    bool m_attentionRaised = false; // emit needsAttention only once per run
    bool m_restorePreviousPatch = false;
};
