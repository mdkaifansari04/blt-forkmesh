#pragma once

#include "ChatBackend.h"
#include "DiscussionInboxBackoff.h"
#include "DiscussionStore.h"
#include "ForkMeshIdentity.h"
#include "IssueStore.h"
#include "PullStore.h"
#include "CoveStore.h"
#include "ActionStore.h"
#include "ActionFile.h"
#include "AgentStore.h"
#include "AgentRunner.h"
#include "ClaudeSessionScan.h"

struct CommitComment; // CommitCommentStore.h

#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QPixmap>
#include <QSet>
#include <QUrl>

#include <functional>
#include <limits>

class MessageRow;
class MarkdownEditor;
class TerminalWidget;
class ClaudeIdeBridge;
class ClaudeStreamSession;
class ClaudeTranscriptView;
class RepoHost;
class ActionRunner;
class QButtonGroup;
class QGridLayout;
class QFileSystemWatcher;
class QSplitter;
class QTextEdit;
class QCheckBox;
class QComboBox;
class QCompleter;
class QStringListModel;
class QGraphicsOpacityEffect;
class QLabel;
class QMouseEvent;
class QAction;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QNetworkAccessManager;
class QPlainTextEdit;
class QProgressBar;
class QPropertyAnimation;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QSystemTrayIcon;
class QTableWidget;
class QTabWidget;
class QTextBrowser;
class QTextEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QProcess;
class QVBoxLayout;
class QHBoxLayout;

// A configured mainnode the user can connect to. The client connects to one at
// a time; the favicon rail switches the active one.
struct ServerConfig {
    QString url;
    QString room;
};

// Per-repository metadata that git can't provide, stored in the repo's info.json
// (about text, topics, social counts, contributor avatar overrides).
struct RepoInfo {
    QString about;
    QString website;
    QString language; // optional primary-language override
    QStringList topics;
    int forks = 0;
    int stars = 0;
    int mirrors = 1;
    QString defaultBranch;
    QHash<QString, QString> contributorAvatars; // name -> image path/URL
};

struct RepositoryRecord {
    QString owner;
    QString name;
    QString description;
    QString cloneUrl;
    QString localPath;
    QString solanaAddress;
    QString mirrorPath;
    bool publishToNetwork = false;
    // Private repo: hidden from the public catalog and readable (browse/clone
    // through the relay) only with an owner-key-signed forkmesh-view-v1 token, so
    // only this node's key holder can reach it. Still published/hosted otherwise.
    bool isPrivate = false;
    // Run .forkmesh/ workflows when a fork pushes to this repo's bare mirror.
    // Enabled by default; can be turned off per repo on the Actions tab. Pushed
    // workflow changes still require explicit approval before they run.
    bool actionsEnabled = true;
    // Temporary, browse-only cache for a repo hosted by another node. Preview
    // repos are not saved, advertised, published, hosted, or wired for actions.
    bool previewOnly = false;
    qint64 hostedSinceMs = 0;
    qint64 lastSyncMs = 0;
    qint64 publishedAtMs = 0;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Apply the saved theme (system/dark/light) to the whole application.
    static void applyTheme();
    void refreshThemedIcons();

#ifdef FORKMESH_WINDOW_TESTS
    using TestIssueHistoryDeleteRunner =
        std::function<bool(int number, QString *error)>;
    void testSetRoster(const QList<MemberInfo> &members) { setRoster(members); }
    void testSetNodeAlertGraceUntilMs(qint64 value) { m_nodeAlertGraceUntilMs = value; }
    QStringList testNetworkLog() const { return m_networkLog; }
    QStringList testQuickUpdatePullArguments(const QString &clientDir) const;
    void testSetSetupInputs(const QString &name, const QString &solana);
    void testSetAccountFlowResult(bool result)
    {
        m_testUseAccountFlowResult = true;
        m_testAccountFlowResult = result;
        m_testEnsureNodeAccountCalls = 0;
    }
    void testEnableSessionStartBypass(bool value) { m_testBypassServerStart = value; }
    void testStartSession() { startSession(); }
    void testEnablePaidMirroring() { enablePaidMirroring(); }
    int testAccountFlowCalls() const { return m_testEnsureNodeAccountCalls; }
    int testStackIndex() const;
    QString testUserName() const { return m_userName; }
    QString testAccountName() const { return m_accountName; }
    QString testSavedSolanaAddress() const;
    bool testAccountAuthenticated() const { return m_accountAuthenticated; }
    QString testAccountTier() const { return m_accountTier; }
    // Verifies makeColumnsResizable(): once rows arrive, ResizeToContents columns
    // flip to draggable Interactive (keeping their fitted widths) while Stretch
    // and Fixed columns are left untouched.
    Q_INVOKABLE bool testColumnsBecomeResizable();
    // Verifies installMarginResize(): dragging a draggable column's divider
    // trades width with its immediate neighbour (like moving a margin) instead
    // of letting a far-off Stretch column absorb the change.
    Q_INVOKABLE bool testMarginResize();
    Q_INVOKABLE int testAddLocalRepository(const QString &owner, const QString &name,
                                           const QString &localPath);
    Q_INVOKABLE bool testOpenRepository(int index);
    Q_INVOKABLE bool testSaveRepoAboutMetadata(const QString &about,
                                               const QString &website)
    {
        return saveRepoAboutMetadata(about, website, nullptr);
    }
    int testAddPublishedRepository(const QString &owner, const QString &name,
                                   const QString &mirrorPath);
    void testPublishRepository(int index) { publishRepository(index, false); }
    void testStartRepoHosts() { startRepoHosts(); }
    void testStopRepoHosts() { stopRepoHosts(); }
    void testShowPublishBar(bool on);
    int testRepoTabContentTop(); // y of the tab content within the window
    int testRepoHostCount() const { return m_repoHosts.size(); }
    void testSetIssueHistoryDeleteRunner(TestIssueHistoryDeleteRunner runner)
    {
        m_testIssueHistoryDeleteRunner = std::move(runner);
    }
    void testDeleteIssueWithHistory(int number) { deleteCurrentIssueWithHistory(number); }
    bool testIssueHistoryDeleteInProgress() const
    {
        return m_issueHistoryDeleteInProgress;
    }
    // Provider id (openai/claude-api/claude-code) currently selected in each
    // agent-assignment picker, plus a way to drive the Settings "Default agent"
    // combo as a user would, so tests can assert the default seeds/updates them.
    QString testQuickAddAgentProvider() const;
    QString testIssueAgentProvider() const;
    void testSetDefaultAgentProvider(const QString &provider);
#endif

protected:
    void closeEvent(QCloseEvent *event) override;
    // Rescan the changes panel when the window regains focus (e.g. after a
    // background agent edited the working tree) so it always shows fresh state.
    void changeEvent(QEvent *event) override;
    // Defers heavy, git-backed startup until the window's first frame is on
    // screen, so launch shows the themed UI instead of an unpainted black frame.
    void showEvent(QShowEvent *event) override;
    // Image drag-and-drop onto the inline issue comment composer.
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    // Setup page
    QWidget *buildSetupPage();
    void startSession();
    // Heavy, git-backed work deferred from the constructor until the window has
    // painted its first frame: silent auth + restoring the last open repository.
    // Idempotent — runs at most once, whichever trigger (expose or fallback)
    // fires first. See showEvent().
    void runDeferredStartup();
    bool m_deferredStartupStarted = false; // showEvent armed the triggers
    bool m_deferredStartupRun = false;     // runDeferredStartup already ran
    int m_pendingRestoreRepoIndex = -1;    // last repo to reopen, or -1
    bool m_pendingSilentAuth = false;      // attempt auto-connect on first frame
    // Account = node identity. Registration (name + Solana + password + TOTP) gates
    // joining the network; the account name is the canonical repo owner.
    bool ensureNodeAccount(const QString &accountName, const QString &solana);
    // Non-interactive auth used on launch: true only if this node key already
    // matches a registered active account (or was confirmed before, offline).
    bool authenticateSilently(const QString &accountName);
    // In-app join wizard: reserve node name -> donate -> email/password, mirroring
    // the website signup funnel (signup.html / signup.js) as one staged dialog.
    bool runSignupFlow(const QString &accountName, const QString &solana);
    bool runLoginFlow(const QString &accountName);
    QJsonArray fetchCatalogRepos();
    int fetchNodesOnline();
    bool hasActiveAccountSession() const;
    void mirrorCatalogRepo(const QString &owner, const QString &name,
                           const QString &cloneUrl, bool isPrivate = false);
    // Hosted git URL (https://<mainnode>/<owner>/<name>) for a catalog repo,
    // used when the catalog record omits an explicit cloneUrl.
    QString hostedCloneUrl(const QString &owner, const QString &name) const;
    // Bootstrap a fresh client by mirroring the flagship ForkMesh repository so
    // it appears in the Repos list without first walking the full join flow.
    void ensureFlagshipRepo();
    // Log in by email (+ optional TOTP). accountName is only used as a fallback
    // node name if the server response omits one.
    bool verifyTotpLogin(const QString &email, const QString &password,
                         const QString &totp, const QString &accountName);
    // Periodic signed heartbeat that keeps this node eligible for the reward
    // split and refreshes its payout Solana address.
    void sendNodeHeartbeat();
    // Admin: poll for newly-joined users and verify their email by hand (until a
    // real email service is wired up). Only active for accounts in ADMIN_NODES.
    void pollPendingUsers();
    void showAdminVerifyDialog();
    bool adminVerifyEmail(const QString &target);
    void verifyWallet();
    QUrl accountsApiUrl(const QString &leaf) const;
    QJsonObject postAccountSync(const QString &leaf, const QJsonObject &body,
                                int *status);
    QJsonObject getAccountSync(const QString &leaf, int *status);
    QString accountOwner() const; // the registered account name (repo namespace)
    // The owner a repo is published/browsed under on the website. Must match the
    // owner the live host tunnel registers with, or the website can't find the
    // host. Mirrors the fallback used when publishing.
    QString catalogOwner(const RepositoryRecord &repo) const;
    void runQuickUpdate();
    // Pull a fresh copy from the install URL (the live hosted mirror), then
    // rebuild and relaunch. Installs into the invoking non-root user's home even
    // when ForkMesh itself is running as root.
    void updateRebuildRestart();
    QString resolveInstallCloneUrl();
    void buildAndRelaunch(const QString &clientDir, const QString &asUser = QString(),
                          const QString &relaunchPath = QString(),
                          const QString &buildType = QStringLiteral("Release"));
    void installAndRelaunch(const QString &built, const QString &appPath);
    // When onFailure is set it is invoked instead of the default "Update failed"
    // handling if the step exits non-zero, letting callers recover (e.g. re-clone
    // a checkout that has diverged from the mirror).
    void runUpdateStep(const QString &program, const QStringList &arguments,
                       const QString &workingDir, std::function<void()> onSuccess,
                       std::function<void()> onFailure = {});
    // Like runUpdateStep, but runs the command as m_updateAsUser (via sudo -u)
    // when that is set, so root-launched updates write files owned by the user.
    void runUpdateStepUser(const QString &program, const QStringList &arguments,
                           const QString &workingDir, std::function<void()> onSuccess,
                           std::function<void()> onFailure = {});
    void setUpdateStatus(const QString &status, bool isError = false);
    // Open (or reset) the live update/rebuild log window and append to it.
    void showUpdateLog();
    void appendUpdateLog(const QString &text);
    void persistProfile();

    // Chat page
    QWidget *buildChatPage();

    // Global donation nudge shown until this node sets a Solana address.
    QWidget *buildSolanaNotice();
    void updateSolanaNotice();
    void promptSetSolanaAddress();
    // The one opt-in entry into the crypto side: set a Solana payout address,
    // activate the node's network account, and start hosting its mirrors so it
    // earns donations. Nothing in the core flow (clone, mirror, issues, PRs)
    // routes here — it is reached only from the "Get paid to mirror" button.
    void enablePaidMirroring();

    // Top breadcrumb: active server > current section.
    QWidget *buildBreadcrumb();
    void updateBreadcrumb();
    void showRelayMenu();          // searchable dropdown to switch/add relays
    void updateRelaySwitcher();    // refresh top-bar relay icon / domain / count
    void openServerWebsite(int index); // open a relay's site in the browser
    void showNodeMenu();           // searchable dropdown to pick a node
    void showNodesWindow();        // full window listing nodes, status, earnings
    void updateNodeSwitcher();     // refresh top-bar node label / count
    void updateNavSolanaBalance(); // refresh top-bar balance for this node
    void cycleNavSolanaCurrency(); // SOL -> USD -> INR -> SOL on balance click
    // Re-render the top-bar balance from the cached lamports/fiat rate without
    // re-hitting the network, so cycling SOL/USD/INR is instant and can't stall
    // on getBalance / price rate-limits.
    void renderNavSolanaBalance();
    void queryNavSolanaBalance(const QString &addr, int endpointIndex);
    void queryNavSolanaUsdPrice(const QString &addr, qint64 lamports);
    void showRepoMenu();           // dropdown to open repos / add a local repo
    void updateRepoSwitcher();     // refresh top-bar repo label / count
    void updateRepoPushButton();   // show pending local commits for the open repo
    void pushCurrentRepoUpstream();
    // Integrity pin: sha256 over the canonical heads+tags advertisement of a bare
    // mirror, byte-for-byte identical to the worker's advertised_refs_canonical().
    // The owner signs this on publish and the relay pins it. Empty when the mirror
    // path is unset/unreadable.
    QString mirrorStateHash(const QString &mirrorPath) const;
    // Signed catalog-list URL (adds our viewer token so the relay also returns our
    // own private repos). Shared by fetchCatalogRepos() and refreshRepoPinBanner().
    QUrl catalogListUrl();
    // Show/hide the "clones are being rejected" warning on the owner's node when the
    // relay's pinned stateHash no longer matches the refs this node serves. The
    // warning surfaces as the top-bar notification toast (with Reset / Why links),
    // not an in-page banner.
    void refreshRepoPinBanner();
    // Show the integrity-pin warning in the top-bar toast, persistent (like an error
    // toast) with clickable "Reset integrity pin" and "Why?" links.
    void showPinWarning();
    // Clear the top-bar toast only if it is currently the integrity-pin warning, so
    // an unrelated toast isn't clobbered when the pin becomes healthy again.
    void dismissPinWarning();
    // Re-attest the open repo's current refs, overwriting a stale relay pin.
    void resetRepoPin();
    // Dialog explaining what the integrity pin is and why a reset is needed.
    void showPinExplanation();
    // True when the open repo's branch tracks the ForkMesh relay (which serves
    // clone/fetch only, no git-receive-pack). Such repos publish by syncing the
    // served mirror from the local copy, not by a git push to the relay.
    bool relayPublishRepo(const RepositoryRecord &repo, QString *localBranch,
                          int *unpublished) const;
    void showChatView();           // open the chat view from the top-bar button
    void updateChatButton();       // refresh the top-bar chat unread indicator
    bool isChatViewVisible() const; // chat tab open + window active (i.e. being read)
    void updateConnectionStatus(); // top-right "● Connected · N nodes online"
    // Bottom quick-add issue bar (the network log now lives in its own section).
    QWidget *buildNetworkLogDock();
    // Refresh the footer's centered git-identity label for the open repo.
    void updateFooterGitIdentity();
    // Full-height "Log" section (section 4) showing the whole network log.
    QWidget *buildLogSection();

