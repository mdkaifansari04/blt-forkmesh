#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class ForkMeshIdentity;

// One entry in an issue's append-only, signed history. Different event types use
// different fields. Signatures are raw Ed25519 over an
// explicit canonical string, matching the worker's ed25519_verify.
struct IssueEvent {
    QString type;          // open | comment | edit | status | labels | milestone | dates | priority | progress | assignees | agent | bounty | delete
    QString id;
    QString author;        // signer pubkey (base64url)
    QString authorName;
    qint64 ts = 0;
    QString title;         // open/title
    QString body;          // open/comment/edit body stored in issue JSON
    QStringList attachments; // open/comment/edit (issue-folder-relative paths)
    QString target;        // edit/delete (event id, or "self" for delete-issue)
    QString status;        // status: open|closed
    QStringList labels;    // labels
    QString milestone;     // milestone (empty = none)
    qint64 startDate = 0;  // dates: planned start (epoch ms, 0 = unset)
    qint64 endDate = 0;    // dates: planned end (epoch ms, 0 = unset)
    int priority = 0;      // priority: 1 (highest) through 8999 (lowest), 0 = unset
    int progress = 0;      // progress: 0..100 percent complete
    QStringList assignees; // assignees
    QString agentProvider; // agent: codex|openai|claude-api|claude-code
    int agentSessionId = 0; // agent
    QString agentStatus;   // agent
    bool agentCreatePr = false; // agent
    double bountyUsd = 0.0;     // legacy bounty audit metadata, in USD
    QString bountyAddress;      // legacy escrow address; migration-only/read-only
    QString bountyStatus;       // historical open | funded | paid state
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
    qint64 startDate = 0; // planned start (epoch ms, 0 = unset)
    qint64 endDate = 0;   // planned end (epoch ms, 0 = unset)
    int priority = 0; // 1 (highest) through 8999 (lowest), 0 = unset
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
    bool isDeleted() const;
    bool hasUnauthorizedDeleteAttempt() const;
};

// Issue-level metadata that rides alongside a remote "open" submission. These
// fields aren't part of the open event's signature (which only covers
// title/body/attachments), so the owner applies them on merge as vouched data.
struct RemoteIssueMeta {
    QStringList labels;
    QString milestone;
    int priority = 0;
    QStringList assignees;
    bool wantsAgent = false;
    QString wantsAgentModel;
    QString wantsAgentProvider;
};

// Raw bytes for one image a no-write-access node attached to a remote "open"
// or "comment" submission. Such a node has no working tree to copy the file
// into (see IssueStore::copyAttachments), so it hashes/names the file itself
// via readAttachmentsForRemoteSubmit() and ships the bytes alongside the
// signed event; applyRemoteEvent() writes them into the issue folder on
// merge, keyed by `name` (which already matches an entry in the event's
// signed IssueEvent::attachments list).
struct RemoteAttachment {
    QString name;
    QByteArray data;
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

class IssueStore
{
public:
    IssueStore(QString workTreePath, QString mirrorPath,
               const ForkMeshIdentity *identity, QString authorName = QString());

    bool canWrite() const;

    QList<Issue> loadAll(QString *error = nullptr,
                         const std::function<void()> &tick = {}) const;
    QList<Issue> loadAllStrict(QString *error = nullptr) const;
    QList<Issue> loadAllStrictAtRef(const QString &ref,
                                    QString *error = nullptr) const;
    QList<IssueLabel> loadLabels() const;
    QList<IssueMilestone> loadMilestones() const;

    // A cheap content signature of the issue metadata subtree: its git tree oid
    // (plus the source path, so a different repo can't collide). Two calls return
    // an equal, non-empty value iff loadAll()/loadLabels()/loadMilestones() would
    // return the same data, so the UI can skip re-reading and rebuilding the issue
    // list when nothing changed. Returns empty when it can't be computed (no
    // source) — treat empty as "unknown" and don't skip. One fast `git rev-parse`,
    // no blob reads.
    QString contentSignature() const;

