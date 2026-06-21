#pragma once

#include "ChatBackend.h"
#include "ForkMeshIdentity.h"
#include "IssueStore.h"
#include "PullStore.h"
#include "ActionStore.h"
#include "ActionFile.h"
#include "AgentStore.h"
#include "AgentRunner.h"

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

// A configured mainnode the user can connect to. The client connects to one at
// a time; the favicon rail switches the active one.
struct ServerConfig {
    QString url;
    QString room;
    QString passphrase;
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
    QString bchAddress;
    QString mirrorPath;
    bool publishToNetwork = false;
    // Run .forkmesh/ workflows when a fork pushes to this repo's bare mirror.
    // Enabled by default; can be turned off per repo on the Actions tab. Pushed
    // workflow changes still require explicit approval before they run.
    bool actionsEnabled = true;
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

protected:
    void closeEvent(QCloseEvent *event) override;
    // Image drag-and-drop onto the inline issue comment composer.
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    // Setup page
    QWidget *buildSetupPage();
    void startSession();
    // Account = node identity. Registration (name + BCH + password + TOTP) gates
    // joining the network; the account name is the canonical repo owner.
    bool ensureNodeAccount(const QString &accountName, const QString &bch);
    bool runSignupFlow(const QString &accountName, const QString &bch);
    bool runLoginFlow(const QString &accountName);
    // Staged-join helpers: pick repos to mirror, then donate + poll.
    bool runRepoPickStep();
    bool runDonationStep(const QString &accountName);
    QJsonArray fetchCatalogRepos();
    int fetchNodesOnline();
    void mirrorCatalogRepo(const QString &owner, const QString &name,
                           const QString &cloneUrl);
    bool verifyTotpLogin(const QString &accountName, const QString &password,
                         const QString &totp);
    // Periodic signed heartbeat that keeps this node eligible for the reward
    // split and refreshes its payout BCH address.
    void sendNodeHeartbeat();
    // Admin: poll for newly-joined users and verify their email by hand (until a
    // real email service is wired up). Only active for accounts in ADMIN_NODES.
    void pollPendingUsers();
    void showAdminVerifyDialog();
    bool adminVerifyEmail(const QString &target);
    void verifyWallet(); // POST /verify-bch for the logged-in account
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
    void buildAndRelaunch(const QString &clientDir);
    void runUpdateStep(const QString &program, const QStringList &arguments,
                       const QString &workingDir, std::function<void()> onSuccess);
    void setUpdateStatus(const QString &status, bool isError = false);
    void persistProfile();

    // Chat page
    QWidget *buildChatPage();
    QWidget *buildServerRail();

    // Global donation nudge shown until this node sets a BCH address.
    QWidget *buildBchNotice();
    void updateBchNotice();
    void promptSetBchAddress();

    // Top breadcrumb: active server > current section.
    QWidget *buildBreadcrumb();
    void updateBreadcrumb();
    void updateConnectionStatus(); // top-right "● Connected · N nodes online"
    // Persistent network log docked at the bottom of the app.
    QWidget *buildNetworkLogDock();

    // Mainnode servers (favicon rail)
    void loadServers();
    void saveServers();
    void refreshServerRail();
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
    void hideNodeProfile();
    void checkNodeBalance();
    // Query the BCH network for a balance via the public Electrum/Fulcrum server
    // pool, trying servers in order and falling back on any failure.
    void queryBalanceFromElectrum(const QString &addr,
                                  const QByteArray &scriptHashHex,
                                  int serverIndex);
    QWidget *buildNodesPanel(); // left column: just the nodes
    QWidget *buildReposPanel(); // column: repos for the selected node
    // Fill the repositories column with the repos owned by the selected node.
    void selectNode(const QString &node);
    QWidget *buildIssuesSection();
    QWidget *buildChatSection();
    QWidget *buildSettingsSection();

