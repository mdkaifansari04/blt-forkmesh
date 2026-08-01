#pragma once

#include "ChatBackend.h"
#include "DiscussionInboxBackoff.h"
#include "NetworkBackoff.h"
#include "DiscussionStore.h"
#include "ForkMeshIdentity.h"
#include "IssueStore.h"
#include "ProjectStore.h"
#include "PullStore.h"
#include "CoveStore.h"
#include "ActionStore.h"
#include "ActionFile.h"
#include "AccountCapability.h"
#include "AgentStore.h"
#include "AgentRunner.h"
#include "ClaudeSessionScan.h"
#include "DirectorySizeScan.h"
#include "RepoSecurity.h"
#include "RepoContributionSnapshot.h"
#include "MirrorCrypto.h"








struct AgentScannerState {
    qint64 lastActivityMs = 0;
    double intensity = 0.0;
};





struct AgentDiffStat {
    int files = -1;
    int ahead = -1;
    int behind = -1;


    bool conflicted = false;





    QString worktree;
    int dirty = -1;



    int added = -1;
    int removed = -1;
};






struct MirrorSelfSnapshot {
    QString key;
    MirrorAdvert advert;
    QString servedCommit;
    int pendingPush = 0;



    qint64 gatheredMs = 0;
};

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QMetaType>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QTextBlockUserData>
#include <QTextCursor>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <limits>
#include <memory>




class FooterLogLineData : public QTextBlockUserData {
public:
    explicit FooterLogLineData(QString line) : rawLine(std::move(line)) {}
    QString rawLine;
};

class MessageRow;
class MarkdownEditor;
class PullBadgeWidget;

namespace forkmesh::ui {
class ActivityRailButton;
class AgentDotMatrix;
class NodeDotMatrix;
class RelaySpeedDot;
class ActionRunStrip;
}
using forkmesh::ui::ActionRunStrip;
using forkmesh::ui::ActivityRailButton;
using forkmesh::ui::AgentDotMatrix;
using forkmesh::ui::NodeDotMatrix;
using forkmesh::ui::RelaySpeedDot;
class PacmanProgress;
class TerminalWidget;
class ClaudeIdeBridge;
class ClaudeStreamSession;
class CodexAppServerSession;
class StallWatchdog;
class ClaudeTranscriptView;
class RepoHost;
class NodeEventSocket;
class WorldSpeechBridge;
class OfficeChannelMirror;
class PrivateMirrorMaterialization;
class ActionRunner;
class QButtonGroup;
class QGridLayout;
class QFileSystemWatcher;
class QSplitter;
class QTextEdit;
class QCheckBox;
class QComboBox;
class QCompleter;
class QAbstractItemView;
class QDateEdit;
class QDialog;
class QStringListModel;
class QGraphicsOpacityEffect;
class QFrame;
class QLabel;
class QMouseEvent;
class QContextMenuEvent;
class QAbstractButton;
class QAction;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QPlainTextEdit;
class QImage;
class QProgressBar;
class QPropertyAnimation;
class QPushButton;
class QToolButton;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QSystemTrayIcon;
class QTableWidget;
class QTableWidgetItem;
class QTabWidget;
class QTextBrowser;
class QTextEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QProcess;
class QTemporaryDir;
class QVBoxLayout;
class QCheckBox;
class QHBoxLayout;
class PublicMirrorMaterialization;
namespace forkmesh::control {
struct MirrorActionsConfigurationRequest;
struct AgentCliCredentials;
}
namespace forkmesh::ui { class DiffFileNavigator; }



struct ServerConfig {
    QString url;
    QString room;
};



struct RepoInfo {
    QString about;
    QString website;
    QString language;
    QStringList topics;
    int forks = 0;
    int stars = 0;
    int mirrors = 1;
    QString defaultBranch;
    QHash<QString, QString> contributorAvatars;
};

struct RepositoryRecord {
    QString owner;
    QString name;
    QString description;
    QString cloneUrl;
    QString localPath;
    QString solanaAddress;
    QString mirrorPath;




    QString privateReplicaId;



    QString publicArchiveId;
    bool publishToNetwork = false;



    bool isPrivate = false;



    bool actionsEnabled = true;





    bool externallyManagedActions = false;
    QString externalActionsSource;
    QString externalActionsRef;




    QStringList disabledWorkflows;





    QStringList workflowNodes;



    bool secretScanningEnabled = true;


    bool previewOnly = false;
    qint64 hostedSinceMs = 0;
    qint64 lastSyncMs = 0;
    qint64 publishedAtMs = 0;
};




struct NotificationLink {

    QString kind;
    QString owner;
    QString name;
    int number = -1;
    QString ref;

    bool isValid() const { return !kind.isEmpty(); }
};
Q_DECLARE_METATYPE(NotificationLink)

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;




    void openCloudflareSetupFromSystemLink(const QString &target = {});


    static void applyTheme();
    void refreshThemedIcons();





    static QString autolinkReferences(const QString &markdown);








    static const Issue *looperPickNext(
        const QList<Issue> &issues,
        const std::function<bool(int)> &hasLocalSession);

#ifdef FORKMESH_WINDOW_TESTS
    using TestIssueHistoryDeleteRunner =
        std::function<bool(int number, QString *error)>;
    QString testMostRecentUnreadConversation(
        const QString &currentConversation, const QStringList &channels,
        const QSet<QString> &unread,
        const QHash<QString, qint64> &lastMessageMs)
    {
        const QString savedCurrent = m_currentConversation;
        const QStringList savedChannels = m_channels;
        const QStringList savedOpenDms = m_openDms;
        const QSet<QString> savedUnread = m_unread;
        const auto savedHistory = m_history;

        m_currentConversation = currentConversation;
        m_channels = channels;
        m_openDms.clear();
        m_unread = unread;
        m_history.clear();
        for (auto it = lastMessageMs.constBegin(); it != lastMessageMs.constEnd();
             ++it) {
            ChatMessage message;
            message.timestampMs = it.value();
            m_history[it.key()].append(message);
        }
        const QString result = mostRecentUnreadConversation();

        m_currentConversation = savedCurrent;
        m_channels = savedChannels;
        m_openDms = savedOpenDms;
        m_unread = savedUnread;
        m_history = savedHistory;
        return result;
    }
    void testSetRoster(const QList<MemberInfo> &members) { setRoster(members); }
    void testResetRosterForAlerts()
    {
        m_homeRoster.clear();
        m_removedPeerIds.clear();
        m_peerLastSeenMs.clear();
    }


    void testAgePeerSightings(qint64 ageMs)
    {
        for (auto it = m_peerLastSeenMs.begin(); it != m_peerLastSeenMs.end(); ++it)
            *it -= ageMs;
    }
    QList<MemberInfo> testHomeRoster() const { return m_homeRoster; }




    void testSetHomeRosterAndReloadMirrorPanel(const QList<MemberInfo> &members)
    {
        m_homeRoster = members;
        loadMirrorNodesPanel();
    }
    void testSetNodeAlertGraceUntilMs(qint64 value) { m_nodeAlertGraceUntilMs = value; }
    QStringList testNetworkLog() const { return m_networkLog; }
    void testResetNetworkLog();
    void testLogSystem(const QString &text) { logSystem(text); }


    void testRecordUiStall(qint64 peakMs, const QString &blockingCall,
                           const QString &backtrace)
    {
        onUiStall(peakMs, blockingCall, backtrace);
    }
    QString testStallFixPrompt() const { return stallFixPrompt(); }


    bool testDraftStallPromptInComposer();
    QString testQuickAddText() const;


    void testShowSettingsSection() { showSection(1); }
    void testShowLogSection() { showSection(4); }
    void testShowHostsSection() { showSection(7); }
    void testSetDirectoryUserNodes(const QString &user,
                                   const QStringList &nodes);
    void testShowNodesSection();
    QStringList testNodeDirectoryNames() const;


    QStringList testChatMemberNames(const QString &conversation);
    void testRenderNetworkRepos(const QJsonArray &repos);
    QStringList testNetworkRepoNames() const;
    QString testNetworkRepoActionText(int row) const;
    QString testNetworkRepoMirrorHeader() const;


    QStringList testNetworkRepoColumns() const;
    QString testNetworkRepoCellText(int row, const QString &header) const;

    int testReposNavBadgeCount() const;
    void testRebuildNetworkLogView() { rebuildNetworkLogView(); }


    QStringList testLogFilterChipLabels() const;
    void testSetLogFilter(const QString &category)
    {
        m_logFilter = category;
        rebuildLogFilterButtons();
        rebuildNetworkLogView();
    }
    QString testLogBadgeFor(const QString &storedLine) const
    {
        return logBadgeFor(storedLine);
    }
    QTextBrowser *testNetworkLogView() const { return m_settingsLog; }
    QTextEdit *testFooterLogView() const { return m_footerUpdateLog; }
    bool testFaviconCached(const QString &host) const
    {
        return m_faviconCache.contains(host);
    }
    void testScrollNetworkLogToTop() { onNetworkLogScrolled(0); }
    QStringList testQuickUpdatePullArguments(const QString &clientDir) const;


    QStringList testBuildAndPreviewSteps(const QString &gitDir,
                                         const QString &previewDir,
                                         const QString &clientDir,
                                         const QString &buildDir,
                                         const QString &commit,
                                         bool haveWorktree) const;
    void testSetSetupInputs(const QString &name, const QString &solana);
    void testSetAccountFlowResult(bool result, bool desktopCapable = true)
    {
        m_testUseAccountFlowResult = true;
        m_testAccountFlowResult = result;
        m_testAccountFlowDesktopCapable = desktopCapable;
        m_testEnsureNodeAccountCalls = 0;
    }
    void testEnableSessionStartBypass(bool value)
    {
        m_testBypassServerStart = value;
        if (value) {

            m_pendingSilentAuth = false;
            m_pendingRestoreRepoIndex = -1;
            m_deferredStartupRun = true;
            m_startupAuthResolved = true;
        }
    }
    void testMarkDeferredStartupRunning()
    {
        m_deferredStartupRun = true;
        m_startupAuthResolved = false;
    }
    void testRunDeferredStartupNow() { runDeferredStartup(); }
    void testStartSession() { startSession(); }
    void testEnablePaidMirroring() { enablePaidMirroring(); }
    int testAccountFlowCalls() const { return m_testEnsureNodeAccountCalls; }
    int testStackIndex() const;



    bool testSignInButtonVisible() const;
    void testRefreshSignInButton() { updateSignInButton(); }
    QString testUserName() const { return m_userName; }
    QString testAccountName() const { return m_accountName; }
    bool testChatIdentityIsGuest() const { return chatIdentityIsGuest(); }
    QString testChatDisplayName() const { return chatDisplayName(); }
    QString testMachineNodeName() const { return machineNodeName(); }
    QString testSavedSolanaAddress() const;
    bool testAccountAuthenticated() const { return m_accountAuthenticated; }
    QString testAccountTier() const { return m_accountTier; }
    bool testHasOwnerSigningCapability() const
    {
        return hasOwnerSigningCapability(m_accountName);
    }



    Q_INVOKABLE bool testColumnsBecomeResizable();



    Q_INVOKABLE bool testSpreadsheetResize();



    Q_INVOKABLE bool testSpreadsheetResizeAfterMove();


    Q_INVOKABLE bool testAgentListChromeHidden() const;


    Q_INVOKABLE QString testAgentColumnLayout() const;
    Q_INVOKABLE int testAddLocalRepository(const QString &owner, const QString &name,
                                           const QString &localPath);
    Q_INVOKABLE bool testOpenRepository(int index);


    Q_INVOKABLE QString testRepoDefaultBranch() const;



    Q_INVOKABLE void testRefreshOpenRepoDetail() { refreshOpenRepoDetail(); }

    Q_INVOKABLE QString testRepoActionsTabText() const;

    Q_INVOKABLE QString testRepoBranchesButtonText() const;


    Q_INVOKABLE bool testShowRepoIssuesTab();
    Q_INVOKABLE bool testSaveRepoAboutMetadata(const QString &about,
                                               const QString &website)
    {
        return saveRepoAboutMetadata(about, website, nullptr);
    }


    Q_INVOKABLE int testProvisionNewRepository(const QString &dest,
                                               const QString &name,
                                               const QString &description,
                                               const QString &firstPrompt,
                                               bool addReadme,
                                               bool isPrivate = false)
    {
        return provisionNewRepository(dest, name, description, firstPrompt,
                                      addReadme, isPrivate, nullptr);
    }
    int testAddPublishedRepository(const QString &owner, const QString &name,
                                   const QString &mirrorPath);



    QString testRepoAgentGitDir(int index) const
    {
        return (index < 0 || index >= m_repositories.size())
                   ? QString()
                   : repoAgentGitDir(m_repositories.at(index));
    }
    void testPublishRepository(int index) { publishRepositoryNow(index, false); }
    void testStartRepoHosts() { startRepoHosts(); }
    void testStopRepoHosts() { stopRepoHosts(); }
    int testRepoTabContentTop();
    int testRepoTabGapAroundIssues() const;
    int testIssueLooperGapFromNewIssueButton() const;
    bool testIssueLooperRowAligned() const;
    int testTopNavTrailingGap() const;
    QString testRepoGitDir() const { return repoGitDir(); }
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





    Q_INVOKABLE int testQuickAddIssueNoAgent(const QString &title);
    bool testIssueDetailVisible() const
    {
        return m_issueDetail && m_issueDetail->isVisible();
    }



    QString testQuickAddAgentProvider() const;
    QString testIssueAgentProvider() const;
    void testSetDefaultAgentProvider(const QString &provider);
    void testSetQuickAddAgentProvider(const QString &provider);
    QStringList testQuickAddModelLabels() const;
    bool testQuickAddModelVisible() const;
    bool testQuickAddModelEditable() const;
    // issue #272: open the Worktrees tab on a branch, rebuild the panel (as an
    // "Update from main" merge does), and read back which worktree stays selected
    // so a test can prove the detail pane doesn't go blank after a refresh.
    void testSwitchToWorktree(const QString &branch)
    {
        showOverviewWorktrees();
        loadWorktreesPanel();
        selectWorktreeRow(branch);
    }
    void testReloadWorktreesPanel() { loadWorktreesPanel(); }
    QString testSelectedWorktreeBranch() const { return m_worktreeSelectedBranch; }



    QString testWorktreeBranchLabel() const;


    QString testWorktreeAheadBehindText(const QString &branch) const;
    QStringList testWorktreeBranches() const;



    QString testArrowOnWorktrees(bool down);


    int testWorktreesTabIndex() const { return m_worktreesTabIndex; }
    void testClickRepoDetailTab(int id);
    bool testOpenMostRecentCommit();



    bool testWorktreesTableHasKeyboardFocus() const;



    int testReleasesTabIndex() const { return m_releasesTabIndex; }
    int testMirrorNodesTabIndex() const { return m_mirrorNodesTabIndex; }
    int testControlNodeSectionIndex() const { return kControlNodeSectionIndex; }
    void testShowControlNode() { showSection(kControlNodeSectionIndex); }
    int testOrganizationTasksSectionIndex() const
    {
        return kOrganizationTasksSectionIndex;
    }
    void testShowOrganizationTasks()
    {
        showSection(kOrganizationTasksSectionIndex);
    }
    void testApplyOrganizationTasks(const QJsonObject &payload)
    {
        applyOrganizationTasks(payload);
    }
    bool testReleasesTableHasKeyboardFocus() const;
    bool testMirrorNodesTableHasKeyboardFocus() const;


    Q_INVOKABLE QStringList testMirrorNodeRows() const;
    bool testMirrorNodesOnlineOnlyChecked() const;
    void testSetMirrorNodesOnlineOnly(bool checked);
    QString testMirrorNodeCellText(const QString &nodeName, int column) const;
    QString testMirrorNodeCellToolTip(const QString &nodeName, int column) const;



    QString testFleetBinaryInstallRemoteCommand(bool reinstall,
                                                qsizetype *uploadByteCount,
                                                QString *errorOut);
    QString testDirectBinaryInstallRemoteCommand(qsizetype *uploadByteCount,
                                                 QString *errorOut);




    void testReloadBranchesPanel();
    QString testBranchWorktreePath(const QString &branch) const;
    // Compact Branch-cell data as
    // files|added|removed|worktree|conflict|updated|behind|ahead.
    QString testBranchVisualBadges(const QString &branch) const;
    QString testBranchHealthIcon(const QString &branch) const;
    bool testBranchesUseCompactColumns() const;
    bool testBranchesKeepFlexibleNameColumn() const;
    bool testBranchDelegatePaintsSingleTextLayer(const QString &branch) const;
    bool testBranchSelectedTextColorIsReadable(const QString &branch) const;
    // Inject an agent session so a test can prove the branches list surfaces the
    // issue/agent a branch is attached to (adhoc #191).
    void testAddAgentSession(const AgentSession &session)
    {
        for (AgentSession &existing : m_agentSessions) {
            if (existing.id == session.id) {
                existing = session;
                return;
            }
        }
        m_agentSessions.append(session);
    }


    void testOpenAgentsOverview() { openAgentsOverview(); }
    void testRefreshAgentDotMatrix() { refreshAgentDotMatrix(); }
    // Whether the leading Branch cell for `branch` carries an icon, so a test can
    // prove the list stamps agent status at the row's left edge (adhoc #251).
    bool testBranchAttachmentHasIcon(const QString &branch) const;
    // Branch names in row order, so a test can prove the default branch
    // is pinned to the top of the list regardless of commit recency (adhoc #185).
    QStringList testBranchRowOrder() const;
    int testBranchesTabIndex() const { return m_branchesTabIndex; }
    int testOverviewBodyPage() const;
    bool testClickBranchRowInOverview(const QString &branch);
    bool testBranchesPanelOwnsDiffView() const;
    // True only when Git owns the full repository workspace: its rail entry is
    // selected, every Code-only chrome band is hidden, and no registered diff
    // viewer outside the Git stack is visible.
    bool testGitWorkspaceIsExclusive() const;
    // Follow a branch link and read back the branch the table landed on right
    // away — no event pumping — so a test can prove the click doesn't wait on the
    // panel's off-thread git reads (adhoc #420).
    QString testSwitchToBranchImmediateSelection(const QString &branch);
    // Take the worktree route and expose the branch the Git view is browsing,
    // proving worktrees no longer open a second diff surface.
    QString testSwitchToWorktreeGitBranch(const QString &branch)
    {
        switchToWorktree(branch);
        return m_repoBranch;
    }
    // The branch whose history the Git view's graph is browsing (empty = HEAD),
    // so a test can prove a branch link lands the graph on that branch.
    QString testBrowsedBranch() const { return m_repoBranch; }
    // Take the agent detail page's "Branch" route, so a test can prove it binds
    // the Git view to that session's own repository before opening its branch
    // there — the sessions list is global (adhoc #131).
    void testSwitchToAgentBranch(int sessionId) { switchToAgentBranch(sessionId); }



    int testCommitWorkspacePage() const;
    QString testBranchDiffBranch() const { return m_branchDiffBranch; }
    QStringList testBranchDiffFiles() const { return m_branchDiffFilePaths; }
    // Paths rendered in the universal CHANGES tree, so branch tests can prove
    // the range's files appear without swapping to a second navigator.
    QStringList testSourceControlPaths() const;
    // Select a CHANGES row and report whether the right-hand diff navigation
    // targeted that exact file.
    bool testClickSourceControlPath(const QString &path);
    // Which page each half of the Git view's left column shows. Both stay on the
    // universal source-control panel and commit graph for every diff kind.
    int testGitFilesSlotPage() const;
    int testGitHistorySlotPage() const;
    // The base end of the "<branch> \xE2\x86\x92 <base>" compare indicator beside
    // the graph's branch button, or empty while it's hidden — so a test can
    // prove browsing a branch shows what it is being compared against, and that
    // the base is switchable (adhoc #16).
    QString testCompareIndicatorText() const;
    // Point the compare indicator's base dropdown at another branch, so a test
    // can prove the range re-diffs against it (adhoc #16).
    void testSetCompareBase(const QString &base);
    // Click the range pane's close button, so a test can prove the left column
    // goes back to the working tree when the review is dismissed (adhoc #110).
    void testCloseBranchRange();
    // Click the activity rail's Git entry, so a test can prove it always lands
    // on the default branch's working-tree view.
    void testClickRailGitButton();
    int testGitPendingSyncCount() const;
    void testNavigateBack() { navigateBack(); }
    void testNavigateForward() { navigateForward(); }
    QString testNavBackToolTip() const;
    QString testNavForwardToolTip() const;
    // Click the range pane's "Merge to main" (deleteAll=false) or "Merge & delete
    // all" button once it's live, so a test can prove merging from the review
    // closes it (adhoc #119). False when the button never became clickable.
    bool testClickBranchReviewMerge(bool deleteAll);
    // issue #291: when an agent task's worktree/PR lands in the base branch the
    // session is flagged "merged" on its Status column and detail page. Drive the
    // eager in-app merge path (the one mergeWorktreeIntoMain / mergeCurrentPull
    // take) for `branch`, then read back the rendered Status-cell text and the
    // persisted merged flag, so a test can prove the note appears.
    bool testMarkAgentBranchMerged(const QString &branch)
    {
        return markAgentSessionsMerged(0, branch);
    }
    QString testAgentStatusCellText(int sessionId) const;
    QString testAgentDetailTitleText() const;
    bool testAgentDetailTitleWraps() const;
    QString testRenderAgentDetailTitle(const QString &text);
    // adhoc #403: the badge data the Status cell hands its branch chip, read back
    // as "files|dirty|worktree|behind|ahead", so a test can prove the chip's
    // file and visible branch-health markers are fed from the session's diff stat.
    QString testAgentStatusCellBadges(int sessionId, const AgentDiffStat &stat) const;
    bool testAgentSessionMerged(int sessionId) const;
#endif






    ChatBackend *currentBackend() const { return m_backend; }
    bool headlessConnected() const;
    QString headlessNodeName() const;


    void headlessStart(const QString &name, const QString &solana = QString());





    void setHeadlessMode(bool headless);

    void headlessSyncNow();




    void headlessUpdateRestart();
    QStringList headlessStatusLines() const;


    QStringList headlessClaudeAuth(const QStringList &args);
    QStringList headlessRosterLines() const;
    QStringList headlessRepoLines() const;
    QStringList headlessMirrorLines() const;



    QString headlessResourceLine() const;

signals:


    void backendAttached(ChatBackend *backend);

protected:
    void closeEvent(QCloseEvent *event) override;


    void changeEvent(QEvent *event) override;


    void showEvent(QShowEvent *event) override;



    bool eventFilter(QObject *obj, QEvent *event) override;
    Qt::Edges resizeEdgesAtGlobalPos(const QPoint &globalPos) const;
    bool handleFramelessResizeEvent(QObject *obj, QEvent *event);
    void updateFramelessResizeCursor(Qt::Edges edges);




    bool maybeShowSendToPromptMenu(QObject *obj, QContextMenuEvent *ce);



    void appendTextToActivePrompt(const QString &text);

    void resizeEvent(QResizeEvent *event) override;