    // Mainnode relays (shown in the top-bar relay switcher)
    void loadServers();
    void saveServers();
    void switchToServer(int index);
    void promptAddServer();
    void removeServer(int index);
    void loadActiveServerIntoEdits();
    void persistEditsToActiveServer();
    void loadCachedFavicons();
    void fetchFavicon(int index);
    QPixmap faviconFor(const ServerConfig &server) const;
    QWidget *buildHomeSection();
    // Slack-style node profile panel (right side of Home).
    QWidget *buildNodeProfilePanel();
    void showNodeProfile(const QString &nodeId, const QString &nodeName);
    void refreshProfileHostingStats(); // rebuild the per-repo hosting lines
    void rescaleProfileAvatar();       // re-render the full-width avatar banner
    void hideNodeProfile();
    void checkNodeBalance();
    // Query the Solana network for a balance via public JSON-RPC endpoints.
    void querySolanaBalance(const QString &addr, int endpointIndex);
    // Fill the repositories column with the repos owned by the selected node.
    void selectNode(const QString &node);
    QWidget *buildIssuesSection();
    QWidget *buildChatSection();
    QWidget *buildSettingsSection();
    QWidget *buildNotificationsSection();
    // Network leaderboards (issue #11): fetched from /api/network/leaderboards.
    QWidget *buildLeaderboardsSection();
    void refreshLeaderboards();
    void populateLeaderboards(const QJsonObject &data);

    // Repo detail view (files + issues tabs), opened by clicking a repository.
    QWidget *buildRepoDetailSection();
    QWidget *buildRepoFilesPanel();
    QWidget *buildRepoOverviewPage();
    QWidget *buildRepoEditorPage();
    QWidget *buildRepoCommitsTab();
    void showCommit(const QString &hash); // open the commit diff detail view
    void showCommitList();                // back to the commits list
    void openMostRecentCommit();          // select newest commit + expand its diff
    // Remove a commit from the browsed branch's history (source-of-truth only),
    // replaying its descendants onto its parent. Rewrites local history.
    void deleteCommit(const QString &hash);
    // Undo a commit by recording a new commit that reverses its changes
    // (git revert). Keeps history intact, unlike deleteCommit. Owner-only.
    void revertCommit(const QString &hash);
    // In-flow banner above the commit table flagging unsynced commits: reveal
    // it, or fade it out (collapsing its row) when everything has synced.
    void showCommitsBanner(const QString &html);
    void hideCommitsBanner();
    // Open the issue or pull request referenced by "#<number>" in a commit
    // message (a PR if one matches, otherwise an issue).
    void openCommitReference(int number);
    // Filter the commit list by the search box (matches hash or summary).
    void filterCommits(const QString &query);
    void downloadCommitPatch();           // save the open commit as a .patch file
    void renderCommitThread(const QString &sha); // per-commit conversation
    void submitCommitComment();                  // post a comment on the open commit
    // Sync a diff split/unified toggle button's label+tooltip to the preference.
    void updateDiffSplitButton(QPushButton *button);
    // Append one conversation card (avatar + header + markdown body) to a thread
    // layout (shared by the PR and commit conversation panels). headerHtml is the
    // already-escaped "<b>name</b> verb when" line; an accent colors the card edge.
    void addConversationCard(QVBoxLayout *layout, const QString &author,
                             const QString &headerHtml, const QString &body,
                             const QString &accent = QString());
    QWidget *buildAboutSidebar();
    QWidget *buildRepoSecurityTab();
    QWidget *buildInsightsTab();
    QWidget *buildPlaceholderTab(const QString &name);

    // Discussions tab (signed repository discussions with inbox fallback).
    QWidget *buildDiscussionsTab();
    DiscussionStore discussionStoreForCurrentRepo() const;
    void reloadDiscussions();
    void showDiscussion(int number);
    void renderDiscussionThread(const Discussion &discussion);
    void updateDiscussionActionState();
    void createDiscussionDialog();
    void postDiscussionComment();
    void submitDiscussionEventToInbox(int number, const DiscussionEvent &ev,
                                      const QString &titleIfNew = QString());
    QUrl discussionsApiUrl(const RepositoryRecord &repo) const;
    void syncDiscussionsInbox();
    void drainDiscussionsInboxFor(RepositoryRecord repo, bool interactive);
    void setDiscussionInlineNotice(const QString &message, bool isError = false);

    // Pull requests tab (cross-node, patch-based) with explorer + diff viewer.
    QWidget *buildPullsTab();
    PullStore pullStoreForCurrentRepo() const;
    void reloadPulls();
    void refreshPullList();
    void showPull(int number);
    void renderPullReviewSummary(const PullRequest &pr);
    void renderPullDiff(const QString &filePath);
    void adjustDiffFont(int delta); // +/- diff text-size zoom
    // Step the Files-changed view through every change: first the open file's
    // hunks, then on to the next/previous file. delta is +1 (next) or -1 (prev).
    void pullSelectAdjacentChange(int delta);
    // Scroll the pull diff to the next/previous hunk header; returns false when
    // there is no further hunk in that direction (so the caller can move files).
    bool pullScrollToAdjacentHunk(int delta, bool fromEnd = false);
    // Handle a click on a diff line-number anchor ("cmt:<side>:<line>"): prompt
    // for a comment and attach it to that line of the current PR file.
    void onPullDiffAnchorClicked(const QUrl &url);
    void submitPullThreadReply(const QString &threadId);
    void setPullThreadState(const QString &threadId, const QString &state);
    void renderPullThread(const PullRequest &pr);   // review/comment conversation
    void renderPullCommits(const PullRequest &pr);  // commits that make up the PR
    void renderPullChecks(const PullRequest &pr);   // action runs for the PR's commits
    void renderPullChecksSummary(const PullRequest &pr); // inline conversation card
    void showPullCheckLog(int runId);               // load a run's log into the panel
    QStringList pullCommitShas(const PullRequest &pr) const; // base..head SHAs
    QList<int> runIdsForPull(const PullRequest &pr) const;   // matching action runs
    void runChecksForCurrentPull();                 // enqueue workflows at PR head
    void updatePullSubTabCounts(const PullRequest &pr);
    void refreshOpenPullChecks();                   // re-render checks for the open PR
    // Enqueue every push-triggered workflow found at `commit` for owner/name.
    // Shared by the push handler and the PR "Run checks" button.
    void queueWorkflowsForCommit(int repoIndex, const QString &owner,
                                 const QString &name, const QString &commit,
                                 const QString &ref);
    void submitPullComment();                       // post a comment on the PR
    void sendPullRevisionToAgent();                 // re-queue the linked agent with revision feedback
    void submitPullReview(const QString &state);    // approve / request changes
    void promptNewPull();
    void importPatchAsPull(); // read a .patch/.diff file and open it as a PR
    void promptNewPullFromDirectory();
    void promptNewPullFromSource(const QString &sourceDir,
                                 const QString &preferredBase = QString(),
                                 const QString &preferredHead = QString());
    void switchToPullTab(int pullNumber);
    void updateCurrentPullBranch();
    void mergeCurrentPull();
    void resolveCurrentPullConflicts(); // open the per-conflict merge editor
    // Shared modal merge-conflict editor (used by the PR and branch merge flows).
    // Returns true if the user committed the resolution, false if they cancelled.
    bool runMergeConflictEditor(const QString &title, const QString &introHtml,
                                const QString &workTree,
                                const QStringList &conflictedFiles,
                                const QString &commitButtonText,
                                const std::function<bool(QString *)> &commitFn);
    // Auto-resolve this PR's conflicts with a low-cost model (Claude/OpenAI) and
    // commit the fix to the PR's own branch (no new PR). The work is surfaced as a
    // live agent session so the user can watch it. provider is "claude" | "openai".
    void fixCurrentPullConflictsWithAi(const QString &provider);
    void editCurrentPullFile();         // edit the selected file on the PR's branch
    void deleteCurrentPullFile();       // delete the selected file on the PR's branch
    void closeIssuesLinkedFromPull(const PullRequest &pr);
    // After a merge, mint the escrow deposit address and show the funding QR for
    // any (pledged-but-unpaid) bounty on the issues this PR closes. Bounties are
    // added to issues without paying up front; merge is when they get funded.
    void fundBountiesForMergedPull(const PullRequest &pr);
    // Poll a bounty escrow after merge; once funded the worker splits it to the
    // author + treasury, and this records the paid state on the issue.
    void pollBountyPayout(const RepositoryRecord &repo, int number, double amount);
    QList<int> issuesLinkedFromPull(const PullRequest &pr) const;
    // Pull requests linked to an issue: PRs whose references (closes/fixes/issue
    // #N, agent-created, or an explicit "Linked issue #N" note) point at this
    // issue, plus "Linked pull request #M" notes in the issue's own thread.
    QList<int> pullsLinkedToIssue(int issueNumber) const;
    // Prompt for the other side and cross-post a link note on both threads.
    void linkPullToIssueFromIssuePage(); // issue page: pick a PR to link
    void linkIssueToPullFromPullPage();  // PR page: pick an issue to link
    // Post a plain link note to an issue / PR, writing directly when this node
    // owns the repo and falling back to the maintainer's relay inbox otherwise.
    void postIssueLinkComment(int issueNumber, const QString &body);
    void postPullLinkComment(int pullNumber, const QString &body);
    void closeCurrentPull();
    void reopenCurrentPull();
    void deleteCurrentPull();
    void deleteCurrentPullAndBranch();
    // Merge the PR, then delete it and its head branch in one confirmed step
    // (issue #261). The merge runs first and synchronously; only if it succeeds
    // do we drop the PR record and its branch.
    void mergeAndDeleteCurrentPull();
    // Shared worker: delete a PR record and (best-effort) remove its local head
    // branch, reporting through the repo-detail notice. The caller owns the
    // confirmation (and any prior merge); pass propagate=true to push the result
    // to the mirror (the merge-and-delete flow needs the merge to reach peers).
    void deletePullAndBranchAsync(int number, const QString &head, bool haveBranch,
                                  bool rewriteHistory, bool propagate);
    void setPullDeleteButtonsEnabled(bool enabled);
    // Confirm a PR deletion; *rewriteHistory is set from an opt-in checkbox
    // (off by default — a plain delete is fast and leaves history intact).
    bool confirmPullDeletion(const QString &prompt, bool *rewriteHistory);
    void syncPullsInbox();
    void submitPullToInbox(const PullRequest &pr);
    void submitPullToInbox(const PullRequest &pr, const RepositoryRecord &targetRepo);
    // Submit a signed PR conversation event (comment/review) to the relay inbox
    // for repos this node can't write directly.
    void submitPullEventToInbox(int number, const PullEvent &ev);
    // Submit a signed commit comment to the relay inbox.
    void submitCommitCommentToInbox(const QString &sha, const CommitComment &c);
    // Drain a repo's commit-comment inbox (owner-only); apply + commit.
    void drainCommitInboxFor(RepositoryRecord repo, bool interactive);
    QUrl commitsApiUrl(const RepositoryRecord &repo) const;
    void updatePullActionState();
    QUrl pullsApiUrl(const RepositoryRecord &repo) const;
    // Agent sessions tab: local OpenAI API / Claude API runs assigned from issues.
    QWidget *buildAgentsTab();
    void initAgents();
    void reloadAgents();
    void refreshAgentTable();
    void updateAgentTokenCell(int sessionId);  // live Tokens-column update
    void updateAgentStatusCell(int sessionId); // in-place Status-column update
    void showAgentSession(int sessionId);
    // Parse "==> [net]" markers from a session log into the traffic graphic.
    void updateAgentNetworkPanel(const QString &log, const QString &status);
    AgentSession *findAgentSession(int sessionId);
    const AgentSession *latestAgentSessionForIssue(int issueNumber) const;
    const AgentSession *agentSessionForPull(int prNumber) const;
    void assignIssueToAgent(const QString &provider);
    void continueSelectedAgentSession();
    void deleteSelectedAgentSession();
    void testOpenAiAgentKey();
    void refreshClaudeSpend();
    // Issue #115: persist and restore month-to-date spend so the figures are
    // shown on restart instead of waiting for a fresh API refresh.
    void cacheSpendLabel(const QString &textKey, const QString &tsKey,
                         const QString &text);
    void applyCachedSpendLabels();
    // Track and display time remaining in each provider's 5-hour and weekly
    // usage windows, anchored at first agent activity and persisted to settings.
    void markAgentLimitWindow(const QString &provider);
    void refreshAgentLimitLabel();
    void openAgentSessionFromIssue();
    void switchToAgentsTab(int sessionId);
    void processAgentQueue();
    // Returns the pooled runner currently executing sessionId, or nullptr.
    AgentRunner *runnerForSession(int sessionId) const;
    // Returns an idle pooled runner, creating (and wiring) a new one if needed.
    AgentRunner *acquireAgentRunner();
    // True while any pooled runner is executing a session.
    bool anyAgentRunning() const;
    void onAgentLog(int sessionId, const QString &text);
    void onAgentStatusChanged(int sessionId, const QString &status);
    void onAgentFinished(int sessionId, bool ok);
    // The agent CLI needs the user to act (e.g. a bad API key); surface it.
    void onAgentNeedsAttention(int sessionId, const QString &message);
    void updateAgentActionState();
    void updateIssueAgentUi(const Issue &issue);
    // IDE extension integration (see ide_extension/). Detection polls the
    // extension's heartbeat file; startIssueInIde drops it a task request.
    bool ideExtensionActive(QString *ideName = nullptr) const;
    bool ideIntegrationReady(QString *ideName = nullptr) const;
    void startIssueInIde(int issueNumber, const QString &title,
                         const QString &provider);
    void updateIssueIdeButtons();
    AgentRunner::Config agentConfigForProvider(const QString &provider) const;
    // Run the Claude Code CLI interactively in the embedded terminal for a
    // session (instead of the headless runner).
    void startClaudeCodeTerminal(AgentSession &session, const Issue &issue,
                                 const QString &repoPath);
    QString agentProviderName(const QString &provider) const;
    // Actions (CI on push to the mirror) — lives as a tab inside the repo detail.
    QWidget *buildRepoActionsTab();
    void refreshRepoActions();           // workflows column + runs for the open repo
    QList<ActionWorkflow> availableWorkflowsForRepo(
        const RepositoryRecord &repo) const;
    // Show/populate the manual-run bar for the selected workflow (branches from
    // the repo's mirror, default "main"); hidden unless it allows manual runs.
    void updateManualRunBar();
    // Queue a manual run of the selected workflow on the chosen branch.
    void runSelectedWorkflowManually();
    // Re-queue the currently selected run (same workflow, commit and ref).
    void rerunSelectedRun();
    void initActions();                  // store/runner/watcher, load history, hooks
    void ensurePushHook(const RepositoryRecord &repo) const;
    void removePushHook(const RepositoryRecord &repo) const;
    void installAllPushHooks() const;
    void scanActionSpool();              // read *.push events, enqueue runs
    void enqueuePushEvent(const QString &owner, const QString &name,
                          const QString &commit, const QString &ref);
    void processActionQueue();
    void onRunLog(int runId, const QString &text);
    void notifyActionEvent(const QString &title, const QString &body,
                           bool warning); // tray alert gated by the run-alert setting
    void addNotification(const QString &title, const QString &body,
                         bool warning = false, int runId = -1);
    void showNotifications();
    // Notifications live in their own top-level section: a sortable table
    // (buildNotificationsSection is declared with the other section builders).
    void refreshNotificationsTable();
    void updateNotificationButton();
    // Show/hide the small top-bar rebuild+restart button per the opt-in setting.
    void updateNavRebuildButton();
    int pendingActionCount() const;
    void openActionRunFromNotification(int runId);
    // Show a desktop notification with both a title and body, using notify-send
    // when available (reliable on Linux) and falling back to the tray icon.
    // `icon` is a freedesktop icon name (e.g. "emblem-default").
    void postNotification(const QString &title, const QString &body,
                          bool warning = false, const QString &icon = QString());
    void onRunStatusChanged(int runId, const QString &status);
    void onRunFinished(int runId, bool ok);
    void refreshActionsTable();
    void showLatestVisibleActionRun();
    void showRun(int runId);
    void approveSelectedRun();
    void rejectSelectedRun();
    ActionRun *findRun(int runId);
    int repoIndexFor(const QString &owner, const QString &name) const;
    // Settings: global variables/secrets editor.
    void reloadVariablesTable();
    void addOrEditVariable();
    void deleteSelectedVariable();
    void toggleVariablesRevealed();
    void persistVariablesFromTable();