    // Repo detail view (files + issues tabs), opened by clicking a repository.
    QWidget *buildRepoDetailSection();
    QWidget *buildRepoFilesPanel();
    QWidget *buildRepoOverviewPage();
    QWidget *buildRepoEditorPage();
    QWidget *buildRepoCommitsTab();
    void showCommit(const QString &hash); // open the commit diff detail view
    void showCommitList();                // back to the commits list
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
    void promptNewPull();
    void mergeCurrentPull();
    void closeCurrentPull();
    void syncPullsInbox();
    void submitPullToInbox(const PullRequest &pr);
    void updatePullActionState();
    QUrl pullsApiUrl(const RepositoryRecord &repo) const;
    // Agent sessions tab: local Codex/Claude Code runs assigned from issues.
    QWidget *buildAgentsTab();
    void initAgents();
    void reloadAgents();
    void refreshAgentTable();
    void showAgentSession(int sessionId);
    AgentSession *findAgentSession(int sessionId);
    const AgentSession *latestAgentSessionForIssue(int issueNumber) const;
    void assignIssueToAgent(const QString &provider);
    void deleteSelectedAgentSession();
    void testOpenAiAgentKey();
    void openAgentSessionFromIssue();
    void switchToAgentsTab(int sessionId);
    void processAgentQueue();
    void onAgentLog(int sessionId, const QString &text);
    void onAgentStatusChanged(int sessionId, const QString &status);
    void onAgentFinished(int sessionId, bool ok);
    void updateAgentActionState();
    void updateIssueAgentUi(const Issue &issue);
    AgentRunner::Config agentConfigForProvider(const QString &provider) const;
    QString agentProviderName(const QString &provider) const;
    // Actions (CI on push to the mirror) — lives as a tab inside the repo detail.
    QWidget *buildRepoActionsTab();
    void refreshRepoActions();           // workflows column + runs for the open repo
    QList<ActionWorkflow> availableWorkflowsForRepo(
        const RepositoryRecord &repo) const;
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
    void forkCurrentRepo();       // clone the open repo into your own node
    void downloadCurrentRepoZip();
    void setRepoDetailNotice(const QString &message, bool error = false);
    void refreshOpenRepoDetail(); // re-read the open repo after its mirror changes
    void updateRepoIssueCount();
    void loadRepoOverview(const QString &path);
    void showRepoOverview();
    void loadRepoFileTree();
    void openRepoFile(const QString &path);
    void loadRepoInfo();
    void loadBranchesAndTags();
    void loadFileSearchIndex();
    void loadAboutSidebar();
    void loadCommits();
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

    // Issues tab
    int issuesRepoIndex() const;                 // selected repo, or -1
    IssueStore issueStoreForCurrentRepo() const; // build a store for that repo
    void refreshIssuesRepoCombo();
    void reloadIssues();        // load issues + label/milestone filters from the store
    void refreshIssueList();    // apply filters into the list widget
    QWidget *makeIssueRow(const Issue &issue,
                          const QHash<QString, QString> &labelColors) const;
    void showIssue(int number); // render the selected issue's thread
    void renderIssueThread(const Issue &issue);
    void showIssueComposePage(QWidget *page);
    void removeIssueComposePage();
    void setIssueInlineNotice(const QString &message, bool error = false);
    void promptEditIssueTitle();
    void saveIssueTitleEdit();
    void cancelIssueTitleEdit();
    void promptNewIssue();
    void quickAddIssue();
    void copyIssueToClipboard();
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
    void editIssueAssignees();
    void saveIssueLabelsInline();
    void saveIssueMilestoneInline();
    void saveIssueAssigneesInline();
    void cancelIssueSidebarEditors();
    void updateIssueActionState();
    QUrl issuesApiUrl(const RepositoryRecord &repo) const;
    void submitIssueCommentToInbox(const QString &body);
    void syncIssuesInbox();
    void chooseAvatar();
    void setSettingsAvatar(const QByteArray &pngData);
    // Effective avatar bytes: the uploaded/generated one, or a deterministic
    // generated identicon when the user hasn't set one.
    QByteArray effectiveAvatar();
    void updateAvatarButton();
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
    void showMemberContextMenu(const QPoint &pos); // right-click a chat member
    void removeChatMember(const QString &id, const QString &name);
    // A conversation key is either a channel ("#general") or a direct chat
    // ("@<peerId>").
    void switchConversation(const QString &conversation);
    void openDirectChat(const QString &peerId, const QString &peerName);
    void refreshChannelList();
    void refreshDmList();
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
    void loadRepositories();
    void saveRepositories() const;
    void refreshRepositoryList();
    void promptAddRepository();
    void mirrorAdvertisedRepo(const QString &ownerName);
    void syncSelectedRepository();
    void syncRepository(int index, bool quiet = false);
    void autoSyncMirrors();
    void quickRebuildRestart();
    void changeMirrorLocation();
    void publishSelectedRepository();
    void publishRepository(int index, bool showDialogOnError = true);
    void updateRepoRemoteInfo();
    void updateRepoDetailStatus();
    QUrl hostWsUrl(const RepositoryRecord &repo) const;
    void startRepoHosts();
    void stopRepoHosts();
    void onRequestServed(const QString &owner, const QString &name, bool clone);
    void loadRepoStats();
    void saveRepoStats() const;
    QString repositorySource(const RepositoryRecord &repo) const;
    QString repositoryChannel(const RepositoryRecord &repo) const;
    QString repositoryMirrorRoot() const;
    QString repositoryWebUrl(const RepositoryRecord &repo) const;
    void updateRepoWebLink();
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

