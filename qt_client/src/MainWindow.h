#pragma once

#include "ChatBackend.h"
#include "ForkMeshIdentity.h"
#include "IssueStore.h"
#include "PullStore.h"
#include "ActionStore.h"
#include "ActionFile.h"
#include "AgentStore.h"
#include "AgentRunner.h"

struct CommitComment; // CommitCommentStore.h

#include <QHash>
#include <QIcon>
#include <QJsonArray>
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
class RepoHost;
class ActionRunner;
class QButtonGroup;
class QFileSystemWatcher;
class QSplitter;
class QTextEdit;
class QCheckBox;
class QComboBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QNetworkAccessManager;
class QPlainTextEdit;
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
    int testAccountFlowCalls() const { return m_testEnsureNodeAccountCalls; }
    int testStackIndex() const;
    QString testUserName() const { return m_userName; }
    QString testAccountName() const { return m_accountName; }
    QString testSavedSolanaAddress() const;
    bool testAccountAuthenticated() const { return m_accountAuthenticated; }
    QString testAccountTier() const { return m_accountTier; }
    int testAddPublishedRepository(const QString &owner, const QString &name,
                                   const QString &mirrorPath);
    void testPublishRepository(int index) { publishRepository(index, false); }
    void testStartRepoHosts() { startRepoHosts(); }
    void testStopRepoHosts() { stopRepoHosts(); }
    int testRepoHostCount() const { return m_repoHosts.size(); }
#endif