    void openRepoDetail(int repoIndex);
    // Open a repo from the top-bar switcher: paint a spinner, then run the heavy
    // (synchronous) load on the next event-loop turn so the menu closes snappily.
    void openRepoDetailDeferred(int repoIndex);
    // Blank the repo-detail panel when the selected node has no repositories.
    void clearRepoDetail();
    void openRepositoryWebsite(); // open the current repo's page in the browser
    void forkCurrentRepo();       // clone the open repo into your own node
    void downloadCurrentRepoZip();
    void setRepoDetailNotice(const QString &message, bool error = false);
    void refreshOpenRepoDetail(); // re-read the open repo after its mirror changes
    void updateRepoCodeSize();
    void updateRepoCommitCount();
    void updateRepoIssueCount();
    void updateRepoDiscussionCount();
    void updateRepoPullCount();
    void loadRepoOverview(const QString &path);
    void showRepoOverview();
    void showRepoEditor();
    void loadRepoFileTree();
    void openRepoFile(const QString &path);
    void openRepoReadme(); // open the repo's README in a file tab (default view)
    void updateRepoFileSaveActions();
    void saveCurrentRepoFile(bool createPull);
    bool saveRepoFileEdit(const QString &path, const QString &content, bool createPull);
    // True when the open repo can receive a proposed change as a pull request even
    // without a local working tree: a node mirroring someone else's repo builds the
    // commit in a throwaway worktree off its bare mirror and sends the signed patch
    // to the owner's inbox. False for preview-only or empty repos.
    bool repoCanProposePull() const;
    // No-working-tree path for "Save as PR": commit the edit in a temporary worktree
    // off the bare mirror, then submit the resulting signed patch to the owner's inbox.
    bool proposePullFromMirrorEdit(const QString &cleanPath, const QString &content);
    void loadRepoInfo();
    void loadBranchesAndTags();
    QStringList repoBranches() const;
    QString repoDefaultBranch(const QStringList &branches) const;
    QWidget *buildBranchesTab();
    void loadBranchesPanel();
    QWidget *buildWorktreesTab();
    void loadWorktreesPanel();
    // Open the Worktrees tab and select the row for a branch (used by the
    // clickable branch link in the agent session header — issue #265).
    void switchToWorktree(const QString &branch);
    void showWorktreeDiff(const QString &branch, const QString &worktreePath);
    // Merge a worktree's branch into the default branch. On success the now-merged
    // worktree is removed (its work is in main); pass its folder so it can be.
    void mergeWorktreeIntoMain(const QString &branch,
                               const QString &worktreePath = QString());
    // Merge the default branch into a worktree's branch, run inside that worktree,
    // so it picks up the latest from main without leaving its folder.
    void updateWorktreeFromMain(const QString &worktreePath, const QString &branch);
    // Remove a worktree's folder (git worktree remove --force). confirm=true asks
    // first; the post-merge cleanup calls it silently. alsoDeleteBranch deletes the
    // now-orphaned branch too (the default for the Worktrees-tab "Remove" action);
    // the post-merge cleanup passes false so the just-merged branch stays visible.
    void removeWorktree(const QString &worktreePath, const QString &branch,
                        bool confirm, bool alsoDeleteBranch = true);
    void showBranchDiff(const QString &branch);
    void createPullFromBranch(const QString &branch);
    void onBranchDiffAnchorClicked(const QUrl &url);
    void updateBranchDiffSticky();
    QString diffViewedScope(const QString &context) const;
    QSet<QString> loadDiffViewed(const QString &context) const;
    void setDiffViewed(const QString &context, const QString &path, bool viewed);
    // Merge the default branch into `branch` so it catches up with main.
    void updateBranchFromBase(const QString &branch);
    // Merge the default branch into every branch that's behind it in one pass;
    // clean merges land via plumbing (no checkout), conflicts are reported so the
    // list can surface them and offer "Fix with agent".
    void pullBaseIntoAllBranches();
    // Merge the default branch into `branch` and have a low-cost model resolve any
    // conflicts, committing the merge onto the branch (watched on the Agents tab).
    void fixBranchConflictsWithAgent(const QString &branch, const QString &provider);
    void promptNewBranch();
    void deleteBranch(const QString &branch);
    void deleteSelectedBranches();
    QWidget *buildReleasesTab();
    void loadReleasesPanel();
    void promptNewRelease();
    QWidget *buildMirrorNodesTab();
    // Per-repo Settings tab: visibility (public/private) and repository deletion.
    QWidget *buildRepoSettingsTab();
    void refreshRepoSettings(); // sync the Settings controls to the open repo

    // --- Coves: encrypted, password-shared file vaults inside a repo ----------
    // A cove is .forkmesh/coves/<slug>.cove (AES-256-GCM, see CoveStore). The team
    // shares one password out-of-band; entered in repo or global Settings, it
    // unlocks matching coves. Opening a cove logs it locally and (if the creator
    // asked) sends a best-effort live alert back to the creator's node.
    CoveStore coveStoreForRepo(int repoIndex) const;
    QString coveRepoSettingsPrefix(int repoIndex) const; // QSettings key prefix
    QString rememberedCovePassword(int repoIndex) const; // repo pw, else global
    bool coveAutoOpenEnabled(int repoIndex) const;       // repo OR global toggle
    // Try the session cache, then the remembered repo/global passwords. On success
    // fills cove (documents/accessLog), caches the working password, and returns it.
    bool tryUnlockCove(Cove &cove, int repoIndex, QString *passwordOut) const;
    void logCoveAccessLocal(const Cove &cove);           // "log yourself" — local
    QStringList coveAccessLogLocal(const QString &coveId) const;
    void announceCoveOpened(const Cove &cove);           // best-effort live alert
    void openCove(const QString &relPath);               // unlock + show a viewer
    void openCoveViewer(int repoIndex, Cove cove, const QString &password);
    void promptCreateCove(int repoIndex);
    void rebuildRepoCovesList();                         // repo Settings list
    QWidget *buildCoveSection();                         // repo Settings "Coves"
    QWidget *buildCoveGlobalSection();                   // global Settings "Coves"
    void applyCovePasswordFromSettings(int repoIndex, bool global,
                                       const QString &password, bool remember);
    static QByteArray coveOpenCanonical(const QString &coveId, const QString &creatorKey,
                                        const QString &openerKey, qint64 ts);
    // Apply an edited source/fork URL to the open repo: persist it and repoint
    // the bare mirror's origin remote so the next sync fetches from it.
    void updateRepoSource();
    // Set "run actions on push" for the open repo and keep both toggles in sync.
    void setRepoActionsEnabled(bool on);
    void loadMirrorNodesPanel();
    // Fetch the worker's catalog mirror list for a repo group so the owner sees
    // every published mirror, not just nodes live in the chat room (issue #223).
    void fetchCatalogMirrors(const QString &owner, const QString &repo,
                             const QString &source);
    void deleteTag(const QString &tag);
    bool repoHasWorkingTree() const;
    void loadFileSearchIndex();
    void loadAboutSidebar();
    // --- Top-bar global search ("search everything"). One box that, as you type,
    // searches across sections, every relay/node/repository, and (for the open
    // repo) its issues, pull requests, branches, files and commit messages, then
    // navigates straight to whatever result you pick.
    QWidget *createGlobalSearchBox();          // build the box + results popup
    void rebuildGlobalSearchResults();         // (re)populate the dropdown (debounced)
    void positionGlobalSearchPopup();          // anchor the dropdown under the box
    void moveGlobalSearchSelection(int delta); // keyboard up/down through results
    void activateGlobalSearchItem(QListWidgetItem *item); // navigate to a result
    void hideGlobalSearchPopup();
    // Full-page deep search opened by pressing Enter in the search box.
    QWidget *buildSearchResultsSection();
    void openSearchResultsPage(const QString &query);
    void startSearchProcess(const QString &dir, const QStringList &gitArgs,
                            QTreeWidgetItem *bucket, int cat);
    void onSearchResultActivated(QTreeWidgetItem *item, int column);
    void stopSearch();
    void updateSearchStatus();
    void editRepoAbout();
    bool saveRepoAboutMetadata(const QString &about, const QString &websiteInput,
                               QString *error = nullptr);
    void loadCommits();
    // Infinite scroll: when the list is scrolled to the bottom and more history
    // exists, deepen the window (m_commitsLimit) and rebuild, preserving scroll.
    void loadMoreCommits();
    // Compose a release-notes message and a (<=280 char) X/Twitter post from the
    // commits the user has multi-selected, shown in a copyable dialog.
    void generatePostFromSelectedCommits();
    // --- Source Control panel (working-tree changes) at the top of the Commits
    // tab: stage/unstage/discard/commit, view per-file diffs, and draft the
    // commit message (or an X post) with Claude/OpenAI.
    QWidget *buildSourceControlPanel();
    void refreshSourceControl();             // re-scan `git status` into the tree
    void scmStagePath(const QString &path);
    void scmUnstagePath(const QString &path);
    void scmDiscardPath(const QString &path, bool untracked);
    void scmStageAll();
    void scmUnstageAll();
    void scmDiscardAll();
    void scmCommit();
    // Commit the staged changes, then publish/push them (see pushCurrentRepoUpstream).
    void scmCommitAndPush();
    // One click: stage every change (git add -A), commit, then publish/push.
    void scmStageAllCommitAndPush();
    // Shared commit body for both buttons above. Returns true once a commit lands
    // so "Commit & push" only pushes after a successful commit.
    bool performScmCommit();
    void showScmDiff(const QString &path, bool staged, bool untracked);
    // "Open Changes" for a whole group: a combined diff of every staged (or every
    // unstaged + untracked) file, rendered in the same diff pane as a single file.
    void showScmDiffAll(bool staged);
    // Walk the working-tree changes with the up/down buttons: step through the
    // open file's hunks first and only move to the next (+1) / previous (-1)
    // changed file once past the last/first hunk.
    void scmSelectAdjacentChange(int delta);
    // Scroll the changes diff to the next (+1) / previous (-1) hunk. With fromEnd
    // the search starts at the bottom (used when entering a file from below).
    // Returns false when there's no further hunk in that direction.
    bool scmScrollToAdjacentHunk(int delta, bool fromEnd = false);
    // The unified working-tree diff used as context for AI generation (staged if
    // anything is staged, else all unstaged+untracked changes), capped for cost.
    QString scmContextDiff() const;
    // The `git diff` scope argument(s) selecting the same change set scmContextDiff()
    // describes: a window's base commit, the staged set, or all unstaged+untracked
    // edits. An empty list means plain `git diff`.
    QStringList scmDiffScopeArgs() const;
    // Draft a Conventional-Commits-style message from the change set's structure
    // alone — file paths, add/delete counts, statuses, the branch name and the
    // function names git records in hunk headers — with no model and no network.
    // A best-effort first draft for the on-device option in the model picker.
    // variant 0 is the deterministic best draft; higher values rotate symbol
    // choice and verb phrasing so repeated Generate clicks offer alternatives.
    QString scmHeuristicCommitMessage(int variant = 0) const;
    // When the on-device engine is selected, fill the (empty) message field from
    // the current changes automatically. No-op for the paid AI models, while a
    // message is being generated, or once the user has typed something.
    void autoFillScmMessage();
    // Draft the commit message (or an X post) inline from the changes using the
    // model picked in the compose bar — no dialog, the result lands in the
    // message box and the cost shows beside it. The "On-device (no AI)" pick
    // routes to scmHeuristicCommitMessage() instead of an API call.
    void generateScmMessage();
    // True when the commit table already shows the current branch's current tip,
    // so a tab click can skip the expensive rebuild. Does one cheap `git
    // rev-parse` to catch tips moved out from under us (e.g. by a background
    // agent committing into the same working tree).
    bool commitsListIsCurrent();
    // The served mirror's tip for the branch currently browsed in the repo
    // detail (or empty when there's no mirror). The "waiting to sync" markers
    // hang off this, so the commit list must reload whenever it moves.
    QString currentMirrorTip() const;
    // If the commit list is on screen but stale (local tip or mirror tip moved),
    // rebuild it so the "waiting to sync" markers stay correct without a manual
    // tab switch. Cheap no-op when the list isn't visible or is already current.
    void refreshCommitMarkersIfStale();
    // Full hashes of commits in the local working copy that the network mirror
    // doesn't have yet (i.e. ahead of the mirror, not yet synced). Empty for a
    // browse-only mirror, which only ever pulls.
    QSet<QString> unpushedCommitHashes() const;
    // Scan recent commit messages for closing keywords ("closes #12", "fixes
    // #3", "resolves #7") and close + annotate the referenced issues. Idempotent.
    void applyCommitIssueClosures();
    // Aggregated action/check status of a commit in the current repo:
    // 0 none, 1 success, 2 failed, 3 running. `sha` may be short or full.
    int commitStatusCode(const QString &sha) const;
    // The same as HTML (coloured check / x / running dot) for rich-text labels.
    QString commitStatusGlyph(const QString &sha) const;
    // Spin the Actions tab label while a run for the open repo is active.
    void updateActionsTabIndicator();
    // Lazily build the floating strip and place it just above the Actions tab.
    void ensureActionStrip();
    void positionActionStrip();  // grow each bar by its run's elapsed time
    void positionRepoPushButton(); // float "Sync changes" just above the Commits tab
    void updateActionStrip();    // build/show/hide the bars for in-flight runs
    // Spin the Agents tab label while any agent session is running.
    void updateAgentsTabIndicator();
    // Lazily build the spinner overlay and place it just above the Agents tab.
    void ensureAgentSpinnerOverlay();
    void positionAgentSpinnerOverlay();
    void positionAgentSnake();
    // Re-render commit check glyphs in whichever repo-detail tab is visible.
    void refreshCommitStatusGlyphs();
    void refreshRepoSecurity();
    void loadRepoInsights();
    // Insights "Contributors & activity": right-click a row to reassign that
    // author's commits to a different identity (rewrites history via
    // git filter-branch) to fix attribution.
    void showInsightsContributorMenu(const QPoint &pos);
    void reassignContributorIdentity(const QString &oldName);
    void setRepoBranch(const QString &branch);
    QString repoHeadBranch() const;          // the checked-out branch (HEAD)
    void refreshCommitsBranchButton();       // commits-page branch indicator/menu
    void checkoutRepoBranch(const QString &branch); // guarded real checkout
    void createAndCheckoutBranch();          // "Create new branch…"
    QString currentRef() const;
    QString repoGitDir() const;
    QString iconsDir() const;
    QIcon iconForFile(const QString &fileName) const;
    QIcon iconForDir(bool opened) const;
    void startRefreshSpin();
    void stopRefreshSpin();
    // Busy feedback for the commits-page Refresh button: rotates its icon while a
    // reload runs (kept visible briefly after, since the reload is near-instant).
    void startCommitsRefreshSpin();
    void stopCommitsRefreshSpin();
    // Generic click feedback for any Refresh button: briefly spins its icon, then
    // restores it. addRefreshSpin wires it onto a button's clicked signal.
    void spinRefreshButton(QPushButton *button);
    void addRefreshSpin(QPushButton *button);
    // Busy feedback for switching nodes in the top nav: the node button shows a
    // spinner and the heavy repo load reports each step to the log. nodeSwitchStep
    // logs the step and, mid-switch, yields the event loop so the spinner animates.
    void startNodeSwitchSpin();
    void stopNodeSwitchSpin();
    void startRepoSwitchSpin();
    void stopRepoSwitchSpin();
    void nodeSwitchStep(const QString &what);
    // Live, visible progress for a repo/node load: a blue pill in the top bar
    // (where the breadcrumb is) naming the current step, e.g. "Loading commit
    // history…". Persistent until the next showLoadStatus / flashMessage clears it.
    void showLoadStatus(const QString &what);