    // Mainnode favicon rail (far left of the chat page).
    QWidget *m_serverRail = nullptr;
    QButtonGroup *m_serverGroup = nullptr;
    QList<ServerConfig> m_servers;
    int m_activeServer = 0;
    QHash<QString, QPixmap> m_faviconCache; // host -> favicon

    // Donation nudge banner (no BCH address yet).
    QWidget *m_bchBanner = nullptr;
    QLabel *m_bchBannerLabel = nullptr;

    // Top breadcrumb bar (active server favicon + server > section).
    QLabel *m_breadcrumb = nullptr;
    QLabel *m_breadcrumbServerIcon = nullptr;
    QLabel *m_connectionStatus = nullptr; // top-right connection indicator
    QLabel *m_topMessage = nullptr;       // compact centered success/failure toast
    QTimer *m_topMessageTimer = nullptr;  // auto-clears the centered toast

    // Setup widgets
    QLineEdit *m_nameEdit;
    QLineEdit *m_bchEdit;
    QLabel *m_pubkeyLabel;
    QLineEdit *m_serverUrlEdit;
    QLineEdit *m_roomNameEdit;
    QLineEdit *m_passphraseEdit;
    QLabel *m_setupError;
    QPushButton *m_updateButton;
    QLabel *m_updateStatus;
    // Active build flow's status label and button (Quick update vs. Settings
    // rebuild), so the shared build steps report to the right place.
    QLabel *m_buildStatusLabel = nullptr;
    QPushButton *m_buildButton = nullptr;

    // Chat widgets
    QLabel *m_statusLine;
    QLabel *m_channelTitle;
    QLabel *m_encryptionLabel;
    QListWidget *m_channelList;
    QListWidget *m_repoList;             // repos for the selected node
    QListWidget *m_nodeList = nullptr;   // left column: the nodes themselves
    QString m_selectedNode;             // node whose repos fill the repos column
    QPushButton *m_syncRepoButton;
    QPushButton *m_publishRepoButton;
    QListWidget *m_dmList;
    QListWidget *m_memberList;
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
    QLabel *m_settingsAvatarPreview = nullptr;
    QPlainTextEdit *m_settingsLog = nullptr;
    QPushButton *m_rebuildButton = nullptr;
    QLabel *m_rebuildStatus = nullptr;
    QLineEdit *m_mirrorRootEdit = nullptr;
    QCheckBox *m_autostartCheck = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QLineEdit *m_codexApiKeyEdit = nullptr;
    QLineEdit *m_codexModelEdit = nullptr;
    QLineEdit *m_claudeApiKeyEdit = nullptr;
    QLineEdit *m_codexCommandEdit = nullptr;
    QLineEdit *m_claudeCommandEdit = nullptr;
    QLineEdit *m_agentContextEdit = nullptr;
    QLineEdit *m_agentMaxOutputEdit = nullptr;
    QTimer *m_mirrorSyncTimer = nullptr;

    // Issues section widgets
    QLineEdit *m_issueSearch = nullptr;
    QComboBox *m_issuesRepoCombo = nullptr;
    QComboBox *m_issueStatusFilter = nullptr;
    QComboBox *m_issueLabelFilter = nullptr;
    QComboBox *m_issueMilestoneFilter = nullptr;
    QTableWidget *m_issueTable = nullptr;
    QWidget *m_issueDetail = nullptr;          // collapsible detail panel
    QStackedWidget *m_issueDetailStack = nullptr;
    QWidget *m_issueComposePage = nullptr;
    QPushButton *m_issueDetailToggle = nullptr;
    QLineEdit *m_issueQuickAdd = nullptr;

