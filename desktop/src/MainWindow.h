#ifndef FORKMESH_MAIN_WINDOW_H
#define FORKMESH_MAIN_WINDOW_H

#include "ChatBackend.h"
#include "ClientErrorReports.h"
#include "PingSyncState.h"
#include "LogSource.h"
#include "DiscussionInboxBackoff.h"
#include "NetworkBackoff.h"
#include "DiscussionStore.h"
#include "ForkMeshIdentity.h"
#include "IssueStore.h"
#include "ProjectStore.h"
#include "PullStore.h"
#include "PullReviewModel.h"
#include "MergeQueue.h"
#include "CoveStore.h"
#include "ActionStore.h"
#include "ActionFile.h"
#include "AccountCapability.h"
#include "AccountSessionCache.h"
#include "AgentStore.h"
#include "AgentRunner.h"
#include "ClaudeSessionScan.h"
#include "DirectorySizeScan.h"
#include "RepoSecurity.h"
#include "RepoContributionSnapshot.h"
#include "MirrorCrypto.h"
#include "MirrorGatewayHealth.h"
#include "GuiPump.h"

namespace forkmesh::ui { class LogActivityLights; }

struct AgentScannerState {
    qint64 lastActivityMs = 0; // wall-clock of the last raw-output chunk
    double intensity = 0.0;    // 0..1 live-output rate the effect reacts to
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
    QString servedCommit; // tip of the branch we actually serve
    int pendingPush = 0;  // local commits not yet in the served mirror
    qint64 gatheredMs = 0;
};

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
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
#include <QRect>
#include <QSet>
#include <QTextBlockUserData>
#include <QTextCursor>
#include <QThread>
#include <QTimer>
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
class BusySpinner;
class NodeDotMatrix;
class RelaySpeedDot;
class ActionRunStrip;
class BackgroundTaskChip;
class ElidingStatusLabel;
class VerticalIconButton;
}
using forkmesh::ui::ActionRunStrip;
using forkmesh::ui::ActivityRailButton;
using forkmesh::ui::VerticalIconButton;
using forkmesh::ui::AgentDotMatrix;
using forkmesh::ui::BusySpinner;
using forkmesh::ui::ElidingStatusLabel;
using forkmesh::ui::NodeDotMatrix;
using forkmesh::ui::RelaySpeedDot;
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
class LogTimelineChart;
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
struct CloudflareBootstrapCommand;
}
namespace forkmesh::ui {
class DiffFileNavigator;  // file-list <-> diff-view sync
struct DiffFileEntry;     // one changed file parsed out of a patch
}

// One commit of the pull request currently on screen, in the form the
// Conversation feed needs. renderPullCommits() already walks base..head (or the
// signed mbox) for the Commits tab, so it records what it found and
// renderPullThread() reuses it: the conversation must not repeat that walk,
// which pumps the GUI event loop.
struct PullActivityCommit {
    QString sha;
    QString subject;
    QString author;
    QString when;         // absolute, already formatted for display
    qint64 committedSecs = 0;
    QString agentTrailer; // ForkMesh-Agent trailer, when the commit has one
};

struct ServerConfig {
    QString url;
    QString room;
};

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
    QString privateReplicaId;
    // Legacy handle from releases that age-encrypted public repositories.
    // New public mirrors use the durable plaintext bare mirrorPath; this is
    // retained only until a successful sync removes the old archive.
    QString publicArchiveId;
    bool publishToNetwork = false;
    // Private repo: absent from public discovery. Authorized owners and
    // collaborators fetch only its opaque encrypted replica over HTTPS, then
    // decrypt into short-lived owner-only local storage with their own key.
    bool isPrivate = false;
    bool actionsEnabled = true;
    bool actionsAutoApprove = true;
    bool requirePeerApproval = true;
    bool mergeQueueEnabled = false;
    bool mergeQueuePaused = false;
    QStringList mergeQueue;
    bool externallyManagedActions = false;
    QString externalActionsSource;
    QString externalActionsRef;
    QStringList disabledWorkflows;
    QStringList workflowNodes;
    // Block pushes when the diff introduces a high-confidence secret (API key,
    // private key, etc.). Enabled by default; the user can bypass per-push or
    // turn it off entirely here.
    bool secretScanningEnabled = true;
    bool previewOnly = false;
    qint64 hostedSinceMs = 0;
    qint64 lastSyncMs = 0;
    qint64 publishedAtMs = 0;
};

struct NotificationLink {
    QString kind;
    QString owner;   // repo owner
    QString name;    // repo name
    int number = -1; // issue / PR / discussion number
    QString ref;     // commit hash ("commit") or chat channel ("chat")

    bool isValid() const { return !kind.isEmpty(); }
};
Q_DECLARE_METATYPE(NotificationLink)

struct ComposerModelChoice {
    QString provider;  // "claude-code", "codex", "openai", "cloudflare-ai", …
    QString model;     // empty for the provider-only API agents
    QString label;     // bare model name shown in the row, e.g. "Opus 5"
    QString agentName; // which CLI/API runs it, for the tooltip
    QString tooltip;   // fixed tooltip; ranked rows build theirs from counts
    int iconIndex = 0; // agentControlIcon() slot
    bool ranked = false;
};

struct GlobalSearchHit {
    QString kind;       // see kSearchCategories in MainWindowSearch.cpp
    int number = 0;     // issue/PR/discussion/project/agent/run id, or repo index
    QString path;       // file path, branch, worktree branch, commit/tag ref
    int line = 0;       // code hit: 1-based line number
    int repoIndex = -1; // repository the hit belongs to (-1 = the open one)
    QString primary;    // main row text
    QString detail;     // dimmer context (where it matched)
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Safe target for the website's secret-free setup link. Only bounded
    // public deployment topology may be prefilled; credentials are rejected
    // by the URL parser and entered in the focused session-only local field.
    void openCloudflareSetupFromSystemLink(const QString &target = {});