    // If this node owns a writable working-tree copy of the same logical repo as
    // `repo` (same owner/name), returns that record; else returns `repo`. Lets the
    // source-of-truth author issues/PRs even when a read-only preview of their own
    // repo is the one currently selected.
    const RepositoryRecord &writableRecordFor(const RepositoryRecord &repo) const;

    // Issues tab
    int issuesRepoIndex() const;                 // selected repo, or -1
    IssueStore issueStoreForCurrentRepo() const; // build a store for that repo
    void refreshIssuesRepoCombo();
    void reloadIssues();        // load issues + label/milestone filters from the store
    void refreshIssueList();    // apply filters into the list widget
    void resetIssueFilters();   // clear status/label/milestone/search filters
    // Switch the issues list stack (0 Issues, 1 Milestones, 2 Labels, 3 Board)
    // and toggle the filter controls, which only apply to the Issues table/board.
    void selectIssueListTab(int id);
    void refreshIssueMilestones();
    void refreshIssueLabels();
    // Kanban board view of the issues (list stack index 3). Each column is a
    // status; a card's column is encoded by a reserved "status:<name>" label and
    // the final "Done" column maps to the issue's closed status. Columns are
    // customizable per repo (stored in QSettings) — see boardColumns().
    QWidget *buildIssueBoard();
    void refreshIssueBoard();    // rebuild the columns/cards from m_currentIssues
    QStringList boardColumns() const;             // configured column names
    void setBoardColumns(const QStringList &cols); // persist + rebuild
    void editBoardColumns();                      // prompt to edit the column list
    // The column an issue currently belongs to (closed -> the Done column; else
    // the column named by its "status:<name>" label, defaulting to the first).
    QString issueBoardColumn(const Issue &issue) const;
    // Move an issue to a board column: rewrites its status label (and, for the
    // Done column, closes/reopens the issue), then syncs and reloads.
    void moveIssueToColumn(int number, const QString &column);
    void editIssueLabelDefinition(int row);
    QWidget *makeIssueRow(const Issue &issue,
                          const QHash<QString, QString> &labelColors) const;
    void showIssue(int number); // render the selected issue's thread
    void renderIssueThread(const Issue &issue);
    void showIssueBurnupChart();
    void showIssueComposePage(QWidget *page);
    void removeIssueComposePage();
    void setIssueInlineNotice(const QString &message, bool error = false);
    void promptEditIssueTitle();
    void saveIssueTitleEdit();
    void cancelIssueTitleEdit();
    void promptNewIssue();
    void quickAddIssue();
    // Pop a QR + address dialog for donating directly to the ForkMesh treasury.
    void showTreasuryDonateDialog();
    void copyIssueToClipboard();
    void copyIssueThreadToClipboard();
    void askAiForCurrentIssue();
    // Animated "AI is answering…" card shown in the issue thread while the
    // OpenAI request is in flight.
    void showIssueAiTyping();
    void hideIssueAiTyping();
    int availableCredits() const;     // 1 voting credit per hour online
    void voteOnCurrentIssue();
    void submitIssueVoteToInbox();
    void updateVoteUi();
    void addIssueComment();
    // Post the composer's comment and close the issue in one action (owner-only).
    void closeIssueWithComment();
    void attachIssueImage();
    void queueIssueAttachment(const QString &path); // dedupe + reference + count
    void toggleIssueStatus();
    void deleteCurrentIssue();
    // Runs the destructive history-rewriting delete on a worker thread so the UI
    // stays responsive while git filter-branch/gc churn.
    void deleteCurrentIssueWithHistory(int number);
    void editIssueLabels();
    void editIssueMilestone();
    void editIssuePriority();
    // Quick one-click priority nudge in the issue detail sidebar. direction < 0
    // raises priority (toward 1, highest); direction > 0 lowers it (toward 99,
    // lowest). Each step moves by a quarter of the 1..99 priority span.
    void nudgeIssuePriority(int direction);
    void nudgeIssueProgress(int deltaPercent);
    void editIssueProgress();
    // Drag-to-set on the Progress column of the issue list. Filtering the table
    // viewport's mouse events lets a press/drag over a progress cell paint a new
    // value live, committing it to the store (and reloading) on release.
    bool handleIssueProgressDrag(QMouseEvent *ev);
    void applyIssueProgressDragAt(const QPoint &pos);
    void commitIssueProgressDrag();
    // Row of the issue list currently being progress-dragged, or -1 when idle.
    int m_issueProgressDragRow = -1;
    void editIssueBounty();
    // Bulk-pledge the same bounty (USD) on every open issue in the current repo.
    void bountyAllOpenIssues(double amountUsd);
    // One-shot triage: give every open issue with no priority an MVP/Phase-2
    // label and an initial priority (votes/age heuristic), and estimate each
    // issue's progress from whether the work landed (closed / merged PR / agent).
    void reprioritizeBacklog();
    // Estimate how done an issue is from repo state (0..100): closed or covered
    // by a merged PR -> 100; an agent produced/started work -> partial.
    int estimateIssueProgress(const Issue &issue,
                              const QSet<int> &mergedIssues) const;
    // Rough USD estimate of having an OpenAI coding agent implement an issue,
    // derived from its text length and the OpenAI token price.
    static double openAiEstimateUsd(const Issue &issue);
    void editIssueAssignees();
    // Gear next to "Assignees": pops a searchable, checkable dropdown of every
    // known mesh node (plus any hand-typed assignee) so they can be ticked on or
    // off. Persists via saveIssueAssigneesInline on close. editIssueAssignees
    // still backs the inline "Assign yourself" text path.
    void pickIssueAssignees();
    void saveIssueLabelsInline();
    void saveIssueMilestoneInline();
    void saveIssuePriorityInline();
    void saveIssueAssigneesInline();
    void cancelIssueSidebarEditors();
    void updateIssueActionState();
    QUrl issuesApiUrl(const RepositoryRecord &repo) const;
    QUrl bountyApiUrl(const RepositoryRecord &repo) const;
    // Private-repo collaborator ACL (issue #9): share/unshare a private repo with
    // other accounts and list current collaborators.
    QUrl sharesApiUrl(const RepositoryRecord &repo) const;
    void shareRepoRequest(const RepositoryRecord &repo, const QString &grantee,
                          const QString &action);
    void addRepoCollaborator(const QString &nameRaw);
    void removeRepoCollaborator(const QString &nameRaw);
    void refreshRepoCollaborators();
    // Pop up a modal with a Solana QR + address so a funder can pay a bounty,
    // showing the exact SOL to send and polling for the deposit like the signup
    // flow. On confirmation the worker splits the escrow 90% author / 10%
    // treasury; the dialog records the paid state on the issue.
    void showBountyQrDialog(const RepositoryRecord &repo, int number,
                            const QString &uri, const QString &address,
                            double amountUsd, const QString &amountSol);
    void submitIssueCommentToInbox(const QString &body);
    // Mirror node path: send a signed new-issue ("open") event to the source of
    // truth's inbox. Returns false only when there is no repo to target.
    bool submitNewIssueToInbox(const QString &title, const QString &body,
                               const QStringList &labels, const QString &milestone,
                               int priority, const QStringList &assignees);
    void syncIssuesInbox();
    // Drain one repo's issue/pull inbox (owner-only). `interactive` shows inline
    // notices/dialogs (manual "Sync inbox"); when false it's a silent background
    // poll that only speaks up (notification + list refresh) when something new
    // arrives. Repo is taken by value so the async reply can't dangle.
    void drainIssuesInboxFor(RepositoryRecord repo, bool interactive);
    void drainPullsInboxFor(RepositoryRecord repo, bool interactive);
    // Notify the local user when they're @mentioned in one of this repo's issues
    // or pull requests. Each node scans its own synced copy, so the mentioned
    // user's node is what alerts them. Deduped and seeded via QSettings so we
    // never repeat an alert or backfill a freshly-cloned repo's history.
    void scanRepoMentionsFor(const RepositoryRecord &repo);
    // Periodically pull every owned repo's inboxes so the source of truth picks
    // up issues/PRs/comments filed by other nodes without a manual sync.
    void pollOwnedInboxes();
    void chooseAvatar();
    void setSettingsAvatar(const QByteArray &pngData);
    // Effective avatar bytes: the uploaded/generated one, or a deterministic
    // generated identicon when the user hasn't set one.
    QByteArray effectiveAvatar();
    void updateAvatarButton();
    void refreshIssueComposerAvatar();
    void logout();
    // Erase every trace of ForkMesh from this computer (data, settings, desktop
    // integration and the program files) after confirmation, then quit.
    void uninstallForkMesh();
    void rebuildAndRelaunch();
    void showSection(int index);
    void updateHomeStats();
    QString selfNodeStats() const; // inline stats line for your own node
    void attachBackend(ChatBackend *backend);
    void leaveSession(const QString &reason = QString());

    void onMessage(const ChatMessage &message);
    void onReaction(const QString &conversation, const QString &messageId,
                    const QString &emoji, const QString &reactorName, bool added);
    void onMessageEdited(const QString &conversation, const QString &messageId,
                         const QString &newText);
    void onMessageDeleted(const QString &conversation, const QString &messageId);
    void promptEditMessage(const QString &messageId, const QString &currentText);
    void confirmDeleteMessage(const QString &messageId);
    // Admin moderation: delete any message (not just your own). The delete is
    // signed by this node's identity and broadcast; peers verify the signature
    // and the signer's admin status before applying.
    void confirmAdminDeleteMessage(const QString &messageId);
    void onAdminDeleteRequested(const QString &conversation, const QString &messageId,
                                const QString &adminId, const QString &adminName,
                                qint64 ts, const QString &sig);
    QString adminDeleteCanonical(const QString &conversation, const QString &messageId,
                                 const QString &adminPubkey, qint64 ts) const;
    void onAvatar(const QString &peerId, const QByteArray &pngData);
    void onTypingChanged(const QString &conversation, const QString &peerId,
                         const QString &peerName, bool active);
    void logSystem(const QString &text);
    // Render one stored "yyyy-MM-dd HH:mm:ss  message" log line into the network
    // log view as colored HTML (dim timestamp, category badge, message), emitting
    // a date-divider row first whenever the day changes. Shared by the live
    // logSystem append and the full rebuild in buildLogSection().
    void appendNetworkLogLine(const QString &storedLine);
    QString m_lastLogRenderDate; // date of the last line rendered (for dividers)
    // Compact, centered success/failure banner shown in the top bar between the
    // breadcrumb and the notifications bell. Auto-clears after a few seconds.
    void flashMessage(const QString &text, bool error = false);
    void dismissTopMessage(); // hide the top toast and its Copy / dismiss buttons
    void showFullMessageDialog(); // scrollable modal with the full (un-elided) toast
    MessageRow *addMessageRow(const ChatMessage &message);
    void rebuildConversationView();
    void scrollToBottom();
    void setChannels(const QStringList &channels);
    void setRoster(const QList<MemberInfo> &members);
    void removeChatMember(const QString &id, const QString &name);
    // A conversation key is either a channel ("#general") or a direct chat
    // ("@<peerId>").
    void switchConversation(const QString &conversation);
    void openDirectChat(const QString &peerId, const QString &peerName);
    void refreshChannelList();
    void refreshDmList();
    // Rebuild the chat's right-hand online-members column from m_homeRoster.
    void refreshChatMembers();
    void promptAddChannel();
    void sendCurrentMessage();
    void onComposerEdited(const QString &text);
    // @-mention autocomplete in the chat composer: refresh the candidate names
    // from the roster, show/hide the popup as an "@token" is typed, and replace
    // that token with the chosen "@name " on selection.
    void refreshMentionCandidates();
    void updateMentionPopup();
    void insertMention(const QString &name);
    void sendTypingState(bool active);
    void refreshTypingLabel();
    void attachFile();
    // Open a chat image attachment full-size in a lightbox dialog.
    void showChatImageDetail(const QString &fileName, const QByteArray &data);
    // Share a clipboard image in the current conversation; true if one was sent.
    bool trySendClipboardImage();
    void saveIncomingFile(const QString &fileName, const QByteArray &data);
    // Local chat history persistence (per active server/room).
    QString chatHistoryKey() const;
    QString chatHistoryPath() const;
    void saveChatHistory();
    void loadChatHistory();
    void scheduleChatSave();
    // Avatars are cached to disk per peer (keyed by node id) so they survive a
    // restart and stay visible for peers who are currently offline — otherwise
    // an avatar only lives as long as the sender keeps re-broadcasting it.
    QString avatarCachePath(const QString &peerId) const;
    void loadCachedAvatars();
    void loadRepositories();
    void saveRepositories() const;
    void refreshRepositoryList();
    // Node handles offered by the @-mention autocomplete in comment editors:
    // every node the relay knows about (connected, discovered, mirroring) plus
    // the authors of the currently loaded issues/PRs (contributors who may be
    // offline). Sorted, de-duplicated, and cheap to build from in-memory state.
    QStringList mentionCandidateNames() const;
    void promptAddRepository();
    // Create a brand-new, empty Git repository: ask for a name and parent folder,
    // run `git init`, then mirror + publish it under this node like a local repo.
    void createNewRepository();
    // Clone a remote repo (GitHub/GitLab/any https git URL) into a local working
    // copy, then add it like a local repo. An optional per-host access token
    // (Settings) authenticates the clone to dodge unauthenticated rate limits.
    void importRemoteRepository();
    // Provider-aware "-c http.extraHeader=Authorization: Basic ..." clone args
    // carrying the saved token for the URL's host, or empty when none is set.
    QStringList importAuthGitArgs(const QString &url) const;
    void previewAdvertisedRepo(const QString &ownerName);
    void mirrorAdvertisedRepo(const QString &ownerName);
    void mirrorPreviewRepository(int index);
    void syncRepository(int index, bool quiet = false);
    void autoSyncMirrors();
    // Roster-driven catch-up: when a peer advertises a commit our mirror lacks,
    // pull it immediately instead of waiting for the next auto-sync tick.
    void syncMirrorsBehindRoster();
    // After a local change to a repo (new/updated issue, PR, comment, merge),
    // push it to the bare mirror and tell peers immediately instead of waiting
    // for the 5-minute auto-sync, so counts and content converge right away.
    void propagateRepoUpdate(int index);
    // A peer announced it refreshed "owner/name" from source; notify if we
    // mirror the same repo.
    void onPeerMirrorUpdated(const QString &ownerName, const QString &peerName);
    // A peer opened an encrypted cove. If this node created it (creatorKey matches
    // our identity) and the opener's signature checks out, raise a notification.
    void onCoveOpened(const QString &creatorKey, const QString &coveId,
                      const QString &coveName, const QString &openerKey,
                      const QString &openerName, qint64 ts, const QString &signature);
    void quickRebuildRestart();
    void changeMirrorLocation();
    void changePreviewCacheLocation();
    void publishRepository(int index, bool showDialogOnError = true);
    void updateRepoActionMenus();
    void deleteCurrentMirror();
    void updateRepoDetailStatus();
    QUrl hostWsUrl(const RepositoryRecord &repo) const;
    void startRepoHosts();
    void stopRepoHosts();
    void onRequestServed(const QString &owner, const QString &name, bool clone);
    void loadRepoStats();
    void saveRepoStats() const;
    QString repositorySource(const RepositoryRecord &repo) const;
    // Fresh "-c http.extraHeader=Authorization: Basic ..." git args carrying an
    // owner-key-signed forkmesh-view-v1 token, so clone/fetch of our own private
    // repo through the mainnode passes the relay's read gate. Empty when the repo
    // is public or the source URL is not the mainnode host (never leak the token).
    QStringList viewAuthGitArgs(const RepositoryRecord &repo,
                                const QString &source) const;
    QString repositoryChannel(const RepositoryRecord &repo) const;
    QString repositoryMirrorRoot() const;
    QString repositoryPreviewRoot() const;
    QString repositoryPreviewPath(const QString &owner, const QString &name) const;
    QString repositoryNetworkCloneUrl(const QString &owner, const QString &name) const;
    QString repositoryWebUrl(const RepositoryRecord &repo) const;
    QUrl catalogApiUrl() const;
    void deleteCatalogRepository(const QString &owner, const QString &name);
    void migrateReposForProfileName(const QString &oldOwner,
                                    const QString &newOwner);
    void onProfileNameChanged(const QString &name);
    void onAvatarChosen(const QByteArray &pngData);
    void showFirewallBanner(const QString &displayCommand,
                            const QString &privilegedCommand);
    void hideFirewallBanner();
    void allowFirewall();
    void notifyIfInactive(const QString &title, const QString &body);
    QString senderColor(const QString &sender) const;

