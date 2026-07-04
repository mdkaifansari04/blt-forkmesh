#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class ForkMeshIdentity;

// One entry in an issue's append-only, signed history. Different event types use
// different fields (see issues/README.md). Signatures are raw Ed25519 over an
// explicit canonical string, matching the worker's ed25519_verify.
struct IssueEvent {
    QString type;          // open | comment | edit | status | labels | milestone | priority | progress | assignees | agent | bounty | delete
    QString id;
    QString author;        // signer pubkey (base64url)
    QString authorName;
    qint64 ts = 0;
    QString title;         // open
    QString bodyFile;      // open/comment/edit: markdown path relative to issue dir
    QString body;          // in-memory body text loaded from bodyFile (not stored in info.json)
    QStringList attachments; // open/comment/edit (issue-folder-relative paths)
    QString target;        // edit/delete (event id, or "self" for delete-issue)
    QString status;        // status: open|closed
    QStringList labels;    // labels
    QString milestone;     // milestone (empty = none)
    int priority = 0;      // priority: 1 (highest) through 99 (lowest), 0 = unset
    int progress = 0;      // progress: 0..100 percent complete
    QStringList assignees; // assignees
    QString agentProvider; // agent: openai|claude-api (legacy: codex, claude-code)
    int agentSessionId = 0; // agent
    QString agentStatus;   // agent
    bool agentCreatePr = false; // agent
    double bountyUsd = 0.0;     // bounty: amount pledged, in USD
    QString bountyAddress;      // bounty: worker-issued Solana deposit address
    QString bountyStatus;       // bounty: open | funded | paid
    QString sig;

    QJsonObject toJson() const;
    static IssueEvent fromJson(const QJsonObject &obj);
};

// A single issue: derived metadata plus the full signed event log.
struct Issue {
    int number = 0;
    QString title;
    QString status = "open";
    QStringList labels;
    QString milestone;
    int priority = 0; // 1 (highest) through 99 (lowest), 0 = unset
    int progress = 0; // 0..100 percent complete (latest signed progress event)
    QStringList assignees;
    qint64 createdAt = 0;
    QString author;
    QString authorName;
    int votes = 0; // distinct voters (uptime-credit votes), derived from events
    // Bounty state folded from the latest signed "bounty" event (0 = none).
    double bountyUsd = 0.0;
    QString bountyAddress;
    QString bountyStatus;
    QList<IssueEvent> events;

    QJsonObject toJson() const;
    static Issue fromJson(const QJsonObject &obj);
    bool isDeleted() const; // a delete event targeting "self" tombstones the issue
};

// Issue-level metadata that rides alongside a remote "open" submission. These
// fields aren't part of the open event's signature (which only covers
// title/body/attachments), so the owner applies them on merge as vouched data.
struct RemoteIssueMeta {
    QStringList labels;
    QString milestone;
    int priority = 0;
    QStringList assignees;
    // Set by the repo owner's own submission form (only available to them) to
    // request that this new issue be auto-assigned to a coding agent as soon as
    // it's merged in from the inbox.
    bool wantsAgent = false;
    // Model the web submitter picked for that auto-assigned agent (e.g. "opus",
    // "sonnet"); empty leaves the provider's own default. Only meaningful
    // alongside wantsAgent.
    QString wantsAgentModel;
    // Agent provider the web submitter picked (e.g. "claude-code", "claude-api",
    // "openai"); empty falls back to the node's default provider. Only meaningful
    // alongside wantsAgent (adhoc #234 added the web-side dropdown).
    QString wantsAgentProvider;
};

struct IssueLabel {
    QString name;
    QString color;
};

struct IssueMilestone {
    QString title;
    qint64 due = 0;
    QString status = "open";
    QString description;
};

// Repo-scoped issue tracker backed by the on-disk issues/ folder. For repos with
// a local working tree the store reads/writes/commits files directly; for repos
// available only as a bare mirror it reads issues read-only via `git show`.
class IssueStore
{
public:
    // workTreePath: local checkout (may be empty for mirror-only repos).
    // mirrorPath: bare mirror used for read-only access when no work tree exists.
    // authorName: this node's display name, stamped onto authored events.
    IssueStore(QString workTreePath, QString mirrorPath,
               const ForkMeshIdentity *identity, QString authorName = QString());

    // True when this node can author/commit issues for the repo (has a real
    // working tree). Otherwise issues are read-only here (submit via the relay).
    bool canWrite() const;

    // `tick`, if set, is invoked once per issue as the (potentially long, on a
    // big repo) file-read loop runs, so a caller in an interactive load can pump
    // the GUI between reads and avoid tripping the stall watchdog.
    QList<Issue> loadAll(QString *error = nullptr,
                         const std::function<void()> &tick = {}) const;
    QList<IssueLabel> loadLabels() const;
    QList<IssueMilestone> loadMilestones() const;

    // A cheap content signature of the issues/ subtree: its git tree oid (plus the
    // source path, so a different repo can't collide). Two calls return an equal,
    // non-empty value iff loadAll()/loadLabels()/loadMilestones() would return the
    // same data, so the UI can skip re-reading and rebuilding the issue list when
    // nothing changed. Returns empty when it can't be computed (no source) — treat
    // empty as "unknown" and don't skip. One fast `git rev-parse`, no blob reads.
    QString contentSignature() const;