private:
    static constexpr int kNetworkReposSectionIndex = 11;
    static constexpr int kNetworkDiagnosticsSectionIndex = 12;
    static constexpr int kNodesSectionIndex = 13;
    static constexpr int kControlNodeSectionIndex = 14;
    static constexpr int kOrganizationTasksSectionIndex = 15;


    QWidget *buildSetupPage();
    void startSession();




    void runDeferredStartup();
    bool m_deferredStartupStarted = false; // showEvent armed the triggers
    bool m_deferredStartupRun = false;     // runDeferredStartup already ran
    // The top-bar sign-in pill stays hidden until the launch-time silent-auth
    // lookup has completed. m_deferredStartupRun flips before that lookup while
    // restoring the last view, so it cannot safely gate the pill by itself.
    bool m_startupAuthResolved = false;
    bool m_framelessResizeCursorActive = false;
    int m_pendingRestoreRepoIndex = -1;
    bool m_pendingSilentAuth = false;
    bool m_headless = false;






    QTimer *m_headlessRegisterRetryTimer = nullptr;
    int m_headlessRegisterAttempt = 0;
    void scheduleHeadlessRegisterRetry(const QString &accountName);



    bool m_freshInstall = false;



    QString m_pendingAutoOpenRepoKey;


    bool ensureNodeAccount(const QString &accountName, const QString &solana);


    bool authenticateSilently(const QString &accountName);







    bool registerNodeAccountSilently(const QString &accountName);




    bool runSignupFlow(const QString &accountName, const QString &solana);
    bool runLoginFlow(const QString &accountName);
    QJsonArray fetchCatalogRepos();
    int fetchNodesOnline();
    bool hasActiveAccountSession() const;
    bool hasOwnerSigningCapability(
        const QString &signerAccount = QString()) const;
    void setDesktopCapability(const QString &accountName, bool capable);
    bool restoreDesktopCapability(const QString &accountName);
    void mirrorCatalogRepo(const QString &owner, const QString &name,
                           const QString &cloneUrl, bool isPrivate = false);


    QString hostedCloneUrl(const QString &owner, const QString &name) const;


    void ensureFlagshipRepo();




    bool serviceManagedCheckout(const QString &localPath) const;





    bool verifyTotpLogin(const QString &email, const QString &password,
                         const QString &totp, const QString &accountName,
                         bool *fatal = nullptr);


    void sendNodeHeartbeat();
    QJsonObject emailNotificationPreferencesPayload() const;



    void showNodeClaimCode(const QString &user, const QString &code);




    void requestNodeOwnership();
    void showOwnershipTransferPrompt(const QString &admin);
    void submitOwnershipTransferDecision(bool approve);



    void promptHostLinkCode(const QString &code);
    void submitHostLinkCode(const QString &code);




    void promptLinkNodeToUser();
    void submitLinkNodeToUser(const QString &identifier, const QString &password);





    void openLinkNodeInBrowser();
    void pollLinkNodeGrant();

    QString linkErrorMessage(const QString &code) const;


    void pollPendingUsers();



    void fetchRoomPassphrase();




    void startOfficeChannelMirror();


    bool isOfficeConversation(const QString &conversation) const;
    void showAdminVerifyDialog();
    bool adminVerifyEmail(const QString &target);







    void setPendingRelayJoins(int count);
    void showRelayJoinApprovalDialog();
    bool adminRelayApprove(const QString &pubkey, const QString &action);
    void verifyWallet();
    QUrl accountsApiUrl(const QString &leaf) const;
    QJsonObject postAccountSync(const QString &leaf, const QJsonObject &body,
                                int *status);
    QJsonObject getAccountSync(const QString &leaf, int *status);
    QString accountOwner() const;
    QString hostLinkUserName();
    QString hostLinkSigningAccountName(QString *userName = nullptr);
    void applyAccountEmailVerified(const QString &accountName, bool verified);
    bool accountEmailVerified(const QString &accountName) const;
    QString settingsAccountName() const;
    void refreshSettingsEmailVerifiedBadge();



    QString catalogOwner(const RepositoryRecord &repo) const;
    void runQuickUpdate();



    void updateRebuildRestart();





    void maybeAutoUpdate();





    bool tryPrebuiltAutoUpdate(const QString &clientDir, const QString &tag,
                               const QString &tagCommit);
    void installPrebuiltAndRelaunch(const QString &artifactPath,
                                    const QString &tag);
    QString resolveInstallCloneUrl();
    void buildAndRelaunch(const QString &clientDir, const QString &asUser = QString(),
                          const QString &relaunchPath = QString(),
                          const QString &buildType = QStringLiteral("Release"));
    void installAndRelaunch(const QString &built, const QString &appPath);



    void runUpdateStep(const QString &program, const QStringList &arguments,
                       const QString &workingDir, std::function<void()> onSuccess,
                       std::function<void()> onFailure = {});


    void runUpdateStepUser(const QString &program, const QStringList &arguments,
                           const QString &workingDir, std::function<void()> onSuccess,
                           std::function<void()> onFailure = {});
    void setUpdateStatus(const QString &status, bool isError = false);

    void showUpdateLog();
    void appendUpdateLog(const QString &text);


    void setFooterUpdateLine(const QString &line);



    QString footerLogLineHtml(const QString &clean);


    void styleFooterUpdateLog();


    void openFullLogAtFooterLine(const QString &rawLine);
    void persistProfile();


    QWidget *buildChatPage();
    void ensureSectionBuilt(int index);


    QWidget *buildSolanaNotice();
    void updateSolanaNotice();
    void promptSetSolanaAddress();




    void enablePaidMirroring();


    QWidget *buildBreadcrumb();
    void updateBreadcrumb();
    void showRelayMenu();
    void updateRelaySwitcher();
    void probeRelayLatency();



    void onRelayLatencySampled(int ms);
    void initRelayReachabilityWatch();
    void openServerWebsite(int index);
    void showNodeMenu();
    void showNodesWindow();
    QString topBarUserName() const;
    QString nodeOwnerDisplayName() const;
    QString chatDisplayName() const;




    bool chatIdentityIsGuest() const;
    QString guestChatName() const;
    QString machineNodeName() const;
    void saveMachineNodeName(const QString &name);


    void saveActionNodeLabels(const QString &labels);
    void updateChatIdentity();
    void updateUserSwitcher();
    void updateNodeSwitcher();
    void updateNavSolanaBalance();
    void refreshWebUserSolanaAddress();
    void cacheWebUserSolanaProfile(const QString &account,
                                   const QJsonObject &profile);
    void cycleNavSolanaCurrency();



    void renderNavSolanaBalance();




    void refreshNavSolanaBalance(bool force = false);
    void queryNavSolanaBalance(const QString &addr, int endpointIndex);
    void queryNavSolanaUsdPrice(const QString &addr, qint64 lamports);
    void showRepoMenu();           // dropdown to open repos / add a local repo
    void updateRepoSwitcher();     // refresh top-bar repo label / count
    void updateReposNavBadge();    // rail badge = repos the Repos page lists
    // Keep the open repo's sync-derived indicators in step. The Sync action lives
    // inside Source Control's Outgoing Changes group (never floating above Code),
    // alongside the activity-rail spinner and commit pending-sync markers.
    void refreshRepoSyncIndicators();
    // Source-control's compact Outgoing Changes group. It counts commits ahead
    // of the served mirror/upstream and drives the one-click safe publish path.
    void refreshSourceControlOutgoing();
    void pushCurrentRepoUpstream();



    void startRepoPush(int index, const RepositoryRecord &repo,
                       const QString &upstream, int ahead);




    QString mirrorStateHash(const QString &mirrorPath) const;


    QUrl catalogListUrl();




    void refreshRepoPinBanner();








    void reattestStalePins();

    void resetRepoPin();



    bool relayPublishRepo(const RepositoryRecord &repo, QString *localBranch,
                          int *unpublished) const;
    void showChatView();
    void updateChatButton();
    bool isChatViewVisible() const;



    void clearActiveConversationUnread();



    QString mostRecentUnreadConversation() const;

    void updateChatUnreadBanner();


    void markAllChatRead();
    void updateConnectionStatus();



    void setNodeOffline(bool offline);

    void updateNodeOnlineControls();

    QWidget *buildNetworkLogDock();

    QWidget *buildStatusBar();




    quint64 beginBackgroundTask(const QString &kind,
                                const QString &detail = QString());
    void finishBackgroundTask(quint64 id, bool success,
                              const QString &detail = QString());

    void noteBackgroundActivity(quint64 id, const QString &kind,
                                const QString &detail, bool backgrounded,
                                bool started);

    void tickBackgroundQueue();


    void recordBackgroundOutcome(const QString &word, qint64 elapsedMs,
                                 const QString &detail, qint64 now,
                                 bool backgrounded);
    void flushBackgroundOutcomes(bool force);

    static QString backgroundTaskWord(const QString &kind);

    void updateFooterGitIdentity();

    void updateFooterCommitInfo();

    void startDiagnostics();



    static QString stallLogPath();
    void updateFooterDiagnostics();
    void refreshRepositoryStats();
    void toggleRepositoryRatchet(bool enabled);
    void onUiStall(qint64 peakMs, const QString &blockingCall, const QString &backtrace);



    void maybeAutoFileStallAgent(qint64 peakMs, const QString &backtrace);


    int stallReportRepoIndex() const;


    void clearStallLog();


    bool sendStallLogToAgent();



    QString stallLogLocationsBlock() const;



    QString stallFixPrompt() const;



    void sendStallReportToComposer();
    void showDiagnosticsDialog();
    void showHighMemoryProcessPanel();
    void refreshHighMemoryProcessTable();
    void killHighMemoryProcess(qint64 pid, const QString &name);


    void killAllHighMemoryProcesses(const QString &name, const QList<qint64> &pids);

    QWidget *buildLogSection();
    void showCloudflareWorkerLogs();


    void loadServers();
    void saveServers();
    void switchToServer(int index);
    void promptAddServer();
    void removeServer(int index);
    void loadActiveServerIntoEdits();
    void persistEditsToActiveServer();
    void loadCachedFavicons();
    void fetchFavicon(int index);
    void fetchFaviconForHost(const QString &host);
    void fetchFaviconFromUrl(const QString &host, const QUrl &url);
    QPixmap faviconFor(const ServerConfig &server) const;



    QString logFaviconTag(const QString &message, QTextEdit *view);
    void registerLogFaviconResource(const QString &host, QTextEdit *view);
    void refreshLogFavicon(const QString &host);
    QWidget *buildHomeSection();

    QWidget *buildNodeProfileSection();
    QWidget *buildNodeProfilePanel();


    void showNodeProfile(const QString &nodeId, const QString &nodeName,
                         bool navigate = true);


    void hostNodeProfilePanel(bool inSettings);
    bool profilePanelInSettings() const
    {
        return m_nodeProfilePanel && m_settingsProfileHost &&
               m_nodeProfilePanel->parentWidget() == m_settingsProfileHost;
    }


    void syncSettingsProfileTab();
    void refreshProfileHostingStats();
    void refreshProfileAccountStatus();
    void renderProfileAccountStatus();
    QString linkedNodesHtml() const;


    void fetchLinkedNodesFromOwner(const QString &node, const QString &owner);
    void rescaleProfileAvatar();
    void hideNodeProfile();
    void checkNodeBalance();

    void querySolanaBalance(const QString &addr, int endpointIndex);

    void selectNode(const QString &node);
    QWidget *buildIssuesSection();
    QWidget *buildOrganizationTasksSection();
    void refreshOrganizationTasks();
    void applyOrganizationTasks(const QJsonObject &payload);



    void setOrganizationTaskBadge(int openCount);
    void restoreOrganizationTaskBadge();
    void refreshOrganizationTaskBadge();
    void renderOrganizationTaskDetail();
    void updateOrganizationTaskActions();
    void createOrganizationTask();
    void createQuickAddOrganizationTask();
    void createOrganizationTaskFollowUp();
    void refreshOrganizationTaskQueue();
    void moveQueuedAgentItemToTasks();
    void editOrganizationTask();
    void assignOrganizationTaskToAgent();
    void queueOrganizationTaskAgent(const QJsonObject &task);
    void runOrganizationTaskAction(const QString &action);
    void deleteOrganizationTask();
    using OrganizationTaskReplyHandler =
        std::function<void(bool, const QJsonObject &, const QString &)>;
    void requestOrganizationTasks(
        const QByteArray &method, const QString &path, const QJsonObject &body,
        OrganizationTaskReplyHandler handler);
    QWidget *buildChatSection();
    QWidget *buildSettingsSection();

    QWidget *buildVulnReportTab();
    void submitVulnerabilityReport();



    QWidget *buildMcpConnectorTab();
    void refreshMcpConnectorTab();
    void generateMcpConnector();
    void revokeMcpConnector();
    void testMcpConnector();
    QString mcpServerScriptPath() const;







    void startGenieAgent();






    void launchGenieRun(int repoIndex, const QString &typedGuidance);
    void requestGenieCredential(int repoIndex, const QString &typedGuidance);
    QString genieSetupPrompt(const QString &extraInstruction) const;
    QUrl genieMcpUrl() const;
    void applyGenieTaskTitle(int sessionId, const QString &assistantText);
    void saveGenieSettings();


    bool m_genieCredentialPending = false;


    QWidget *buildQuickSetupTab();


    QWidget *buildDataSection();
    void refreshDataDirTable();
    void exportConfigData();
    void importConfigData();
    void deleteDataDir(const QString &label, const QString &path, bool critical);
    void deleteAllData();
    void setDataStatus(const QString &text, bool error = false);
    void stopLiveServicesForDataOp();
    void relaunchForkMesh();





    void startAutoBackups();
    void takeBackupNow(bool automatic);
    void refreshBackupTable();
    void pruneOldBackups();
    void restoreConfigArchive(const QString &archivePath);
    void setBackupStatus(const QString &text, bool error = false);
    QString backupRoot() const;
    bool autoBackupEnabled() const;
    int backupKeepCount() const;
    QWidget *buildNotificationsSection();


    QWidget *buildNetworkReposSection();
    void refreshNetworkReposPage();
    void renderNetworkRepos(const QJsonArray &repos);


    void fillNetworkRepoDataCells(int row, const QJsonObject &repo);
    void fetchNetworkRepoMirrors(const QString &owner, const QString &name,
                                 int row, int generation);
    int findNetworkRepoIndex(const QString &owner, const QString &name,
                             bool includePreview = true) const;
    int findNetworkLocalForkIndex(const QString &owner, const QString &name) const;
    void openNetworkRepo(const QString &owner, const QString &name,
                         const QString &cloneUrl, bool isPrivate);
    void forkNetworkRepo(const QString &owner, const QString &name,
                         const QString &cloneUrl, bool isPrivate);
    void mirrorNetworkRepo(const QString &owner, const QString &name,
                           const QString &cloneUrl, bool isPrivate);




    QWidget *buildControlNodeSection();
    void refreshControlNode();
    void refreshControlMirrorReadiness();
    void runControlNodeHealthCheck();
    void startControlNodeServing();
    void stopControlNodeServing();
    void syncControlNodeMirrors();
    void updateControlRepositoryPermission(QTableWidgetItem *item);
    void saveControlWalletAddress();
    void runCloudflareBootstrap(bool dryRun);
    void cancelCloudflareBootstrap();
    void installCloudflareFirstMirror();
    void provisionDirectMirrorEndpoint(bool dryRun);
    bool rebuildDirectMirrorGatewayConfiguration(
        QString *error = nullptr, bool restartRunningGateway = false);
    void startDirectMirrorServices();
    void stopDirectMirrorServices();




    void maybeAutoStartDirectMirrorServices();


    void ensureDirectMirrorRegistrationTimer(QObject *parent);
    void registerDirectMirrorEndpoint();
    void checkDirectMirrorGatewayHealth();
    void appendControlNodeOutput(const QString &text);


    QWidget *buildSiteDeployCard();
    void runSiteDeploy();
    void cancelSiteDeploy();
    void appendSiteDeployOutput(const QString &text);
    void connectToDeployedRelay(const QString &hostname);
    void deploySavedHostsFromControl();





    QWidget *buildCloudflareTokenCard();
    QString resolvedCloudflareApiToken() const;
    void testCloudflareApiToken();
    void checkCloudflareTokenPolicies(const QString &token,
                                      const QString &tokenId);
    void resolveCloudflareTokenTopology(const QString &token);
    void runCloudflareTokenProbe(const QString &token, int index);
    void renderCloudflareTokenReport();
    void setCloudflareTokenBusy(bool busy);
    void endCloudflareTokenRun();
    void appendCloudflareTokenOutput(const QString &text);
    void generateCloudflareApiToken();
    void adoptRotatedCloudflareToken(const QString &token);
    // Persist a validated Cloudflare token in both credential stores used by
    // the Qt app and deployment scripts.
    QStringList rememberCloudflareApiToken(const QString &token,
                                           QString *error = nullptr);
    // First-instance-owner community reward-pool signer. The Solana private key
    // is imported into an encrypted local vault and never leaves this desktop;
    // the Worker only authors public intents and records public reconciliation.
    QWidget *buildRewardPoolControlCard();
    void refreshRewardPoolControls();
    void importRewardPoolKey();
    void saveRewardPoolRpcConfiguration();
    void fetchRewardPoolIntents();
    void reviewAndSignRewardIntent();
    void prepareRewardPoolTransaction(const QJsonObject &job);
    void submitRewardPoolTransaction(const QJsonObject &intent,
                                     const QString &signedTransactionBase64,
                                     const QString &chainSignature);
    void submitRewardSignatureReceipt(const QString &intentId,
                                      const QString &chainSignature,
                                      bool finalizationObserved);
    void pollRewardPoolFinalization(int attempt);
    void reconcileRewardPoolIntent(const QString &intentId,
                                   const QString &chainSignature,
                                   const QString &observedStatus,
                                   const QString &slot);
    QUrl rewardPoolWorkerEndpoint(const QString &path) const;







    QWidget *buildHostsSection();
















    void runHostInstall(bool forceUploadBinary = false,
                        std::function<void(bool)> onFinished = {},
                        bool reinstall = false, bool fromSource = false,
                        bool suppressFailureStatus = false);


    void runHostUninstall();

    void viewHostLogsForSelection(int row);
    void runHostLogSession(const QString &ip, const QString &user,
                          const QString &pass, const QString &node);



    void browseHostDiskUsageForSelection(int row);
    void runHostDiskUsageBrowser(const QString &ip, const QString &user,
                                 const QString &pass, const QString &node);




    void configureHostActionsForSelection(int row);



    void installAgentClisForHost(int row);




    void openHostAgentLoginTerminalForSelection(int row);
    void openHostAgentLoginTerminal(const QString &node, const QString &ip,
                                    const QString &user);






    void runAgentCliInstall(const QString &node, const QString &ip,
                            const QString &user, const QString &sshPassword,
                            const QString &identityFile, bool copyCredentials,
                            std::function<void(bool, QString)> onFinished);



    static forkmesh::control::AgentCliCredentials localAgentCliCredentials();
    void runHostActionsConfiguration(
        forkmesh::control::MirrorActionsConfigurationRequest request,
        const QString &sshPassword);





    void runHostInstallAllFromBinary();
    void runHostReinstallAllFromBinary();
    void runHostUpdateAllFromSource();
    void appendHostInstallLog(const QString &text);






    enum class FleetDeployMode { InstallBinary, Reinstall, UpdateSource };
    struct FleetDeployOptions {
        bool uploadBinary = false;
        bool reinstall = false;
        bool fromSource = false;
        bool requirePublishedBinary = false;
    };
    static FleetDeployOptions fleetDeployOptions(FleetDeployMode mode);



    struct HostDeploySession {
        QString node;
        QString ip;
        QString user;
        QString pass;
        QProcess *proc = nullptr;
        QPlainTextEdit *log = nullptr;
        QLabel *header = nullptr;
        QString ansiCarry;
        int ansiFg = -1;
        bool ansiBold = false;
        QString linkTail;
        bool linkPrompted = false;
        bool finished = false;
    };
    void runHostDeployAllParallel(FleetDeployMode mode);
    void startHostDeploySession(HostDeploySession *session,
                                const FleetDeployOptions &options);
    void appendHostDeployLog(HostDeploySession *session, const QString &text);
    void onHostDeploySessionFinished(HostDeploySession *session, bool ok);







    bool buildHostInstallCommand(const QString &ip, const QString &user,
                                 const QString &node, bool uploadBinary,
                                 bool reinstall, bool fromSource,
                                 bool requirePublishedBinary,
                                 QString *remoteCmd, QByteArray *uploadBytes,
                                 QString *errorOut);





    void addHostFromForm();
    void rememberHost(const QString &name, const QString &ip, const QString &user,
                      const QString &pass, const QString &status = QStringLiteral("installed"),
                      const QString &identityFile = QString(),
                      const QJsonObject &metadata = QJsonObject());



    QString savedHostIdentityFile(const QString &name, const QString &ip,
                                  const QString &user) const;
    void refreshHostsTable();
    void probeSavedHosts();
    void probeSavedHost(const QString &name, const QString &ip,
                        const QString &user, const QString &savedStatus);



    void forgetHostAtRow(int row);






    void destroyVultrHostAtRow(int row);
    void sendVultrInstanceDestroy(const QString &apiKey,
                                  const QString &instanceId,
                                  const QString &name);







    void createVultrMirrorFromForm();








    QStringList rememberVultrApiKey(const QString &apiKey,
                                    QString *error = nullptr);
    void vultrApiCall(const QString &apiKey, const QString &path,
                      const QByteArray &method, const QJsonObject &body,
                      std::function<void(QJsonObject, QString)> onDone);
    void ensureVultrManagedKeypair(
        std::function<void(QString privateKeyPath, QString publicKey,
                           QString error)> onDone);
    void resolveVultrSshKeyId(
        const QString &apiKey, const QString &publicKey,
        std::function<void(QString keyId, QString error)> onDone);
    void pollVultrInstance(const QString &apiKey, const QString &instanceId,
                           const QString &node, const QString &identityFile);
    void startVultrHostInstall(const QString &node, const QString &ip,
                               const QString &identityFile);



    void waitForVultrMirrorPublication(const QString &node,
                                       const QString &successMessage,
                                       int attempt = 0);
    void finishVultrProvision(bool ok, const QString &message);


    void appendVultrAttemptHistory();



    void cloudflareApiCall(const QString &apiToken, const QString &path,
                           const QByteArray &method, const QJsonObject &body,
                           std::function<void(QJsonObject, QString)> onDone);





    void ensureVultrMirrorDns(const QString &node, const QString &ip,
                              std::function<void(QString hostname)> onDone);
    // Drop "<node>.<zone>" again when a node is deleted for good, so no A
    // record is left pointing at an address that no longer answers. Soft like
    // ensureVultrMirrorDns: it reports what it did through `onDone` and never
    // blocks the rest of the deletion.
    void removeVultrMirrorDns(const QString &node,
                              std::function<void(QString outcome)> onDone);
    // Reload a saved host's server info from the table. A password is restored
    // only when it remains in this process's session cache.
    void loadHostIntoForm(int row, int column);

    QString installScriptUrl() const;

    QString uninstallScriptUrl() const;




    QWidget *buildRelaysSection();
    void refreshRelaysTable();
    void probeRelayRow(int row);





    QWidget *buildNodesSection();
    void refreshNodesTable();           // re-list the known nodes into the table
    void showNodeDetailForRow(int row); // fill the detail panel for a table row
    // Per-row Delete button in the Nodes table's action column. Kept in its own
    // pass so it can be re-attached after the user re-sorts the table, and so
    // the admin/protected-node gating lives in one place.
    void refreshNodeActionButtons();
    // --- Delete a node for good (adhoc #19) ----------------------------------
    // The Nodes page's Delete button: the same permanent removal the World
    // panel performs, plus the provider teardown the web has no credentials
    // for. In order — destroy the Vultr instance behind the node, drop its
    // Cloudflare DNS record, remove every trace of it from the relay (accounts,
    // mirrors, agent jobs and the /status history), then forget the saved SSH
    // host locally. The provider steps are best-effort and never stop the mesh
    // removal; the mesh removal itself is the one step that must succeed.
    void deleteMeshNodeCompletely(const QString &node, const QString &nodeId);
    // Resolve the node's Vultr instance (the id recorded at provision time, or
    // a unique label/hostname/address match) and destroy it. Reports what
    // happened through `onDone` — including "nothing to destroy" — so the
    // deletion continues for nodes this app never provisioned.
    void destroyVultrServerForNode(const QString &node,
                                   std::function<void(QString outcome)> onDone);
    void sendNodeVultrDestroy(const QString &apiKey, const QString &instanceId,
                              const QString &node,
                              std::function<void(QString outcome)> onDone);
    // POST /api/world/admin/nodes/delete. A desktop that signed in with a
    // password holds a session token; the ordinary launch authenticates
    // silently and holds keys only, so this falls back to the signed
    // node/ts/sig proof the worker accepts for this one deletion.
    void sendMeshNodeDeleteRequest(const QString &node, const QString &nodeId);
    // Drop the saved SSH host whose name matches a node that no longer exists.
    void forgetSavedHostNamed(const QString &node);
    // Progress/result line for a deletion, shown on the Nodes page.
    void setNodeDeleteStatus(const QString &text);
    // Fetch the relay's list of currently-online node names (/api/network/stats
    // "onlineNodes": repository update channel or fresh signed heartbeat). Headless
    // mirror nodes serve through the relay without joining this client's chat
    // room, so room presence alone painted them offline (adhoc #27).
    void fetchRelayOnlineNodes(bool force = false);


    QWidget *buildFirewallSection();
    void refreshFirewallTables();
    void addFirewallRuleFromEdit();
    void removeSelectedFirewallRules();
    void clearFirewallHistory();
    bool promptFirewallRequest(const QString &method, const QUrl &url,
                               QString *allowRuleOut);
    bool authorizeFirewallConnection(const QString &method, const QUrl &url);
    void recordFirewallRequest(const QString &method, const QUrl &url,
                               const QString &rule, bool allowed);


    QWidget *buildNetworkDiagnosticsSection();
    void refreshNetworkDiagnostics();



    void updateNetworkCounts(int relays, int nodes, int hosts);



    void showNetworkTab(int tabIndex);



    void refreshNetworkTab(int tabIndex);
    void showEndpointRequestDetails(int row, int column);


    void ensureRepoDetailSectionBuilt();
    void ensureRepoDetailTabBuilt(int index);
    QWidget *buildRepoDetailSection();
    QWidget *buildRepoFilesPanel();
    QWidget *buildRepoOverviewPage();
    QWidget *buildRepoEditorPage();
    QWidget *buildRepoCoveExplorerPage();
    QWidget *buildRepoCommitsTab();
    void showCommit(const QString &hash);


    void renderCommitDetail(const QString &dir, const QString &hash,
                            const QStringList &metaFields,
                            const QByteArray &patchRaw);
    void showCommitList();
    void openMostRecentCommit();


    void deleteCommit(const QString &hash);


    void revertCommit(const QString &hash);


    void showCommitsBanner(const QString &html);
    void hideCommitsBanner();

    void openCommitReference(int number);
    void openIssueReference(int number);
    void openPullReference(int number);
    void openCommitHashReference(const QString &hash);
    void openReferenceLink(const QString &href);





    void openGlobalSearch();
    void runGlobalSearch(const QString &query);
    QDialog *m_searchDialog = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QListWidget *m_searchList = nullptr;
    QLabel *m_searchStatus = nullptr;


    int m_searchGen = 0;




    void openBodyReference(const QString &href);



    void copyReferenceLink(const QString &kind, const QString &id);

    void filterCommits(const QString &query);
    void downloadCommitPatch();

    void updateDiffSplitButton(QPushButton *button);



    void addConversationCard(QVBoxLayout *layout, const QString &author,
                             const QString &headerHtml, const QString &body,
                             const QString &accent = QString(),
                             const QString &copyLink = QString(),
                             const QString &authorId = QString(),
                             const std::function<void()> &onDelete = {});
    QWidget *buildRepoSecurityTab();
    QWidget *buildRepoQualityTab();
    QWidget *buildInsightsTab();



    QWidget *buildSizeMapTab();




    void refreshSizeMapTab(bool force, bool allowElevation = false);


    QString sizeMapRoot() const;
    void chooseSizeMapFolder();
    void setSizeMapRootOverride(const QString &path);


    QWidget *buildSizeMapVolumesPanel();
    void refreshSizeMapVolumes();



    QSet<QString> sizeMapPrunedPaths(const QString &path) const;







    void rescanSizeMapElevated(bool upfront = false);



    void stopSizeMapScan();


    void showSizeMapScanProgress(const QString &current, qint64 bytes,
                                 int files, bool elevated);
    void applySizeMapResult(const QString &path,
                            forkmesh::DirectorySizeScanResult result,
                            bool hideIgnored, bool elevated);
    QWidget *buildPlaceholderTab(const QString &name);


    QWidget *buildDiscussionsTab();
    DiscussionStore discussionStoreForCurrentRepo() const;
    void reloadDiscussions();


    void reloadDiscussionCountInBackground();
    void showDiscussion(int number);
    void renderDiscussionThread(const Discussion &discussion);
    void updateDiscussionActionState();
    void createDiscussionDialog();
    void postDiscussionComment();
    void deleteDiscussionComment(int number, const QString &eventId);
    void startDiscussionFromComposer();
    static QString discussionTitleFromBody(const QString &body);
    void submitDiscussionEventToInbox(int number, const DiscussionEvent &ev,
                                      const QString &titleIfNew = QString());
    QUrl discussionsApiUrl(const RepositoryRecord &repo) const;
    void syncDiscussionsInbox();
    void drainDiscussionsInboxFor(RepositoryRecord repo, bool interactive);
    void setDiscussionInlineNotice(const QString &message, bool isError = false);


    QWidget *buildPullsTab();
    PullStore pullStoreForCurrentRepo() const;
    void reloadPulls();


    void reloadPullsInBackground();
    void applyLoadedPulls(const PullStore &store, QList<PullRequest> pulls,
                          const QString &baseTip);


    void processPendingPullConflicts(quint64 gen);



    void queuePullConflictCheck(int number, const QString &fingerprint);


    void setPullConflictBadge(int number, bool conflict);



    QString pullConflictBadgeTooltip(int number) const;
    void refreshPullList();
    void showPull(int number);


    void applyPullFileAuthorFilter();



    void renderPullReviewSummary(PullRequest pr);



    void renderPullDiff();

    void scrollPullDiffToFile(const QString &filePath);




    void updatePullDiffScrollState();


    void selectPullFileInList(const QString &filePath);

    void layoutPullStickyHeader();


    void computePullFileTops();




    void applyAutoMarkViewedOnScroll();
    void adjustDiffFont(int delta);


    void registerDiffView(QTextEdit *view);




    void setDiffHtml(QTextEdit *view, const QString &html);


    void onDiffStreamFinished(QTextEdit *view);


    void pullSelectAdjacentChange(int delta);



    bool pullScrollToAdjacentHunk(int delta);



    void togglePullDiffSearch(bool show);
    void pullDiffSearchRecompute();
    void pullDiffSearchGoTo(int delta);


    void onPullDiffAnchorClicked(const QUrl &url);
    void submitPullThreadReply(const QString &threadId);
    void setPullThreadState(const QString &threadId, const QString &state);
    void renderPullThread(const PullRequest &pr);
    void renderPullCommits(PullRequest pr);






    void renderPullChecks(PullRequest pr);
    void renderPullChecksSummary(PullRequest pr);
    void showPullCheckLog(int runId);
    QStringList pullCommitShas(const PullRequest &pr) const;
    QList<int> runIdsForPull(PullRequest pr) const;
    void runChecksForCurrentPull();



    void buildAndPreviewCurrentPull();
    void updatePullSubTabCounts(PullRequest pr);
    void refreshOpenPullChecks();


    enum class WorkflowTrigger { Push, Release };



    void queueWorkflowsForCommit(int repoIndex, const QString &owner,
                                 const QString &name, const QString &commit,
                                 const QString &ref,
                                 WorkflowTrigger trigger = WorkflowTrigger::Push);
    void submitPullComment();
    void sendPullRevisionToAgent();
    void submitPullReview(const QString &state);
    void promptNewPull();
    void importPatchAsPull();
    void promptNewPullFromDirectory();
    void promptNewPullFromSource(const QString &sourceDir,
                                 const QString &preferredBase = QString(),
                                 const QString &preferredHead = QString());
    void switchToPullTab(int pullNumber);



    void openPullDiffInGitView(int pullNumber);



    QHash<QString, QString> buildPullLineNotes(const PullRequest &pr);
    void updateCurrentPullBranch();
    void mergeCurrentPull();
    void resolveCurrentPullConflicts();


    bool runMergeConflictEditor(const QString &title, const QString &introHtml,
                                const QString &workTree,
                                const QStringList &conflictedFiles,
                                const QString &commitButtonText,
                                const std::function<bool(QString *)> &commitFn);



    void fixCurrentPullConflictsWithAi(const QString &provider);
    // The PR header's "Fix" button (adhoc #7): writes a ready-made
    // conflict-resolution task into the footer prompt box instead of launching a
    // provider straight away, so the user can edit it before sending.
    void fillPromptWithPullConflictFix();
    // Continue the agent session that originally authored this PR's branch,
    // asking it to merge the base branch in and resolve conflicts itself — the
    // same flow as the agent detail view's "Fix conflicts with agent" button.
    // Only available when such a session is attached and idle.
    void fixCurrentPullConflictsWithOriginatingAgent();
    void editCurrentPullFile();
    void deleteCurrentPullFile();
    void closeIssuesLinkedFromPull(const PullRequest &pr);






    QList<int> closeIssuesForMerge(const QList<int> &numbers,
                                   const QString &comment, const QString &via);


    void fundBountiesForMergedPull(const PullRequest &pr);


    void autoBountyForMergedPull(const PullRequest &pr);

    void pollBountyPayout(const RepositoryRecord &repo, int number, double amount,
                          const QString &kind = QString());
    QList<int> issuesLinkedFromPull(const PullRequest &pr) const;



    QList<int> pullsLinkedToIssue(int issueNumber) const;

    void linkPullToIssueFromIssuePage();
    void linkIssueToPullFromPullPage();


    void postIssueLinkComment(int issueNumber, const QString &body);
    void postPullLinkComment(int pullNumber, const QString &body);




    void linkAgentPullToIssue(const AgentSession &session, int prNumber);
    void closeCurrentPull();
    void reopenCurrentPull();



    void sendCurrentPullToSource();
    void deleteCurrentPull();
    void deleteCurrentPullAndBranch();



    void mergeAndDeleteCurrentPull();




    void deleteAllMergedPullsAndBranches();






    void deletePullAndBranchAsync(int number, const QString &head, bool haveBranch,
                                  bool rewriteHistory, bool propagate,
                                  std::function<void()> onDone = {});
    void setPullDeleteButtonsEnabled(bool enabled);


    bool confirmPullDeletion(const QString &prompt, bool *rewriteHistory);
    void syncPullsInbox();
    void submitPullToInbox(const PullRequest &pr);



    void submitPullToInbox(const PullRequest &pr, const RepositoryRecord &targetRepo,
                           bool quiet = false);


    void submitPullEventToInbox(int number, const PullEvent &ev);
    void updatePullActionState();
    QUrl pullsApiUrl(const RepositoryRecord &repo) const;

    QWidget *buildAgentsTab();
    void initAgents();
    void reloadAgents();
    void refreshAgentTable();






    void applyAgentRowCells(int row, const AgentSession &session,
                            const QString &agentGitDir, const QString &agentBase);



    AgentDiffStat agentDiffStat(const AgentSession &session, const QString &gitDir,
                                const QString &base);
    void updateAgentTokenCell(int sessionId);
    void updateAgentCostCell(int sessionId);
    void updateAgentRunSummaryCells(int sessionId);
    void updateAgentStatusCell(int sessionId);
    void refreshAgentStatusPill(int sessionId);
    void animateRunningAgentIcons();




    void noteAgentActivity(int sessionId, int bytes = 0);
    void onScannerTick();
    void showAgentSession(int sessionId);




    void refreshAgentDetailMeta(int sessionId);



    qint64 agentSessionProcessId(int sessionId) const;






    void setAgentLogText(int sessionId, const QString &text);





    qint64 sessionTokenTotal(const AgentSession &session) const;


    void seedSessionTokens();



    void setAgentUsageLabel(const AgentSession &session);



    struct AgentLogScan {
        QString log;
        int requests = 0, responses = 0, errors = 0;
        qint64 inTokens = 0, outTokens = 0;
    };

    static AgentLogScan scanAgentLog(QString log);
    bool m_agentNetScanInFlight = false;

    void applyAgentNetworkPanel(const AgentLogScan &scan, const QString &status);
    AgentSession *findAgentSession(int sessionId);
    const AgentSession *latestAgentSessionForIssue(int issueNumber) const;






    const AgentSession *agentSessionForPull(int prNumber,
                                            const QString &headBranch = QString()) const;






    bool markAgentSessionsMerged(int prNumber, const QString &branch);
    void refreshAgentMergeState();
    void markAgentSessionsLanded(const QList<int> &sessionIds, bool refreshUi);
    void assignIssueToAgent(const QString &provider, const QString &model = QString());








    int startAgentForIssue(const Issue &issue, const QString &provider, bool createPr,
                           bool quiet = false, const QString &model = QString(),
                           const RepositoryRecord *repoHint = nullptr);



    void toggleIssueLooper();
    void looperStartNext();
    void looperOnSessionFinished(int sessionId);





    void looperClaimIssue(int number, const QStringList &existingAssignees);


    QString nodeAssigneeTag() const;



    void updateIssueLooperButton();
    void persistLooperState();
    void maybeRestoreIssueLooper();
    void continueSelectedAgentSession();






    void continueAgentSession(int sessionId, bool deferRefresh = false);



    void fixAgentConflictsWithAgent(int sessionId);




    void applyComposerSelectionToAgentSession(int sessionId);



    void sendPromptToSelectedAgent(const QString &prompt);


    void sendPromptToAgentSession(int sessionId, const QString &prompt);





    QString issueContextPrompt(const Issue &issue) const;


    void sendIssueContextToSelectedAgent();
    void deleteSelectedAgentSession();


    void createLinkedIssueForSelectedSession();





    bool deleteStoredAgentSession(int sessionId, bool cleanupWorktree = true);


    void deleteWorktreeBranchAndAgentInBackground(
        const QString &worktreePath, const QString &branch);









    void deleteWorktreeBranchAndAgent(const QString &worktreePath,
                                      const QString &branch, bool confirm = true,
                                      bool async = true, bool deferRefresh = false);



    void deleteAllMergedAgentSessions();
    void testOpenAiAgentKey();
    void refreshClaudeSpend();







    void refreshClaudeCodeUsage(bool fromHover = false);


    void flashUsageChart(QWidget *chart, bool ok);







    void refreshClaudeModelCombo();




    void applyLiveClaudeModelsToCombos();


    void refreshCodexUsageRemaining();
    void applyCodexRateLimits(const QJsonObject &rateLimits);



    void applyClaudeUsage(bool weekly, int percent);




    int m_claudeUsageLastWeekPct = -1;
    int m_claudeUsageLast5hPct = -1;



    void applyClaudeReset(bool weekly, qint64 resetMs);



    void applyClaudeFableUsage(int percent);
    void applyClaudeFableReset(qint64 resetMs);




    void maybeEmailCreditsRefilled(bool weekly);
    // When a provider reports a reset time for an exhausted usage window, make
    // one local iCalendar reminder and arm the matching desktop ping. The
    // calendar app owns alerts while ForkMesh is closed; the timer covers a
    // running desktop.
    void scheduleUsageLimitReminder(const QString &providerKey,
                                    const QString &windowKey,
                                    const QString &providerName,
                                    const QString &windowName,
                                    qint64 resetMs);
    void restoreUsageLimitReminders();
    void clearUsageLimitReminders();
    void notifyUsageLimitReady(const QString &providerKey,
                               const QString &windowKey,
                               const QString &providerName,
                               const QString &windowName,
                               qint64 resetMs);
    // Issue #115: persist and restore month-to-date spend so the figures are
    // shown on restart instead of waiting for a fresh API refresh.
    void cacheSpendLabel(const QString &textKey, const QString &tsKey,
                         const QString &text);
    void applyCachedSpendLabels();


    void markAgentLimitWindow(const QString &provider);
    void refreshAgentLimitLabel();
    void openAgentSessionFromIssue();
    void switchToAgentsTab(int sessionId);


    void switchToAgentBranch(int sessionId);


    void openAgentsOverview();

    void updateAgentsNavBadge();



    void refreshAgentDotMatrix();


    void refreshActionRunStrip();
    void processAgentQueue();


    void scheduleAgentQueuePump();
    // Persist the machine-wide concurrent-agent limit, keep both controls in
    // sync, and immediately drain any newly available queue slots.
    void setAgentConcurrencyLimit(int limit);
    // Update the Agents-toolbar queue readout and its one-click limit controls.
    void refreshAgentQueueControls();
    // How many sessions currently hold one of the maxRunningAgents() slots
    // (adhoc #433): our own, unmerged, actively-executing ones.
    int runningAgentCount() const;

    QList<int> stoppableAgentSessionIds() const;

    void stopAllRunningAgents();


    QList<int> startableAgentSessionIds() const;

    void startAllStoppedAgents();

    AgentRunner *runnerForSession(int sessionId) const;

    AgentRunner *acquireAgentRunner();

    bool anyAgentRunning() const;

    QStringList runningAgentBlockers() const;
    void onAgentLog(int sessionId, const QString &text);


    void appendAgentRawLog(const QString &text);

    void showAgentRawOutput();
    void onAgentStatusChanged(int sessionId, const QString &status);
    void onAgentFinished(int sessionId, bool ok);

    void onAgentNeedsAttention(int sessionId, const QString &message);
    void updateAgentActionState();





    void updateQuickAddEnterTarget();









    bool quickAddShouldFollowUpAgent() const;
    void updateIssueAgentUi(const Issue &issue);



    void refreshIssueFilesPanel(const Issue &issue);
    void renderIssueDiff(int issueNumber, const QByteArray &patch,
                         const QString &dir, const QString &base);



    void populateIssueFilesCell(int row, const Issue &issue);




    QIcon issueAssigneeAvatar(const QString &name);
    QHash<QString, QIcon> m_assigneeAvatarCache;


    bool ideExtensionActive(QString *ideName = nullptr) const;
    bool ideIntegrationReady(QString *ideName = nullptr) const;
    void startIssueInIde(int issueNumber, const QString &title,
                         const QString &provider);
    void updateIssueIdeButtons();
    AgentRunner::Config agentConfigForProvider(const QString &provider) const;


    void startClaudeCodeTerminal(AgentSession &session, const Issue &issue,
                                 const QString &repoPath);
    QString agentProviderName(const QString &provider) const;

    QWidget *buildRepoActionsTab();
    void refreshRepoActions();
    QList<ActionWorkflow> availableWorkflowsForRepo(
        const RepositoryRecord &repo) const;


    void reloadWorkflowCountInBackground();


    void updateManualRunBar();

    void runSelectedWorkflowManually();

    void rerunSelectedRun();


    void stopSelectedRun();


    void skipSelectedRun();


    void fixSelectedRunWithAgent(const QString &provider, const QString &model);





    void maybeAutoFixFailedRun(const ActionRun &run);


    void clearActionRuns();
    void initActions();
    void ensurePushHook(const RepositoryRecord &repo) const;




    void ensureCommitSignalHook(const RepositoryRecord &repo) const;
    void removePushHook(const RepositoryRecord &repo) const;
    void installAllPushHooks() const;
    void scanActionSpool();



    void syncMirrorActionsConfiguration();
    void scanExternalActionsSources();
    void updateMirrorActionsRuntimeState();



    void scheduleMirrorActionsSummary(int delayMs = 0);
    void writeMirrorActionsSummary();
    void enqueuePushEvent(const QString &owner, const QString &name,
                          const QString &commit, const QString &ref);





    QStringList actionNodeLabels() const;


    QString workflowNodePin(const RepositoryRecord &repo,
                            const QString &path) const;


    QStringList workflowRunsOnLabels(const RepositoryRecord &repo,
                                     const ActionWorkflow &workflow) const;

    bool workflowRunsOnThisNode(const RepositoryRecord &repo,
                                const ActionWorkflow &workflow) const;

    QString workflowDedicationLabel(const RepositoryRecord &repo,
                                    const ActionWorkflow &workflow) const;


    QStringList actionNodeCandidates() const;


    void setWorkflowNode(const QString &node);
    void refreshWorkflowNodeCombo();
    void updateWorkflowListItem(QListWidgetItem *item,
                                const ActionWorkflow &workflow,
                                const RepositoryRecord &repo);
    void processActionQueue();











    std::shared_ptr<void> pinActionMirror(const RepositoryRecord &repo,
                                          QString *mirrorPath) const;




    void releaseActionMirrorPin(int runId);


    ActionRunner *runnerForRun(int runId) const;





    void cancelSupersededRuns(const ActionRun &newRun);
    void onRunLog(int runId, const QString &text);
    void notifyActionEvent(const QString &title, const QString &body,
                           bool warning);


    struct AppNotification;
    void addNotification(const QString &title, const QString &body,
                         bool warning = false, int runId = -1);


    void addNotification(const QString &title, const QString &body, bool warning,
                         const NotificationLink &link);



    void addNotification(const QString &title, const QString &body, bool warning,
                         const NotificationLink &link, const QString &kind,
                         const QString &actor);



    void recordNotification(AppNotification item);



    void flashNotification(const AppNotification &item);


    void flashErrorBorder();

    void openNotificationLink(const NotificationLink &link);
    void showNotifications();


    void refreshNotificationsTable();

    void refreshLogEventList();


    void deleteSelectedNotifications();
    void deleteWebAlert(const QString &alertId);


    void openNotificationRow(int row);
    static QString notificationLinkLabel(const NotificationLink &link);
    void updateNotificationButton();



    void refreshWebAlerts(bool force = false);

    void markWebAlertsRead();

    void updateNavRebuildButton();


    void updateSignInButton();

    void showSignInMenu();

    void positionFloatingLogButton();


    void positionFooterLogPauseButton();
    void updateFooterLogPauseButton();
    int pendingActionCount() const;
    void openActionRunFromNotification(int runId);



    void postNotification(const QString &title, const QString &body,
                          bool warning = false, const QString &icon = QString());
    void onRunStatusChanged(int runId, const QString &status);
    void onRunFinished(int runId, bool ok);


    void onReleaseMetadataLanded(int runId);
    void refreshActionsTable();
    void showLatestVisibleActionRun();
    void showRun(int runId);
    void approveSelectedRun();
    void rejectSelectedRun();
    ActionRun *findRun(int runId);


    ActionNeeds::State actionRunNeedsState(int runId, QString *detail);

    void noteActionRunWaiting(int runId, const QString &detail);
    int repoIndexFor(const QString &owner, const QString &name) const;

    void reloadVariablesTable();
    void addOrEditVariable(bool editSelected);
    void deleteSelectedVariable();
    void exportVariables();
    void importVariables();
    void toggleVariablesRevealed();
    void persistVariablesFromTable();

    void openRepoDetail(int repoIndex);



    bool bindRepoDetailToRepo(int repoIndex);


    void openRepoDetailDeferred(int repoIndex);

    void clearRepoDetail();
    void openRepositoryWebsite();
    void forkCurrentRepo();
    void downloadCurrentRepoZip();
    void setRepoDetailNotice(const QString &message, bool error = false);
    void refreshOpenRepoDetail();


    void scheduleOpenRepoDetailRefresh();
    void updateRepoCodeSize();
    void updateRepoIssueCount();
    void updateRepoDiscussionCount();
    void updateRepoPullCount();



    void refreshRepoTabCounts();
    void loadRepoOverview(const QString &path);
    void showRepoOverview();
    void showRepoEditor();
    void showRepoCoveExplorer();
    // The Git workspace is stored beside the Code overview for layout reuse,
    // but only the activity-rail Git destination opens it.
    void showOverviewCommits();
    void showOverviewFiles();
    void showOverviewBranches();
    void showOverviewWorktrees();


    void updateRepoActivityRail();
    void loadRepoFileTree();
    void loadCoveExplorer();
    void refreshCoveExplorerTree();
    void openCoveExplorerDocument(const QString &path);
    void saveCurrentCoveExplorerDocument();
    void createCoveExplorerCove();
    void inviteUserToCurrentCove();
    void createCoveExplorerDocument();
    void deleteCurrentCoveExplorerDocument();
    QString coveAccountName();
    bool coveInviteAccountValid(const QString &accountName, QString *error);




    void showRepoFileTreeMenu(const QPoint &pos);
    void newRepoFileEntry(const QString &parentDir, bool folder);
    void renameRepoFileEntry(const QString &path, bool isDir);
    void deleteRepoFileEntry(const QString &path, bool isDir);
    void closeRepoFileTabsUnder(const QString &path, bool isDir);



    QString prepareRepoFileOp(QString *base);


    void finishRepoFileOp(const QString &base);
    void openRepoFile(const QString &path);
    void openRepoReadme();
    void updateRepoFileSaveActions();
    void saveCurrentRepoFile(bool createPull);

    void toggleRepoFileMarkdownPreview();


    void showRepoFileHistory(const QString &path);
    bool saveRepoFileEdit(const QString &path, const QString &content, bool createPull);




    bool repoCanProposePull() const;


    bool proposePullFromMirrorEdit(const QString &cleanPath, const QString &content);
    void loadRepoInfo();
    void loadBranchesAndTags();





    struct BranchesTagsSnapshot {

        QString dir;
        QString localPath;
        QString configuredDefault;
        QString checkedOut;

        QString base;
        QString head;
        QStringList branches;
        int worktreeCount = 0;
        struct Remote {
            QString name;
            QString fetchUrl;
            QString pushUrl;
        };
        QList<Remote> remotes;
        int tagCount = 0;
    };

    static BranchesTagsSnapshot readBranchesTagsGit(BranchesTagsSnapshot snap);

    void applyBranchesTags(const BranchesTagsSnapshot &snap);


    bool m_branchesTagsLoading = false;
    bool m_branchesTagsReloadQueued = false;
    QStringList repoBranches() const;


    static QStringList listRepoBranches(const QString &dir);
    static QString chooseDefaultBranch(const QStringList &branches,
                                       const QString &configured,
                                       const QString &dir,
                                       const QString &checkedOut);
    QString repoDefaultBranch(const QStringList &branches) const;




    QString repoDefaultBranchFast() const;
    QWidget *buildBranchesTab();




    struct BranchesPanelData {
        QString dir;
        QString configuredDefault;
        QString checkedOut;
        QString base;
        QString selected;
        QString previouslyViewed;
        bool writable = false;
        QStringList branches;
        QStringList remoteBranches;
        QHash<QString, qint64> times;
        QHash<QString, QString> shortShas;
        QHash<QString, QString> subjects;
        QHash<QString, QString> authors;


        QHash<QString, QPair<int, int>> localAheadBehind;
        QHash<QString, QPair<int, int>> remoteAheadBehind;
        QHash<QString, QString> worktrees;
    };
    struct BranchChangeStat {
        int files = -1;
        int added = -1;
        int removed = -1;
    };
    // Branches table layout: destructive action first, immediately followed by
    // the branch name. Updated/worktree remain hidden backing columns whose data
    // is folded into the compact Branch delegate.
    static constexpr int kBranchesDeleteColumn = 0;
    static constexpr int kBranchesNameColumn = 1;
    static constexpr int kBranchesStatusColumn = 2;
    static constexpr int kBranchesUpdatedColumn = 3;
    static constexpr int kBranchesWorktreeColumn = 4;
    // Runs on a worker thread: fills the git-derived half of `data`.
    static BranchesPanelData readBranchesPanelGit(BranchesPanelData data);

    void renderBranchesPanel(const BranchesPanelData &data);
    // Fill the compact files/+/- badges after the table is visible. Computing a
    // range numstat for every branch can be expensive in a large repository, so
    // this deliberately runs as a second, cached worker pass rather than holding
    // up the initial Branches render.
    void startBranchChangeStats(const BranchesPanelData &data);
    void loadBranchesPanel();
    QWidget *buildWorktreesTab();
    void loadWorktreesPanel();




    void focusRepoDetailTable(int id);




    void runGitDetached(const QString &dir, const QStringList &args,
                        std::function<void(bool, const QByteArray &)> onDone);






    template <typename T>
    void runOffThread(std::function<T()> work, std::function<void(T)> apply)
    {
        QThread *worker = QThread::create(
            [this, work = std::move(work), apply = std::move(apply)]() mutable {
                T result = work();
                QMetaObject::invokeMethod(
                    this,
                    [apply = std::move(apply), result = std::move(result)]() mutable {
                        apply(std::move(result));
                    },
                    Qt::QueuedConnection);
            });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    }


    int m_worktreeStatusGen = 0;


    void switchToWorktree(const QString &branch);
    // Open a branch in the Git view (adhoc #1; used by the clickable branch
    // links in the agent session header, the PR header, the Branches panel and
    // the graph's branch dropdown — adhoc #123). The graph browses the branch's
    // history and, for any branch other than the compare base, the right pane
    // opens its range diff against that base at the same time — one combined
    // view, "<branch> -> <base>", rather than a separate review page (adhoc
    // #16).
    void switchToBranch(const QString &branch);
    // Base branch of the Git view's comparison. Empty means "the repo's default
    // branch", which is where every comparison starts; the compare indicator's
    // base dropdown sets it, and leaving the compare view clears it (adhoc #16).
    QString m_branchCompareBase;
    QString branchCompareBase() const;
    // Re-diff the branch under review against `base` (the indicator's base
    // dropdown). Empty restores the repo's default branch (adhoc #16).
    void setBranchCompareBase(const QString &base);
    // Leave the branch compare view: right pane back to the working-tree diff,
    // graph back on the default branch, compare base reset (adhoc #16).
    void closeBranchCompareView();
    // Select the worktrees-table row whose branch matches, repopulating the diff
    // pane and detail buttons. Returns false if no such row exists. Used to keep
    // the selection on the worktree being acted on after loadWorktreesPanel()
    // rebuilds the table (which would otherwise clear it — issue #272).
    bool selectWorktreeRow(const QString &branch);



    bool selectBranchRow(const QString &branch);

    void reportBranchNotFound(const QString &branch);
    void updateWorktreeSelection(const QString &branch,
                                 const QString &worktreePath);
    // Merge a worktree's branch into the default branch. On success the now-merged
    // worktree and its branch are removed (the work is preserved in the merge
    // commit); pass its folder so it can be. deleteAgent=true additionally tears
    // down the agent session(s) that produced the branch. Returns true only when
    // the branch's commits provably landed in the base branch — callers that tidy
    // up after themselves (closing the branch diff, adhoc #119) must leave the
    // review open when the merge was refused or conflicted.
    bool mergeWorktreeIntoMain(const QString &branch,
                               const QString &worktreePath = QString(),
                               bool deleteAgent = false);


    void closeBranchDiffAfterMerge();



    bool mergeAgentBranchIntoBase(int repoIndex, const QString &branch,
                                  bool deleteAgent);






    void updateWorktreeFromMain(const QString &worktreePath, const QString &branch,
                                const QString &baseArg = QString());





    bool editWorktreeConflicts(const QString &worktreePath, const QString &branch,
                               const QString &base);

    void resolveWorktreeConflicts();


    void commitWorktreeChanges(const QString &worktreePath, const QString &branch);








    void removeWorktree(const QString &worktreePath, const QString &branch,
                        bool confirm, bool alsoDeleteBranch = true,
                        bool async = false, std::function<void()> onDone = {});



    QString worktreePathForBranch(const QString &repoPath,
                                  const QString &branch) const;



    bool localBranchExists(const QString &repoPath, const QString &branch) const;
    void showBranchDiff(const QString &branch);


    void applyBranchDetailActions(const QString &branch, const QString &base,
                                  int behind, int ahead, bool hasConflict,
                                  bool worktreeConflict = false);
    void maybeAutoPullBranch(const QString &branch);

    int m_branchDetailActionsGen = 0;
    // Render the branch's whole range diff (everything it adds over base).
    void renderBranchScopeDiff();



    QString branchWorkDir(const QString &branch) const;
    // Render an already-captured patch into the branch diff view. `viewedContext`
    // scopes the per-file "viewed" toggles; `emptyMessage` shows when the patch
    // has no changes.
    void renderBranchDiffPatch(const QString &patch, const QString &emptyMessage,
                               const QString &viewedContext);


    void rebuildBranchDiffSpans();




    QWidget *buildBranchRangePane();


    void computeBranchFileTops();


    void applyBranchAutoMarkViewedOnScroll();

    bool branchScrollToAdjacentHunk(int delta);

    void toggleBranchDiffSearch(bool show);
    void branchDiffSearchRecompute();
    void branchDiffSearchGoTo(int delta);


    void openBranchInCodium(const QString &branch);


    void updateBranchDetailActions(const QString &branch);
    void createPullFromBranch(const QString &branch);
    void onBranchDiffAnchorClicked(const QUrl &url);
    void updateBranchDiffSticky();
    QString diffViewedScope(const QString &context) const;
    QSet<QString> loadDiffViewed(const QString &context) const;
    void setDiffViewed(const QString &context, const QString &path, bool viewed);

    void updateBranchFromBase(const QString &branch);


    void openBranchMergeEditor(const QString &branch);




    void pullBaseIntoAllBranches();




    void fixBranchConflictsWithAgent(const QString &branch, const QString &provider,
                                     const QString &model = QString());
    void promptNewBranch();
    void deleteBranch(const QString &branch);



    void deleteRemoteBranch(const QString &branch);


    QString neighbourBranchInList(const QString &branch) const;

    int branchRowInList(const QString &branch) const;




    void flashMergedBranchRow(const QString &branch);
    void clearMergedBranchFlash();


    void deleteMergedBranches();
    void deleteSelectedBranches();
    QWidget *buildReleasesTab();
    void loadReleasesPanel();
    void promptNewRelease();




    void generateReleaseNotesWithAgent(const QString &dir, const QString &prevTag,
                                       const QString &newTag,
                                       const QString &targetRef,
                                       const QString &provider,
                                       const QString &modelChoice,
                                       QPlainTextEdit *notesEdit,
                                       QPushButton *button);
    void pruneReleaseArtifactsForCurrentRepo(const QString &releaseTag);




    void pruneReleaseTagsForCurrentRepo(const QString &keepTag);



    void announceReleaseOnFediverse(const QString &tag, const QString &title,
                                    const QString &notes);

    void showReleaseDetail(const QString &tag);




    QWidget *buildArtifactsTab();
    void loadArtifactsPanel();
    void deleteArtifact(const QString &hash, const QString &label);
    QWidget *buildMirrorNodesTab();






    QWidget *buildShortcutsTab();
    void loadShortcutsPanel();
    QString shortcutsDirPath() const;
    void runShortcut(const QString &filePath);
    void stopShortcut();

    void openShortcutEditor(const QString &filePath);
    void deleteShortcut(const QString &filePath);

    QWidget *buildRepoSettingsTab();
    void refreshRepoSettings();






    CoveStore coveStoreForRepo(int repoIndex) const;
    QString coveRepoSettingsPrefix(int repoIndex) const;
    QString rememberedCovePassword(int repoIndex) const;
    bool coveAutoOpenEnabled(int repoIndex) const;


    bool tryUnlockCove(Cove &cove, int repoIndex, QString *passwordOut) const;
    void logCoveAccessLocal(const Cove &cove);
    QStringList coveAccessLogLocal(const QString &coveId) const;
    void announceCoveOpened(const Cove &cove);
    void openCove(const QString &relPath);
    void openCoveViewer(int repoIndex, Cove cove, const QString &password);
    void promptCreateCove(int repoIndex);
    void rebuildRepoCovesList();
    QWidget *buildCoveSection();
    QWidget *buildCoveGlobalSection();
    void applyCovePasswordFromSettings(int repoIndex, bool global,
                                       const QString &password, bool remember);
    static QByteArray coveOpenCanonical(const QString &coveId, const QString &creatorKey,
                                        const QString &openerKey, qint64 ts);


    void updateRepoSource();
    void reloadRepoRemotesTable();
    void promptAddRepoRemote();
    void promptEditRepoRemote();
    void deleteSelectedRepoRemote();


    void promptSetRepoForkLocation();


    void setRepoGitIdentityFromForkMesh();

    void setRepoActionsEnabled(bool on);

    void setRepoSecretScanningEnabled(bool on);


    void setWorkflowDisabled(const QString &path, bool disabled);

    bool isWorkflowDisabled(const QString &path) const;
    void loadMirrorNodesPanel();





    QString mirrorSelfSnapshotKey(const RepositoryRecord &repo) const;
    void refreshMirrorSelfSnapshot(const RepositoryRecord &repo, const QString &key);
    QHash<QString, MirrorSelfSnapshot> m_mirrorSelfSnapshots;
    QSet<QString> m_mirrorSelfSnapshotsInFlight;



    void animateMirrorNodeLights();
    void updateMirrorNodeLightTimer();
    void requestMirrorNodesRefresh();
    void onMirrorRefreshRequested(const QString &source,
                                  const QString &requesterName);


    void fetchCatalogMirrors(const QString &owner, const QString &repo,
                             const QString &source);
    // Verify that one exact mirror (with failover disabled server-side) can
    // return README.md. Results feed the Reachability column after Artifacts.
    void fetchMirrorReachability(const QString &owner, const QString &repo,
                                 const QString &source, const QString &node);
    // Fetch the worker's per-artifact release download counts (logged each time
    // /releases/blob/sha256/<hash> streams a binary out), so the Releases tab can
    // show how many times each artifact has been downloaded.
    void fetchReleaseDownloadCounts(const QString &owner, const QString &repo,
                                    const QString &source);




    void replicateReleaseArtifacts(int index);



    void downloadNextReleaseBlob(int index, const QString &mirrorPath,
                                 QMap<QString, QString> pending);
    void pushReleaseToMirrors(const QString &tag);
    void deleteTag(const QString &tag);
    bool repoHasWorkingTree() const;
    void loadFileSearchIndex();




    QWidget *createGlobalSearchBox();
    void rebuildGlobalSearchResults();
    void positionGlobalSearchPopup();
    void moveGlobalSearchSelection(int delta);
    void activateGlobalSearchItem(QListWidgetItem *item);
    void hideGlobalSearchPopup();



    void syncGitCommitFilter();



    QWidget *createNavHistoryButtons();
    void scheduleNavRecord();
    void recordNavLocation();
    void restoreNavEntry(int index);
    struct NavPlace;
    void applyNavDetailTab(const NavPlace &place); // re-select a recorded repo tab
    QString navPlaceLabel(const NavPlace &place) const; // human-readable trail destination
    void navigateBack();
    void navigateForward();
    void updateNavHistoryButtons();

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


    bool applyRepoAboutMetadataAt(int index, const QString &about,
                                  const QString &websiteInput,
                                  QString *error = nullptr);


    QUrl repoAboutApiUrl(const QString &owner, const QString &name) const;



    void saveRepoFediverseSettings(const QString &owner, const QString &name,
                                   bool federate, bool broadcastEvents,
                                   bool acceptComments);
    void loadCommits();





    void fillCommitStats(int loadGen);


    void loadMoreCommits();



    void toggleCommitFilesRows(int row);
    void collapseAllCommitFileRows();


    void updateCommitRowHover(int row);


    void updateCommitsUnsyncedFilesPanel();



    void fetchCurrentRepo();
    void pullCurrentRepo();



    QWidget *buildSourceControlPanel();
    void refreshSourceControl();             // re-scan `git status` into the tree
    void refreshSourceControl(bool force);   // force refresh path bypassing cache short-circuit
    // While a branch/PR comparison is open, populate the same CHANGES tree with
    // that range's files while leaving its composer and actions in place.
    void showRangeFilesInSourceControl(const QStringList &paths,
                                       const QStringList &statuses);
    bool sourceControlShowsRange() const;
    QString sourceControlGitDir() const;
    void scrollBranchDiffToFile(const QString &path);
    // Detached `git status` that only updates the activity rail's Git badge, so
    // the uncommitted-file count is right on every repo tab (and right after a
    // repo opens), not just while the changes panel is the visible view.
    void refreshRepoChangeBadge();
    void scmStagePath(const QString &path);
    void scmUnstagePath(const QString &path);
    void scmDiscardPath(const QString &path, bool untracked);
    void scmStageAll();
    void scmUnstageAll();
    void scmDiscardAll();
    void scmCommit();

    void scmCommitAndPush();

    void scmStageAllCommitAndPush();


    bool performScmCommit();

    void showScmDiff(const QString &path, bool staged, bool untracked);


    void showScmDiffAll(bool staged);



    void renderScmCombinedDiff();


    void setupScmDiffPane();


    void scrollScmDiffToFile(const QString &path, bool staged);



    void updateScmDiffScrollState();

    void layoutScmStickyHeader();


    void computeScmFileTops();


    void applyScmAutoMarkViewedOnScroll();


    void selectScmFileInTree(const QString &path, bool staged);

    QTreeWidgetItem *scmFindItem(const QString &path, bool staged) const;

    void updateScmViewedCount();

    void onScmDiffAnchorClicked(const QUrl &url);

    static QString scmViewedContext();



    void scmSelectAdjacentChange(int delta);



    bool scmScrollToAdjacentHunk(int delta, bool fromEnd = false);


    QString scmContextDiff() const;



    QStringList scmDiffScopeArgs() const;






    QString scmHeuristicCommitMessage(int variant = 0) const;



    void autoFillScmMessage();




    void generateScmMessage();




    bool commitsListIsCurrent();



    QString currentMirrorTip() const;



    void refreshCommitMarkersIfStale();



    QSet<QString> unpushedCommitHashes() const;


    void applyCommitIssueClosures();


    int commitStatusCode(const QString &sha) const;

    QString commitStatusGlyph(const QString &sha) const;



    void refreshCommitBarStatusGlyph();
    void refreshCommitTableStatusGlyphs();

    void updateActionsTabIndicator();


    qint64 estimatedRunDurationMs(const ActionRun &run) const;



    void updateAgentsTabIndicator();

    void refreshCommitStatusGlyphs();
    void refreshRepoSecurity();



    RepoSecurityInput buildRepoSecurityInput(const RepositoryRecord &selected,
                                              const RepositoryRecord &writable) const;



    void applyRepoSecuritySnapshot(const RepoSecuritySnapshot &snapshot,
                                   const QString &localBase);





    void runRepoDependencyScan(const QString &manifestPath = QString());
    void refreshRepoQuality();


    void openRepoFileAtLine(const QString &path, int line);
    void loadRepoInsights();



    void showInsightsContributorMenu(const QPoint &pos);
    void reassignContributorIdentity(const QString &oldName);


    void openCommitsForContributor(const QString &author);
    void setRepoBranch(const QString &branch);
    QString repoHeadBranch() const;
    void refreshCommitsBranchButton();
    void createAndCheckoutBranch();
    QString currentRef() const;
    QString repoGitDir() const;
    QString iconsDir() const;
    QIcon iconForFile(const QString &fileName) const;
    QIcon iconForDir(bool opened) const;
    void startRefreshSpin();
    void stopRefreshSpin();



    void startCommitDiffSpin();
    void stopCommitDiffSpin();


    void spinRefreshButton(QPushButton *button);
    void addRefreshSpin(QPushButton *button);



    void startButtonSpin(QPushButton *button);
    void stopButtonSpin(QPushButton *button);





    void startRestartSpin(QPushButton *button);
    void stopRestartSpin();



    void setRestartSpinHourglass(bool hourglass);



    void startNodeSwitchSpin();
    void stopNodeSwitchSpin();
    void startRepoSwitchSpin();
    void stopRepoSwitchSpin();
    void nodeSwitchStep(const QString &what);
    void finishLoadStepTiming();



    void showLoadStatus(const QString &what);





    const RepositoryRecord &writableRecordFor(const RepositoryRecord &repo) const;





    QString repoAgentGitDir(const RepositoryRecord &repo) const;


    int issuesRepoIndex() const;
    IssueStore issueStoreForCurrentRepo() const;
    void refreshIssuesRepoCombo();
    void reloadIssues();
    void reloadIssuesInBackground();
    void applyLoadedIssues(const QString &signature, QList<Issue> issues,
                           QList<IssueLabel> labels,
                           QList<IssueMilestone> milestones);




    void appendCreatedIssue(const IssueStore &store, const Issue &issue);
    void refreshIssueList();
    void resetIssueFilters();


    void selectIssueListTab(int id);
    void refreshIssueMilestones();
    void refreshIssueLabels();




    QWidget *buildIssueBoard();
    void refreshIssueBoard();
    QStringList boardColumns() const;
    void setBoardColumns(const QStringList &cols);
    void editBoardColumns();


    QString issueBoardColumn(const Issue &issue) const;


    void moveIssueToColumn(int number, const QString &column);
    void editIssueLabelDefinition(int row);
    QWidget *makeIssueRow(const Issue &issue,
                          const QHash<QString, QString> &labelColors) const;
    void showIssue(int number);
    void renderIssueThread(const Issue &issue);
    void showIssueBurnupChart();
    void showIssueComposePage(QWidget *page);
    void removeIssueComposePage();
    void setIssueInlineNotice(const QString &message, bool error = false);
    void promptEditIssueTitle();
    void saveIssueTitleEdit();
    void cancelIssueTitleEdit();
    void promptNewIssue();



    void composeNewIssue(const QString &prefillTitle, const QString &prefillBody);


    void promptIssueFromChatMessage(const QString &text, const QString &senderName,
                                    qint64 timestampMs);
    void quickAddIssue();



    void attachQuickAddImage();
    bool tryPasteImageIntoQuickAdd();
    void queueQuickAddImage(const QString &path);
    void removeQuickAddImage(const QString &path);
    void clearQuickAddImages();
    void updateQuickAddImageButton();


    void rebuildQuickAddAttachChips();


    void showQuickAddImageDetail(const QString &path);



    void captureScreenRegion();




    void startVoiceCapture();
    void stopVoiceCapture();



    void startVoiceCaptureFor(QPlainTextEdit *target, QPushButton *button);


    void initializeWorldSpeechBridge();
    void createWorldSpeechPairing();
    void revokeWorldSpeechPairing();
    void cancelWorldVoiceCapture();


    void openVoiceSettings();



    QPushButton *makeVoiceButton(MarkdownEditor *composer);
    void startVoiceTranscription(bool finalPass);
    void applyVoiceTranscript(const QString &text, bool finalPass);
    void updateVoiceInputButton();


    void updateVoiceLevelMeter();
    void stopVoiceLevelMeter();



    void showVoiceTranscribeSpinner();
    void hideVoiceTranscribeSpinner();



    void toggleMicTest();
    void updateMicTestMeter();
    void stopMicTest();

    void installWhisperCpp();

    void installParakeet();

    void installVoiceEngine();
    void refreshWhisperStatus();



    void recordQuickAddHistory(const QString &text);
    bool navigateQuickAddHistory(int direction);




    void openQuickAddSlashActions();
    void populateSlashActionsList();
    void moveSlashActionsSelection(int delta);
    void activateSlashActionRow(QWidget *row);
    void refreshClaudeSlashCommands();









    QStringList agentEffortLevels() const;
    void refreshQuickAddSpeedSelector();



    void refreshClaudeEffortLevels();
    void mentionProjectFileInQuickAdd();


    void showTreasuryDonateDialog();
    void copyIssueToClipboard();
    void copyIssueThreadToClipboard();
    void askAiForCurrentIssue();


    void showIssueAiTyping();
    void hideIssueAiTyping();
    int availableCredits() const;
    void voteOnCurrentIssue();
    void submitIssueVoteToInbox();
    void updateVoteUi();
    void addIssueComment();

    void closeIssueWithComment();
    void attachIssueImage();
    void queueIssueAttachment(const QString &path);
    void toggleIssueStatus();


    int nextVisibleIssueAfter(int number) const;
    void deleteCurrentIssue();


    void deleteCurrentIssueWithHistory(int number);
    void editIssueLabels();
    void editIssueMilestone();


    void editIssueDates();
    void saveIssueDatesInline();
    void editIssuePriority();



    void nudgeIssuePriority(int direction);
    void nudgeIssueProgress(int deltaPercent);
    void editIssueProgress();



    bool handleIssueProgressDrag(QMouseEvent *ev);
    void applyIssueProgressDragAt(const QPoint &pos);
    void commitIssueProgressDrag();

    int m_issueProgressDragRow = -1;

    void editIssueBounty();

    void bountyAllOpenIssues(double amountUsd);



    void reprioritizeBacklog();




    void prioritizeIssuesFromReadme();



    void analyzeIssueCompleteness();


    QString currentRepoReadme() const;


    int estimateIssueProgress(const Issue &issue,
                              const QSet<int> &mergedIssues) const;


    static double openAiEstimateUsd(const Issue &issue);
    void editIssueAssignees();




    void pickIssueAssignees();
    void saveIssueLabelsInline();
    void saveIssueMilestoneInline();
    void saveIssuePriorityInline();
    void saveIssueAssigneesInline();
    void cancelIssueSidebarEditors();
    void updateIssueActionState();




    QWidget *buildProjectsSection();
    ProjectStore projectStoreForCurrentRepo() const;
    void reloadProjects();
    void refreshProjectList();
    void refreshProjectGantt();
    void showProject(int number);
    void promptNewProject();
    void editProjectLinkedIssues();
    void setProjectInlineNotice(const QString &message, bool error = false);


    int projectProgressPercent(const Project &project) const;

    QUrl issuesApiUrl(const RepositoryRecord &repo) const;


    QUrl agentsApiUrl(const RepositoryRecord &repo) const;



    void pushAgentSessionsSnapshot();
    void pushAgentSessionsForRepo(RepositoryRecord repo, QList<AgentSession> sessions);



    void ensureAgentE2EEControlPlane(
        RepositoryRecord repo, std::function<void(bool ok)> onDone);
    bool loadOwnerEncryptionIdentity(MirrorCrypto::Identity *identity,
                                     QString *error = nullptr) const;


    void scheduleAgentSessionsPush();


    void drainAgentPrompts();
    void drainAgentPromptsFor(RepositoryRecord repo);



    void drainOrgAgentJobsFor(RepositoryRecord repo);
    void applyOrgAgentJobsPayload(const RepositoryRecord &repo,
                                  const QJsonArray &jobs);
    void runOrgAgentSafetyCheck(const RepositoryRecord &repo,
                                const QJsonObject &job);
    void reportOrgAgentJob(const RepositoryRecord &repo,
                           const QJsonObject &job,
                           const QString &securityVerdict,
                           const QString &status,
                           int localAgentId = 0,
                           const QString &reason = QString(),
                           const QString &result = QString());
    void reportCompletedOrgAgentJobs();



    void deliverQueuedAgentPrompt(int sessionId, const QString &text);


    void startWebNewAgentForRepo(const RepositoryRecord &repo, const QString &task,
                                 const QString &providerOverride = QString(),
                                 const QStringList &images = QStringList());


    QStringList saveWebAgentImages(const RepositoryRecord &repo,
                                   const QStringList &images);


    QUrl sharesApiUrl(const RepositoryRecord &repo) const;
    void shareRepoRequest(const RepositoryRecord &repo, const QString &grantee,
                          const QString &action);
    void requestPrivateRecipientBundles(
        const RepositoryRecord &repo, bool updateCollaboratorList,
        std::function<void(bool ok, QList<QJsonObject> bundles,
                           QStringList grantees, QString error)> onDone);
    void addRepoCollaborator(const QString &nameRaw);
    void removeRepoCollaborator(const QString &nameRaw);
    void refreshRepoCollaborators();


    void showBountyQrDialog(const RepositoryRecord &repo, int number,
                            const QString &uri, const QString &address,
                            double amountUsd, const QString &amountSol,
                            const QString &kind = QString(),
                            const QString &payee = QString());


    void showBountyWalletDialog();
    void submitIssueCommentToInbox(const QString &body,
                                   const QStringList &attachmentSrcPaths = {},
                                   const QStringList &attachmentPlaceholders = {});



    void submitIssueAssigneesToInbox(int number, const QStringList &assignees);





    bool submitNewIssueToInbox(const QString &title, const QString &body,
                               const QStringList &labels, const QString &milestone,
                               int priority, const QStringList &assignees,
                               const QStringList &attachmentSrcPaths = {},
                               const QStringList &attachmentPlaceholders = {},
                               std::function<void(bool ok, const QString &error)> onDone = {});
    void syncIssuesInbox();




    void drainIssuesInboxFor(RepositoryRecord repo, bool interactive);
    void pollMirrorIssueInboxes();
    void drainPullsInboxFor(RepositoryRecord repo, bool interactive);




    void scanRepoMentionsFor(const RepositoryRecord &repo);



    void applyRepoMentions(const RepositoryRecord &repo,
                           const QList<Issue> &allIssues,
                           const QList<PullRequest> &allPulls);


    void pollOwnedInboxes();




    void scheduleRelaySync();
    void performRelaySync();



    void applyIssuesInboxPayload(const RepositoryRecord &repo,
                                 const QJsonArray &pending, bool interactive,
                                 bool mirrorIntake = false);
    void applyPullsInboxPayload(const RepositoryRecord &repo,
                                const QJsonArray &pending, bool interactive,
                                bool mirrorIntake = false);
    void applyDiscussionsInboxPayload(const RepositoryRecord &repo,
                                      const QJsonArray &pending,
                                      bool interactive,
                                      bool mirrorIntake = false);
    void applyAgentPromptsPayload(const RepositoryRecord &repo,
                                  const QJsonArray &prompts);
    void acknowledgeAgentPrompts(const RepositoryRecord &repo,
                                 const QList<qint64> &queueIds);


    QUrlQuery signedInboxQuery(const QString &owner) const;




    void appendMirrorStateAttestation(QUrlQuery *query,
                                      const RepositoryRecord &repo,
                                      const QString &signer) const;

    void backUpIdentityKey();
    void refreshIdentityBackupNag();


    void showClaudeCodeDeviceSetup();
    void chooseAvatar();
    void setSettingsAvatar(const QByteArray &pngData);


    QByteArray effectiveAvatar();
    QByteArray effectiveUserAvatar();



    void pushAccountAvatar();


    void adoptWebAccountAvatar(const QByteArray &png);
    void updateAvatarButton();
    void updateUserAvatarButton();


    void updateAdminCrownBadge();
    void refreshIssueComposerAvatar();



    QWidget *makeComposerIdentity(QLabel **outAvatar = nullptr,
                                  const QString &verb = QString());
    void logout();



    void revokeAccountSession();




    bool promptRelogin(const QString &previousAccount);



    void loginToUserAccount();


    void uninstallForkMesh();
    void rebuildAndRelaunch();
    void showSection(int index);
    void updateHomeStats();
    QString selfNodeStats() const;
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
    void openChatThread(const QString &rootMessageId);
    void rebuildChatThreadDialog();
    void sendChatThreadReply();
    int chatThreadReplyCount(const QString &conversation,
                             const QString &rootMessageId) const;
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




    void appendNetworkLogLine(const QString &storedLine);
    QString m_lastLogRenderDate;





    bool m_networkLogViewStale = false;



    QString logBadgeFor(const QString &storedLine) const;
    QString logAccentFor(const QString &storedLine) const;
    void rebuildLogFilterButtons();

    QString logFilterChipLabel(const QString &name, const QString &category) const;
    void updateLogFilterChipCounts();
    void rebuildNetworkLogView();
    QString networkLogPath() const;
    void loadNetworkLog();
    void saveNetworkLog();




    int m_logRenderFrom = 0;




    bool m_logViewMutating = false;


    bool m_logFilterEmptyNotice = false;
    void loadOlderNetworkLogSegment();
    void onNetworkLogScrolled(int value);





    void flashMessage(const QString &text, bool error = false,
                      const QString &clickHref = QString());
    void dismissTopMessage();
    void advanceTopMessageQueue();
    void queueTopMessage(const QString &text, bool error);
    bool topMessageBusy() const;
    void renderTopMessageCountdown();
    void renderTopMessage();
    void positionTopMessageOverlay();



    bool topMessageDockVisible() const;
    MessageRow *addMessageRow(const ChatMessage &message);
    MessageRow *createMessageRow(const ChatMessage &message,
                                 bool threadContext = false);
    void renderConversationRows(); // rebuilds rows in place; caller handles scrolling
    void rebuildConversationView();
    void scrollToBottom();
    void setChannels(const QStringList &channels);
    void setRoster(const QList<MemberInfo> &members);





    QString welcomeChannelForIdentity() const;
    void maybeAnnounceWelcome();
    void removeChatMember(const QString &id, const QString &name);


    void switchConversation(const QString &conversation);
    void openDirectChat(const QString &peerId, const QString &peerName);
    void refreshChannelList();
    void refreshDmList();


    void refreshChatMembers();
    void refreshChatUserDirectory();
    void mergeChatUserDirectory(const QJsonArray &users);


    void showChatUserProfile(const MemberInfo &member, const QStringList &nodeLines);
    void promptAddChannel();


    void promptAddPrivateChannel();

    void promptInviteToPrivateChannel();


    void promptDeleteRoom(const QString &channel);


    void showEmojiPicker(QWidget *anchor);
    void insertEmojiIntoComposer(const QString &emoji);



    void sendMessageToPrompt(const QString &text);


    void restorePrivateChannels();

    void persistPrivateChannels();
    void sendCurrentMessage();





    void maybeAskForkbot(const QString &conversation, const QString &text);
    void onComposerEdited(const QString &text);



    void refreshMentionCandidates();
    void updateMentionPopup();
    void insertMention(const QString &name);
    void sendTypingState(bool active);
    void refreshTypingLabel();
    void attachFile();

    void showChatImageDetail(const QString &fileName, const QByteArray &data);

    bool trySendClipboardImage();
    void saveIncomingFile(const QString &fileName, const QByteArray &data);

    QString chatHistoryKey() const;
    QString chatHistoryPath() const;
    void saveChatHistory();
    void loadChatHistory();
    void scheduleChatSave();



    void pruneExpiredChatHistory();



    QString avatarCachePath(const QString &peerId) const;
    void loadCachedAvatars();
    void loadRepositories();









    bool reconcileMirrorPath(RepositoryRecord &repo);




    void adoptMaterializedMirror(RepositoryRecord &repo,
                                 const QString &repositoryPath);
    void saveRepositories() const;
    void refreshRepositoryList();




    QStringList mentionCandidateNames() const;
    void promptAddRepository();




    void createNewRepository();







    int provisionNewRepository(const QString &dest, const QString &name,
                               const QString &description,
                               const QString &firstPrompt, bool addReadme,
                               bool isPrivate, QString *error);



    void importRemoteRepository();


    QStringList importAuthGitArgs(const QString &url) const;
    void previewAdvertisedRepo(const QString &ownerName);
    void mirrorAdvertisedRepo(const QString &ownerName);
    void mirrorPreviewRepository(int index);
    void syncRepository(int index, bool quiet = false);




    void pushToSshMirrorRemotes(int index);
    int pushToSshMirrorRemotes(int index, bool userInitiated,
                               const QString &releaseTag);
    void syncPublicEncryptedRepository(int index, bool quiet = false);
    void syncPrivateRepository(int index, bool quiet = false);
    void syncPrivateRepositoryWithRecipients(
        int index, bool quiet,
        const QList<QJsonObject> &recipientBundles);
    void resealPrivateRepositoryRecipients(
        const RepositoryRecord &repo,
        const QList<QJsonObject> &recipientBundles,
        std::function<void(bool ok, QString error)> onDone);
    void ensurePrivateMirrorRecipientIdentityRegistered(
        std::function<void(bool ok, QString error)> onDone = {});
    void downloadPrivateReplica(int index, bool quiet = false);


    void startSyncFetch(int index, bool quiet, bool hasMirror,
                        const QStringList &args, const QString &beforeDigest,
                        const QString &beforeHeadCommit);


    bool completePendingRepoAutoOpen(int index);
    void autoSyncMirrors();




    void autoSyncMirrorsIfRelayHealthy();


    void syncMirrorsBehindRoster();



    bool mirrorHasCommit(const QString &mirrorPath, const QString &commit);
    QSet<QString> m_mirrorCommitsPresent;





    void convergeSourceRepoFromMesh(int index);
    QHash<QString, qint64> m_sourceConvergeAttemptMs;



    void propagateRepoUpdate(int index);


    void onPeerMirrorUpdated(const QString &ownerName, const QString &peerName,
                             const QString &commit);


    void onPeerMirrorSynced(const QString &ownerName, const QString &peerName,
                            const QString &commit);




    bool applyPeerMirrorCommit(const QString &ownerName, const QString &peerName,
                               const QString &commit);


    void onCoveOpened(const QString &creatorKey, const QString &coveId,
                      const QString &coveName, const QString &openerKey,
                      const QString &openerName, qint64 ts, const QString &signature);


    void onCoveInvited(const QString &inviteeAccount, const QString &coveId,
                       const QString &coveName, const QString &inviterName, qint64 ts);
    void quickRebuildRestart();


    void maybeStartQueuedRebuild();
    void changeMirrorLocation();
    void changePreviewCacheLocation();
    void publishRepositoryAfterMirrorRefresh(int index,
                                             bool showDialogOnError = true,
                                             bool quietSync = false);
    void publishRepository(int index, bool showDialogOnError = true);
    void scheduleCatalogPublish(const QString &key, bool showDialogOnError,
                                qint64 minDelayMs = 0);
    void publishRepositoryNow(int index, bool showDialogOnError);
    void ensurePrivateRepositoryControlPlane(int index,
                                             bool showDialogOnError);
    void registerPrivateReplicaRoute(int index);
    int repositoryIndexForCatalogPublishKey(const QString &key) const;
    QString catalogPublishKey(const RepositoryRecord &repo) const;
    void updateRepoActionMenus();
    void deleteCurrentMirror();
    void deleteRepositoryAt(int index, bool reopenRepoDetail);
    void updateRepoDetailStatus();
    void startRepoHosts();
    void stopRepoHosts();



    void startNodeEventSocket();
    void stopNodeEventSocket();
    void onRequestServed(const QString &owner, const QString &name, bool clone);
    void loadRepoStats();
    void saveRepoStats() const;
    QString repositorySource(const RepositoryRecord &repo) const;




    QStringList viewAuthGitArgs(const RepositoryRecord &repo,
                                const QString &source) const;
    QByteArray privateReplicaAuthorization(
        const RepositoryRecord &repo) const;
    QString repositoryChannel(const RepositoryRecord &repo) const;
    QString repositoryMirrorRoot() const;
    QString repositoryPreviewRoot() const;
    QString repositoryPreviewPath(const QString &owner, const QString &name) const;
    QString repositoryNetworkCloneUrl(const QString &owner, const QString &name) const;
    QString repositoryWebUrl(const QString &owner, const QString &name) const;
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



    QVBoxLayout *m_appNavigationRailLayout = nullptr;
    QSystemTrayIcon *m_trayIcon;


    QList<ServerConfig> m_servers;
    int m_activeServer = 0;
    QHash<QString, QPixmap> m_faviconCache;
    QSet<QString> m_faviconFetching;
    QSet<QString> m_faviconMissing;


    QWidget *m_solanaBanner = nullptr;
    QLabel *m_solanaBannerLabel = nullptr;



    QWidget *m_walletVerifyBanner = nullptr;
    QWidget *buildWalletVerifyNotice();
    void updateWalletVerifyNotice();


    QLabel *m_breadcrumb = nullptr;


    QPushButton *m_relayMenuButton = nullptr;




    QLabel *m_relayJoinDot = nullptr;
    QPushButton *m_relayJoinApproveButton = nullptr;
    int m_pendingRelayJoins = 0;





    RelaySpeedDot *m_relaySpeedDot = nullptr;


    void setRelayLinkSpeed(const QString &host, int ms);
    void refreshRelayMenuTooltip();



    struct RelayLatencySample {
        int ms = -1;
        qint64 stampMs = 0;
    };
    QHash<QString, RelayLatencySample> m_relayHostLatency;
    QSet<QString> m_relaySpeedProbes;
    void probeRelayHostSpeed(const QString &serverUrl,
                             std::function<void(int)> done);


    QString relaySpeedText(const QString &host) const;
    QString relayMenuEntryText(const QString &host) const;
    QTimer *m_relayLatencyTimer = nullptr;
    bool m_relayProbeInFlight = false;
    qint64 m_lastWsLatencySampleMs = 0;
    int m_relayProbeFailures = 0;


    int m_relayProbeElevated = 0;





    QLabel *m_nodeLabel = nullptr;
    QLabel *m_repoLabel = nullptr;



    QLabel *m_navSolanaBalance = nullptr;




    QWidget *m_navTokenUsage = nullptr;
    QWidget *m_navCodexUsage = nullptr;








    QAbstractButton *m_nodeOnlineToggle = nullptr;
    QLabel *m_nodeOnlineStatusLabel = nullptr;
    QLabel *m_nodeRewardStatus = nullptr;
    QLabel *m_nodeUptimeLabel = nullptr;
    QWidget *m_profileOnlineSection = nullptr;
    bool m_nodeOffline = false;



    qint64 m_navSolanaLamports = -1;



    qint64 m_navSolanaFetchedMs = 0;
    bool m_navSolanaFetchInFlight = false;


    QHash<QString, QPair<double, qint64>> m_navFiatRates;
    bool m_navFiatFetchInFlight = false;


    QString m_webSolanaAccount;
    QString m_webSolanaAddress;
    bool m_webSolanaKnown = false;
    bool m_webSolanaFetchInFlight = false;
    qint64 m_webSolanaFetchedMs = 0;
    QTimer *m_webSolanaTimer = nullptr;
    QPushButton *m_chatButton = nullptr;
    QPushButton *m_tasksNavButton = nullptr;
    QTableWidget *m_organizationTasksTable = nullptr;
    QTableWidget *m_organizationTaskQueueTable = nullptr;
    QLineEdit *m_organizationTasksSearch = nullptr;
    QTextBrowser *m_organizationTaskDetail = nullptr;
    QLabel *m_organizationTasksStatus = nullptr;
    QLabel *m_organizationTasksSummary = nullptr;
    QPushButton *m_organizationTaskNewButton = nullptr;
    QPushButton *m_organizationTaskFollowUpButton = nullptr;
    QPushButton *m_organizationTaskQueueMoveButton = nullptr;
    QPushButton *m_organizationTaskEditButton = nullptr;
    QPushButton *m_organizationTaskAgentButton = nullptr;
    QPushButton *m_organizationTaskStartButton = nullptr;
    QPushButton *m_organizationTaskCompleteButton = nullptr;
    QPushButton *m_organizationTaskQaButton = nullptr;
    QPushButton *m_organizationTaskReturnButton = nullptr;
    QPushButton *m_organizationTaskDeleteButton = nullptr;
    QJsonArray m_organizationTasks;
    QStringList m_organizationTaskMembers;
    QStringList m_organizationTaskDepartments;
    QString m_organizationTaskActor;
    bool m_organizationTasksCanManage = false;
    bool m_organizationTasksLoading = false;



    QPushButton *m_agentsNavButton = nullptr;
    AgentDotMatrix *m_agentDotMatrix = nullptr;


    NodeDotMatrix *m_nodeDotMatrix = nullptr;
    QWidget *m_chromeDotDivider = nullptr;
    void refreshNodeDotMatrix();
    void updateChromeDotDivider();
    ActionRunStrip *m_actionRunStrip = nullptr;


    QString m_agentDotTooltipKey;

    QString m_actionRunStripTooltipKey;


    QLabel *m_connectionDot = nullptr;
    QString m_connectionStatusColor;


    QLabel *m_adminCrownBadge = nullptr;
    QLabel *m_topMessage = nullptr;
    QFrame *m_topMessageContainer = nullptr;
    QTimer *m_topMessageTimer = nullptr;
    QPushButton *m_topMessageCopy = nullptr;
    QPushButton *m_topMessageClose = nullptr;
    QPushButton *m_topMessageExpand = nullptr;
    QFrame *m_topMessageOverlay = nullptr;
    QLabel *m_topMessageOverlayText = nullptr;
    QString m_topMessageRaw;
    QString m_topMessageBaseHtml;
    QString m_topMessageHref;
    int m_topMessageSecondsLeft = 0;






    QList<QPair<QString, bool>> m_topMessageQueue;
    bool m_topMessageError = false;


    QWidget *m_errorBorderOverlay = nullptr;
    QTimer *m_errorBorderTimer = nullptr;
    bool m_topMessageElided = false;
    bool m_topMessageExpanded = false;
    bool m_repoPinMismatch = false;
    QHash<QString, qint64> m_repoPinAutoHealAtMs;


    QLineEdit *m_nameEdit;
    QLineEdit *m_solanaEdit;
    QLabel *m_pubkeyLabel;
    QLineEdit *m_serverUrlEdit;
    QLineEdit *m_roomNameEdit;
    QLabel *m_setupError;
    QPushButton *m_updateButton;
    QLabel *m_updateStatus;


    QLabel *m_buildStatusLabel = nullptr;
    QPushButton *m_buildButton = nullptr;


    QDialog *m_updateLogDialog = nullptr;
    QPlainTextEdit *m_updateLog = nullptr;






    QTextEdit *m_footerUpdateLog = nullptr;



    QPushButton *m_footerLogPauseButton = nullptr;
    bool m_footerLogScrollPaused = false;


    QWidget *m_footerDock = nullptr;




    QFrame *m_backgroundQueue = nullptr;
    QLabel *m_backgroundQueueTitle = nullptr;


    QLabel *m_backgroundQueueIdleLabel = nullptr;
    QWidget *m_backgroundQueueRowsHost = nullptr;
    QVBoxLayout *m_backgroundQueueRowsLayout = nullptr;
    QScrollArea *m_backgroundQueueScroll = nullptr;
    QHash<QString, QWidget *> m_backgroundTaskRows;
    QHash<QString, QLabel *> m_backgroundTaskSpinners;
    QHash<QString, QLabel *> m_backgroundTaskLabels;
    QHash<QString, int> m_backgroundTaskCounts;
    QHash<QString, qint64> m_backgroundTaskSince;
    QHash<QString, QString> m_backgroundTaskDetails;
    QHash<quint64, QString> m_backgroundTaskWords;
    QHash<QString, bool> m_backgroundTaskHadUiBlocking;



    struct BackgroundOutcomeTally {
        int runs = 0;
        qint64 longestMs = 0;
        qint64 firstAt = 0;
        QString detail;
    };
    QHash<QString, BackgroundOutcomeTally> m_backgroundTaskDone;
    QHash<QString, BackgroundOutcomeTally> m_backgroundTaskUiBlocking;
    QTimer *m_backgroundTaskSpinTimer = nullptr;
    int m_backgroundTaskRowHeight = 18;
    int m_backgroundTaskSpinFrame = 0;
    int m_backgroundTaskIdleTicks = 0;


    QString m_updateAsUser;


    QLabel *m_statusLine;
    QLabel *m_channelTitle;
    QLabel *m_encryptionLabel;
    QPushButton *m_inviteButton = nullptr;
    QListWidget *m_channelList;
    QPushButton *m_nodeMenuButton = nullptr;
    QString m_navSolanaBalanceAddress;


    struct NodeMenuEntry {
        QString name;
        QString platform;
        bool online = false;
        bool self = false;
        int repoCount = 0;
    };
    QList<NodeMenuEntry> m_nodeMenuEntries;




    QList<NodeMenuEntry> m_nodeDotEntries;




    struct NodeDotRepoState {
        bool behind = false;
        bool integrityFailing = false;
    };
    QHash<QString, NodeDotRepoState> m_nodeDotRepoStates;
    void setNodeDotRepoStates(const QHash<QString, NodeDotRepoState> &states);
    QString m_selectedNode;
    QPushButton *m_repoMenuButton = nullptr;
    QPushButton *m_repoViewButton = nullptr;
    QPushButton *m_reposNavButton = nullptr;
    QPushButton *m_settingsNavButton = nullptr;
    QPushButton *m_logNavButton = nullptr;
    QPushButton *m_floatingLogButton = nullptr;
    QPushButton *m_controlNodeNavButton = nullptr;



    QPushButton *m_networkNavButton = nullptr;
    QPushButton *m_navRebuildButton = nullptr;




    QPushButton *m_navSignInButton = nullptr;
    QPushButton *m_navScreenshotButton = nullptr;
    QPushButton *m_navResizeButton = nullptr;
    QPushButton *m_restartSpinButton = nullptr;


    bool m_rebuildRestartQueued = false;



    QTimer *m_rebuildQueuePollTimer = nullptr;
    QTableWidget *m_networkReposTable = nullptr;
    QLabel *m_networkReposStatus = nullptr;
    QPushButton *m_networkReposRefreshButton = nullptr;
    int m_networkReposLoadGen = 0;
    QJsonArray m_networkReposLastPayload;


    int m_networkRepoRowCount = -1;




    QHash<QString, QString> m_privateCatalogAccessIds;





    QTabWidget *m_controlNodeTabs = nullptr;
    int m_controlCloudflareTabIndex = -1;
    QLabel *m_controlNodeStatus = nullptr;
    QLabel *m_controlNodeHealth = nullptr;
    QLabel *m_controlIdentityStatus = nullptr;
    QLabel *m_controlWalletStatus = nullptr;
    QLabel *m_controlHostsStatus = nullptr;
    QTableWidget *m_controlPermissionsTable = nullptr;
    bool m_controlRefreshingPermissions = false;
    QLineEdit *m_controlWalletEdit = nullptr;
    QLineEdit *m_cloudflareHostnameEdit = nullptr;
    QLineEdit *m_cloudflareMirrorHostnameEdit = nullptr;
    QLineEdit *m_cloudflareZoneEdit = nullptr;
    QLineEdit *m_cloudflareAccountEdit = nullptr;
    QLineEdit *m_cloudflareNodeNameEdit = nullptr;
    QLineEdit *m_cloudflareRelayLabelEdit = nullptr;
    QLineEdit *m_cloudflareMainRelayEdit = nullptr;
    QLineEdit *m_cloudflareTokenEdit = nullptr;
    QLineEdit *m_cloudflareVpsHostEdit = nullptr;
    QLineEdit *m_cloudflareVpsUserEdit = nullptr;
    QLineEdit *m_cloudflareVpsPasswordEdit = nullptr;
    QCheckBox *m_cloudflareInstallVpsCheck = nullptr;
    QCheckBox *m_cloudflareConnectCheck = nullptr;
    QPushButton *m_cloudflareDryRunButton = nullptr;
    QPushButton *m_cloudflareDeployButton = nullptr;
    QPushButton *m_cloudflareCancelButton = nullptr;
    QPlainTextEdit *m_controlNodeOutput = nullptr;



    QLineEdit *m_controlTokenEdit = nullptr;
    QLabel *m_controlTokenStatus = nullptr;
    QLabel *m_controlTokenTargets = nullptr;
    QTableWidget *m_controlTokenTable = nullptr;
    QPushButton *m_controlTokenTestButton = nullptr;
    QPushButton *m_controlTokenGenerateButton = nullptr;
    QPlainTextEdit *m_controlTokenOutput = nullptr;
    QString m_controlTokenSecret;
    QStringList m_controlTokenGrantedGroups;
    QHash<QString, QString> m_controlTokenProbeResults;
    QString m_controlTokenAccountId;
    QString m_controlTokenZoneId;
    QString m_controlTokenUserResource;
    bool m_controlTokenPolicyReadable = false;
    bool m_controlTokenBusy = false;

    QPushButton *m_siteDeployButton = nullptr;
    QPushButton *m_siteDeployCancelButton = nullptr;
    QLabel *m_siteDeployStatus = nullptr;
    QPlainTextEdit *m_siteDeployOutput = nullptr;
    QProcess *m_siteDeployProcess = nullptr;
    QProcess *m_cloudflareBootstrapProcess = nullptr;
    QProcess *m_cloudflareTunnelBootstrapProcess = nullptr;
    QProcess *m_cloudflaredInstallProcess = nullptr;
    QProcess *m_mirrorGatewayProcess = nullptr;
    QProcess *m_cloudflaredProcess = nullptr;
    QString m_cloudflareActiveSecret;
    QString m_cloudflareDeployHostname;
    QString m_directMirrorHostname;
    QString m_directMirrorRouterPublicKey;
    bool m_directMirrorGatewayHealthy = false;
    bool m_directMirrorEndpointRegistered = false;
    bool m_managedCloudflaredVerified = false;
    bool m_cloudflareConnectAfterDeploy = false;
    bool m_cloudflareInstallVpsAfterDeploy = false;
    QTimer *m_controlNodeRefreshTimer = nullptr;
    QTimer *m_directMirrorRegistrationTimer = nullptr;



    QHash<QString, bool> m_controlMirrorReadyCache;
    bool m_controlMirrorProbeInFlight = false;
    qint64 m_controlMirrorProbeCompletedAtMs = 0;
    QString m_controlPermissionsSignature;



    QLabel *m_rewardPoolVaultStatus = nullptr;
    QLabel *m_rewardPoolAddress = nullptr;
    QLabel *m_rewardPoolStatus = nullptr;
    QLineEdit *m_rewardPoolRpcEdit = nullptr;
    QComboBox *m_rewardPoolNetworkCombo = nullptr;
    QTableWidget *m_rewardPoolIntentsTable = nullptr;
    QPushButton *m_rewardPoolFetchButton = nullptr;
    QPushButton *m_rewardPoolSignButton = nullptr;
    QPushButton *m_rewardPoolReconcileButton = nullptr;
    QHash<QString, QJsonObject> m_rewardPoolIntents;
    bool m_rewardPoolBusy = false;
    QTimer *m_rewardPoolFinalizeTimer = nullptr;

    QLineEdit *m_hostIpEdit = nullptr;
    QLineEdit *m_hostUserEdit = nullptr;
    QLineEdit *m_hostPassEdit = nullptr;
    QLineEdit *m_hostNameEdit = nullptr;


    QHash<QString, QString> m_hostSessionPasswords;



    QCheckBox *m_hostUploadBinaryCheck = nullptr;
    QPushButton *m_hostAddButton = nullptr;
    QPushButton *m_hostInstallButton = nullptr;


    QPushButton *m_hostInstallAllButton = nullptr;

    QPushButton *m_hostReinstallAllButton = nullptr;


    QPushButton *m_hostUpdateAllSourceButton = nullptr;
    QLabel *m_hostInstallStatus = nullptr;
    QPlainTextEdit *m_hostInstallLog = nullptr;



    QString m_hostInstallLogCarry;
    int m_hostInstallLogFg = -1;
    bool m_hostInstallLogBold = false;



    QString m_hostInstallRawTail;




    QString m_hostInstallAttemptBanner;


    QString m_hostInstallLastFailure;
    QTableWidget *m_hostsTable = nullptr;
    QTimer *m_hostProbeTimer = nullptr;
    QSet<QString> m_hostProbesInFlight;
    QHash<QString, QString> m_hostReachability;
    QHash<QString, QString> m_hostClaudeAvailability;
    QHash<QString, QString> m_hostCodexAvailability;
    QProcess *m_hostInstallProcess = nullptr;
    QProcess *m_hostLogProcess = nullptr;
    QProcess *m_hostActionsProcess = nullptr;
    QProcess *m_hostAgentInstallProcess = nullptr;
    QProcess *m_hostDiskProcess = nullptr;


    QLineEdit *m_vultrApiKeyEdit = nullptr;



    QString m_vultrRememberedKey;
    QLineEdit *m_vultrNameEdit = nullptr;



    QCheckBox *m_vultrAgentClisCheck = nullptr;
    QPushButton *m_vultrCreateButton = nullptr;
    QLabel *m_vultrStatus = nullptr;
    bool m_vultrProvisionActive = false;
    int m_vultrPollCount = 0;
    int m_vultrInstallAttempts = 0;





    bool m_vultrInstallUseLocalBinary = false;


    bool m_vultrInstallAgentClis = false;
    QString m_vultrDnsHostname;      // Cloudflare name provisioned this run
    // Session-only Cloudflare credential for the fresh node's Tunnel
    // bootstrap. It is sent as the first SSH stdin line, never argv/logged,
    // and scrubbed as soon as provisioning reaches a terminal state.
    QString m_vultrTunnelApiToken;
    // Non-secret billing/provenance facts captured from Vultr's selected plan
    // and created instance. These are persisted with the saved Host row so an
    // operator can identify the plan and expected monthly cost later.
    QJsonObject m_vultrHostMetadata;


    QStringList m_vultrInstallAttemptLog;



    QString m_hostInstallLinkTail;
    bool m_hostLinkPrompted = false;






    QLabel *m_hostDeployLabel = nullptr;
    QWidget *m_hostDeployPanel = nullptr;
    QGridLayout *m_hostDeployGrid = nullptr;
    QList<HostDeploySession *> m_hostDeploySessions;
    int m_hostDeployRemaining = 0;
    int m_hostDeployFailed = 0;


    QTableWidget *m_relaysTable = nullptr;
    QLabel *m_relaysStatus = nullptr;
    QPushButton *m_relaysRefreshButton = nullptr;
    int m_relayProbesInFlight = 0;

    QTableWidget *m_nodesTable = nullptr;
    QLabel *m_nodesStatus = nullptr;
    QPushButton *m_nodesRefreshButton = nullptr;
    QScrollArea *m_nodeDetailScroll = nullptr;




    QSet<QString> m_relayOnlineNodes;
    qint64 m_relayOnlineNodesFetchedMs = 0;




    bool m_relayOnlineNodesFetched = false;



    struct FirewallHistoryEntry {
        QString method;
        QString url;
        QString destination;
        QString rule;
        bool allowed = false;
        qint64 timestampMs = 0;
    };
    QCheckBox *m_requestFirewallEnabledCheck = nullptr;
    QLabel *m_requestFirewallStatus = nullptr;
    QLineEdit *m_requestFirewallRuleEdit = nullptr;
    QTableWidget *m_requestFirewallRulesTable = nullptr;
    QTableWidget *m_requestFirewallHistoryTable = nullptr;
    QPushButton *m_requestFirewallRemoveButton = nullptr;
    QList<FirewallHistoryEntry> m_requestFirewallHistory;
    QLabel *m_networkDiagnosticsStatus = nullptr;
    QTableWidget *m_networkEndpointsTable = nullptr;
    QTableWidget *m_networkDiagnosticsTable = nullptr;
    QPushButton *m_networkDiagnosticsRefreshButton = nullptr;
    bool m_networkEndpointFadeScheduled = false;
    bool m_networkEndpointsUserSorted = false;



    QTabWidget *m_networkTabs = nullptr;
    static constexpr int kNetworkRelaysTab = 0;
    static constexpr int kNetworkNodesTab = 1;
    static constexpr int kNetworkHostsTab = 2;
    int m_networkRelayCount = 0;
    int m_networkNodeCount = 0;
    int m_networkHostCount = 0;
    int m_repoPinCheckIndex = -1;            // repo index an in-flight pin check belongs to
    // One row per repo of the selected node, shown in the repo dropdown.
    struct RepoMenuEntry {
        QString label;
        QIcon icon;
        int index = -1;
        QString advertised;
        QString detail;
    };
    QList<RepoMenuEntry> m_repoMenuEntries;
    QListWidget *m_dmList;



    QPushButton *m_chatMembersButton = nullptr;
    QVBoxLayout *m_chatMembersLayout = nullptr;
    QLabel *m_chatMembersHeading = nullptr;
    QWidget *m_firewallBanner;
    QLabel *m_firewallBannerLabel;
    QPushButton *m_firewallAllowButton;
    QString m_firewallPrivilegedCommand;


    QWidget *m_chatUnreadBanner = nullptr;
    QLabel *m_chatUnreadBannerLabel = nullptr;
    QScrollArea *m_messageScroll;
    QWidget *m_messageContainer;
    QVBoxLayout *m_messageLayout;


    bool m_stickToBottom = true;
    QLabel *m_typingLabel;
    QLineEdit *m_messageInput;



    QCompleter *m_mentionCompleter = nullptr;
    QStringListModel *m_mentionModel = nullptr;






    QAbstractItemView *m_mentionCompleterPopup = nullptr;


    QLineEdit *m_settingsNameEdit = nullptr;
    QLineEdit *m_settingsMachineNodeEdit = nullptr;
    QLineEdit *m_settingsNodeLabelsEdit = nullptr;
    QLineEdit *m_settingsSolanaEdit = nullptr;
    QLabel *m_settingsEmailLabel = nullptr;
    QLabel *m_settingsEmailVerifiedBadge = nullptr;
    QLabel *m_settingsAvatarPreview = nullptr;
    QLabel *m_identityBackupNag = nullptr;
    QTextBrowser *m_settingsLog = nullptr;


    QListWidget *m_logEventList = nullptr;
    QPushButton *m_logScrollLockButton = nullptr;
    bool m_logScrollLocked = false;
    QHBoxLayout *m_logFilterRow = nullptr;
    QButtonGroup *m_logFilterGroup = nullptr;
    QString m_logFilter;
    QPushButton *m_rebuildButton = nullptr;
    QLabel *m_rebuildStatus = nullptr;
    QLineEdit *m_mirrorRootEdit = nullptr;
    QLineEdit *m_previewCacheRootEdit = nullptr;

    QTableWidget *m_dataDirTable = nullptr;
    QLabel *m_dataStatus = nullptr;

    QTableWidget *m_backupTable = nullptr;
    QCheckBox *m_backupEnabledCheck = nullptr;
    QSpinBox *m_backupKeepSpin = nullptr;
    QLabel *m_backupStatus = nullptr;
    QPushButton *m_backupNowButton = nullptr;
    QTimer *m_backupTimer = nullptr;
    QProcess *m_backupProcess = nullptr;

    QLineEdit *m_importUrlEdit = nullptr;
    QPushButton *m_importButton = nullptr;
    QLabel *m_importStatus = nullptr;
    QCheckBox *m_autostartCheck = nullptr;
    QLabel *m_autostartInfo = nullptr;
    QPushButton *m_autostartRemoveButton = nullptr;
    QComboBox *m_themeCombo = nullptr;


    QComboBox *m_defaultAgentProviderCombo = nullptr;
    QLineEdit *m_maxRunningAgentsEdit = nullptr;
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
    QPlainTextEdit *m_prioritizePromptEdit = nullptr;
    QTimer *m_mirrorSyncTimer = nullptr;
    QTimer *m_inboxPollTimer = nullptr;

    QTimer *m_relaySyncDebounce = nullptr;


    bool m_relaySyncSupported = true;
    bool m_relaySyncInFlight = false;
    QSet<QString> m_mirrorIssueIntakeInFlight;
    QTimer *m_autoUpdateTimer = nullptr;
    bool m_autoUpdateChecking = false;


    QLineEdit *m_issueSearch = nullptr;
    QComboBox *m_issuesRepoCombo = nullptr;
    QComboBox *m_issueStatusFilter = nullptr;
    QComboBox *m_issueLabelFilter = nullptr;
    QComboBox *m_issueMilestoneFilter = nullptr;
    QTableWidget *m_issueTable = nullptr;
    QStackedWidget *m_issueListStack = nullptr;
    QTableWidget *m_issueMilestonesTable = nullptr;
    QTableWidget *m_issueLabelsTable = nullptr;
    QWidget *m_issueBoard = nullptr;
    QHBoxLayout *m_issueBoardColumns = nullptr;
    QWidget *m_issueDetail = nullptr;
    QStackedWidget *m_issueDetailStack = nullptr;
    QWidget *m_issueComposePage = nullptr;
    QPushButton *m_issueDetailToggle = nullptr;



    QPlainTextEdit *m_issueQuickAdd = nullptr;
    QLabel *m_quickAddCharCount = nullptr;



    QComboBox *m_quickAddAgentProvider = nullptr;


    QComboBox *m_quickAddClaudeModel = nullptr;



    QComboBox *m_quickAddModeSelector = nullptr;




    QComboBox *m_quickAddSpeedSelector = nullptr;
    QCheckBox *m_quickAddCreatePr = nullptr;



    QPushButton *m_quickAddSendToAgentButton = nullptr;
    // The "task" button, stacked above "add" and "new", files the typed prompt
    // as an unassigned item in the organization's General task list (adhoc
    // #151). The member name is retained to avoid churning the existing QSS.
    QPushButton *m_quickAddGenieButton = nullptr;




    QPushButton *m_quickAddSendButton = nullptr;



    QPushButton *m_quickAddImageButton = nullptr;
    QStringList m_quickAddImages;
    QWidget *m_quickAddAttachStrip = nullptr;




    QPushButton *m_quickAddSlashButton = nullptr;
    QFrame *m_slashActionsPopup = nullptr;
    QLineEdit *m_slashActionsFilter = nullptr;
    QScrollArea *m_slashActionsScroll = nullptr;
    QWidget *m_slashActionsListHost = nullptr;
    QVBoxLayout *m_slashActionsListLayout = nullptr;
    QList<QWidget *> m_slashActionRows;
    int m_slashActionSelected = -1;
    struct ClaudeSlashCommand {
        QString name;
        QString description;
        QString argumentHint;
    };
    QList<ClaudeSlashCommand> m_claudeSlashCommands;
    bool m_claudeSlashCommandsLoaded = false;
    QProcess *m_claudeSlashProbe = nullptr;
    QByteArray m_claudeSlashProbeBuf;

    QProcess *m_claudeEffortProbe = nullptr;
    QByteArray m_claudeEffortProbeBuf;






    QPushButton *m_quickAddMicButton = nullptr;



    QCheckBox *m_quickAddVoiceAutoSubmit = nullptr;










    QPlainTextEdit *m_voiceTargetEdit = nullptr;
    QPushButton *m_voiceActiveButton = nullptr;
    QString m_voiceIdlePlaceholder;
    QList<QPushButton *> m_voiceButtons;
    QProcess *m_voiceRecordProc = nullptr;
    QProcess *m_voiceTranscribeProc = nullptr;
    QTimer *m_voiceLiveTimer = nullptr;
    QString m_voiceWavPath;
    bool m_voiceRecording = false;
    int m_voiceInsertPos = -1;
    int m_voiceInsertLen = 0;





    qint64 m_voiceLastTranscribeSize = 0;
    QString m_voiceLastPreview;




    WorldSpeechBridge *m_worldSpeechBridge = nullptr;
    OfficeChannelMirror *m_officeChannelMirror = nullptr;
    QLineEdit *m_worldSpeechOriginEdit = nullptr;
    QLineEdit *m_worldSpeechPairCodeEdit = nullptr;
    QLabel *m_worldSpeechStatusLabel = nullptr;
    QPushButton *m_worldSpeechPairButton = nullptr;
    QPushButton *m_worldSpeechRevokeButton = nullptr;
    QPlainTextEdit *m_worldSpeechDraftEdit = nullptr;
    QPushButton *m_worldSpeechHiddenMicButton = nullptr;
    QString m_worldSpeechCaptureId;
    bool m_worldSpeechCancelPending = false;



    QProgressBar *m_voiceLevelMeter = nullptr;
    QTimer *m_voiceLevelTimer = nullptr;
    qint64 m_voiceLevelPos = 0;




    QWidget *m_voiceTranscribeSpinner = nullptr;


    QProcess *m_whisperInstallProc = nullptr;
    QLabel *m_whisperStatusLabel = nullptr;
    QPushButton *m_whisperInstallButton = nullptr;
    QComboBox *m_whisperModelCombo = nullptr;



    QComboBox *m_voiceEngineCombo = nullptr;
    QComboBox *m_parakeetModelCombo = nullptr;
    QProcess *m_parakeetInstallProc = nullptr;
    QComboBox *m_voiceDeviceCombo = nullptr;


    QTabWidget *m_settingsTabs = nullptr;
    int m_voiceSettingsTabIndex = -1;


    QWidget *m_settingsProfileHost = nullptr;
    int m_profileSettingsTabIndex = -1;



    QPushButton *m_voiceTestMicButton = nullptr;
    QProgressBar *m_voiceTestMeter = nullptr;
    QProcess *m_voiceTestProc = nullptr;
    QTimer *m_voiceTestTimer = nullptr;
    QString m_voiceTestWavPath;
    qint64 m_voiceTestPos = 0;
    bool m_voiceTestRecording = false;




    QStringList m_quickAddHistory;
    int m_quickAddHistoryIndex = -1;
    QString m_quickAddDraft;



    bool m_quickAddHistoryNavigating = false;


    QLabel *m_footerGitIdentity = nullptr;



    QLabel *m_footerCommitInfo = nullptr;


    QLabel *m_statusAppPath = nullptr;

    QPushButton *m_footerDiagnostics = nullptr;




    QWidget *m_cpuChart = nullptr;
    QWidget *m_repoSizeChart = nullptr;
    QWidget *m_repoLinesChart = nullptr;
    QWidget *m_repoFilesChart = nullptr;
    QToolButton *m_repoRatchetButton = nullptr;
    qint64 m_repoStatsLastRefreshMs = 0;
    QWidget *m_memChart = nullptr;
    QWidget *m_diskChart = nullptr;
    StallWatchdog *m_stallWatchdog = nullptr;
    QTimer *m_diagTimer = nullptr;
    int m_stallCount = 0;
    QStringList m_stallLog;
    QString m_stallLogPath;


    QSet<QString> m_autoFiledStallSignatures;
    bool m_highMemoryAlertArmed = true;
    QPointer<QDialog> m_highMemoryDialog;
    QTableWidget *m_highMemoryProcessTable = nullptr;
    QLabel *m_highMemoryProcessStatus = nullptr;
    QPointer<QProcess> m_highMemoryProcessQuery;



    QHash<qint64, QVector<double>> m_highMemoryRssHistory;
    qulonglong m_diagLastCpuTicks = 0;
    qint64 m_diagLastCpuMs = 0;


    int m_repoDetailIndex = -1;
    QButtonGroup *m_issueTabGroup = nullptr;
    QButtonGroup *m_repoDetailTabs = nullptr;


    QWidget *m_repoDetailChrome = nullptr;
    QWidget *m_repoFilesModeBar = nullptr;
    QWidget *m_repoOverviewChrome = nullptr;




    ActivityRailButton *m_railCodeButton = nullptr;
    ActivityRailButton *m_railGitButton = nullptr;
    QPushButton *m_repoCodeTab = nullptr;
    QString m_repoCodeSizePath;
    QPushButton *m_repoIssuesTab = nullptr;
    QPushButton *m_repoPullsTab = nullptr;
    QPushButton *m_repoDiscussionsTab = nullptr;
    QPushButton *m_repoActionsTab = nullptr;
    QPushButton *m_repoMirrorsTab = nullptr;
    QPushButton *m_repoReleasesTab = nullptr;
    QPushButton *m_repoBranchesTab = nullptr;
    QStackedWidget *m_repoDetailStack = nullptr;
    int m_chatStackIndex = -1;
    int m_insightsTabIndex = -1;
    int m_branchesTabIndex = -1;
    int m_worktreesTabIndex = -1;
    QPushButton *m_repoWorktreesTab = nullptr;
    QTableWidget *m_worktreesTable = nullptr;
    QLabel *m_worktreesSummary = nullptr;
    QLabel *m_worktreeBranchLabel = nullptr; // shows which branch the open detail is on
    QPushButton *m_worktreeMergeButton = nullptr;  // merge the selected worktree into main
    QPushButton *m_worktreeMergeDeleteAgentButton = nullptr; // merge, then delete its agent too
    QPushButton *m_worktreeUpdateButton = nullptr; // merge main into the selected worktree
    QPushButton *m_worktreeResolveButton = nullptr; // resolve a conflicted merge in the worktree
    QPushButton *m_worktreeCommitButton = nullptr; // commit the worktree's uncommitted changes
    QPushButton *m_worktreeRemoveButton = nullptr; // remove the selected worktree
    QString m_worktreeSelectedBranch;              // branch behind the open worktree detail
    QString m_worktreeSelectedPath;                // its on-disk worktree folder
    int m_releasesTabIndex = -1; // index of the Releases page
    int m_mirrorNodesTabIndex = -1; // index of the Mirror nodes page
    int m_artifactsTabIndex = -1; // index of the Artifacts page
    int m_shortcutsTabIndex = -1; // index of the Shortcuts page
    int m_settingsTabIndex = -1; // index of the Settings page
    int m_projectsTabIndex = -1; // index of the Projects page (issue #384)
    int m_sizeMapTabIndex = -1; // index of the Size map page (adhoc #189)
    // Size map tab state: the chart is a RepoSunburstChart (MainWindowInternal.h),
    // held as QWidget* like the other inline-widget members. m_sizeMapScannedPath
    // remembers which working copy the chart currently shows so re-opening the
    // tab on the same repo skips the rescan; the epoch discards a scan that
    // lands after the user switched repos.
    QWidget *m_sizeMapChart = nullptr;
    QLabel *m_sizeMapStatus = nullptr;

    QCheckBox *m_sizeMapHideIgnored = nullptr;



    QString m_sizeMapRootOverride;
    QLabel *m_sizeMapRootLabel = nullptr;
    QPushButton *m_sizeMapResetRoot = nullptr;
    QString m_sizeMapScannedPath;
    bool m_sizeMapScanning = false;
    int m_sizeMapScanEpoch = 0;



    QPushButton *m_sizeMapElevate = nullptr;

    QPushButton *m_sizeMapStop = nullptr;


    QFutureWatcher<forkmesh::DirectorySizeScanResult> *m_sizeMapWatcher = nullptr;


    QProcess *m_sizeMapElevatedProcess = nullptr;


    QWidget *m_sizeMapVolumesBox = nullptr;
    QPushButton *m_repoProjectsTab = nullptr;
    QLabel *m_repoVisibilityHint = nullptr;
    QTableWidget *m_branchesTable = nullptr;
    QLabel *m_branchesSummary = nullptr;
    QPushButton *m_branchPullAllButton = nullptr;


    QCheckBox *m_branchAutoPullAllCheck = nullptr;
    QPushButton *m_branchDeleteMergedButton = nullptr; // "Delete merged" header action
    QTextBrowser *m_branchDiffView = nullptr;
    QString m_branchDiffBranch;





    int m_branchDiffPullNumber = -1;
    // Branch that auto-pull has already been attempted for (see showBranchDiff),
    // so a failed update doesn't retry on every incidental rebuild while the same
    // branch stays selected. Cleared implicitly by simply differing once another
    // branch is selected.
    QString m_branchAutoPullAttempted;
    // Bumped each time a branch is selected / a range diff is requested so the
    // off-thread git read that renders the diff can drop its result if the user
    // has since switched branch (issue #353 — showBranchDiff/
    // renderBranchScopeDiff shelled git on the GUI thread).
    int m_branchScopeDiffGen = 0;







    QHash<QString, bool> m_branchConflictCache;
    QSet<QString> m_branchConflictProbes; // branches a worker is probing right now
    QHash<QString, BranchChangeStat> m_branchChangeStatsCache;
    int m_branchChangeStatsGen = 0;
    bool m_branchChangeStatsLoading = false;
    // Probe the given branches (pairs of branch name + cache key) for conflicts
    // with `base` off the GUI thread, painting each verdict into the table when
    // it lands.
    void startBranchConflictProbes(const QString &dir, const QString &base,
                                   const QList<QPair<QString, QString>> &probes);





    QByteArray m_branchDiffLastPatch;
    QString m_branchDiffLastEmpty;
    bool m_branchDiffLastValid = false;



    QString m_branchDiffViewedContext;
    // Checkout whose complete snapshot is compared with the base. For a linked
    // worktree this lets the one Git view include committed, staged, unstaged,
    // and untracked changes rather than only the branch tip.
    QString m_branchDiffWorkDir;
    // Sticky header pinned over the branch/PR diff (same form as the PR viewer's:
    // filename, Pac-Man read-progress chart, percent label and a Viewed toggle).
    QFrame *m_branchDiffSticky = nullptr;
    QLabel *m_branchStickyPath = nullptr;
    PacmanProgress *m_branchStickyPacman = nullptr;
    QLabel *m_branchStickyPercent = nullptr;
    QPushButton *m_branchStickyViewed = nullptr;
    QString m_branchStickyFile;
    QList<QPair<int, QString>> m_branchDiffFileSpans;
    // Ordered file paths of the diff currently in the branch view, so the
    // sticky-bar span map can be rebuilt once the whole diff has landed (the
    // render is progressive — see renderDiffStreamed, adhoc #51/#421), and each
    // file's document anchor so the auto-mark-viewed re-render can land back on
    // the file still being read.
    QStringList m_branchDiffFilePaths;
    QStringList m_branchDiffFileAnchors;
    // Last file a CHANGES-row action navigated to in either the branch-range or
    // working-tree diff. Also gives the window tests a stable assertion that
    // does not depend on viewport height or font metrics.
    QString m_lastSourceControlDiffPath;
    // Absolute document y of each file header (aligned to m_branchDiffFileSpans),
    // cached so the per-scroll-tick sticky/progress update doesn't re-measure the
    // document; cleared on every re-render / stream-finish (mirrors the PR pane).
    QList<int> m_branchFileTops;
    // Debounces the auto-mark-viewed sweep off the branch diff's scrollbar, same
    // rhythm as the PR viewer's m_pullAutoViewedDebounce (adhoc #107).
    QTimer *m_branchAutoViewedDebounce = nullptr;

    QWidget *m_branchDiffSearchBar = nullptr;
    QLineEdit *m_branchDiffSearchInput = nullptr;
    QLabel *m_branchDiffSearchCount = nullptr;
    QList<QTextCursor> m_branchDiffSearchMatches;
    int m_branchDiffSearchIndex = -1;

    QPushButton *m_branchSplitButton = nullptr;

    QPushButton *m_branchOpenPullButton = nullptr;
    QPushButton *m_branchesDeleteSelBtn = nullptr;



    QLabel *m_branchDetailLabel = nullptr;


    QPushButton *m_branchCloseButton = nullptr;
    QPushButton *m_branchOpenCodiumButton = nullptr;
    QPushButton *m_branchMergeEditorButton = nullptr;
    QPushButton *m_branchPullButton = nullptr;
    QPushButton *m_branchFixButton = nullptr;



    QComboBox *m_branchFixAgentCombo = nullptr;
    QComboBox *m_branchFixModelCombo = nullptr;
    QPushButton *m_branchPrButton = nullptr;
    QPushButton *m_branchMergeButton = nullptr;


    QPushButton *m_branchMergeDeleteButton = nullptr;
    QTableWidget *m_releasesTable = nullptr;
    QLabel *m_releasesSummary = nullptr;
    QTableWidget *m_artifactsTable = nullptr;
    QLabel *m_artifactsSummary = nullptr;
    QWidget *m_shortcutCardsHost = nullptr;
    QLabel *m_shortcutsSummary = nullptr;
    QPlainTextEdit *m_shortcutOutput = nullptr;
    QLabel *m_shortcutRunStatus = nullptr;
    QPushButton *m_shortcutStopButton = nullptr;
    QProcess *m_shortcutProcess = nullptr;
    QTableWidget *m_mirrorNodesTable = nullptr;
    QLabel *m_mirrorNodesSummary = nullptr;
    QCheckBox *m_mirrorNodesOnlineOnlyCheck = nullptr;


    QTimer *m_nodeLightTimer = nullptr;
    int m_nodeLightFrame = 0;



    QTimer *m_requestServedFlushTimer = nullptr;

    QTimer *m_mirrorPanelRosterTimer = nullptr;



    QHash<QString, CommitIdentity> m_commitIdentityCache;


    QPushButton *m_mirrorResetPinButton = nullptr;

    QString m_repoBranch;
    RepoInfo m_repoInfo;
    QLabel *m_repoHeaderTitle = nullptr;
    QLabel *m_repoDetailNotice = nullptr;
    QLabel *m_repoDetailStatus = nullptr;
    QPushButton *m_notifyButton = nullptr;
    QPushButton *m_forkButton = nullptr;
    QPushButton *m_mirrorButton = nullptr;
    QPushButton *m_sourceButton = nullptr;
    QPushButton *m_repoOpenButton = nullptr;
    QMenu *m_mirrorMenu = nullptr;
    QMenu *m_sourceMenu = nullptr;
    QPushButton *m_branchButton = nullptr;
    QPushButton *m_branchesButton = nullptr;
    QPushButton *m_worktreesButton = nullptr;
    QPushButton *m_remotesButton = nullptr;
    QPushButton *m_tagsButton = nullptr;
    // Persistent segmented toggle, always visible above the Code page, that
    // switches between the GitHub-style overview and the explorer/editor view.
    QPushButton *m_filesModeOverviewButton = nullptr; // -> code overview
    QPushButton *m_filesModeExplorerButton = nullptr; // -> explorer/editor
    QPushButton *m_filesModeCoveExplorerButton = nullptr; // -> account cove explorer
    QLineEdit *m_fileSearch = nullptr;
    QCompleter *m_fileCompleter = nullptr;
    QLabel *m_securitySummary = nullptr;
    QWidget *m_securitySignalsPanel = nullptr;
    QGridLayout *m_securitySignalsGrid = nullptr;
    QTableWidget *m_securityFindingsTable = nullptr;
    QPushButton *m_securityRefreshButton = nullptr;


    bool m_repoSecurityScanRunning = false;


    QString m_repoSecurityScanningPath;



    RepoSecuritySnapshot m_lastRepoSecuritySnapshot;
    QLabel *m_qualitySummary = nullptr;
    QWidget *m_qualitySignalsPanel = nullptr;
    QGridLayout *m_qualitySignalsGrid = nullptr;
    QTableWidget *m_qualityFindingsTable = nullptr;
    QPushButton *m_qualityRefreshButton = nullptr;

    QLineEdit *m_vulnTitleEdit = nullptr;
    QPlainTextEdit *m_vulnBodyEdit = nullptr;
    QLineEdit *m_vulnContactEdit = nullptr;
    QComboBox *m_vulnComponentCombo = nullptr;
    QPushButton *m_vulnSubmitButton = nullptr;
    QLabel *m_vulnStatusLabel = nullptr;

    QLabel *m_mcpStatusLabel = nullptr;
    QLineEdit *m_mcpTokenEdit = nullptr;
    QPlainTextEdit *m_mcpConfigEdit = nullptr;
    QPushButton *m_mcpGenerateButton = nullptr;
    QPushButton *m_mcpRevokeButton = nullptr;
    QPushButton *m_mcpTestButton = nullptr;
    QLabel *m_mcpTestLabel = nullptr;


    QProcess *m_mcpTestProcess = nullptr;


    QLineEdit *m_genieTokenEdit = nullptr;
    QLineEdit *m_genieOrgEdit = nullptr;
    QComboBox *m_genieWorkflowCombo = nullptr;
    QLabel *m_genieStatusLabel = nullptr;
    QTableWidget *m_commitsTable = nullptr;



    QString m_commitsLoadedRef;
    QString m_commitsLoadedTip;




    QString m_commitsLoadedMirrorTip;



    int m_commitsLoadGen = 0;



    mutable QStringList m_branchesCache;
    mutable QString m_branchesCacheDir;
    mutable qint64 m_branchesCacheTime = 0;




    mutable QString m_defaultBranchFastCache;
    mutable QString m_defaultBranchFastCacheDir;
    mutable qint64 m_defaultBranchFastCacheTime = 0;



    mutable QString m_defaultBranchFastCacheSig;
    QLineEdit *m_commitSearch = nullptr;



    QLineEdit *m_globalSearch = nullptr;
    QListWidget *m_globalSearchPopup = nullptr;
    QTimer *m_globalSearchTimer = nullptr;     // debounce keystrokes before rebuilding
    // Back / forward navigation trail (left of the search box). Each entry is a
    // place we landed on: section, repository, repo tab, and—inside Git—the
    // browsed branch. This lets Back / Forward cross Code ↔ Git and branch ↔
    // branch rather than treating them as the same Code-tab location.
    struct NavPlace {
        int section = 0;
        int repoIndex = -1;
        int detailTab = -1;
        int overviewPage = -1; // files=0, Git=1, branches=2, worktrees=3
        QString branch;
        bool operator==(const NavPlace &o) const
        {
            return section == o.section && repoIndex == o.repoIndex &&
                   detailTab == o.detailTab && overviewPage == o.overviewPage &&
                   branch == o.branch;
        }
    };
    QPushButton *m_navBackButton = nullptr;
    QPushButton *m_navForwardButton = nullptr;
    QList<NavPlace> m_navHistory;
    int m_navHistoryIndex = -1;
    bool m_navRestoring = false;
    bool m_navRecordPending = false;


    QTreeWidget *m_searchResultsTree = nullptr;
    QLabel *m_searchResultsTitle = nullptr;
    QLabel *m_searchResultsStatus = nullptr;
    QPushButton *m_searchResultsStop = nullptr;
    QList<QProcess *> m_searchProcs;
    int m_searchPending = 0;
    QString m_searchPageQuery;
    QLabel *m_commitsUnsyncedBanner = nullptr;



    QTreeWidget *m_commitsUnsyncedFiles = nullptr;
    bool m_commitsUnsyncedExpanded = false;
    QStringList m_commitsUnsyncedHashes;


    QString m_pendingCommitFileScroll;


    QWidget *m_commitsListPage = nullptr;
    QGraphicsOpacityEffect *m_commitsBannerOpacity = nullptr;
    QPropertyAnimation *m_commitsBannerFade = nullptr;
    QLabel *m_insightsSummary = nullptr;
    QLabel *m_insightsTraffic = nullptr;
    QLabel *m_insightsLanguageBar = nullptr;
    QLabel *m_insightsLanguageLegend = nullptr;
    QLabel *m_insightsActivity = nullptr;


    QTableWidget *m_insightsContributors = nullptr;
    QComboBox *m_insightsRangeCombo = nullptr;
    QLabel *m_insightsActivityAxis = nullptr;
    QPushButton *m_insightsRefreshButton = nullptr;

    static constexpr int kCommitWorkspaceChangesPage = 0;
    static constexpr int kCommitWorkspaceCommitPage = 1;
    // Branch/PR range review page: its diff fills the right pane while the
    // universal source-control panel and commit graph stay in the left column.
    static constexpr int kCommitWorkspaceRangePage = 2;
    // The Git view's left column: source control above commit history.
    QStackedWidget *m_gitFilesSlot = nullptr;
    QStackedWidget *m_gitHistorySlot = nullptr;
    // Show a right-pane page and keep its compare indicator in step.
    void setCommitWorkspacePage(int page);
    // The "<branch> -> <base>" compare indicator on the graph's branch row:
    // while a branch/PR comparison is open on the right pane, an arrow and a
    // base button appear after the branch button, and the base button's
    // dropdown picks which branch the range is diffed against (adhoc #16).
    QLabel *m_commitsCompareArrow = nullptr;
    QPushButton *m_commitsCompareBaseButton = nullptr;
    void updateCommitsCompareIndicator();
    QWidget *m_scmPanel = nullptr;
    QWidget *m_scmControlsPanel = nullptr;
    QTreeWidget *m_scmTree = nullptr;
    QPlainTextEdit *m_scmMessage = nullptr;
    QTextBrowser *m_scmDiff = nullptr;
    QLabel *m_scmCountLabel = nullptr;
    QLabel *m_scmViewedLabel = nullptr;
    QPushButton *m_scmAutoViewedButton = nullptr;
    QPushButton *m_scmGenerateButton = nullptr;
    QComboBox *m_scmGenModel = nullptr;
    QComboBox *m_scmGenKind = nullptr;
    QComboBox *m_scmGenDuration = nullptr;
    QPushButton *m_scmCopyButton = nullptr;
    QLabel *m_scmGenStatus = nullptr;
    bool m_scmGenerating = false;
    int m_scmHeuristicVariant = 0;
    QPushButton *m_scmCommitButton = nullptr;
    QPushButton *m_scmCommitPushButton = nullptr; // commit, then publish/push
    QPushButton *m_scmStageCommitPushButton = nullptr; // stage all, commit, push
    QWidget *m_scmOutgoingPanel = nullptr;
    QLabel *m_scmOutgoingLabel = nullptr; // branch + pending commit count
    QPushButton *m_scmSyncButton = nullptr; // publish/push pending commits
    int m_scmOutgoingGeneration = 0; // rejects late ahead-count callbacks
    // Stage all / Unstage all / Discard all have no buttons of their own in the
    // panel any more — the CHANGES group headers carry those three actions.
    QPushButton *m_scmRefreshButton = nullptr;
    QPushButton *m_scmPrevButton = nullptr;
    QPushButton *m_scmNextButton = nullptr;
    QLabel *m_scmEmptyNote = nullptr;


    QByteArray m_scmStatusCache;





    QString m_scmCombinedPatch;
    int m_scmCombinedStagedFiles = 0;
    bool m_scmPatchValid = false;
    QStringList m_scmSectionKeys;
    QStringList m_scmSectionAnchors;
    QStringList m_scmSectionPaths;
    QList<int> m_scmFileTops;
    QHash<QString, QString> m_scmStickyLabelHtml;
    QString m_scmDiffRenderKey;
    QFrame *m_scmStickyHeader = nullptr;
    QLabel *m_scmStickyPath = nullptr;
    PacmanProgress *m_scmStickyPacman = nullptr;
    QLabel *m_scmStickyPercent = nullptr;
    QPushButton *m_scmStickyViewed = nullptr;
    QString m_scmStickySection;
    QTimer *m_scmAutoViewedDebounce = nullptr;


    bool m_scmSuppressFileScroll = false;

    QStackedWidget *m_commitsStack = nullptr;
    QLabel *m_commitTitle = nullptr;
    QLabel *m_commitMeta = nullptr;
    QLabel *m_commitMessage = nullptr;
    QLabel *m_commitFilesSummary = nullptr;
    QListWidget *m_commitFileList = nullptr;
    QTextBrowser *m_commitDiffView = nullptr;
    QWidget *m_commitDiffSpinner = nullptr;


    int m_commitLoadGen = 0;
    QPushButton *m_commitPrevButton = nullptr;
    QPushButton *m_commitNextButton = nullptr;
    QPushButton *m_commitDownloadButton = nullptr;
    QPushButton *m_commitDeleteButton = nullptr;
    QPushButton *m_commitRevertButton = nullptr;
    QPushButton *m_commitSplitButton = nullptr;
    QString m_currentCommitHash;
    int m_currentCommitRow = -1;


    QStackedWidget *m_filesStack = nullptr;
    QString m_overviewPath;


    QString m_overviewLoadedKey;



    bool m_overviewLoading = false;
    int m_treeLoadedForIndex = -1;
    QLabel *m_commitBar = nullptr;
    // Area under the latest-commit bar: 0 = crumb + file list + README,
    // 1 = the Git workspace, reachable only from the activity rail.
    QStackedWidget *m_overviewBodyStack = nullptr;
    QLabel *m_overviewCrumb = nullptr;
    QString m_commitBarStatusHash;
    QString m_commitBarBodyHtml;


    QTreeWidget *m_overviewList = nullptr;
    struct OverviewRow {
        QString name;
        QString path;
        bool isDir = false;
        qint64 size = 0;
        qint64 loc = 0;
        qint64 fileCount = 0;
        qint64 commitTs = 0;
        QString subject;
        QString whenText;
    };
    QList<OverviewRow> m_overviewRows;
    qint64 m_overviewRepoBytes = 0;

    QString m_overviewSortKey = QStringLiteral("name");
    bool m_overviewSortDesc = false;
    void populateOverviewTree();
    QTextBrowser *m_readmeView = nullptr;
    QTreeWidget *m_repoFileTree = nullptr;
    QTabWidget *m_repoFileTabs = nullptr;
    QPushButton *m_repoFileCommitButton = nullptr;
    QPushButton *m_repoFilePullButton = nullptr;
    QPushButton *m_repoFileHistoryButton = nullptr;
    QPushButton *m_repoFilePreviewButton = nullptr;
    QHash<QString, QWidget *> m_openFileTabs;
    QComboBox *m_coveExplorerSelector = nullptr;
    QLabel *m_coveExplorerStatus = nullptr;
    QTreeWidget *m_coveExplorerTree = nullptr;
    QTabWidget *m_coveExplorerTabs = nullptr;
    QPushButton *m_coveExplorerNewButton = nullptr;
    QPushButton *m_coveExplorerInviteButton = nullptr;
    QPushButton *m_coveExplorerNewFileButton = nullptr;
    QPushButton *m_coveExplorerDeleteButton = nullptr;
    QPushButton *m_coveExplorerSaveButton = nullptr;
    QList<Cove> m_coveExplorerCoves;
    QHash<QString, QWidget *> m_openCoveExplorerTabs;
    QString m_coveExplorerCurrentId;


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

    int m_discussionCountLoadGen = 0;
    int m_currentDiscussionNumber = -1;
    DiscussionInboxBackoff m_discussionInboxBackoff;




    NetworkBackoff m_pollBackoff;



    bool m_pendingCreditsRefilled5h = false;
    bool m_pendingCreditsRefilledWeekly = false;
    // One armed timer per provider/window; calendar reminders persist outside
    // the process, while these timers make the desktop ping prompt when the
    // app remains open.
    QHash<QString, QTimer *> m_usageLimitReminderTimers;


    QWidget *m_coveSection = nullptr;
    QListWidget *m_coveList = nullptr;
    QLineEdit *m_covePasswordEdit = nullptr;
    QPushButton *m_covePwRevealBtn = nullptr;
    QCheckBox *m_coveAutoOpenCheck = nullptr;
    QLabel *m_coveEmptyHint = nullptr;
    QLineEdit *m_coveGlobalPasswordEdit = nullptr;
    QCheckBox *m_coveGlobalAutoOpenCheck = nullptr;


    QHash<QString, QString> m_coveSessionPasswords;


    QSet<QString> m_coveFailedUnlocks;


    QTableWidget *m_pullTable = nullptr;
    QLineEdit *m_pullSearch = nullptr;
    QPushButton *m_pullNewButton = nullptr;
    QPushButton *m_pullChooseDirButton = nullptr;
    QPushButton *m_pullImportButton = nullptr;
    QPushButton *m_pullSyncButton = nullptr;
    QPushButton *m_pullDeleteAllMergedButton = nullptr;
    QWidget *m_pullDetail = nullptr;
    QPushButton *m_pullHideDetailButton = nullptr;
    bool m_pullDetailHidden = false;
    QLabel *m_pullTitle = nullptr;
    QLabel *m_pullMeta = nullptr;
    QLabel *m_pullMergeStatus = nullptr;
    QLabel *m_pullReviewSummary = nullptr;
    QPushButton *m_pullUpdateButton = nullptr;
    QPushButton *m_pullMergeButton = nullptr;
    QPushButton *m_pullResolveButton = nullptr; // opens the conflict merge editor
    // "Fix": on a conflicted PR, drops a ready-made conflict-resolution task
    // into the footer prompt box (adhoc #7 — the provider dropdown it used to
    // carry is gone; the prompt bar picks the agent).
    QPushButton *m_pullFixButton = nullptr;
    // Shown alongside "Fix" only when an agent session authored this
    // PR's branch: continues that same session rather than spinning up a fresh,
    // isolated conflict-only run.
    QPushButton *m_pullFixConflictsButton = nullptr;




    QPushButton *m_pullReviewAiButton = nullptr;
    QPushButton *m_pullFixAllAiButton = nullptr;
    QPushButton *m_pullEditFileButton = nullptr;
    QPushButton *m_pullDeleteFileButton = nullptr;
    QPushButton *m_pullCloseButton = nullptr;
    QPushButton *m_pullReopenButton = nullptr;


    QPushButton *m_pullSendToSourceButton = nullptr;
    QPushButton *m_pullDeleteButton = nullptr;
    QPushButton *m_pullDeleteBranchButton = nullptr;
    QPushButton *m_pullMergeDeleteButton = nullptr;
    QPushButton *m_pullPreviewButton = nullptr;
    QDialog *m_pullPreviewDialog = nullptr;
    bool m_pullDeleteConfirmPending = false;
    bool m_pullDeleteInProgress = false;
    QListWidget *m_pullFiles = nullptr;



    QComboBox *m_pullFileAuthorFilter = nullptr;

    QHash<QString, bool> m_pullFileAuthorship;
    QPushButton *m_pullPrevButton = nullptr;
    QPushButton *m_pullNextButton = nullptr;
    QTextBrowser *m_pullDiff = nullptr;



    QWidget *m_pullDiffSearchBar = nullptr;
    QLineEdit *m_pullDiffSearchInput = nullptr;
    QLabel *m_pullDiffSearchCount = nullptr;



    QList<QTextCursor> m_pullDiffSearchMatches;
    int m_pullDiffSearchIndex = -1;




    QString m_pullDiffRenderKey;


    QHash<QString, QString> m_pullFileAnchors;


    QStringList m_pullFileOrder;


    bool m_pullSuppressFileScroll = false;


    QPushButton *m_pullAutoViewedButton = nullptr;
    QTimer *m_pullAutoViewedDebounce = nullptr;




    QFrame *m_pullStickyHeader = nullptr;
    QLabel *m_pullStickyPath = nullptr;
    PacmanProgress *m_pullStickyPacman = nullptr;
    QLabel *m_pullStickyPercent = nullptr;
    QPushButton *m_pullStickyViewed = nullptr;
    QString m_pullStickyFile;

    QHash<QString, QString> m_pullStickyLabelHtml;





    QList<int> m_pullFileTops;
    int m_diffFontPt = 12;


    QList<QTextEdit *> m_diffViews;



    QHash<QTextEdit *, int> m_diffRestoreScroll;
    QPushButton *m_pullSplitButton = nullptr;
    QListWidget *m_pullCommitsList = nullptr;


    QButtonGroup *m_pullSubTabs = nullptr;
    QStackedWidget *m_pullSubStack = nullptr;
    QPushButton *m_pullTabConversation = nullptr;
    QPushButton *m_pullTabCommits = nullptr;
    QPushButton *m_pullTabChecks = nullptr;
    QPushButton *m_pullTabFiles = nullptr;
    QPushButton *m_pullTabBadge = nullptr;


    PullBadgeWidget *m_pullBadgeWidget = nullptr;

    QScrollArea *m_pullThreadScroll = nullptr;
    QWidget *m_pullThreadContainer = nullptr;
    QVBoxLayout *m_pullThreadLayout = nullptr;
    QLabel *m_pullChecksSummary = nullptr;
    QLabel *m_pullConflictDetails = nullptr;
    MarkdownEditor *m_pullComposer = nullptr;
    QPushButton *m_pullCommentButton = nullptr;
    QPushButton *m_pullApproveButton = nullptr;
    QPushButton *m_pullRequestChangesButton = nullptr;

    QWidget *m_pullAgentRevisionRow = nullptr;
    QLineEdit *m_pullAgentRevisionEdit = nullptr;
    QPushButton *m_pullSendToAgentButton = nullptr;
    QPushButton *m_pullLinkIssueButton = nullptr;
    QLabel *m_pullLinksValue = nullptr;

    QTableWidget *m_pullChecksTable = nullptr;
    QPlainTextEdit *m_pullChecksLog = nullptr;
    QPushButton *m_pullRunChecksButton = nullptr;
    QList<PullRequest> m_currentPulls;
    QHash<QString, QString> m_pullFileDiffs;



    QHash<int, bool> m_pullConflictByNumber;





    QString m_pullConflictCacheBaseTip;




    struct PullConflictEntry {
        QString fingerprint;
        bool conflict = false;
        QStringList conflictFiles;
    };
    QHash<int, PullConflictEntry> m_pullConflictCache;


    quint64 m_pullConflictGen = 0;
    quint64 m_pullLoadGen = 0;
    bool m_pullBackgroundLoadInFlight = false;
    bool m_pullBackgroundReloadQueued = false;



    QList<QPair<int, QString>> m_pendingPullConflictChecks;




    bool m_pullConflictCheckInFlight = false;


    static QString pullPatchFingerprint(const QString &patch);
    int m_currentPullNumber = -1;


    struct AppNotification {
        QString title;
        QString body;
        qint64 timestampMs = 0;
        bool warning = false;
        int runId = -1;
        NotificationLink link;



        QString kind;
        QString actor;
        QString repo;
        qint64 id = 0;
    };
    ActionStore *m_actionStore = nullptr;




    QList<ActionRunner *> m_actionRunners;
    QFileSystemWatcher *m_actionSpoolWatcher = nullptr;
    QList<ActionRun> m_actionRuns;
    QList<int> m_actionQueue;
    QSet<int> m_actionWaitingRuns;



    struct ActionMirrorPin {
        std::shared_ptr<void> materialization;
        QString path;
        QString owner;
        QString name;
    };
    QHash<int, ActionMirrorPin> m_actionMirrorPins;
    QString m_mirrorActionsConfigGeneration;
    QString m_mirrorActionsRuntimeState;
    qint64 m_mirrorActionsRuntimeStateWrittenAtMs = 0;
    QTimer *m_mirrorActionsSummaryTimer = nullptr;
    qint64 m_mirrorActionsSummaryAttemptedAtMs = 0;
    qint64 m_lastExternalActionsScanMs = 0;
    QList<AppNotification> m_notifications;
    qint64 m_nextNotificationId = 1;


    QSet<QString> m_flashedWebAlertIds;
    QPushButton *m_notificationButton = nullptr;
    QTableWidget *m_notificationsTable = nullptr;

    QJsonArray m_webAlerts;
    int m_webAlertsUnread = 0;
    bool m_webAlertsLoading = false;
    qint64 m_webAlertsFetchedAtMs = 0;
    int m_selectedRunId = -1;
    QListWidget *m_actionWorkflowList = nullptr;
    QComboBox *m_actionNodeCombo = nullptr;
    QLabel *m_actionNodeLabel = nullptr;
    QString m_selectedWorkflowFilter;
    QList<ActionWorkflow> m_repoWorkflows;



    int m_workflowCountLoadGen = 0;




    QTimer *m_openRepoRefreshTimer = nullptr;
    QTimer *m_agentsSpinTimer = nullptr;



    QHash<int, double> m_agentRowSpinAngles;
    int m_agentSpinTicks = 0;
    QTableWidget *m_actionsTable = nullptr;
    QLabel *m_actionRunTitle = nullptr;
    QLabel *m_actionRunMeta = nullptr;
    QLabel *m_actionApprovalBanner = nullptr;
    QPlainTextEdit *m_actionLog = nullptr;
    QTextBrowser *m_actionDiff = nullptr;
    QWidget *m_actionApprovalBar = nullptr;
    QPushButton *m_actionApproveButton = nullptr;
    QPushButton *m_actionRejectButton = nullptr;
    QPushButton *m_actionSplitButton = nullptr;


    QWidget *m_actionManualRunBar = nullptr;
    QPushButton *m_actionManualRunButton = nullptr;
    QComboBox *m_actionRunBranchCombo = nullptr;

    QPushButton *m_actionRerunButton = nullptr;

    QPushButton *m_actionStopButton = nullptr;

    QPushButton *m_actionSkipButton = nullptr;

    QPushButton *m_actionCopyLogButton = nullptr;



    QPushButton *m_actionFixButton = nullptr;
    QComboBox *m_actionFixAgentCombo = nullptr;
    QComboBox *m_actionFixModelCombo = nullptr;

    QTableWidget *m_varsTable = nullptr;


    bool m_varsRevealed = false;
    QPushButton *m_varsRevealButton = nullptr;



    QCheckBox *m_actionsEnabledCheck = nullptr;
    QCheckBox *m_settingsActionsCheck = nullptr;
    QCheckBox *m_secretScanCheck = nullptr;


    QCheckBox *m_repoPrivateCheck = nullptr;



    QWidget *m_collabSection = nullptr;
    QListWidget *m_collabList = nullptr;
    QLineEdit *m_collabEdit = nullptr;
    QLabel *m_collabEmptyHint = nullptr;



    QLineEdit *m_repoSourceEdit = nullptr;
    QLabel *m_repoSourceHint = nullptr;
    QLabel *m_repoForkLocation = nullptr;
    QLabel *m_repoMirrorLocation = nullptr;
    QTableWidget *m_repoRemotesTable = nullptr;

    AgentStore *m_agentStore = nullptr;


    QList<AgentRunner *> m_agentRunners;
    QList<AgentSession> m_agentSessions;
    QList<int> m_agentQueue;


    bool m_agentQueuePumpScheduled = false;





    QHash<int, bool> m_agentQueueAwaitingEvents;





    bool m_agentQuietResume = false;




    QSet<int> m_startupQuietAgentSessions;
    int m_selectedAgentSessionId = -1;




    int m_agentDetailTabSession = -1;




    struct AiConflictFix {
        PullStore *store = nullptr;
        int number = 0;
        int repoIndex = -1;
        int sessionId = 0;
        QString provider;
        QString model;
        QString apiKey;
        QString workTree;
        QStringList files;
        int index = 0;
        double costUsd = 0.0;
        qint64 inTokens = 0;
        qint64 outTokens = 0;



        bool claudeCode = false;
        QProcess *process = nullptr;



        bool branchMerge = false;
        QString branch;
        QString baseBranch;
        QString restoreBranch;





        bool agentEdit = false;
        QString agentEditPrompt;
        int agentEditFindings = 0;
        QString promptFile;
    };
    AiConflictFix *m_aiFix = nullptr;
    void aiFixResolveNextFile();
    void aiFixRunClaudeCode();
    void aiFixApplyResolved(const QString &resolved);
    void aiFixFinish();
    void aiFixFail(const QString &message);
    void aiFixLog(const QString &text);
    void aiFixSetSessionStatus(const QString &status, const QString &error = QString());




    struct AiPullReview {
        int number = 0;
        int repoIndex = -1;
        int sessionId = 0;
        QString provider;
        QString model;
        double costUsd = 0.0;
        qint64 inTokens = 0;
        qint64 outTokens = 0;
        QProcess *process = nullptr;
        QString output;
        QString promptFile;
    };
    AiPullReview *m_aiReview = nullptr;
    void reviewCurrentPullWithAi();
    void aiReviewRunClaudeCode(const QString &prompt);
    void aiReviewHandleReply(const QString &text);
    void aiReviewFail(const QString &message);
    void aiReviewLog(const QString &text);
    void aiReviewSetSessionStatus(const QString &status,
                                  const QString &error = QString());

    void applyPullSuggestionFix(const QString &threadId);



    void fixCurrentPullFindingsWithAgent();
    QTableWidget *m_agentTable = nullptr;


    QLineEdit *m_agentSearch = nullptr;



    QComboBox *m_agentComposeRepo = nullptr;
    QLineEdit *m_agentComposePrompt = nullptr;
    QComboBox *m_agentComposeProvider = nullptr;
    QPushButton *m_agentComposeButton = nullptr;
    void startAgentFromComposer();
    QWidget *m_agentDetail = nullptr;


    QPushButton *m_agentHideDetailButton = nullptr;
    bool m_agentDetailHidden = false;
    QLabel *m_agentTitle = nullptr;
    QLabel *m_agentStatusPill = nullptr;



    QLabel *m_agentMeta = nullptr;
    QFrame *m_agentMetaPopup = nullptr;
    QPushButton *m_agentInfoButton = nullptr;
    QLabel *m_agentNetPanel = nullptr;
    QPushButton *m_agentViewPrButton = nullptr;
    // "Create PR" — pull requests are user-driven (adhoc #2 follow-up): a run
    // finishing no longer opens one, this button does. Shown until the session
    // has a PR.
    QPushButton *m_agentCreatePrButton = nullptr;
    // "Create linked issue" — shown for ad-hoc sessions with no issue yet, so the
    // run can be promoted to a tracked issue from the detail header (adhoc #189).
    QPushButton *m_agentCreateIssueButton = nullptr;
    QPlainTextEdit *m_agentLog = nullptr;
    QStackedWidget *m_agentOutputStack = nullptr;
    TerminalWidget *m_agentTerminal = nullptr;
    int m_terminalSessionId = -1;



    ClaudeIdeBridge *m_ideBridge = nullptr;
    ClaudeIdeBridge *ensureIdeBridge();
    void onClaudeOpenDiff(const QString &tabName, const QString &oldPath,
                          const QString &newPath, const QString &newContents);




    ClaudeTranscriptView *m_agentTranscript = nullptr;
    QPushButton *m_transcriptModeButton = nullptr;
    QPushButton *m_terminalModeButton = nullptr;
    QWidget *m_agentOutputToggle = nullptr;



    QWidget *m_agentTranscriptTools = nullptr;



    QLineEdit *m_transcriptSearch = nullptr;
    QLabel *m_transcriptSearchCount = nullptr;
    QListWidget *m_agentFilesList = nullptr;
    QWidget *m_agentFilesPanel = nullptr;




    QTabWidget *m_agentDetailTabs = nullptr;
    int m_agentFilesTabIndex = -1;
    QTextBrowser *m_agentDiffView = nullptr;
    forkmesh::ui::DiffFileNavigator *m_agentDiffNav = nullptr;
    QLabel *m_agentFilesChangedSummary = nullptr;
    QLabel *m_agentCommitsHeading = nullptr;
    QListWidget *m_agentCommitsList = nullptr;




    int m_agentDiffRenderedSession = -1;





    QString m_agentDiffLastHtml;






    QString m_agentDiffRenderKey;
    QByteArray m_agentDiffRenderedPatch;
    QPushButton *m_agentMergeButton = nullptr;
    QPushButton *m_agentMergeDeleteButton = nullptr;
    QPushButton *m_agentUpdateButton = nullptr;
    QPushButton *m_agentWtDeleteButton = nullptr;
    QTimer *m_agentHourlyTimer = nullptr;



    QTimer *m_agentSyncPushTimer = nullptr;
    QTimer *m_agentSyncDebounceTimer = nullptr;



    QHash<QString, QByteArray> m_lastAgentPushPayload;



    QSet<QString> m_agentE2EEReady;
    QSet<QString> m_agentE2EEInFlight;
    QSet<QString> m_orgAgentJobsInFlight;


    QHash<int, QJsonObject> m_orgAgentBindings;


    QHash<int, ClaudeStreamSession *> m_streamSessions;
    QHash<int, CodexAppServerSession *> m_codexStreams;
    QHash<int, QList<QJsonObject>> m_streamEvents;





    int m_renderedTranscriptSession = -1;
    int m_renderedTranscriptCount = -1;





    int m_transcriptSkipped = 0;



    int m_renderedExternalSession = -1;


    int m_agentLogSession = -1;
    QString m_agentLogText;
    QHash<int, QString> m_streamRaw;
    QHash<int, qint64> m_sessionTokens;


    QHash<int, AgentScannerState> m_scannerStates;
    QTimer *m_scannerTimer = nullptr;


    QHash<int, AgentDiffStat> m_agentDiffStats;




    QHash<int, QString> m_agentDiffSig;
    bool m_agentDiffRefreshPending = false;
    bool m_agentDiffStatsRefreshing = false;
    bool m_agentDiffStatsRefreshQueued = false;
    int m_agentDiffStatsGen = 0;



    bool m_agentTableRefreshing = false;
    QHash<int, QString> m_lastAssistantText;
    void notifyAgentWaiting(int sessionId, bool needsPermission);
    void markAgentSessionRunning(int sessionId);
    QHash<int, QStringList> m_streamFiles;
    QHash<int, QString> m_streamWorktree;





    QHash<int, QThread *> m_worktreeTeardown;



    QHash<int, QStringList> m_streamPending;





    QHash<int, QString> m_sessionWorkdirCache;



    QHash<int, QString> m_pendingSteerMessage;



    QHash<int, AgentSession> m_streamSessionInfo;



    void startCliTranscript(AgentSession &session, const Issue &issue,
                            const QString &repoPath,
                            const QString &customPrompt = QString());









    void resolveAutoClaudeModel(int sessionId, const QString &task,
                                const QString &workdir, ClaudeStreamSession *live,
                                std::function<void(const QString &)> launch);
    void runClaudeAutoTriageRung(int sessionId, int rung, bool errorsOnly,
                                 const QString &task, const QString &workdir,
                                 ClaudeStreamSession *live,
                                 std::function<void(const QString &)> launch);
    void applyTranscriptEvent(int sessionId, const QJsonObject &ev);



    QString lastClaudeSessionId(int sessionId) const;
    QString lastCodexThreadId(int sessionId) const;
    void renderTranscriptForSession(int sessionId);





    void loadEarlierTranscriptEvents();


    void reapplyTranscriptSearch();




    void refreshAgentFilesPanel(int sessionId);
    void populateAgentFilesPanel(int sessionId, const QStringList &diffFiles);
    void scheduleAgentFilesDiff(int sessionId);






    struct AgentDiffProbe {
        QByteArray patch;
        bool patchOk = false;
        QSet<QString> uncommitted;
        QStringList commitLines;
        int behind = 0;
        int pending = 0;
    };




    void renderAgentDiff(int sessionId, const AgentDiffProbe &probe);
    void updateAgentFilesTabState(int sessionId);
    QString sessionBaseRef(int sessionId);
    QString sessionBaseBranch(int sessionId);




    QString sessionDiffBase(int sessionId, const QString &dir);
    QTimer *m_agentFilesDiffTimer = nullptr;
    void maybeCreatePullForStreamSession(int sessionId);









    void landAgentPullForSession(AgentSession session, const QString &patch,
                                 const QString &commits);
    bool isStreamTranscriptSession(int sessionId) const;





    void ensureStreamEventsLoaded(int sessionId);




    bool ensureStreamEventsLoadedAsync(int sessionId);
    QSet<int> m_streamEventsLoading;
    QSet<int> m_streamEventsAbsent;





    void stopStreamSession(int sessionId, bool refreshUi = true);








    void purgeSessionState(int sessionId);

    QString sessionWorkdir(int sessionId);






    QString cachedSessionWorktree(int sessionId, const QString &repoLocal,
                                  const QString &branch);




    void cleanupStreamWorktree(int sessionId);



    void maybeAutoMergeForSession(int sessionId);











    QString agentBotLabel(const QString &provider) const;

    QString composerAgentStrength() const;






    bool authenticateOrgTaskRequest(QUrl &url, QNetworkRequest &request,
                                    const QString &proofPrefix,
                                    const QString &resource) const;


    void openOrgTaskForSession(const AgentSession &session);









    void completeOrgTaskForSession(int sessionId,
                                   const QString &followUp = QString());



    void recordOrgTaskFields(int sessionId, const QString &taskId,
                             const QString &finishedByBot);








    static constexpr int kExternalIdBase = -1000;
    QTimer *m_externalClaudeTimer = nullptr;
    QList<ExternalClaudeSession> m_externalClaude;
    QHash<QString, int> m_externalTempId;
    QHash<int, ExternalClaudeSession> m_externalSurfaced;
    QHash<int, QString> m_externalSurfacedRepo;
    QHash<int, qint64> m_externalReadOffset;
    QString m_externalSig;
    int m_nextExternalTempId = kExternalIdBase;
    void scanExternalClaudeSessions();
    void onExternalClaudeTick();
    void injectExternalSessions();
    int registerExternalSession(const QString &uuid);
    void surfaceExternalSession(const QString &uuid);
    void unsurfaceExternalSession(int sessionId);
    void deleteExternalSession(int sessionId);
    int externalTempIdFor(const QString &uuid);
    bool isExternalSession(int sessionId) const { return sessionId <= kExternalIdBase; }
    bool externalIsLive(const QString &uuid) const;
    void renderExternalTranscript(int sessionId, bool full);
    void stopExternalSession(int sessionId);
    void killExternalSessionPids(const QList<qint64> &pids);
    QSet<QString> m_externalStopped;
    QSet<QString> ownStreamCwds() const;



    qint64 m_claudeModelsFetchedMs = 0;



    QJsonArray m_liveClaudeModels;







    int startAdHocAgentForRepo(int repoIndex, const QString &task,
                               const QString &provider, bool createPr,
                               const QString &model = QString(),
                               const QString &titleOverride = QString(),
                               bool genie = false);


    QString saveNewAgentPromptImage(const QImage &image);
    QPushButton *m_agentStopButton = nullptr;



    QPushButton *m_agentStopAllButton = nullptr;
    QPushButton *m_agentStartAllButton = nullptr;
    // Beside Start all: queued sessions / concurrent run limit, with direct
    // one-click controls for that limit.
    QLabel *m_agentQueueStatusLabel = nullptr;
    QPushButton *m_agentQueueLimitDecreaseButton = nullptr;
    QPushButton *m_agentQueueLimitIncreaseButton = nullptr;
    QPushButton *m_agentDeleteAllButton = nullptr; // delete agent + worktree + branch
    // Detail-toolbar buttons (adhoc #51) opening this session's branch in the
    // Branches tab and its worktree in the Worktrees tab. Full-size buttons like
    // their neighbours since adhoc #61; the names they open are rows in the Info
    // popup's list and tooltips here.
    QPushButton *m_agentBranchButton = nullptr;
    QPushButton *m_agentWorktreeButton = nullptr;


    QPushButton *m_agentDeleteMergedButton = nullptr;
    QPushButton *m_agentTestApiKeyButton = nullptr;
    QLabel *m_agentOpenAiSpend = nullptr;
    QLabel *m_agentApiKeyStatus = nullptr;
    QLabel *m_agentClaudeSpend = nullptr;
    QLabel *m_agentClaudeStatus = nullptr;

    QLabel *m_agentOpenAiCredit = nullptr;
    QLabel *m_agentClaudeCredit = nullptr;



    QLabel *m_agentTotalSpend = nullptr;
    double m_openAiSpendUsd = std::numeric_limits<double>::quiet_NaN();
    double m_claudeSpendUsd = std::numeric_limits<double>::quiet_NaN();
    void updateAgentTotalSpend();

    QLabel *m_agentLimitsLabel = nullptr;
    QTimer *m_agentLimitsTimer = nullptr;

    QPushButton *m_refreshButton = nullptr;
    QTimer *m_refreshSpinTimer = nullptr;
    int m_refreshAngle = 0;

    QPushButton *m_commitsBranchButton = nullptr;


    QPushButton *m_commitsFetchButton = nullptr;
    QPushButton *m_commitsPullButton = nullptr;


    int m_commitsLimit = 300;
    bool m_commitsHasMore = false;
    bool m_commitsLoadingMore = false;



    bool m_commitsShowingAll = false;

    QTimer *m_nodeSwitchSpinTimer = nullptr;
    int m_nodeSwitchAngle = 0;


    QProgressBar *m_nodeSwitchProgress = nullptr;
    void positionNodeSwitchProgress();

    QTimer *m_repoSwitchSpinTimer = nullptr;
    int m_repoSwitchAngle = 0;

    QTimer *m_issueSpinTimer = nullptr;
    int m_issueSpinFrame = 0;
    void tickIssueListSpinners();
    bool m_nodeSwitching = false;
    bool m_repoDetailLoading = false;



    bool m_branchesPanelLoading = false;
    bool m_branchesPanelReloadQueued = false;

    int m_branchesPanelGen = 0;


    QString m_branchesPanelDir;


    QString m_branchesPanelPendingSelect;




    QString m_branchMergedFlashBranch;
    QString m_branchMergedFlashDir;
    int m_branchMergedFlashRow = -1;



    static constexpr int kBranchMergedFlashMs = 20000;




    bool m_mirrorNodesPanelLoading = false;
    bool m_agentMergeStateRefreshing = false;





    bool m_heavyRefreshInFlight = false;
    bool m_repoListRefreshQueued = false;





    QString m_mirrorAdvertSig;
    QString m_mirrorAdvertInputSig;
    bool m_mirrorAdvertRefreshInFlight = false;
    qint64 m_mirrorAdvertCompletedAtMs = 0;
    void refreshMirrorAdverts();
    int m_repoOpenPending = -1;


    bool m_repoLoadActive = false;


    bool m_loadStatusShowing = false;



    QElapsedTimer m_loadStepTimer;
    QString m_loadStepName;
    QLabel *m_issueTitle = nullptr;
    QLineEdit *m_issueTitleEditor = nullptr;
    QPushButton *m_issueTitleEditButton = nullptr;
    QPushButton *m_issueTitleSaveButton = nullptr;
    QPushButton *m_issueTitleCancelButton = nullptr;
    QLabel *m_issueMeta = nullptr;
    QLabel *m_issueComposerAvatar = nullptr;
    QLabel *m_issueComposerTitle = nullptr;
    QLabel *m_issueReadonlyNote = nullptr;
    QLabel *m_issueInlineNotice = nullptr;
    QLabel *m_issueAssigneesValue = nullptr;
    QLabel *m_issueLabelsValue = nullptr;
    QLabel *m_issueMilestoneValue = nullptr;
    QLabel *m_issuePriorityValue = nullptr;


    QWidget *m_issueProgressSlider = nullptr;
    QLabel *m_issueEstimateValue = nullptr;
    QLabel *m_issueBountyValue = nullptr;
    QStackedWidget *m_issueAssigneesStack = nullptr;
    QStackedWidget *m_issueLabelsStack = nullptr;
    QStackedWidget *m_issueMilestoneStack = nullptr;
    QStackedWidget *m_issuePriorityStack = nullptr;
    QLineEdit *m_issueAssigneesEdit = nullptr;
    QLineEdit *m_issueLabelsEdit = nullptr;
    QComboBox *m_issueMilestoneEdit = nullptr;
    QComboBox *m_issuePriorityEdit = nullptr;


    QLabel *m_issueDatesValue = nullptr;
    QStackedWidget *m_issueDatesStack = nullptr;
    QPushButton *m_issueDatesButton = nullptr;
    QDateEdit *m_issueStartDateEdit = nullptr;
    QDateEdit *m_issueEndDateEdit = nullptr;
    QCheckBox *m_issueStartDateEnable = nullptr;
    QCheckBox *m_issueEndDateEnable = nullptr;
    QScrollArea *m_issueThreadScroll = nullptr;
    QWidget *m_issueThreadContainer = nullptr;
    QVBoxLayout *m_issueThreadLayout = nullptr;
    QWidget *m_issueAiTypingRow = nullptr;
    QTimer *m_issueAiTypingTimer = nullptr;
    MarkdownEditor *m_issueComposer = nullptr;
    QPushButton *m_issueNewButton = nullptr;


    QPushButton *m_issueListNewButton = nullptr;
    QPushButton *m_issueSyncButton = nullptr;
    QPushButton *m_issuePrioritizeButton = nullptr;


    QComboBox *m_issuePrioritizeAgentCombo = nullptr;
    bool m_prioritizeInFlight = false;


    QPushButton *m_issueCompletenessButton = nullptr;
    bool m_completenessInFlight = false;



    bool m_looperActive = false;
    int m_looperSessionId = 0;
    QString m_looperProvider;
    int m_looperCurrentIssue = 0;
    QString m_looperCurrentTitle;







    QWidget *m_looperToggle = nullptr;
    QString m_looperRepoSlug;
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
    QLabel *m_issueDevelopmentValue = nullptr;
    QPushButton *m_issueLinkPullButton = nullptr;



    QTabWidget *m_issueDetailTabs = nullptr;
    int m_issueFilesTabIndex = -1;
    QListWidget *m_issueFilesList = nullptr;
    QTextBrowser *m_issueDiffView = nullptr;
    forkmesh::ui::DiffFileNavigator *m_issueDiffNav = nullptr;
    QLabel *m_issueFilesChangedSummary = nullptr;
    QPushButton *m_issueDeleteButton = nullptr;
    QLabel *m_issueAgentValue = nullptr;
    QCheckBox *m_issueAgentCreatePrCheck = nullptr;
    QComboBox *m_issueAgentProvider = nullptr;
    QComboBox *m_issueAgentModel = nullptr;
    QPushButton *m_issueAssignAgentButton = nullptr;
    QPushButton *m_issueAgentViewButton = nullptr;

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




    bool m_keepCurrentOnReload = false;



    int m_selectIssueOnReload = -1;






    QString m_issuesLoadedSig;
    int m_issueLoadGen = 0;
    bool m_issueBackgroundLoadInFlight = false;
    bool m_issueBackgroundReloadQueued = false;
    QStringList m_pendingIssueAttachments;


    QTableWidget *m_projectTable = nullptr;
    QStackedWidget *m_projectViewStack = nullptr;
    QComboBox *m_projectStatusFilter = nullptr;


    QWidget *m_projectGantt = nullptr;
    QWidget *m_projectDetailPane = nullptr;
    QLabel *m_projectDetailTitle = nullptr;
    QLabel *m_projectDetailStatus = nullptr;
    QLabel *m_projectDetailBody = nullptr;
    QLabel *m_projectDetailProgress = nullptr;
    QLabel *m_projectInlineNotice = nullptr;
    QDateEdit *m_projectStartEdit = nullptr;
    QDateEdit *m_projectEndEdit = nullptr;
    QCheckBox *m_projectStartEnable = nullptr;
    QCheckBox *m_projectEndEnable = nullptr;
    QComboBox *m_projectMilestoneCombo = nullptr;
    QListWidget *m_projectIssuesList = nullptr;
    QPushButton *m_projectNewButton = nullptr;
    QPushButton *m_projectCloseButton = nullptr;
    QPushButton *m_projectDeleteButton = nullptr;
    QList<Project> m_currentProjects;
    int m_currentProjectNumber = -1;
    bool m_projectDeleteConfirmPending = false;


    QWidget *m_nodeProfilePanel = nullptr;
    QWidget *m_nodeProfileSectionHost = nullptr;
    QPushButton *m_profileCloseButton = nullptr;
    QWidget *m_repoDetailSection = nullptr;
    QLabel *m_profileAvatar = nullptr;
    QPixmap m_profileAvatarSource;
    QLabel *m_profileName = nullptr;
    QLabel *m_profileStatus = nullptr;



    QLabel *m_profileDetails = nullptr;
    QLabel *m_profileMirrorsLabel = nullptr;
    QLabel *m_profileMirrors = nullptr;



    QWidget *m_profileAccountSection = nullptr;
    QLabel *m_profileAccountLabel = nullptr;
    QLabel *m_profileAccountStatus = nullptr;
    QListWidget *m_profileUserNodesList = nullptr;
    QPushButton *m_profileLinkUserButton = nullptr;


    QPushButton *m_profileLinkBrowserButton = nullptr;
    int m_linkGrantPollsLeft = 0;


    QString m_linkGrantBaselineOwner;
    QString m_nodeOwnerUser;




    QStringList m_profileLinkedNodes;
    bool m_profileIsUserAccount = false;
    QLabel *m_profileNote = nullptr;

    QWidget *m_profileStatGrid = nullptr;
    QLabel *m_profileTileRepos = nullptr;
    QLabel *m_profileTileMirrored = nullptr;
    QLabel *m_profileTileOnline = nullptr;
    QLabel *m_profileTileChats = nullptr;


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

    QPushButton *m_profileTakeOwnershipButton = nullptr;


    QPushButton *m_profileGetPaidButton = nullptr;

    QWidget *m_profileSelfActions = nullptr;
    QPushButton *m_profileRebuildButton = nullptr;
    QPushButton *m_profileUpdateButton = nullptr;
    QString m_profileNodeId;
    QString m_profileNodeName;
    QString m_profileSolanaValue;

    QStringList m_channels;


    QStringList m_officeConversations;



    QSet<QString> m_privateChannels;
    QList<RepositoryRecord> m_repositories;



    QHash<QString, QTimer *> m_catalogPublishTimers;
    QHash<QString, qint64> m_catalogPublishOwnerLastAttemptMs;
    QSet<QString> m_catalogPublishInFlight;
    QSet<QString> m_catalogPublishQueued;
    QSet<QString> m_catalogPublishDialogQueued;



    QHash<QString, int> m_catalogPublishConsecutiveFailures;





    QHash<QString, QByteArray> m_catalogPublishedFingerprint;
    QHash<QString, qint64> m_catalogPublishedFingerprintAtMs;




    QHash<QString, bool> m_catalogPublishServeState;



    QSet<QString> m_privateControlReady;
    QSet<QString> m_privateControlInFlight;



    RepoContributionPublicationCache m_contributionPublicationCache;
    QHash<QString, QString> m_catalogContributionScanKey;
    QHash<QString, QString> m_catalogContributionSnapshotKey;
    QHash<QString, QString> m_catalogContributionDependencyFingerprint;
    QHash<QString, QString> m_catalogContributionPreparedScanKey;
    QHash<QString, QString> m_catalogContributionPreparedSnapshotKey;
    QList<RepoHost *> m_repoHosts;
    QSet<QString> m_repoHostKeys;
    NodeEventSocket *m_nodeEventSocket = nullptr;



    QHash<QString, std::shared_ptr<PrivateMirrorMaterialization>>
        m_privateMirrorMaterializations;
    QHash<QString, std::shared_ptr<PublicMirrorMaterialization>>
        m_publicMirrorMaterializations;
    QList<MemberInfo> m_homeRoster;
    QHash<QString, MemberInfo> m_chatDirectoryUsers;
    bool m_chatDirectoryFetchInFlight = false;
    qint64 m_chatDirectoryFetchedMs = 0;


    QTimer *m_chatDirectoryTimer = nullptr;
    bool m_chatDirectoryLoaded = false;
    QSet<QString> m_removedPeerIds;





    QHash<QString, qint64> m_peerLastSeenMs;


    bool m_welcomeAnnounced = false;
    // Catalog-backed mirror list (issue #223): the worker's /mirrors payload for
    // the repo group currently shown in the mirror-nodes panel, merged in so a
    // mirror that isn't live in the chat room is still listed for the owner.
    QString m_catalogMirrorsSource;        // "owner/name" the cache holds
    QJsonArray m_catalogMirrorsCache;      // last /mirrors payload's "mirrors"
    QString m_catalogMirrorsFetchSource;   // source the last fetch was kicked for
    qint64 m_catalogMirrorsFetchedMs = 0;  // throttle: last fetch kick time
    // "owner/repo|node" -> bounded result from the exact-node README probe.
    QHash<QString, QJsonObject> m_mirrorReachabilityCache;
    QSet<QString> m_mirrorReachabilityInFlight;
    // Per-artifact release download counts for the repo currently shown in the
    // Releases panel (sha256 -> times downloaded), from the worker's
    // /releases/downloads endpoint.
    QString m_releaseDownloadsSource;      // "owner/name" the cache holds
    QHash<QString, int> m_releaseDownloadsCache; // sha256 -> download count
    QString m_releaseDownloadsFetchSource; // source the last fetch was kicked for
    qint64 m_releaseDownloadsFetchedMs = 0; // throttle: last fetch kick time
    // "owner/name" -> { times served through the mainnode, clones }.
    QHash<QString, QPair<int, int>> m_repoStats;




    QHash<int, bool> m_syncingRepos;
    QSet<int> m_pushingRepos;


    QSet<QString> m_sshMirrorPushing;



    QSet<QString> m_sshMirrorPushPending;



    QSet<QString> m_mentionScanInFlight;
    QString m_currentConversation;

    QHash<QString, QList<ChatMessage>> m_history;
    QSet<QString> m_historyIds;
    QTimer *m_chatSaveTimer = nullptr;
    QTimer *m_chatExpiryTimer = nullptr; // periodic pruneExpiredChatHistory()
    QHash<QString, MessageRow *> m_visibleRows; // messageId -> row (current conv)
    QString m_activeChatThreadRootId;
    QDialog *m_chatThreadDialog = nullptr;
    QVBoxLayout *m_chatThreadRowsLayout = nullptr;
    QPlainTextEdit *m_chatThreadInput = nullptr;
    QLabel *m_chatThreadCountLabel = nullptr;
    // messageId -> emoji -> reactor display names.
    QHash<QString, QMap<QString, QStringList>> m_reactions;
    QHash<QString, QPixmap> m_avatars;
    QHash<QString, QString> m_dmNames;
    QHash<QString, QHash<QString, QString>> m_typing;
    QStringList m_networkLog;


    QHash<QString, int> m_logFilterCounts;
    int m_networkLogDiskLines = 0;
    QStringList m_openDms;
    QSet<QString> m_unread;


    QSet<QString> m_knownAdminPubkeys;


    QHash<QString, int> m_unreadCounts;
    QString m_userName;
    QByteArray m_userAvatar;
    QString m_lastChatDisplayName;
    QByteArray m_lastChatAvatar;
    QString m_typingConversation;
    QTimer *m_typingStopTimer;
    QTimer *m_homeStatsTimer = nullptr;


    QTimer *m_repoChangeBadgeTimer = nullptr;
    qint64 m_connectedAtMs = 0;
    qint64 m_totalConnectionMs = 0;


    qint64 m_uptimePersistedAtMs = 0;



    qint64 m_nodeAlertGraceUntilMs = 0;


    bool m_accountAuthenticated = false;
    QString m_accountName;



    QString m_accountSessionToken;
    bool m_accountSolanaVerified = false;
    bool m_accountDesktopCapable = false;


    QString m_accountTier = QStringLiteral("free");
    QTimer *m_heartbeatTimer = nullptr;
    bool m_isAdmin = false;
    QTimer *m_adminPollTimer = nullptr;
    QStringList m_seenPendingUsers;


    QString m_roomPassphrase;


    QString m_lastClaimCodeShown;



    QString m_lastOwnershipTransferAdminShown;



    QSet<QString> m_handledMirrorRequests;
    QStringList m_pendingMirrorRequestAcks;



    QPushButton *m_userAvatarNavButton = nullptr;
#ifdef FORKMESH_WINDOW_TESTS
    bool m_testUseAccountFlowResult = false;
    bool m_testAccountFlowResult = true;
    bool m_testAccountFlowDesktopCapable = true;
    int m_testEnsureNodeAccountCalls = 0;
    bool m_testBypassServerStart = false;
    TestIssueHistoryDeleteRunner m_testIssueHistoryDeleteRunner;
#endif
};
