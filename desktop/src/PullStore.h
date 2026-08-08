#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class ForkMeshIdentity;

// One entry in a pull request's append-only, signed conversation log. Stored as
// forkmesh/pulls/<N>/NNNN-<type>.md alongside pull.md, mirroring the issue event model.
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
    QString path;
    QString side;
    int line = 0;
    QString threadId;
    QString parentId;
    int lineStart = 0;
    int lineEnd = 0;
    QString suggestionPatch;
    QString targetPath;
    QString appliedCommit;
    QString sig;

    QJsonObject toJson() const; // wire format for the relay inbox
    static PullEvent fromJson(const QJsonObject &obj);
};

// A pull request: a signed proposal to merge `head` into `base`, carrying a
// unified diff/patch. Lives in the repo's.forkmesh/pulls/ folder; submitted cross-node
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
    QString patch;           // unified diff (from changes.patch, or derived from refs)
    QString commits;         // from commits.mbox, or derived from refs
    // Branch-backed PRs keep their diff out of the repo entirely: the head branch
    // ref already carries every commit (full author history), so nothing but this
    // signed pull.md pointer is committed and `patch`/`commits` are reconstructed
    // from base..head on demand. Working-tree/imported/cross-node PRs are not
    // branch-backed and persist a portable patch/mbox alongside pull.md.
    bool branchBacked = false;
    // Immutable commits captured when a branch-backed pull is signed. Strict
    // readers reconstruct the signed patch and mbox from these OIDs instead of
    // following the mutable base/head branch names.
    QString creationBaseOid;
    QString creationHeadOid;
    QString mergeBase;
    QString mergeHead;
    int filesChanged = 0;
    int additions = 0;
    int deletions = 0;
    QList<PullEvent> events; // conversation log (comments + reviews)

    QJsonObject toJson() const; // wire format for the relay inbox (includes patch)
    static PullRequest fromJson(const QJsonObject &obj);

    QString reviewSummary() const;
    int independentApprovalCount() const;
    bool hasIndependentChangesRequested() const;
    bool independentReviewGateSatisfied() const;
};

struct PullReviewCheckout {
    QString path;             // detached linked worktree holding the change
    QString baseOid;          // commit the submission was replayed onto
    QString headOid;          // tip of the review copy
    int commitCount = 0;      // commits the submission adds over the base
    QString patch;            // base..head diff, for the in-app review view
    bool uncommitted = false;
    QStringList conflicts;    // paths that did not apply cleanly (if any)
};

class PullStore
{
public:
    PullStore(QString workTreePath, QString mirrorPath,
              const ForkMeshIdentity *identity, QString authorName = QString());

    bool canWrite() const; // has a working tree (can author/merge)

    QList<PullRequest> loadAll(QString *error = nullptr) const;
    QList<PullRequest> loadAllStrict(QString *error = nullptr) const;
    QList<PullRequest> loadAllStrictAtRef(const QString &ref,
                                          QString *error = nullptr) const;

    // Owner-side: create a PR locally from an already-computed diff. `commits` is
    // the optional format-patch mbox (base..head) used to preserve authorship on
    // merge; pass an empty string for working-tree/imported patches. Set
    // `branchBacked` when `head` is a real branch whose committed range (base..head)
    // *is* the change: the PR then stores only its signed pointer and reconstructs
    // the diff/commits from the synced ref, so no diff text is committed to the
    // repo (the passed patch/commits are recomputed from the refs before signing).
    int createPull(const QString &title, const QString &description,
                   const QString &base, const QString &head, const QString &patch,
                   const QString &commits, bool branchBacked = false,
                   QString *error = nullptr);
    bool setStatus(int number, const QString &status, QString *error = nullptr);
    bool isBranchBehindBase(int number, bool *behind, QString *error = nullptr,
                            int *behindCount = nullptr) const;
    bool updateBranchFromBase(int number, QString *error = nullptr);
    bool mergePull(int number, QString *error = nullptr,
                   bool requirePeerReview = true);
    bool checkMergeable(int number, bool *clean,
                        QStringList *conflictFiles = nullptr,
                        QString *error = nullptr,
                        bool keepGuiAlive = false) const;
    QString baseTip() const;

    bool startConflictMerge(int number, QStringList *conflicted,
                            bool *resolvedClean, QString *error = nullptr);
    bool startConflictAgentEdit(int number, QStringList *conflicted,
                                bool *resolvedClean,
                                QString *error = nullptr);
    bool finishConflictMerge(int number, QString *error = nullptr);
    void abortConflictMerge();
    bool conflictMergeInProgress() const;