    ForkMeshIdentity m_profileIdentity;
    ChatBackend *m_backend = nullptr;
    QNetworkAccessManager *m_networkAccess;

    QStackedWidget *m_stack;
    QStackedWidget *m_sectionStack = nullptr;
    QButtonGroup *m_navGroup = nullptr;
    QSystemTrayIcon *m_trayIcon;

    // Configured mainnode relays (switched via the top-bar relay dropdown).
    QList<ServerConfig> m_servers;
    int m_activeServer = 0;
    QHash<QString, QPixmap> m_faviconCache; // host -> favicon

    // Donation nudge banner (no Solana address yet).
    QWidget *m_solanaBanner = nullptr;
    QLabel *m_solanaBannerLabel = nullptr;
    // Payout-wallet verification banner: shown when an address is set but not yet
    // verified (a small deposit proves control before payouts can be received).
    QWidget *m_walletVerifyBanner = nullptr;
    QWidget *buildWalletVerifyNotice();
    void updateWalletVerifyNotice();

    // Top breadcrumb bar (active server favicon + server > section).
    QLabel *m_breadcrumb = nullptr;
    // Top-bar relay switcher: a clickable favicon (shows that relay's nodes), a
    // "domain ▾ count" dropdown button (search/switch/add relays), and an
    // open-in-browser icon.
    QPushButton *m_relayIconButton = nullptr;
    QPushButton *m_relayMenuButton = nullptr;
    QPushButton *m_relayOpenButton = nullptr;
    // "Relay" / "Node" / "Repo" captions before each top-bar dropdown.
    QLabel *m_relayLabel = nullptr;
    QLabel *m_nodeLabel = nullptr;
    QLabel *m_repoLabel = nullptr;
    QLabel *m_navNodeName = nullptr;     // node name shown above the balance
    QLabel *m_navSolanaBalance = nullptr;
    // Super-tiny Claude Code usage chart in the top-right cluster (issue #266):
    // two horizontal bars (5-hour + weekly) sitting beside the earnings/avatar,
    // fed by rate-limit events. Held as a QWidget* and poked via static_cast,
    // since its concrete type (TokenUsageMiniChart) is private to MainWindow.cpp.
    QWidget *m_navTokenUsage = nullptr;
    // Cached balance + fiat rates so cycling the currency view reuses what we
    // already fetched instead of re-querying getBalance / the price API each
    // click (which used to rate-limit and leave the figure stuck).
    qint64 m_navSolanaLamports = -1; // last known balance, -1 = not yet fetched
    QHash<QString, QPair<double, qint64>> m_navFiatRates; // cur -> {rate, fetchedMs}
    QPushButton *m_chatButton = nullptr; // top-bar chat toggle (next to the bell)
    QLabel *m_chatUnreadBadge = nullptr; // red unread-count badge over the chat button
    // Small connection status dot painted over the top-right avatar (green
    // online / amber connecting / grey offline), replacing the old text pill.
    QLabel *m_connectionDot = nullptr;
    QString m_connectionStatusColor;      // last dot colour (skip redundant repaints)
    QLabel *m_topMessage = nullptr;       // compact centered success/failure toast
    QTimer *m_topMessageTimer = nullptr;  // auto-clears the centered toast
    QPushButton *m_topMessageCopy = nullptr; // copy-to-clipboard for error toasts
    QPushButton *m_topMessageClose = nullptr; // dismiss "x" for persistent error toasts
    QString m_topMessageRaw;              // plain text of the current toast, for copy
    bool m_topMessageElided = false;      // current toast was truncated (hover opens the full modal)
    bool m_topMessageDialogOpen = false;  // guards against stacking the full-message modal
    bool m_pinWarningActive = false;      // true while the top toast holds the integrity-pin warning

    // Setup widgets
    QLineEdit *m_nameEdit;
    QLineEdit *m_solanaEdit;
    QLabel *m_pubkeyLabel;
    QLineEdit *m_serverUrlEdit;
    QLineEdit *m_roomNameEdit;
    QLabel *m_setupError;
    QPushButton *m_updateButton;
    QLabel *m_updateStatus;
    // Active build flow's status label and button (Quick update vs. Settings
    // rebuild), so the shared build steps report to the right place.
    QLabel *m_buildStatusLabel = nullptr;
    QPushButton *m_buildButton = nullptr;
    // Live update/rebuild log: a modeless window streaming each step's command,
    // git/cmake output, and phase headers so the user sees exactly what's running.
    QDialog *m_updateLogDialog = nullptr;
    QPlainTextEdit *m_updateLog = nullptr;
    // Set while a root-launched "Update, rebuild & restart" is running so build
    // steps and the relaunch run as this non-root user. Empty = run in-process.
    QString m_updateAsUser;

    // Chat widgets
    QLabel *m_statusLine;
    QLabel *m_channelTitle;
    QLabel *m_encryptionLabel;
    QListWidget *m_channelList;
    QPushButton *m_nodeMenuButton = nullptr; // top-bar node switcher
    QString m_navSolanaBalanceAddress;
    // One row per node, populated by refreshRepositoryList and shown in the
    // node dropdown (showNodeMenu).
    struct NodeMenuEntry {
        QString name;
        QString platform;
        bool online = false;
        bool self = false;
        int repoCount = 0;
    };
    QList<NodeMenuEntry> m_nodeMenuEntries;
    QString m_selectedNode;             // node whose repos fill the repos column
    QPushButton *m_repoMenuButton = nullptr; // top-bar repo switcher
    QPushButton *m_repoViewButton = nullptr; // "Code" button on the repo header row
    QPushButton *m_settingsNavButton = nullptr; // Settings button on the repo header row
    QPushButton *m_logNavButton = nullptr; // "Log" button in the persistent top nav
    QPushButton *m_leaderboardNavButton = nullptr; // "Leaderboards" top-nav button
    QPushButton *m_navRebuildButton = nullptr; // small rebuild+restart button (opt-in)
    QWidget *m_leaderboardsContent = nullptr; // container repopulated on refresh
    QLabel *m_leaderboardsStatus = nullptr;   // loading / error / empty notice
    QHBoxLayout *m_repoHeaderLeft = nullptr; // left cluster of the repo header row
    QPushButton *m_repoPushButton = nullptr; // "Publish N" button shown above the tab bar
    QWidget *m_repoPublishBar = nullptr;     // row hosting m_repoPushButton, hidden when idle
    int m_repoPinCheckIndex = -1;            // repo index an in-flight pin check belongs to
    // One row per repo of the selected node, shown in the repo dropdown.
    struct RepoMenuEntry {
        QString label;
        QIcon icon;
        int index = -1;     // m_repositories index; -2 = advertised mirror
        QString advertised; // ownerName when index == -2
    };
    QList<RepoMenuEntry> m_repoMenuEntries;
    QListWidget *m_dmList;
    // Right-hand online-members column in the chat view: a scroll area whose
    // inner layout holds one card per online (or self) node.
    QVBoxLayout *m_chatMembersLayout = nullptr;
    QLabel *m_chatMembersHeading = nullptr;
    QWidget *m_firewallBanner;
    QLabel *m_firewallBannerLabel;
    QPushButton *m_firewallAllowButton;
    QString m_firewallPrivilegedCommand;
    QScrollArea *m_messageScroll;
    QWidget *m_messageContainer;
    QVBoxLayout *m_messageLayout; // message rows + a trailing stretch
    // True while the view is pinned to the newest message; cleared when the
    // user scrolls up to read history so new arrivals don't yank them away.
    bool m_stickToBottom = true;
    QLabel *m_typingLabel;
    QLineEdit *m_messageInput;
    // @-mention autocomplete for the composer. Driven manually (setWidget, not
    // setCompleter) so it completes the "@token" under the cursor rather than the
    // whole line; its model holds the current roster's names.
    QCompleter *m_mentionCompleter = nullptr;
    QStringListModel *m_mentionModel = nullptr;

    // Settings section widgets
    QLineEdit *m_settingsNameEdit = nullptr;
    QLineEdit *m_settingsSolanaEdit = nullptr; // #66: node Solana address in Settings
    QLabel *m_settingsAvatarPreview = nullptr;
    QPlainTextEdit *m_settingsLog = nullptr;
    QPushButton *m_rebuildButton = nullptr;
    QLabel *m_rebuildStatus = nullptr;
    QLineEdit *m_mirrorRootEdit = nullptr;
    QLineEdit *m_previewCacheRootEdit = nullptr;
    // Import-a-repo (GitHub/GitLab) controls.
    QLineEdit *m_importUrlEdit = nullptr;
    QPushButton *m_importButton = nullptr;
    QLabel *m_importStatus = nullptr;
    QCheckBox *m_autostartCheck = nullptr;
    QComboBox *m_themeCombo = nullptr;
    // Default coding-agent provider for new assignments; seeds the quick-add and
    // issue-detail provider pickers. OpenAI API | Claude API | Claude Code.
    QComboBox *m_defaultAgentProviderCombo = nullptr;
    QLineEdit *m_codexApiKeyEdit = nullptr;
    QLineEdit *m_openAiAdminKeyEdit = nullptr;
    QLineEdit *m_codexModelEdit = nullptr;
    QLineEdit *m_claudeApiKeyEdit = nullptr;
    QLineEdit *m_claudeAdminKeyEdit = nullptr;
    QLineEdit *m_codexCommandEdit = nullptr;
    QLineEdit *m_claudeCommandEdit = nullptr;
    QLineEdit *m_agentContextEdit = nullptr;
    QLineEdit *m_agentMaxOutputEdit = nullptr;
    QPlainTextEdit *m_agentPromptPreambleEdit = nullptr;
    QTimer *m_mirrorSyncTimer = nullptr;
    QTimer *m_inboxPollTimer = nullptr; // background drain of owned repo inboxes

    // Issues section widgets
    QLineEdit *m_issueSearch = nullptr;
    QComboBox *m_issuesRepoCombo = nullptr;
    QComboBox *m_issueStatusFilter = nullptr;
    QComboBox *m_issueLabelFilter = nullptr;
    QComboBox *m_issueMilestoneFilter = nullptr;
    QTableWidget *m_issueTable = nullptr;
    QStackedWidget *m_issueListStack = nullptr;
    QTableWidget *m_issueMilestonesTable = nullptr;
    QTableWidget *m_issueLabelsTable = nullptr;
    QWidget *m_issueBoard = nullptr;            // Kanban board (list stack index 3)
    QHBoxLayout *m_issueBoardColumns = nullptr; // holds one widget per board column
    QWidget *m_issueDetail = nullptr;          // collapsible detail panel
    QStackedWidget *m_issueDetailStack = nullptr;
    QWidget *m_issueComposePage = nullptr;
    QPushButton *m_issueDetailToggle = nullptr;
    QLineEdit *m_issueQuickAdd = nullptr;
    QCheckBox *m_quickAddAssignAgent = nullptr; // assign a coding agent on add
    QComboBox *m_quickAddAgentProvider = nullptr;
    QCheckBox *m_quickAddCreatePr = nullptr;    // request PR from quick-add agent
    // Centered in the footer: the git identity (name <email>) configured for the
    // repo currently open in the detail view. Updated by openRepoDetail.
    QLabel *m_footerGitIdentity = nullptr;

