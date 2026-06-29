#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace AgentStatus {
inline const QString Queued = QStringLiteral("queued");
inline const QString Running = QStringLiteral("running");
inline const QString Waiting = QStringLiteral("waiting");
inline const QString Success = QStringLiteral("success");
inline const QString Failed = QStringLiteral("failed");
inline const QString Stopped = QStringLiteral("stopped");
inline const QString Cleared = QStringLiteral("cleared");
} // namespace AgentStatus

struct AgentSession {
    int id = 0;
    QString owner;
    QString name;
    int issueNumber = 0;
    QString issueTitle;
    // Ad-hoc sessions (issueNumber == 0, the Agents-tab composer) have no issue
    // to re-read their task from, so the free-form prompt is persisted here and
    // replayed verbatim when the session is resumed after an app restart.
    QString prompt;
    QString provider; // openai | claude-api (legacy: codex, claude-code)
    // Preferred model for this session. Empty falls back to the provider's
    // default (the `claude` CLI's own default for Claude Code). For Claude Code
    // this is a CLI alias: opus | sonnet | haiku. Applied on the next launch or
    // continuation, and shown in the agent header.
    QString model;
    bool createPr = false;
    int prNumber = 0;
    QString status = AgentStatus::Queued;
    QString branchName;
    QString baseRef;    // base commit SHA captured at run start (worktree/diff)
    QString baseBranch; // base branch the PR targets (e.g. main)
    // Set once this session's worktree/PR has landed in the base branch (issue
    // #291), so the status and detail page can flag it.
    bool merged = false;
    qint64 mergedAtMs = 0;
    qint64 createdAtMs = 0;
    qint64 startedAtMs = 0;
    qint64 finishedAtMs = 0;
    int promptTokens = 0;
    int completionTokens = 0;
    int totalTokens = 0;
    int contextTokens = 0;
    int contextWindow = 0;
    int maxOutputTokens = 0;
    int estimatedCredits = 0;
    // Estimated USD cost of the task, derived from token usage and the model's
    // price. costUsd is the running total; spendBeforeUsd / spendAfterUsd
    // snapshot the total around the latest run so the per-run diff can be logged.
    double costUsd = 0.0;
    double spendBeforeUsd = 0.0;
    double spendAfterUsd = 0.0;
    // Claude Code run summary, captured from the CLI's final `result` event
    // ("done · N turns · Ms · $X"): the number of turns and total wall-clock
    // duration the run took, persisted so the list shows it after a restart.
    int numTurns = 0;
    qint64 durationMs = 0;
    QString lastError;

    QString repoKey() const;
    QJsonObject toJson() const;
    static AgentSession fromJson(const QJsonObject &obj);
};

class AgentStore
{
public:
    explicit AgentStore(QString rootDir);

    QList<AgentSession> loadAllSessions() const;
    AgentSession createSession(AgentSession session);
    bool saveSession(const AgentSession &session) const;
    bool deleteSession(const AgentSession &session) const;
    void appendLog(const AgentSession &session, const QString &text) const;
    QString readLog(const AgentSession &session) const;
    // Claude Code stream-json transcript events, persisted one JSON object per
    // line so the rich transcript survives an app restart (issue #41).
    void appendEvent(const AgentSession &session, const QJsonObject &ev) const;
    QList<QJsonObject> loadEvents(const AgentSession &session) const;
    void clearEvents(const AgentSession &session) const;
    void writePatch(const AgentSession &session, const QString &patch) const;
    QString readPatch(const AgentSession &session) const;

private:
    QString sessionsDir() const;
    QString sessionDir(const AgentSession &session) const;
    int nextId() const;

    QString m_root;
};
