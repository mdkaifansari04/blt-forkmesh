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
    QString provider; // codex | openai | claude-api | claude-code
    // Preferred model for this session. Empty falls back to the provider's
    // default (the `claude` CLI's own default for Claude Code). For Claude Code
    // this is a CLI alias: opus | sonnet | haiku. Applied on the next launch or
    // continuation, and shown in the agent header.
    QString model;
    // Permission mode this session runs under, as the label from the composer's
    // mode selector ("Ask" / "Edit" / "Plan" / "Auto"; sessions written before
    // adhoc #38 shortened them say "Ask before edits" / "Auto mode" / …).
    // Captured when a follow-up prompt is sent so the next resume honors the live
    // selection, and shown in the agent header. Empty falls back to the global
    // kClaudeAutoModeSetting. Only "Auto" skips the CLI's permission prompts
    // today (see agentModeSkipsPermissions).
    QString mode;
    bool createPr = false;
    // A durable branch/PR association created for work that did not originate
    // in an Agent run. It is provenance only: it must never be resumed or claim
    // that an agent authored the branch's commits.
    bool associationOnly = false;
    // "YOLO" (adhoc #12): merge this session's branch straight into the repo's
    // default branch as soon as the run finishes successfully, with no review
    // step. Captured from the quick-add bar's checkbox when the session starts.
    bool yolo = false;
    // "Genie" (adhoc #42/#38): launched from the composer's genie button, which
    // starts a long-running run against the website's remote MCP server so the
    // agent works the organization's shared task list on its own. Stamped at
    // launch so a resumed genie is still one, and so every list can draw it with
    // its own sparkle status glyph instead of the ordinary run spinner.
    bool genie = false;
    // Reasoning strength ("low"/"medium"/"high"/"xhigh"/"max") the run was
    // launched with, snapshotted from kClaudeEffortSetting alongside the model
    // and mode so the organization task records what this run actually used.
    QString strength;
    // Organization task mirror (adhoc #18): a prompt launched with the composer's
    // "Task" toggle on also opens a task in the org, so work started on a desktop
    // is visible to everyone else. orgTaskId is the id the relay assigned (empty
    // when the task was declined, offline, or the toggle was off);
    // startedByBot/finishedByBot are the "<provider>@<machine>" labels of the bot
    // that launched the run and the one that reported it finished — finishedByBot
    // stays empty until the completion has been reported, and doubles as the
    // guard against reporting it twice.
    bool orgTask = false;
    QString orgTaskId;
    QString startedByBot;
    QString finishedByBot;
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

    // Whether this run should be drawn with the genie sparkle (adhoc #38): a
    // genie that is still working. Merged and finished sessions keep the
    // ordinary status glyphs, so their outcome reads like every other run's.
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
    // Claude Code stream-json transcript events, persisted one JSON object per
    // line so the rich transcript survives an app restart (issue #41).
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

    // Image attachments a session carries (adhoc #222), so the sessions list can
    // show a thumbnail of the screenshot a run was started from. Prompts and
    // transcripts name them as "Attached image: <path>" lines (see
    // AgentPromptImages), which is all this looks for. Paths come back exactly as
    // they were written, in the order they appear and de-duplicated: whether the
    // file is still on disk — and where it moved to — is the caller's business.
    static QStringList attachmentPathsIn(const QString &text);
    // The same, for one session: its stored prompt plus both persisted
    // transcripts. Only the two ends of each transcript are read (see
    // kAttachmentScanBytes), so scanning every session of a repo stays cheap.
    QStringList attachmentPaths(const AgentSession &session) const;
    // Cheap "has this session's transcript moved" stamp — the size and modified
    // time of both transcript files — so a caller can skip re-scanning a session
    // nothing has been appended to.
    QString transcriptStamp(const AgentSession &session) const;
    // How much of each transcript file the attachment scan reads, from the head
    // and again from the tail: the launch prompt sits at the head and the newest
    // follow-up at the tail, and an attachment can only be named in a prompt.
    static constexpr qint64 kAttachmentScanBytes = 128 * 1024;

private:
    QString sessionsDir() const;
    QString sessionDir(const AgentSession &session) const;
    int nextId() const;

    QString m_root;
};
