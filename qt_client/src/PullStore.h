#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class ForkMeshIdentity;

// One entry in a pull request's append-only, signed conversation log. Stored as
// pulls/<N>/NNNN-<type>.md alongside pull.md, mirroring the issue event model.
// Signatures are raw Ed25519 over an explicit canonical string that the worker's
// verify_pull_comment_event reproduces byte-for-byte.
struct PullEvent {
    QString type;        // comment | review | line-comment
    QString id;
    QString author;      // signer pubkey (base64url)
    QString authorName;
    qint64 ts = 0;
    QString body;        // comment/review/line-comment body (markdown)
    QString state;       // review: approved | changes_requested | commented
    // line-comment: the file and line the comment is anchored to. side is
    // "old" (left/base) or "new" (right/head); line is the 1-based line number.
    QString path;
    QString side;
    int line = 0;
    QString sig;

    QJsonObject toJson() const; // wire format for the relay inbox
    static PullEvent fromJson(const QJsonObject &obj);
};

// A pull request: a signed proposal to merge `head` into `base`, carrying a
// unified diff/patch. Lives in the repo's pulls/ folder; submitted cross-node
// through the relay inbox (like issue submissions).
struct PullRequest {
    int number = 0;
    QString title;
    QString description;
    QString base;
    QString head;
    QString status = "open"; // open | merged | closed
    qint64 ts = 0;
    QString author;          // signer pubkey (base64url)
    QString authorName;
    QString sig;
    QString patch;           // unified diff (from changes.patch)
    int filesChanged = 0;
    int additions = 0;
    int deletions = 0;
    QList<PullEvent> events; // conversation log (comments + reviews)

    QJsonObject toJson() const; // wire format for the relay inbox (includes patch)
    static PullRequest fromJson(const QJsonObject &obj);

    // Latest review state per author, folded over the event log:
    // "approved" if any node currently approves and none requests changes,
    // "changes_requested" if any node currently requests changes, else "".
    QString reviewSummary() const;
};

// Repo-scoped pull-request store backed by the on-disk pulls/ folder. Mirrors
// IssueStore: read/write/commit for repos with a working tree, read-only via the
// bare mirror otherwise.
class PullStore
{
public:
    PullStore(QString workTreePath, QString mirrorPath,
              const ForkMeshIdentity *identity, QString authorName = QString());

    bool canWrite() const; // has a working tree (can author/merge)

    QList<PullRequest> loadAll(QString *error = nullptr) const;

    // Owner-side: create a PR locally from an already-computed diff.
    int createPull(const QString &title, const QString &description,
                   const QString &base, const QString &head, const QString &patch,
                   QString *error = nullptr);
    bool setStatus(int number, const QString &status, QString *error = nullptr);
    bool isBranchBehindBase(int number, bool *behind, QString *error = nullptr) const;
    bool updateBranchFromBase(int number, QString *error = nullptr);
    // Apply the PR's patch into the working tree, commit, mark merged.
    bool mergePull(int number, QString *error = nullptr);
    // Dry-run the PR's patch against the working tree — the same 3-way apply
    // mergePull performs, but with --check so nothing is modified — to report
    // whether it will merge cleanly. Returns false only on a hard error
    // (no working tree, PR/patch missing); on success sets *clean and, when not
    // clean, fills *conflictFiles with the conflicting paths.
    bool checkMergeable(int number, bool *clean,
                        QStringList *conflictFiles = nullptr,
                        QString *error = nullptr) const;
    // Merge a signed PR received from the relay inbox into pulls/.
    bool applyRemotePull(const PullRequest &pr, QString *error = nullptr);
    // Remove the PR folder entirely and commit the deletion.
    bool deletePull(int number, QString *error = nullptr);

    // Conversation: append a signed comment or review event, then commit. The
    // review state is one of approved | changes_requested | commented.
    bool addComment(int number, const QString &body, QString *error = nullptr);
    bool addReview(int number, const QString &state, const QString &body,
                   QString *error = nullptr);
    // Append a signed comment anchored to a specific file + line of the diff.
    bool addLineComment(int number, const QString &path, const QString &side,
                        int line, const QString &body, QString *error = nullptr);
    // Merge a signed event received from the relay inbox into pulls/<N>/. The
    // event must already be signed and verified by the caller.
    bool applyRemoteEvent(int number, const PullEvent &ev, QString *error = nullptr);

    // Sign a conversation event with the node identity (fills id/author/
    // authorName/ts/sig). The signature binds the PR number — reviewers act on
    // the owner's synced mirror, which carries canonical numbers.
    PullEvent makeSignedEvent(int number, PullEvent ev) const;
    static QByteArray canonicalString(int number, const PullEvent &ev);
    static QString contentForSigning(const PullEvent &ev);

    // Sign a PR with the node identity (fills author/authorName/ts/sig). The
    // signature commits to title/base/head/patch (not the number, which the
    // owner assigns on merge).
    PullRequest makeSignedPull(PullRequest pr) const;
    static QByteArray canonicalString(const PullRequest &pr);

    // Count files/additions/deletions from a unified diff.
    static void computeStats(PullRequest &pr);

private:
    QString pullsDir() const;
    QString pullDir(int number) const;
    int nextNumber() const;
    bool writePull(const PullRequest &pr, QString *error) const;
    bool readPull(int number, PullRequest &out) const;
    // Append-only event files: read all, find the next NNNN index, write one.
    QList<PullEvent> readEvents(int number) const;
    int nextEventIndex(int number) const;
    bool writeEventFile(int number, int index, const PullEvent &ev,
                        QString *error) const;
    bool appendEvent(int number, const PullEvent &ev, const QString &commitMsg,
                     QString *error);
    bool commit(const QString &message, QString *error) const;
    QString mirrorRef() const;
    QByteArray showFromMirror(const QString &repoRelPath, bool *ok) const;
    QList<PullRequest> loadFromMirror(QString *error) const;

    QString m_workTree;
    QString m_mirror;
    const ForkMeshIdentity *m_identity;
    QString m_authorName;
};