    bool startPullFileEdit(int number, const QString &relPath, QString *content,
                           QString *error = nullptr);
    bool finishPullFileEdit(int number, const QString &relPath,
                            const QString &content, QString *error = nullptr);
    bool deletePullFile(int number, const QString &relPath, QString *error = nullptr);
    bool startPullAgentEdit(int number, QString *error = nullptr);
    bool finishPullAgentEdit(int number, const QString &commitMsg,
                             QString *error = nullptr);
    QString agentEditWorkTree() const;
    static QString syntheticMbox(const PullRequest &pr);

    // Merge a signed PR received from the relay inbox into.forkmesh/pulls/.
    bool applyRemotePull(const PullRequest &pr, QString *error = nullptr);

    bool checkoutForReview(const PullRequest &pr, const QString &dir,
                           PullReviewCheckout *out, QString *error = nullptr) const;
    void discardReviewCheckout(const QString &dir) const;
    bool deletePull(int number, bool rewriteHistory, QString *error = nullptr);

    // Conversation: append a signed comment or review event, then commit. The
    // review state is one of approved | changes_requested | commented.
    bool addComment(int number, const QString &body, QString *error = nullptr);
    bool addReview(int number, const QString &state, const QString &body,
                   QString *error = nullptr);
    // Append a signed comment anchored to a specific file + line of the diff.
    bool addLineComment(int number, const QString &path, const QString &side,
                        int line, const QString &body, QString *error = nullptr);
    bool addThreadComment(int number, const QString &path, const QString &side,
                          int lineStart, int lineEnd, const QString &body,
                          const QString &suggestionPatch = QString(),
                          QString *error = nullptr);
    bool addThreadReply(int number, const QString &threadId,
                        const QString &parentId, const QString &body,
                        QString *error = nullptr);
    bool setThreadState(int number, const QString &threadId, const QString &state,
                        const QString &body = QString(),
                        QString *error = nullptr);
    bool setSuggestionState(int number, const QString &threadId,
                            const QString &state,
                            const QString &appliedCommit = QString(),
                            const QString &body = QString(),
                            QString *error = nullptr);
    // Merge a signed event received from the relay inbox into.forkmesh/pulls/<N>/. The
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
    static QByteArray legacyCanonicalString(const PullRequest &pr);

    static void computeStats(PullRequest &pr);

    QString metaWorkTree() const;

private:
    bool beginPullBranch(int number, QStringList *conflicted, bool *cleanApply,
                         QString *error);
    bool finalizeOnPullBranch(int number, const QString &commitMsg,
                              QString *error);
    bool beginPullEditWorkTree(int number, QString *error);
    bool beginPullConflictWorkTree(int number, QStringList *conflicted,
                                   bool *cleanApply, QString *error);
    void discardPullEditWorkTree();
    bool moveBranchToEditTip(QString *error);

    QString pullsDir() const;
    QString pullDir(int number) const;
    bool materializePullRef(const PullRequest &pr, QString *error) const;
    // Tie the signed metadata ledger state to the PR it describes. The shared
    // forkmesh/pulls branch remains the transport/index for older peers, while
    // refs/pr/<n>/metadata gives each PR its own stable metadata pointer.
    bool materializePullMetadataRef(int number, QString *error) const;
    bool purgePullPayloadHistory(int number, QString *error) const;
    int nextNumber() const;
    bool writePull(const PullRequest &pr, QString *error) const;
    bool readPull(int number, PullRequest &out) const;
    QList<PullEvent> readEvents(int number) const;
    int nextEventIndex(int number) const;
    bool writeEventFile(int number, int index, const PullEvent &ev,
                        QString *error) const;
    bool appendEvent(int number, const PullEvent &ev, const QString &commitMsg,
                     QString *error);
    bool commit(const QString &message, QString *error) const;
    QString mirrorRef() const;
    QByteArray showFromMirror(const QString &repoRelPath, bool *ok,
                              const QString &refOverride = QString()) const;
    QList<PullRequest> loadFromMirror(
        QString *error, bool strict = false,
        const QString &refOverride = QString()) const;

    QString m_workTree;
    QString m_mirror;
    const ForkMeshIdentity *m_identity;
    QString m_authorName;

    mutable QString m_metaWorkTreeCache;

    QString m_amBranch;
    QString m_amBase;
    QString m_amRestoreRef;
    QString m_editWorkTree;
};