    // Repo detail view
    int m_repoDetailIndex = -1;
    QButtonGroup *m_issueTabGroup = nullptr; // Issues / Milestones / Labels tabs
    QButtonGroup *m_repoDetailTabs = nullptr;
    QPushButton *m_repoCodeTab = nullptr;
    QPushButton *m_repoCommitsTab = nullptr;
    QPushButton *m_repoIssuesTab = nullptr;
    QPushButton *m_repoPullsTab = nullptr;
    QPushButton *m_repoDiscussionsTab = nullptr;
    QPushButton *m_repoAgentsTab = nullptr;
    // Floating strip of slowly-spinning provider marks shown just above the
    // Agents tab while agents are busy (up to 5 visible, scroll for more).
    QWidget *m_agentSpinnerOverlay = nullptr;
    QScrollArea *m_agentSpinnerScroll = nullptr;
    QHBoxLayout *m_agentSpinnerRow = nullptr;
    QList<int> m_agentSpinnerIds; // running session ids currently shown (skip rebuilds)
    // Purple braille "snake" activity indicator overlaid on the Agents tab while
    // an agent runs. A separate label so the tab text keeps its normal colour.
    QLabel *m_agentSnake = nullptr;
    QPushButton *m_repoActionsTab = nullptr;
    // Floating strip of thin bars above the Actions tab — one per in-flight run,
    // each labelled with the workflow name and growing to the right the longer
    // its run has been going. Hidden when nothing is running.
    QWidget *m_actionStrip = nullptr;
    QVBoxLayout *m_actionStripCol = nullptr;
    QList<int> m_actionStripIds; // running run ids currently shown (skip rebuilds)
    QPushButton *m_repoMirrorsTab = nullptr; // handle for the Mirror nodes (N) badge
    QPushButton *m_repoBranchesTab = nullptr; // handle for the Branches (N) badge
    QStackedWidget *m_repoDetailStack = nullptr;
    int m_chatStackIndex = -1; // index of the Chat page in m_repoDetailStack
    int m_insightsTabIndex = -1; // index of the Insights page
    int m_branchesTabIndex = -1; // index of the Branches page
    int m_worktreesTabIndex = -1; // index of the Worktrees page (next to Branches)
    QPushButton *m_repoWorktreesTab = nullptr; // handle for the Worktrees (N) badge
    QTableWidget *m_worktreesTable = nullptr;
    QLabel *m_worktreesSummary = nullptr;
    QTextBrowser *m_worktreeDiffView = nullptr;
    QListWidget *m_worktreeFileList = nullptr;
    QLabel *m_worktreeFilesSummary = nullptr;
    QPushButton *m_worktreeMergeButton = nullptr;  // merge the selected worktree into main
    QPushButton *m_worktreeUpdateButton = nullptr; // merge main into the selected worktree
    QPushButton *m_worktreeRemoveButton = nullptr; // remove the selected worktree
    QString m_worktreeSelectedBranch;              // branch behind the open worktree detail
    QString m_worktreeSelectedPath;                // its on-disk worktree folder
    int m_releasesTabIndex = -1; // index of the Releases page
    int m_mirrorNodesTabIndex = -1; // index of the Mirror nodes page
    int m_settingsTabIndex = -1; // index of the Settings page
    QLabel *m_repoVisibilityHint = nullptr; // explains the current visibility
    QTableWidget *m_branchesTable = nullptr;
    QLabel *m_branchesSummary = nullptr;
    QPushButton *m_branchPullAllButton = nullptr; // "Pull <base> into all" header action
    QListWidget *m_branchFileList = nullptr;    // changed-files list beside the diff
    QLabel *m_branchFilesSummary = nullptr;     // "N files changed" header
    QTextBrowser *m_branchDiffView = nullptr;
    QString m_branchDiffBranch;
    QLabel *m_branchDiffSticky = nullptr;
    QList<QPair<int, QString>> m_branchDiffFileSpans;
    QPushButton *m_branchesDeleteSelBtn = nullptr;
    QTableWidget *m_releasesTable = nullptr;
    QLabel *m_releasesSummary = nullptr;
    QTableWidget *m_mirrorNodesTable = nullptr;
    QLabel *m_mirrorNodesSummary = nullptr;
    // GitHub-style repo page: header actions, tabs, branch/search, About sidebar.
    QString m_repoBranch;
    RepoInfo m_repoInfo;
    QLabel *m_repoHeaderTitle = nullptr;
    QLabel *m_repoDetailNotice = nullptr;
    QLabel *m_repoDetailStatus = nullptr;
    QPushButton *m_forkButton = nullptr;
    QPushButton *m_mirrorButton = nullptr;
    QPushButton *m_sourceButton = nullptr;
    QPushButton *m_repoOpenButton = nullptr; // open this repo on the web
    QMenu *m_mirrorMenu = nullptr;
    QMenu *m_sourceMenu = nullptr;
    QPushButton *m_branchButton = nullptr;
    QPushButton *m_branchesButton = nullptr;
    QPushButton *m_tagsButton = nullptr;
    QPushButton *m_editorModeButton = nullptr; // overview -> explorer/editor view
    QPushButton *m_overviewBackButton = nullptr; // editor view -> overview
    QLineEdit *m_fileSearch = nullptr;
    QCompleter *m_fileCompleter = nullptr;
    QLabel *m_securitySummary = nullptr;
    QWidget *m_securitySignalsPanel = nullptr;
    QGridLayout *m_securitySignalsGrid = nullptr;
    QTableWidget *m_securityFindingsTable = nullptr;
    QPushButton *m_securityRefreshButton = nullptr;
    QPushButton *m_aboutEditButton = nullptr;
    QLabel *m_aboutText = nullptr;
    QLabel *m_aboutTopics = nullptr;
    QLabel *m_aboutFiles = nullptr;
    QLabel *m_releaseHeader = nullptr;
    QLabel *m_releaseRow = nullptr;
    QLabel *m_langBar = nullptr;
    QLabel *m_langLegend = nullptr;
    QLabel *m_contributorsHeader = nullptr;
    QLabel *m_contributorsRow = nullptr;
    QTableWidget *m_commitsTable = nullptr;
    // What the commit table currently shows, so a repeat tab click (or the
    // redundant load when a repo first opens) can skip the full rebuild — 4 git
    // subprocesses plus 300 row widgets — when the branch and tip are unchanged.
    QString m_commitsLoadedRef;
    QString m_commitsLoadedTip;
    // The served mirror's tip at the time the list was built. The "waiting to
    // sync" markers compare the local history against this, so a publish/sync
    // that advances the mirror (without moving the local tip) must invalidate the
    // cached list — otherwise it keeps showing stale markers.
    QString m_commitsLoadedMirrorTip;
    QLineEdit *m_commitSearch = nullptr;       // filter the commit list by hash/summary
    // Top-bar "search everything" box and its floating results dropdown. The popup
    // is parented to the window (not the short top bar) so it isn't clipped, and is
    // NoFocus so clicking a result doesn't steal the keyboard from the search box.
    QLineEdit *m_globalSearch = nullptr;
    QListWidget *m_globalSearchPopup = nullptr;
    QTimer *m_globalSearchTimer = nullptr;     // debounce keystrokes before rebuilding
    // Full-page deep search (Enter in the box): streams working-tree text
    // matches, commit-message matches and history-diff (pickaxe) hits live.
    QTreeWidget *m_searchResultsTree = nullptr;
    QLabel *m_searchResultsTitle = nullptr;
    QLabel *m_searchResultsStatus = nullptr;
    QPushButton *m_searchResultsStop = nullptr;
    QList<QProcess *> m_searchProcs; // running git searches (killed by Stop)
    int m_searchPending = 0;         // how many of those are still running
    QString m_searchPageQuery;
    QLabel *m_commitsUnsyncedBanner = nullptr; // "N commits not yet synced" banner
    // The unsynced banner sits in the list page's layout above the table; it
    // fades out (collapsing its row) when every commit has synced.
    QWidget *m_commitsListPage = nullptr;
    QGraphicsOpacityEffect *m_commitsBannerOpacity = nullptr;
    QPropertyAnimation *m_commitsBannerFade = nullptr;
    QLabel *m_insightsSummary = nullptr;
    QLabel *m_insightsTraffic = nullptr;
    QLabel *m_insightsLanguageBar = nullptr;
    QLabel *m_insightsLanguageLegend = nullptr;
    QLabel *m_insightsActivity = nullptr;
    // Merged "Contributors & activity" table: name / commits / share / an
    // embedded commits-over-time bar chart, all scoped to m_insightsRangeCombo.
    QTableWidget *m_insightsContributors = nullptr;
    QComboBox *m_insightsRangeCombo = nullptr; // activity time window selector
    QLabel *m_insightsActivityAxis = nullptr;  // "oldest <- ... -> newest" caption
    QPushButton *m_insightsRefreshButton = nullptr;
    // Source Control panel (top of the Commits tab).
    QWidget *m_scmPanel = nullptr;
    QTreeWidget *m_scmTree = nullptr;
    QLineEdit *m_scmMessage = nullptr;
    QTextBrowser *m_scmDiff = nullptr;
    QLabel *m_scmCountLabel = nullptr;
    QPushButton *m_scmGenerateButton = nullptr;
    QComboBox *m_scmGenModel = nullptr;       // AI model for inline generation
    QComboBox *m_scmGenKind = nullptr;        // "Commit message" vs "X post"
    QComboBox *m_scmGenDuration = nullptr;    // diff scope: current / past hour / all day
    QPushButton *m_scmCopyButton = nullptr;   // copy the message to the clipboard
    QLabel *m_scmGenStatus = nullptr;         // inline cost / progress note
    bool m_scmGenerating = false;             // a generation request is in flight
    int m_scmHeuristicVariant = 0;            // cycles on-device drafts on re-click
    QPushButton *m_scmCommitButton = nullptr;
    QPushButton *m_scmCommitPushButton = nullptr; // commit, then publish/push
    QPushButton *m_scmStageCommitPushButton = nullptr; // stage all, commit, push
    QPushButton *m_scmStageAllButton = nullptr;
    QPushButton *m_scmUnstageAllButton = nullptr;
    QPushButton *m_scmDiscardAllButton = nullptr;
    QPushButton *m_scmRefreshButton = nullptr;
    QPushButton *m_scmPrevButton = nullptr;   // jump to previous changed file
    QPushButton *m_scmNextButton = nullptr;   // jump to next changed file
    QLabel *m_scmEmptyNote = nullptr;
    // Last `git status` output, so a focus/tab-click rescan can skip the (flickery)
    // full tree rebuild when nothing in the working tree actually changed.
    QByteArray m_scmStatusCache;
    // Rendered per-file diff HTML, keyed by "staged|untracked|path", so clicking
    // between files (or walking them with the up/down buttons) is instant after the
    // first view. Cleared whenever the working tree is rescanned.
    QHash<QString, QString> m_scmDiffCache;
    // Commits tab: a stack flipping between the list and a per-commit diff view.
    QStackedWidget *m_commitsStack = nullptr;
    QLabel *m_commitTitle = nullptr;
    QLabel *m_commitMeta = nullptr;
    QLabel *m_commitMessage = nullptr;
    QLabel *m_commitFilesSummary = nullptr;
    QListWidget *m_commitFileList = nullptr;
    QTextBrowser *m_commitDiffView = nullptr;
    QPushButton *m_commitPrevButton = nullptr;
    QPushButton *m_commitNextButton = nullptr;
    QPushButton *m_commitDownloadButton = nullptr;
    QPushButton *m_commitDeleteButton = nullptr; // drop this commit from history
    QPushButton *m_commitRevertButton = nullptr; // commit the inverse of this one
    QPushButton *m_commitSplitButton = nullptr; // toggle unified <-> side-by-side
    QString m_currentCommitHash; // full hash shown in the detail view
    int m_currentCommitRow = -1; // row in m_commitsTable the detail view is showing
    // Per-commit conversation (comment thread + composer).
    QWidget *m_commitThreadContainer = nullptr;
    QVBoxLayout *m_commitThreadLayout = nullptr;
    MarkdownEditor *m_commitComposer = nullptr;
    QPushButton *m_commitCommentButton = nullptr;
    // Files view: a GitHub-style overview (latest commit + file list + README)
    // that switches to an explorer-tree + editor-tabs view when a file is open.
    QStackedWidget *m_filesStack = nullptr; // 0 overview, 1 editor
    QString m_overviewPath;                 // current directory in the overview
    // Signature (repo|path|branch|HEAD) of the overview currently rendered, so
    // re-entering the repo screen unchanged skips the expensive git re-read.
    QString m_overviewLoadedKey;
    int m_treeLoadedForIndex = -1;          // repo whose explorer tree is built
    QLabel *m_commitBar = nullptr;
    QPushButton *m_historyButton = nullptr;
    QLabel *m_overviewCrumb = nullptr;
    // Code overview file list: a small table per directory (name, size bar, last
    // commit, when). Rows are cached so re-sorting doesn't re-shell out to git.
    QTreeWidget *m_overviewList = nullptr;
    struct OverviewRow {
        QString name;
        QString path;
        bool isDir = false;
        qint64 size = 0;     // blob bytes (recursive sum for directories)
        qint64 loc = 0;      // lines of code (recursive sum for directories)
        qint64 commitTs = 0; // last commit unix time that touched this entry
        QString subject;     // last commit subject
        QString whenText;    // relative "x ago"
    };
    QList<OverviewRow> m_overviewRows;
    qint64 m_overviewRepoBytes = 0; // whole-repo blob total (size-bar denominator)
    // Sorting is driven by clicking the overview table's column headers.
    QString m_overviewSortKey = QStringLiteral("name");
    bool m_overviewSortDesc = false;
    void populateOverviewTree(); // (re)fill m_overviewList from m_overviewRows
    QTextBrowser *m_readmeView = nullptr;
    QTreeWidget *m_repoFileTree = nullptr;
    QTabWidget *m_repoFileTabs = nullptr;
    QPushButton *m_repoFileCommitButton = nullptr;
    QPushButton *m_repoFilePullButton = nullptr;
    QHash<QString, QWidget *> m_openFileTabs; // repo-relative path -> editor tab

    // Discussions tab
    QTableWidget *m_discussionTable = nullptr;
    QLineEdit *m_discussionSearch = nullptr;
    QComboBox *m_discussionCategoryFilter = nullptr;
    QPushButton *m_discussionNewButton = nullptr;
    QPushButton *m_discussionSyncButton = nullptr;
    QLabel *m_discussionTitle = nullptr;
    QLabel *m_discussionMeta = nullptr;
    QLabel *m_discussionInlineNotice = nullptr;
    QScrollArea *m_discussionThreadScroll = nullptr;
    QWidget *m_discussionThreadContainer = nullptr;
    QVBoxLayout *m_discussionThreadLayout = nullptr;
    MarkdownEditor *m_discussionComposer = nullptr;
    QPushButton *m_discussionCommentButton = nullptr;
    QLabel *m_discussionCategorySummary = nullptr;
    QList<Discussion> m_currentDiscussions;
    int m_currentDiscussionNumber = -1;
    DiscussionInboxBackoff m_discussionInboxBackoff;

    // --- Cove (encrypted vault) UI + session state ----------------------------
    QWidget *m_coveSection = nullptr;        // repo Settings "Coves" group
    QListWidget *m_coveList = nullptr;       // coves in the open repo (lock state)
    QLineEdit *m_covePasswordEdit = nullptr; // per-repo unlock password field
    QCheckBox *m_coveAutoOpenCheck = nullptr;// per-repo auto-open toggle
    QLabel *m_coveEmptyHint = nullptr;
    QLineEdit *m_coveGlobalPasswordEdit = nullptr; // global Settings password
    QCheckBox *m_coveGlobalAutoOpenCheck = nullptr;
    // Cove id -> the password that unlocked it this session (memory only). Lets
    // "auto-show" reveal a cove without re-prompting and re-encrypt on save.
    QHash<QString, QString> m_coveSessionPasswords;

