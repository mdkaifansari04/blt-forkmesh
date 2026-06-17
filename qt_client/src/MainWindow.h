#pragma once

#include "ChatBackend.h"
#include "ForkMeshIdentity.h"
#include "IssueStore.h"

#include <QHash>
#include <QIcon>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QPixmap>
#include <QSet>
#include <QUrl>

#include <functional>

class MessageRow;
class RepoHost;
class QButtonGroup;
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

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // Setup page
    QWidget *buildSetupPage();
    void startSession();
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
    QWidget *buildReposPanel();
    QWidget *buildIssuesSection();
    QWidget *buildChatSection();
    QWidget *buildSettingsSection();

    // Repo detail view (files + issues tabs), opened by clicking a repository.
    QWidget *buildRepoDetailSection();
    QWidget *buildRepoFilesPanel();
    QWidget *buildRepoOverviewPage();
    QWidget *buildRepoEditorPage();
    QWidget *buildRepoCommitsTab();
    QWidget *buildAboutSidebar();
    QWidget *buildPlaceholderTab(const QString &name);
    void openRepoDetail(int repoIndex);
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
    void promptNewIssue();
    void quickAddIssue();
    void copyIssueToClipboard();
    int availableCredits() const;     // 1 voting credit per hour online
    void voteOnCurrentIssue();
    void submitIssueVoteToInbox();
    void updateVoteUi();
    void addIssueComment();
    void attachIssueImage();
    void toggleIssueStatus();
    void deleteCurrentIssue();
    void editIssueLabels();
    void editIssueMilestone();
    void editIssueAssignees();
    void updateIssueActionState();
    QUrl issuesApiUrl(const RepositoryRecord &repo) const;
    void submitIssueCommentToInbox(const QString &body);
    void syncIssuesInbox();
    void chooseAvatar();
    void setSettingsAvatar(const QByteArray &pngData);
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
    MessageRow *addMessageRow(const ChatMessage &message);
    void rebuildConversationView();
    void scrollToBottom();
    void setChannels(const QStringList &channels);
    void setRoster(const QList<MemberInfo> &members);
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
    void publishRepositoryFiles(int index);
    void updateRepoRemoteInfo();
    QUrl filesApiUrl(const RepositoryRecord &repo) const;
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
    void onDisplayNameChanged(const QString &name);
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

    // Setup widgets
    QLineEdit *m_nameEdit;
    QLineEdit *m_handleEdit;
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
    QListWidget *m_repoList;
    QLabel *m_repoWebLink = nullptr;
    QLineEdit *m_repoRemoteEdit = nullptr; // local mirror path = push remote
    QLabel *m_repoRemoteHint = nullptr;
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
    QTimer *m_mirrorSyncTimer = nullptr;

    // Issues section widgets
    QComboBox *m_issuesRepoCombo = nullptr;
    QComboBox *m_issueStatusFilter = nullptr;
    QComboBox *m_issueLabelFilter = nullptr;
    QComboBox *m_issueMilestoneFilter = nullptr;
    QTableWidget *m_issueTable = nullptr;
    QWidget *m_issueDetail = nullptr;          // collapsible detail panel
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
    QPushButton *m_forkButton = nullptr;
    QPushButton *m_mirrorButton = nullptr;
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
    // Spinning refresh (rebuild) button in the nav rail.
    QPushButton *m_refreshButton = nullptr;
    QTimer *m_refreshSpinTimer = nullptr;
    int m_refreshAngle = 0;
    QLabel *m_issueTitle = nullptr;
    QLabel *m_issueMeta = nullptr;
    QLabel *m_issueReadonlyNote = nullptr;
    QLabel *m_issueAssigneesValue = nullptr;
    QLabel *m_issueLabelsValue = nullptr;
    QLabel *m_issueMilestoneValue = nullptr;
    QScrollArea *m_issueThreadScroll = nullptr;
    QWidget *m_issueThreadContainer = nullptr;
    QVBoxLayout *m_issueThreadLayout = nullptr;
    QPlainTextEdit *m_issueComposer = nullptr;
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
    QList<Issue> m_currentIssues;
    QList<IssueLabel> m_currentLabels;
    QList<IssueMilestone> m_currentMilestones;
    int m_currentIssueNumber = -1;
    QStringList m_pendingIssueAttachments; // images queued for the next comment

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
};