    // Mutations (require canWrite()). Each writes the issue's info.json (and the
    // label/milestone def files when relevant), then commits the issues/ folder.
    int createIssue(const QString &title, const QString &body,
                    const QStringList &labels, const QString &milestone,
                    int priority,
                    const QStringList &assignees,
                    const QStringList &attachmentSrcPaths, QString *error = nullptr);
    int createIssue(const QString &title, const QString &body,
                    const QStringList &labels, const QString &milestone,
                    int priority,
                    const QStringList &assignees,
                    const QStringList &attachmentSrcPaths,
                    const QStringList &attachmentPlaceholders,
                    QString *error = nullptr);
    bool addComment(int number, const QString &body,
                    const QStringList &attachmentSrcPaths, QString *error = nullptr);
    bool addComment(int number, const QString &body,
                    const QStringList &attachmentSrcPaths,
                    const QStringList &attachmentPlaceholders,
                    QString *error = nullptr);
    // Cast a vote on an issue (one vote per author). Owner-side write path.
    bool addVote(int number, QString *error = nullptr);
    // Edit an event's body. keepAttachments are existing issue-folder-relative
    // names to retain; newAttachmentSrcPaths are absolute images to copy in.
    bool editEvent(int number, const QString &eventId, const QString &newBody,
                   const QStringList &keepAttachments = {},
                   const QStringList &newAttachmentSrcPaths = {},
                   QString *error = nullptr);
    bool editEvent(int number, const QString &eventId, const QString &newBody,
                   const QStringList &keepAttachments,
                   const QStringList &newAttachmentSrcPaths,
                   const QStringList &newAttachmentPlaceholders,
                   QString *error = nullptr);
    // Rename an issue via a signed "title" event (folds into Issue::title).
    bool setTitle(int number, const QString &newTitle, QString *error = nullptr);
    bool setStatus(int number, const QString &status, QString *error = nullptr);
    bool setLabels(int number, const QStringList &labels, QString *error = nullptr);
    bool setMilestone(int number, const QString &milestone, QString *error = nullptr);
    bool setPriority(int number, int priority, QString *error = nullptr);
    bool setProgress(int number, int progress, QString *error = nullptr);
    // Pledge (or update) a bounty on an issue. address is the worker-issued
    // Solana deposit address; status is open|funded|paid.
    bool setBounty(int number, double amountUsd, const QString &address,
                   const QString &status, QString *error = nullptr);
    bool setAssignees(int number, const QStringList &assignees, QString *error = nullptr);
    bool assignAgent(int number, const QString &provider, int sessionId,
                     bool createPr, const QString &status, QString *error = nullptr);
    bool deleteEvent(int number, const QString &eventId, QString *error = nullptr);
    // Fast "regular" delete: append a signed delete/self tombstone event and
    // commit. The issue disappears from every list (see Issue::isDeleted) and the
    // tombstone syncs through the inbox like any other event. Cheap — no history
    // rewrite, so it never freezes the UI.
    bool tombstoneIssue(int number, QString *error = nullptr);
    // Destructive "delete with history": purge the issue from all of git history
    // via filter-branch + gc. Thorough but slow; callers should run it off the UI
    // thread.
    bool deleteIssue(int number, QString *error = nullptr);

    bool saveLabels(const QList<IssueLabel> &labels, QString *error = nullptr);
    bool saveMilestones(const QList<IssueMilestone> &milestones, QString *error = nullptr);

    // Build a signed event with the node identity (also used by the relay-sync
    // path). Fills id/author/authorName/ts/sig.
    IssueEvent makeSignedEvent(int number, IssueEvent ev) const;

    // Merge a signature-bearing event received from the relay inbox into the
    // local issues/ folder (used by cross-user sync). The event must already be
    // signed and verified by the caller.
    bool applyRemoteEvent(int number, const IssueEvent &ev, const QString &titleIfNew,
                          QString *error = nullptr,
                          const RemoteIssueMeta &meta = {});

    // The exact bytes that an event's signature commits to. Public + static so
    // it can be unit-tested and kept byte-identical to the worker's verifier.
    static QByteArray canonicalString(int number, const IssueEvent &ev);
    static QString contentForSigning(const IssueEvent &ev);

private:
    QString issuesDir() const;                 // <workTree>/issues
    QString issueDir(int number) const;        // <workTree>/issues/<n>
    bool readIssueFile(int number, Issue &out) const;
    bool writeIssueFile(const Issue &issue, QString *error) const;
    void recomputeMetadata(Issue &issue) const; // fold events into top-level fields
    QStringList copyAttachments(int number, const QStringList &srcPaths) const;
    int nextNumber() const;
    bool commit(const QString &message, QString *error) const;

    // Read-only access from a bare mirror via `git show <ref>:issues/...`.
    QList<Issue> loadFromMirror(QString *error) const;
    QByteArray showFromMirror(const QString &repoRelPath, bool *ok) const;
    QString mirrorRef() const;

    QString m_workTree;
    QString m_mirror;
    const ForkMeshIdentity *m_identity;
    QString m_authorName;

    // mirrorRef() is hit once per blob read from the mirror; cache it for the
    // store's lifetime so we resolve the ref via git at most once.
    mutable bool m_mirrorRefResolved = false;
    mutable QString m_cachedMirrorRef;
};
