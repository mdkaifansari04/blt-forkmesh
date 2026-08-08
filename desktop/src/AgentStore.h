#pragma once

#include <QHash>
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
    QString prompt;
    QString provider; // codex | openai | claude-api | claude-code
    QString model;
    // Permission mode this session runs under, as the label from the composer's
    // mode selector ("Ask" / "Edit" / "Plan" / "Auto"; sessions written before
    // shortened them say "Ask before edits" / "Auto mode" / …).
    // Captured when a follow-up prompt is sent so the next resume honors the live
    // selection, and shown in the agent header. Empty falls back to the global
    // kClaudeAutoModeSetting. Only "Auto" skips the CLI's permission prompts
    // today (see agentModeSkipsPermissions).
    QString mode;
    bool createPr = false;
    bool associationOnly = false;
    bool yolo = false;
    bool genie = false;
    QString strength;
    bool orgTask = false;
    QString orgTaskId;
    QString startedByBot;
    QString finishedByBot;
    // A website-created organization job remains tied to its authenticated
    // Worker lease until the terminal result is acknowledged. Persisting the
    // exact transport envelope lets a resumed local run report completion after
    // ForkMesh itself restarts without claiming or executing the prompt twice.
    QJsonObject orgAgentJob;
    int prNumber = 0;
    QString status = AgentStatus::Queued;
    QString branchName;
    QString baseRef;    // base commit SHA captured at run start (worktree/diff)
    QString baseBranch; // base branch the PR targets (e.g. main)
    QString mergeCandidateHead;
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
    double costUsd = 0.0;
    double spendBeforeUsd = 0.0;
    double spendAfterUsd = 0.0;
    int numTurns = 0;
    qint64 durationMs = 0;
    QString lastError;

    bool genieInFlight() const
    {
        return genie && !merged &&
               (status == AgentStatus::Running || status == AgentStatus::Queued ||
                status == AgentStatus::Waiting);
    }

    QString repoKey() const;
    QJsonObject toJson() const;
    static AgentSession fromJson(const QJsonObject &obj);
};

struct AgentModelOutcome {
    int runs = 0;
    int merged = 0;
};

class AgentStore
{
public:
    explicit AgentStore(QString rootDir);

    QList<AgentSession> loadAllSessions() const;
    static QList<int> queuedSessionIdsOldestFirst(
        const QList<AgentSession> &sessions);
    AgentSession createSession(AgentSession session);
    bool saveSession(const AgentSession &session) const;
    bool deleteSession(const AgentSession &session) const;
    void appendLog(const AgentSession &session, const QString &text) const;

    static QString modelOutcomeKey(const QString &provider, const QString &model);
    QHash<QString, AgentModelOutcome> retiredModelOutcomes() const;
    QString readLog(const AgentSession &session) const;
    void appendEvent(const AgentSession &session, const QJsonObject &ev) const;
    QList<QJsonObject> loadEvents(const AgentSession &session) const;
    void clearEvents(const AgentSession &session) const;
    void writePatch(const AgentSession &session, const QString &patch) const;
    QString readPatch(const AgentSession &session) const;

    static constexpr qint64 kTranscriptSearchTailBytes = 512 * 1024;
    int searchTranscript(const AgentSession &session, const QString &needle,
                         QString *snippet = nullptr) const;

    static QStringList attachmentPathsIn(const QString &text);
    QStringList attachmentPaths(const AgentSession &session) const;
    QString transcriptStamp(const AgentSession &session) const;
    static constexpr qint64 kAttachmentScanBytes = 128 * 1024;

private:
    QString sessionsDir() const;
    QString sessionDir(const AgentSession &session) const;
    QString modelOutcomesPath() const;
    void recordRetiredSession(const AgentSession &session) const;
    int nextId() const;

    QString m_root;
};
