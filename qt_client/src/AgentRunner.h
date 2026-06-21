#pragma once

#include "AgentStore.h"
#include "IssueStore.h"

#include <QObject>
#include <QString>

class QProcess;

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
        bool preferApiKeyAuth = false;
        int contextWindow = 32000;
        int maxOutputTokens = 2000;
    };

    explicit AgentRunner(AgentStore *store, QObject *parent = nullptr);

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

private:
    enum class Phase { Idle, Worktree, Agent };

    void launch(Phase phase, const QString &program, const QStringList &args,
                const QString &workingDir = QString());
    void onProcessFinished(int exitCode);
    void runAgentProcess();
    void complete(bool ok, const QString &status, const QString &message);
    void cleanupWorktree();
    QString buildPrompt() const;
    QString expandCommand(const QString &promptPath) const;
    QString redact(QString text) const;
    void emitLog(const QString &text);
    void refreshUsage();
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
};