    // Repo detail view
    int m_repoDetailIndex = -1;
    QButtonGroup *m_repoDetailTabs = nullptr;
    QPushButton *m_repoIssuesTab = nullptr;
    QStackedWidget *m_repoDetailStack = nullptr;
    // GitHub-style repo page: header actions, tabs, branch/search, About sidebar.
    QString m_repoBranch;
    RepoInfo m_repoInfo;
    QLabel *m_repoHeaderTitle = nullptr;
    QLabel *m_repoDetailNotice = nullptr;
    QLabel *m_repoDetailStatus = nullptr;
    QLabel *m_repoWebLink = nullptr;
    QLineEdit *m_repoRemoteEdit = nullptr; // local mirror path = push remote
    QLabel *m_repoRemoteHint = nullptr;
    QPushButton *m_forkButton = nullptr;
    QPushButton *m_mirrorButton = nullptr;
    QPushButton *m_downloadZipButton = nullptr;
    QPushButton *m_starButton = nullptr;
    QPushButton *m_branchButton = nullptr;
    QPushButton *m_tagsButton = nullptr;
    QLineEdit *m_fileSearch = nullptr;
    QCompleter *m_fileCompleter = nullptr;
    QLabel *m_aboutText = nullptr;
    QLabel *m_aboutTopics = nullptr;
    QLabel *m_langBar = nullptr;
    QLabel *m_langLegend = nullptr;
    QLabel *m_contributorsHeader = nullptr;
    QLabel *m_contributorsRow = nullptr;
    QListWidget *m_commitsList = nullptr;
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
    int m_currentCommitRow = -1; // row in m_commitsList the detail view is showing
    // Files view: a GitHub-style overview (latest commit + file list + README)
    // that switches to an explorer-tree + editor-tabs view when a file is open.
    QStackedWidget *m_filesStack = nullptr; // 0 overview, 1 editor
    QString m_overviewPath;                 // current directory in the overview
    int m_treeLoadedForIndex = -1;          // repo whose explorer tree is built
    QLabel *m_commitBar = nullptr;
    QPushButton *m_historyButton = nullptr;
    QLabel *m_overviewCrumb = nullptr;
    QListWidget *m_overviewList = nullptr;
    QTextBrowser *m_readmeView = nullptr;
    QTreeWidget *m_repoFileTree = nullptr;
    QTabWidget *m_repoFileTabs = nullptr;
    QHash<QString, QWidget *> m_openFileTabs; // repo-relative path -> editor tab

    // Pull requests tab
    QTableWidget *m_pullTable = nullptr;
    QLineEdit *m_pullSearch = nullptr;
    QPushButton *m_pullNewButton = nullptr;
    QPushButton *m_pullSyncButton = nullptr;
    QWidget *m_pullDetail = nullptr;
    QLabel *m_pullTitle = nullptr;
    QLabel *m_pullMeta = nullptr;
    QLabel *m_pullDesc = nullptr;
    QPushButton *m_pullMergeButton = nullptr;
    QPushButton *m_pullCloseButton = nullptr;
    QListWidget *m_pullFiles = nullptr;
    QTextEdit *m_pullDiff = nullptr;
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
    int m_selectedRunId = -1;
    QListWidget *m_actionWorkflowList = nullptr; // available actions (left column)
    QString m_selectedWorkflowFilter;            // workflow path filter, empty = all
    QTimer *m_actionsSpinTimer = nullptr;        // animates the Actions tab while running
    int m_actionsSpinFrame = 0;
    QTableWidget *m_actionsTable = nullptr;
    QLabel *m_actionRunTitle = nullptr;
    QLabel *m_actionRunMeta = nullptr;
    QLabel *m_actionApprovalBanner = nullptr;
    QPlainTextEdit *m_actionLog = nullptr;
    QTextEdit *m_actionDiff = nullptr;
    QWidget *m_actionApprovalBar = nullptr;
    QPushButton *m_actionApproveButton = nullptr;
    QPushButton *m_actionRejectButton = nullptr;
    // Settings: variables/secrets table.
    QTableWidget *m_varsTable = nullptr;
    // Repos panel: per-repo "run actions on push" toggle for the selection.
    QCheckBox *m_actionsEnabledCheck = nullptr;
    // Agent sessions assigned from issues.
    AgentStore *m_agentStore = nullptr;
    AgentRunner *m_agentRunner = nullptr;
    QList<AgentSession> m_agentSessions;
    QList<int> m_agentQueue;
    int m_selectedAgentSessionId = -1;
    QTableWidget *m_agentTable = nullptr;
    QLabel *m_agentTitle = nullptr;
    QLabel *m_agentMeta = nullptr;
    QLabel *m_agentUsage = nullptr;
    QPlainTextEdit *m_agentLog = nullptr;
    QPlainTextEdit *m_agentPromptEdit = nullptr;
    QPushButton *m_agentStopButton = nullptr;
    QPushButton *m_agentDeleteButton = nullptr;
    QPushButton *m_agentTestApiKeyButton = nullptr;
    QLabel *m_agentApiKeyStatus = nullptr;
    QPushButton *m_agentSendPromptButton = nullptr;
    // Spinning refresh (rebuild) button in the nav rail.
    QPushButton *m_refreshButton = nullptr;
    QTimer *m_refreshSpinTimer = nullptr;
    int m_refreshAngle = 0;
    QLabel *m_issueTitle = nullptr;
    QLineEdit *m_issueTitleEditor = nullptr;
    QPushButton *m_issueTitleEditButton = nullptr;
    QPushButton *m_issueTitleSaveButton = nullptr;
    QPushButton *m_issueTitleCancelButton = nullptr;
    QLabel *m_issueMeta = nullptr;
    QLabel *m_issueReadonlyNote = nullptr;
    QLabel *m_issueInlineNotice = nullptr;
    QLabel *m_issueAssigneesValue = nullptr;
    QLabel *m_issueLabelsValue = nullptr;
    QLabel *m_issueMilestoneValue = nullptr;
    QStackedWidget *m_issueAssigneesStack = nullptr;
    QStackedWidget *m_issueLabelsStack = nullptr;
    QStackedWidget *m_issueMilestoneStack = nullptr;
    QLineEdit *m_issueAssigneesEdit = nullptr;
    QLineEdit *m_issueLabelsEdit = nullptr;
    QComboBox *m_issueMilestoneEdit = nullptr;
    QScrollArea *m_issueThreadScroll = nullptr;
    QWidget *m_issueThreadContainer = nullptr;
    QVBoxLayout *m_issueThreadLayout = nullptr;
    MarkdownEditor *m_issueComposer = nullptr;
    QPushButton *m_issueNewButton = nullptr;
    QPushButton *m_issueSyncButton = nullptr;
    QPushButton *m_issueCopyButton = nullptr;
    QPushButton *m_issueVoteButton = nullptr;
    QLabel *m_issueCreditsLabel = nullptr;
    QPushButton *m_issueCommentButton = nullptr;
    QPushButton *m_issueAttachButton = nullptr;
    QPushButton *m_issueCloseButton = nullptr;
    QPushButton *m_issueLabelsButton = nullptr;
    QPushButton *m_issueMilestoneButton = nullptr;
    QPushButton *m_issueAssigneesButton = nullptr;
    QPushButton *m_issueDeleteButton = nullptr;
    QLabel *m_issueAgentValue = nullptr;
    QCheckBox *m_issueAgentCreatePrCheck = nullptr;
    QPushButton *m_issueAssignCodexButton = nullptr;
    QPushButton *m_issueAssignClaudeButton = nullptr;
    QPushButton *m_issueAgentViewButton = nullptr;
    QList<Issue> m_currentIssues;
    QList<IssueLabel> m_currentLabels;
    QList<IssueMilestone> m_currentMilestones;
    int m_currentIssueNumber = -1;
    bool m_issueDeleteConfirmPending = false;
    QStringList m_pendingIssueAttachments; // images queued for the next comment