    int createIssue(const QString &title, const QString &body,
                    const QStringList &labels, const QString &milestone,
                    int priority,
                    const QStringList &assignees,
                    const QStringList &attachmentSrcPaths, QString *error = nullptr,
                    Issue *createdOut = nullptr);
    int createIssue(const QString &title, const QString &body,
                    const QStringList &labels, const QString &milestone,
                    int priority,
                    const QStringList &assignees,
                    const QStringList &attachmentSrcPaths,
                    const QStringList &attachmentPlaceholders,
                    QString *error = nullptr, Issue *createdOut = nullptr);
    bool addComment(int number, const QString &body,
                    const QStringList &attachmentSrcPaths, QString *error = nullptr);
    bool addComment(int number, const QString &body,
                    const QStringList &attachmentSrcPaths,
                    const QStringList &attachmentPlaceholders,
                    QString *error = nullptr);
    bool addVote(int number, QString *error = nullptr);
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
    bool setDates(int number, qint64 startDate, qint64 endDate,
                  QString *error = nullptr);
    bool setPriority(int number, int priority, QString *error = nullptr);
    bool setProgress(int number, int progress, QString *error = nullptr);
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
    bool deleteIssue(int number, QString *error = nullptr);

    bool saveLabels(const QList<IssueLabel> &labels, QString *error = nullptr);
    bool saveMilestones(const QList<IssueMilestone> &milestones, QString *error = nullptr);

    // Build a signed event with the node identity (also used by the relay-sync
    // path). Fills id/author/authorName/ts/sig.
    IssueEvent makeSignedEvent(int number, IssueEvent ev) const;

    // Merge a signature-bearing event received from the relay inbox into the
    // local issue store (used by cross-user sync). The event must already be
    // signed and verified by the caller. `attachments` carries the raw bytes
    // for any names listed in ev.attachments (a remote submitter has no
    // working tree to have copied them into already); each is written into
    // the issue folder before the event is committed.
    bool applyRemoteEvent(int number, const IssueEvent &ev, const QString &titleIfNew,
                          QString *error = nullptr,
                          const RemoteIssueMeta &meta = {},
                          const QList<RemoteAttachment> &attachments = {});

    // For a node with no working tree (relay/inbox submission path): read and
    // content-address each source file the same way copyAttachments() would
    // (sha256-prefix + extension), without touching disk. The returned names
    // (RemoteAttachment::name) are what to put in IssueEvent::attachments
    // before signing; the bytes ride along in the submission so the owner's
    // applyRemoteEvent() can materialize them on merge.
    static QList<RemoteAttachment> readAttachmentsForRemoteSubmit(
        const QStringList &srcPaths);

    static QString substituteAttachmentPlaceholders(
        QString body, const QStringList &srcPaths, const QStringList &placeholders,
        const QStringList &attachmentNames);

    // The exact bytes that an event's signature commits to. Public + static so
    // it can be unit-tested and kept byte-identical to the worker's verifier.
    static QByteArray canonicalString(int number, const IssueEvent &ev);
    static QString contentForSigning(const IssueEvent &ev);

    static QString issueDirPath(const QString &workTree, int number);

private:
    QString issuesDir() const;                 // <workTree>/.forkmesh/issues
    QString issueDir(int number) const;        // <workTree>/.forkmesh/issues/{open,closed}/<n>
    QString issueFilePath(int number) const;   // <issueDir>/issue-<n>.json
    void migrateLegacyLayout() const;
    bool readIssueFile(int number, Issue &out) const;
    bool writeIssueFile(const Issue &issue, QString *error) const;
    void recomputeMetadata(Issue &issue) const; // fold events into top-level fields
    QStringList copyAttachments(int number, const QStringList &srcPaths) const;
    // Write the raw bytes a remote submitter shipped alongside a signed event
    // into the issue folder, one file per name in `names` that has a matching
    // entry in `attachments` (materializing what copyAttachments() would have
    // written had the submitter had a working tree to copy from).
    void writeRemoteAttachmentFiles(int number, const QStringList &names,
                                    const QList<RemoteAttachment> &attachments) const;
    int nextNumber() const;
    bool commit(const QString &message, QString *error) const;

    QList<Issue> loadFromMirror(QString *error, bool strict = false,
                                const QString &refOverride = QString()) const;
    QByteArray showFromMirror(const QString &repoRelPath, bool *ok,
                              const QString &refOverride = QString()) const;
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