    // Pull requests tab
    QTableWidget *m_pullTable = nullptr;
    QLineEdit *m_pullSearch = nullptr;
    QPushButton *m_pullNewButton = nullptr;
    QPushButton *m_pullChooseDirButton = nullptr;
    QPushButton *m_pullImportButton = nullptr;
    QPushButton *m_pullSyncButton = nullptr;
    QWidget *m_pullDetail = nullptr;
    QLabel *m_pullTitle = nullptr;
    QLabel *m_pullMeta = nullptr;
    QLabel *m_pullMergeStatus = nullptr; // conflict / ready-to-merge banner
    QLabel *m_pullReviewSummary = nullptr;
    QPushButton *m_pullUpdateButton = nullptr;
    QPushButton *m_pullMergeButton = nullptr;
    QPushButton *m_pullResolveButton = nullptr; // opens the conflict merge editor
    // "Fix with agent" split button: a dropdown that rolls the Claude API,
    // OpenAI API and Claude Code conflict resolvers into one control (issue #150).
    QPushButton *m_pullFixButton = nullptr;
    QMenu *m_pullFixMenu = nullptr;
    QAction *m_pullFixClaudeAction = nullptr;   // resolve via the Claude API
    QAction *m_pullFixOpenAiAction = nullptr;   // resolve via the OpenAI API
    QAction *m_pullFixClaudeCodeAction = nullptr; // resolve via the Claude Code CLI
    QPushButton *m_pullEditFileButton = nullptr; // edit selected file on PR branch
    QPushButton *m_pullDeleteFileButton = nullptr; // delete selected file on PR branch
    QPushButton *m_pullCloseButton = nullptr;
    QPushButton *m_pullReopenButton = nullptr;
    QPushButton *m_pullDeleteButton = nullptr;
    QPushButton *m_pullDeleteBranchButton = nullptr; // delete the PR and its head branch
    QPushButton *m_pullMergeDeleteButton = nullptr;  // merge, then delete the PR + branch
    bool m_pullDeleteConfirmPending = false;
    bool m_pullDeleteInProgress = false; // a deletePull worker thread is running
    QListWidget *m_pullFiles = nullptr;
    QPushButton *m_pullPrevButton = nullptr; // jump to previous change in the PR
    QPushButton *m_pullNextButton = nullptr; // jump to next change in the PR
    QTextBrowser *m_pullDiff = nullptr;
    int m_diffFontPt = 12; // diff viewer text size (the +/- zoom control)
    QPushButton *m_pullSplitButton = nullptr; // toggle unified <-> side-by-side
    QListWidget *m_pullCommitsList = nullptr;  // commits that make up the PR
    // PR detail sub-tabs: Conversation / Commits / Checks / Files changed.
    QButtonGroup *m_pullSubTabs = nullptr;
    QStackedWidget *m_pullSubStack = nullptr;
    QPushButton *m_pullTabConversation = nullptr;
    QPushButton *m_pullTabCommits = nullptr;
    QPushButton *m_pullTabChecks = nullptr;
    QPushButton *m_pullTabFiles = nullptr;
    // Conversation: review thread + inline checks summary + inline composer.
    QScrollArea *m_pullThreadScroll = nullptr;
    QWidget *m_pullThreadContainer = nullptr;
    QVBoxLayout *m_pullThreadLayout = nullptr;
    QLabel *m_pullChecksSummary = nullptr; // compact pass/fail/running card in thread
    QLabel *m_pullConflictDetails = nullptr; // conflicting-files card above the composer
    MarkdownEditor *m_pullComposer = nullptr;
    QPushButton *m_pullCommentButton = nullptr;
    QPushButton *m_pullApproveButton = nullptr;
    QPushButton *m_pullRequestChangesButton = nullptr;
    // Agent revision: send feedback back to the agent that created this PR.
    QWidget *m_pullAgentRevisionRow = nullptr;
    QLineEdit *m_pullAgentRevisionEdit = nullptr;
    QPushButton *m_pullSendToAgentButton = nullptr;
    QPushButton *m_pullLinkIssueButton = nullptr; // "Link issue" in the PR header
    QLabel *m_pullLinksValue = nullptr;           // linked issues card in the thread
    // Checks tab: action runs for this PR's commits + a manual trigger.
    QTableWidget *m_pullChecksTable = nullptr;
    QPlainTextEdit *m_pullChecksLog = nullptr;
    QPushButton *m_pullRunChecksButton = nullptr;
    QList<PullRequest> m_currentPulls;
    QHash<QString, QString> m_pullFileDiffs; // current PR: file path -> diff text
    // Per-PR conflict flag (open PRs whose patch no longer applies cleanly to the
    // base), computed in reloadPulls() and read by refreshPullList() to badge the
    // list rows without re-running the dry-run apply on every search keystroke.
    QHash<int, bool> m_pullConflictByNumber;
    int m_currentPullNumber = -1;

    // Actions (CI on push to the mirror)
    struct AppNotification {
        QString title;
        QString body;
        qint64 timestampMs = 0;
        bool warning = false;
        int runId = -1;
    };
    ActionStore *m_actionStore = nullptr;
    ActionRunner *m_actionRunner = nullptr;
    QFileSystemWatcher *m_actionSpoolWatcher = nullptr;
    QList<ActionRun> m_actionRuns;   // loaded history, newest first
    QList<int> m_actionQueue;        // run ids queued for execution
    QList<AppNotification> m_notifications;
    QPushButton *m_notificationButton = nullptr;
    QTableWidget *m_notificationsTable = nullptr; // sortable Notifications page
    int m_selectedRunId = -1;
    QListWidget *m_actionWorkflowList = nullptr; // available actions (left column)
    QString m_selectedWorkflowFilter;            // workflow path filter, empty = all
    QList<ActionWorkflow> m_repoWorkflows;       // parsed workflows for the open repo
    QTimer *m_actionStripTimer = nullptr;        // grows the Actions strip while running
    QTimer *m_repoPushTimer = nullptr;           // keeps "Sync changes" pinned over Commits
    QTimer *m_agentsSpinTimer = nullptr;         // animates the Agents tab while running
    int m_agentsSpinFrame = 0;
    QTableWidget *m_actionsTable = nullptr;
    QLabel *m_actionRunTitle = nullptr;
    QLabel *m_actionRunMeta = nullptr;
    QLabel *m_actionApprovalBanner = nullptr;
    QPlainTextEdit *m_actionLog = nullptr;
    QTextEdit *m_actionDiff = nullptr;
    QWidget *m_actionApprovalBar = nullptr;
    QPushButton *m_actionApproveButton = nullptr;
    QPushButton *m_actionRejectButton = nullptr;
    // Manual ("workflow_dispatch") run controls, shown atop the detail pane only
    // when the selected workflow opts in. The branch combo defaults to "main".
    QWidget *m_actionManualRunBar = nullptr;
    QPushButton *m_actionManualRunButton = nullptr;
    QComboBox *m_actionRunBranchCombo = nullptr;
    // Re-queues the selected run with its original workflow/commit/ref.
    QPushButton *m_actionRerunButton = nullptr;
    // Copies the selected run's full log to the clipboard.
    QPushButton *m_actionCopyLogButton = nullptr;
    // Settings: variables/secrets table.
    QTableWidget *m_varsTable = nullptr;
    // When true, the variables table shows secret values in clear text instead
    // of the masked bullets. Toggled by the Reveal/Hide button.
    bool m_varsRevealed = false;
    QPushButton *m_varsRevealButton = nullptr;
    // Per-repo "run actions on push" toggle. Mirrored repos default off; the
    // user opts in either on the Actions tab or the repo Settings tab — both
    // checkboxes drive the same state via setRepoActionsEnabled().
    QCheckBox *m_actionsEnabledCheck = nullptr;
    QCheckBox *m_settingsActionsCheck = nullptr;
    // Per-repo visibility toggle: when checked the repo is private (hidden from
    // the public catalog; browse/clone gated on the owner's view token).
    QCheckBox *m_repoPrivateCheck = nullptr;
    // Collaborators panel for a private repo (issue #9): shown only when this
    // node owns a published private repo. m_collabSection wraps the whole block
    // so it can be hidden in one call.
    QWidget *m_collabSection = nullptr;
    QListWidget *m_collabList = nullptr;
    QLineEdit *m_collabEdit = nullptr;
    QLabel *m_collabEmptyHint = nullptr;
    // Per-repo source/fork location: the upstream clone URL this mirror was
    // forked from. Editable so a user can repoint the mirror at a live node
    // when the original baked-in URL goes stale (offline node id, moved relay).
    QLineEdit *m_repoSourceEdit = nullptr;
    QLabel *m_repoSourceHint = nullptr;
    // Agent sessions assigned from issues.
    AgentStore *m_agentStore = nullptr;
    // Pool of agent runners so sessions execute in parallel (one process each)
    // instead of being serialized through a single runner.
    QList<AgentRunner *> m_agentRunners;
    QList<AgentSession> m_agentSessions;
    QList<int> m_agentQueue;
    int m_selectedAgentSessionId = -1;

    // In-flight AI conflict resolution (see fixCurrentPullConflictsWithAi). The
    // PullStore carries the git-am session state across the async API calls, so it
    // must outlive each network reply; the struct is null when nothing is running.
    struct AiConflictFix {
        PullStore *store = nullptr; // null in branch-merge mode
        int number = 0;       // PR number (PR mode)
        int repoIndex = -1;
        int sessionId = 0;    // backing agent session
        QString provider;     // "claude" | "openai" | "claude-code"
        QString model;
        QString apiKey;
        QString workTree;
        QStringList files;    // conflicted paths, repo-relative
        int index = 0;        // next file to resolve
        double costUsd = 0.0;
        qint64 inTokens = 0;
        qint64 outTokens = 0;
        // Claude Code mode: instead of POSTing each file to an API, the real
        // `claude` CLI runs once over the whole conflict-marked tree. The process
        // streams into the session log and finish/fail commit-or-abort as usual.
        bool claudeCode = false;
        QProcess *process = nullptr; // running CLI (claudeCode mode), else null
        // Branch-merge mode: resolving a `branch <- baseBranch` merge already laid
        // down (with conflict markers) in the working tree, instead of a PR patch
        // apply. finish/fail commit-or-abort the merge directly.
        bool branchMerge = false;
        QString branch;        // target branch being brought up to date
        QString baseBranch;    // base branch merged into it
        QString restoreBranch; // branch to check back out when done
    };
    AiConflictFix *m_aiFix = nullptr;
    void aiFixResolveNextFile();           // send the next conflicted file to the model
    void aiFixRunClaudeCode();             // run the `claude` CLI over the conflict tree
    void aiFixApplyResolved(const QString &resolved); // write back + advance
    void aiFixFinish();                    // commit to branch, mark session success
    void aiFixFail(const QString &message); // abort the merge, mark session failed
    void aiFixLog(const QString &text);    // stream a line into the agent session log
    void aiFixSetSessionStatus(const QString &status, const QString &error = QString());
    QTableWidget *m_agentTable = nullptr;
    QWidget *m_agentDetail = nullptr; // collapsible detail panel (hidden until a row is picked)
    QLabel *m_agentTitle = nullptr;
    QLabel *m_agentStatusPill = nullptr; // connected/working/done status
    QLabel *m_agentMeta = nullptr;
    QLabel *m_agentUsage = nullptr;
    QLabel *m_agentNetPanel = nullptr;   // live API-traffic graphic
    QPushButton *m_agentViewPrButton = nullptr;
    QPlainTextEdit *m_agentLog = nullptr;
    QStackedWidget *m_agentOutputStack = nullptr; // log (0) | embedded terminal (1)
    TerminalWidget *m_agentTerminal = nullptr;  // Claude Code runs here
    int m_terminalSessionId = -1; // session currently driving the embedded terminal
    // Makes the app act as the IDE the `claude` CLI connects to (issue #191):
    // a localhost WebSocket/JSON-RPC server advertised via ~/.claude/ide. Lazily
    // created on the first Claude Code terminal launch.
    ClaudeIdeBridge *m_ideBridge = nullptr;
    ClaudeIdeBridge *ensureIdeBridge();
    void onClaudeOpenDiff(const QString &tabName, const QString &oldPath,
                          const QString &newPath, const QString &newContents);
    // Extension-style transcript for Claude Code sessions: claude runs in
    // stream-json mode (ClaudeStreamSession) and the events render as native
    // cards (ClaudeTranscriptView, output stack page 2). A header toggle flips
    // to the raw process output (page 0) for debugging.
    ClaudeTranscriptView *m_agentTranscript = nullptr;
    QPushButton *m_transcriptModeButton = nullptr;
    QPushButton *m_terminalModeButton = nullptr;
    QComboBox *m_agentDiffModeCombo = nullptr; // unified vs split diff selector
    QWidget *m_agentOutputToggle = nullptr;
    QListWidget *m_agentFilesList = nullptr;     // files edited in this session
    QWidget *m_agentFilesPanel = nullptr;        // wraps the list + heading
    QProgressBar *m_agentUsageBar = nullptr;     // weekly usage graph
    QProgressBar *m_agentUsage5hBar = nullptr;   // 5-hour usage graph
    QLabel *m_agentStatsLabel = nullptr;         // live tokens + cost counter
    QTimer *m_agentHourlyTimer = nullptr;        // refreshes usage + files hourly
    // Each running Claude Code session has its own worktree + stream + buffered
    // events, so their output never leaks across sessions; the transcript view is
    // repainted from the selected session's buffer.
    QHash<int, ClaudeStreamSession *> m_streamSessions;
    QHash<int, QList<QJsonObject>> m_streamEvents;
    QHash<int, QString> m_streamRaw;
    QHash<int, qint64> m_sessionTokens; // live token total per session, for the list
    QHash<int, QString> m_lastAssistantText; // last assistant prose, for waiting/question
    void notifyAgentWaiting(int sessionId, bool needsPermission);
    QHash<int, QStringList> m_streamFiles;
    QHash<int, QString> m_streamWorktree;        // sessionId -> worktree path
    void startClaudeCodeTranscript(AgentSession &session, const Issue &issue,
                                   const QString &repoPath);
    void applyTranscriptEvent(int sessionId, const QJsonObject &ev);
    void renderTranscriptForSession(int sessionId);
    void refreshAgentFilesPanel(int sessionId);
    void maybeCreatePullForStreamSession(int sessionId);
    bool isStreamTranscriptSession(int sessionId) const;
    // Stop a live Claude Code stream session (the Stop button). stop() emits no
    // `finished`, so transition the session to Stopped and refresh here.
    void stopStreamSession(int sessionId);
    // Working directory for a session: its worktree if it has one, else the repo.
    QString sessionWorkdir(int sessionId);

