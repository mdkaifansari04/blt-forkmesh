#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class ForkMeshIdentity;

// One entry in an issue's append-only, signed history. Different event types use
// different fields (see issues/README.md). Signatures are raw Ed25519 over an
// explicit canonical string, matching the worker's ed25519_verify.
struct IssueEvent {
    QString type;          // open | comment | edit | status | labels | milestone | priority | assignees | agent | delete
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
    QStringList assignees; // assignees
    QString agentProvider; // agent: codex|claude
    int agentSessionId = 0; // agent
    QString agentStatus;   // agent
    bool agentCreatePr = false; // agent
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
    QStringList assignees;
    qint64 createdAt = 0;
    QString author;
    QString authorName;
    int votes = 0; // distinct voters (uptime-credit votes), derived from events
    QList<IssueEvent> events;

    QJsonObject toJson() const;
    static Issue fromJson(const QJsonObject &obj);
    bool isDeleted() const; // a delete event targeting "self" tombstones the issue
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

    QList<Issue> loadAll(QString *error = nullptr) const;
    QList<IssueLabel> loadLabels() const;
    QList<IssueMilestone> loadMilestones() const;

    // Mutations (require canWrite()). Each writes the issue's info.json (and the
    // label/milestone def files when relevant), then commits the issues/ folder.
    int createIssue(const QString &title, const QString &body,
                    const QStringList &labels, const QString &milestone,
                    int priority,
                    const QStringList &assignees,
                    const QStringList &attachmentSrcPaths, QString *error = nullptr);
    bool addComment(int number, const QString &body,
                    const QStringList &attachmentSrcPaths, QString *error = nullptr);
    // Cast a vote on an issue (one vote per author). Owner-side write path.
    bool addVote(int number, QString *error = nullptr);
    // Edit an event's body. keepAttachments are existing issue-folder-relative
    // names to retain; newAttachmentSrcPaths are absolute images to copy in.
    bool editEvent(int number, const QString &eventId, const QString &newBody,
                   const QStringList &keepAttachments = {},
                   const QStringList &newAttachmentSrcPaths = {},
                   QString *error = nullptr);
    // Rename an issue via a signed "title" event (folds into Issue::title).
    bool setTitle(int number, const QString &newTitle, QString *error = nullptr);
    bool setStatus(int number, const QString &status, QString *error = nullptr);
    bool setLabels(int number, const QStringList &labels, QString *error = nullptr);
    bool setMilestone(int number, const QString &milestone, QString *error = nullptr);
    bool setPriority(int number, int priority, QString *error = nullptr);
    bool setAssignees(int number, const QStringList &assignees, QString *error = nullptr);
    bool assignAgent(int number, const QString &provider, int sessionId,
                     bool createPr, const QString &status, QString *error = nullptr);
    bool deleteEvent(int number, const QString &eventId, QString *error = nullptr);
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
                          QString *error = nullptr);

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
};