    // Node profile panel widgets + the node it currently shows.
    QWidget *m_nodeProfilePanel = nullptr;
    QLabel *m_profileAvatar = nullptr;
    QLabel *m_profileName = nullptr;
    QLabel *m_profileStatus = nullptr;
    QLabel *m_profilePlatform = nullptr;
    QLabel *m_profileMirrors = nullptr;
    QLabel *m_profileNote = nullptr;
    QLabel *m_profileStats = nullptr; // node stats, moved here from the node list
    QLabel *m_profileNodeKey = nullptr;
    QLabel *m_profileBchAddr = nullptr;
    QLabel *m_profileQr = nullptr;
    QWidget *m_profileBchSection = nullptr;
    QLabel *m_profileBalance = nullptr;
    QPushButton *m_profileBalanceButton = nullptr;
    QPushButton *m_profileVerifyButton = nullptr;
    QLabel *m_profileEligibility = nullptr;
    QPushButton *m_profileMessageButton = nullptr;
    QString m_profileNodeId;
    QString m_profileNodeName;
    QString m_profileBchValue;

    QStringList m_channels;
    QList<RepositoryRecord> m_repositories;
    QList<RepoHost *> m_repoHosts;
    QList<MemberInfo> m_homeRoster;
    // "owner/name" -> { times served through the mainnode, clones }.
    QHash<QString, QPair<int, int>> m_repoStats;
    QSet<int> m_syncingRepos;
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
    QString m_userName;
    QByteArray m_userAvatar;
    QString m_typingConversation;
    QTimer *m_typingStopTimer;
    QTimer *m_homeStatsTimer = nullptr;
    qint64 m_connectedAtMs = 0;
    qint64 m_totalConnectionMs = 0;

    // Registered account/node identity for this session.
    bool m_accountAuthenticated = false;
    QString m_accountName;
    bool m_accountBchVerified = false;
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
};