    // ---- External Claude Code sessions ------------------------------------
    // Claude Code runs started outside ForkMesh (a terminal, another editor) are
    // detected from the transcripts the CLI writes to disk. They surface as extra
    // spinners over the Agents tab; clicking one adds a read-only, temporary entry
    // to the agents list whose transcript we render and tail live. Temp entries
    // use synthetic ids <= kExternalIdBase (real sessions are positive; -1 is the
    // "nothing selected" sentinel), and never touch the AgentStore.
    static constexpr int kExternalIdBase = -1000;
    QTimer *m_externalClaudeTimer = nullptr;
    QList<ExternalClaudeSession> m_externalClaude;        // detected for the open repo
    QHash<QString, int> m_externalTempId;                 // uuid -> synthetic id
    QHash<int, ExternalClaudeSession> m_externalSurfaced; // synthetic id -> session
    QHash<int, QString> m_externalSurfacedRepo;           // synthetic id -> "owner/name"
    QHash<int, qint64> m_externalReadOffset;              // synthetic id -> tail offset
    QString m_externalSig;          // last list/spinner signature, to skip no-op rebuilds
    int m_nextExternalTempId = kExternalIdBase;
    void scanExternalClaudeSessions();
    void onExternalClaudeTick();
    void injectExternalSessions();          // append surfaced temp entries to the list
    int registerExternalSession(const QString &uuid); // auto-create a temp list entry
    void surfaceExternalSession(const QString &uuid); // click -> jump to its entry
    void unsurfaceExternalSession(int sessionId);     // remove a temp entry
    int externalTempIdFor(const QString &uuid);
    bool isExternalSession(int sessionId) const { return sessionId <= kExternalIdBase; }
    bool externalIsLive(const QString &uuid) const; // still in the detected set
    void renderExternalTranscript(int sessionId, bool full);
    QSet<QString> ownStreamCwds() const;    // dirs ForkMesh's own streams drive
    QPlainTextEdit *m_agentPromptEdit = nullptr;
    QPushButton *m_agentAddFilesButton = nullptr; // composer "+" : attach files
    QPushButton *m_agentSlashButton = nullptr;    // composer "/" : slash commands
    QComboBox *m_agentAutoModeCombo = nullptr;    // composer Auto-mode selector
    void addFilesToAgentPrompt();
    void showAgentSlashMenu();
    QPushButton *m_agentStopButton = nullptr;
    QPushButton *m_agentContinueButton = nullptr;
    QPushButton *m_agentDeleteButton = nullptr;
    QPushButton *m_agentTestApiKeyButton = nullptr;
    QLabel *m_agentOpenAiSpend = nullptr;
    QLabel *m_agentApiKeyStatus = nullptr;
    QLabel *m_agentClaudeSpend = nullptr;
    QLabel *m_agentClaudeStatus = nullptr;
    // Remaining API credits fetched from each provider.
    QLabel *m_agentOpenAiCredit = nullptr;
    QLabel *m_agentClaudeCredit = nullptr;
    // Combined OpenAI + Claude month-to-date spend. The doubles hold the last
    // known numeric USD figure per provider (NaN = not yet known) so the total
    // can be recomputed whenever either side refreshes.
    QLabel *m_agentTotalSpend = nullptr;
    double m_openAiSpendUsd = std::numeric_limits<double>::quiet_NaN();
    double m_claudeSpendUsd = std::numeric_limits<double>::quiet_NaN();
    void updateAgentTotalSpend();
    // Time left in the rolling 5-hour and weekly usage windows per provider.
    QLabel *m_agentLimitsLabel = nullptr;
    QTimer *m_agentLimitsTimer = nullptr;
    QPushButton *m_agentSendPromptButton = nullptr;
    // Spinning refresh (rebuild) button in the nav rail.
    QPushButton *m_refreshButton = nullptr;
    QTimer *m_refreshSpinTimer = nullptr;
    int m_refreshAngle = 0;
    // Commits-page branch indicator + checkout switcher.
    QPushButton *m_commitsBranchButton = nullptr;
    // Commits-page Refresh button + its spin animation state.
    QPushButton *m_commitsRefreshButton = nullptr;
    QPushButton *m_commitsGenerateButton = nullptr; // "Generate post" (multi-select)
    QTimer *m_commitsRefreshSpinTimer = nullptr;
    // Infinite-scroll paging for the commit list: how many commits are currently
    // loaded, whether older history remains, and a re-entrancy guard.
    int m_commitsLimit = 300;
    bool m_commitsHasMore = false;
    bool m_commitsLoadingMore = false;
    // While a commit search is active, the lazily-paged window is deepened to the
    // whole history so the filter spans every commit (incl. by hash); cleared back
    // to the paged window when the search box empties.
    bool m_commitsShowingAll = false;
    int m_commitsRefreshAngle = 0;
    // Node-switch busy indicator (spinner on the top-nav node button).
    QTimer *m_nodeSwitchSpinTimer = nullptr;
    int m_nodeSwitchAngle = 0;
    // Indeterminate loading bar floated just under the node button while a node
    // switch's heavy repo load runs. Created lazily by startNodeSwitchSpin.
    QProgressBar *m_nodeSwitchProgress = nullptr;
    void positionNodeSwitchProgress();
    // Repo-switch busy indicator (spinner on the top-nav repo button).
    QTimer *m_repoSwitchSpinTimer = nullptr;
    int m_repoSwitchAngle = 0;
    // Per-row spinner in the issue list's Agent column while a session is active.
    QTimer *m_issueSpinTimer = nullptr;
    int m_issueSpinFrame = 0;
    void tickIssueListSpinners();
    bool m_nodeSwitching = false;      // a node switch's heavy load is running
    bool m_repoDetailLoading = false;  // re-entrancy guard for openRepoDetail
    int m_repoOpenPending = -1;        // repo index queued by openRepoDetailDeferred
    // True while a user-driven repo load (a node switch or opening a repo) runs,
    // so nodeSwitchStep narrates progress for both, not just node switches.
    bool m_repoLoadActive = false;
    // True while the blue progress pill (showLoadStatus) owns the top-bar toast,
    // so the load can clear it on finish without stomping a real success/error.
    bool m_loadStatusShowing = false;
    QLabel *m_issueTitle = nullptr;
    QLineEdit *m_issueTitleEditor = nullptr;
    QPushButton *m_issueTitleEditButton = nullptr;
    QPushButton *m_issueTitleSaveButton = nullptr;
    QPushButton *m_issueTitleCancelButton = nullptr;
    QLabel *m_issueMeta = nullptr;
    QLabel *m_issueComposerAvatar = nullptr;
    QLabel *m_issueReadonlyNote = nullptr;
    QLabel *m_issueInlineNotice = nullptr;
    QLabel *m_issueAssigneesValue = nullptr;
    QLabel *m_issueLabelsValue = nullptr;
    QLabel *m_issueMilestoneValue = nullptr;
    QLabel *m_issuePriorityValue = nullptr;
    // Draggable progress bar on the detail panel (a ProgressSlider, kept as a
    // QWidget* since that type is private to MainWindow.cpp).
    QWidget *m_issueProgressSlider = nullptr;
    QLabel *m_issueEstimateValue = nullptr; // derived OpenAI coding cost estimate
    QLabel *m_issueBountyValue = nullptr;
    QStackedWidget *m_issueAssigneesStack = nullptr;
    QStackedWidget *m_issueLabelsStack = nullptr;
    QStackedWidget *m_issueMilestoneStack = nullptr;
    QStackedWidget *m_issuePriorityStack = nullptr;
    QLineEdit *m_issueAssigneesEdit = nullptr;
    QLineEdit *m_issueLabelsEdit = nullptr;
    QComboBox *m_issueMilestoneEdit = nullptr;
    QComboBox *m_issuePriorityEdit = nullptr;
    QScrollArea *m_issueThreadScroll = nullptr;
    QWidget *m_issueThreadContainer = nullptr;
    QVBoxLayout *m_issueThreadLayout = nullptr;
    QWidget *m_issueAiTypingRow = nullptr; // transient "AI is answering…" card
    QTimer *m_issueAiTypingTimer = nullptr;
    MarkdownEditor *m_issueComposer = nullptr;
    QPushButton *m_issueNewButton = nullptr;
    QPushButton *m_issueSyncButton = nullptr;
    QPushButton *m_issueCopyButton = nullptr;
    QPushButton *m_issueCopyAllButton = nullptr;
    QPushButton *m_issueVoteButton = nullptr;
    QLabel *m_issueCreditsLabel = nullptr;
    QPushButton *m_issueCommentButton = nullptr;
    QPushButton *m_issueAskAiButton = nullptr;
    QPushButton *m_issueAttachButton = nullptr;
    QPushButton *m_issueCloseButton = nullptr;
    QPushButton *m_issueCloseCommentButton = nullptr;
    QPushButton *m_issueLabelsButton = nullptr;
    QPushButton *m_issueMilestoneButton = nullptr;
    QPushButton *m_issuePriorityButton = nullptr;
    QPushButton *m_issuePriorityRaiseButton = nullptr;
    QPushButton *m_issuePriorityLowerButton = nullptr;
    QPushButton *m_issueProgressButton = nullptr;
    QPushButton *m_issueProgressBoostButton = nullptr;
    QPushButton *m_issueBountyButton = nullptr;
    QPushButton *m_issueAssigneesButton = nullptr;
    QLabel *m_issueDevelopmentValue = nullptr;    // linked pull requests list
    QPushButton *m_issueLinkPullButton = nullptr; // "Link pull request"
    QPushButton *m_issueDeleteButton = nullptr;
    QLabel *m_issueAgentValue = nullptr;
    QCheckBox *m_issueAgentCreatePrCheck = nullptr;
    QComboBox *m_issueAgentProvider = nullptr;   // OpenAI API | Claude API
    QPushButton *m_issueAssignAgentButton = nullptr;
    QPushButton *m_issueAgentViewButton = nullptr;
    // "Run in IDE" hand-off (shown only when IDE integration is on + detected).
    QLabel *m_issueIdeLabel = nullptr;
    QPushButton *m_issueIdeClaudeButton = nullptr;
    QPushButton *m_issueIdeCodexButton = nullptr;
    QList<Issue> m_currentIssues;
    QList<IssueLabel> m_currentLabels;
    QList<IssueMilestone> m_currentMilestones;
    int m_currentIssueNumber = -1;
    QString m_currentIssueTitle;
    bool m_issueDeleteConfirmPending = false;
    bool m_issueHistoryDeleteInProgress = false;
    // Set just before a reload that closes the viewed issue: if closing drops it
    // out of the filtered list, refreshIssueList keeps the detail panel open on
    // that same (now-closed) issue instead of collapsing to the full-width list
    // or jumping elsewhere (issue #188).
    bool m_keepCurrentOnReload = false;
    QStringList m_pendingIssueAttachments; // images queued for the next comment

    // Node profile panel widgets + the node it currently shows.
    QWidget *m_nodeProfilePanel = nullptr;
    QWidget *m_repoDetailSection = nullptr; // hidden while node profile is full-page
    QLabel *m_profileAvatar = nullptr;
    QPixmap m_profileAvatarSource; // raw avatar, re-scaled to a banner on resize
    QLabel *m_profileName = nullptr;
    QLabel *m_profileStatus = nullptr;
    // Single rich-text "details" card holding the key:value rows (status,
    // platform, version, uptime, key, …). Replaces the old one-label-per-line
    // layout so the panel reads like a control panel.
    QLabel *m_profileDetails = nullptr;
    QLabel *m_profileMirrorsLabel = nullptr;
    QLabel *m_profileMirrors = nullptr;
    QLabel *m_profileNote = nullptr;
    // Headline stat tiles (self only): repos / mirrored / online / chats.
    QWidget *m_profileStatGrid = nullptr;
    QLabel *m_profileTileRepos = nullptr;
    QLabel *m_profileTileMirrored = nullptr;
    QLabel *m_profileTileOnline = nullptr;
    QLabel *m_profileTileChats = nullptr;
    // Per-repo hosting stats (served/clones/hosted-since/last-sync), moved here
    // from the repo detail view. Shown only for your own node.
    QLabel *m_profileHostingLabel = nullptr;
    QLabel *m_profileHosting = nullptr;
    bool m_profileIsSelf = false;
    QLabel *m_profileNodeKey = nullptr;
    QLabel *m_profileSolanaAddr = nullptr;
    QLabel *m_profileQr = nullptr;
    QWidget *m_profileSolanaSection = nullptr;
    QLabel *m_profileBalance = nullptr;
    QPushButton *m_profileBalanceButton = nullptr;
    QPushButton *m_profileVerifyButton = nullptr;
    QLabel *m_profileEligibility = nullptr;
    QPushButton *m_profileMessageButton = nullptr;
    // "Get paid to mirror": opt-in CTA shown under the username on your own
    // profile. Flips to an "earning" label once the node is activated.
    QPushButton *m_profileGetPaidButton = nullptr;
    // Self-only actions pinned to the top of the node profile panel.
    QWidget *m_profileSelfActions = nullptr;
    QPushButton *m_profileRebuildButton = nullptr;
    QPushButton *m_profileUpdateButton = nullptr;
    QString m_profileNodeId;
    QString m_profileNodeName;
    QString m_profileSolanaValue;

    QStringList m_channels;
    QList<RepositoryRecord> m_repositories;
    QList<RepoHost *> m_repoHosts;
    QList<MemberInfo> m_homeRoster;
    // Catalog-backed mirror list (issue #223): the worker's /mirrors payload for
    // the repo group currently shown in the mirror-nodes panel, merged in so a
    // mirror that isn't live in the chat room is still listed for the owner.
    QString m_catalogMirrorsSource;        // "owner/name" the cache holds
    QJsonArray m_catalogMirrorsCache;      // last /mirrors payload's "mirrors"
    QString m_catalogMirrorsFetchSource;   // source the last fetch was kicked for
    qint64 m_catalogMirrorsFetchedMs = 0;  // throttle: last fetch kick time
    // "owner/name" -> { times served through the mainnode, clones }.
    QHash<QString, QPair<int, int>> m_repoStats;
    QSet<int> m_syncingRepos;
    QSet<int> m_pushingRepos;
    QString m_currentConversation;
    // Per-conversation message log and the live rows for the open conversation.
    QHash<QString, QList<ChatMessage>> m_history;
    QSet<QString> m_historyIds; // message ids already in m_history (dedup)
    QTimer *m_chatSaveTimer = nullptr;
    QHash<QString, MessageRow *> m_visibleRows; // messageId -> row (current conv)
    // messageId -> emoji -> reactor display names.
    QHash<QString, QMap<QString, QStringList>> m_reactions;
    QHash<QString, QPixmap> m_avatars;          // senderId -> avatar
    QHash<QString, QString> m_dmNames;          // peerId -> display name
    QHash<QString, QHash<QString, QString>> m_typing; // conversation -> peerId -> name
    QStringList m_networkLog;
    QStringList m_openDms;                      // peerIds in sidebar order
    QSet<QString> m_unread;
    // Node ids (pubkeys) confirmed to be admins, so repeated moderation deletes
    // from the same admin don't re-hit the accounts API.
    QSet<QString> m_knownAdminPubkeys;
    // Per-conversation unread message tally, summed into the red count badge on
    // the chat button. Kept in lockstep with m_unread.
    QHash<QString, int> m_unreadCounts;
    QString m_userName;
    QByteArray m_userAvatar;
    QString m_typingConversation;
    QTimer *m_typingStopTimer;
    QTimer *m_homeStatsTimer = nullptr;
    qint64 m_connectedAtMs = 0;
    qint64 m_totalConnectionMs = 0;
    // Until this moment, "node connected" alerts are suppressed: the roster
    // arrives incrementally right after we connect, so without a grace window
    // every node that was already online would pop a notification on startup.
    qint64 m_nodeAlertGraceUntilMs = 0;

    // Registered account/node identity for this session.
    bool m_accountAuthenticated = false;
    QString m_accountName;
    bool m_accountSolanaVerified = false;
    // "free" = view-only (must mirror >=1 repo) until the user joins by
    // donating; "active" = donated + email/password set.
    QString m_accountTier = QStringLiteral("free");
    QTimer *m_heartbeatTimer = nullptr;
    bool m_isAdmin = false;
    QTimer *m_adminPollTimer = nullptr;
    QStringList m_seenPendingUsers;
    // Avatar shown in the server rail (in place of the old settings gear); a
    // click opens Settings.
    QPushButton *m_avatarNavButton = nullptr;
#ifdef FORKMESH_WINDOW_TESTS
    bool m_testUseAccountFlowResult = false;
    bool m_testAccountFlowResult = true;
    int m_testEnsureNodeAccountCalls = 0;
    bool m_testBypassServerStart = false;
    TestIssueHistoryDeleteRunner m_testIssueHistoryDeleteRunner;
#endif
};