    void logCapturedMessage(QtMsgType type, const QString &text,
                            const QString &sourceFile = QString(),
                            int sourceLine = 0);

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
    void testLoadNetworkLog(const QStringList &lines)
    {
        m_networkLog = lines;
        m_networkLogViewStale = true;
    }
    void testRecordUiStall(qint64 peakMs, const QString &blockingCall,
                           const QString &backtrace)
    {
        onUiStall(peakMs, blockingCall, backtrace);
    }
    QString testStallFixPrompt() const { return stallFixPrompt(); }
    void testFlashMessage(const QString &text, bool error = false,
                          const QString &clickHref = QString(),
                          const QString &kind = QString())
    {
        flashMessage(text, error, clickHref, 0, kind);
    }
    void testAddNotification(const QString &title, const QString &body,
                             bool warning = false)
    {
        addNotification(title, body, warning);
    }
    void testSetRestartCautionFlash(bool active)
    {
        if (active)
            startRestartCautionFlash();
        else
            stopRestartCautionFlash();
    }
    void testShowPromptBubble(const QString &prompt, const QString &status)
    {
        showPromptBubble(prompt, -1, status);
    }
    void testCloudLogMonitorLine(const QString &line)
    {
        handleCloudLogMonitorLine(line);
    }
    void testCloudLogMonitorChunk(const QByteArray &chunk)
    {
        consumeCloudLogMonitorBytes(chunk);
    }
    int testCloudLogMonitorErrors() const { return m_cloudLogMonitorErrors; }
    int testCloudLogMonitorEvents() const { return m_cloudLogMonitorEvents; }
    void testResetCloudLogMonitor()
    {
        setCloudLogMonitorEnabled(false);
        cancelCloudLogMonitorRestart();
        m_cloudLogMonitorErrors = 0;
        m_cloudLogMonitorEvents = 0;
        m_cloudLogMonitorRestarts = 0;
        m_cloudLogMonitorIdleReason.clear();
    }
    int testCloudLogMonitorTailEnded(int exitCode, qint64 uptimeMs)
    {
        handleCloudLogMonitorEnded(exitCode, uptimeMs);
        const int pending = m_cloudLogMonitorRestartTimer &&
                                    m_cloudLogMonitorRestartTimer->isActive()
                                ? m_cloudLogMonitorRestartTimer->remainingTime()
                                : 0;
        cancelCloudLogMonitorRestart();
        return pending;
    }
    bool testCloudLogMonitorRunning() const { return cloudLogMonitorRunning(); }
    QPushButton *testLogFilterChip(const QString &category) const;
    int testLogFilterChipCount(const QString &category) const
    {
        return logFilterChipCount(category);
    }
    QRect testTopMessageRect() { return topMessageBubbleRect(); }
    int testTopMessageQueueDepth() const { return m_topMessageQueue.size(); }
    void testDismissTopMessage() { dismissTopMessage(); }
    QString testTopMessageRaw() const { return m_topMessageRaw; }
    bool testErrorBorderVisible() const
    {
        return m_errorBorderOverlay && m_errorBorderOverlay->isVisible();
    }
    void testNotifyAgentDone(int sessionId, const QString &closingSummary)
    {
        if (closingSummary.isEmpty())
            m_lastAssistantText.remove(sessionId);
        else
            m_lastAssistantText[sessionId] = closingSummary;
        notifyAgentDone(sessionId);
    }
    QString testTopMessageAgentHeadline() const;
    bool testTopMessageAgentIconShown() const;
    bool testTopMessageAvatarShown() const;
    void testDeliverChatMessage(const QString &conversation,
                                const QString &senderId,
                                const QString &senderName, const QString &text)
    {
        static int seq = 0;
        ChatMessage message;
        message.id = QStringLiteral("test-chat-%1").arg(++seq);
        message.conversation = conversation;
        message.senderId = senderId;
        message.senderName = senderName;
        message.text = text;
        message.timestampMs = QDateTime::currentMSecsSinceEpoch();
        onMessage(message);
    }
    bool testCelebrationBorderVisible() const
    {
        return m_celebrationBorderOverlay &&
               m_celebrationBorderOverlay->isVisible();
    }
    void testResetLoggedErrorAlerts()
    {
        m_lastLoggedErrorText.clear();
        m_lastLoggedErrorAtMs = 0;
        m_loggedErrorBurstStartMs = 0;
        m_loggedErrorBurstCount = 0;
        m_loggedErrorBurstNoticeShown = false;
        if (m_errorBorderTimer)
            m_errorBorderTimer->stop();
        if (m_errorBorderOverlay)
            m_errorBorderOverlay->hide();
    }
    bool testDraftStallPromptInComposer();
    QString testQuickAddText() const;
    void testShowSettingsSection() { showSection(1); }
    void testShowHomeSection() { showSection(0); }
    void testShowLogSection() { showSection(4); }
    void testSetLogOverlayExpanded(bool expanded)
    {
        setLogOverlayExpanded(expanded);
    }
    bool testApplyFooterWebsiteStatusPayload(const QJsonObject &payload)
    {
        return applyFooterWebsiteStatusPayload(payload);
    }
    void testApplyFooterWebsiteStatusFailure(int httpStatus)
    {
        applyFooterWebsiteStatusFailure(httpStatus);
    }
    QUrl testWebsiteStatusTargetUrl(const QString &statusId) const
    {
        return websiteStatusTargetUrl(statusId);
    }
    QString testApplyDesktopWebsiteProbe(const QString &id, int httpStatus,
                                         const QByteArray &body,
                                         const QString &transportError = QString());
    QString testDesktopWebsiteRowStatus(const QString &id) const;
    QStringList testWebsiteRecheckProbes(const QString &statusId) const;
    void testClearDesktopProbeHistory() { m_desktopProbeHistory.clear(); }
    int testNotificationsTitled(const QString &needle) const
    {
        int count = 0;
        for (const AppNotification &item : m_notifications) {
            if (item.title.contains(needle))
                ++count;
        }
        return count;
    }
    QString testPingStatusFor(const QString &needle);
    void testSetFooterUpdateLine(const QString &line)
    {
        setFooterUpdateLine(line);
    }
    void testShowHostsSection() { showSection(7); }
    void testSetDirectoryUserNodes(const QString &user,
                                   const QStringList &nodes);
    void testShowNodesSection();
    QStringList testNodeDirectoryNames() const;
    void testSetAdmin(bool admin)
    {
        m_isAdmin = admin;
        updateAdminCrownBadge();
        applyDebugBarStartupPreference();
    }
    bool testUsersNavButtonVisible() const;
    void testShowUsersSection() { showSection(kUsersSectionIndex); }
    void testApplyUsersDirectory(const QJsonArray &users)
    {
        mergeChatUserDirectory(users);
    }
    bool testFilesNavButtonVisible() const;
    void testShowFilesSection() { showSection(kFilesSectionIndex); }
    void testSetFileExplorerShowHidden(bool on);
    void testSetFileExplorerRoot(const QString &path)
    {
        setFileExplorerRoot(path);
    }
    QString testFileExplorerRoot() const { return m_fileExplorerRoot; }
    QStringList testFileExplorerNames() const;
    QStringList testExpandFileExplorerEntry(const QString &name);
    QStringList testExpandedFileExplorerPaths() const;
    void testPreviewFileExplorerFile(const QString &path)
    {
        previewFileExplorerFile(path);
    }
    QString testFileExplorerPreview() const;
    void testRevealFileInExplorer(const QString &path, int line)
    {
        revealFileInExplorer(path, line);
    }
    int testFileExplorerPreviewLine() const;
    QStringList testUsersColumns() const;
    QString testUsersCellText(int row, const QString &header) const;
    QStringList testSortUsersBy(const QString &header, Qt::SortOrder order);
    QStringList testChatMemberNames(const QString &conversation);
    void testRenderNetworkRepos(const QJsonArray &repos);
    QStringList testNetworkRepoNames() const;
    QString testNetworkRepoActionText(int row) const;
    QString testNetworkRepoMirrorHeader() const;
    QStringList testNetworkRepoColumns() const;
    QString testNetworkRepoCellText(int row, const QString &header) const;
    bool testNetworkRepoHasCommitSparkline(int row) const;
    QString testNetworkRepoCommitActivitySummary(int row) const;
    QStringList testNetworkTabLabels() const;
    void testRenderNetworkWebRequests(const QJsonObject &payload);
    QStringList testNetworkWebRequestsGroups() const;
    QString testNetworkWebRequestsCellText(int row, const QString &header) const;
    QString testNetworkWebRequestsStatusText() const;
    int testNetworkWebRequestsChartHeight() const;
    QString testNetworkWebRequestsFailureText(int httpStatus,
                                              const QString &transportError,
                                              const QString &parseError,
                                              const QString &payloadError,
                                              const QByteArray &body,
                                              qint64 cooldownMs,
                                              qint64 retryInMs);
    int testReposNavBadgeCount() const;
    void testRebuildNetworkLogView() { rebuildNetworkLogView(); }
    QStringList testLogFilterChipLabels() const;
    void testSetLogFilter(const QString &category)
    {
        m_logFilter = category;
        rebuildLogFilterButtons();
        rebuildNetworkLogView();
        refreshLogTimelineChart();
    }
    QString testLogBadgeFor(const QString &storedLine) const
    {
        return logBadgeFor(storedLine);
    }
    QTextBrowser *testNetworkLogView() const { return m_settingsLog; }
    QTextEdit *testFooterLogView() const { return m_footerUpdateLog; }
    QWidget *testLogTimelineChart() const;
    int testLogTimelineVisibleCount() const;
    void testSetLogTimelineHours(int hours);
    QString testLogTimelineSummary() const;
    bool testFaviconCached(const QString &host) const
    {
        return m_faviconCache.contains(host);
    }
    void testScrollNetworkLogToTop() { onNetworkLogScrolled(0); }
    QStringList testQuickUpdatePullArguments(const QString &clientDir) const;
    QString testWorkingClientDir() const;
    QString testRunningClientDir() const;
    QString testRunningClientExecutable() const;
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
    QString testRecentUserInstance(const QString &accountName) const;
    void testCacheAccountSession(const QString &accountName,
                                 const QString &instanceUrl,
                                 const QString &token,
                                 bool desktopCapable,
                                 const QString &desktopPublicKey);
    QString testCachedAccountSession(const QString &accountName,
                                     const QString &instanceUrl) const;
    bool testCachedAccountSessionMaySendTo(const QString &accountName,
                                           const QString &instanceUrl,
                                           const QUrl &target) const;
    bool testCachedAccountSessionMayRestoreDesktop(
        const QString &accountName, const QString &instanceUrl,
        const QString &currentPublicKey,
        const QString &validatedAccountPublicKey) const;
    void testClearAccountSessionCache();
    int testAccountSessionCacheSize() const { return m_accountSessionCache.size(); }
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
    bool testRepoRequiresPeerApproval() const
    {
        return m_repoDetailIndex >= 0 &&
               m_repoDetailIndex < m_repositories.size() &&
               m_repositories.at(m_repoDetailIndex).requirePeerApproval;
    }
    void testSetRepoRequirePeerApproval(bool on)
    {
        setRepoRequirePeerApproval(on);
    }
    Q_INVOKABLE QString testRepoDefaultBranch() const;
    Q_INVOKABLE void testRefreshOpenRepoDetail() { refreshOpenRepoDetail(); }
    Q_INVOKABLE QString testRepoActionsTabText() const;
    Q_INVOKABLE QString testRepoBranchesButtonText() const;
    Q_INVOKABLE bool testShowRepoIssuesTab();
    void testSetPendingInboxCounts(int issues, int pulls, int discussions);
    int testIssueInboxBadgeCount() const;
    int testPullInboxBadgeCount() const;
    int testPullsTabAlertBadgeCount() const;
    QString testDiscussionInboxButtonText() const;
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
                                               bool isPrivate = false,
                                               bool localOnly = false)
    {
        return provisionNewRepository(dest, name, description, firstPrompt,
                                      addReadme, isPrivate, localOnly, nullptr);
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
    int testRepoTabContentTop(); // y of the tab content within the window
    int testRepoTabGapAroundIssues() const;
    int testRailTabIconLineSkew() const;
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
    void testRefreshQuickAddAgentModelSelector();
    QString testQuickAddAgentModelLabel(const QString &model) const;
    QString testQuickAddAgentModelMergedNote(const QString &model) const;
    QString testPromptOverlayPlacement() const;
    QStringList testShortcutMetadata(const QString &filePath) const;
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
    bool testMirrorNodesAreSingleLineRows() const;
    QString testMirrorNodeCellText(const QString &nodeName, int column) const;
    QString testMirrorNodeCellToolTip(const QString &nodeName, int column) const;
    bool testDraftMirrorNodeDiagnostics(const QString &nodeName);
    QString testFleetBinaryInstallRemoteCommand(bool reinstall,
                                                qsizetype *uploadByteCount,
                                                QString *errorOut);
    QString testDirectBinaryInstallRemoteCommand(qsizetype *uploadByteCount,
                                                 QString *errorOut);
    QString testVultrGoMirrorInstallRemoteCommand(qsizetype *uploadByteCount,
                                                  QString *errorOut);
    void testReloadBranchesPanel();
    QString testBranchWorktreePath(const QString &branch) const;
    QString testBranchVisualBadges(const QString &branch) const;
    QString testBranchHealthIcon(const QString &branch) const;
    bool testBranchesUseCompactColumns() const;
    bool testBranchesKeepFlexibleNameColumn() const;
    bool testBranchDelegatePaintsSingleTextLayer(const QString &branch) const;
    bool testBranchSelectedTextColorIsReadable(const QString &branch) const;
    void testAddAgentSession(const AgentSession &session)
    {
        if (m_agentStore)
            m_agentStore->saveSession(session);
        for (AgentSession &existing : m_agentSessions) {
            if (existing.id == session.id) {
                existing = session;
                return;
            }
        }
        m_agentSessions.append(session);
    }
    void testRefreshAgentQueueControls() { refreshAgentQueueControls(); }
    void testUpdateAgentActionState() { updateAgentActionState(); }
    void testRefreshAgentDiffStats()
    {
        m_agentDiffRefreshPending = true;
        refreshAgentTable();
    }
    int testAgentSessionForPullId(int prNumber, const QString &headBranch) const
    {
        const AgentSession *session = agentSessionForPull(prNumber, headBranch);
        return session ? session->id : 0;
    }
    QString testResolvablePullHead(PullRequest pr) const
    {
        return resolvablePullHead(std::move(pr));
    }
    void testSeedPullActivityCommits(int prNumber,
                                     const QList<PullActivityCommit> &commits)
    {
        m_pullActivityCommitsNumber = prNumber;
        m_pullActivityCommits = commits;
    }
    void testRenderPullThread(const PullRequest &pr) { renderPullThread(pr); }
    QStringList testPullThreadCardHeaders() const;
    int testPullActivityExtraCards() const { return m_pullActivityExtraCards; }
    bool testBindAgentSessionsToPull(int prNumber, const QString &headBranch)
    {
        return bindAgentSessionsToPull(prNumber, headBranch);
    }
    int testAgentSessionPullNumber(int sessionId) const
    {
        for (const AgentSession &session : m_agentSessions)
            if (session.id == sessionId)
                return session.prNumber;
        return 0;
    }
    QString testAgentPrButtonText(int sessionId);
    QString testAgentMetaHtml(int sessionId);
    void testOpenAgentsOverview() { openAgentsOverview(); }
    void testRefreshAgentDotMatrix() { refreshAgentDotMatrix(); }
    int testAgentDotCount() const;
    void testSetAgentSessionStatus(int sessionId, const QString &status);
    void testRemoveAgentSession(int sessionId);
    void testSeedResumeConversationId(int sessionId, const QString &conversationId,
                                      bool codex)
    {
        m_streamEvents[sessionId].append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("system")},
            {QStringLiteral("subtype"), QStringLiteral("init")},
            {codex ? QStringLiteral("thread_id") : QStringLiteral("session_id"),
             conversationId}});
    }
    QString testReplayCliExitsWithoutResult(int sessionId, bool codex, int times)
    {
        QString outcomes;
        for (int i = 0; i < times; ++i) {
            if (AgentSession *session = findAgentSession(sessionId))
                session->status = AgentStatus::Running;
            outcomes += applyCliExitWithoutResult(sessionId, codex, /*exitCode=*/1)
                            ? QLatin1Char('q')
                            : QLatin1Char('f');
            m_agentQueue.removeAll(sessionId);
        }
        return outcomes;
    }
    void testQueueAgentSteerMessage(int sessionId, const QString &prompt)
    {
        queueAgentSteerMessage(sessionId, prompt);
    }
    QString testPendingSteerMessage(int sessionId) const
    {
        return m_pendingSteerMessage.value(sessionId);
    }
    QString testTakeSteerMessageForLaunch(int sessionId)
    {
        const QString steer = m_pendingSteerMessage.take(sessionId);
        if (steer.isEmpty())
            m_inFlightSteerMessage.remove(sessionId);
        else
            m_inFlightSteerMessage.insert(sessionId, steer);
        return steer;
    }
    QString testAgentSessionLastError(int sessionId)
    {
        const AgentSession *session = findAgentSession(sessionId);
        return session ? session->lastError : QString();
    }
    void testMarkAgentSessionRunning(int sessionId)
    {
        markAgentSessionRunning(sessionId);
    }
    bool testQueueDetachedRunningAgentSession(int sessionId)
    {
        continueAgentSession(sessionId, /*deferRefresh=*/true);
        const AgentSession *session = findAgentSession(sessionId);
        const bool queued = session && session->status == AgentStatus::Queued &&
                            m_agentQueue.contains(sessionId);
        m_agentQueue.removeAll(sessionId);
        return queued;
    }
    void testTypeGlobalSearch(const QString &text);
    QString testAgentSearchText() const;
    QString testTranscriptSearchText() const;
    void testAppendAgentTranscript(int sessionId, const QString &text)
    {
        if (const AgentSession *session = findAgentSession(sessionId))
            if (m_agentStore)
                m_agentStore->appendLog(*session, text);
    }
    void testRunAgentTranscriptSearch() { runAgentTranscriptSearch(); }
    void testScanAgentSessionImages()
    {
        m_agentImageScanPending = true;
        refreshAgentTable();
    }
    QStringList testAgentRowTitles() const;
    QStringList testAgentRowImages(int sessionId) const;
    bool testAgentRowHasThumbnail(int sessionId) const;
    bool testBranchAttachmentHasIcon(const QString &branch) const;
    QStringList testBranchRowOrder() const;
    int testBranchesTabIndex() const { return m_branchesTabIndex; }
    int testOverviewBodyPage() const;
    bool testClickBranchRowInOverview(const QString &branch);
    bool testBranchesPanelOwnsDiffView() const;
    bool testGitWorkspaceIsExclusive() const;
    bool testGitPromptFloatsBottomLeft() const;
    QString testSwitchToBranchImmediateSelection(const QString &branch);
    QString testSwitchToWorktreeGitBranch(const QString &branch)
    {
        switchToWorktree(branch);
        return m_repoBranch;
    }
    QString testBrowsedBranch() const { return m_repoBranch; }
    void testSwitchToAgentBranch(int sessionId) { switchToAgentBranch(sessionId); }
    int testCommitWorkspacePage() const;
    QString testBranchDiffBranch() const { return m_branchDiffBranch; }
    QStringList testBranchDiffFiles() const { return m_branchDiffFilePaths; }
    QString testBranchDiffText() const;
    bool testBranchDiffCached(const QString &branch) const;
    bool testBranchDiffPaintedFromCache() const
    {
        return m_branchDiffPaintedFromCache;
    }
    QStringList testSourceControlPaths() const;
    QString testSourceControlDiffText() const;
    QString testScmCommitControlsState() const;
    QString testSourceControlGitDir() const { return sourceControlGitDir(); }
    void testRefreshSourceControl() { refreshSourceControl(/*force=*/true); }
    bool testClickSourceControlPath(const QString &path);
    QString testArrowOnSourceControl(bool down);
    QString testBranchStickyHeaderText() const;
    QString testLastSourceControlDiffPath() const
    {
        return m_lastSourceControlDiffPath;
    }
    QString testScmStrokedRowFile() const;
    QString testBranchOutlinedFile() const;
    QRect testBranchOutlineRect() const;
    QString testScmOutlinedFile() const;
    QRect testScmOutlineRect() const;
    bool testScmDiffUsesFullSurface() const;
    bool testScmDiffFilePinnedToTop(const QString &path) const;
    int testGitFilesSlotPage() const;
    int testGitHistorySlotPage() const;
    bool testScmAutoViewedRoundTrip();
    QString testCompareIndicatorText() const;
    QString testComparedBranchText() const;
    void testSetCompareBase(const QString &base);
    void testCloseBranchRange();
    void testClickRailGitButton();
    int testGitPendingSyncCount() const;
    void testNavigateBack() { navigateBack(); }
    void testNavigateForward() { navigateForward(); }
    QString testNavBackToolTip() const;
    QString testNavForwardToolTip() const;
    void testOpenRepoFile(const QString &path) { openRepoFile(path); }
    QString testOpenRepoFilePath() const { return openRepoFilePath(); }
    void testOpenRepoDirectory(const QString &path) { loadRepoOverview(path); }
    QString testOverviewDirectory() const { return m_overviewPath; }
    int testFilesStackPage() const;
    bool testFilesSectionShowing() const;
    QString testOpenCommitHash() const;
    void testShowCommit(const QString &hash) { showCommit(hash); }
    bool testClickBranchReviewMerge(bool deleteAll);
    void testPullBaseIntoAllBranches() { pullBaseIntoAllBranches(); }
    bool testBranchPullEnabled() const;
    bool testClickBranchPull();
    bool testMarkAgentBranchMerged(const QString &branch, bool mergeVerified = false)
    {
        return markAgentSessionsMerged(0, branch, mergeVerified);
    }
    bool testMergeBranchAndCleanUp(const QString &branch)
    {
        return mergeWorktreeIntoMain(branch,
                                     worktreePathForBranch(repoGitDir(), branch),
                                     /*deleteAgent=*/true);
    }
    bool testHasAgentSession(int sessionId)
    {
        return findAgentSession(sessionId) != nullptr;
    }
    void testRefreshAgentMergeState() { refreshAgentMergeState(); }
    bool testAgentMergeStateRefreshing() const { return m_agentMergeStateRefreshing; }
    QString testAgentStatusCellText(int sessionId) const;
    QString testAgentDetailTitleText() const;
    bool testAgentDetailTitleWraps() const;
    QString testRenderAgentDetailTitle(const QString &text);
    QString testAgentStatusCellBadges(int sessionId, const AgentDiffStat &stat) const;
    QString testAgentStatusCellToolTip(int sessionId,
                                       const AgentDiffStat &stat) const;
    void testSetCachedAgentDiffFiles(int sessionId, int files)
    {
        AgentDiffStat stat = m_agentDiffStats.value(sessionId);
        stat.files = files;
        m_agentDiffStats.insert(sessionId, stat);
    }
    int testCachedAgentDiffFiles(int sessionId) const
    {
        return m_agentDiffStats.value(sessionId).files;
    }
    QStringList testAgentOwnedDiffPaths(const QString &gitDir,
                                        const QString &base,
                                        const QString &branch) const;
    bool testAgentSessionMerged(int sessionId) const;
    void testApplyCodexRateLimits(const QJsonObject &rateLimits)
    {
        applyCodexRateLimits(rateLimits);
    }
    QString testCodexUsageToolTip() const
    {
        return m_navCodexUsage ? m_navCodexUsage->toolTip() : QString();
    }
    QString testClaudeUsageToolTip() const
    {
        return m_navTokenUsage ? m_navTokenUsage->toolTip() : QString();
    }
    void testShowAgentAccountMenu(const QString &provider)
    {
        showAgentAccountMenu(provider, QPoint(20, 20));
    }
    void testApplyClaudeUsageResponse(const QJsonObject &response)
    {
        applyClaudeUsageResponse(response);
    }
    void testSelectAgentAccount(const QString &provider,
                                const QString &accountId)
    {
        selectAgentAccount(provider, accountId);
    }
    bool testRenameAgentAccount(const QString &provider,
                                const QString &accountId,
                                const QString &label)
    {
        return renameAgentAccount(provider, accountId, label);
    }
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
    void paintEvent(QPaintEvent *event) override;
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
    static constexpr int kNotesSectionIndex = 16;
    static constexpr int kUsersSectionIndex = 17;
    static constexpr int kFilesSectionIndex = 18;

    QWidget *buildSetupPage();
    void startSession();
    void runDeferredStartup();
    bool m_deferredStartupStarted = false; // showEvent armed the triggers
    bool m_deferredStartupRun = false;     // runDeferredStartup already ran
    bool m_firstFramePainted = false;      // splash handover happened
    bool m_startupAuthResolved = false;
    bool m_framelessResizeCursorActive = false;
    int m_pendingRestoreRepoIndex = -1;    // last repo to reopen, or -1
    bool m_pendingSilentAuth = false;      // attempt auto-connect on first frame
    bool m_headless = false;               // no-GUI node (offscreen); see setHeadlessMode
    QTimer *m_headlessRegisterRetryTimer = nullptr;
    int m_headlessRegisterAttempt = 0;
    void scheduleHeadlessRegisterRetry(const QString &accountName);
    bool m_freshInstall = false;
    QString m_pendingAutoOpenRepoKey;
    bool ensureNodeAccount(const QString &accountName, const QString &solana);
    bool authenticateSilently(const QString &accountName);
    // Non-interactive equivalent of runSignupFlow for a headless mirror node: a
    // VM has no GUI to click "Join ForkMesh", so its auto-start path reserves +
    // finalizes its node name (free, key-bound, no dialog) here. Once the account
    // is key-bound the relay accepts this node's catalog writes and signed
    // direct-endpoint registration, so the mirror registers in the database
    // and shows on the repository page.
    // Returns true when the node ends up with an active, key-bound account.
    bool registerNodeAccountSilently(const QString &accountName);
    // In-app join: pick a username and you're in. Joining is free — the
    // username is reserved and activated against this device key (no donation,
    // no email/password); the user can attach more nodes to it later.
    // Cross-device credentials can be added later.
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
    // Log in by email (+ optional TOTP). accountName is only used as a fallback
    // node name if the server response omits one. If *fatal is non-null it is set
    // true when the failure is unrecoverable (the same credentials can never
    // succeed from this device, e.g. the account is bound to another key), so the
    // caller can stop re-prompting instead of looping the login dialog forever.
    bool verifyTotpLogin(const QString &email, const QString &password,
                         const QString &totp, const QString &accountName,
                         bool *fatal = nullptr);
    void activateAccountSession(const QJsonObject &payload,
                                const QString &fallbackAccount,
                                bool ownsDesktopKey,
                                const QString &instanceUrl);
    void cacheActiveAccountSession(const QString &instanceUrl);
    bool restoreCachedAccountSession(const QString &accountName,
                                     const QString &instanceUrl);
    QString currentAccountInstanceUrl() const;
    QString accountSessionTokenForUrl(const QUrl &url) const;
    QString activeAccountSessionToken() const;
    QUrl accountApiUrlForInstance(const QString &instanceUrl,
                                  const QString &leaf) const;
    void clearAccountSessionMemory();
    // Periodic signed heartbeat that keeps this node eligible for the reward
    // split and refreshes its payout Solana address.
    void sendNodeHeartbeat();
    QJsonObject emailNotificationPreferencesPayload() const;
    void showNodeClaimCode(const QString &user, const QString &code);
    // Admin node-ownership takeover: an admin viewing another
    // node's profile can request ownership; the heartbeat reply on the TARGET
    // node then carries the pending request, prompted here for that node's own
    // owner to approve or deny. requestNodeOwnership is the admin-side trigger.
    void requestNodeOwnership();
    void showOwnershipTransferPrompt(const QString &admin);
    void submitOwnershipTransferDecision(bool approve);
    // Installer link-code flow: the hosts/SSH installer printed a
    // link code on the fresh machine; confirm + submit it here, signed with
    // this account's key, so the new node is attached to this user.
    void promptHostLinkCode(const QString &code);
    void submitHostLinkCode(const QString &code);
    // "Log in as a user" from the node profile: this node holds its own key, so
    // proving the user's password lets the relay attach this node to that user
    // (users can own many nodes). promptLinkNodeToUser asks for the credentials;
    // submitLinkNodeToUser signs with this node's key and POSTs link-self.
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
    // Pending federated-instance join requests: a freshly launched
    // instance pings the main relay asking to join; for admins the pending
    // count rides the signed heartbeat reply, a red dot over the top-left
    // relay favicon flags it, and the Approve button beside it opens the link
    // dialog. Approving marks the relay approved upstream, which adds it to
    // /api/world/instances — the World then runs its firework show for the
    // newly joined instance.
    void setPendingRelayJoins(int count);
    void showRelayJoinApprovalDialog();
    bool adminRelayApprove(const QString &pubkey, const QString &action);
    void verifyWallet();
    QUrl accountsApiUrl(const QString &leaf) const;
    QJsonObject postAccountSync(const QString &leaf, const QJsonObject &body,
                                int *status);
    QJsonObject getAccountSync(const QString &leaf, int *status);
    QString accountOwner() const; // the registered account name (repo namespace)
    QString hostLinkUserName();
    QString hostLinkSigningAccountName(QString *userName = nullptr);
    void applyAccountEmailVerified(const QString &accountName, bool verified);
    bool accountEmailVerified(const QString &accountName) const;
    QString settingsAccountName() const;
    void refreshSettingsEmailVerifiedBadge();
    // The owner a repo is published/browsed under on the website. It must match
    // the owner used by the signed catalog, direct-HTTPS endpoint binding, and
    // minimal repository update channel.
    QString catalogOwner(const RepositoryRecord &repo) const;
    void runQuickUpdate();
    void updateRebuildRestart();
    void maybeAutoUpdate();
    bool tryPrebuiltAutoUpdate(const QString &clientDir, const QString &tag,
                               const QString &tagCommit);
    void installPrebuiltAndRelaunch(const QString &artifactPath,
                                    const QString &tag);
    void installMirrorCompanionThenRelaunch(const QString &artifactPath,
                                            const QString &tag,
                                            const QString &owner,
                                            const QString &name,
                                            const QString &companionHash);
    QString resolveInstallCloneUrl();
    void buildAndRelaunch(const QString &clientDir, const QString &asUser = QString(),
                          const QString &relaunchPath = QString(),
                          const QString &buildType = QStringLiteral("Release"));
    void buildMirrorNodeCompanion(const QString &clientDir, const QString &buildDir,
                                  std::function<void()> onDone);
    void installAndRelaunch(const QString &built, const QString &appPath);
    void runUpdateStep(const QString &program, const QStringList &arguments,
                       const QString &workingDir, std::function<void()> onSuccess,
                       std::function<void()> onFailure = {},
                       const QMap<QString, QString> &extraEnv = {});
    void runUpdateStepUser(const QString &program, const QStringList &arguments,
                           const QString &workingDir, std::function<void()> onSuccess,
                           std::function<void()> onFailure = {},
                           const QMap<QString, QString> &extraEnv = {});
    void setUpdateStatus(const QString &status, bool isError = false);
    void showUpdateLog();
    void appendUpdateLog(const QString &text);
    void setFooterUpdateLine(const QString &line);
    QString footerLogLineHtml(const QString &clean);
    void styleFooterUpdateLog();
    void openFullLogAtFooterLine(const QString &rawLine);
    void openFullLogForCategory(const QString &category);
    void persistProfile();

    QWidget *buildChatPage();
    void ensureSectionBuilt(int index);

    // Optional public payout-address notice; crypto never gates the core app.
    QWidget *buildSolanaNotice();
    void updateSolanaNotice();
    void promptSetSolanaAddress();
    void enablePaidMirroring();

    QWidget *buildBreadcrumb();
    void updateBreadcrumb();
    void showRelayMenu();          // searchable dropdown to switch/add relays
    void updateRelaySwitcher();    // refresh top-bar relay icon / domain / count
    void probeRelayLatency();      // measure round-trip to the active relay (radar)
    void refreshFooterWebsiteStatus();
    bool applyFooterWebsiteStatusPayload(const QJsonObject &payload);
    void applyFooterWebsiteStatusFailure(int httpStatus);
    void refreshDesktopWebsiteProbes();
    void probeDesktopWebsite(const QString &id);
    void applyDesktopWebsiteProbe(const QString &id, int httpStatus,
                                  const QByteArray &body,
                                  const QString &transportError);
    struct FooterStatusRow; // defined with the rest of the footer state below
    void alertOnDesktopEdgeOutage(const FooterStatusRow &row);
    void publishFooterWebsiteStatuses();
    // Clicking a dot opens the surface that dot is about: the site, /status,
    // the API host, the flagship repository page, or the admin error console
    // An invalid URL means only the admin console, which is
    // resolved separately because its path is a deployment secret.
    QUrl websiteStatusTargetUrl(const QString &statusId) const;
    void openWebsiteStatusTarget(const QString &statusId);
    void recheckWebsiteStatus(const QString &statusId);
    void openAdminErrorConsole();
    void onRelayLatencySampled(int ms);
    void initRelayReachabilityWatch(); // OS reachability → instant speed-dot flips
    void openServerWebsite(int index); // open a relay's site in the browser
    void showNodeMenu();           // searchable dropdown to pick a node
    void showNodesWindow();        // full window listing nodes, status, public wallet
    QString topBarUserName() const; // linked user/account name shown in the top bar
    QString nodeOwnerDisplayName() const; // user account that owns this node, if known
    QString chatDisplayName() const; // user identity used for chat sender names
    bool chatIdentityIsGuest() const;
    QString guestChatName() const;   // stable "Guest ####" for this install
    QString machineNodeName() const; // THIS machine's node name (never the username)
    void saveMachineNodeName(const QString &name); // persist + re-advertise
    void saveActionNodeLabels(const QString &labels);
    void updateChatIdentity();     // push user name/avatar into the chat backend
    void updateUserSwitcher();     // refresh top-bar user label/avatar
    QStringList recentUserAccounts() const;
    void rememberUserAccount(const QString &accountName);
    bool switchToUserAccount(const QString &accountName,
                             const QString &serverUrl = QString());
    void showUserAccountMenu();
    void openAccountManagement();
    void updateNodeSwitcher();     // refresh top-bar node label / count
    void updateNavSolanaBalance();
    void refreshWebUserSolanaAddress();
    void cacheWebUserSolanaProfile(const QString &account,
                                   const QJsonObject &profile);
    void renderNavSolanaBalance();
    void refreshNavSolanaBalance(bool force = false);
    void queryNavSolanaBalance(const QString &addr, int endpointIndex);
    void showRepoMenu();           // dropdown to open repos / add a local repo
    void updateRepoSwitcher();     // refresh top-bar repo label / count
    void updateReposNavBadge();    // rail badge = repos the Repos page lists
    void refreshRepoSyncIndicators();
    void refreshSourceControlOutgoing();
    void updateScmCommitControlVisibility();
    void pushCurrentRepoUpstream();
    // Launch the async `git push` for a repo whose secret scan has completed and
    // been approved (see pushCurrentRepoUpstream). The repo must already be marked
    // in m_pushingRepos.
    void startRepoPush(int index, const RepositoryRecord &repo,
                       const QString &upstream, int ahead);
    static QString mirrorStateHash(const QString &mirrorPath);
    // Signed catalog-list URL (adds our viewer token so the relay also returns our
    // own private repos). Shared by fetchCatalogRepos() and refreshRepoPinBanner().
    QUrl catalogListUrl();
    void refreshRepoPinBanner();
    void reattestStalePins();
    void resetRepoPin();
    bool relayPublishRepo(const RepositoryRecord &repo, QString *localBranch,
                          int *unpublished) const;
    void showChatView();           // open the chat view from the top-bar button
    void updateChatButton();       // refresh the top-bar chat unread indicator
    bool isChatViewVisible() const; // chat tab open + window active (i.e. being read)
    void clearActiveConversationUnread();
    QString mostRecentUnreadConversation() const;
    void updateChatUnreadBanner();
    void markAllChatRead();
    void updateConnectionStatus(); // top-right "● Connected · N nodes online"
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
    void checkFileDescriptorPressure();
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
    void stopHighMemoryAgent(int sessionId, const QString &label);
    QWidget *buildLogSection();
    // Resolve everything a Wrangler tail needs: the Worker bundle directory,
    // npx, the account, and the API token (from the settings field or the
    // stored deploy secret). Returns false after explaining why in a toast. The
    // token is returned to the caller so it can be scrubbed from memory once
    // the tail ends.
    bool prepareCloudflareTail(QString *token, QString *workerDirectory,
                               forkmesh::control::CloudflareBootstrapCommand
                                   *command);
    void setCloudLogMonitorEnabled(bool enabled);
    bool cloudLogMonitorTokenAvailable() const;
    QString cloudLogMonitorNodeRequirement() const;
    void startCloudLogMonitorIfConfigured();
    void stopCloudLogMonitorAfterFailure();
    void handleCloudLogMonitorEnded(int exitCode, qint64 uptimeMs);
    void scheduleCloudLogMonitorRestart(int delayMs);
    void cancelCloudLogMonitorRestart();
    void restartCloudLogMonitor();
    void releaseCloudLogMonitorProcess();
    void applyDebugBarStartupPreference();
    void readCloudLogMonitorOutput();
    void consumeCloudLogMonitorBytes(const QByteArray &chunk);
    void handleCloudLogMonitorLine(const QString &line);
    void updateCloudLogMonitorTooltip();
    bool cloudLogMonitorRunning() const;
    void showNetworkLogPopout();

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
    QWidget *buildNodeProfilePanel(); // builds inner scroll area; called by buildNodeProfileSection
    void showNodeProfile(const QString &nodeId, const QString &nodeName,
                         bool navigate = true);
    void hostNodeProfilePanel(bool inSettings);
    bool profilePanelInSettings() const
    {
        return m_nodeProfilePanel && m_settingsProfileHost &&
               m_nodeProfilePanel->parentWidget() == m_settingsProfileHost;
    }
    void syncSettingsProfileTab();
    void refreshProfileHostingStats(); // rebuild the per-repo hosting lines
    void refreshProfileAccountStatus(); // "USER ACCOUNT" section: link state + CTA
    void renderProfileAccountStatus();  // paint the section from cached state only
    QString linkedNodesHtml() const;    // "<b>a</b>, <b>b</b>" of m_profileLinkedNodes
    void fetchLinkedNodesFromOwner(const QString &node, const QString &owner);
    void rescaleProfileAvatar();       // re-render the full-width avatar banner
    void hideNodeProfile();
    void checkNodeBalance();
    void querySolanaBalance(const QString &addr, int endpointIndex);
    void selectNode(const QString &node);
    QWidget *buildIssuesSection();
    QWidget *buildOrganizationTasksSection();
    void refreshOrganizationTasks();
    QWidget *buildNotesSection();
    void refreshNotes();
    void renderNotesList(const QString &selectId = QString());
    void selectNote(const QString &id, const QString &storage);
    void createNote();
    void saveNote();
    void deleteNote();
    void updateNotePublishState();
    QJsonObject cloudNoteFor(const QJsonObject &note) const;
    void shareNote();
    void attachNoteConversation();
    void showNoteVersions();
    using NoteReplyHandler = std::function<void(
        bool, const QJsonObject &, const QString &)>;
    void requestNotes(const QByteArray &method, const QString &path,
                      const QJsonObject &body, NoteReplyHandler handler);
    void applyOrganizationTasks(const QJsonObject &payload);
    void renderOrganizationTaskRows(const QString &selectTaskId = QString());
    void setOrganizationTaskBadge(int openCount);
    void restoreOrganizationTaskBadge();
    void refreshOrganizationTaskBadge();
    void renderOrganizationTaskDetail();
    void updateOrganizationTaskActions();
    void createOrganizationTask();
    void createQuickAddOrganizationTask();
    void createOrganizationTaskFollowUp();
    void addOrganizationTaskToPrompt();
    // One click from the task detail to a running agent: the task is attached to
    // the footer prompt box and launched with that box's live settings
    // (repository, provider, model, permission mode, effort, attachments), and
    // the run it starts is bound back to this task instead of opening a second
    // one — so status and completion report against the task the operator was
    // looking at.
    void startOrganizationTaskAgentFromPrompt();
    QString localAgentRunLabelForTask(const QString &taskId) const;
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
    // Settings -> Security tab: private vulnerability reporting form.
    QWidget *buildVulnReportTab();
    void submitVulnerabilityReport();
    QWidget *buildMcpConnectorTab();
    void refreshMcpConnectorTab();
    void generateMcpConnector();
    void revokeMcpConnector();
    void testMcpConnector();
    QString mcpServerScriptPath() const;
    void startGenieAgent();
    // launchGenieRun() is the part that actually starts the tracked session, so
    // the credential mint below can call it once the relay answers.
    // requestGenieCredential() is the "we're already logged in" path
    // this install signs for its own task-only remote-MCP bearer with the
    // account key it already holds, saves it like the pasted one, and then
    // launches — no website round trip, no Settings detour.
    void launchGenieRun(int repoIndex, const QString &typedGuidance);
    void requestGenieCredential(int repoIndex, const QString &typedGuidance);
    QString genieSetupPrompt(const QString &extraInstruction) const;
    QUrl genieMcpUrl() const;
    void applyGenieTaskTitle(int sessionId, const QString &assistantText);
    void saveGenieSettings();
    // True while a credential mint is in flight, so a second press of "genie"
    // waits for the first instead of minting a second credential.
    bool m_genieCredentialPending = false;
    // Settings -> Quick Setup tab: provision a fresh instance in one pass —
    // identity, workflow credentials and world appearance applied together.
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

    // Local control node: one operational surface for this machine's mirrors,
    // sync/health/logs, local identity + wallet public address, repository
    // permissions, Cloudflare relay bootstrap, and remote host deployment.
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
    bool directMirrorGatewayConfigured() const;
    // The node name, mirror hostname, and Worker router public key the direct
    // gateway would be built from, returning whether all three are usable.
    bool resolveDirectMirrorGatewayIdentity(
        QString *nodeName, QString *hostname,
        QString *routerPublicKey) const;
    void startDirectMirrorServices();
    void stopDirectMirrorServices();
    bool rebuildManagedMirrorNodeConfiguration(QString *error = nullptr);
    bool startManagedMirrorNodeServer();
    void stopManagedMirrorNodeServer();
    void requestManagedMirrorNodeSync();
    void showMirrorNodeCompanion();
    void refreshMirrorNodeCompanion();
    void maybeAutoStartDirectMirrorServices();
    void ensureDirectMirrorRegistrationTimer(QObject *parent);
    void registerDirectMirrorEndpoint();
    void checkDirectMirrorGatewayHealth();
    void appendControlNodeOutput(const QString &text);
    QWidget *buildSiteDeployCard();
    void runSiteDeploy(const QString &target = QStringLiteral("changed"));
    void cancelSiteDeploy();
    void setSiteDeployRunning(bool running);
    void appendSiteDeployOutput(const QString &text);
    void connectToDeployedRelay(const QString &hostname);
    void deploySavedHostsFromControl();
    // API token tab: check a Cloudflare token against the exact
    // permissions the deploy path needs, and mint a correctly scoped
    // replacement into this device's variables and app/
    // env.production. Token values live in the password edit, the in-memory
    // redaction copy and those two stores only.
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
    // Browse a saved host's disk usage over its authenticated SSH channel: one
    // read-only `du` level per directory, so the biggest consumers of the
    // host's disk can be drilled into without leaving the app.
    void browseHostDiskUsageForSelection(int row);
    void runHostDiskUsageBrowser(const QString &ip, const QString &user,
                                 const QString &pass, const QString &node);
    // Configure a saved mirror host's Actions executor over its authenticated
    // SSH channel. Secret values are collected in a one-shot dialog and sent
    // only in a bounded JSON stdin payload; they are never saved in QSettings
    // or placed in process arguments/logs.
    void configureHostActionsForSelection(int row);
    // Install the official user-scoped Claude Code and Codex CLI binaries on
    // one selected mirror over its existing TOFU-pinned SSH connection.
    // Authentication is intentionally separate and never copied by this action.
    void installAgentClisForHost(int row);
    // Open a live in-app terminal on one mirror so the provider sign-ins an
    // install without copied credentials skips (`claude` then /login,
    // `codex login`) can be completed by hand. Opened automatically once such
    // an install finishes, and available on demand from the host row.
    void openHostAgentLoginTerminalForSelection(int row);
    void openHostAgentLoginTerminal(const QString &node, const QString &ip,
                                    const QString &user);
    // Shared driver behind that button and the one-click Vultr flow's "also
    // install the agent CLIs" option. With copyCredentials the
    // run additionally hands the mirror this device's provider logins on the
    // SSH session's stdin, so it comes up able to run agent sessions; without
    // it only the binaries are installed. onFinished(ok, message) reports the
    // outcome; the caller owns whatever it puts on screen.
    void runAgentCliInstall(const QString &node, const QString &ip,
                            const QString &user, const QString &sshPassword,
                            const QString &identityFile, bool copyCredentials,
                            std::function<void(bool, QString)> onFinished);
    // This device's copyable agent logins: the Claude Code and Codex CLI
    // credential files, plus the Settings API key for a provider with no CLI
    // login (a key alongside a login makes the CLI warn and switch billing).
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
    bool buildVultrMirrorNodeInstallCommand(const QString &node,
                                            QString *remoteCmd,
                                            QByteArray *uploadBytes,
                                            QString *errorOut) const;
    void addHostFromForm();
    void rememberHost(const QString &name, const QString &ip, const QString &user,
                      const QString &pass, const QString &status = QStringLiteral("installed"),
                      const QString &identityFile = QString(),
                      const QJsonObject &metadata = QJsonObject());
    QString savedHostIdentityFile(const QString &name, const QString &ip,
                                  const QString &user) const;
    void refreshHostsTable();
    void probeSavedHosts();
    void updateHostsFleetSummary();
    void probeSavedHost(const QString &name, const QString &ip,
                        const QString &user, const QString &savedStatus);
    void forgetHostAtRow(int row);
    void destroyVultrHostAtRow(int row);
    void sendVultrInstanceDestroy(const QString &apiKey,
                                  const QString &instanceId,
                                  const QString &name);
    void createVultrMirrorFromForm();
    // Six-stage durable deployment UI/state. The non-secret checkpoint is
    // written after every transition and restored when Hosts is rebuilt, so a
    // restarted desktop resumes boot polling, installation, or public mirror
    // verification instead of starting another billable instance.
    void setVultrProvisionStage(int stage, const QString &detail = QString(),
                                bool failed = false);
    void pingVultrProvisionStage(int stage, bool ok,
                                 const QString &detail = QString());
    void renderVultrProvisionProgress(bool failed = false);
    void recordVultrStageDetail(int stage, const QString &detail);
    void resetVultrStageDetails();
    void persistVultrProvisionState(const QString &state = QStringLiteral("active"),
                                    const QString &message = QString());
    void restoreVultrProvision();
    void resumeVultrProvision();
    void endVultrProvision();
    void updateVultrEndDeploymentButtonVisibility();
    QString vultrProvisionLogPath() const;
    void saveVultrProvisionLog();
    void scheduleVultrProvisionLogSave();
    void findVultrProvisionInstance(
        const QString &apiKey, const QString &node,
        std::function<void(QString instanceId, QString error)> onDone);
    // Persist a Vultr API key Vultr itself has just accepted, in the two places
    // this app reads provisioning credentials from: the canonical
    // VULTR_API_KEY device variable (Settings > Variables / Secrets, injected
    // into every action run) and app/.env.production beside the
    // Cloudflare deploy credentials. Returns where it was written — empty when
    // the key is unusable or already stored everywhere — and reports a failed
    // file write through *error without undoing the variable that succeeded

    QStringList rememberVultrApiKey(const QString &apiKey,
                                    QString *error = nullptr);
    void vultrApiCall(const QString &apiKey, const QString &path,
                      const QByteArray &method, const QJsonObject &body,
                      std::function<void(QJsonObject, QString)> onDone);
    // Generate — or adopt, on a device that already has one — the single SSH
    // key the whole host fleet authorizes. Every provisioning and deploy path
    // authenticates with it; the private half never leaves this device.
    void ensureSharedHostKeypair(
        std::function<void(QString privateKeyPath, QString publicKey,
                           QString error)> onDone);
    void resolveVultrSshKeyId(
        const QString &apiKey, const QString &publicKey,
        std::function<void(QString keyId, QString error)> onDone);
    void pollVultrInstance(const QString &apiKey, const QString &instanceId,
                           const QString &node, const QString &identityFile);
    void waitForVultrSshReady(const QString &node, const QString &ip,
                              const QString &identityFile);
    void startVultrHostInstall(const QString &node, const QString &ip,
                               const QString &identityFile);
    // Do not report a provisioned mirror as complete merely because SSH and
    // systemd succeeded. Wait until its signed flagship catalog row is visible
    // through the public relay—the same source used by Mirror nodes and World.
    void waitForVultrMirrorPublication(const QString &node,
                                       const QString &successMessage,
                                       int attempt = 0);
    void finishVultrProvision(bool ok, const QString &message);
    void reconcileDesiredMirrorFleet();
    void destroyDesiredMirrorFleetNode(const QString &node);
    void armMirrorFleetCountdown();
    void updateMirrorFleetCountdownLabel();
    void setMirrorFleetStatus(const QString &text);
    void appendVultrAttemptHistory();
    void cloudflareApiCall(const QString &apiToken, const QString &path,
                           const QByteArray &method, const QJsonObject &body,
                           std::function<void(QJsonObject, QString)> onDone);
    // Point "<node>.<zone>" at a freshly booted Vultr instance so it joins the
    // mesh under a stable name like the other mirrors. Reports the hostname it
    // provisioned, or an empty string when no Cloudflare credentials/zone are
    // configured or the record could not be written — provisioning continues
    // either way, the node just keeps its raw address.
    void ensureVultrMirrorDns(const QString &node, const QString &ip,
                              std::function<void(QString hostname)> onDone);
    void removeVultrMirrorDns(const QString &node,
                              std::function<void(QString outcome)> onDone);
    void loadHostIntoForm(int row, int column);
    QString installScriptUrl() const;
    QString uninstallScriptUrl() const;

    QWidget *buildRelaysSection();
    void refreshRelaysTable();   // re-list relays and (re)probe each one
    void probeRelayRow(int row); // measure latency + read version for one relay
    QWidget *buildNodesSection();
    void refreshNodesTable();           // re-list the known nodes into the table
    void showNodeDetailForRow(int row); // fill the detail panel for a table row
    void refreshNodeActionButtons();
    // --- Delete a node for good ----------------------------------
    // The Nodes page's Delete button: the same permanent removal the World
    // panel performs, plus the provider teardown the web has no credentials
    // for. In order — destroy the Vultr instance behind the node, drop its
    // Cloudflare DNS record, remove every trace of it from the relay (accounts,
    // mirrors, agent jobs and the /status history), then forget the saved SSH
    // host locally. The provider steps are best-effort and never stop the mesh
    // removal; the mesh removal itself is the one step that must succeed.
    void deleteMeshNodeCompletely(const QString &node, const QString &nodeId);
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
    void forgetSavedHostNamed(const QString &node);
    void setNodeDeleteStatus(const QString &text);
    // Fetch the relay's list of currently-online node names (/api/network/stats
    // "onlineNodes": repository update channel or fresh signed heartbeat). Headless
    // mirror nodes serve through the relay without joining this client's chat
    // room, so room presence alone painted them offline.
    void fetchRelayOnlineNodes(bool force = false);
    void fetchNodesCatalogInfo(bool force = false);
    void applyCatalogNodeInfo(MemberInfo &info, const QString &node) const;
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
    // Network diagnostics: live websocket / Durable Object details, plus the
    // Relays / Nodes / Hosts pages as its first tabs.
    QWidget *buildNetworkDiagnosticsSection();
    void refreshNetworkDiagnostics();
    void updateNetworkCounts(int relays, int nodes, int hosts);
    void showNetworkTab(int tabIndex);
    void refreshNetworkTab(int tabIndex);
    void showEndpointRequestDetails(int row, int column);
    void refreshNetworkWebRequests(bool isRetry = false);
    void renderNetworkWebRequests(const QJsonObject &payload);
    void showNetworkWebRequestsFailure(const QString &detail, bool willRetry,
                                       qint64 retryInMs);

    QWidget *buildUsersSection();
    void refreshUsersPage(bool force = false);
    void renderUsersPage(const QJsonArray &users);

    QWidget *buildFilesSection();
    QLayout *buildFilesEditorBar();
    void refreshFilesPage();
    void showFilesRepoTree();
    void showFilesDiskTree();
    void openLocalFileTab(const QString &path);
    void commitFilesTabChanges();
    void updateFilesCommitActions();
    void clearRepoFileTabs();
    void setFileExplorerRoot(const QString &path);
    void populateFileExplorerItem(QTreeWidgetItem *item);
    QTreeWidgetItem *addFileExplorerRow(QTreeWidgetItem *parent,
                                        const QFileInfo &entry);
    QFileInfoList fileExplorerEntries(const QString &dir) const;
    void showFileExplorerMenu(const QPoint &pos);
    void previewFileExplorerFile(const QString &path, int line = 0);
    void revealFileInExplorer(const QString &path, int line = 0);
    void revealLogSourceInExplorer(const QString &relativePath, int line);
    void highlightFileExplorerPreviewLine(int line);

    void ensureRepoDetailSectionBuilt();
    void ensureRepoDetailTabBuilt(int index);
    QWidget *buildRepoDetailSection();
    QWidget *buildRepoFilesPanel();
    QWidget *buildRepoOverviewPage();
    QWidget *buildRepoCoveExplorerPage();
    QWidget *buildRepoCommitsTab();
    void showCommit(const QString &hash); // open the commit diff detail view
    void renderCommitDetail(const QString &dir, const QString &hash,
                            const QStringList &metaFields,
                            const QByteArray &patchRaw);
    void showCommitList();                // reset the right pane to working changes
    void openMostRecentCommit();          // select newest commit + expand its diff
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
    void rebuildGlobalSearchList(const QString &query);
    void activateGlobalSearchHit(const GlobalSearchHit &hit);
    QDialog *m_searchDialog = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QListWidget *m_searchList = nullptr;
    QLabel *m_searchStatus = nullptr;
    int m_searchGen = 0;
    QVector<GlobalSearchHit> m_searchHits;
    int m_searchWavesPending = 0;
    void openBodyReference(const QString &href);
    void openAgentTranscriptReference(const QString &href);
    void copyReferenceLink(const QString &kind, const QString &id);
    void filterCommits(const QString &query);
    void downloadCommitPatch();           // save the open commit as a .patch file
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
    void showSizeMapScanProgress(
        const QString &root,
        const forkmesh::DirectorySizeScanProgressUpdate &update, bool elevated);
    void clearSizeMapWorkerLines();
    void applySizeMapResult(const QString &path,
                            forkmesh::DirectorySizeScanResult result,
                            bool hideIgnored, bool elevated);
    QWidget *buildPlaceholderTab(const QString &name);

    // Discussions tab (signed repository discussions with inbox fallback).
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
    void setDiscussionStatus(const QString &status);
    void deleteCurrentDiscussion();
    void startDiscussionFromComposer();
    static QString discussionTitleFromBody(const QString &body);
    void submitDiscussionEventToInbox(int number, const DiscussionEvent &ev,
                                      const QString &titleIfNew = QString());
    QUrl discussionsApiUrl(const RepositoryRecord &repo) const;
    void syncDiscussionsInbox();
    void drainDiscussionsInboxFor(RepositoryRecord repo, bool interactive,
                                  bool forceMirrorIntake = false);
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
    void refreshPullList();
    void showPull(int number);
    void applyPullFileAuthorFilter();
    void renderPullDiff();
    void scrollPullDiffToFile(const QString &filePath);
    void updatePullDiffScrollState();
    void selectPullFileInList(const QString &filePath);
    void layoutPullStickyHeader();
    void computePullFileTops();
    void applyAutoMarkViewedOnScroll();
    void adjustDiffFont(int delta);
    void registerDiffView(QTextEdit *view);
    void setDiffHtml(QTextEdit *view, const QString &html, bool endCap = true);
    void onDiffStreamFinished(QTextEdit *view);
    void pullSelectAdjacentChange(int delta);
    bool pullScrollToAdjacentHunk(int delta);
    void togglePullDiffSearch(bool show);
    void pullDiffSearchRecompute();
    void pullDiffSearchGoTo(int delta);
    void onPullDiffAnchorClicked(const QUrl &url);
    void submitPullThreadReply(const QString &threadId);
    void setPullThreadState(const QString &threadId, const QString &state);
    void renderPullThread(const PullRequest &pr);   // review/comment conversation
    void renderPullCommits(PullRequest pr);         // commits that make up the PR
    QList<PullActivityCommit> m_pullActivityCommits;
    int m_pullActivityCommitsNumber = 0; // PR the list above was collected for
    void refreshPullAgentActivity();
    QString pullAgentActivityDigest(const PullRequest &pr) const;
    QString m_pullAgentActivityDigest;
    // Commit and agent cards renderPullThread() added on top of the signed review
    // items, and the review-item count updatePullSubTabCounts() derived. Held so
    // the Conversation badge can be corrected after a live agent re-render
    // without repeating that function's git-backed tallies.
    int m_pullActivityExtraCards = 0;
    int m_pullActivityCardsNumber = 0; // PR the count above belongs to
    int m_pullConversationBaseCount = 0;
    bool m_pullActivityRefreshing = false;
    void renderPullChecks(PullRequest pr);          // action runs for the PR's commits
    void renderPullChecksSummary(PullRequest pr);   // inline conversation card
    void showPullCheckLog(int runId);               // load a run's log into the panel
    QStringList pullCommitShas(const PullRequest &pr) const; // base..head SHAs
    QList<int> runIdsForPull(PullRequest pr) const; // matching action runs
    // Resolve the signed historical head when it still exists, otherwise use
    // the durable canonical pr/<n> branch materialized for this PR. Takes a
    // value because the git probes pump the GUI event loop.
    QString resolvablePullHead(PullRequest pr) const;
    struct PullRangeSnapshot {
        QString baseSha;
        QString headSha;
        QByteArray detailedLog; // renderPullCommits' rich per-commit format
        QStringList shas;       // %H list for pullCommitShas/runIdsForPull
    };
    bool pullRangeSnapshot(const QString &base, const QString &head,
                           PullRangeSnapshot *out) const;
    mutable QHash<QString, PullRangeSnapshot> m_pullRangeCache;
    void runChecksForCurrentPull();                 // enqueue workflows at PR head
    void buildAndPreviewCurrentPull();
    void updatePullSubTabCounts(PullRequest pr);
    void refreshOpenPullChecks();                   // re-render checks for the open PR
    enum class WorkflowTrigger { Push, Release };
    void queueWorkflowsForCommit(int repoIndex, const QString &owner,
                                 const QString &name, const QString &commit,
                                 const QString &ref,
                                 WorkflowTrigger trigger = WorkflowTrigger::Push);
    bool resolveActionApproval(const RepositoryRecord &repo,
                               const ActionRun &run);
    int autoApproveAwaitingRuns(const RepositoryRecord &repo);
    struct WorkflowScan {
        bool mirrorMissing = false;
        bool metadataOnly = false;
        QList<QPair<QString, QString>> workflows; // .forkmesh/<file> -> content
    };
    void applyWorkflowScan(const QString &owner, const QString &name,
                           const QString &commit, const QString &ref,
                           WorkflowTrigger trigger, const WorkflowScan &scan);
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
    void openPullDiffInGitView(int pullNumber);
    QHash<QString, QString> buildPullLineNotes(const PullRequest &pr);
    void updateCurrentPullBranch();
    void mergeCurrentPull();
    void afterPullMerged(PullRequest pr);
    void resolveCurrentPullConflicts(); // open the per-conflict merge editor
    bool runMergeConflictEditor(const QString &title, const QString &introHtml,
                                const QString &workTree,
                                const QStringList &conflictedFiles,
                                const QString &commitButtonText,
                                const std::function<bool(QString *)> &commitFn);
    void fixCurrentPullConflictsWithAi(const QString &provider);
    void fillPromptWithPullConflictFix();
    void fixCurrentPullConflictsWithOriginatingAgent();
    void editCurrentPullFile();         // edit the selected file on the PR's branch
    void deleteCurrentPullFile();       // delete the selected file on the PR's branch
    void closeIssuesLinkedFromPull(const PullRequest &pr);
    QList<int> closeIssuesForMerge(const QList<int> &numbers,
                                   const QString &comment, const QString &via);
    void fundBountiesForMergedPull(const PullRequest &pr);
    void autoBountyForMergedPull(const PullRequest &pr);
    void pollBountyPayout(const RepositoryRecord &repo, int number, double amount,
                          const QString &kind = QString());
    QList<int> issuesLinkedFromPull(const PullRequest &pr) const;
    QList<int> pullsLinkedToIssue(int issueNumber) const;
    void linkPullToIssueFromIssuePage(); // issue page: pick a PR to link
    void linkIssueToPullFromPullPage();  // PR page: pick an issue to link
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
    // Submit a signed PR conversation event (comment/review) to the relay inbox
    // for repos this node can't write directly.
    void submitPullEventToInbox(int number, const PullEvent &ev);
    void updatePullActionState();
    QUrl pullsApiUrl(const RepositoryRecord &repo) const;

    QWidget *buildMergeQueuePanel();   // the editable queue in the Pulls pane
    void refreshMergeQueuePanel();     // repaint it from the open repo's queue
    bool mergeQueueEnabledForOpenRepo() const;
    void setRepoMergeQueueEnabled(bool on);   // repository Settings toggle
    void setMergeQueuePaused(bool paused);
    void addPullToMergeQueue(int number);
    void removePullFromMergeQueue(int number);
    void toggleCurrentPullInMergeQueue(); // the PR header's "Queue" tile
    void moveMergeQueueSelection(int delta);
    void removeMergeQueueSelection();
    void clearMergeQueue();
    void queueBranchForMerge(const QString &branch);
    void scheduleMergeQueueRun(int delayMs);
    void processMergeQueue();
    MergeQueue openRepoMergeQueue() const;
    MergeQueue mergeQueueFor(const QString &owner, const QString &name) const;
    void saveMergeQueue(const QString &owner, const QString &name,
                        const MergeQueue &queue);
    void setMergeQueueEntryState(const QString &owner, const QString &name,
                                 int number, const QString &state,
                                 const QString &detail);
    void dropMergeQueueEntry(const QString &owner, const QString &name,
                             int number);
    QWidget *buildAgentsTab();
    void initAgents();
    void reloadAgents();
    void refreshAgentTable();
    void applyAgentRowCells(int row, const AgentSession &session,
                            const QString &agentGitDir, const QString &agentBase);
    QPixmap agentSessionPullAvatar(const AgentSession &session);
    AgentDiffStat agentDiffStat(const AgentSession &session, const QString &gitDir,
                                const QString &base);
    void updateAgentTokenCell(int sessionId);  // live Tokens-column update
    void updateAgentCostCell(int sessionId);   // in-place Cost-column update
    void updateAgentRunSummaryCells(int sessionId); // in-place Turns/Time update
    void updateAgentStatusCell(int sessionId); // in-place Status-column update
    void applyAgentDiffStatResult(int generation, int repoIndex, int sessionId,
                                  const AgentDiffStat &stat,
                                  const QString &signature);
    void refreshAgentStatusPill(int sessionId); // in-place detail-header pill update
    void showAgentMetaPopup(); // open the detail metadata from hover or click
    void toggleAgentMetaPopup(); // click handler for the detail metadata popup
    void hideAgentMetaPopupIfPointerAway(); // preserve the popup while entering it
    void animateRunningAgentIcons();           // spins running rows' Status glyph
    void noteAgentActivity(int sessionId, int bytes = 0);
    void onScannerTick();
    void showAgentSession(int sessionId);
    void syncQuickAddControlsToAgentSession(const AgentSession &session);
    void refreshAgentDetailMeta(int sessionId);
    qint64 agentSessionProcessId(int sessionId) const;
    void completeAgentSessionWhenSubprocessesExit(int sessionId);
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
    bool m_agentNetScanInFlight = false; // one live [net] rescan at a time
    void applyAgentNetworkPanel(const AgentLogScan &scan, const QString &status);
    AgentSession *findAgentSession(int sessionId);
    const AgentSession *latestAgentSessionForIssue(int issueNumber) const;
    const AgentSession *agentSessionForPull(int prNumber,
                                            const QString &headBranch = QString()) const;
    bool bindAgentSessionsToPull(int prNumber, const QString &headBranch);
    bool markAgentSessionsMerged(int prNumber, const QString &branch,
                                 bool mergeVerified);
    void refreshAgentMergeState();
    void updateAgentMergeCandidates(const QHash<int, QString> &heads);
    void markAgentSessionsLanded(const QList<int> &sessionIds, bool refreshUi);
    void assignIssueToAgent(const QString &provider, const QString &model = QString());
    int startAgentForIssue(const Issue &issue, const QString &provider, bool createPr,
                           bool quiet = false, const QString &model = QString(),
                           const RepositoryRecord *repoHint = nullptr);
    void toggleIssueLooper();
    void looperStartNext();
    void looperOnSessionFinished(int sessionId);
    // stamp this node onto an issue's assignees the moment the looper
    // takes it, so the claim syncs to every node and no second looper (here or on
    // another mirror) starts the same task. The host appends the node and commits
    // (which syncs to mirrors); a mirror with no write access files the signed
    // assignees event to the owner's inbox, which merges and syncs it back.
    void looperClaimIssue(int number, const QStringList &existingAssignees);
    QString nodeAssigneeTag() const;
    void updateIssueLooperButton();
    void persistLooperState();
    void maybeRestoreIssueLooper();
    void continueSelectedAgentSession();
    void continueAgentSession(int sessionId, bool deferRefresh = false);
    void fixAgentConflictsWithAgent(int sessionId);
    bool agentSessionHasLiveTransport(int sessionId) const;
    void applyComposerSelectionToAgentSession(int sessionId);
    void sendPromptToSelectedAgent(const QString &prompt);

    void sendPromptToAgentSession(int sessionId, const QString &prompt);
    bool deliverPromptToLiveAgentTransport(int sessionId, const QString &prompt);
    void queueAgentSteerMessage(int sessionId, const QString &prompt);
    bool discardWedgedAgentTransport(int sessionId);
    void noteAgentSessionNotice(int sessionId, const QString &text,
                                bool error = false);
    void failQueuedAgentSession(AgentSession &session, const QString &reason);
    QString issueContextPrompt(const Issue &issue) const;
    void sendIssueContextToSelectedAgent();
    void deleteSelectedAgentSession();
    void deleteAgentSessionEntry(int sessionId);
    void createLinkedIssueForSelectedSession();
    bool deleteStoredAgentSession(int sessionId, bool cleanupWorktree = true,
                                  bool allowAssociationOnly = false);
    void deleteWorktreeBranchAndAgentInBackground(
        const QString &worktreePath, const QString &branch);
    void deleteWorktreeBranchAndAgent(const QString &worktreePath,
                                      const QString &branch, bool confirm = true,
                                      bool async = true, bool deferRefresh = false);
    void deleteAllMergedAgentSessions();
    void testOpenAiAgentKey();
    void refreshClaudeSpend();
    void refreshClaudeCodeUsage(bool fromHover = false);
    void applyClaudeUsageResponse(const QJsonObject &response);
    void showAgentAccountMenu(const QString &provider,
                              const QPoint &globalPosition);
    void probeClaudeAgentAccountIdentity(const QString &configDir,
                                         QAction *accountAction,
                                         const QString &baseLabel);
    void selectAgentAccount(const QString &provider, const QString &accountId);
    void addAgentAccount(const QString &provider);
    void editAgentAccount(const QString &provider, const QString &accountId);
    bool renameAgentAccount(const QString &provider, const QString &accountId,
                            const QString &label);
    void refreshAgentAccountUsageMenu(const QString &provider);
    void launchAgentSystemTerminal(const QString &provider,
                                   const QString &mode = QStringLiteral("agent"));
    bool launchSystemTerminal(const QString &program, const QStringList &commandArgs,
                              const QString &cwd,
                              const QMap<QString, QString> &extraEnv = {});
    QStringList agentAccountUsageLines(const QString &provider,
                                       const QString &accountId) const;
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
    void setAgentConcurrencyLimit(int limit);
    void refreshAgentQueueControls();
    int runningAgentCount() const;
    QList<int> stoppableAgentSessionIds() const;
    void stopAllRunningAgents();
    bool stopAgentSessionById(int sessionId);
    bool isStoppableAgentSession(int sessionId) const;
    bool agentSessionWorkInFlight(int sessionId) const;
    qint64 agentSessionLastLiveMs(int sessionId) const;
    static constexpr qint64 kAgentSilentStaleMs = 90'000;
    QList<int> startableAgentSessionIds() const;
    void startAllStoppedAgents();
    QList<int> updatableAgentSessionIds() const;
    void updateAllAgentWorktreesFromMain();
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
    void updateQuickAddTargetAgentLabel();
    void updateIssueAgentUi(const Issue &issue);
    void refreshIssueFilesPanel(const Issue &issue);
    void renderIssueDiff(int issueNumber, const QByteArray &patch,
                         const QString &dir, const QString &base);
    void populateIssueFilesCell(int row, const Issue &issue);
    QIcon issueAssigneeAvatar(const QString &name);
    QHash<QString, QIcon> m_assigneeAvatarCache; // assignee name -> avatar icon
    bool ideExtensionActive(QString *ideName = nullptr) const;
    bool ideIntegrationReady(QString *ideName = nullptr) const;
    void startIssueInIde(int issueNumber, const QString &title,
                         const QString &provider);
    void updateIssueIdeButtons();
    AgentRunner::Config agentConfigForProvider(const QString &provider);
    void startClaudeCodeTerminal(AgentSession &session, const Issue &issue,
                                 const QString &repoPath);
    QString agentProviderName(const QString &provider) const;
    QWidget *buildRepoActionsTab();
    void refreshRepoActions();           // workflows column + runs for the open repo
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
    void initActions();                  // store/runner/watcher, load history, hooks
    void ensurePushHook(const RepositoryRecord &repo) const;
    // Working-copy post-commit/post-merge hooks: spool a *.commit event the
    // instant a commit lands on the source of truth (terminal/IDE/agent), so
    // scanActionSpool can sync the mirror and signal peers over the websocket
    // immediately instead of at the next heartbeat.
    void ensureCommitSignalHook(const RepositoryRecord &repo) const;
    void removePushHook(const RepositoryRecord &repo) const;
    void installAllPushHooks() const;
    void scanActionSpool();              // read *.push/*.commit events, enqueue runs
    bool m_actionSpoolSweepPending = false;
    void scanServedMirrorHeads(const QHash<int, QString> &pushedCommits);
    void syncMirrorActionsConfiguration();
    void scanExternalActionsSources();
    void updateMirrorActionsRuntimeState();
    // Atomically publish a bounded, redacted Actions run summary for the
    // gateway. Live log lines are coalesced; lifecycle changes publish on the
    // next event-loop turn and the lease is refreshed periodically.
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
    // An encrypted repository is served out of a temporary materialization whose
    // directory is recreated by every sealing pass and deleted as soon as the
    // replacement is installed. A run checks out of, clones from, and lands
    // release artifacts into that directory for its whole lifetime, so a routine
    // re-seal (publishing a release triggers one) can delete the mirror out from
    // under an in-flight build: `git -C <mirror> worktree add` then fails with
    // "cannot change to '/tmp/ForkMesh-XXXXXX/repository.git'".
    // Returns the live materialization for this repository — writing its current
    // path into `mirrorPath` when the record lagged behind a re-seal — and keeps
    // that directory alive for as long as the caller holds the returned handle.
    // Null for a plain durable mirror, which needs no pinning.
    std::shared_ptr<void> pinActionMirror(const RepositoryRecord &repo,
                                          QString *mirrorPath) const;
    void releaseActionMirrorPin(int runId);
    ActionRunner *runnerForRun(int runId) const;
    void cancelSupersededRuns(const ActionRun &newRun);
    void onRunLog(int runId, const QString &text);
    void notifyActionEvent(const QString &title, const QString &body,
                           bool warning, int runId = -1); // tray alert gated by the run-alert setting
    struct AppNotification;
    void addNotification(const QString &title, const QString &body,
                         bool warning = false, int runId = -1);
    void addNotification(const QString &title, const QString &body, bool warning,
                         const NotificationLink &link);
    void addNotification(const QString &title, const QString &body, bool warning,
                         const NotificationLink &link, const QString &kind,
                         const QString &actor);
    qint64 recordNotification(AppNotification item);
    forkmesh::PingSync initialPingSync(const AppNotification &item,
                                       QString *reasonOut) const;
    void trimLocalPings();
    void setPingSync(qint64 pingId, forkmesh::PingSync state,
                     const QString &reason = QString());
    QString notificationJournalPath() const;
    void loadNotificationJournal();
    void saveNotificationJournal();
    void scheduleNotificationJournalSave();
    void flashNotification(const AppNotification &item);
    QPixmap pingActorAvatar(const AppNotification &item) const;
    QPixmap chatActorAvatar(const QString &senderId, const QString &senderName,
                            int side) const;
    void flashErrorBorder();
    void flashCelebrationBorder();
    void startRestartCautionFlash();
    void stopRestartCautionFlash();
    void openNotificationLink(const NotificationLink &link);
    void showNotifications();
    void refreshNotificationsTable();
    void deleteSelectedNotifications();
    void deleteWebAlert(const QString &alertId);
    void openNotificationRow(int row);
    static QString notificationLinkLabel(const NotificationLink &link);
    void updateNotificationButton();
    void refreshWebAlerts(bool force = false);
    void clearWebAlerts();
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
    void updateActionsSpinTimer();
    void animateRunningActionIcons();
    void showLatestVisibleActionRun();
    void showRun(int runId);
    void approveSelectedRun();
    void rejectSelectedRun();
    ActionRun *findRun(int runId);
    ActionNeeds::State actionRunNeedsState(int runId, QString *detail);
    void noteActionRunWaiting(int runId, const QString &detail);
    int repoIndexFor(const QString &owner, const QString &name) const;
    // Settings: global variables/secrets editor.
    void reloadVariablesList();
    void addOrEditVariable(const QString &name = QString());
    void deleteVariable(const QString &name);
    void exportVariables();
    void importVariables();
    void toggleVariablesRevealed();
    void persistVariablesFromTable();

    void openRepoDetail(int repoIndex);
    bool bindRepoDetailToRepo(int repoIndex);
    void openRepoDetailDeferred(int repoIndex);
    void clearRepoDetail();
    void openRepositoryWebsite(); // open the current repo's page in the browser
    void forkCurrentRepo();       // clone the open repo into your own node
    bool configureForkSource(int index, const QString &sourceUrl,
                             bool notifyOnError = true);
    void downloadCurrentRepoZip();
    void setRepoDetailNotice(const QString &message, bool error = false);
    void refreshOpenRepoDetail(); // re-read the open repo after its mirror changes
    void scheduleOpenRepoDetailRefresh();
    void updateRepoCodeSize();
    void updateRepoIssueCount();
    void updateRepoDiscussionCount();
    void updateRepoPullCount();
    void refreshRepoTabCounts();
    void loadRepoOverview(const QString &path);
    void showRepoOverview();
    void showRepoCoveExplorer();
    void showOverviewCommits();
    void showOverviewFiles();
    void showOverviewBranches();
    void showOverviewWorktrees();
    void updateRepoActivityRail();
    void setGitPromptOverlay(bool enabled);
    void positionGitPromptOverlay();
    void positionGlobalFooterOverlays();
    void setPromptOverlayCollapsed(bool collapsed);
    void setPromptOverlayDetached(bool detached);
    void resetPromptOverlayPlacement();
    void loadPromptOverlayPlacement();
    void savePromptOverlayPlacement();
    bool handlePromptPlacementEvent(QObject *object, QEvent *event);
    void clampPromptOverlayIntoHost();
    void setLogOverlayExpanded(bool expanded);
    void setDebugLogTailVisible(bool visible);
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
    void openWorkingTreeFile(const QString &dir, const QString &relPath);
    bool saveWorkingTreeFileEdit(const QString &absPath, const QString &content);
    void openRepoReadme(); // open the repo's README in a file tab (default view)
    void updateRepoFileSaveActions();
    void saveCurrentRepoFile(bool createPull);
    void toggleRepoFileMarkdownPreview();
    void showRepoFileHistory(const QString &path);
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
    struct BranchesTagsSnapshot {
        QString dir;               // repoGitDir()
        QString localPath;         // working copy for the worktree count
        QString configuredDefault; // m_repoInfo.defaultBranch
        QString checkedOut;        // m_repoBranch at issue time
        QString base;          // default branch (chooseDefaultBranch)
        QString head;          // checked-out branch (empty = detached/none)
        QStringList branches;  // local heads, committerdate-sorted
        int worktreeCount = 0; // linked worktrees, minus forkmesh/pulls
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
        QString configuredDefault; // m_repoInfo.defaultBranch, for the base pick
        QString checkedOut;        // m_repoBranch
        QString base;              // default branch (picked by the worker)
        QString selected;          // branch the repo view is parked on
        QString previouslyViewed; // branch whose diff was on screen
        bool writable = false;
        QStringList branches;       // local heads, default branch first
        QStringList remoteBranches; // refs/remotes/* (read-only rows)
        QHash<QString, qint64> times;
        QHash<QString, QString> shortShas;
        QHash<QString, QString> subjects;
        QHash<QString, QString> authors;
        QHash<QString, QPair<int, int>> localAheadBehind;
        QHash<QString, QPair<int, int>> remoteAheadBehind;
        QHash<QString, QString> worktrees; // branch -> linked worktree path
    };
    struct BranchChangeStat {
        int files = -1;
        int added = -1;
        int removed = -1;
    };
    struct BranchDiffCacheEntry {
        QByteArray patch;
        QString emptyMessage;
    };
    static constexpr int kBranchesDeleteColumn = 0;
    static constexpr int kBranchesNameColumn = 1;
    static constexpr int kBranchesStatusColumn = 2;
    static constexpr int kBranchesUpdatedColumn = 3;
    static constexpr int kBranchesWorktreeColumn = 4;
    static BranchesPanelData readBranchesPanelGit(BranchesPanelData data);
    void renderBranchesPanel(const BranchesPanelData &data);
    void startBranchChangeStats(const BranchesPanelData &data);
    void loadBranchesPanel();
    QWidget *buildWorktreesTab();
    void loadWorktreesPanel();

    enum UiRefreshKind : unsigned {
        UiRefreshSourceControl = 1u << 0,
        UiRefreshSourceControlForce = 1u << 1,
        UiRefreshWorktrees = 1u << 2,
        UiRefreshBranches = 1u << 3,
        UiRefreshAgents = 1u << 4,
        UiRefreshIssues = 1u << 5,
    };
    int m_uiRefreshBatchDepth = 0;
    unsigned m_uiRefreshPending = 0;
    bool deferUiRefresh(unsigned kind);
    void runPendingUiRefreshes();
    class UiRefreshBatch {
    public:
        explicit UiRefreshBatch(MainWindow *window) : m_window(window)
        {
            if (m_window)
                ++m_window->m_uiRefreshBatchDepth;
        }
        ~UiRefreshBatch()
        {
            if (!m_window)
                return;
            if (--m_window->m_uiRefreshBatchDepth == 0)
                m_window->runPendingUiRefreshes();
        }
        UiRefreshBatch(const UiRefreshBatch &) = delete;
        UiRefreshBatch &operator=(const UiRefreshBatch &) = delete;

    private:
        MainWindow *m_window = nullptr;
    };
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
                    [this, apply = std::move(apply),
                     result = std::move(result)]() mutable {
                        deliverOffThreadResult(std::move(apply),
                                               std::move(result));
                    },
                    Qt::QueuedConnection);
            });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    }
    template <typename T>
    void deliverOffThreadResult(std::function<void(T)> apply, T result)
    {
        if (forkmesh::ui::inKeepAlivePump()) {
            QTimer::singleShot(
                0, this,
                [this, apply = std::move(apply),
                 result = std::move(result)]() mutable {
                    deliverOffThreadResult(std::move(apply), std::move(result));
                });
            return;
        }
        apply(std::move(result));
    }
    bool deferredOutOfKeepAlivePump(bool &pending, std::function<void()> work)
    {
        if (!forkmesh::ui::inKeepAlivePump())
            return false;
        if (pending)
            return true; // already queued for the next clean turn
        pending = true;
        QTimer::singleShot(0, this,
                           [&pending, work = std::move(work)]() mutable {
                               pending = false;
                               work();
                           });
        return true;
    }
    int m_worktreeStatusGen = 0;
    void switchToWorktree(const QString &branch);
    void switchToBranch(const QString &branch, int agentSessionId = -1);
    QString m_branchCompareBase;
    QString branchCompareBase() const;
    void setBranchCompareBase(const QString &base);
    void closeBranchCompareView();
    bool selectWorktreeRow(const QString &branch);
    bool selectBranchRow(const QString &branch);
    void reportBranchNotFound(const QString &branch);
    void updateWorktreeSelection(const QString &branch,
                                 const QString &worktreePath);
    bool mergeWorktreeIntoMain(const QString &branch,
                               const QString &worktreePath = QString(),
                               bool deleteAgent = false);
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
    void showBranchDiff(const QString &branch, int agentSessionId = -1);
    void showBranchDiffBranchGone(QString branch, QString base);
    void applyBranchDetailActions(const QString &branch, const QString &base,
                                  int behind, int ahead, bool hasConflict,
                                  bool worktreeConflict = false);
    int m_branchDetailActionsGen = 0;
    void renderBranchScopeDiff();
    QString branchWorkDir(const QString &branch) const;
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
    int createPullFromBranch(const QString &branch);
    void onBranchDiffAnchorClicked(const QUrl &url);
    void updateBranchDiffSticky();
    QString diffViewedScope(const QString &context) const;
    QSet<QString> loadDiffViewed(const QString &context) const;
    void setDiffViewed(const QString &context, const QString &path, bool viewed);
    struct WorktreeMergeReport
    {
        enum Status {
            Merged,     // base is in; any local edits were restored on top
            Busy,       // a merge or unresolved files were already in the way
            Conflicted, // refused or rolled back; the branch is untouched
            Failed,     // couldn't be attempted at all
        };
        Status status = Failed;
        QString message; // ready to show, naming the branch and the base
    };
    WorktreeMergeReport mergeBaseIntoLinkedWorktree(const QString &worktree,
                                                    const QString &branch,
                                                    const QString &base);
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
    QString mergedBranchCelebrationHtml(const QString &branch, const QString &base,
                                        const QString &dir);
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
    // Push a just-published release to the repo's fediverse followers via the
    // relay's owner-signed POST /ap-publish (releases never pass through the
    // signed inboxes, so the relay can't federate them on its own).
    void announceReleaseOnFediverse(const QString &tag, const QString &title,
                                    const QString &notes);
    void showReleaseDetail(const QString &tag);
    QWidget *buildArtifactsTab();
    void loadArtifactsPanel();
    void deleteArtifact(const QString &hash, const QString &label);
    QWidget *buildMirrorNodesTab();
    QWidget *buildShortcutsTab();
    void loadShortcutsPanel();
    QString shortcutsDirPath() const; // <working tree>/.forkmesh/shortcuts, "" without one
    void runShortcut(const QString &filePath);
    void draftShortcutPrompt(const QString &filePath);
    void stopShortcut();
    void openShortcutEditor(const QString &filePath);
    void deleteShortcut(const QString &filePath);
    QWidget *buildRepoSettingsTab();
    void refreshRepoSettings(); // sync the Settings controls to the open repo

    // --- Coves: encrypted, password-shared file vaults inside a repo ----------
    // A cove is.forkmesh/coves/<slug>.cove (AES-256-GCM, see CoveStore). The team
    // shares one password out-of-band; entered in repo settings, it unlocks
    // matching legacy coves. Account-scoped coves live in the repository explorer.
    // Opening a cove logs it locally and (if the creator
    // asked) sends a best-effort live alert back to the creator's node.
    CoveStore coveStoreForRepo(int repoIndex) const;
    QString coveRepoSettingsPrefix(int repoIndex) const; // QSettings key prefix
    QString rememberedCovePassword(int repoIndex) const; // repo password
    bool coveAutoOpenEnabled(int repoIndex) const;       // repo toggle
    bool tryUnlockCove(Cove &cove, int repoIndex, QString *passwordOut) const;
    void logCoveAccessLocal(const Cove &cove);           // "log yourself" — local
    QStringList coveAccessLogLocal(const QString &coveId) const;
    void announceCoveOpened(const Cove &cove);           // best-effort live alert
    void openCove(const QString &relPath);               // unlock + show a viewer
    void openCoveViewer(int repoIndex, Cove cove, const QString &password);
    void promptCreateCove(int repoIndex);
    void rebuildRepoCovesList();                         // repo Settings list
    QWidget *buildCoveSection();                         // repo Settings "Coves"
    void applyCovePasswordFromSettings(int repoIndex, const QString &password,
                                       bool remember);
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
    void setRepoActionsAutoApprove(bool on);
    void setRepoRequirePeerApproval(bool on);
    // Enable or disable secret-scanning push protection for the open repo.
    void setRepoSecretScanningEnabled(bool on);
    void setWorkflowDisabled(const QString &path, bool disabled);
    bool isWorkflowDisabled(const QString &path) const;
    void loadMirrorNodesPanel();
    void draftMirrorNodeDiagnosticsPrompt(QTableWidgetItem *healthItem);
    QString mirrorSelfSnapshotKey(const RepositoryRecord &repo) const;
    void refreshMirrorSelfSnapshot(const RepositoryRecord &repo, const QString &key);
    QHash<QString, MirrorSelfSnapshot> m_mirrorSelfSnapshots; // keyed by mirrorPath
    QSet<QString> m_mirrorSelfSnapshotsInFlight;
    void animateMirrorNodeLights();
    void updateMirrorNodeLightTimer();
    void requestMirrorNodesRefresh();
    void onMirrorRefreshRequested(const QString &source,
                                  const QString &requesterName,
                                  bool sync = false);
    void syncMirrorNodeNow(int row);
    void updateMirrorNodeServerButtons(bool visible);
    void fetchCatalogMirrors(const QString &owner, const QString &repo,
                             const QString &source);
    void fetchMirrorReachability(const QString &owner, const QString &repo,
                                 const QString &source, const QString &node);
    void fetchMirrorPendingCounts(const QString &owner, const QString &repo,
                                  const QString &source);
    void fetchReleaseDownloadCounts(const QString &owner, const QString &repo,
                                    const QString &source);
    void replicateReleaseArtifacts(int index);
    void downloadNextReleaseBlob(int index, const QString &mirrorPath,
                                 QMap<QString, QString> pending);
    void pushReleaseToMirrors(const QString &tag);
    void deleteTag(const QString &tag);
    bool repoHasWorkingTree() const;
    void loadFileSearchIndex();
    QWidget *createGlobalSearchBox();          // build the box + results popup
    void rebuildGlobalSearchResults();         // (re)populate the dropdown (debounced)
    void positionGlobalSearchPopup();          // anchor the dropdown under the box
    void moveGlobalSearchSelection(int delta); // keyboard up/down through results
    void activateGlobalSearchItem(QListWidgetItem *item); // navigate to a result
    void hideGlobalSearchPopup();
    void syncGitCommitFilter();
    void syncAgentPageSearch();
    QWidget *createNavHistoryButtons();        // build the Back / Forward pair
    void scheduleNavRecord();                  // queue a debounced location capture
    void recordNavLocation();                  // snapshot the current place onto the trail
    void restoreNavEntry(int index);           // navigate to a recorded place
    struct NavPlace;
    void applyNavDetailTab(const NavPlace &place); // re-select a recorded repo tab
    void captureNavSubPlace(NavPlace &place) const;
    void applyNavSubPlace(const NavPlace &place);
    QString openRepoFilePath() const; // file the code editor is showing
    bool gitWorkspaceIsVisible() const;
    QString navPlaceLabel(const NavPlace &place) const; // human-readable trail destination
    void navigateBack();
    void navigateForward();
    void updateNavHistoryButtons();            // enable/disable per trail position
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
    void showSourceControlLoading(const QString &branch);
    void showRangeFilesInSourceControl(const QStringList &paths,
                                       const QStringList &statuses);
    void clearRangeFilesInSourceControl();
    bool sourceControlShowsRange() const;
    QString sourceControlGitDir() const;
    void scrollBranchDiffToFile(const QString &path, bool moveFocus = true);
    bool stepScmTreeFile(int delta);
    void setScmCurrentItemSilently(QTreeWidgetItem *item);
    void updateBranchDiffActiveOutline();
    void updateScmDiffActiveOutline();
    void refreshRepoChangeBadge();
    void scmStagePath(const QString &path);
    void scmUnstagePath(const QString &path);
    void scmDiscardPath(const QString &path, bool untracked);
    void showScmFileMenu(const QPoint &pos);
    void scmIgnorePath(const QString &path);
    void scmDeletePath(const QString &path);
    void scmStageAll();
    void scmUnstageAll();
    void scmDiscardAll();
    void scmCommit();
    void scmCommitAndPush();
    void scmStageAllCommitAndPush();
    bool performScmCommit();
    bool commitWorkingTree(const QString &message);
    void showScmDiff(const QString &path, bool staged, bool untracked);
    void showScmDiffAll(bool staged);
    void renderScmCombinedDiff();
    void setupScmDiffPane();
    void scrollScmDiffToFile(const QString &path, bool staged);
    bool pinScmDiffSectionToTop(int sectionIndex);
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
    struct CommitMarkerProbe {
        QString localTip;
        QString mirrorTip;
    };
    // mtime+size fingerprint of the working copy's and the served mirror's ref
    // storage. Empty when no repo is open. Same trick as refreshMirrorAdverts()'s
    // input signature: asking git whether git has moved costs exactly what the
    // probe is trying to avoid.
    QString commitMarkerProbeSignature() const;
    QString m_commitMarkerProbeSig;
    qint64 m_commitMarkerProbedAtMs = 0;
    bool m_commitMarkerProbeInFlight = false;
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
    // Shared by refreshRepoSecurity() and runRepoDependencyScan(): gathers the
    // repo state RepoSecurity::scan() needs so it can be handed to a worker
    // thread without touching MainWindow/GUI state from there.
    RepoSecurityInput buildRepoSecurityInput(const RepositoryRecord &selected,
                                              const RepositoryRecord &writable) const;
    // Renders a computed snapshot into the Security tab's widgets (also caches
    // it in m_lastRepoSecuritySnapshot so a "Run scan" click can re-render the
    // busy state instantly, before the rescan itself has produced anything new).
    void applyRepoSecuritySnapshot(const RepoSecuritySnapshot &snapshot,
                                   const QString &localBase);
    void runRepoDependencyScan(const QString &manifestPath = QString());
    void refreshRepoQuality();
    // Open a repo file in the editor and highlight/centre the given 1-based
    // line (used by the Security and Quality findings tables).
    void openRepoFileAtLine(const QString &path, int line);
    void loadRepoInsights();
    void showInsightsContributorMenu(const QPoint &pos);
    void reassignContributorIdentity(const QString &oldName);
    void openCommitsForContributor(const QString &author);
    void setRepoBranch(const QString &branch, bool loadContent = true);
    QString repoHeadBranch() const;          // the checked-out branch (HEAD)
    void updateCommitsBranchButtonLabel();   // branch + current worktree identity
    void refreshCommitsBranchButton();       // commits-page branch indicator/menu
    void createAndCheckoutBranch();          // "Create new branch…"
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
    void setRestartSpinProgress(int percent);
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

    int issuesRepoIndex() const;                 // selected repo, or -1
    IssueStore issueStoreForCurrentRepo() const; // build a store for that repo
    void refreshIssuesRepoCombo();
    void reloadIssues();        // load issues + label/milestone filters from the store
    void reloadIssuesInBackground();
    void applyLoadedIssues(const QString &signature, QList<Issue> issues,
                           QList<IssueLabel> labels,
                           QList<IssueMilestone> milestones);
    void appendCreatedIssue(const IssueStore &store, const Issue &issue);
    void refreshIssueList();    // apply filters into the list widget
    void resetIssueFilters();   // clear status/label/milestone/search filters
    void selectIssueListTab(int id);
    void refreshIssueMilestones();
    void refreshIssueLabels();
    QWidget *buildIssueBoard();
    void refreshIssueBoard();    // rebuild the columns/cards from m_currentIssues
    QStringList boardColumns() const;             // configured column names
    void setBoardColumns(const QStringList &cols); // persist + rebuild
    void editBoardColumns();                      // prompt to edit the column list
    QString issueBoardColumn(const Issue &issue) const;
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
    // Footer slash-actions menu: the "/" button left of the Agent
    // checkbox opens a filterable popup mirroring the Claude Code extension's
    // actions menu (Context/Model sections plus the CLI's own slash commands,
    // pulled live from `claude` via a control-protocol initialize probe).
    void openQuickAddSlashActions();
    void populateSlashActionsList();
    void moveSlashActionsSelection(int delta);
    void activateSlashActionRow(QWidget *row);
    void refreshClaudeSlashCommands();
    QStringList agentEffortLevels() const;
    void refreshQuickAddSpeedSelector();
    void refreshQuickAddAgentModelSelector();
    QList<ComposerModelChoice> composerModelCatalog() const;
    void refreshComposerModelVisibilityList();
    void refreshCloudflareAiModels();
    void refreshClaudeEffortLevels();
    void mentionProjectFileInQuickAdd();
    void showTreasuryDonateDialog();
    void showTreasuryDonateDialogForPool(const QJsonObject &pool);
    void copyIssueToClipboard();
    void copyIssueThreadToClipboard();
    void askAiForCurrentIssue();
    void showIssueAiTyping();
    void hideIssueAiTyping();
    int availableCredits() const;     // 1 voting credit per hour online
    void voteOnCurrentIssue();
    void submitIssueVoteToInbox();
    void updateVoteUi();
    void addIssueComment();
    void closeIssueWithComment();
    void attachIssueImage();
    void queueIssueAttachment(const QString &path); // dedupe + reference + count
    void toggleIssueStatus();
    int nextVisibleIssueAfter(int number) const;
    void deleteCurrentIssue();
    void deleteCurrentIssueWithHistory(int number);
    void editIssueLabels();
    void editIssueMilestone();
    // Planned start/end dates: opens the inline two-QDateEdit
    // editor in the sidebar's "Dates" row; save writes a signed "dates" event.
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
    ProjectStore projectStoreForCurrentRepo() const; // build a store for that repo
    void reloadProjects();      // load projects (+ issues, for progress) from the store
    void refreshProjectList();  // apply the status filter into the table
    void refreshProjectGantt(); // rebuild the Gantt rows from the loaded data
    void showProject(int number); // render the selected project's detail pane
    void promptNewProject();
    void editProjectLinkedIssues(); // multi-select dialog over the repo's issues
    void setProjectInlineNotice(const QString &message, bool error = false);
    int projectProgressPercent(const Project &project) const;

    QUrl issuesApiUrl(const RepositoryRecord &repo) const;
    // Owner-encrypted agent relay: clear session content remains
    // on the desktop; the relay stores only recipient-sealed snapshots/prompts.
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
    // Organization-member Claude/Codex jobs are a distinct, encrypted-at-rest
    // channel. This mirror runs a tool-free Haiku preflight and executes only
    // an exact ALLOW verdict; owner-only E2EE prompts above remain unchanged.
    void drainOrgAgentJobsFor(RepositoryRecord repo);
    void applyOrgAgentJobsPayload(const RepositoryRecord &repo,
                                  const QJsonArray &jobs);
    void runOrgAgentSafetyCheck(const RepositoryRecord &repo,
                                const QJsonObject &job);
    void persistOrgAgentBinding(int localAgentId, const QJsonObject &job);
    void clearOrgAgentBinding(int localAgentId);
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
    // Mirror node path: file a signed "assignees" event to the source of truth's
    // inbox so the looper's claim on an issue reaches the owner and syncs back to
    // every mirror.
    void submitIssueAssigneesToInbox(int number, const QStringList &assignees);
    // Mirror node path: send a signed new-issue ("open") event to the source of
    // truth's inbox. Returns false only when there is no repo to target (checked
    // synchronously); the actual POST result is only known once it completes, so
    // callers get it via onDone rather than assuming success immediately (a
    // failed submission must not be reported as "sent").
    bool submitNewIssueToInbox(const QString &title, const QString &body,
                               const QStringList &labels, const QString &milestone,
                               int priority, const QStringList &assignees,
                               const QStringList &attachmentSrcPaths = {},
                               const QStringList &attachmentPlaceholders = {},
                               std::function<void(bool ok, const QString &error)> onDone = {});
    void syncIssuesInbox();
    // Fetch the signed contents of one collaboration inbox and present a
    // reviewable list with per-submission and sync-all actions. Counts stay on
    // the three toolbar buttons via the public, content-free /pending endpoint.
    void showPendingInbox(const RepositoryRecord &repo, const QString &kind);
    QString relayCooldownMessage(const QString &host) const;
    void showPendingInboxDialog(const RepositoryRecord &repo,
                                const QString &kind,
                                const QJsonArray &pending);
    void applyPendingInboxSelection(const RepositoryRecord &repo,
                                    const QString &kind,
                                    const QJsonArray &pending);
    void reviewPendingPull(RepositoryRecord repo, QJsonObject item);
    void showPullReviewDialog(RepositoryRecord repo, PullRequest pr,
                              PullReviewCheckout checkout);
    QString pullReviewCheckoutPath(const RepositoryRecord &repo,
                                   const QString &slug) const;
    void refreshPendingInboxBadges();
    void setPendingPullsTabBadge(int pending);
    void setPendingInboxCount(const RepositoryRecord &repo,
                              const QString &kind, int count);
    void drainIssuesInboxFor(RepositoryRecord repo, bool interactive,
                             bool forceMirrorIntake = false);
    // Backoff bookkeeping shared by the issue/pull/discussion inbox drains. A
    // rejected drain (HTTP 401/403) is not transient backpressure: the relay
    // decides authorization from this node's identity and its membership in
    // the repo's signed mirror group, so a node outside that group is rejected
    // identically on every retry. See MainWindowIssues.cpp for why the first
    // few rejections still retry on the normal cadence.
    void noteInboxDrainFailure(const QString &backoffKey, int status,
                               bool mirrorIntake);
    void noteInboxDrainSuccess(const QString &backoffKey);
    void pollMirrorIssueInboxes();
    void drainPullsInboxFor(RepositoryRecord repo, bool interactive,
                            bool forceMirrorIntake = false);
    void scanRepoMentionsFor(const RepositoryRecord &repo);
    void applyRepoMentions(const RepositoryRecord &repo,
                           const QList<Issue> &allIssues,
                           const QList<PullRequest> &allPulls);
    void pollOwnedInboxes();
    // Bounded relay sync: scheduleRelaySync() coalesces explicit refresh
    // requests into one signed GET /api/sync that returns every owned repo's
    // pending control-plane changes. The normal timer provides the fallback
    // and no per-repository socket is opened.
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
    // On a mirror-intake ack, attach this node's fresh signed refs
    // attestation (state/stateTs/stateSig) so the relay pins the state the
    // mirror now serves — without it, the merged submissions would knock the
    // mirror out of the clone integrity gate while the source is offline.
    void appendMirrorStateAttestation(QUrlQuery *query,
                                      const RepositoryRecord &repo,
                                      const QString &signer) const;
    void backUpIdentityKey();
    void refreshIdentityBackupNag();
    // Explain provider-owned, per-device Claude login and the owner-sealed
    // shared-workspace boundary. No credential export/import controls exist.
    void showClaudeCodeDeviceSetup();
    void chooseAvatar();
    void setSettingsAvatar(const QByteArray &pngData);
    QByteArray effectiveAvatar();
    QByteArray effectiveUserAvatar();
    // Persist the local avatar to the account record (POST /api/accounts/profile)
    // so the web dashboard shows the same picture. No-op without an authenticated
    // session token or a local avatar to upload.
    void pushAccountAvatar();
    void adoptWebAccountAvatar(const QByteArray &png);
    void updateAvatarButton();
    void updateUserAvatarButton();
    void updateAdminCrownBadge();
    void refreshIssueComposerAvatar();
    QWidget *makeComposerIdentity(QLabel **outAvatar = nullptr,
                                  const QString &verb = QString());
    void logout();
    // Tell the relay to drop the browser-visible account session minted by the
    // last email/password login, so logging out here also logs out on the
    // website. No-op when this desktop only ever authenticated with its key.
    void revokeAccountSession();
    void revokeAccountSessionToken(const QString &token,
                                   const QString &serverUrl = QString());
    // Immediately after a logout, ask for the account password and sign back in
    // against the website. Password login (unlike the silent key-based auth) both
    // mints a website session and re-registers this desktop's public key with the
    // relay, so the site knows this device. Returns true once signed back in.
    bool promptRelogin(const QString &previousAccount);
    void loginToUserAccount();
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
    void logSystem(const QString &text,
                   const char *sourceFile = FORKMESH_LOG_SOURCE_FILE,
                   int sourceLine = FORKMESH_LOG_SOURCE_LINE);
    void logSystemFrom(const QString &text, const QString &sourcePath,
                       int sourceLine);
    void appendNetworkLogLine(const QString &storedLine);
    QString m_lastLogRenderDate; // date of the last line rendered (for dividers)
    bool m_networkLogViewStale = false;
    QString logBadgeFor(const QString &storedLine) const; // category of a line
    QString logAccentFor(const QString &storedLine) const; // badge colour of a line
    void rebuildLogFilterButtons(); // (re)build the category chip row
    int logFilterChipCount(const QString &category) const;
    void updateLogFilterChipCounts(); // refresh the counts without rebuilding
    void updateCloudLogFilterChip();
    void refreshLogTimelineChart();
    void appendLogTimelineEntry(const QString &storedLine);
    void setLogTimelinePresetHours(int hours);
    void updateLogTimelineSummary();
    void chooseCustomLogTimelineRange();
    void rebuildNetworkLogView();   // re-render the log honoring m_logFilter
    QString popoutLogLineHtml(const QString &storedLine, QString &runningDate);
    void appendNetworkLogPopoutLine(const QString &storedLine);
    void updateNetworkLogPopoutStatus();
    QString networkLogPath() const; // on-disk path for the persisted log
    void loadNetworkLog();          // restore log history at startup
    void saveNetworkLog();          // rewrite (and trim) the on-disk log
    int m_logRenderFrom = 0;
    bool m_logViewMutating = false;
    bool m_logFilterEmptyNotice = false;
    void loadOlderNetworkLogSegment();
    void onNetworkLogScrolled(int value);
    void flashMessage(const QString &text, bool error = false,
                      const QString &clickHref = QString(),
                      int durationSeconds = 0,
                      const QString &kind = QString(), int actionRunId = -1,
                      const char *sourceFile = FORKMESH_LOG_SOURCE_FILE,
                      int sourceLine = FORKMESH_LOG_SOURCE_LINE);
    // Ping the mesh about the failures this app used to only ever show to itself
    // Every warning/critical modal (caught by eventFilter's
    // QEvent::Show branch, so no call site has to remember) and every error toast
    // is reported to the relay, which records it in the shared operational error
    // log and pings the administrators the first time a distinct failure appears
    // — the only way an error on a headless node, or on a machine nobody is
    // watching, is ever seen. Signed by this node's account; QSettings
    // kReportUserVisibleErrorsSetting = false turns it off. See
    // ClientErrorReports.h for the redaction, dedupe and deferral bounds.
    // `pingId` is the Pings row this failure was filed as, so the report's fate
    // (sent / parked / refused) lands back on that row. 0 means
    // "not filed", e.g. a report raised before the page existed.
    void reportUserVisibleError(const QString &kind, const QString &title,
                                const QString &message,
                                const QString &surface = QString(),
                                qint64 pingId = 0);
    void sendUserVisibleErrorReport(
        const forkmesh::ClientErrorReports::Report &report);
    void flushDeferredErrorReports();
    void scheduleDeferredErrorReportFlush();
    forkmesh::ClientErrorReports m_errorReports;
    QTimer *m_errorReportFlushTimer = nullptr;
    void showTopMessage(const QString &text, bool error,
                        const QString &clickHref = QString(),
                        int durationSeconds = 0,
                        const QString &kind = QString(), int actionRunId = -1);
    void alertOnLoggedError(const QString &message);
    void dismissTopMessage(); // hide the top toast and its Copy / dismiss buttons
    void advanceTopMessageQueue(); // show the next queued message, or dismiss if none left
    void queueTopMessage(const QString &text, bool error,
                         const QString &clickHref = QString(),
                         const QString &kind = QString(),
                         int durationSeconds = 0,
                         int actionRunId = -1); // park one behind the current toast
    void appendTopMessageToPrompt(const QString &text);
    void dismissQueuedTopMessage(quint64 id);
    void renderTopMessageQueue(); // repaint the visible stack of queued notifications
    bool topMessageBusy() const; // a toast is up and still counting down
    void renderTopMessageCountdown(); // (re)paint the toast with its seconds-left suffix
    void renderTopMessage(); // (re)paint the current notification bubble
    int topMessageAgentDoneSessionId() const;
    QString agentDoneHeadlineHtml(int sessionId);
    void updateTopMessagePromptLiveStatus(const QString &line);
    void renderTopMessagePromptImages(); // rebuild thumbnails for a sent prompt
    void positionTopMessageBubble(bool animate = false);
    QRect topMessageBubbleRect(); // calculates the prompt-anchored stack geometry
    QWidget *topMessagePromptAnchor() const;
    int topMessageStackCeiling(int promptTop, int margin) const;
    int topMessageBodyHeight(int textWidth) const;
    int topMessageEntryRise(const QRect &target) const;
    void placeTopMessageQueue(const QRect &bubble, bool animate);
    void animateTopMessageEntry(const QRect &target);
    void setTopMessagePaused(bool paused); // hover pauses the countdown
    void slideTopMessageOut(); // countdown finished: ease the bubble off the right edge, then advance
    void showPromptBubble(const QString &prompt, int agentSessionId = -1,
                          const QString &status = QString(),
                          const QStringList &images = QStringList());
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
    QString chatHistoryPathForServerUrl(const QString &serverUrl,
                                        const QString &room) const;
    QStringList chatHistoryCompatibilityPaths() const;
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
                               bool isPrivate, bool localOnly, QString *error);
    // Clone a remote repo (GitHub/GitLab/any https git URL) into a local working
    // copy, then add it like a local repo. An optional per-host access token
    // (Settings) authenticates the clone to dodge unauthenticated rate limits.
    void importRemoteRepository();
    void importSingleRemoteRepository(const QString &url);
    QStringList importAuthGitArgs(const QString &url) const;
    struct GitlabGroupImport {
        QString group;          // full group path, e.g. acme/platform
        QString parentDir;      // directory each clone lands in
        QStringList cloneUrls;  // projects still to clone
        QStringList names;      // repository name chosen for each clone URL
        int total = 0;
        int imported = 0;
        int failed = 0;
    };
    GitlabGroupImport m_gitlabGroupImport;
    QString gitlabGroupPathFor(const QUrl &url) const;
    void probeGitlabGroup(const QString &url, const QString &groupPath);
    void fetchGitlabGroupProjects(const QString &groupPath, int page);
    void cloneNextGitlabGroupProject();
    void finishGitlabGroupImport();
    void setImportStatus(const QString &text, bool error);
    void setImportControlsEnabled(bool enabled);
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
    void restartMirrorSyncTimer();
    void syncMirrorsBehindRoster();
    bool mirrorHasCommit(const QString &mirrorPath, const QString &commit);
    QSet<QString> m_mirrorCommitsPresent; // "<mirrorPath>\x1f<commit>" seen present
    QHash<QString, QString> m_mirrorRefsFingerprintActed;
    void convergeSourceRepoFromMesh(int index);
    QHash<QString, qint64> m_sourceConvergeAttemptMs; // owner/name -> last try
    void propagateRepoUpdate(int index);
    void onPeerMirrorUpdated(const QString &ownerName, const QString &peerName,
                             const QString &commit);
    void onPeerMirrorSynced(const QString &ownerName, const QString &peerName,
                            const QString &commit);
    bool applyPeerMirrorCommit(const QString &ownerName, const QString &peerName,
                               const QString &commit);
    // A peer opened an encrypted cove. If this node created it (creatorKey matches
    // our identity) and the opener's signature checks out, raise a notification.
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
    // Fresh "-c http.extraHeader=Authorization: Basic..." git args carrying an
    // owner-key-signed forkmesh-view-v1 token, so clone/fetch of our own private
    // repo through the mainnode passes the relay's read gate. Empty when the repo
    // is public or the source URL is not the mainnode host (never leak the token).
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
    QStringList m_legacyServerUrls;
    QStringList m_legacyServerRooms;
    int m_activeServer = 0;
    QHash<QString, QPixmap> m_faviconCache; // host -> favicon
    QSet<QString> m_faviconFetching;        // hosts with an in-flight favicon GET
    QSet<QString> m_faviconMissing;         // hosts whose favicon GET failed once

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
    QSet<QString> m_relaySpeedProbes; // hosts with a dropdown probe in flight
    void probeRelayHostSpeed(const QString &serverUrl,
                             std::function<void(int)> done);
    QString relaySpeedText(const QString &host) const;
    QString relayMenuEntryText(const QString &host) const;
    QTimer *m_relayLatencyTimer = nullptr; // one-minute relay-latency probe
    bool m_relayProbeInFlight = false;     // guard against overlapping probes
    qint64 m_lastWsLatencySampleMs = 0;    // when the room socket last ponged
    int m_relayProbeFailures = 0;          // consecutive failed probes; the radar
    int m_relayProbeElevated = 0;          // consecutive elevated-latency samples;
    QLabel *m_nodeLabel = nullptr;
    QLabel *m_repoLabel = nullptr;
    QLabel *m_navSolanaBalance = nullptr;
    QWidget *m_navTokenUsage = nullptr;
    QWidget *m_navCodexUsage = nullptr;
    QAbstractButton *m_nodeOnlineToggle = nullptr;
    QLabel *m_nodeOnlineStatusLabel = nullptr;
    QLabel *m_nodeRewardStatus = nullptr;
    QLabel *m_nodeUptimeLabel = nullptr;
    QWidget *m_profileOnlineSection = nullptr; // wraps the switch + status lines
    bool m_nodeOffline = false;
    qint64 m_navSolanaLamports = -1; // last known balance, -1 = not yet fetched
    qint64 m_navSolanaFetchedMs = 0;
    bool m_navSolanaFetchInFlight = false;
    QString m_webSolanaAccount;
    QString m_webSolanaAddress;
    bool m_webSolanaKnown = false;
    bool m_webSolanaFetchInFlight = false;
    qint64 m_webSolanaFetchedMs = 0;
    QTimer *m_webSolanaTimer = nullptr;
    QPushButton *m_chatButton = nullptr; // top-bar chat toggle (next to the bell)
    QPushButton *m_tasksNavButton = nullptr;
    QPushButton *m_notesNavButton = nullptr;
    QListWidget *m_notesList = nullptr;
    QLineEdit *m_noteTitle = nullptr;
    MarkdownEditor *m_noteEditor = nullptr;
    QComboBox *m_noteStorageMode = nullptr;
    QCheckBox *m_notePublic = nullptr;
    QLabel *m_notePublicLink = nullptr;
    QLabel *m_notesStatus = nullptr;
    QJsonArray m_localNotes;
    QJsonArray m_cloudNotes;
    QJsonObject m_selectedNote;
    QString m_selectedNoteStorage;
    bool m_notesLoading = false;
    bool m_noteDirty = false;
    QTimer *m_noteRefreshTimer = nullptr;
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
    QPushButton *m_organizationTaskPromptButton = nullptr;
    QPushButton *m_organizationTaskStartAgentButton = nullptr;
    QPushButton *m_organizationTaskPrevPageButton = nullptr;
    QPushButton *m_organizationTaskNextPageButton = nullptr;
    QLabel *m_organizationTaskPageLabel = nullptr;
    int m_organizationTasksPage = 0;
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
    QWidget *m_chromeActionDivider = nullptr;
    QString m_agentDotTooltipKey;
    QString m_actionRunStripTooltipKey;
    QLabel *m_connectionDot = nullptr;
    QString m_connectionStatusColor;      // last dot colour (skip redundant repaints)
    QLabel *m_adminCrownBadge = nullptr;
    QLabel *m_topMessage = nullptr;       // prompt-anchored success/failure bubble text
    QFrame *m_topMessageContainer = nullptr; // floating bubble wrapping text + actions
    QWidget *m_topMessageBody = nullptr;  // scrollable prompt/notification content
    QLabel *m_topMessagePromptHeader = nullptr; // "Prompt sent" line
    QLabel *m_topMessagePromptStatusLabel = nullptr; // agent info on its own line
    // Headline row of an "agent finished" celebration: the agent's
    // own list icon beside "🎉 Agent #12 is done! · #42 · repo · 2m 04s". The
    // summary the run signed off with is the message text beneath it.
    QWidget *m_topMessageAgentRow = nullptr;
    QLabel *m_topMessageAgentIcon = nullptr;
    QLabel *m_topMessageAgentHeadline = nullptr;
    QWidget *m_topMessagePromptImages = nullptr; // submitted image thumbnails
    QStringList m_topMessagePromptImagePaths;
    QWidget *m_topMessageContentRow = nullptr;
    QLabel *m_topMessageAvatar = nullptr;
    QPixmap m_topMessageAvatarPixmap;
    QPixmap m_pendingToastAvatar;
    QScrollArea *m_topMessageQueueScroll = nullptr;
    QWidget *m_topMessageQueueContent = nullptr;
    QVBoxLayout *m_topMessageQueueLayout = nullptr;
    QScrollArea *m_topMessageScroll = nullptr;
    QWidget *m_topMessageActions = nullptr; // full-width row beneath the text: countdown + buttons
    QLabel *m_topMessageMeta = nullptr;   // dim "5s · +2 more · paused" on the left of that row
    QTimer *m_topMessageTimer = nullptr;  // auto-clears the bubble
    QPropertyAnimation *m_topMessageFlight = nullptr;
    QPropertyAnimation *m_topMessageQueueFlight = nullptr;
    int m_topMessagePromptFloor = -1;
    bool m_topMessageSlidingOut = false;  // countdown finished; bubble is easing off the right edge
    bool m_topMessageEntering = false;    // wait for the entry glide before starting its countdown
    bool m_topMessageShifting = false;    // gliding up/down because the queue changed depth
    QPushButton *m_topMessageCopy = nullptr;
    QPushButton *m_topMessageActionOutput = nullptr;
    QPushButton *m_topMessageSendToPrompt = nullptr;
    QPushButton *m_topMessageClose = nullptr;
    QLabel *m_topMessageTypeBadge = nullptr;
    QString m_topMessageRaw;              // plain text of the current bubble, for copy/retry
    QString m_topMessageBaseHtml;         // bubble HTML (the whole message; never elided)
    QString m_topMessageHref;             // when set, the toast is a clickable link (routed by linkActivated)
    QString m_topMessageKind;             // typed badge on the current notification
    int m_topMessageAgentSessionId = -1;  // prompt notification's exact agent, if any
    int m_topMessageActionRunId = -1;     // failed action whose output the toast can open
    int m_topMessageSecondsLeft = 0;      // seconds before an auto-dismiss toast slides away
    struct TopMessageQueueEntry {
        quint64 id = 0;
        QString text;
        bool error = false;
        QString clickHref;
        int durationSeconds = 0; // its full countdown starts when it reaches the top
        QString kind;
        int actionRunId = -1;
        QPixmap avatar;
    };
    QList<TopMessageQueueEntry> m_topMessageQueue;
    quint64 m_nextTopMessageQueueId = 1;
    bool m_topMessageError = false;       // current toast is a failure (red) vs success (green)
    bool m_topMessageHovering = false;    // pauses the countdown while reading/actions
    bool m_topMessageIsPromptBubble = false; // submitted prompt gets a fuller, animated treatment
    QString m_topMessagePromptStatus;        // optional agent-start confirmation on its own line
    QWidget *m_errorBorderOverlay = nullptr;
    QTimer *m_errorBorderTimer = nullptr;
    QWidget *m_celebrationBorderOverlay = nullptr;
    QTimer *m_celebrationBorderTimer = nullptr;
    bool m_topMessageOwnsLoggedError = false;
    bool m_inLoggedErrorAlert = false;    // re-entrancy guard for alertOnLoggedError
    QString m_lastLoggedErrorText;        // last error alerted on, for repeat de-duplication
    qint64 m_lastLoggedErrorAtMs = 0;
    qint64 m_loggedErrorBurstStartMs = 0;
    int m_loggedErrorBurstCount = 0;
    bool m_loggedErrorBurstNoticeShown = false;
    QWidget *m_restartCautionBorderOverlay = nullptr;
    QTimer *m_restartCautionBorderTimer = nullptr;
    bool m_repoPinMismatch = false;
    QHash<QString, qint64> m_repoPinAutoHealAtMs; // owner/name -> last automatic pin re-attest (rate-limits the source-of-truth auto-heal in refreshRepoPinBanner)

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
    QWidget *m_footerLeftRegion = nullptr;
    QWidget *m_debugBar = nullptr;
    QPushButton *m_statusVersionButton = nullptr;
    bool m_debugBarStartupApplied = false;
    QCheckBox *m_debugBarStartupCheck = nullptr;
    QPlainTextEdit *m_debugLogTail = nullptr;
    QPushButton *m_debugLogTailButton = nullptr;
    int m_debugLogTailHeight = 0; // the strip the window grows by, in pixels
    bool m_debugLogTailShown = false;
    QPointer<VerticalIconButton> m_cloudLogFilterChip;
    QCheckBox *m_cloudLogMonitorSettingCheck = nullptr;
    QProcess *m_cloudLogMonitorProcess = nullptr;
    QByteArray m_cloudLogMonitorBuffer; // partial tail record across reads
    int m_cloudLogMonitorErrors = 0;     // errors seen since monitoring began
    int m_cloudLogMonitorEvents = 0;     // Worker events seen since then
    bool m_cloudLogMonitorStopping = false; // a deliberate stop, not a crash
    QTimer *m_cloudLogMonitorRestartTimer = nullptr;
    QElapsedTimer m_cloudLogMonitorUptime;
    int m_cloudLogMonitorRestarts = 0;
    bool m_cloudLogMonitorResuming = false;
    QString m_cloudLogMonitorIdleReason;
    QWidget *m_globalOverlayHost = nullptr;
    QWidget *m_promptOverlayHost = nullptr;
    forkmesh::ui::LogActivityLights *m_logActivityLights = nullptr;
    forkmesh::ui::LogActivityLights *m_logActivityHeader = nullptr;
    bool m_logOverlayExpanded = false;
    bool m_promptOverlayCollapsed = false;
    QWidget *m_promptDragHandle = nullptr;
    QWidget *m_promptResizeGrip = nullptr;
    QPushButton *m_promptDetachButton = nullptr;
    QPushButton *m_promptResetButton = nullptr;
    int m_promptAnchoredHeight = 0;
    QWidget *m_promptDetachWindow = nullptr;
    bool m_promptOverlayFloating = false;
    bool m_promptOverlayDetached = false;
    QPoint m_promptOverlayPos;  // top-left within m_globalOverlayHost
    QSize m_promptOverlaySize;  // user-chosen size; invalid means "auto"
    QSize m_promptOverlayHostSize;
    QRect m_promptDetachGeometry;
    bool m_promptPlacementDragging = false;
    bool m_promptPlacementResizing = false;
    QPoint m_promptPlacementGrab;
    QRect m_promptPlacementStartRect;
    bool m_footerWebsiteStatusInFlight = false;
    struct FooterStatusRow {
        QString id;
        QString label;
        QString status;
        QString reason;
        qint64 minuteTs = 0;
        bool local = false; // measured here rather than reported by the relay
        QString sampleStatus;
    };
    QList<FooterStatusRow> m_footerRelayStatuses;
    QList<FooterStatusRow> m_footerDesktopStatuses;
    QSet<QString> m_desktopProbesInFlight;
    struct DesktopProbeSample {
        qint64 ts = 0;
        QString status;
    };
    QHash<QString, QList<DesktopProbeSample>> m_desktopProbeHistory;
    bool m_adminConsoleUrlInFlight = false;
    QWidget *m_statusBackgroundHost = nullptr;
    QHBoxLayout *m_statusBackgroundLayout = nullptr;
    QHash<QString, forkmesh::ui::BackgroundTaskChip *>
        m_backgroundTaskChips;                           // word -> chip
    QHash<QString, int> m_backgroundTaskCounts;          // word -> open tickets
    QHash<QString, qint64> m_backgroundTaskSince;        // word -> first ticket ms
    QHash<QString, QString> m_backgroundTaskDetails;     // word -> newest note
    QHash<quint64, QString> m_backgroundTaskWords;       // ticket -> word
    QHash<QString, bool> m_backgroundTaskHadUiBlocking;  // word -> any UI scope
    struct BackgroundOutcomeTally {
        int runs = 0;
        qint64 longestMs = 0;
        qint64 firstAt = 0;
        QString detail;
    };
    QHash<QString, BackgroundOutcomeTally> m_backgroundTaskDone; // word -> ✓
    QHash<QString, BackgroundOutcomeTally> m_backgroundTaskUiBlocking; // word -> ✕
    QTimer *m_backgroundTaskSpinTimer = nullptr;
    qreal m_backgroundTaskSpinAngle = 0.0; // shared ring rotation, degrees
    int m_backgroundTaskIdleTicks = 0;
    QString m_updateAsUser;

    QLabel *m_statusLine;
    QLabel *m_channelTitle;
    QLabel *m_encryptionLabel;
    QPushButton *m_inviteButton = nullptr; // "Invite" — shown only in private rooms
    QListWidget *m_channelList;
    QPushButton *m_nodeMenuButton = nullptr; // top-bar node switcher
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
    QString m_selectedNode;             // node whose repos fill the repos column
    QPushButton *m_repoMenuButton = nullptr; // top-bar repo switcher
    QPushButton *m_repoViewButton = nullptr; // "Code" button on the repo header row
    QPushButton *m_reposNavButton = nullptr; // network-wide "Repos" section
    QPushButton *m_settingsNavButton = nullptr; // Settings button on the repo header row
    QPushButton *m_logNavButton = nullptr; // full Log destination in the left rail
    QPushButton *m_floatingLogButton = nullptr; // retired; retained for safe resize no-op
    QPushButton *m_controlNodeNavButton = nullptr; // local control-node operations
    QPushButton *m_networkNavButton = nullptr; // "Network" diagnostics top-nav button
    QPushButton *m_usersNavButton = nullptr; // admin-only World user statistics
    QPushButton *m_filesNavButton = nullptr; // local file explorer, under Users
    QPushButton *m_navRebuildButton = nullptr; // small rebuild+restart button (opt-in)
    QPushButton *m_navSignInButton = nullptr;
    QPushButton *m_accountSwitcherButton = nullptr;
    QPushButton *m_navScreenshotButton = nullptr; // drag-a-region screenshot -> prompt
    QPushButton *m_navResizeButton = nullptr; // snap window to a common minimal size
    QPushButton *m_restartSpinButton = nullptr; // button whose icon spins mid-restart
    bool m_rebuildRestartQueued = false;
    QTimer *m_rebuildQueuePollTimer = nullptr;
    QTableWidget *m_networkReposTable = nullptr;
    QLabel *m_networkReposStatus = nullptr;
    QPushButton *m_networkReposRefreshButton = nullptr;
    int m_networkReposLoadGen = 0;
    QJsonArray m_networkReposLastPayload; // regroup when user/node ownership arrives
    int m_networkRepoRowCount = -1;
    // Opaque private archive ids are delivered only in an authenticated,
    // ACL-filtered catalog response. They let the client use a name-free
    // /api/private-replicas/<id> URL; no private owner/repository identity is
    // placed in an edge-visible path or query string.
    QHash<QString, QString> m_privateCatalogAccessIds; // owner/name -> 64 hex
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
    // API token tab. m_controlTokenSecret is the same kind of short-lived
    // redaction copy as m_cloudflareActiveSecret: it exists so this tab's own
    // output can never echo the credential, and is cleared when a check ends.
    QLineEdit *m_controlTokenEdit = nullptr;
    QLabel *m_controlTokenStatus = nullptr;
    QLabel *m_controlTokenTargets = nullptr;
    QTableWidget *m_controlTokenTable = nullptr;
    QPushButton *m_controlTokenTestButton = nullptr;
    QPushButton *m_controlTokenGenerateButton = nullptr;
    QPlainTextEdit *m_controlTokenOutput = nullptr;
    QString m_controlTokenSecret;
    QStringList m_controlTokenGrantedGroups;
    QHash<QString, QString> m_controlTokenProbeResults; // requirement key -> live check
    QString m_controlTokenAccountId;
    QString m_controlTokenZoneId;
    QString m_controlTokenUserResource;
    bool m_controlTokenPolicyReadable = false;
    bool m_controlTokenBusy = false;
    QPushButton *m_siteDeployButton = nullptr;
    QList<QPushButton *> m_siteDeployTargetButtons;
    QPushButton *m_siteDeployCancelButton = nullptr;
    QLabel *m_siteDeployStatus = nullptr;
    BusySpinner *m_siteDeploySpinner = nullptr;
    QPlainTextEdit *m_siteDeployOutput = nullptr;
    QProcess *m_siteDeployProcess = nullptr;
    QString m_siteDeployTarget;
    QProcess *m_cloudflareBootstrapProcess = nullptr;
    QProcess *m_cloudflareTunnelBootstrapProcess = nullptr;
    QProcess *m_cloudflaredInstallProcess = nullptr;
    QProcess *m_mirrorNodeProcess = nullptr;
    bool m_mirrorNodeStopRequested = false;
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
    QDialog *m_mirrorNodeCompanion = nullptr;
    QLabel *m_mirrorNodeCompanionStatus = nullptr;
    QLabel *m_mirrorNodeCompanionStats = nullptr;
    QLabel *m_mirrorNodeCompanionAccount = nullptr;
    QLabel *m_mirrorNodeCompanionBalance = nullptr;
    QLabel *m_mirrorNodeCompanionQr = nullptr;
    QPushButton *m_mirrorNodeCompanionLogout = nullptr;
    QTimer *m_mirrorNodeCompanionTimer = nullptr;
    // Full encrypted-archive authentication hashes hundreds of megabytes for a
    // large mirror. Keep it off the GUI thread and let the Control page render
    // the most recent completed snapshot.
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
    QLabel *m_hostsFleetSummary = nullptr;
    QPushButton *m_hostsProbeButton = nullptr;
    QTimer *m_hostProbeTimer = nullptr;
    QSet<QString> m_hostProbesInFlight;
    QHash<QString, QString> m_hostReachability;
    QHash<QString, QString> m_hostClaudeAvailability;
    QHash<QString, QString> m_hostCodexAvailability;
    QProcess *m_hostInstallProcess = nullptr; // running ssh install session, if any
    QProcess *m_hostLogProcess = nullptr;     // running ssh log-tail session, if any
    QProcess *m_hostActionsProcess = nullptr; // one-shot stdin-only Actions config
    QProcess *m_hostAgentInstallProcess = nullptr; // Claude/Codex CLI install
    QProcess *m_hostDiskProcess = nullptr;    // running ssh size-map read, if any
    QLineEdit *m_vultrApiKeyEdit = nullptr;
    QString m_vultrRememberedKey;
    QLineEdit *m_vultrNameEdit = nullptr;
    QCheckBox *m_vultrAgentClisCheck = nullptr;
    QPushButton *m_vultrCreateButton = nullptr;
    QPushButton *m_vultrEndDeploymentButton = nullptr;
    QLabel *m_vultrStatus = nullptr;
    QCheckBox *m_mirrorFleetEnabledCheck = nullptr;
    QSpinBox *m_mirrorFleetDesiredSpin = nullptr;
    QLabel *m_mirrorFleetStatus = nullptr;
    QTimer *m_mirrorFleetCheckTimer = nullptr;
    QTimer *m_mirrorFleetCountdownTimer = nullptr;
    QString m_mirrorFleetStatusText;
    bool m_mirrorFleetReconcileInFlight = false;
    bool m_mirrorFleetMutationInFlight = false;
    QWidget *m_vultrProgressPanel = nullptr;
    QList<QLabel *> m_vultrStageNumbers;
    QList<QLabel *> m_vultrStageLabels;
    QList<QLabel *> m_vultrStageDetails;
    QList<QString> m_vultrStageDetailText;
    QLabel *m_vultrLiveBadge = nullptr;
    bool m_vultrProvisionActive = false;
    bool m_vultrResumeRequested = false;
    bool m_vultrResumeChain = false;
    bool m_vultrLogSaveScheduled = false;
    int m_vultrProvisionStage = 0;
    QString m_vultrProvisionState;
    QString m_vultrProvisionDetail;
    QString m_vultrProvisionMessage;
    QString m_vultrProvisionNode;
    QString m_vultrInstanceId;
    QString m_vultrInstanceIp;
    QString m_vultrIdentityFile;
    int m_vultrPollCount = 0;        // instance boot polls used this run
    int m_vultrInstallAttempts = 0;  // SSH install attempts used this run
    int m_vultrSshWaitCount = 0;     // SSH reachability probes used this run
    QProcess *m_vultrSshProbeProcess = nullptr;
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

    QLabel *m_hostDeployLabel = nullptr;      // "Live output — per host" header
    QWidget *m_hostDeployPanel = nullptr;     // container holding the per-host panes
    QGridLayout *m_hostDeployGrid = nullptr;  // lays the panes out roughly square
    QList<HostDeploySession *> m_hostDeploySessions;
    int m_hostDeployRemaining = 0;            // sessions still running
    int m_hostDeployFailed = 0;               // sessions that finished with an error

    QTableWidget *m_relaysTable = nullptr;
    QLabel *m_relaysStatus = nullptr;       // "Probing N relays…" / last-refreshed line
    QPushButton *m_relaysRefreshButton = nullptr;
    int m_relayProbesInFlight = 0;          // outstanding /api/version probes
    QTableWidget *m_nodesTable = nullptr;
    QLabel *m_nodesStatus = nullptr;            // "N nodes · M online" summary line
    QPushButton *m_nodesRefreshButton = nullptr;
    QScrollArea *m_nodeDetailScroll = nullptr;  // detail panel for the selected node
    bool m_nodeDeleteInProgress = false;
    QString m_nodeDeleteTarget;
    QSet<QString> m_deletedMeshNodeNames;
    // Node names (lowercased) the relay currently reports online — an update
    // channel or a fresh signed heartbeat. Merged into the Nodes page's status so
    // headless mirror nodes that serve via the relay (but never join this
    // client's chat room) show online instead of permanently offline.
    QSet<QString> m_relayOnlineNodes;
    qint64 m_relayOnlineNodesFetchedMs = 0; // throttle between relay fetches
    // True once /api/network/stats has answered at least once, so the Nodes page
    // knows the relay's authoritative live set is available. Before the first
    // reply we fall back to the encrypted roster's presence flag; after it, the
    // relay is trusted over a possibly-stale roster entry.
    bool m_relayOnlineNodesFetched = false;
    QHash<QString, QJsonObject> m_nodesCatalogInfo;
    qint64 m_nodesCatalogFetchedMs = 0; // throttle between catalog sweeps
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
    QWidget *m_networkWebRequestsChart = nullptr;
    QTableWidget *m_networkWebRequestsTable = nullptr;
    QLabel *m_networkWebRequestsStatus = nullptr;
    QComboBox *m_networkWebRequestsRange = nullptr;
    int m_networkWebRequestsTabIndex = -1;
    int m_networkWebRequestsMinutes = 60;
    bool m_networkWebRequestsInFlight = false;
    bool m_networkWebRequestsUserSorted = false;
    int m_networkWebRequestsAttempt = 0;
    QTimer *m_networkWebRequestsRetryTimer = nullptr;
    QTabWidget *m_networkTabs = nullptr;
    static constexpr int kNetworkRelaysTab = 0;
    static constexpr int kNetworkNodesTab = 1;
    static constexpr int kNetworkHostsTab = 2;
    int m_networkRelayCount = 0;
    int m_networkNodeCount = 0;
    int m_networkHostCount = 0;
    QTableWidget *m_usersTable = nullptr;
    QLabel *m_usersStatus = nullptr;
    QPushButton *m_usersRefreshButton = nullptr;
    QJsonArray m_usersDirectoryPayload;
    int m_repoPinCheckIndex = -1;            // repo index an in-flight pin check belongs to
    struct RepoMenuEntry {
        QString label;
        QIcon icon;
        int index = -1;     // m_repositories index; -2 = advertised mirror
        QString advertised; // ownerName when index == -2
        QString detail;     // tooltip explaining what distinguishes this entry
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
    QVBoxLayout *m_messageLayout; // message rows + a trailing stretch
    bool m_stickToBottom = true;
    QLabel *m_typingLabel;
    QLineEdit *m_messageInput;
    QCompleter *m_mentionCompleter = nullptr;
    QStringListModel *m_mentionModel = nullptr;
    QAbstractItemView *m_mentionCompleterPopup = nullptr;

    QLineEdit *m_settingsNameEdit = nullptr;        // Username (the account)
    QLineEdit *m_settingsMachineNodeEdit = nullptr; // this machine's node name
    QLineEdit *m_settingsNodeLabelsEdit = nullptr;  // extra Actions `runs-on:` labels
    QLineEdit *m_settingsSolanaEdit = nullptr; // #66: node Solana address in Settings
    QLabel *m_settingsEmailLabel = nullptr;
    QLabel *m_settingsEmailVerifiedBadge = nullptr;
    QLabel *m_settingsAvatarPreview = nullptr;
    QLabel *m_identityBackupNag = nullptr; // #368: "back up your key" warning
    QTextBrowser *m_settingsLog = nullptr;
    QPointer<QDialog> m_logPopout;
    QPointer<QTextBrowser> m_logPopoutView;
    QPointer<QLabel> m_logPopoutStatus;
    QString m_logPopoutDate;         // last day divider written to the pop-out
    bool m_logPopoutFilling = false; // history render in progress
    QStringList m_logPopoutPending;  // lines logged while it was filling
    LogTimelineChart *m_logTimelineChart = nullptr;
    QLabel *m_logTimelineSummary = nullptr;
    QPushButton *m_logTimelineResetZoom = nullptr;
    QButtonGroup *m_logTimelineRangeGroup = nullptr;
    int m_logTimelinePresetHours = 24; // 0 means the user supplied a custom range
    qint64 m_logTimelineCustomFromMs = 0;
    qint64 m_logTimelineCustomToMs = 0;
    QHBoxLayout *m_logFilterRow = nullptr;    // chip row above the network log
    QButtonGroup *m_logFilterGroup = nullptr; // exclusive group for filter chips
    QString m_logFilter;                      // active category badge ("" = All)
    QPushButton *m_rebuildButton = nullptr;
    QLabel *m_rebuildStatus = nullptr;
    QLineEdit *m_mirrorRootEdit = nullptr;
    QLineEdit *m_previewCacheRootEdit = nullptr;
    QTableWidget *m_dataDirTable = nullptr;
    quint64 m_dataDirScanGeneration = 0;
    QLabel *m_dataStatus = nullptr;
    QTableWidget *m_backupTable = nullptr;
    QCheckBox *m_backupEnabledCheck = nullptr;
    QSpinBox *m_backupKeepSpin = nullptr;
    QLabel *m_backupStatus = nullptr;
    QPushButton *m_backupNowButton = nullptr;
    QTimer *m_backupTimer = nullptr;     // hourly tick
    QProcess *m_backupProcess = nullptr; // the in-flight `tar` (one at a time)
    QLineEdit *m_importUrlEdit = nullptr;
    QPushButton *m_importButton = nullptr;
    QLabel *m_importStatus = nullptr;
    QCheckBox *m_autostartCheck = nullptr;
    QLabel *m_autostartInfo = nullptr;
    QPushButton *m_autostartRemoveButton = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_defaultAgentProviderCombo = nullptr;
    QListWidget *m_composerModelVisibilityList = nullptr;
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
    QTimer *m_relaySyncDebounce = nullptr;
    bool m_relaySyncSupported = true;
    bool m_relaySyncInFlight = false;
    QSet<QString> m_mirrorIssueIntakeInFlight;
    QTimer *m_autoUpdateTimer = nullptr; // periodic check for maybeAutoUpdate()
    bool m_autoUpdateChecking = false;   // a background "git fetch" check is in flight
    bool m_closingDown = false;

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
    QPlainTextEdit *m_issueQuickAdd = nullptr;
    QFrame *m_promptWrapper = nullptr; // geometry anchor for notification/prompt bubbles
    QLabel *m_quickAddCharCount = nullptr; // characters left in the title (max 16000)
    QComboBox *m_quickAddAgentProvider = nullptr;
    QComboBox *m_quickAddAgentModelSelector = nullptr;
    QComboBox *m_quickAddClaudeModel = nullptr;
    // Permission-mode chooser: Ask before edits/Edit automatically/
    // Plan mode/Auto mode, styled like the provider/model combos beside it and
    // backed by the same kClaudeAutoModeSetting as the agent composer's toggle.
    QComboBox *m_quickAddModeSelector = nullptr;
    QComboBox *m_quickAddSpeedSelector = nullptr;
    QCheckBox *m_quickAddCreatePr = nullptr;    // request PR from quick-add agent
    QPushButton *m_quickAddSendToAgentButton = nullptr;
    QPushButton *m_quickAddGenieButton = nullptr;
    QPushButton *m_quickAddSendButton = nullptr;
    QPushButton *m_quickAddImageButton = nullptr;
    QStringList m_quickAddImages;               // image paths queued for next send
    QWidget *m_quickAddAttachStrip = nullptr;   // chips w/ thumbnail + "x" remove
    // Slash-actions menu: the "/" button left of the Agent checkbox
    // and its popup — a filter box over Context/Model action rows plus the
    // Claude Code CLI's slash commands. The command list is probed live from
    // `claude` (control-protocol initialize) the first time the popup opens.
    QPushButton *m_quickAddSlashButton = nullptr;
    QFrame *m_slashActionsPopup = nullptr;
    QLineEdit *m_slashActionsFilter = nullptr;
    QScrollArea *m_slashActionsScroll = nullptr;
    QWidget *m_slashActionsListHost = nullptr;
    QVBoxLayout *m_slashActionsListLayout = nullptr;
    QList<QWidget *> m_slashActionRows; // visible activatable rows, display order
    int m_slashActionSelected = -1;     // index into m_slashActionRows
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
    QLabel *m_whisperStatusLabel = nullptr;      // Settings install-status line
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
    QLabel *m_footerWorktreeInfo = nullptr;
    QLabel *m_footerCommitInfo = nullptr;
    QLabel *m_statusAppPath = nullptr;
    QWidget *m_resourceChart = nullptr;
    QWidget *m_repoSizeChart = nullptr;
    QWidget *m_repoLinesChart = nullptr;
    QWidget *m_repoFilesChart = nullptr;
    QToolButton *m_repoRatchetButton = nullptr;
    qint64 m_repoStatsLastRefreshMs = 0;
    StallWatchdog *m_stallWatchdog = nullptr;
    QTimer *m_diagTimer = nullptr;
    int m_stallCount = 0;
    QStringList m_stallLog;          // recent stalls, each with its backtrace
    QString m_stallLogPath;          // durable on-disk stall log
    // Stall signatures already handed to an agent this session, so a recurring
    // freeze doesn't spawn a fresh agent task every time it fires.
    QSet<QString> m_autoFiledStallSignatures;
    bool m_highMemoryAlertArmed = true;
    QPointer<QDialog> m_highMemoryDialog;
    QTableWidget *m_highMemoryProcessTable = nullptr;
    QLabel *m_highMemoryProcessStatus = nullptr;
    QPointer<QProcess> m_highMemoryProcessQuery;
    QHash<qint64, QVector<double>> m_highMemoryRssHistory;
    qulonglong m_diagLastCpuTicks = 0;
    qint64 m_diagLastCpuMs = 0;
    qint64 m_fdPressureLastCheckMs = 0;
    bool m_fdPressureAlertArmed = true;

    int m_repoDetailIndex = -1;
    QButtonGroup *m_issueTabGroup = nullptr; // Issues / Milestones / Labels tabs
    QButtonGroup *m_repoDetailTabs = nullptr;
    QWidget *m_repoDetailChrome = nullptr;   // repo actions + repository tabs
    QWidget *m_repoFilesModeBar = nullptr;   // Code overview / Coves toggles
    QWidget *m_repoOverviewChrome = nullptr; // branch toolbar + commit strip
    ActivityRailButton *m_railCodeButton = nullptr;
    ActivityRailButton *m_railGitButton = nullptr;
    QPushButton *m_repoCodeTab = nullptr;
    QString m_repoCodeSizePath; // mirror the displayed "Code (N MB)" was computed for
    QPushButton *m_repoIssuesTab = nullptr;
    QPushButton *m_repoPullsTab = nullptr;
    QPushButton *m_repoDiscussionsTab = nullptr;
    QPushButton *m_repoActionsTab = nullptr;
    QPushButton *m_repoMirrorsTab = nullptr; // handle for the Mirror nodes (N) badge
    QPushButton *m_repoReleasesTab = nullptr; // handle for the Releases (N) badge
    QPushButton *m_repoBranchesTab = nullptr; // handle for the Branches (N) badge
    QStackedWidget *m_repoDetailStack = nullptr;
    int m_chatStackIndex = -1; // index of the Chat page in m_repoDetailStack
    int m_insightsTabIndex = -1; // index of the Insights page
    int m_branchesTabIndex = -1; // index of the Branches page
    int m_worktreesTabIndex = -1; // index of the Worktrees page (next to Branches)
    QPushButton *m_repoWorktreesTab = nullptr; // handle for the Worktrees (N) badge
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
    int m_projectsTabIndex = -1;
    int m_sizeMapTabIndex = -1;
    QWidget *m_sizeMapChart = nullptr;
    QLabel *m_sizeMapStatus = nullptr;
    QWidget *m_sizeMapWorkersBox = nullptr;
    QList<QLabel *> m_sizeMapWorkerLines;
    QLabel *m_sizeMapWorkerOverflow = nullptr;
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
    QPushButton *m_repoProjectsTab = nullptr; // handle for the Projects (N) badge
    QLabel *m_repoVisibilityHint = nullptr; // explains the current visibility
    QTableWidget *m_branchesTable = nullptr;
    QLabel *m_branchesSummary = nullptr;
    QPushButton *m_branchPullAllButton = nullptr; // safe bulk fast-forward action
    QPushButton *m_branchDeleteMergedButton = nullptr; // "Delete merged" header action
    QTextBrowser *m_branchDiffView = nullptr;
    QString m_branchDiffBranch;
    int m_branchDiffPullNumber = -1;
    int m_branchScopeDiffGen = 0;
    int m_branchDiffAgentSessionId = -1;
    QHash<QString, bool> m_branchConflictCache;
    QSet<QString> m_branchConflictProbes; // branches a worker is probing right now
    QHash<QString, BranchChangeStat> m_branchChangeStatsCache;
    int m_branchChangeStatsGen = 0;
    bool m_branchChangeStatsLoading = false;
    void startBranchConflictProbes(const QString &dir, const QString &base,
                                   const QList<QPair<QString, QString>> &probes);
    QByteArray m_branchDiffLastPatch;
    QString m_branchDiffLastEmpty;
    bool m_branchDiffLastValid = false;
    QHash<QString, BranchDiffCacheEntry> m_branchDiffCache;
    QStringList m_branchDiffCacheOrder; // oldest key first, for eviction
    qint64 m_branchDiffCacheBytes = 0;
    static constexpr int kBranchDiffCacheEntries = 8;
    static constexpr qint64 kBranchDiffCacheBytes = 8 * 1024 * 1024;
    QString branchDiffCacheKey(const QString &branch, const QString &workDir) const;
    void rememberBranchDiff(const QString &key, const QByteArray &patch,
                            const QString &emptyMessage);
    void forgetBranchDiff(const QString &branch); // after the branch is written to
    void clearBranchDiffCache();
    // Pass by value because git event-loop pumping can invalidate caller storage.
    void beginBranchDiffTransition(QString branch);
    void finishBranchDiffTransition();
    bool m_branchDiffPendingFade = false;
    bool m_branchDiffPaintedFromCache = false;
    QString branchDiffViewedContext(const QString &branch) const;
    QString m_branchDiffViewedContext;
    QString m_branchDiffWorkDir;
    QFrame *m_branchDiffActiveOutline = nullptr;
    QString m_branchActiveFile; // file CHANGES currently points at
    QFrame *m_branchDiffSticky = nullptr;
    QLabel *m_branchStickyPath = nullptr;
    QLabel *m_branchStickyControls = nullptr;
    QString m_branchStickyFile; // file the sticky bar currently mirrors
    QList<QPair<int, QString>> m_branchDiffFileSpans;
    QStringList m_branchDiffFilePaths;
    QStringList m_branchDiffFileAnchors;
    QHash<QString, QString> m_branchStickyLabelHtml;
    QString m_lastSourceControlDiffPath;
    QList<int> m_branchFileTops;
    QTimer *m_branchAutoViewedDebounce = nullptr;
    QWidget *m_branchDiffSearchBar = nullptr;
    QLineEdit *m_branchDiffSearchInput = nullptr;
    QLabel *m_branchDiffSearchCount = nullptr;
    QList<QTextCursor> m_branchDiffSearchMatches;
    int m_branchDiffSearchIndex = -1;
    QPushButton *m_branchSplitButton = nullptr;
    QPushButton *m_branchOpenPullButton = nullptr;
    QPushButton *m_branchesDeleteSelBtn = nullptr;
    QLabel *m_branchDetailLabel = nullptr;      // "<branch> -> <base> · N behind · M ahead"
    QPushButton *m_branchCloseButton = nullptr;
    QPushButton *m_branchOpenCodiumButton = nullptr; // "Open in Codium" (VSCodium)
    QPushButton *m_branchMergeEditorButton = nullptr; // "Merge editor" (resolve by hand)
    QPushButton *m_branchPullButton = nullptr;  // "Pull <base>" into the branch
    QPushButton *m_branchFixButton = nullptr;   // "Fix with agent" (conflicts only)
    QComboBox *m_branchFixAgentCombo = nullptr;
    QComboBox *m_branchFixModelCombo = nullptr;
    QPushButton *m_branchPrButton = nullptr;    // "Create PR" from the branch
    QPushButton *m_branchQueueButton = nullptr;
    QPushButton *m_branchMergeButton = nullptr; // "Merge to main"
    QPushButton *m_branchMergeDeleteButton = nullptr;
    QTableWidget *m_releasesTable = nullptr;
    QLabel *m_releasesSummary = nullptr;
    QTableWidget *m_artifactsTable = nullptr;
    QLabel *m_artifactsSummary = nullptr;
    QWidget *m_shortcutCardsHost = nullptr; // card rows, rebuilt by loadShortcutsPanel
    QLabel *m_shortcutsSummary = nullptr;
    QPlainTextEdit *m_shortcutOutput = nullptr; // live output of the running shortcut
    QLabel *m_shortcutRunStatus = nullptr;
    QPushButton *m_shortcutStopButton = nullptr;
    QProcess *m_shortcutProcess = nullptr; // running shortcut, if any
    QTableWidget *m_mirrorNodesTable = nullptr;
    QLabel *m_mirrorNodesSummary = nullptr;
    QCheckBox *m_mirrorNodesOnlineOnlyCheck = nullptr;
    QTimer *m_nodeLightTimer = nullptr;
    int m_nodeLightFrame = 0;
    QTimer *m_requestServedFlushTimer = nullptr;
    QTimer *m_mirrorPanelRosterTimer = nullptr;
    QHash<QString, CommitIdentity> m_commitIdentityCache;
    QPushButton *m_mirrorResetPinButton = nullptr;
    QPushButton *m_mirrorNodeServerButton = nullptr;
    QPushButton *m_mirrorNodeServerPowerButton = nullptr;
    QString m_repoBranch;
    RepoInfo m_repoInfo;
    QLabel *m_repoHeaderTitle = nullptr;
    QLabel *m_repoDetailNotice = nullptr;
    QLabel *m_repoDetailStatus = nullptr;
    QPushButton *m_notifyButton = nullptr;
    QPushButton *m_forkButton = nullptr;
    QPushButton *m_mirrorButton = nullptr;
    QPushButton *m_sourceButton = nullptr;
    QPushButton *m_repoOpenButton = nullptr; // open this repo on the web
    QMenu *m_mirrorMenu = nullptr;
    QMenu *m_sourceMenu = nullptr;
    QPushButton *m_branchButton = nullptr;
    QPushButton *m_branchesButton = nullptr;
    QPushButton *m_worktreesButton = nullptr; // "N worktrees" toggle in the Code toolbar
    QPushButton *m_remotesButton = nullptr;   // "N remotes" dropdown in the Code toolbar
    QPushButton *m_tagsButton = nullptr;
    QPushButton *m_filesModeOverviewButton = nullptr; // -> code overview
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
    // Most recently rendered Security-tab snapshot, cached so a "Run scan" click
    // can redraw the dependency card in its busy state immediately, without
    // re-running the (possibly slow) scan just to get a frame to render.
    RepoSecuritySnapshot m_lastRepoSecuritySnapshot;
    QLabel *m_qualitySummary = nullptr;
    QWidget *m_qualitySignalsPanel = nullptr;
    QGridLayout *m_qualitySignalsGrid = nullptr;
    QTableWidget *m_qualityFindingsTable = nullptr;
    QPushButton *m_qualityRefreshButton = nullptr;
    // Private vulnerability report form (Settings → Security tab).
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
    // Genie: the website's remote-MCP credentials, edited in the
    // same Settings → MCP page and used by the quick-add bar's "genie" button.
    QLineEdit *m_genieTokenEdit = nullptr;
    QLineEdit *m_genieOrgEdit = nullptr;
    QComboBox *m_genieWorkflowCombo = nullptr;
    QLabel *m_genieStatusLabel = nullptr;
    QTableWidget *m_commitsTable = nullptr;
    QString m_commitsLoadedRef;
    QString m_commitsLoadedTip;
    QString m_commitsLoadedMirrorTip;
    int m_commitsLoadGen = 0;
    // repo dir + ref + tip + issue-store signature that applyCommitIssueClosures()
    // last scanned. Reading 500 full commit messages and then every issue's signed
    // event log is expensive enough to show up in the stall log, and its result is
    // a pure function of those four things, so an unchanged key means there is
    // nothing to re-derive.
    QString m_commitClosureScanKey;
    mutable QStringList m_branchesCache;
    mutable QString m_branchesCacheDir;
    mutable qint64 m_branchesCacheTime = 0;
    mutable QString m_defaultBranchFastCache;
    mutable QString m_defaultBranchFastCacheDir;
    mutable qint64 m_defaultBranchFastCacheTime = 0;
    mutable QString m_defaultBranchFastCacheSig;
    QLineEdit *m_commitSearch = nullptr;       // filter the commit list by hash/summary
    QLineEdit *m_globalSearch = nullptr;
    QListWidget *m_globalSearchPopup = nullptr;
    QTimer *m_globalSearchTimer = nullptr;     // debounce keystrokes before rebuilding
    QString m_agentPageSearchMirror;
    struct NavPlace {
        int section = 0;
        int repoIndex = -1;
        int detailTab = -1;
        int overviewPage = -1; // files=0, Git=1, branches=2, worktrees=3
        QString branch;
        int subTab = -1;
        int itemNumber = -1;
        QString commit;    // Git view: the commit whose diff is open
        QString filePath;  // Code view: the file open in the editor
        QString overviewDir; // Code view: the directory the file list is showing
        QString conversation; // Chat: the room / DM on screen
        bool operator==(const NavPlace &o) const
        {
            return section == o.section && repoIndex == o.repoIndex &&
                   detailTab == o.detailTab && overviewPage == o.overviewPage &&
                   branch == o.branch && subTab == o.subTab &&
                   itemNumber == o.itemNumber && commit == o.commit &&
                   filePath == o.filePath && overviewDir == o.overviewDir &&
                   conversation == o.conversation;
        }
    };
    QPushButton *m_navBackButton = nullptr;
    QPushButton *m_navForwardButton = nullptr;
    QList<NavPlace> m_navHistory;
    int m_navHistoryIndex = -1;     // current position in m_navHistory
    bool m_navRestoring = false;    // suppress recording while replaying the trail
    bool m_navRecordPending = false; // a debounced capture is already queued
    QTreeWidget *m_searchResultsTree = nullptr;
    QLabel *m_searchResultsTitle = nullptr;
    QLabel *m_searchResultsStatus = nullptr;
    QPushButton *m_searchResultsStop = nullptr;
    QList<QProcess *> m_searchProcs; // running git searches (killed by Stop)
    int m_searchPending = 0;         // how many of those are still running
    QString m_searchPageQuery;
    QLabel *m_commitsUnsyncedBanner = nullptr; // "N commits not yet synced" banner
    QPushButton *m_commitsUnsyncedSyncButton = nullptr;
    QTreeWidget *m_commitsUnsyncedFiles = nullptr;
    bool m_commitsUnsyncedExpanded = false;
    QStringList m_commitsUnsyncedHashes; // pending commits, newest first
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
    QComboBox *m_insightsRangeCombo = nullptr; // activity time window selector
    QLabel *m_insightsActivityAxis = nullptr;  // "oldest <- ... -> newest" caption
    QPushButton *m_insightsRefreshButton = nullptr;
    static constexpr int kCommitWorkspaceChangesPage = 0;
    static constexpr int kCommitWorkspaceCommitPage = 1;
    static constexpr int kCommitWorkspaceRangePage = 2;
    QStackedWidget *m_gitFilesSlot = nullptr;
    QStackedWidget *m_gitHistorySlot = nullptr;
    void setCommitWorkspacePage(int page);
    QLabel *m_commitsCompareArrow = nullptr;
    QPushButton *m_commitsCompareBaseButton = nullptr;
    void updateCommitsCompareIndicator();
    QWidget *m_scmPanel = nullptr;
    QWidget *m_scmControlsPanel = nullptr;
    QTreeWidget *m_scmTree = nullptr;
    QPlainTextEdit *m_scmMessage = nullptr; // compact two-line commit/post draft
    QTextBrowser *m_scmDiff = nullptr;
    QLabel *m_scmCountLabel = nullptr;
    QLabel *m_scmViewedLabel = nullptr;  // "3 of 26 files viewed"
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
    QWidget *m_scmOutgoingPanel = nullptr;
    QLabel *m_scmOutgoingLabel = nullptr; // branch + pending commit count
    QPushButton *m_scmSyncButton = nullptr; // publish/push pending commits
    QWidget *m_scmSyncRow = nullptr;
    ElidingStatusLabel *m_scmSyncStatus = nullptr; // below button:
    int m_scmOutgoingGeneration = 0; // rejects late ahead-count callbacks
    quint64 m_scmStatusGeneration = 0; // rejects late `git status` callbacks
    bool m_scmOutgoingBlocking = false; // commits ahead, or a sync in flight
    int m_scmPendingChangeCount = 0;    // working-tree rows the panel would list
    QHash<int, QString> m_repoSyncActivity;
    void setRepoSyncActivity(int index, const QString &line);
    void clearRepoSyncActivity(int index);
    std::shared_ptr<QString> streamGitProgressActivity(QProcess *process,
                                                       int index,
                                                       const QString &prefix);
    static QString gitErrorsWithoutProgress(const QString &text);
    QPushButton *m_scmRefreshButton = nullptr;
    QPushButton *m_scmPrevButton = nullptr;   // jump to previous changed file
    QPushButton *m_scmNextButton = nullptr;   // jump to next changed file
    QPushButton *m_scmAutoViewedButton = nullptr;
    QLabel *m_scmEmptyNote = nullptr;
    QByteArray m_scmStatusCache;
    QString m_scmCombinedPatch;      // cached raw patch behind the render
    int m_scmCombinedStagedFiles = 0; // how many of its files are the staged half
    bool m_scmPatchValid = false;    // false until the patch is (re-)read from git
    QStringList m_scmSectionKeys;
    QStringList m_scmSectionAnchors; // "file-N" per section, aligned to the keys
    QStringList m_scmSectionPaths;   // repo-relative path per section
    QList<int> m_scmFileTops;        // cached absolute y of each section header
    QHash<QString, QString> m_scmStickyLabelHtml; // section key -> sticky label
    QFrame *m_scmDiffActiveOutline = nullptr;
    QString m_scmActiveSectionKey;
    QString m_scmPendingScrollKey;
    QString m_scmDiffRenderKey;      // skip the re-layout when nothing changed
    QString m_scmDiffSourceKey;
    QFrame *m_scmStickyHeader = nullptr;
    QLabel *m_scmStickyPath = nullptr;
    QLabel *m_scmStickyControls = nullptr;
    QString m_scmStickySection;      // section key shown in the sticky header
    QTimer *m_scmAutoViewedDebounce = nullptr;
    int m_scmLastAutoViewedScrollValue = 0;
    int m_scmAutoViewedScrollDirection = 1; // +1 down, -1 back up
    bool m_scmApplyingAutoViewed = false; // ignore render/anchor scroll signals
    bool m_scmSuppressFileScroll = false;
    QStackedWidget *m_commitsStack = nullptr;
    QLabel *m_commitTitle = nullptr;
    QLabel *m_commitMeta = nullptr;
    QLabel *m_commitMessage = nullptr;
    QLabel *m_commitFilesSummary = nullptr;
    QListWidget *m_commitFileList = nullptr;
    QTextBrowser *m_commitDiffView = nullptr;
    QWidget *m_commitDiffSpinner = nullptr; // inline spinner by the files heading
    int m_commitLoadGen = 0;
    QPushButton *m_commitPrevButton = nullptr;
    QPushButton *m_commitNextButton = nullptr;
    QPushButton *m_commitDownloadButton = nullptr;
    QPushButton *m_commitDeleteButton = nullptr; // drop this commit from history
    QPushButton *m_commitRevertButton = nullptr; // commit the inverse of this one
    QPushButton *m_commitSplitButton = nullptr; // toggle unified <-> side-by-side
    QString m_currentCommitHash; // full hash shown in the detail view
    int m_currentCommitRow = -1; // row in m_commitsTable the detail view is showing
    QStackedWidget *m_filesStack = nullptr; // 0 overview, 1 secure cove
    QString m_overviewPath;                 // current directory in the overview
    // Signature (repo|path|branch|HEAD) of the overview currently rendered, so
    // re-entering the repo screen unchanged skips the expensive git re-read.
    QString m_overviewLoadedKey;
    // Re-entrancy guard: loadRepoOverview's git reads pump the event loop
    // (GitKeepAlive), so a queued slot serviced mid-load could call back in and
    // interleave a second rebuild with the first.
    bool m_overviewLoading = false;
    int m_treeLoadedForIndex = -1;          // repo whose explorer tree is built
    QLabel *m_commitBar = nullptr;
    QStackedWidget *m_overviewBodyStack = nullptr;
    QLabel *m_overviewCrumb = nullptr;
    QString m_commitBarStatusHash;
    QString m_commitBarBodyHtml;
    QTreeWidget *m_overviewList = nullptr;
    struct OverviewRow {
        QString name;
        QString path;
        bool isDir = false;
        qint64 size = 0;     // blob bytes (recursive sum for directories)
        qint64 loc = 0;      // lines of code (recursive sum for directories)
        qint64 fileCount = 0; // number of files (recursive) — shown for directories
        qint64 commitTs = 0; // last commit unix time that touched this entry
        QString subject;     // last commit subject
        QString whenText;    // relative "x ago"
    };
    QList<OverviewRow> m_overviewRows;
    qint64 m_overviewRepoBytes = 0; // whole-repo blob total (size-bar denominator)
    QString m_overviewSortKey = QStringLiteral("name");
    bool m_overviewSortDesc = false;
    void populateOverviewTree(); // (re)fill m_overviewList from m_overviewRows
    void showOverviewLoadingPlaceholders();
    QTextBrowser *m_readmeView = nullptr;
    QTreeWidget *m_repoFileTree = nullptr;
    QTabWidget *m_repoFileTabs = nullptr;
    QPushButton *m_repoFileCommitButton = nullptr;
    QPushButton *m_repoFilePullButton = nullptr;
    QPushButton *m_repoFileSaveButton = nullptr;
    QPushButton *m_repoFileHistoryButton = nullptr;
    QPushButton *m_repoFilePreviewButton = nullptr; // toggle markdown source/render
    QHash<QString, QWidget *> m_openFileTabs;
    QStackedWidget *m_filesTreeStack = nullptr; // 0 filesystem, 1 repository
    QPushButton *m_filesRepoTreeButton = nullptr; // -> the repository's tree
    QTreeWidget *m_fileExplorerTree = nullptr;
    QPlainTextEdit *m_fileExplorerPreview = nullptr;
    QLineEdit *m_fileExplorerPath = nullptr;
    QLabel *m_fileExplorerStatus = nullptr;
    QCheckBox *m_fileExplorerHidden = nullptr;
    QString m_fileExplorerRoot;
    QLineEdit *m_filesCommitMessage = nullptr;
    QPushButton *m_filesCommitButton = nullptr;
    QPushButton *m_filesPushButton = nullptr;

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
    QHash<QString, QWidget *> m_openCoveExplorerTabs; // coveId\x1fdocPath -> editor
    QString m_coveExplorerCurrentId;

    QTableWidget *m_discussionTable = nullptr;
    QLineEdit *m_discussionSearch = nullptr;
    QComboBox *m_discussionCategoryFilter = nullptr;
    QPushButton *m_discussionNewButton = nullptr;
    QPushButton *m_discussionSyncButton = nullptr;
    QPushButton *m_discussionCloseButton = nullptr;
    QPushButton *m_discussionArchiveButton = nullptr;
    QPushButton *m_discussionDeleteButton = nullptr;
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
    QHash<QString, int> m_inboxAuthRejections;
    // a refill notification waiting to ride the next signed
    // heartbeat (kept as flags, not fired directly, so it retries on the
    // periodic heartbeat timer if the immediate send fails).
    bool m_pendingCreditsRefilled5h = false;
    bool m_pendingCreditsRefilledWeekly = false;
    QHash<QString, QTimer *> m_usageLimitReminderTimers;

    // --- Cove (encrypted vault) UI + session state ----------------------------
    QWidget *m_coveSection = nullptr;        // repo Settings "Coves" group
    QListWidget *m_coveList = nullptr;       // coves in the open repo (lock state)
    QLineEdit *m_covePasswordEdit = nullptr; // per-repo unlock password field
    QPushButton *m_covePwRevealBtn = nullptr;// reveal pw (source-of-truth only)
    QCheckBox *m_coveAutoOpenCheck = nullptr;// per-repo auto-open toggle
    QLabel *m_coveEmptyHint = nullptr;
    // Cove id -> the password that unlocked it this session (memory only). Lets
    // "auto-show" reveal a cove without re-prompting and re-encrypt on save.
    QHash<QString, QString> m_coveSessionPasswords;
    QSet<QString> m_coveFailedUnlocks;

    QTableWidget *m_pullTable = nullptr;
    QLineEdit *m_pullSearch = nullptr;
    QPushButton *m_pullNewButton = nullptr;
    QPushButton *m_pullChooseDirButton = nullptr;
    QPushButton *m_pullImportButton = nullptr;
    QPushButton *m_pullSyncButton = nullptr;
    QPushButton *m_pullDeleteAllMergedButton = nullptr; // bulk-delete merged PRs + branches
    QWidget *m_pullDetail = nullptr;
    QPushButton *m_pullHideDetailButton = nullptr;
    bool m_pullDetailHidden = false;
    QLabel *m_pullTitle = nullptr;
    QLabel *m_pullMeta = nullptr;
    QLabel *m_pullMergeStatus = nullptr; // conflict / ready-to-merge banner
    QPushButton *m_pullUpdateButton = nullptr;
    QPushButton *m_pullMergeButton = nullptr;
    QPushButton *m_pullResolveButton = nullptr; // opens the conflict merge editor
    QPushButton *m_pullFixButton = nullptr;
    QPushButton *m_pullFixConflictsButton = nullptr;
    QPushButton *m_pullReviewAiButton = nullptr;
    QPushButton *m_pullFixAllAiButton = nullptr;
    QPushButton *m_pullEditFileButton = nullptr; // edit selected file on PR branch
    QPushButton *m_pullDeleteFileButton = nullptr; // delete selected file on PR branch
    QPushButton *m_pullCloseButton = nullptr;
    QPushButton *m_pullReopenButton = nullptr;
    QPushButton *m_pullSendToSourceButton = nullptr;
    QPushButton *m_pullDeleteButton = nullptr;
    QPushButton *m_pullDeleteBranchButton = nullptr; // delete the PR and its head branch
    QPushButton *m_pullMergeDeleteButton = nullptr;  // merge, then delete the PR + branch
    QPushButton *m_pullQueueButton = nullptr;        // add/remove this PR in the merge queue
    QPushButton *m_pullPreviewButton = nullptr;      // build the PR and launch the app
    QDialog *m_pullPreviewDialog = nullptr;          // live build log for the preview
    bool m_pullDeleteConfirmPending = false;
    bool m_pullDeleteInProgress = false; // a deletePull worker thread is running

    QWidget *m_mergeQueuePanel = nullptr;
    QLabel *m_mergeQueueSummary = nullptr;
    QListWidget *m_mergeQueueList = nullptr;
    QPushButton *m_mergeQueuePauseButton = nullptr;
    QPushButton *m_mergeQueueUpButton = nullptr;
    QPushButton *m_mergeQueueDownButton = nullptr;
    QPushButton *m_mergeQueueRemoveButton = nullptr;
    QPushButton *m_mergeQueueClearButton = nullptr;
    QTimer *m_mergeQueueTimer = nullptr;
    bool m_mergeQueueBusy = false;
    QListWidget *m_pullFiles = nullptr;
    QComboBox *m_pullFileAuthorFilter = nullptr;
    QHash<QString, bool> m_pullFileAuthorship;
    QPushButton *m_pullPrevButton = nullptr; // jump to previous change in the PR
    QPushButton *m_pullNextButton = nullptr; // jump to next change in the PR
    QTextBrowser *m_pullDiff = nullptr;
    QWidget *m_pullDiffSearchBar = nullptr;
    QLineEdit *m_pullDiffSearchInput = nullptr;
    QLabel *m_pullDiffSearchCount = nullptr;
    QList<QTextCursor> m_pullDiffSearchMatches;
    int m_pullDiffSearchIndex = -1;
    // Signature (stylesheet + html) of what m_pullDiff currently shows, so
    // renderPullDiff can skip the costly QTextDocument table re-layout when a
    // refresh/poll re-renders the same file with unchanged content. Cleared
    // whenever the widget is set to something other than a rendered diff.
    QString m_pullDiffRenderKey;
    QString m_pullDiffSourceKey;
    QHash<QString, QString> m_pullFileAnchors;
    QStringList m_pullFileOrder;
    bool m_pullSuppressFileScroll = false;
    QPushButton *m_pullAutoViewedButton = nullptr;
    QTimer *m_pullAutoViewedDebounce = nullptr;
    QFrame *m_pullStickyHeader = nullptr;
    QLabel *m_pullStickyPath = nullptr;
    QLabel *m_pullStickyControls = nullptr;
    QString m_pullStickyFile; // file path currently shown in the sticky header
    QHash<QString, QString> m_pullStickyLabelHtml;
    QList<int> m_pullFileTops;
    int m_diffFontPt = 12; // diff viewer text size (the +/- zoom control)
    QList<QTextEdit *> m_diffViews;
    QHash<QTextEdit *, int> m_diffRestoreScroll;
    QPushButton *m_pullSplitButton = nullptr; // toggle unified <-> side-by-side
    QListWidget *m_pullCommitsList = nullptr;  // commits that make up the PR
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
    QLabel *m_pullChecksSummary = nullptr; // compact pass/fail/running card in thread
    QLabel *m_pullConflictDetails = nullptr; // conflicting-files card above the composer
    MarkdownEditor *m_pullComposer = nullptr;
    QPushButton *m_pullCommentButton = nullptr;
    QPushButton *m_pullApproveButton = nullptr;
    QPushButton *m_pullRequestChangesButton = nullptr;
    QPushButton *m_pullConversationMergeButton = nullptr;
    QPushButton *m_pullConversationCloseButton = nullptr;
    QPushButton *m_pullConversationDeleteButton = nullptr;
    QWidget *m_pullAgentRevisionRow = nullptr;
    QLineEdit *m_pullAgentRevisionEdit = nullptr;
    QPushButton *m_pullSendToAgentButton = nullptr;
    QPushButton *m_pullLinkIssueButton = nullptr; // "Link issue" in the PR header
    QLabel *m_pullLinksValue = nullptr;           // linked issues card in the thread
    QTableWidget *m_pullChecksTable = nullptr;
    QPlainTextEdit *m_pullChecksLog = nullptr;
    QPushButton *m_pullRunChecksButton = nullptr;
    QList<PullRequest> m_currentPulls;
    QHash<QString, QString> m_pullFileDiffs; // current PR: file path -> diff text
    QHash<int, bool> m_pullConflictByNumber;
    QString m_pullConflictCacheBaseTip;
    struct PullConflictEntry {
        QString fingerprint;
        bool conflict = false;
        QStringList conflictFiles;
    };
    QHash<int, PullConflictEntry> m_pullConflictCache;
    // Agent attribution per PR, read from the ForkMesh-Agent trailer in the
    // signed commit series. Computed once per load on a worker rather than
    // inside refreshPullList(), which called pullAgentProvenance() for every
    // visible row: a PR with no trailer scans its whole (multi-megabyte) mbox to
    // the end before answering "no", and the stall log caught that table build
    // at 1.15s. Missing entry simply means "no badge yet".
    QHash<int, PullAgentProvenance> m_pullProvenance;
    quint64 m_pullProvenanceGen = 0;
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
        QString kind;   // "issue" | "chat" | "action" | … (defaults from link)
        QString actor;  // who caused it, when known
        QString actorId;
        QString repo;   // "owner/name", when it is about a repository
        qint64 id = 0;  // stable row identity, so one row can be deleted
        forkmesh::PingSync sync = forkmesh::PingSync::LocalOnly;
        QString syncReason; // the specific "why", e.g. "signed out"
        bool syncDecided = false;
        bool quiet = false;
    };
    ActionStore *m_actionStore = nullptr;
    QList<ActionRunner *> m_actionRunners;
    QFileSystemWatcher *m_actionSpoolWatcher = nullptr;
    QList<ActionRun> m_actionRuns;   // loaded history, newest first
    QList<int> m_actionQueue;        // run ids queued for execution
    QSet<QString> m_explicitActionPushes;
    QSet<int> m_actionWaitingRuns;   // queued ids already logged as `needs:`-blocked
    // The encrypted mirror materialization a run is executing out of (see
    // pinActionMirror). Held until the run finishes so a concurrent re-seal
    // cannot delete the served mirror mid-build.
    struct ActionMirrorPin {
        std::shared_ptr<void> materialization; // keeps the directory alive
        QString path;                          // mirror the run was handed
        QString owner;
        QString name;
    };
    QHash<int, ActionMirrorPin> m_actionMirrorPins; // run id -> pinned mirror
    QString m_mirrorActionsConfigGeneration;
    QString m_mirrorActionsRuntimeState;
    qint64 m_mirrorActionsRuntimeStateWrittenAtMs = 0;
    QTimer *m_mirrorActionsSummaryTimer = nullptr;
    qint64 m_mirrorActionsSummaryAttemptedAtMs = 0;
    qint64 m_lastExternalActionsScanMs = 0;
    QList<AppNotification> m_notifications;
    qint64 m_nextNotificationId = 1;
    qint64 m_pingToastId = 0;
    QTimer *m_notificationJournalTimer = nullptr; // debounced journal write
    QSet<QString> m_flashedWebAlertIds;
    bool m_webAlertsBaselineLoaded = false;
    QPushButton *m_notificationButton = nullptr;
    QTableWidget *m_notificationsTable = nullptr; // sortable Notifications page
    QJsonArray m_webAlerts;
    int m_webAlertsUnread = 0;
    bool m_webAlertsLoading = false;
    qint64 m_webAlertsFetchedAtMs = 0;
    int m_selectedRunId = -1;
    QListWidget *m_actionWorkflowList = nullptr; // available actions (left column)
    QComboBox *m_actionNodeCombo = nullptr;      // node the selected action runs on
    QLabel *m_actionNodeLabel = nullptr;         // caption above that dropdown
    QString m_selectedWorkflowFilter;            // workflow path filter, empty = all
    QList<ActionWorkflow> m_repoWorkflows;       // parsed workflows for the open repo
    int m_workflowCountLoadGen = 0;
    QTimer *m_openRepoRefreshTimer = nullptr;
    QTimer *m_agentsSpinTimer = nullptr; // refreshes live Agents metadata
    int m_agentSpinTicks = 0; // paces the detail header's run-stat refresh
    QTimer *m_actionsSpinTimer = nullptr; // refreshes live Actions status icons
    int m_actionSpinTicks = 0; // paces the actions table status icon spin
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
    // Settings: full-width variables/secrets list.
    QWidget *m_varsList = nullptr;
    QVBoxLayout *m_varsListLayout = nullptr;
    // When true, the variables list shows secret values in clear text instead
    // of password masking. Toggled by the Reveal/Hide button.
    bool m_varsRevealed = false;
    QPushButton *m_varsRevealButton = nullptr;
    QCheckBox *m_actionsEnabledCheck = nullptr;
    QCheckBox *m_settingsActionsCheck = nullptr;
    QCheckBox *m_actionsAutoApproveCheck = nullptr;
    QCheckBox *m_settingsAutoApproveCheck = nullptr;
    QCheckBox *m_settingsRequirePeerApprovalCheck = nullptr;
    QCheckBox *m_settingsMergeQueueCheck = nullptr;
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
        bool claudeCode = false;
        QProcess *process = nullptr; // running CLI (claudeCode mode), else null
        bool branchMerge = false;
        QString branch;        // target branch being brought up to date
        QString baseBranch;    // base branch merged into it
        QString restoreBranch; // branch to check back out when done
        bool agentEdit = false;
        QString agentEditPrompt;
        int agentEditFindings = 0;
        QString promptFile;    // prompt handed to the CLI; removed when the run ends
    };
    AiConflictFix *m_aiFix = nullptr;
    void aiFixResolveNextFile();           // send the next conflicted file to the model
    void aiFixRunClaudeCode();             // run the `claude` CLI over the conflict tree
    void aiFixApplyResolved(const QString &resolved); // write back + advance
    void aiFixFinish();                    // commit to branch, mark session success
    void aiFixFail(const QString &message); // abort the merge, mark session failed
    void aiFixLog(const QString &text);    // stream a line into the agent session log
    void aiFixSetSessionStatus(const QString &status, const QString &error = QString());
    // In-flight AI code review: one model
    // call over the PR's diff whose findings land as signed review threads. Null
    // when no review is running. Read-only towards the repo, so unlike m_aiFix it
    // holds no PullStore/git state — just the session bookkeeping.
    struct AiPullReview {
        int number = 0;       // PR under review
        int repoIndex = -1;
        int sessionId = 0;    // backing agent session (Agents-tab visibility)
        QString provider;     // "claude" | "openai" | "claude-code"
        QString model;
        double costUsd = 0.0;
        qint64 inTokens = 0;
        qint64 outTokens = 0;
        QProcess *process = nullptr; // running CLI (claude-code mode), else null
        QString output;              // accumulated CLI stdout (claude-code mode)
        QString promptFile;          // prompt handed to the CLI; removed on finish
    };
    AiPullReview *m_aiReview = nullptr;
    void reviewCurrentPullWithAi();        // "Review with AI" on the PR header
    void aiReviewRunClaudeCode(const QString &prompt); // CLI fallback (no API key)
    void aiReviewHandleReply(const QString &text); // parse findings + post threads
    void aiReviewFail(const QString &message);
    void aiReviewLog(const QString &text);
    void aiReviewSetSessionStatus(const QString &status,
                                  const QString &error = QString());
    void applyPullSuggestionFix(const QString &threadId);
    void fixCurrentPullFindingsWithAgent();
    QWidget *m_agentsPage = nullptr; // reserves room for the docked full-width composer
    QTableWidget *m_agentTable = nullptr;
    struct AgentTranscriptHit {
        int count = 0;
        QString snippet;
    };
    QTimer *m_agentTranscriptSearchTimer = nullptr;
    QString m_agentTranscriptSearchQuery;
    QString m_agentTranscriptSearchRepo;
    QHash<int, AgentTranscriptHit> m_agentTranscriptHits; // session id -> hit
    int m_agentTranscriptSearchGen = 0;
    void scheduleAgentTranscriptSearch(); // (re)start the debounce
    void runAgentTranscriptSearch();      // scan transcripts off-thread
    AgentTranscriptHit agentTranscriptHit(int sessionId) const;
    QString agentTranscriptSearchRepoKey() const;
    QString agentFilterQuery() const;
    QString agentTranscriptQuery() const;
    QHash<int, QStringList> m_agentSessionImages; // session id -> image paths
    QHash<int, QString> m_agentImageStamps;       // session id -> scanned stamp
    int m_agentImageScanGen = 0;
    bool m_agentImageScanRunning = false;
    bool m_agentImageScanQueued = false;
    bool m_agentImageScanPending = true;
    QHash<QString, QIcon> m_agentThumbnails;
    QSet<QString> m_agentThumbnailsPending;       // decodes in flight
    void scanAgentSessionImages(const QList<AgentSession> &sessions,
                                const QString &owner, const QString &name);
    QStringList agentSessionImages(int sessionId) const;
    QIcon agentThumbnail(const QString &path);
    void applyAgentThumbnail(const QString &path);
    void showAgentSessionImages(int sessionId);
    QComboBox *m_agentComposeRepo = nullptr;
    QLineEdit *m_agentComposePrompt = nullptr;
    QComboBox *m_agentComposeProvider = nullptr;
    QPushButton *m_agentComposeButton = nullptr;
    void startAgentFromComposer();
    QWidget *m_agentDetail = nullptr; // collapsible detail panel (hidden until a row is picked)
    QPushButton *m_agentHideDetailButton = nullptr;
    bool m_agentDetailHidden = false;
    QLabel *m_agentTitle = nullptr;
    QWidget *m_agentPromptImages = nullptr;
    QStringList m_agentPromptImagePaths;
    int m_agentPromptImageSession = -1;
    void renderAgentPromptImages(int sessionId);
    QToolButton *m_agentStatusPill = nullptr; // compact model + outcome control
    QLabel *m_agentMeta = nullptr;
    QFrame *m_agentMetaPopup = nullptr;
    QPushButton *m_agentPopOutButton = nullptr;
    QLabel *m_agentNetPanel = nullptr;   // live API-traffic graphic
    QPushButton *m_agentViewPrButton = nullptr;
    QPushButton *m_agentCreatePrButton = nullptr;
    QPushButton *m_agentCreateIssueButton = nullptr;
    QPlainTextEdit *m_agentLog = nullptr;
    QStackedWidget *m_agentOutputStack = nullptr; // log (0) | embedded terminal (1)
    TerminalWidget *m_agentTerminal = nullptr;  // Claude Code runs here
    int m_terminalSessionId = -1; // session currently driving the embedded terminal
    // Makes the app act as the IDE the `claude` CLI connects to:
    // a localhost WebSocket/JSON-RPC server advertised via ~/.claude/ide. Lazily
    // created on the first Claude Code terminal launch.
    ClaudeIdeBridge *m_ideBridge = nullptr;
    ClaudeIdeBridge *ensureIdeBridge();
    void onClaudeOpenDiff(const QString &tabName, const QString &oldPath,
                          const QString &newPath, const QString &newContents);
    ClaudeTranscriptView *m_agentTranscript = nullptr;
    ActivityRailButton *m_agentOutputModeButton = nullptr;
    bool m_agentRawOutputMode = false;
    void setAgentRawOutputMode(bool raw);
    QLineEdit *m_transcriptSearch = nullptr;
    QLabel *m_transcriptSearchCount = nullptr;
    QListWidget *m_agentFilesList = nullptr;     // files edited in this session
    QWidget *m_agentFilesPanel = nullptr;        // wraps the list + heading
    QTabWidget *m_agentDetailTabs = nullptr;
    int m_agentFilesTabIndex = -1;               // tab index of "Files changed"
    QTextBrowser *m_agentDiffView = nullptr;     // diff viewer in the files tab
    forkmesh::ui::DiffFileNavigator *m_agentDiffNav = nullptr; // sticky header + scroll<->select
    QLabel *m_agentFilesChangedSummary = nullptr; // "N files changed" line
    QLabel *m_agentCommitsHeading = nullptr;     // "Commits" heading over the list
    QListWidget *m_agentCommitsList = nullptr;   // this branch's commits, ahead of base
    int m_agentDiffRenderedSession = -1;
    QString m_agentDiffLastHtml;
    QString m_agentDiffRenderKey;
    QByteArray m_agentDiffRenderedPatch;
    QPushButton *m_agentMergeButton = nullptr;   // worktree: merge into main
    QPushButton *m_agentMergeDeleteButton = nullptr; // merge + delete agent too
    QPushButton *m_agentUpdateButton = nullptr;  // worktree: update from main
    QPushButton *m_agentWtDeleteButton = nullptr; // worktree: delete worktree+branch
    QTimer *m_agentHourlyTimer = nullptr;        // refreshes spend + files hourly
    // relay owner-encrypted session snapshots and drain encrypted
    // steering prompts. m_agentSyncDebounceTimer is a singleShot
    // re-armed after a status flip so a burst of updates coalesces into one push.
    QTimer *m_agentSyncPushTimer = nullptr;
    QTimer *m_agentSyncDebounceTimer = nullptr;
    QHash<QString, QByteArray> m_lastAgentPushPayload;
    QSet<QString> m_agentE2EEReady;
    QSet<QString> m_agentE2EEInFlight;
    QSet<QString> m_orgAgentJobsInFlight;
    QSet<QString> m_orgAgentJobDrainsInFlight;
    QSet<QString> m_orgAgentJobDrainsPending;
    // local AgentSession id -> start-job transport needed to publish terminal
    // status through the same authenticated lease after the run finishes. The
    // same object is persisted on AgentSession for restart recovery.
    QHash<int, QJsonObject> m_orgAgentBindings;
    QHash<int, QString> m_orgTaskAgentStatusSent;
    QHash<int, QString> m_orgTaskAgentStatusPending;
    bool m_orgTaskAgentStatusFlushQueued = false;
    QList<QList<QPair<int, QString>>> m_orgTaskAgentStatusInFlight;
    QTimer *m_orgTaskAgentStatusAckTimer = nullptr;
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
    QHash<int, qint64> m_sessionTokens; // live token total per session, for the list
    QHash<int, AgentScannerState> m_scannerStates;
    QTimer *m_scannerTimer = nullptr;
    QHash<int, AgentDiffStat> m_agentDiffStats;
    QHash<int, QString> m_agentDiffSig;
    bool m_agentDiffRefreshPending = false;
    bool m_agentDiffStatsRefreshing = false;
    bool m_agentDiffStatsRefreshQueued = false;
    int m_agentDiffStatsGen = 0;
    // Re-entrancy guard for refreshAgentTable(): its cold-cache Diff cells shell
    // git and pump the event loop (GitKeepAlive), so a queued slot can re-enter
    // and corrupt the half-built table unless we skip the nested rebuild.
    bool m_agentTableRefreshing = false;
    QHash<int, QString> m_lastAssistantText; // last assistant prose, for waiting/question
    QSet<int> m_agentCompletionChecks;
    QHash<int, int> m_agentCompletionPollCounts;
    QHash<int, int> m_agentRelaunchAttempts;
    void notifyAgentWaiting(int sessionId, bool needsPermission);
    // The counterpart for a run that reached the end: a celebration card
    // carrying the agent's own icon and the summary it signed off with, plus a
    // green pulse round the window. Every completion path funnels
    // through here, and m_agentDoneNotified keeps a re-fired signal or a requeue
    // from celebrating the same run twice.
    void notifyAgentDone(int sessionId);
    QString agentClosingSummary(int sessionId) const;
    QSet<int> m_agentDoneNotified; // sessions already celebrated; cleared on a new turn
    bool applyCliExitWithoutResult(int sessionId, bool codex, int exitCode);
    void markAgentSessionRunning(int sessionId);
    QHash<int, QStringList> m_streamFiles;
    QHash<int, QString> m_streamWorktree;        // sessionId -> worktree path
    QHash<int, QThread *> m_worktreeTeardown;
    QHash<int, QStringList> m_streamPending;
    // sessionWorkdir() cache for *reloaded* sessions (not in m_streamWorktree):
    // resolving their worktree shells `git worktree list`, which the Files-changed
    // panel drove on every transcript turn — a per-event subprocess that stalled
    // the GUI thread. A session's branch->worktree binding is fixed
    // for its lifetime, so cache it; re-resolve only if the path was since removed.
    QHash<int, QString> m_sessionWorkdirCache;   // sessionId -> resolved worktree ("" = none)
    QHash<int, QString> m_pendingSteerMessage;
    QHash<int, QString> m_inFlightSteerMessage;
    QHash<int, AgentSession> m_streamSessionInfo;
    QHash<int, QString> m_streamAccountId;
    void startCliTranscript(AgentSession &session, const Issue &issue,
                            const QString &repoPath,
                            const QString &customPrompt = QString(),
                            bool switchToTab = true);
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
    QString agentCliConversationId(int sessionId);
    struct AgentTerminalHandoff {
        bool ok = false;
        QString program;
        QStringList args;
        QString cwd;
        QMap<QString, QString> env;
        QString conversationId;
        QString blocker;
    };
    AgentTerminalHandoff agentTerminalHandoff(int sessionId);
    void popOutAgentSessionToTerminal(int sessionId);
    void renderTranscriptForSession(int sessionId);
    void loadEarlierTranscriptEvents();
    void reapplyTranscriptSearch();
    void refreshAgentFilesPanel(int sessionId);
    void populateAgentFilesPanel(int sessionId, const QStringList &diffFiles);
    void scheduleAgentFilesDiff(int sessionId);
    struct AgentDiffProbe {
        QByteArray patch;        // git diff <base>, limited to agent-owned paths
        bool patchOk = false;    // that read succeeded (else keep the old view)
        QSet<QString> uncommitted; // paths with working-tree changes / untracked
        QSet<QString> ownedPaths;  // paths from patch-unique, non-merge commits
        QStringList commitLines; // patch-unique "abc1234 subject" entries
        int behind = 0;          // commits the base branch has that we don't
        int pending = 0;         // async probes still in flight
    };
    void renderAgentDiff(int sessionId, const AgentDiffProbe &probe);
    void applyAgentDiff(int sessionId, const AgentDiffProbe &probe,
                        const QList<forkmesh::ui::DiffFileEntry> &files,
                        const QString &shown);
    void updateAgentFilesTabState(int sessionId);
    QString sessionBaseRef(int sessionId);
    QString sessionBaseBranch(int sessionId);
    QString sessionDiffBase(int sessionId, const QString &dir);
    QTimer *m_agentFilesDiffTimer = nullptr; // debounces the async working-tree diff
    void maybeCreatePullForStreamSession(int sessionId);
    // Land a finished agent session's change as a pull request. On the
    // source of truth (we own the repo with a working tree) the PR is created and
    // committed locally. On a mirror node we can't write the owner's repo, so the
    // PR is signed and delivered to the owner's relay inbox instead — so a looper
    // running on a mirror still gets its work to the source of truth. `commits` is
    // the optional format-patch mbox the owner replays to preserve authorship;
    // pass empty when none is available (the owner synthesizes one from the patch).
    // Takes the session by value: creating the PR pumps the GUI event loop, and a
    // reloadAgents() during the pump would leave a reference dangling.
    void landAgentPullForSession(AgentSession session, const QString &patch,
                                 const QString &commits);
    bool isStreamTranscriptSession(int sessionId) const;
    void ensureStreamEventsLoaded(int sessionId);
    bool ensureStreamEventsLoadedAsync(int sessionId);
    QSet<int> m_streamEventsLoading; // async loads in flight
    QSet<int> m_streamEventsAbsent;  // probed: nothing persisted on disk
    void stopStreamSession(int sessionId, bool refreshUi = true);
    void purgeSessionState(int sessionId);
    QString sessionWorkdir(int sessionId);
    // A session's dedicated worktree path ("" when its branch has no separate
    // worktree / is the main checkout), resolved without re-shelling `git worktree
    // list` on every click. Sessions launched this run know it from
    // m_streamWorktree; reloaded ones resolve once via git and cache it for the
    // session's lifetime (m_sessionWorkdirCache). See the definition for why the
    // click path leaned on this.
    QString cachedSessionWorktree(int sessionId, const QString &repoLocal,
                                  const QString &branch);
    void cleanupStreamWorktree(int sessionId);
    void maybeAutoMergeForSession(int sessionId);

    // ---- Organization tasks for prompted runs ------------------
    // A prompt typed here starts an agent locally; with the composer's "Task"
    // toggle on it also opens a task in the organization so the run is visible
    // beyond this desktop. The task carries the run's provenance — the bot that
    // launched it, the bot that reported it finished, and the model, permission
    // mode, and reasoning strength it used.

    QString agentBotLabel(const QString &provider) const;
    QString composerAgentStrength() const;
    // Authenticate one task write: the account session token when a password
    // login produced one, otherwise a proof signed with this node's account key
    // appended to `url` as node/ts/sig. The silent-auth launch path has no
    // token at all, so without the signed fallback the Task toggle would do
    // nothing for most desktops. `resource` names the task a proof is bound to
    // (empty for the collection). False => this node can present neither.
    bool authenticateOrgTaskRequest(QUrl &url, QNetworkRequest &request,
                                    const QString &proofPrefix,
                                    const QString &resource) const;
    // The node/ts/sig triple behind that signature, for a write that has no
    // URL to hang it on — the batched agent-status report travels as a node
    // event socket frame. Same canonical string, so the relay verifies it with
    // the same code either way. False => this node cannot sign.
    bool signOrgTaskProof(const QString &proofPrefix, const QString &resource,
                          QString *node, QString *ts, QString *sig) const;
    void openOrgTaskForSession(const AgentSession &session);
    // Queue one session's live state for the board. The write itself is
    // batched: every queued session goes out together in one signed
    // agent-status report, so a restart publishes the whole fleet at once
    // instead of one request per session. That report rides the
    // node event socket this desktop already holds — no HTTP request at all —
    // and falls back to POST /api/tasks/agent-status when the socket is down
    // or the relay is too old to accept the frame.
    void syncOrgTaskAgentStatus(int sessionId);
    void flushOrgTaskAgentStatus();
    void applyOrgTaskAgentStatusResult(const QList<QPair<int, QString>> &sent,
                                       bool ok, const QJsonArray &results);
    void onOrgTaskAgentStatusFrame(bool ok, const QJsonArray &results);
    void requeueOrgTaskAgentStatusInFlight();
    void completeOrgTaskForSession(int sessionId,
                                   const QString &followUp = QString());
    void recordOrgTaskFields(int sessionId, const QString &taskId,
                             const QString &finishedByBot);

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
    void deleteExternalSession(int sessionId); // kill the CLI process, then unsurface
    int externalTempIdFor(const QString &uuid);
    bool isExternalSession(int sessionId) const { return sessionId <= kExternalIdBase; }
    bool externalIsLive(const QString &uuid) const; // still in the detected set
    void renderExternalTranscript(int sessionId, bool full);
    void stopExternalSession(int sessionId); // signal the external CLI process to quit
    void killExternalSessionPids(const QList<qint64> &pids); // SIGTERM, then SIGKILL fallback
    QSet<QString> m_externalStopped;         // uuids we've stopped — keep them idle
    QSet<QString> ownStreamCwds() const;    // dirs ForkMesh's own streams drive
    qint64 m_claudeModelsFetchedMs = 0;
    QJsonArray m_liveClaudeModels;
    bool m_cloudflareAiModelsFetched = false;
    int startAdHocAgentForRepo(int repoIndex, const QString &task,
                               const QString &provider, bool createPr,
                               const QString &model = QString(),
                               const QString &titleOverride = QString(),
                               bool genie = false, bool switchToTab = true,
                               const QString &orgTaskId = QString());
    QString saveNewAgentPromptImage(const QImage &image);
    QPushButton *m_agentStopButton = nullptr;
    QPushButton *m_agentStartButton = nullptr;
    QPushButton *m_agentStopAllButton = nullptr;
    QPushButton *m_agentStartAllButton = nullptr;
    QPushButton *m_agentUpdateAllButton = nullptr;
    QLabel *m_agentQueueStatusLabel = nullptr;
    QPushButton *m_agentQueueLimitDecreaseButton = nullptr;
    QPushButton *m_agentQueueLimitIncreaseButton = nullptr;
    QPushButton *m_agentDeleteAllButton = nullptr; // delete agent + worktree + branch
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
    // Infinite-scroll paging for the commit list: how many commits are currently
    // loaded, whether older history remains, and a re-entrancy guard.
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
    bool m_nodeSwitching = false;      // a node switch's heavy load is running
    bool m_repoDetailLoading = false;  // re-entrancy guard for openRepoDetail
    bool m_branchesPanelLoading = false;
    bool m_branchesPanelReloadQueued = false;
    int m_branchesPanelGen = 0;
    QString m_branchesPanelDir;
    QString m_branchesPanelPendingSelect;
    QString m_branchMergedFlashBranch;
    QString m_branchMergedFlashDir;
    int m_branchMergedFlashRow = -1;
    QString m_branchMergedFlashHtml;
    static constexpr int kBranchMergedFlashMs = 20000;
    // Re-entrancy guard for loadMirrorNodesPanel: its synchronous git reads pump
    // the event loop, so a queued roster/mirror callback could start a second
    // pass that appends its own rows on top of the half-built table — every node
    // listed twice.
    bool m_mirrorNodesPanelLoading = false;
    bool m_agentMergeStateRefreshing = false; // refreshAgentMergeState worker in flight
    // Shared re-entrancy guard for the two heavy periodic refreshes
    // (refreshOpenRepoDetail + refreshRepositoryList): each runs synchronous git
    // reads under a GitKeepAlive that pumps the event loop, so a second one firing
    // during that pump would nest its git work and compound into a GUI stall
    // The second one defers instead.
    bool m_heavyRefreshInFlight = false;
    bool m_repoListRefreshQueued = false; // a refreshRepositoryList deferred past a pump
    // Signature of the inputs to the advertised per-repo mirror stats
    // (mirrorPath|lastSyncMs|localPath|worktrees-dir mtime, per non-preview repo).
    // Those ~9 git subprocesses per repo froze the GUI thread when re-run on every
    // onRequestServed; skip the rebuild while the signature is
    // unchanged. Reset by attachBackend so a freshly attached backend is re-pushed.
    QString m_mirrorAdvertSig;
    QString m_mirrorAdvertInputSig;
    bool m_mirrorAdvertRefreshInFlight = false;
    qint64 m_mirrorAdvertCompletedAtMs = 0;
    void refreshMirrorAdverts();
    int m_repoOpenPending = -1;        // repo index queued by openRepoDetailDeferred
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
    QLabel *m_issueEstimateValue = nullptr; // derived OpenAI coding cost estimate
    QLabel *m_issueBountyValue = nullptr;
    QLabel *m_issueCommentsValue = nullptr;
    QStackedWidget *m_issueAssigneesStack = nullptr;
    QStackedWidget *m_issueLabelsStack = nullptr;
    QStackedWidget *m_issueMilestoneStack = nullptr;
    QStackedWidget *m_issuePriorityStack = nullptr;
    QLineEdit *m_issueAssigneesEdit = nullptr;
    QLineEdit *m_issueLabelsEdit = nullptr;
    QComboBox *m_issueMilestoneEdit = nullptr;
    QSpinBox *m_issuePriorityEdit = nullptr;
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
    QWidget *m_issueAiTypingRow = nullptr; // transient "AI is answering…" card
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
    QLabel *m_issueDevelopmentValue = nullptr;    // linked pull requests list
    QPushButton *m_issueLinkPullButton = nullptr; // "Link pull request"
    QTabWidget *m_issueDetailTabs = nullptr;
    int m_issueFilesTabIndex = -1;                // tab index of "Files changed"
    QListWidget *m_issueFilesList = nullptr;      // changed files in the linked diff
    QTextBrowser *m_issueDiffView = nullptr;      // diff viewer in the files tab
    forkmesh::ui::DiffFileNavigator *m_issueDiffNav = nullptr;  // sticky header + scroll<->select
    QLabel *m_issueFilesChangedSummary = nullptr; // "N files changed" line
    QPushButton *m_issueDeleteButton = nullptr;
    QLabel *m_issueAgentValue = nullptr;
    QCheckBox *m_issueAgentCreatePrCheck = nullptr;
    QComboBox *m_issueAgentProvider = nullptr;   // Codex | OpenAI API | Claude API | Claude Code
    QComboBox *m_issueAgentModel = nullptr;      // model for the picked provider
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
    // Content signature (.forkmesh/issues/ subtree oid + repo path) of the data the issue
    // list was last built from. reloadIssues() fires on every push/sync — i.e.
    // every agent commit — but a code-only commit doesn't touch issue metadata, so this
    // lets it skip re-reading git and tearing down/rebuilding the issue rows when
    // nothing changed (a rebuild mid-interaction drops the click/keystroke the user
    // aimed at a row or the search box). Empty = "unknown", never skip.
    QString m_issuesLoadedSig;
    int m_issueLoadGen = 0;
    bool m_issueBackgroundLoadInFlight = false;
    bool m_issueBackgroundReloadQueued = false;
    QStringList m_pendingIssueAttachments; // images queued for the next comment

    QTableWidget *m_projectTable = nullptr;
    QStackedWidget *m_projectViewStack = nullptr; // 0 list table, 1 Gantt
    QButtonGroup *m_projectViewTabs = nullptr;    // its List / Gantt toggle
    QComboBox *m_projectStatusFilter = nullptr;   // Open | Closed | All
    QWidget *m_projectGantt = nullptr;
    QWidget *m_projectDetailPane = nullptr; // right-hand detail beside the list
    QLabel *m_projectDetailTitle = nullptr;
    QLabel *m_projectDetailStatus = nullptr; // Open/Closed status pill
    QLabel *m_projectDetailBody = nullptr;
    QLabel *m_projectDetailProgress = nullptr;
    QLabel *m_projectInlineNotice = nullptr;
    QDateEdit *m_projectStartEdit = nullptr;
    QDateEdit *m_projectEndEdit = nullptr;
    QCheckBox *m_projectStartEnable = nullptr;
    QCheckBox *m_projectEndEnable = nullptr;
    QComboBox *m_projectMilestoneCombo = nullptr;
    QListWidget *m_projectIssuesList = nullptr; // linked issues in the detail pane
    QPushButton *m_projectNewButton = nullptr;
    QPushButton *m_projectCloseButton = nullptr; // Close/Reopen the project
    QPushButton *m_projectDeleteButton = nullptr;
    QList<Project> m_currentProjects;
    int m_currentProjectNumber = -1;
    bool m_projectDeleteConfirmPending = false;

    QWidget *m_nodeProfilePanel = nullptr;
    QWidget *m_nodeProfileSectionHost = nullptr; // full-page home (section 10)
    QPushButton *m_profileCloseButton = nullptr; // hidden while shown as a tab
    QWidget *m_repoDetailSection = nullptr; // hidden while node profile is full-page
    QLabel *m_profileAvatar = nullptr;
    QPixmap m_profileAvatarSource; // raw avatar, re-scaled to a banner on resize
    QLabel *m_profileName = nullptr;
    QLabel *m_profileStatus = nullptr;
    QLabel *m_profileDetails = nullptr;
    QLabel *m_profileMirrorsLabel = nullptr;
    QLabel *m_profileMirrors = nullptr;
    QWidget *m_profileAccountSection = nullptr;
    QLabel *m_profileAccountLabel = nullptr;  // "NODES (n)" / "USER ACCOUNT" header
    QLabel *m_profileAccountStatus = nullptr; // link-state text; hidden once linked
    QListWidget *m_profileUserNodesList = nullptr;
    QPushButton *m_profileLinkUserButton = nullptr;
    // "Link this node to your account" next to the node ID: browser-based
    // linking via a node-signed grant URL.
    QPushButton *m_profileLinkBrowserButton = nullptr;
    int m_linkGrantPollsLeft = 0; // post-browser-link polling countdown
    QString m_linkGrantBaselineOwner;
    QString m_nodeOwnerUser;
    QStringList m_profileLinkedNodes;
    bool m_profileIsUserAccount = false; // this account has login creds (a user)
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
    // Admin-only "Take ownership" request on another node's profile.
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
    QHash<QString, QTimer *> m_catalogPublishTimers; // owner/name -> timer
    QHash<QString, qint64> m_catalogPublishOwnerLastAttemptMs; // owner -> ms
    QSet<QString> m_catalogPublishInFlight;      // owner/name
    QSet<QString> m_catalogPublishQueued;        // owner/name dirtied mid-flight
    QSet<QString> m_catalogPublishDialogQueued;  // owner/name wants UI feedback
    QHash<QString, int> m_catalogPublishConsecutiveFailures; // owner/name -> n
    QHash<QString, QByteArray> m_catalogPublishedFingerprint; // owner/name -> hash
    QHash<QString, qint64> m_catalogPublishedFingerprintAtMs; // owner/name -> ms
    QHash<QString, bool> m_catalogPublishServeState; // owner/name -> serving
    // A private catalog write is permitted only after this process has
    // idempotently registered its public hybrid key and repository privacy
    // policy with an authenticated owner session.
    QSet<QString> m_privateControlReady;
    QSet<QString> m_privateControlInFlight;
    RepoContributionPublicationCache m_contributionPublicationCache;
    QHash<QString, QString> m_catalogContributionScanKey;
    QHash<QString, QString> m_catalogContributionSnapshotKey;
    QHash<QString, QString> m_catalogContributionDependencyFingerprint;
    QHash<QString, QString> m_catalogContributionPreparedScanKey;
    QHash<QString, QString> m_catalogContributionPreparedSnapshotKey;
    QList<RepoHost *> m_repoHosts;
    QSet<QString> m_repoHostKeys; // empty compatibility state; sockets retired
    NodeEventSocket *m_nodeEventSocket = nullptr; // relay push -> /api/sync
    QHash<QString, std::shared_ptr<PrivateMirrorMaterialization>>
        m_privateMirrorMaterializations; // opaque replica id -> temp repo
    QHash<QString, std::shared_ptr<PublicMirrorMaterialization>>
        m_publicMirrorMaterializations; // legacy public-age migration only
    QList<MemberInfo> m_homeRoster;
    QHash<QString, MemberInfo> m_chatDirectoryUsers; // lowercased user -> profile
    bool m_chatDirectoryFetchInFlight = false;
    qint64 m_chatDirectoryFetchedMs = 0;
    QTimer *m_chatDirectoryTimer = nullptr;
    bool m_chatDirectoryLoaded = false; // first fill done (may legitimately be empty)
    QSet<QString> m_removedPeerIds;  // IDs explicitly removed via removeChatMember
    QHash<QString, qint64> m_peerLastSeenMs;
    bool m_welcomeAnnounced = false;
    QString m_catalogMirrorsSource;        // "owner/name" the cache holds
    QJsonArray m_catalogMirrorsCache;      // last /mirrors payload's "mirrors"
    QString m_catalogMirrorsFetchSource;   // source the last fetch was kicked for
    qint64 m_catalogMirrorsFetchedMs = 0;  // throttle: last fetch kick time
    QHash<QString, QJsonObject> m_mirrorReachabilityCache;
    QSet<QString> m_mirrorReachabilityInFlight;
    QHash<QString, QJsonObject> m_mirrorPendingCache;
    QSet<QString> m_mirrorPendingInFlight;
    QString m_releaseDownloadsSource;      // "owner/name" the cache holds
    QHash<QString, int> m_releaseDownloadsCache; // sha256 -> download count
    QString m_releaseDownloadsFetchSource; // source the last fetch was kicked for
    qint64 m_releaseDownloadsFetchedMs = 0; // throttle: last fetch kick time
    QHash<QString, QPair<int, int>> m_repoStats;
    QHash<int, bool> m_syncingRepos;
    QSet<int> m_pushingRepos;
    QSet<QString> m_sshMirrorPushing;
    QSet<QString> m_sshMirrorPushPending;
    MirrorGatewayHealth m_sshMirrorHealth;
    QSet<QString> m_mentionScanInFlight;
    QString m_currentConversation;
    QHash<QString, QList<ChatMessage>> m_history;
    QSet<QString> m_historyIds; // message ids already in m_history (dedup)
    QTimer *m_chatSaveTimer = nullptr;
    QTimer *m_chatExpiryTimer = nullptr; // periodic pruneExpiredChatHistory()
    QHash<QString, MessageRow *> m_visibleRows; // messageId -> row (current conv)
    QString m_activeChatThreadRootId;
    QDialog *m_chatThreadDialog = nullptr;
    QVBoxLayout *m_chatThreadRowsLayout = nullptr;
    QPlainTextEdit *m_chatThreadInput = nullptr;
    QLabel *m_chatThreadCountLabel = nullptr;
    QHash<QString, QMap<QString, QStringList>> m_reactions;
    QHash<QString, QPixmap> m_avatars;          // senderId -> avatar
    QHash<QString, QString> m_dmNames;          // peerId -> display name
    QHash<QString, QHash<QString, QString>> m_typing; // conversation -> peerId -> name
    QStringList m_networkLog;
    QHash<QString, int> m_logFilterCounts;
    int m_networkLogDiskLines = 0;              // lines written to the on-disk log
    QStringList m_openDms;                      // peerIds in sidebar order
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
    QString m_accountEmail;
    // Session token minted by /api/accounts/login, used to authenticate
    // profile writes (e.g. persisting the chosen avatar to the account record
    // so the web dashboard shows the same picture the desktop app does).
    QString m_accountSessionToken;
    QString m_accountSessionInstanceUrl;
    forkmesh::accounts::SessionCache m_accountSessionCache;
    bool m_accountSolanaVerified = false;
    bool m_accountDesktopCapable = false;
    QString m_accountTier = QStringLiteral("free");
    QTimer *m_heartbeatTimer = nullptr;
    bool m_isAdmin = false;
    QTimer *m_adminPollTimer = nullptr;
    QStringList m_seenPendingUsers;
    QString m_roomPassphrase;
    QString m_lastClaimCodeShown;
    // Admin claiming this node last shown for an ownership-transfer prompt
    // so the per-minute heartbeat doesn't reopen the dialog
    // while the request is still pending a decision.
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

#endif // FORKMESH_MAIN_WINDOW_H