protected:
    void closeEvent(QCloseEvent *event) override;
    // Image drag-and-drop onto the inline issue comment composer.
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    // Setup page
    QWidget *buildSetupPage();
    void startSession();
    // Account = node identity. Registration (name + Solana + password + TOTP) gates
    // joining the network; the account name is the canonical repo owner.
    bool ensureNodeAccount(const QString &accountName, const QString &solana);
    // Non-interactive auth used on launch: true only if this node key already
    // matches a registered active account (or was confirmed before, offline).
    bool authenticateSilently(const QString &accountName);
    bool runSignupFlow(const QString &accountName, const QString &solana);
    bool runLoginFlow(const QString &accountName);
    // Staged-join helpers: pick repos to mirror, then donate + poll.
    bool runRepoPickStep();
    bool runDonationStep(const QString &accountName);
    QJsonArray fetchCatalogRepos();
    int fetchNodesOnline();
    bool hasActiveAccountSession() const;
    void mirrorCatalogRepo(const QString &owner, const QString &name,
                           const QString &cloneUrl);
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
    void runUpdateStep(const QString &program, const QStringList &arguments,
                       const QString &workingDir, std::function<void()> onSuccess);
    // Like runUpdateStep, but runs the command as m_updateAsUser (via sudo -u)
    // when that is set, so root-launched updates write files owned by the user.
    void runUpdateStepUser(const QString &program, const QStringList &arguments,
                           const QString &workingDir, std::function<void()> onSuccess);
    void setUpdateStatus(const QString &status, bool isError = false);
    void persistProfile();

    // Chat page
    QWidget *buildChatPage();

    // Global donation nudge shown until this node sets a Solana address.
    QWidget *buildSolanaNotice();
    void updateSolanaNotice();
    void promptSetSolanaAddress();

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

    // Repo detail view (files + issues tabs), opened by clicking a repository.
    QWidget *buildRepoDetailSection();
    QWidget *buildRepoFilesPanel();
    QWidget *buildRepoOverviewPage();
    QWidget *buildRepoEditorPage();
    QWidget *buildRepoCommitsTab();
    void showCommit(const QString &hash); // open the commit diff detail view
    void showCommitList();                // back to the commits list
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
    QWidget *buildInsightsTab();
    QWidget *buildPlaceholderTab(const QString &name);

    // Pull requests tab (cross-node, patch-based) with explorer + diff viewer.
    QWidget *buildPullsTab();
    PullStore pullStoreForCurrentRepo() const;
    void reloadPulls();
    void refreshPullList();
    void showPull(int number);
    void renderPullDiff(const QString &filePath);
    // Handle a click on a diff line-number anchor ("cmt:<side>:<line>"): prompt
    // for a comment and attach it to that line of the current PR file.
    void onPullDiffAnchorClicked(const QUrl &url);
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
    void closeIssuesLinkedFromPull(const PullRequest &pr);
    // After a merge, mint the escrow deposit address and show the funding QR for
    // any (pledged-but-unpaid) bounty on the issues this PR closes. Bounties are
    // added to issues without paying up front; merge is when they get funded.
    void fundBountiesForMergedPull(const PullRequest &pr);
    // Poll a bounty escrow after merge; once funded the worker splits it to the
    // author + treasury, and this records the paid state on the issue.
    void pollBountyPayout(const RepositoryRecord &repo, int number, double amount);
    QList<int> issuesLinkedFromPull(const PullRequest &pr) const;
    void closeCurrentPull();
    void deleteCurrentPull();
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
    AgentRunner::Config agentConfigForProvider(const QString &provider) const;
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
    void persistVariablesFromTable();

    void openRepoDetail(int repoIndex);
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
    void loadRepoInfo();
    void loadBranchesAndTags();
    QStringList repoBranches() const;
    QString repoDefaultBranch(const QStringList &branches) const;
    QWidget *buildBranchesTab();
    void loadBranchesPanel();
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
    // Set "run actions on push" for the open repo and keep both toggles in sync.
    void setRepoActionsEnabled(bool on);
    void loadMirrorNodesPanel();
    void deleteTag(const QString &tag);
    bool repoHasWorkingTree() const;
    void loadFileSearchIndex();
    void loadAboutSidebar();
    void loadCommits();
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
    // Spin the Agents tab label while any agent session is running.
    void updateAgentsTabIndicator();
    // Re-render commit check glyphs in whichever repo-detail tab is visible.
    void refreshCommitStatusGlyphs();
    void loadRepoInsights();
    void setRepoBranch(const QString &branch);
    QString currentRef() const;
    QString repoGitDir() const;
    QString iconsDir() const;
    QIcon iconForFile(const QString &fileName) const;
    QIcon iconForDir(bool opened) const;
    void startRefreshSpin();
    void stopRefreshSpin();
    // Busy feedback for switching nodes in the top nav: the node button shows a
    // spinner and the heavy repo load reports each step to the log. nodeSwitchStep
    // logs the step and, mid-switch, yields the event loop so the spinner animates.
    void startNodeSwitchSpin();
    void stopNodeSwitchSpin();
    void nodeSwitchStep(const QString &what);

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
    // Switch the issues list stack (0 Issues, 1 Milestones, 2 Labels) and toggle
    // the filter controls, which only apply to the Issues table.
    void selectIssueListTab(int id);
    void refreshIssueMilestones();
    void refreshIssueLabels();
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
    void copyIssueToClipboard();
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
    void attachIssueImage();
    void queueIssueAttachment(const QString &path); // dedupe + reference + count
    void toggleIssueStatus();
    void deleteCurrentIssue();
    void editIssueLabels();
    void editIssueMilestone();
    void editIssuePriority();
    void editIssueProgress();
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
    void saveIssueLabelsInline();
    void saveIssueMilestoneInline();
    void saveIssuePriorityInline();
    void saveIssueAssigneesInline();
    void cancelIssueSidebarEditors();
    void updateIssueActionState();
    QUrl issuesApiUrl(const RepositoryRecord &repo) const;
    QUrl bountyApiUrl(const RepositoryRecord &repo) const;
    // Pop up a modal with a Solana QR + address so a funder can pay a bounty.
    void showBountyQrDialog(const QString &uri, const QString &address,
                            double amountUsd);
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
    // Compact, centered success/failure banner shown in the top bar between the
    // breadcrumb and the notifications bell. Auto-clears after a few seconds.
    void flashMessage(const QString &text, bool error = false);
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
    void sendTypingState(bool active);
    void refreshTypingLabel();
    void attachFile();
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
    void promptAddRepository();
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
    // After a local change to a repo (new/updated issue, PR, comment, merge),
    // push it to the bare mirror and tell peers immediately instead of waiting
    // for the 5-minute auto-sync, so counts and content converge right away.
    void propagateRepoUpdate(int index);
    // A peer announced it refreshed "owner/name" from source; notify if we
    // mirror the same repo.
    void onPeerMirrorUpdated(const QString &ownerName, const QString &peerName);
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
    QString m_topMessageRaw;              // plain text of the current toast, for copy

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
    QHBoxLayout *m_repoHeaderLeft = nullptr; // left cluster of the repo header row
    QPushButton *m_repoPushButton = nullptr; // top-bar push for commits ahead of upstream
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
    QLineEdit *m_codexApiKeyEdit = nullptr;
    QLineEdit *m_openAiAdminKeyEdit = nullptr;
    QLineEdit *m_codexModelEdit = nullptr;
    QLineEdit *m_claudeApiKeyEdit = nullptr;
    QLineEdit *m_claudeAdminKeyEdit = nullptr;
    QLineEdit *m_codexCommandEdit = nullptr;
    QLineEdit *m_claudeCommandEdit = nullptr;
    QLineEdit *m_agentContextEdit = nullptr;
    QLineEdit *m_agentMaxOutputEdit = nullptr;
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
    QWidget *m_issueDetail = nullptr;          // collapsible detail panel
    QStackedWidget *m_issueDetailStack = nullptr;
    QWidget *m_issueComposePage = nullptr;
    QPushButton *m_issueDetailToggle = nullptr;
    QLineEdit *m_issueQuickAdd = nullptr;
    QCheckBox *m_quickAddAssignAgent = nullptr; // assign a coding agent on add
    QComboBox *m_quickAddAgentProvider = nullptr;
    QCheckBox *m_quickAddCreatePr = nullptr;    // request PR from quick-add agent

    // Repo detail view
    int m_repoDetailIndex = -1;
    QButtonGroup *m_issueTabGroup = nullptr; // Issues / Milestones / Labels tabs
    QButtonGroup *m_repoDetailTabs = nullptr;
    QPushButton *m_repoCodeTab = nullptr;
    QPushButton *m_repoCommitsTab = nullptr;
    QPushButton *m_repoIssuesTab = nullptr;
    QPushButton *m_repoPullsTab = nullptr;
    QPushButton *m_repoAgentsTab = nullptr;
    QPushButton *m_repoActionsTab = nullptr;
    QPushButton *m_repoMirrorsTab = nullptr; // handle for the Mirror nodes (N) badge
    QStackedWidget *m_repoDetailStack = nullptr;
    int m_chatStackIndex = -1; // index of the Chat page in m_repoDetailStack
    int m_branchesTabIndex = -1; // index of the Branches page
    int m_releasesTabIndex = -1; // index of the Releases page
    int m_mirrorNodesTabIndex = -1; // index of the Mirror nodes page
    int m_settingsTabIndex = -1; // index of the Settings page
    QLabel *m_repoVisibilityHint = nullptr; // explains the current visibility
    QTableWidget *m_branchesTable = nullptr;
    QLabel *m_branchesSummary = nullptr;
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
    QMenu *m_forkMenu = nullptr;
    QMenu *m_mirrorMenu = nullptr;
    QMenu *m_sourceMenu = nullptr;
    QPushButton *m_branchButton = nullptr;
    QPushButton *m_branchesButton = nullptr;
    QPushButton *m_tagsButton = nullptr;
    QPushButton *m_editorModeButton = nullptr; // overview -> explorer/editor view
    QPushButton *m_overviewBackButton = nullptr; // editor view -> overview
    QLineEdit *m_fileSearch = nullptr;
    QCompleter *m_fileCompleter = nullptr;
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
    QLabel *m_commitsUnsyncedBanner = nullptr; // "N commits not yet synced" banner
    QLabel *m_insightsSummary = nullptr;
    QLabel *m_insightsTraffic = nullptr;
    QLabel *m_insightsLanguageBar = nullptr;
    QLabel *m_insightsLanguageLegend = nullptr;
    QLabel *m_insightsActivity = nullptr;
    QTableWidget *m_insightsContributors = nullptr;
    QTableWidget *m_insightsRecentCommits = nullptr;
    QPushButton *m_insightsRefreshButton = nullptr;
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
        qint64 commitTs = 0; // last commit unix time that touched this entry
        QString subject;     // last commit subject
        QString whenText;    // relative "x ago"
    };
    QList<OverviewRow> m_overviewRows;
    qint64 m_overviewRepoBytes = 0; // whole-repo blob total (size-bar denominator)
    QComboBox *m_overviewSortCombo = nullptr;
    QPushButton *m_overviewSortDirButton = nullptr;
    bool m_overviewSortDesc = false;
    void populateOverviewTree(); // (re)fill m_overviewList from m_overviewRows
    QTextBrowser *m_readmeView = nullptr;
    QTreeWidget *m_repoFileTree = nullptr;
    QTabWidget *m_repoFileTabs = nullptr;
    QPushButton *m_repoFileCommitButton = nullptr;
    QPushButton *m_repoFilePullButton = nullptr;
    QHash<QString, QWidget *> m_openFileTabs; // repo-relative path -> editor tab

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
    QPushButton *m_pullUpdateButton = nullptr;
    QPushButton *m_pullMergeButton = nullptr;
    QPushButton *m_pullResolveButton = nullptr; // opens the conflict merge editor
    QPushButton *m_pullCloseButton = nullptr;
    QPushButton *m_pullDeleteButton = nullptr;
    bool m_pullDeleteConfirmPending = false;
    QListWidget *m_pullFiles = nullptr;
    QTextBrowser *m_pullDiff = nullptr;
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
    MarkdownEditor *m_pullComposer = nullptr;
    QPushButton *m_pullCommentButton = nullptr;
    QPushButton *m_pullApproveButton = nullptr;
    QPushButton *m_pullRequestChangesButton = nullptr;
    // Checks tab: action runs for this PR's commits + a manual trigger.
    QTableWidget *m_pullChecksTable = nullptr;
    QPlainTextEdit *m_pullChecksLog = nullptr;
    QPushButton *m_pullRunChecksButton = nullptr;
    QList<PullRequest> m_currentPulls;
    QHash<QString, QString> m_pullFileDiffs; // current PR: file path -> diff text
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
    QTimer *m_actionsSpinTimer = nullptr;        // animates the Actions tab while running
    int m_actionsSpinFrame = 0;
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
    // Per-repo "run actions on push" toggle. Mirrored repos default off; the
    // user opts in either on the Actions tab or the repo Settings tab — both
    // checkboxes drive the same state via setRepoActionsEnabled().
    QCheckBox *m_actionsEnabledCheck = nullptr;
    QCheckBox *m_settingsActionsCheck = nullptr;
    // Per-repo visibility toggle: when checked the repo is private (hidden from
    // the public catalog; browse/clone gated on the owner's view token).
    QCheckBox *m_repoPrivateCheck = nullptr;
    // Agent sessions assigned from issues.
    AgentStore *m_agentStore = nullptr;
    // Pool of agent runners so sessions execute in parallel (one process each)
    // instead of being serialized through a single runner.
    QList<AgentRunner *> m_agentRunners;
    QList<AgentSession> m_agentSessions;
    QList<int> m_agentQueue;
    int m_selectedAgentSessionId = -1;
    QTableWidget *m_agentTable = nullptr;
    QWidget *m_agentDetail = nullptr; // collapsible detail panel (hidden until a row is picked)
    QLabel *m_agentTitle = nullptr;
    QLabel *m_agentStatusPill = nullptr; // connected/working/done status
    QLabel *m_agentMeta = nullptr;
    QLabel *m_agentUsage = nullptr;
    QLabel *m_agentNetPanel = nullptr;   // live API-traffic graphic
    QPushButton *m_agentViewPrButton = nullptr;
    QPlainTextEdit *m_agentLog = nullptr;
    QPlainTextEdit *m_agentPromptEdit = nullptr;
    QPushButton *m_agentStopButton = nullptr;
    QPushButton *m_agentContinueButton = nullptr;
    QPushButton *m_agentDeleteButton = nullptr;
    QPushButton *m_agentTestApiKeyButton = nullptr;
    QLabel *m_agentOpenAiSpend = nullptr;
    QLabel *m_agentApiKeyStatus = nullptr;
    QLabel *m_agentClaudeSpend = nullptr;
    QLabel *m_agentClaudeStatus = nullptr;
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
    // Node-switch busy indicator (spinner on the top-nav node button).
    QTimer *m_nodeSwitchSpinTimer = nullptr;
    int m_nodeSwitchAngle = 0;
    bool m_nodeSwitching = false;      // a node switch's heavy load is running
    bool m_repoDetailLoading = false;  // re-entrancy guard for openRepoDetail
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
    QLabel *m_issueProgressValue = nullptr;
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
    QPushButton *m_issueVoteButton = nullptr;
    QLabel *m_issueCreditsLabel = nullptr;
    QPushButton *m_issueCommentButton = nullptr;
    QPushButton *m_issueAskAiButton = nullptr;
    QPushButton *m_issueAttachButton = nullptr;
    QPushButton *m_issueCloseButton = nullptr;
    QPushButton *m_issueLabelsButton = nullptr;
    QPushButton *m_issueMilestoneButton = nullptr;
    QPushButton *m_issuePriorityButton = nullptr;
    QPushButton *m_issueProgressButton = nullptr;
    QPushButton *m_issueBountyButton = nullptr;
    QPushButton *m_issueAssigneesButton = nullptr;
    QPushButton *m_issueDeleteButton = nullptr;
    QLabel *m_issueAgentValue = nullptr;
    QCheckBox *m_issueAgentCreatePrCheck = nullptr;
    QComboBox *m_issueAgentProvider = nullptr;   // OpenAI API | Claude API
    QPushButton *m_issueAssignAgentButton = nullptr;
    QPushButton *m_issueAgentViewButton = nullptr;
    QList<Issue> m_currentIssues;
    QList<IssueLabel> m_currentLabels;
    QList<IssueMilestone> m_currentMilestones;
    int m_currentIssueNumber = -1;
    bool m_issueDeleteConfirmPending = false;
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
#endif
};
