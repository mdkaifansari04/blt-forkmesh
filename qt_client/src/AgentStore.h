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
}

struct AgentSession {
    int id = 0;
    QString owner;
    QString name;
    int issueNumber = 0;
    QString issueTitle;



    QString prompt;
    QString provider;




    QString model;







    QString mode;
    bool createPr = false;



    bool yolo = false;





    bool genie = false;



    QString strength;








    bool orgTask = false;
    QString orgTaskId;
    QString startedByBot;
    QString finishedByBot;
    int prNumber = 0;
    QString status = AgentStatus::Queued;
    QString branchName;
    QString baseRef;
    QString baseBranch;


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


    void appendEvent(const AgentSession &session, const QJsonObject &ev) const;
    QList<QJsonObject> loadEvents(const AgentSession &session) const;
    void clearEvents(const AgentSession &session) const;
    void writePatch(const AgentSession &session, const QString &patch) const;
    QString readPatch(const AgentSession &session) const;

    // Live transcript search: count case-insensitive occurrences of `needle`
    // across this session's persisted transcript — both the raw log and the
    // stream-json events, since a Claude Code run only writes the latter. Only
    // the last kTranscriptSearchTailBytes of each file are read, so scanning
    // every session of a repo on each keystroke stays cheap even next to a run
    // that has been talking for hours. `snippet`, when given, receives the text
    // around the first hit (for the matching row's tooltip). Event lines are
    // searched as stored, so a query is matched against JSON-escaped text.
    static constexpr qint64 kTranscriptSearchTailBytes = 512 * 1024;
    int searchTranscript(const AgentSession &session, const QString &needle,
                         QString *snippet = nullptr) const;

private:
    QString sessionsDir() const;
    QString sessionDir(const AgentSession &session) const;
    int nextId() const;

    QString m_root;
};
