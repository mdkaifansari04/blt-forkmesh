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
    // review threads and suggestions: a durable thread id plus optional range,
    // parent reply, suggestion patch, and applied commit state.
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
    QString patch;           // unified diff (from changes.patch, or derived from refs)
    // git format-patch series (mbox) for base..head when the PR is built from a
    // branch range, so the owner can replay it with `git am` and keep every
    // commit's author/date/message. Empty for working-tree or imported-patch PRs,
    // which fall back to a single `git apply` of `patch`.
    QString commits;         // from commits.mbox, or derived from refs
    // Branch-backed PRs keep their diff out of the repo entirely: the head branch
    // ref already carries every commit (full author history), so nothing but this
    // signed pull.md pointer is committed and `patch`/`commits` are reconstructed
    // from base..head on demand. Working-tree/imported/cross-node PRs are not
    // branch-backed and persist a portable patch/mbox alongside pull.md.
    bool branchBacked = false;
    // For a *merged* branch-backed PR, the exact base/head commits the merge
    // applied, so the historical diff stays viewable after the base absorbs the
    // commits (a live base...head would then resolve to empty). Empty otherwise.
    QString mergeBase;
    QString mergeHead;
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
    // Whether the PR's head branch is behind its base (the base carries commits the
    // head lacks). When `behindCount` is non-null it also receives how many such
    // commits the head is missing, so callers can show the branch's position
    // relative to the base ("N commits behind main").
    bool isBranchBehindBase(int number, bool *behind, QString *error = nullptr,
                            int *behindCount = nullptr) const;
    bool updateBranchFromBase(int number, QString *error = nullptr);
    // Apply the PR's patch into the working tree, commit, mark merged.
    bool mergePull(int number, QString *error = nullptr);
    // Dry-run the PR's patch against the working tree — the same 3-way apply
    // mergePull performs, but with --check so nothing is modified — to report
    // whether it will merge cleanly. Returns false only on a hard error
    // (no working tree, PR/patch missing); on success sets *clean and, when not
    // clean, fills *conflictFiles with the conflicting paths.
    // When `keepGuiAlive` is set the (potentially slow) `git apply --check` is
    // waited on by pumping posted events in short slices instead of blocking, so
    // a GUI-thread caller keeps the window responsive while the dry-run runs.
    bool checkMergeable(int number, bool *clean,
                        QStringList *conflictFiles = nullptr,
                        QString *error = nullptr,
                        bool keepGuiAlive = false) const;
    // The working tree's current HEAD commit, or empty when unavailable. Cheap
    // (one `git rev-parse`); used to fingerprint the base a checkMergeable()
    // result was computed against so callers can cache the dry-run apply and skip
    // re-spawning it for every open PR when neither the base nor the patch moved.
    QString baseTip() const;

    // Interactive conflict resolution that isolates the fix on the PR's own
    // branch instead of merging into the checked-out (base) branch.
    // startConflictMerge checks out a dedicated branch (pr.head, or pull/<N>)
    // started from the base, then runs `git am --3way` (the PR's commit series,
    // or a synthesized one-commit mbox from the flat patch) so conflict markers
    // land in the working tree. It needs a clean tree. On a clean apply it
    // finalizes immediately and sets *resolvedClean=true; on conflict it leaves
    // the am session in progress, fills *conflicted with the unmerged paths, and
    // returns true. The caller edits the files, then calls finishConflictMerge —
    // `git am --continue`, return to the original branch, regenerate the PR's
    // patch from the resolved branch, and leave the PR *open* (a later mergePull
    // lands it on the base) — or abortConflictMerge to restore the prior state
    // and drop the throwaway branch.
    bool startConflictMerge(int number, QStringList *conflicted,
                            bool *resolvedClean, QString *error = nullptr);
    bool finishConflictMerge(int number, QString *error = nullptr);
    // Tears down any in-progress PR-branch operation (resolve or edit): aborts a
    // pending `git am`, returns to the original branch, and drops the work branch.
    void abortConflictMerge();
    // Whether a `git am` session is currently in progress in the working tree.
    bool conflictMergeInProgress() const;

    // Edit a single file in an open PR, committing the change onto the PR's own
    // branch (like the conflict editor) and leaving the PR open + mergeable.
    // startPullFileEdit checks out the PR's branch with the PR applied (it needs a
    // clean tree and a conflict-free PR — resolve conflicts first otherwise) and
    // hands back the file's current contents via *content, leaving the branch
    // checked out. finishPullFileEdit writes the new contents, commits them on the
    // branch, returns to the original branch and regenerates the PR's patch. Cancel
    // with abortConflictMerge to discard the edit and drop the branch.
    bool startPullFileEdit(int number, const QString &relPath, QString *content,
                           QString *error = nullptr);
    bool finishPullFileEdit(int number, const QString &relPath,
                            const QString &content, QString *error = nullptr);
    // Delete a file on the PR's branch in one step: checks out the branch with the
    // PR applied (clean, conflict-free), removes the file, commits the deletion,
    // returns to the original branch and regenerates the PR's patch — the PR stays
    // open and mergeable. On failure the caller should abortConflictMerge().
    bool deletePullFile(int number, const QString &relPath, QString *error = nullptr);
    // Multi-file agent edit on the PR's branch (adhoc #82): startPullAgentEdit
    // checks out the PR's branch with the PR applied (clean tree and a
    // conflict-free PR required) and leaves it checked out so an agent can edit
    // any number of files in the working tree. finishPullAgentEdit stages
    // everything, commits it on the branch, returns to the original branch and
    // regenerates the PR's patch — the PR stays open and mergeable. Cancel with
    // abortConflictMerge to discard the edits and drop the branch.
    bool startPullAgentEdit(int number, QString *error = nullptr);
    bool finishPullAgentEdit(int number, const QString &commitMsg,
                             QString *error = nullptr);
    // Build a minimal mbox (single commit) from a flat patch so `git am` can
    // apply it and credit the PR author. Public for testing.
    static QString syntheticMbox(const PullRequest &pr);

    // Merge a signed PR received from the relay inbox into pulls/.
    bool applyRemotePull(const PullRequest &pr, QString *error = nullptr);
    // Remove the PR folder entirely and commit the deletion. This is fast and
    // leaves history intact. Pass rewriteHistory=true to also purge the PR's diff
    // text (changes.patch / commits.mbox) from every commit via a filter-branch
    // rewrite — thorough but slow (seconds to minutes on a large repo).
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
    // Shared machinery for the on-branch PR operations (resolve, edit file):
    // beginPullBranch checks out the PR's work branch and `git am`s the PR onto
    // it; finalizeOnPullBranch returns to the original branch, regenerates the
    // PR's patch/commits from the work branch, keeps it open, and commits the
    // refreshed pulls/ metadata. Both use the m_am* state.
    bool beginPullBranch(int number, QStringList *conflicted, bool *cleanApply,
                         QString *error);
    bool finalizeOnPullBranch(int number, const QString &commitMsg,
                              QString *error);

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

    // In-progress conflict-resolution state, carried from startConflictMerge to
    // finishConflictMerge/abortConflictMerge. The resolution is committed onto
    // m_amBranch (started from m_amBase); m_amRestoreRef is the branch/commit to
    // return to afterwards. All empty when no resolution is in progress.
    QString m_amBranch;
    QString m_amBase;
    QString m_amRestoreRef;
};
