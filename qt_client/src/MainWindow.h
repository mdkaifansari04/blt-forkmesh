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
#include "RepoSecurity.h"
#include "RepoContributionSnapshot.h"
#include "MirrorCrypto.h"

struct CommitComment; // CommitCommentStore.h

// Per-session "night rider" scanner-light state, animated in the agents list
// while raw output is streaming. phase is the Larson-sweep parameter advanced on
// a timer; lastActivityMs is bumped on every raw-output chunk so the sweep keeps
// running while the agent is actively producing output. intensity is a smoothed
// live-output meter (decays every frame, re-bumped per chunk by how much just
// streamed) that the sweep's speed, brightness, trail and colour ride in real
// time — so a quiet agent crawls dim red and a busy one races hot and bright.
struct AgentScannerState {
    double phase = 0.0;        // 0..1 sweep parameter (bounced into a triangle)
    qint64 lastActivityMs = 0; // wall-clock of the last raw-output chunk
    double intensity = 0.0;    // 0..1 live-output rate the effect reacts to
};

// Per-session "what did this agent change" summary shown in the agents list
// (issue #170): files its patch touched, and how far its branch sits ahead of /
// behind the base branch. -1 means "unknown / not applicable" — e.g. a running
// session with no patch yet, or a branch that has since been removed.
struct AgentDiffStat {
    int files = -1;
    int ahead = -1;
    int behind = -1;
    // True when re-merging the base branch into this session's branch would
    // conflict (adhoc #229) — surfaced as a conflict marker in the agents list.
    bool conflicted = false;
    // Working-copy state of the session's own worktree (adhoc #403), surfaced on
    // the Status cell's branch chip: `worktree` is the dedicated checkout's path
    // ("" when the session has none left), and `dirty` counts the entries
    // `git status --porcelain` reports there (-1 when there was no worktree to
    // ask, 0 when it is clean).
    QString worktree;
    int dirty = -1;
};

#include <QElapsedTimer>
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

// Attached to each block of the always-on footer log (adhoc #133) so a clipped,
// no-wrap line still carries its full untruncated text — surfaced on hover and
// used to open the full Log view at the matching entry on click.
class FooterLogLineData : public QTextBlockUserData {
public:
    explicit FooterLogLineData(QString line) : rawLine(std::move(line)) {}
    QString rawLine;
};

class MessageRow;
class MarkdownEditor;
class PullBadgeWidget;
// Defined in MainWindowInternal.h, which lives in namespace forkmesh::ui.
namespace forkmesh::ui {
class ActivityRailButton;
class AgentDotMatrix;
}
using forkmesh::ui::ActivityRailButton;
using forkmesh::ui::AgentDotMatrix;
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
namespace forkmesh::ui { class DiffFileNavigator; } // file-list <-> diff-view sync

// A configured mainnode the user can connect to. The client connects to one at
// a time; the favicon rail switches the active one.
struct ServerConfig {
    QString url;
    QString room;
};

// Per-repository metadata that git can't provide, stored in .forkmesh/info.json
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
    // Random local handle for the durable owner-sealed .fm-private replica.
    // It is never a repository name and is visible only in this owner's local
    // settings. Private mirrorPath values are runtime-temporary and are never
    // persisted once this handle exists.
    QString privateReplicaId;
    // Random local handle for an official-age encrypted public mirror archive.
    // Once present, mirrorPath is an owner-only runtime materialization and is
    // never written to settings.
    QString publicArchiveId;
    bool publishToNetwork = false;
    // Private repo: absent from public discovery. Authorized owners and
    // collaborators fetch only its opaque encrypted replica over HTTPS, then
    // decrypt into short-lived owner-only local storage with their own key.
    bool isPrivate = false;
    // Run .forkmesh/ workflows when a fork pushes to this repo's bare mirror.
    // Enabled by default; can be turned off per repo on the Actions tab. Pushed
    // workflow changes still require explicit approval before they run.
    bool actionsEnabled = true;
    // A gateway-managed serving repository must keep its own post-receive hook
    // and object database isolated from workflow-created objects. The remote
    // Actions helper therefore maintains a separate local bare mirror and this
    // source/ref pair is polled for bounded branch changes instead of replacing
    // the serving hook.
    bool externallyManagedActions = false;
    QString externalActionsSource;
    QString externalActionsRef;
    // Workflow paths (relative to the repo root, e.g. ".forkmesh/ci.yml") that
    // the owner has switched off individually. Disabled workflows are skipped on
    // push and can't be triggered manually, but stay listed so past runs remain
    // visible and the switch can be flipped back on.
    QStringList disabledWorkflows;
    // Block pushes when the diff introduces a high-confidence secret (API key,
    // private key, etc.). Enabled by default; the user can bypass per-push or
    // turn it off entirely here.
    bool secretScanningEnabled = true;
    // Temporary, browse-only cache for a repo hosted by another node. Preview
    // repos are not saved, advertised, published, hosted, or wired for actions.
    bool previewOnly = false;
    qint64 hostedSinceMs = 0;
    qint64 lastSyncMs = 0;
    qint64 publishedAtMs = 0;
};

// A clickable navigation target attached to a notification so its row opens the
// related screen/item when double-clicked (issue #292). An empty kind means the
// notification carries no destination and the row is inert.
struct NotificationLink {
    QString kind;    // "issue" | "pull" | "discussion" | "commit"
    QString owner;   // repo owner
    QString name;    // repo name
    int number = -1; // issue / PR / discussion number
    QString ref;     // commit hash, when kind == "commit"

    bool isValid() const { return !kind.isEmpty(); }
};
Q_DECLARE_METATYPE(NotificationLink)

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Safe target for the website's secret-free setup link. This only opens the
    // local Control Node page and focuses its session-only Cloudflare token
    // field; link parameters are never accepted.
    void openCloudflareSetupFromSystemLink();

    // Apply the saved theme (system/dark/light) to the whole application.
    static void applyTheme();
    void refreshThemedIcons();

    // Turn "#N" issue/PR references, pasted commit SHAs and forkmesh:// permalinks
    // inside a markdown comment body into links the conversation views resolve via
    // openBodyReference(). Leaves fenced/inline code and existing links untouched.
    // Static + pure so the window tests can exercise it directly.
    static QString autolinkReferences(const QString &markdown);

    // adhoc #38: choose the next open issue the issue looper should work, or
    // nullptr when none qualify. An issue is skipped when it is deleted, not open,
    // already has a local agent session (hasLocalSession), or already carries an
    // assignee. The assignee is the cross-mirror claim: once a looper takes an
    // issue it assigns its node, so neither this node's looper nor a looper on
    // another mirror starts the same task twice. Highest priority wins; ties go to
    // the lowest issue number. Static + pure so the window tests can exercise it.
    static const Issue *looperPickNext(
        const QList<Issue> &issues,
        const std::function<bool(int)> &hasLocalSession);

#ifdef FORKMESH_WINDOW_TESTS
    using TestIssueHistoryDeleteRunner =
        std::function<bool(int number, QString *error)>;
    void testSetRoster(const QList<MemberInfo> &members) { setRoster(members); }
    void testResetRosterForAlerts()
    {
        m_homeRoster.clear();
        m_removedPeerIds.clear();
        m_peerLastSeenMs.clear();
    }
    // Backdates every remembered sighting by `ageMs` so a test can exercise the
    // idle-visitor sweep without waiting ten real minutes (adhoc #404).
    void testAgePeerSightings(qint64 ageMs)
    {
        for (auto it = m_peerLastSeenMs.begin(); it != m_peerLastSeenMs.end(); ++it)
            *it -= ageMs;
    }
    QList<MemberInfo> testHomeRoster() const { return m_homeRoster; }
    // Sets the live roster directly (skipping setRoster's side effects, e.g.
    // refreshRepositoryList's node-switcher bookkeeping) and rebuilds the Mirror
    // nodes panel, so a test can exercise loadMirrorNodesPanel's row-building
    // in isolation (adhoc #46).
    void testSetHomeRosterAndReloadMirrorPanel(const QList<MemberInfo> &members)
    {
        m_homeRoster = members;
        loadMirrorNodesPanel();
    }
    void testSetNodeAlertGraceUntilMs(qint64 value) { m_nodeAlertGraceUntilMs = value; }
    QStringList testNetworkLog() const { return m_networkLog; }
    void testResetNetworkLog();
    void testLogSystem(const QString &text) { logSystem(text); }
    // Drives the network log's segmented-render + scroll-to-top-loads-more path
    // (adhoc #15) without needing real scroll-wheel input.
    void testShowSettingsSection() { showSection(1); }
    void testShowLogSection() { showSection(4); }
    void testShowHostsSection() { showSection(7); }
    void testSetDirectoryUserNodes(const QString &user,
                                   const QStringList &nodes);
    void testShowNodesSection();
    QStringList testNodeDirectoryNames() const;
    void testRenderNetworkRepos(const QJsonArray &repos);
    QStringList testNetworkRepoNames() const;
    QString testNetworkRepoActionText(int row) const;
    QString testNetworkRepoMirrorHeader() const;
    void testRebuildNetworkLogView() { rebuildNetworkLogView(); }
    // Quick log filter (the chip row above the log): the chips currently offered,
    // and clicking one by category ("" = All).
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
    // Issue #214: the ordered "Build & preview" command pipeline — checkout into a
    // throwaway worktree, CMake configure, build — as "<program> <args…>" lines.
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
            // Keep test windows on the state selected explicitly by the test.
            m_pendingSilentAuth = false;
            m_pendingRestoreRepoIndex = -1;
            m_deferredStartupRun = true;
        }
    }
    void testRunDeferredStartupNow() { runDeferredStartup(); }
    void testStartSession() { startSession(); }
    void testEnablePaidMirroring() { enablePaidMirroring(); }
    int testAccountFlowCalls() const { return m_testEnsureNodeAccountCalls; }
    int testStackIndex() const;
    QString testUserName() const { return m_userName; }
    QString testAccountName() const { return m_accountName; }
    QString testSavedSolanaAddress() const;
    bool testAccountAuthenticated() const { return m_accountAuthenticated; }
    QString testAccountTier() const { return m_accountTier; }
    bool testHasOwnerSigningCapability() const
    {
        return hasOwnerSigningCapability(m_accountName);
    }
    // Verifies makeColumnsResizable(): once rows arrive, every auto-sized column
    // (ResizeToContents and the Stretch flex column) flips to draggable
    // Interactive keeping its current width, while Fixed columns are left alone.
    Q_INVOKABLE bool testColumnsBecomeResizable();
    // Verifies the spreadsheet drag rule: dragging a column's divider resizes only
    // that column; the columns to its right keep their widths and simply shift,
    // rather than a neighbour or far-off Stretch column donating the difference.
    Q_INVOKABLE bool testSpreadsheetResize();
    // Verifies the spreadsheet rule still holds after a column is dragged into a
    // new order: resizing one column leaves every other column's width untouched
    // for movable-header tables like the agents list.
    Q_INVOKABLE bool testSpreadsheetResizeAfterMove();
    // Verifies the agents list lets the user drag its column headers into a new
    // order (in addition to resizing them).
    Q_INVOKABLE bool testAgentColumnsMovable() const;
    Q_INVOKABLE int testAddLocalRepository(const QString &owner, const QString &name,
                                           const QString &localPath);
    Q_INVOKABLE bool testOpenRepository(int index);
    // The branch the open repo treats as its default/merge base, so a test can
    // prove it stays main even when the working tree is parked on a feature branch.
    Q_INVOKABLE QString testRepoDefaultBranch() const;
    // Switch the open repo-detail view to its Issues sub-tab (stack index 2) so
    // the issues toolbar gets real geometry. Returns false if not built yet.
    Q_INVOKABLE bool testShowRepoIssuesTab();
    Q_INVOKABLE bool testSaveRepoAboutMetadata(const QString &about,
                                               const QString &website)
    {
        return saveRepoAboutMetadata(about, website, nullptr);
    }
    // Drive provisionNewRepository() without its dialog: returns the new repo
    // index (>= 0) or -1 on failure.
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
    // adhoc #191: the git dir agents/looper would run against for a repo — a
    // working-tree checkout, else a bare mirror, else empty. Lets a test prove a
    // mirror-only node (no working tree) is now treated as able to run agents.
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
    // Issue #203: drive the footer quick-add for a plain (no-agent) issue — set
    // the title, clear the "assign agent" / "no issue" toggles, run
    // quickAddIssue() — and return the now-current issue number so a test can
    // prove the new issue's detail pane auto-opens. testIssueDetailVisible()
    // reads whether that right-hand detail pane is showing.
    Q_INVOKABLE int testQuickAddIssueNoAgent(const QString &title);
    bool testIssueDetailVisible() const
    {
        return m_issueDetail && m_issueDetail->isVisible();
    }
    // Provider id (codex/openai/claude-api/claude-code) currently selected in each
    // agent-assignment picker, plus a way to drive the Settings "Default agent"
    // combo as a user would, so tests can assert the default seeds/updates them.
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
    void testSwitchToWorktree(const QString &branch) { switchToWorktree(branch); }
    void testReloadWorktreesPanel() { loadWorktreesPanel(); }
    QString testSelectedWorktreeBranch() const { return m_worktreeSelectedBranch; }
    // Detail-pane branch label text, so a test can prove the worktree detail
    // shows which branch the selected worktree is on. Defined in MainWindow.cpp
    // because QLabel is only forward-declared here.
    QString testWorktreeBranchLabel() const;
    // Ahead/behind cell text (column 3) for the worktree row on `branch`, so a
    // test can prove the list shows how far each worktree diverges from main.
    QString testWorktreeAheadBehindText(const QString &branch) const;
    QStringList testWorktreeBranches() const;
    // Focus the worktrees table and deliver an Up/Down key press, returning the
    // branch that ends up selected so a test can prove keyboard arrow keys move
    // the selection (and drive the detail pane) like a click does.
    QString testArrowOnWorktrees(bool down);
    // Open a repo-detail tab exactly as a user clicking its nav button would, so a
    // test can prove the tab switch hands keyboard focus to that tab's list.
    int testWorktreesTabIndex() const { return m_worktreesTabIndex; }
    void testClickRepoDetailTab(int id);
    bool testOpenMostRecentCommit();
    // Activation-independent: is the worktrees table the focus widget of its
    // window? (hasFocus() also requires the window to be active, which an
    // offscreen test window isn't.)
    bool testWorktreesTableHasKeyboardFocus() const;
    // Same coverage for the Releases and Mirror-nodes tabs, whose list tables
    // also grab keyboard focus on open so Up/Down arrows (and Enter to open)
    // work without a click first (adhoc #183).
    int testReleasesTabIndex() const { return m_releasesTabIndex; }
    int testMirrorNodesTabIndex() const { return m_mirrorNodesTabIndex; }
    int testControlNodeSectionIndex() const { return kControlNodeSectionIndex; }
    void testShowControlNode() { showSection(kControlNodeSectionIndex); }
    bool testReleasesTableHasKeyboardFocus() const;
    bool testMirrorNodesTableHasKeyboardFocus() const;
    // The Mirror nodes rows as "name-cell-text|node-id", so a test can prove a
    // node that re-registered under a new key shows exactly one row (adhoc #46).
    Q_INVOKABLE QStringList testMirrorNodeRows() const;
    bool testMirrorNodesOnlineOnlyChecked() const;
    void testSetMirrorNodesOnlineOnly(bool checked);
    QString testMirrorNodeCellText(const QString &nodeName, int column) const;
    QString testMirrorNodeCellToolTip(const QString &nodeName, int column) const;
    // Build the exact command used by the fleet-wide binary action without
    // starting SSH. Tests use this to keep that action pinned to the published,
    // checksum-verified release rather than the currently-running executable.
    QString testFleetBinaryInstallRemoteCommand(bool reinstall,
                                                qsizetype *uploadByteCount,
                                                QString *errorOut);
    QString testDirectBinaryInstallRemoteCommand(qsizetype *uploadByteCount,
                                                 QString *errorOut);
    // Rebuild the Branches panel, then read back the Worktree column (column 3)
    // for `branch`, so a test can prove the branches list surfaces the worktree a
    // branch is checked out in (issue #172).
    // Rebuilds off-thread now (adhoc #420), so this pumps until the rows land.
    void testReloadBranchesPanel();
    QString testBranchWorktreePath(const QString &branch) const;
    // Inject an agent session so a test can prove the branches list surfaces the
    // issue/agent a branch is attached to (adhoc #191).
    void testAddAgentSession(const AgentSession &session)
    {
        m_agentSessions.append(session);
    }
    void testRefreshAgentStatusRow() { refreshAgentStatusRow(); }
    // "Issue / Agent" column (column 4) text for `branch`, so a test can prove
    // the branches list names the issue/agent a branch is attached to (adhoc #191).
    QString testBranchAttachmentText(const QString &branch) const;
    // Whether the "Issue / Agent" cell (column 4) for `branch` carries an icon, so
    // a test can prove the branches list stamps the agent's status icon on a branch
    // an agent is working (adhoc #251).
    bool testBranchAttachmentHasIcon(const QString &branch) const;
    // Branch names (column 0) in row order, so a test can prove the default branch
    // is pinned to the top of the list regardless of commit recency (adhoc #185).
    QStringList testBranchRowOrder() const;
    // Follow a branch link and read back the branch the table landed on right
    // away — no event pumping — so a test can prove the click doesn't wait on the
    // panel's off-thread git reads (adhoc #420).
    QString testSwitchToBranchImmediateSelection(const QString &branch);
    // Click the "Issue / Agent" cell (column 4) for `branch` and return the agent
    // session the app navigated to (m_selectedAgentSessionId), so a test can prove
    // clicking the cell jumps to that branch's agent (adhoc #258).
    int testClickBranchAgentCell(const QString &branch);
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
    // adhoc #403: the badge data the Status cell hands its branch chip, read back
    // as "files|dirty|worktree", so a test can prove the chip's files-changed /
    // uncommitted / worktree-present markers are fed from the session's diff stat.
    QString testAgentStatusCellBadges(int sessionId, const AgentDiffStat &stat) const;
    bool testAgentSessionMerged(int sessionId) const;
#endif

    // --- Headless / CLI support (HeadlessConsole) ------------------------------
    // Always compiled (unlike the FORKMESH_WINDOW_TESTS hooks above): read-only
    // views and a few control entry points so a no-display node started with
    // --headless can be driven from a stdin REPL. Each reuses the exact path the
    // GUI uses, so headless behaviour stays in lock-step with the desktop app.
    ChatBackend *currentBackend() const { return m_backend; }
    bool headlessConnected() const;
    QString headlessNodeName() const;
    // First-run / connect: equivalent to typing a name and pressing the GUI
    // connect button (drives startSession via the offscreen setup widgets).
    void headlessStart(const QString &name, const QString &solana = QString());
    // Flags this process as a no-GUI (headless / offscreen) node. main() sets it
    // right after construction so startSession can auto-register a fresh mirror's
    // account — the desktop opens a "Join ForkMesh" dialog for that, which a
    // headless VM has no way to click. Also clears any persisted parked-offline
    // state, since a headless node has no GUI toggle to bring itself back online.
    void setHeadlessMode(bool headless);
    // Kick the periodic mirror sync + owned-inbox poll right now.
    void headlessSyncNow();
    // Pull the latest version from the live install mirror, rebuild and relaunch
    // (the relaunched process inherits QT_QPA_PLATFORM=offscreen, so it comes back
    // up headless). Reuses the exact GUI "update, rebuild & restart" path; progress
    // streams to the terminal via the [restart +Nms] log lines.
    void headlessUpdateRestart();
    QStringList headlessStatusLines() const;
    // Device-local Claude login status from the headless console. Historical
    // export/import verbs return a hard refusal; tokens are never serialized.
    QStringList headlessClaudeAuth(const QStringList &args);
    QStringList headlessRosterLines() const;
    QStringList headlessRepoLines() const;
    QStringList headlessMirrorLines() const;
    // One-line "cpu N%  ·  mem N MB" snapshot of this node's own resource use,
    // shown in the headless mirrors/status views so a durable daemon's load is
    // visible (issue #287).
    QString headlessResourceLine() const;

signals:
    // Emitted whenever a backend is (re)created and wired up, so a headless
    // console can attach its live event feed to the new ChatBackend.
    void backendAttached(ChatBackend *backend);

protected:
    void closeEvent(QCloseEvent *event) override;
    // Rescan the changes panel when the window regains focus (e.g. after a
    // background agent edited the working tree) so it always shows fresh state.
    void changeEvent(QEvent *event) override;
    // Defers heavy, git-backed startup until the window's first frame is on
    // screen, so launch shows the themed UI instead of an unpainted black frame.
    void showEvent(QShowEvent *event) override;
    // Image drag-and-drop onto the inline issue comment composer. Also
    // intercepts right-click context menus app-wide to offer "Send to
    // Prompt" on any selected text (adhoc #126).
    bool eventFilter(QObject *obj, QEvent *event) override;
    Qt::Edges resizeEdgesAtGlobalPos(const QPoint &globalPos) const;
    bool handleFramelessResizeEvent(QObject *obj, QEvent *event);
    void updateFramelessResizeCursor(Qt::Edges edges);
    // Right-click on selected text anywhere (transcript, diff, README, logs):
    // shows the widget's normal context menu plus a "Send to Prompt" action.
    // Returns true (event consumed) only when it took over the menu.
    bool maybeShowSendToPromptMenu(QObject *obj, QContextMenuEvent *ce);
    // Appends text to whichever prompt box is the relevant target: the
    // per-agent composer if an agent session is open and visible, else the
    // footer's global quick-add box.
    void appendTextToActivePrompt(const QString &text);
    // Keep the floating expanded-toast overlay anchored to the toast on resize.
    void resizeEvent(QResizeEvent *event) override;

private:
    static constexpr int kNetworkReposSectionIndex = 11;
    static constexpr int kNetworkDiagnosticsSectionIndex = 12;
    static constexpr int kNodesSectionIndex = 13; // "Nodes" directory (adhoc #9)
    static constexpr int kControlNodeSectionIndex = 14;

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
    bool m_framelessResizeCursorActive = false;
    int m_pendingRestoreRepoIndex = -1;    // last repo to reopen, or -1
    bool m_pendingSilentAuth = false;      // attempt auto-connect on first frame
    bool m_headless = false;               // no-GUI node (offscreen); see setHeadlessMode
    // A headless node has no GUI and no other periodic hook that retries
    // registerNodeAccountSilently() — startSession() only calls it once, at
    // first boot. If the relay is briefly unreachable right then (common on a
    // fresh VPS: DNS/network still settling), the node was previously stranded
    // unregistered forever, mirroring + chatting but never appearing on the
    // website (adhoc #219). This timer retries with backoff until it succeeds.
    QTimer *m_headlessRegisterRetryTimer = nullptr;
    int m_headlessRegisterAttempt = 0;
    void scheduleHeadlessRegisterRetry(const QString &accountName);
    // True first run only (no saved node name yet). Gates the "we're syncing" toast
    // + auto-open in ensureFlagshipRepo() so a fresh install lands on real content
    // without manual setup, without re-interrupting an existing user (adhoc #113).
    bool m_freshInstall = false;
    // "owner/name" of a repo whose initial mirror sync should auto-open its repo
    // detail view once syncRepository's fetch/clone finishes; cleared after firing
    // once. Set by ensureFlagshipRepo() on a fresh install.
    QString m_pendingAutoOpenRepoKey;
    // Account = node identity. Registration (name + Solana + password + TOTP) gates
    // joining the network; the account name is the canonical repo owner.
    bool ensureNodeAccount(const QString &accountName, const QString &solana);
    // Non-interactive auth used on launch: true only if this node key already
    // matches a registered active account (or was confirmed before, offline).
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
    // Hosted git URL (https://<mainnode>/<owner>/<name>) for a catalog repo,
    // used when the catalog record omits an explicit cloneUrl.
    QString hostedCloneUrl(const QString &owner, const QString &name) const;
    // Bootstrap a fresh client by mirroring the flagship ForkMesh repository so
    // it appears in the Repos list without first walking the full join flow.
    void ensureFlagshipRepo();
    // Log in by email (+ optional TOTP). accountName is only used as a fallback
    // node name if the server response omits one. If *fatal is non-null it is set
    // true when the failure is unrecoverable (the same credentials can never
    // succeed from this device, e.g. the account is bound to another key), so the
    // caller can stop re-prompting instead of looping the login dialog forever.
    bool verifyTotpLogin(const QString &email, const QString &password,
                         const QString &totp, const QString &accountName,
                         bool *fatal = nullptr);
    // Periodic signed heartbeat that keeps this node eligible for the reward
    // split and refreshes its payout Solana address.
    void sendNodeHeartbeat();
    QJsonObject emailNotificationPreferencesPayload() const;
    // A user on forkmesh.com claimed this node's ID (adhoc #53): the heartbeat
    // reply carried a confirmation code, shown on this machine so the person
    // standing at both screens can type it back into the website.
    void showNodeClaimCode(const QString &user, const QString &code);
    // Admin node-ownership takeover (adhoc #141): an admin viewing another
    // node's profile can request ownership; the heartbeat reply on the TARGET
    // node then carries the pending request, prompted here for that node's own
    // owner to approve or deny. requestNodeOwnership is the admin-side trigger.
    void requestNodeOwnership();
    void showOwnershipTransferPrompt(const QString &admin);
    void submitOwnershipTransferDecision(bool approve);
    // Installer link-code flow (adhoc #53): the hosts/SSH installer printed a
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
    // "Link this node to your account" (adhoc #120): signs a short-lived grant
    // with this node's key and opens it as a dashboard URL in the default
    // browser, where the logged-in web user completes the link with no further
    // prompts. pollLinkNodeGrant re-checks the account for a while afterwards so
    // the profile flips to "linked" on its own once the browser side finishes.
    void openLinkNodeInBrowser();
    void pollLinkNodeGrant();
    // Human-readable text for a link-self error code (for the account section).
    QString linkErrorMessage(const QString &code) const;
    // Admin: poll for newly-joined users and verify their email by hand (until a
    // real email service is wired up). Only active for accounts in ADMIN_NODES.
    void pollPendingUsers();
    // Fetch the shared room-chat key (server-derived from DATA_KEY) so it is no
    // longer a public constant baked into the client. Cached in m_roomPassphrase
    // and passed to ServerNode; empty falls back to the legacy app key.
    void fetchRoomPassphrase();
    // Start (once the account identity is known) the asynchronous bridge that
    // brings World-office channels into chat and opens their send sockets.
    // The office's #general room needs nothing here: it is the same mainnode
    // room this client already joins, so it lands in #general.
    void startOfficeChannelMirror();
    // True for a World-office conversation whose text sends use the office
    // bridge rather than the primary mesh backend.
    bool isOfficeConversation(const QString &conversation) const;
    void showAdminVerifyDialog();
    bool adminVerifyEmail(const QString &target);
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
    // Pull a fresh copy from the install URL (the direct-HTTPS mirror), then
    // rebuild and relaunch. Installs into the invoking non-root user's home even
    // when ForkMesh itself is running as root.
    void updateRebuildRestart();
    // Settings → "Automatically update ForkMesh": periodic, quiet check for a new
    // tagged release on the update remote (ordinary commits on main don't count).
    // Only ever triggers an update when one is actually found, and never while
    // an agent is running. Prefers the release's prebuilt artifact
    // (tryPrebuiltAutoUpdate); updateRebuildRestart() is the source fallback.
    void maybeAutoUpdate();
    // Install the new release from its sha256-verified prebuilt artifact
    // (local mirror CAS first, else the relay's content-addressed blob route)
    // instead of rebuilding from source next to the live node. Returns false
    // when the tag has no artifact matching this OS/arch — caller falls back
    // to the source rebuild.
    bool tryPrebuiltAutoUpdate(const QString &clientDir, const QString &tag,
                               const QString &tagCommit);
    void installPrebuiltAndRelaunch(const QString &artifactPath,
                                    const QString &tag);
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
    // Mirror the newest update/rebuild log line onto the footer one-liner so the
    // live progress is visible at the bottom of the app without the full window.
    void setFooterUpdateLine(const QString &line);
    // The rendered HTML for one footer-strip line (favicon, time, badge, body).
    // Shared by the live append and the startup seed, which builds its whole
    // block of lines in one pass.
    QString footerLogLineHtml(const QString &clean);
    // (Re)apply the always-on footer log line's inline stylesheet for the active
    // theme, tinting the text by the current line's tone. Called on theme switch.
    void styleFooterUpdateLog();
    // Open the full Log section (section 4) and scroll it to the entry matching a
    // line clicked in the always-on footer strip (adhoc #133).
    void openFullLogAtFooterLine(const QString &rawLine);
    void persistProfile();

    // Chat page
    QWidget *buildChatPage();
    void ensureSectionBuilt(int index);

    // Optional public payout-address notice; crypto never gates the core app.
    QWidget *buildSolanaNotice();
    void updateSolanaNotice();
    void promptSetSolanaAddress();
    // Opt-in reward settings for an already registered, locally signable node:
    // accept only a public self-custodial payout address and start its existing
    // public mirrors. This never runs account reservation/donation and never
    // promises selection or payment.
    void enablePaidMirroring();

    // Top breadcrumb: active server > current section.
    QWidget *buildBreadcrumb();
    void updateBreadcrumb();
    void showRelayMenu();          // searchable dropdown to switch/add relays
    void updateRelaySwitcher();    // refresh top-bar relay icon / domain / count
    void probeRelayLatency();      // measure round-trip to the active relay (radar)
    // Room-socket keepalive RTT (ChatBackend::latencySampled): feeds the radar
    // for free every ~25s, so probeRelayLatency skips its HTTP GET while a
    // fresh sample exists and only probes when the socket is down.
    void onRelayLatencySampled(int ms);
    void initRelayReachabilityWatch(); // OS reachability → instant radar flips
    void openServerWebsite(int index); // open a relay's site in the browser
    void showNodeMenu();           // searchable dropdown to pick a node
    void showNodesWindow();        // full window listing nodes, status, public wallet
    QString topBarUserName() const; // linked user/account name shown in the top bar
    QString nodeOwnerDisplayName() const; // user account that owns this node, if known
    QString chatDisplayName() const; // user identity used for chat sender names
    QString machineNodeName() const; // THIS machine's node name (never the username)
    void saveMachineNodeName(const QString &name); // persist + re-advertise
    // Persist the extra Actions `runs-on:` labels this machine answers to,
    // normalized to distinct lower-cased tags.
    void saveActionNodeLabels(const QString &labels);
    void updateChatIdentity();     // push user name/avatar into the chat backend
    void updateUserSwitcher();     // refresh top-bar user label/avatar
    void updateNodeSwitcher();     // refresh top-bar node label / count
    void updateNavSolanaBalance(); // refresh top-bar balance for the web user
    void refreshWebUserSolanaAddress();
    void cacheWebUserSolanaProfile(const QString &account,
                                   const QJsonObject &profile);
    void cycleNavSolanaCurrency(); // SOL -> USD -> INR -> SOL on balance click
    // Re-render the top-bar balance from the cached lamports/fiat rate without
    // re-hitting the network, so cycling SOL/USD/INR is instant and can't stall
    // on getBalance / price rate-limits.
    void renderNavSolanaBalance();
    // Hover-gated getBalance. Every other path (profile hydration, currency
    // cycling, the web-profile poll) renders from cache; only pointing at the
    // top-bar balance actually spends a Solana RPC call, and even then only
    // once per kNavSolanaBalanceTtlMs.
    void refreshNavSolanaBalance(bool force = false);
    void queryNavSolanaBalance(const QString &addr, int endpointIndex);
    void queryNavSolanaUsdPrice(const QString &addr, qint64 lamports);
    void showRepoMenu();           // dropdown to open repos / add a local repo
    void updateRepoSwitcher();     // refresh top-bar repo label / count
    // Keep the open repo's sync-derived indicators in step (adhoc #374 removed the
    // floating "Sync" pill that used to hover above the Code tab; the activity
    // rail's Git glyph and the commit list's "waiting to sync" markers remain).
    void refreshRepoSyncIndicators();
    void pushCurrentRepoUpstream();
    // Launch the async `git push` for a repo whose secret scan has completed and
    // been approved (see pushCurrentRepoUpstream). The repo must already be marked
    // in m_pushingRepos.
    void startRepoPush(int index, const RepositoryRecord &repo,
                       const QString &upstream, int ahead);
    // Integrity pin: sha256 over the canonical heads+tags advertisement of a bare
    // mirror, byte-for-byte identical to the worker's advertised_refs_canonical().
    // The owner signs this on publish and the relay pins it. Empty when the mirror
    // path is unset/unreadable.
    QString mirrorStateHash(const QString &mirrorPath) const;
    // Signed catalog-list URL (adds our viewer token so the relay also returns our
    // own private repos). Shared by fetchCatalogRepos() and refreshRepoPinBanner().
    QUrl catalogListUrl();
    // Detect when the relay's pinned stateHash no longer matches the refs the open
    // repo's owner node actually serves ("clones are being rejected"). Rather than
    // an intrusive top-bar toast, this just flags m_repoPinMismatch, which paints
    // the self row/dot in the Mirror nodes panel as a caution triangle (adhoc #65).
    void refreshRepoPinBanner();
    // Periodic auto-heal: for EVERY repo this node is the source of truth for (not
    // just the open one), re-attest the relay's integrity pin when the refs we
    // serve have drifted past it. The automatic form of the manual "Reset
    // integrity pin" — a source repo the owner isn't currently viewing would
    // otherwise sit with every clone rejected until they happened to open it and
    // click reset. Only re-signs our own authentic served refs (identical to any
    // publish), and when the source is offline it simply never runs, so the pin
    // freezes and keeps protecting clones against a tampered mirror as before.
    void reattestStalePins();
    // Re-attest the open repo's current refs, overwriting a stale relay pin.
    void resetRepoPin();
    // True when the open repo's branch tracks the ForkMesh relay (which serves
    // clone/fetch only, no git-receive-pack). Such repos publish by syncing the
    // served mirror from the local copy, not by a git push to the relay.
    bool relayPublishRepo(const RepositoryRecord &repo, QString *localBranch,
                          int *unpublished) const;
    void showChatView();           // open the chat view from the top-bar button
    void updateChatButton();       // refresh the top-bar chat unread indicator
    bool isChatViewVisible() const; // chat tab open + window active (i.e. being read)
    // Clears the unread marker for the open conversation whenever it becomes
    // actually visible (chat section shown, or window regains focus while
    // already on it) — called from showSection() and changeEvent().
    void clearActiveConversationUnread();
    // Show/hide the in-transcript unread banner and update its count text.
    void updateChatUnreadBanner();
    // Mark every conversation read at once (from the unread banner's arrow) and
    // jump the current transcript to the newest messages.
    void markAllChatRead();
    void updateConnectionStatus(); // top-right "● Connected · N nodes online"
    // Take this node online / offline from the top-bar toggle. Offline stops the
    // reward heartbeat and live repo serving (so the node stops collecting
    // rewards) while leaving the user in the app; online resumes both.
    void setNodeOffline(bool offline);
    // Refresh the top-bar reward toggle, status line and "online Xh" uptime.
    void updateNodeOnlineControls();
    // Bottom quick-add issue bar (the network log now lives in its own section).
    QWidget *buildNetworkLogDock();
    // One-line strip pinned to the very bottom of the window (adhoc #2).
    QWidget *buildStatusBar();
    // Compact footer queue between the live log and agent prompt. It is always on
    // screen (reading "idle" when nothing is running) and gives each kind of job
    // a spinner plus a one-word tag ("git", "net", "fork" …); past five tags it
    // scrolls.
    quint64 beginBackgroundTask(const QString &kind,
                                const QString &detail = QString());
    void finishBackgroundTask(quint64 id, bool success,
                              const QString &detail = QString());
    // BackgroundActivity listener body, always run on the GUI thread.
    void noteBackgroundActivity(quint64 id, const QString &kind,
                                const QString &detail, bool backgrounded,
                                bool started);
    // Spin the glyphs and reconcile the visible rows with the open tickets.
    void tickBackgroundQueue();
    // Tally a finished run of one kind of work for the log's ✓ / ✕ outcome line,
    // and emit the tallies that are ready (or all of them, when force is set).
    void recordBackgroundOutcome(const QString &word, qint64 elapsedMs,
                                 const QString &detail, qint64 now,
                                 bool backgrounded);
    void flushBackgroundOutcomes(bool force);
    // Collapse a caller's note to the single lowercase word shown in the strip.
    static QString backgroundTaskWord(const QString &kind);
    // Refresh the footer's centered git-identity label for the open repo.
    void updateFooterGitIdentity();
    // Live CPU/memory readout + UI-stall watchdog (footer diagnostics).
    void startDiagnostics();
    void updateFooterDiagnostics();
    void onUiStall(qint64 peakMs, const QString &blockingCall, const QString &backtrace);
    // If "auto-create an agent task for new stalls" is on, hand a freshly-detected
    // stall's backtrace to a coding agent so the freeze gets fixed (adhoc #205).
    // De-duped by backtrace so one recurring freeze files a single task.
    void maybeAutoFileStallAgent(qint64 peakMs, const QString &backtrace);
    // Repo whose checkout a stall-fix agent runs in: ForkMesh's own source tree
    // (the freeze is in this app's GUI thread), else the Issues tab's repo, or -1.
    int stallReportRepoIndex() const;
    // Wipe every recorded UI stall (in-memory count/log and the durable on-disk
    // log) so diagnostics start fresh. Backs the dialog's Clear button.
    void clearStallLog();
    // Hand every recorded UI stall to a fresh coding agent as one task. Returns
    // true if an agent was started. Backs the dialog's "Send to a new agent" button.
    bool sendStallLogToAgent();
    void showDiagnosticsDialog();
    void showHighMemoryProcessPanel();
    void refreshHighMemoryProcessTable();
    void killHighMemoryProcess(qint64 pid, const QString &name);
    // Full-height "Log" section (section 4) showing the whole network log.
    QWidget *buildLogSection();
    void showCloudflareWorkerLogs();

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
    void fetchFaviconForHost(const QString &host);
    void fetchFaviconFromUrl(const QString &host, const QUrl &url);
    QPixmap faviconFor(const ServerConfig &server) const;
    // Network-log favicons (adhoc #190): show each request's site icon inline.
    // Both log views (the full Log tab and the footer strip) render the tag, so
    // the icon resource is registered per target document (adhoc #436).
    QString logFaviconTag(const QString &message, QTextEdit *view);
    void registerLogFaviconResource(const QString &host, QTextEdit *view);
    void refreshLogFavicon(const QString &host);
    QWidget *buildHomeSection();
    // Node profile: full-page centered section (index 10 in m_sectionStack).
    QWidget *buildNodeProfileSection();
    QWidget *buildNodeProfilePanel(); // builds inner scroll area; called by buildNodeProfileSection
    // navigate=false populates the panel in place (Settings > Profile tab)
    // instead of re-homing it and switching to the full-page section.
    void showNodeProfile(const QString &nodeId, const QString &nodeName,
                         bool navigate = true);
    // There is only ever one profile panel, so it is moved between its own
    // full-page section and the Settings > Profile tab on demand.
    void hostNodeProfilePanel(bool inSettings);
    bool profilePanelInSettings() const
    {
        return m_nodeProfilePanel && m_settingsProfileHost &&
               m_nodeProfilePanel->parentWidget() == m_settingsProfileHost;
    }
    // Pull the panel into the Settings > Profile tab (and refresh it with your
    // own node) whenever that tab is the visible one.
    void syncSettingsProfileTab();
    void refreshProfileHostingStats(); // rebuild the per-repo hosting lines
    void refreshProfileAccountStatus(); // "USER ACCOUNT" section: link state + CTA
    void renderProfileAccountStatus();  // paint the section from cached state only
    QString linkedNodesHtml() const;    // "<b>a</b>, <b>b</b>" of m_profileLinkedNodes
    // Second-hop lookup: fetch the owning user's nodes list for a child node so
    // its profile can show the whole fleet, not just "linked to user X".
    void fetchLinkedNodesFromOwner(const QString &node, const QString &owner);
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
    // Settings -> Security tab: private vulnerability reporting form.
    QWidget *buildVulnReportTab();
    void submitVulnerabilityReport();
    // Settings -> MCP tab: mint/revoke the connector token that lets an
    // external MCP agent work this node's issues and PRs, and show the exact
    // config to paste into that agent (adhoc #16).
    QWidget *buildMcpConnectorTab();
    void refreshMcpConnectorTab();
    void generateMcpConnector();
    void revokeMcpConnector();
    void testMcpConnector();
    QString mcpServerScriptPath() const;
    // Settings -> Quick Setup tab: provision a fresh instance in one pass —
    // identity, workflow credentials and world appearance applied together.
    QWidget *buildQuickSetupTab();
    // Settings -> Data tab: where configuration data is stored, per-directory
    // file/folder breakdown, open/delete, and export/import as a .tar.gz backup.
    QWidget *buildDataSection();
    void refreshDataDirTable();
    void exportConfigData();
    void importConfigData();
    void deleteDataDir(const QString &label, const QString &path, bool critical);
    void deleteAllData();
    void setDataStatus(const QString &text, bool error = false);
    void stopLiveServicesForDataOp();
    void relaunchForkMesh();
    // Settings -> Data tab: hourly local snapshots of the live database.
    // startAutoBackups() arms the hourly timer (and catches up when the app was
    // shut for longer than an hour), takeBackupNow() runs one `tar` in the
    // background, and restoreConfigArchive() unpacks any snapshot over the live
    // data through the same swap-and-relaunch path as a manual import.
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
    // Network repository catalog: all repos known by the active relay, with local
    // fork/mirror actions and the relay's mirror-node list per repo.
    QWidget *buildNetworkReposSection();
    void refreshNetworkReposPage();
    void renderNetworkRepos(const QJsonArray &repos);
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
    void provisionDirectMirrorEndpoint(bool dryRun);
    bool rebuildDirectMirrorGatewayConfiguration(
        QString *error = nullptr, bool restartRunningGateway = false);
    void startDirectMirrorServices();
    void stopDirectMirrorServices();
    void registerDirectMirrorEndpoint();
    void checkDirectMirrorGatewayHealth();
    void appendControlNodeOutput(const QString &text);
    void connectToDeployedRelay(const QString &hostname);
    void deploySavedHostsFromControl();
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

    // Hosts (adhoc #263): SSH into a remote machine and run the ForkMesh
    // installer over a verified TOFU connection, streaming the live session
    // output. OpenSSH agent/default-key auth is preferred; an optional password
    // is retained only in this MainWindow's memory. Once the install finishes
    // the new node joins the network and shows up in the per-repo Mirror nodes
    // list on its own.
    QWidget *buildHostsSection();
    // forceUploadBinary bypasses the "Upload the release from this app"
    // checkbox (used by the per-row / install-all-from-binary buttons, which
    // are always direct-upload regardless of the form's checkbox state).
    // onFinished, if given, is called once with whether the install succeeded
    // — used to chain installs when running against every saved host.
    // reinstall passes FORKMESH_REINSTALL=1 to the hosted installer so it wipes
    // the host's existing install + data before installing fresh (adhoc #258).
    // fromSource passes FORKMESH_FROM_SOURCE=1 FORKMESH_RESTART=1 to the hosted
    // installer so it clones/pulls the latest source, rebuilds the client and
    // stops+relaunches the daemon — an update straight from source without
    // waiting for a published release (adhoc). A source build never uploads this
    // app's binary, so fromSource forces the direct-upload path off.
    // suppressFailureStatus is set by callers that will retry a failed attempt
    // themselves (the Vultr auto-provision flow): it skips the terminal
    // "Install failed" status/host-list update so a retryable hiccup doesn't
    // read as a final failure before the caller's own retries are exhausted.
    void runHostInstall(bool forceUploadBinary = false,
                        std::function<void(bool)> onFinished = {},
                        bool reinstall = false, bool fromSource = false,
                        bool suppressFailureStatus = false);
    // SSH into a saved host and run the hosted uninstaller (uninstall.sh),
    // which removes the ForkMesh binary, launcher and ALL of that host's data.
    void runHostUninstall();
    // Open a live SSH tail of a saved host's node log file (when available).
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
    // install the agent CLIs" option (adhoc #418). With copyCredentials the
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
    // Fleet-wide deploys (adhoc): each runs against EVERY saved host in
    // parallel, streaming into its own pane of the split live-output grid — a
    // published, checksum-verified binary install (#257), an
    // uninstall+reinstall (#258), or an update straight from source. Thin
    // wrappers over runHostDeployAllParallel.
    void runHostInstallAllFromBinary();
    void runHostReinstallAllFromBinary();
    void runHostUpdateAllFromSource();
    void appendHostInstallLog(const QString &text);
    // Parallel fleet deploy (adhoc): deploy against EVERY saved host at once,
    // each host streaming into its own pane of a split live-output grid, so the
    // whole fleet updates simultaneously and its live output is visible side by
    // side. mode selects install-from-binary, uninstall+reinstall or
    // update-from-source; the runHost*All* drivers above are thin wrappers that
    // confirm and then call this.
    enum class FleetDeployMode { InstallBinary, Reinstall, UpdateSource };
    struct FleetDeployOptions {
        bool uploadBinary = false;
        bool reinstall = false;
        bool fromSource = false;
        bool requirePublishedBinary = false;
    };
    static FleetDeployOptions fleetDeployOptions(FleetDeployMode mode);
    // One host's slice of a parallel fleet deploy: its own SSH process, output
    // pane and its own copy of the ANSI-render + link-detect state that the
    // single-log path keeps in the m_hostInstall* members.
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
    // Shared SSH command builder used by both the single-host runHostInstall and
    // the parallel fleet path. Fills sshArgs/remoteCmd (and, for a binary upload,
    // the bytes to stream on stdin); returns false with a message in *errorOut on
    // failure (unresolved installer URL, unreadable local binary). When
    // requirePublishedBinary is true, the remote installer is forced to use the
    // latest published checksum-verified release and its reported version is
    // checked before that host is marked successful.
    bool buildHostInstallCommand(const QString &ip, const QString &user,
                                 const QString &node, bool uploadBinary,
                                 bool reinstall, bool fromSource,
                                 bool requirePublishedBinary,
                                 QString *remoteCmd, QByteArray *uploadBytes,
                                 QString *errorOut);
    // Save non-sensitive host metadata from the form without running the
    // installer. pass is cached only for the current process; it is never
    // written to QSettings. identityFile records the ForkMesh-managed private
    // key path for auto-provisioned hosts (public metadata, no key material);
    // when empty, an already-saved path for the same node is preserved.
    void addHostFromForm();
    void rememberHost(const QString &name, const QString &ip, const QString &user,
                      const QString &pass, const QString &status = QStringLiteral("installed"),
                      const QString &identityFile = QString(),
                      const QJsonObject &metadata = QJsonObject());
    // The managed private-key path configured for a host. The SSH command
    // builder validates that it still exists and fails closed instead of
    // silently falling back to a password.
    QString savedHostIdentityFile(const QString &name, const QString &ip,
                                  const QString &user) const;
    void refreshHostsTable();
    void probeSavedHosts();
    void probeSavedHost(const QString &name, const QString &ip,
                        const QString &user, const QString &savedStatus);
    // Drop a saved host from this app's list only \xe2\x80\x94 no SSH session is
    // opened and nothing is changed on the remote host itself. Use Uninstall
    // instead to actually remove ForkMesh from the host.
    void forgetHostAtRow(int row);
    // --- One-click Vultr mirror (adhoc #315) ---------------------------------
    // Create a brand-new mirror VPS on the user's Vultr account: pick the
    // cheapest plan and newest Debian via the Vultr v2 API, create/reuse the
    // ForkMesh-managed SSH key, boot the instance, then hand off to the normal
    // runHostInstall flow which installs ForkMesh and auto-links the node to
    // this account. The API key lives in memory only for the duration of the
    // run; it is never written to QSettings or argv.
    void createVultrMirrorFromForm();
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
    // Do not report a provisioned mirror as complete merely because SSH and
    // systemd succeeded. Wait until its signed flagship catalog row is visible
    // through the public relay—the same source used by Mirror nodes and World.
    void waitForVultrMirrorPublication(const QString &node,
                                       const QString &successMessage,
                                       int attempt = 0);
    void finishVultrProvision(bool ok, const QString &message);
    // Print the per-attempt record collected for this provision run into the
    // install log, so a finished run shows what every attempt did (adhoc #342).
    void appendVultrAttemptHistory();
    // Cloudflare API v4 call for the DNS record a fresh Vultr mirror needs
    // (adhoc #331). Same shape as vultrApiCall: the token travels only in the
    // Authorization header of this HTTPS request.
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
    // Reload a saved host's server info from the table. A password is restored
    // only when it remains in this process's session cache.
    void loadHostIntoForm(int row, int column);
    // URL of the hosted installer script the remote host curls and runs.
    QString installScriptUrl() const;
    // URL of the hosted uninstaller script the remote host curls and runs.
    QString uninstallScriptUrl() const;

    // Relays: a sibling of Hosts that lists the configured mainnode relays with
    // their live online status, round-trip response time and running version.
    // Each row is filled by probing that relay's lightweight /api/version endpoint.
    QWidget *buildRelaysSection();
    void refreshRelaysTable();   // re-list relays and (re)probe each one
    void probeRelayRow(int row); // measure latency + read version for one relay
    // Nodes: a sortable directory of every node this client knows about (the same
    // nodes offered by the top-bar node dropdown), showing each node's platform,
    // online state, owner, version, repo/mirror counts and telemetry. Selecting a
    // row opens a detail panel with that node's details plus the repositories it
    // hosts and mirrors (adhoc #9, #27).
    QWidget *buildNodesSection();
    void refreshNodesTable();           // re-list the known nodes into the table
    void showNodeDetailForRow(int row); // fill the detail panel for a table row
    // Fetch the relay's list of currently-online node names (/api/network/stats
    // "onlineNodes": repository update channel or fresh signed heartbeat). Headless
    // mirror nodes serve through the relay without joining this client's chat
    // room, so room presence alone painted them offline (adhoc #27).
    void fetchRelayOnlineNodes(bool force = false);
    // Firewall: whitelist-only outbound request gate for traffic created by
    // ForkMesh's shared network manager.
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
    // Network diagnostics: live websocket / Durable Object details.
    QWidget *buildNetworkDiagnosticsSection();
    void refreshNetworkDiagnostics();
    void showEndpointRequestDetails(int row, int column);

    // Repo detail view (files + issues tabs), opened by clicking a repository.
    void ensureRepoDetailSectionBuilt();
    void ensureRepoDetailTabBuilt(int index);
    QWidget *buildRepoDetailSection();
    QWidget *buildRepoFilesPanel();
    QWidget *buildRepoOverviewPage();
    QWidget *buildRepoEditorPage();
    QWidget *buildRepoCoveExplorerPage();
    QWidget *buildRepoCommitsTab();
    void showCommit(const QString &hash); // open the commit diff detail view
    // showCommit's synchronous tail: pure widget population once the async
    // `show -s` metadata and `diff -M` patch are both in hand.
    void renderCommitDetail(const QString &dir, const QString &hash,
                            const QStringList &metaFields,
                            const QByteArray &patchRaw);
    void showCommitList();                // reset the right pane to working changes
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
    // Open the issue referenced by "#<number>" in a commit message.
    void openCommitReference(int number);
    void openIssueReference(int number);
    void openPullReference(int number);
    void openCommitHashReference(const QString &hash);
    void openReferenceLink(const QString &href);
    // Global search (Ctrl+K, issue #360). A repo-scoped overlay that searches the
    // open repo's issues and pull requests (titles, bodies and comments — parsed
    // from the stores) plus its code (`git grep` on the working tree/mirror, run
    // off the GUI thread). Results are clickable and jump to the matching issue,
    // PR, or file+line. Repo-scoped only for v1; full-mesh code search is out.
    void openGlobalSearch();
    void runGlobalSearch(const QString &query);
    QDialog *m_searchDialog = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QListWidget *m_searchList = nullptr;
    QLabel *m_searchStatus = nullptr;
    // Bumped on every keystroke so a stale off-thread `git grep` result is dropped
    // when the query has moved on (mirrors the m_worktreeStatusGen pattern).
    int m_searchGen = 0;
    // Resolve a reference link clicked inside an issue/PR comment body. Handles
    // the private schemes autolinkReferences() emits (forkmesh-ref:N → issue/PR,
    // forkmesh-commit:SHA → commit) and forkmesh:// permalinks (issue/pull/commit);
    // anything else opens externally.
    void openBodyReference(const QString &href);
    // Copy a forkmesh://<kind>/<owner>/<repo>/<id> permalink for the open PR or
    // commit to the clipboard (owner/repo from the repo detail view). Pasting it
    // into a comment renders a link via autolinkReferences() (issue #154).
    void copyReferenceLink(const QString &kind, const QString &id);
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
                             const QString &accent = QString(),
                             const QString &copyLink = QString(),
                             const QString &authorId = QString(),
                             const std::function<void()> &onDelete = {});
    QWidget *buildRepoSecurityTab();
    QWidget *buildRepoQualityTab();
    QWidget *buildInsightsTab();
    // Size map tab (adhoc #189): sunburst of the working tree's directory
    // sizes. refreshSizeMapTab scans on a worker thread; force=false is the
    // lazy tab-click path that reuses the last scan of the same repo.
    QWidget *buildSizeMapTab();
    void refreshSizeMapTab(bool force);
    // Folder the size map scans: m_sizeMapRootOverride when the user picked one
    // with "Choose folder…", otherwise this repository's working copy.
    QString sizeMapRoot() const;
    void chooseSizeMapFolder();
    void setSizeMapRootOverride(const QString &path);
    // Filesystem shortcuts beside the map (adhoc #21): one small used/free map
    // per mount point, clicking one re-roots the full scan there.
    QWidget *buildSizeMapVolumesPanel();
    void refreshSizeMapVolumes();
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
    void deleteDiscussionComment(int number, const QString &eventId);
    void startDiscussionFromComposer();
    static QString discussionTitleFromBody(const QString &body);
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
    // Push/sync refreshes use the worker-backed variant so deriving every PR's
    // patch/commit series never serializes git on the event thread.
    void reloadPullsInBackground();
    void applyLoadedPulls(const PullStore &store, QList<PullRequest> pulls,
                          const QString &baseTip);
    // Drains m_pendingPullConflictChecks one PR per event-loop turn so the (slow)
    // `git apply --check` dry-runs never block the GUI thread in a single sweep.
    void processPendingPullConflicts(quint64 gen);
    // Queue a single PR's dry-run apply for the async pass (deduped against any
    // pending entry) and kick off the drain if it's idle. Lets the merge-status
    // path defer a cold-cache check instead of blocking the GUI on it.
    void queuePullConflictCheck(int number, const QString &fingerprint);
    // Set/clear the conflict badge on a single pull-list row, in place, so async
    // badge updates don't rebuild (and flicker) the whole table.
    void setPullConflictBadge(int number, bool conflict);
    // Tooltip for a PR's conflict badge: the "why" behind the flag — the files
    // whose patch no longer applies, pulled from m_pullConflictCache. Falls back
    // to the bare "has merge conflicts" line when the file list isn't cached.
    QString pullConflictBadgeTooltip(int number) const;
    void refreshPullList();
    void showPull(int number);
    // Files-changed authorship filter: show only agent- or human-authored files
    // in the current PR, driven by m_pullFileAuthorFilter (issue #365).
    void applyPullFileAuthorFilter();
    // Takes the PR by value: runIdsForPull() below pumps the event loop, which
    // can re-enter reloadPulls() and reassign m_currentPulls — a reference into
    // it would dangle mid-call (adhoc #119).
    void renderPullReviewSummary(PullRequest pr);
    // Render every changed file of the current PR into one continuously
    // scrollable diff view (issue #250), so the reviewer can scroll the whole PR
    // and the file list / Prev-Next jump between files.
    void renderPullDiff();
    // Scroll the all-files diff so the given file's section is at the top.
    void scrollPullDiffToFile(const QString &filePath);
    // Follow the diff scroll (adhoc #56): keep the sticky header showing the
    // topmost visible file, advance its Pac-Man progress chart as the file
    // scrolls past, and select that file in the list — without bouncing the
    // diff back to the file header. Cheap (no re-render); runs on every tick.
    void updatePullDiffScrollState();
    // Select a file in the changed-files list without scrolling the diff back to
    // it (used while the selection follows the scroll).
    void selectPullFileInList(const QString &filePath);
    // Position the sticky header overlay across the top of the diff viewport.
    void layoutPullStickyHeader();
    // Walk the rendered diff once, caching each file header's absolute y into
    // m_pullFileTops so the per-scroll-tick sticky-header update stays cheap.
    void computePullFileTops();
    // Debounced off the diff view's scrollbar (issue: auto-mark viewed on
    // scroll): while m_pullAutoViewedButton is checked, marks every file that
    // has scrolled entirely above the viewport as "Viewed" and re-renders,
    // restoring the scroll position to the file still on screen.
    void applyAutoMarkViewedOnScroll();
    void adjustDiffFont(int delta); // +/- diff text-size zoom (issue #254)
    // Register a diff viewer so it shares the text-size zoom: tracks it for the
    // +/- buttons and watches its viewport for Ctrl+wheel (issue #254).
    void registerDiffView(QTextEdit *view);
    // Set a diff viewer's HTML, remembering the source so a later font-size
    // change can re-render it in place without re-running its renderer. Renders
    // progressively (renderDiffStreamed): the visible window first, the rest off
    // the event loop, so no diff ever blocks the GUI thread (adhoc #421).
    void setDiffHtml(QTextEdit *view, const QString &html);
    // Hook run when a diff view's streamed document is complete; refreshes the
    // state derived from the whole document (sticky file positions, search).
    void onDiffStreamFinished(QTextEdit *view);
    // Scroll the Files-changed diff to the next/previous change relative to what
    // is currently on screen. delta is +1 (next) or -1 (prev).
    void pullSelectAdjacentChange(int delta);
    // Scroll the pull diff to the next/previous hunk header relative to the
    // current scroll position; returns false when there is no further hunk in
    // that direction.
    bool pullScrollToAdjacentHunk(int delta);
    // Text search over the Files-changed diff (issue #333): show/hide the find
    // bar, recompute matches against the current diff text, and step between
    // them.
    void togglePullDiffSearch(bool show);
    void pullDiffSearchRecompute();
    void pullDiffSearchGoTo(int delta);
    // Handle a click on a diff line-number anchor ("cmt:<path>?s=<side>&l=<line>"):
    // prompt for a comment and attach it to that line of the PR file.
    void onPullDiffAnchorClicked(const QUrl &url);
    void submitPullThreadReply(const QString &threadId);
    void setPullThreadState(const QString &threadId, const QString &state);
    void renderPullThread(const PullRequest &pr);   // review/comment conversation
    void renderPullCommits(PullRequest pr);         // commits that make up the PR
                                                    // (by value: pumps a git read,
                                                    // see adhoc #119/#124)
    // The next two and runIdsForPull/updatePullSubTabCounts take the PR by
    // value on purpose: they pump the event loop (git reads), and callers often
    // pass references into m_currentPulls, which a nested reloadPulls() can
    // reassign mid-call (adhoc #119).
    void renderPullChecks(PullRequest pr);          // action runs for the PR's commits
    void renderPullChecksSummary(PullRequest pr);   // inline conversation card
    void showPullCheckLog(int runId);               // load a run's log into the panel
    QStringList pullCommitShas(const PullRequest &pr) const; // base..head SHAs
    QList<int> runIdsForPull(PullRequest pr) const; // matching action runs
    void runChecksForCurrentPull();                 // enqueue workflows at PR head
    // Check out the PR's head into a throwaway worktree, build the ForkMesh app
    // from it, and launch the freshly built binary as an isolated preview node so
    // the reviewer can try the change running before merging (issue #214).
    void buildAndPreviewCurrentPull();
    void updatePullSubTabCounts(PullRequest pr);
    void refreshOpenPullChecks();                   // re-render checks for the open PR
    // Which event a workflow run is being queued for: a code push (the default)
    // or a published release. Selects the matching `on:` trigger to enqueue.
    enum class WorkflowTrigger { Push, Release };
    // Enqueue every workflow found at `commit` for owner/name whose `on:` matches
    // `trigger`. Shared by the push handler, the PR "Run checks" button, and the
    // Releases panel's "Publish release" action.
    void queueWorkflowsForCommit(int repoIndex, const QString &owner,
                                 const QString &name, const QString &commit,
                                 const QString &ref,
                                 WorkflowTrigger trigger = WorkflowTrigger::Push);
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
    // Continue the agent session that originally authored this PR's branch,
    // asking it to merge the base branch in and resolve conflicts itself — the
    // same flow as the agent detail view's "Fix conflicts with agent" button.
    // Only available when such a session is attached and idle.
    void fixCurrentPullConflictsWithOriginatingAgent();
    void editCurrentPullFile();         // edit the selected file on the PR's branch
    void deleteCurrentPullFile();       // delete the selected file on the PR's branch
    void closeIssuesLinkedFromPull(const PullRequest &pr);
    // Close each still-open issue in `numbers` as resolved by a just-merged change,
    // posting `comment` into the issue thread and logging it as closed "via" `via`
    // (e.g. "pull request #5" or 'branch "fix-x"'). Refreshes the issue list and
    // counters when anything changed, and returns the numbers actually closed so
    // callers can word their own status message. Shared by the pull-request merge
    // (closeIssuesLinkedFromPull) and the worktree/branch merge flows (adhoc #23).
    QList<int> closeIssuesForMerge(const QList<int> &numbers,
                                   const QString &comment, const QString &via);
    // Migration-only compatibility hook for historical issue bounties. New
    // Worker-held escrow creation is disabled and this performs no transfer.
    void fundBountiesForMergedPull(const PullRequest &pr);
    // Migration-only compatibility hook: clears stale automatic PR-bounty
    // preferences without contacting the retired custody API.
    void autoBountyForMergedPull(const PullRequest &pr);
    // Retained for source compatibility; legacy escrow polling is disabled.
    void pollBountyPayout(const RepositoryRecord &repo, int number, double amount,
                          const QString &kind = QString());
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
    // Issue #156: after an agent opens a PR for the issue it was working, record an
    // explicit "Linked pull request #M" note on that issue so the link is durable
    // in the Development section, the same way a manual link is. Repo-aware: the
    // agent's repo may differ from the one currently on screen.
    void linkAgentPullToIssue(const AgentSession &session, int prNumber);
    void closeCurrentPull();
    void reopenCurrentPull();
    // Deliver the open PR on screen to the repo owner's inbox (the relay queues it
    // so it lands even if the source-of-truth node is offline). Shown on mirror
    // nodes, which can't merge locally.
    void sendCurrentPullToSource();
    void deleteCurrentPull();
    void deleteCurrentPullAndBranch();
    // Merge the PR, then delete it and its head branch in one confirmed step
    // (issue #261). The merge runs first and synchronously; only if it succeeds
    // do we drop the PR record and its branch.
    void mergeAndDeleteCurrentPull();
    // Bulk-delete every merged PR (and its head branch, where safe) in the
    // current repo in one confirmed step. Runs the deletions one at a time,
    // chaining through deletePullAndBranchAsync's onDone callback so a single
    // PullStore worker is never touched concurrently.
    void deleteAllMergedPullsAndBranches();
    // Shared worker: delete a PR record and (best-effort) remove its local head
    // branch, reporting through the repo-detail notice. The caller owns the
    // confirmation (and any prior merge); pass propagate=true to push the result
    // to the mirror (the merge-and-delete flow needs the merge to reach peers).
    // onDone fires after the worker settles (success or failure) so callers can
    // chain further work, e.g. the next deletion in a bulk run.
    void deletePullAndBranchAsync(int number, const QString &head, bool haveBranch,
                                  bool rewriteHistory, bool propagate,
                                  std::function<void()> onDone = {});
    void setPullDeleteButtonsEnabled(bool enabled);
    // Confirm a PR deletion; *rewriteHistory is set from an opt-in checkbox
    // (off by default — a plain delete is fast and leaves history intact).
    bool confirmPullDeletion(const QString &prompt, bool *rewriteHistory);
    void syncPullsInbox();
    void submitPullToInbox(const PullRequest &pr);
    // `quiet` suppresses the modal delivery confirmation (used for automated
    // agent-completion submissions, which report through the session log/status
    // bar instead of a dialog the looper would have to wait on).
    void submitPullToInbox(const PullRequest &pr, const RepositoryRecord &targetRepo,
                           bool quiet = false);
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
    // Populate (or update in place) one Agents-table row's cells for a session.
    // Creates each column's item when the row is empty (a full rebuild) and
    // otherwise rewrites the existing items, so refreshAgentTable() can reuse the
    // rows instead of clearing the table when the visible set is unchanged — which
    // is what stops the list flashing / the Status column blanking when a queued
    // message re-refreshes it (adhoc #74).
    void applyAgentRowCells(int row, const AgentSession &session,
                            const QString &agentGitDir, const QString &agentBase);
    // Files-changed + branch ahead/behind summary for a session's Diff cell.
    // This is deliberately a cache-only UI accessor: cold disk/git probes are
    // gathered by refreshAgentTable() on its worker and delivered later.
    AgentDiffStat agentDiffStat(const AgentSession &session, const QString &gitDir,
                                const QString &base);
    void updateAgentTokenCell(int sessionId);  // live Tokens-column update
    void updateAgentCostCell(int sessionId);   // in-place Cost-column update
    void updateAgentRunSummaryCells(int sessionId); // in-place Turns/Time update
    void updateAgentStatusCell(int sessionId); // in-place Status-column update
    void refreshAgentStatusPill(int sessionId); // in-place detail-header pill update
    void animateRunningAgentIcons();           // spins running rows' Status glyph
    // Pulse a session's night-rider light so the agents-list activity column
    // sweeps while its raw output is streaming; onScannerTick drives the frames.
    // bytes is how much just streamed, which drives the live-output intensity
    // effect (the sweep's speed/brightness/colour); 0 applies a default bump.
    void noteAgentActivity(int sessionId, int bytes = 0);
    void onScannerTick();
    void showAgentSession(int sessionId);
    // Rebuild only the detail header's meta lines (identity + issue/branch/worktree
    // /PR chips + run Stats), without a transcript rebuild — used by the live
    // token/cost/run-summary update paths and the running-row ticker (adhoc #42).
    void refreshAgentDetailMeta(int sessionId);
    // Detail-header permission-mode selector (adhoc #26): sync the combo to the
    // shown session (and hide it for providers without a mode), and apply a live
    // change back onto the selected session.
    void syncAgentModeSelector(const AgentSession &session);
    void applySelectedAgentMode();
    // Populate the raw-log QPlainTextEdit (m_agentLog) only when the content
    // actually changed. setPlainText()+moveCursor(End) forces a full document
    // layout, which for a large transcript blocks the GUI thread for seconds
    // (adhoc #245). refreshAgentTable() re-selects the open session on every
    // reload/external tick, re-firing showAgentSession with identical text, so
    // skip the re-layout when neither the session nor its log has changed.
    void setAgentLogText(int sessionId, const QString &text);
    // Authoritative cumulative token total for a session: the live running
    // counter (m_sessionTokens) clamped to never fall below the value persisted
    // on the session. Reloading sessions from disk mid-run would otherwise reset
    // the detail panel's "Session token usage" back to a stale/zero figure before
    // climbing again, so both the table cell and the detail line read this.
    qint64 sessionTokenTotal(const AgentSession &session) const;
    // Seed m_sessionTokens from the persisted totals (taking the max) so the live
    // counter survives reloads and continues from the saved base, not from zero.
    void seedSessionTokens();
    // Refresh the "Session token usage" line for one session. Since issue #84
    // this no longer paints a detail-panel label — it feeds the figures into the
    // top-bar usage chart's hover tooltip (TokenUsageMiniChart::setStats).
    void setAgentUsageLabel(const AgentSession &session);
    // A session log read + its "[net]" marker counters, produced off the GUI
    // thread by showAgentSession (the log can be megabytes; reading and regex-
    // scanning it per click is what paused the radar between agent clicks).
    struct AgentLogScan {
        QString log;
        int requests = 0, responses = 0, errors = 0;
        qint64 inTokens = 0, outTokens = 0;
    };
    // Count a log's "[net]" markers. Pure; safe on a worker thread.
    static AgentLogScan scanAgentLog(QString log);
    bool m_agentNetScanInFlight = false; // one live [net] rescan at a time
    // Render pre-scanned "[net]" counters into the traffic graphic. Pure UI.
    void applyAgentNetworkPanel(const AgentLogScan &scan, const QString &status);
    AgentSession *findAgentSession(int sessionId);
    const AgentSession *latestAgentSessionForIssue(int issueNumber) const;
    // The agent session attached to a PR, if any. Matches the PR number first
    // (an agent that recorded the PR it produced), then — when a head branch is
    // given — falls back to a session run on that same branch (issue #257: an
    // agent attached "through the branch" even though it never recorded a PR
    // number). The branch fallback is scoped to the detail repo to avoid matching
    // a like-named branch in another repo.
    const AgentSession *agentSessionForPull(int prNumber,
                                            const QString &headBranch = QString()) const;
    // Issue #291: flag agent sessions whose worktree/PR has landed in the base
    // branch. markAgentSessionsMerged() records it eagerly when ForkMesh merges
    // a PR/worktree; refreshAgentMergeState() is the catch-all run on reload (it
    // also picks up merges synced from peers or done by hand) — its per-branch
    // git reads run on a worker thread, and markAgentSessionsLanded() applies
    // the verdicts (by session id) back on the main thread.
    bool markAgentSessionsMerged(int prNumber, const QString &branch);
    void refreshAgentMergeState();
    void markAgentSessionsLanded(const QList<int> &sessionIds, bool refreshUi);
    void assignIssueToAgent(const QString &provider, const QString &model = QString());
    // Core of assignIssueToAgent, factored out so the issue looper can drive it
    // for any issue (not just the selected one). Returns the new session id, or 0
    // on failure. quiet suppresses the inline notice + Agents-tab switch the
    // manual assign path shows. model is the provider-specific model id/alias
    // picked in the sidebar; empty leaves the provider's own default. repoHint
    // names the issue's repo explicitly for callers running outside the Issues
    // tab (e.g. an inbox drain for a repo that isn't the one on screen); when
    // null the selected repo (issuesRepoIndex()) is used, as before.
    int startAgentForIssue(const Issue &issue, const QString &provider, bool createPr,
                           bool quiet = false, const QString &model = QString(),
                           const RepositoryRecord *repoHint = nullptr);
    // Issue looper (adhoc #92): start an agent on the next open issue, watch it to
    // completion, then automatically start the next — working through the open
    // backlog one issue at a time until toggled off or the backlog is exhausted.
    void toggleIssueLooper();
    void looperStartNext();
    void looperOnSessionFinished(int sessionId);
    // adhoc #38: stamp this node onto an issue's assignees the moment the looper
    // takes it, so the claim syncs to every node and no second looper (here or on
    // another mirror) starts the same task. The host appends the node and commits
    // (which syncs to mirrors); a mirror with no write access files the signed
    // assignees event to the owner's inbox, which merges and syncs it back.
    void looperClaimIssue(int number, const QStringList &existingAssignees);
    // The label the looper assigns to mark a claimed issue: this node's display
    // name, or a public-key prefix when no name is set (adhoc #38).
    QString nodeAssigneeTag() const;
    // Funnel for every looper state change: refresh the inline toggle in the
    // Issues heading row and persist the running state so the loop resumes
    // after a restart (adhoc #130, #125, #354).
    void updateIssueLooperButton();
    void persistLooperState();
    void maybeRestoreIssueLooper();
    void continueSelectedAgentSession();
    // Same as continueSelectedAgentSession, but for an arbitrary session id —
    // used to resume a session steered from the website (adhoc #182) without
    // disturbing whatever session is currently selected in the UI.
    void continueAgentSession(int sessionId);
    // Ask the given session's agent to merge base and resolve conflicts, then
    // resume it. Driven by the agents list's orange conflict button (adhoc
    // #446); a no-op while that session is already running or queued.
    void fixAgentConflictsWithAgent(int sessionId);
    // Stash the quick-add composer's provider/model/mode dropdowns onto the
    // given session, so the next resume runs with what the user has selected
    // right now. Shared by the follow-up path and the bare "add" (continue,
    // nothing typed) path.
    void applyComposerSelectionToAgentSession(int sessionId);
    // Steer m_selectedAgentSessionId with a follow-up message. Shared by the
    // agent detail composer's Send button and the footer quick-add's up-arrow
    // ("send to the visible agent") button.
    void sendPromptToSelectedAgent(const QString &prompt);
    // Same as sendPromptToSelectedAgent, but for an arbitrary session id
    // (adhoc #182: an authenticated owner-sealed prompt names its session).
    void sendPromptToAgentSession(int sessionId, const QString &prompt);
    // Full issue title + description + every comment, formatted for an agent
    // prompt. Shared by the initial issue-assignment prompt and the "Send
    // issue context" resend action, so a run that missed the context the
    // first time (or was given a bare "Continue" on resume) can be handed
    // the whole thing again on demand (adhoc #256).
    QString issueContextPrompt(const Issue &issue) const;
    // Re-sends the full context of the issue linked to the currently-open
    // agent session (adhoc #256's "Send issue context" action).
    void sendIssueContextToSelectedAgent();
    void deleteSelectedAgentSession();
    // Promote the selected ad-hoc session (no issue) into a tracked issue, then
    // link the two so the detail header shows the issue (adhoc #189).
    void createLinkedIssueForSelectedSession();
    // Stop and remove one stored agent session (clear its issue assignment, drop
    // it from the run queue, delete it from the store). Returns true once it's
    // gone; false (after flashing why) when it can't go yet — a running agent
    // still stopping, or no host write access to clear the issue. External
    // (watch-only) sessions aren't handled here.
    bool deleteStoredAgentSession(int sessionId, bool cleanupWorktree = true);
    // Agent-detail "Delete all": remove the session from the UI/store first,
    // then clean its worktree, branch, and linked issues asynchronously.
    void deleteWorktreeBranchAndAgentInBackground(
        const QString &worktreePath, const QString &branch);
    // Delete everything an agent left behind in one action: its worktree folder,
    // its branch, and the stored agent session(s) that ran on it. Used by the
    // Worktrees-tab "Delete" buttons and the agent detail's "Delete all".
    // confirm=false skips the per-item dialog (the batch "Delete all merged" asks
    // once up front); async=false removes the worktree synchronously so a batch of
    // deletes runs one git worktree-remove at a time rather than racing.
    // deferRefresh=true skips the trailing agent/issue UI reload so a batch caller
    // (deleteAllMergedAgentSessions) can rebuild the table once at the end instead of
    // once per branch — repeated rebuilds mid-batch flickered the Status column blank.
    void deleteWorktreeBranchAndAgent(const QString &worktreePath,
                                      const QString &branch, bool confirm = true,
                                      bool async = true, bool deferRefresh = false);
    // Batch counterpart to "Delete all": wipe the worktree, branch and session of
    // every merged agent session in the open repo after one confirmation (adhoc
    // #235).
    void deleteAllMergedAgentSessions();
    void testOpenAiAgentKey();
    void refreshClaudeSpend();
    // Issue #290: pull the live Claude Code rolling-window utilisation (the same
    // 5-hour + weekly figures the CLI's /usage shows) straight from the claude.ai
    // OAuth usage endpoint. No background timer drives this (adhoc #76), and
    // adhoc #20 dropped every other trigger too — this now only runs when the
    // user hovers the top-bar chart to check the current figures.
    void refreshClaudeCodeUsage();
    // Ask the provider which models this account can drive right now
    // (GET /v1/models) and merge them into the composer's per-session model
    // picker, so the dropdown reflects the live line-up (new releases appear
    // without an app update). Best-effort: on any failure the static defaults
    // from populateClaudeModelCombo() stand. Only called from the top-bar
    // usage chart's hover (adhoc #41) so opening/switching model combos never
    // hits the provider on its own.
    void refreshClaudeModelCombo();
    // Push whatever's already cached in m_liveClaudeModels into every model
    // combo without touching the network. Call this anywhere a combo is
    // built/switched so it reflects the last live fetch; only the hover-driven
    // refreshClaudeModelCombo() actually re-fetches.
    void applyLiveClaudeModelsToCombos();
    // Keep the top-bar Codex meter in sync with live app-server rate-limit
    // updates, falling back to ForkMesh's locally tracked rolling windows.
    void refreshCodexUsageRemaining();
    void applyCodexRateLimits(const QJsonObject &rateLimits);
    // Push one rolling-window utilisation figure (0..100) into every place that
    // shows it: the per-session usage bar, the top-bar mini chart and the
    // persisted cache. `weekly` picks the window.
    void applyClaudeUsage(bool weekly, int percent);
    // Issue #50: feed one window's reset instant (epoch ms, from the OAuth usage
    // endpoint's resets_at) into the top-bar mini chart's tooltip as a "resets in
    // Xh / Xd" countdown, and cache it so the figure survives a restart.
    void applyClaudeReset(bool weekly, qint64 resetMs);
    // Issue #346: when a Claude Code usage window that was previously maxed out
    // (>=99%) drops back down, optionally tell the node's owner by email — the
    // only useful signal for a headless node that has no one watching its
    // screen. Opt-in via kEmailOnCreditsRefillSetting; a no-op when off.
    void maybeEmailCreditsRefilled(bool weekly);
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
    // Footer "Agents:" status strip (adhoc #111): one small status glyph per
    // known agent session, rebuilt from m_agentSessions whenever it changes.
    void refreshAgentStatusRow();
    void animateAgentStatusIcons(); // spins the strip's running icons (adhoc #114)
    // Clicking the "Agents:" label itself (as opposed to one of the dots):
    // jumps to the most relevant session's Agents tab, falling back to the
    // open repo's Agents tab if no session exists yet.
    void openAgentsOverview();
    // Refreshes the count badge on the top-bar Agents nav button from
    // m_agentSessions.size().
    void updateAgentsNavBadge();
    // Repaints the matrix of per-agent squares beside that button: one square
    // per session, tinted like its status icon, with the live output meter of
    // each running session driving its night-rider pulse.
    void refreshAgentDotMatrix();
    void processAgentQueue();
    // Re-drain the queue after a slot frees, coalesced onto the event loop and
    // skipped unless something is queued AND there is room to start it.
    void scheduleAgentQueuePump();
    // How many sessions currently hold one of the maxRunningAgents() slots
    // (adhoc #433): our own, unmerged, actively-executing ones.
    int runningAgentCount() const;
    // The running/waiting/queued sessions "Stop all" acts on, across all repos.
    QList<int> stoppableAgentSessionIds() const;
    // Stop every session above and clear the pending queue (adhoc #433).
    void stopAllRunningAgents();
    // Returns the pooled runner currently executing sessionId, or nullptr.
    AgentRunner *runnerForSession(int sessionId) const;
    // Returns an idle pooled runner, creating (and wiring) a new one if needed.
    AgentRunner *acquireAgentRunner();
    // True while any pooled runner or stream/codex process is executing a session.
    bool anyAgentRunning() const;
    // The sessions anyAgentRunning() is counting, described for the restart log.
    QStringList runningAgentBlockers() const;
    void onAgentLog(int sessionId, const QString &text);
    // Live-append a line to the raw-output edit, but only while it's the visible
    // surface (the buffer carries it otherwise); avoids per-line text-layout stalls.
    void appendAgentRawLog(const QString &text);
    // Show the raw-output edit, rebuilding it from the live buffer first.
    void showAgentRawOutput();
    void onAgentStatusChanged(int sessionId, const QString &status);
    void onAgentFinished(int sessionId, bool ok);
    // The agent CLI needs the user to act (e.g. a bad API key); surface it.
    void onAgentNeedsAttention(int sessionId, const QString &message);
    void updateAgentActionState();
    // Restyle the quick-add "new"/"add" send buttons (adhoc #89) so the one Enter
    // would actually activate — "add" while an agent session is open above (a
    // follow-up message), otherwise "new" (start a fresh agent) — carries a green
    // outline. Called whenever the selected agent session changes via
    // updateAgentActionState.
    void updateQuickAddEnterTarget();
    // Whether Enter in the quick-add composer should follow up on the agent
    // session open above ("add") rather than start a fresh one ("new"). This
    // requires the agent output panel to actually be on screen — otherwise a
    // session selected on a previous visit to that tab would keep stealing
    // Enter from every other section (Chat, Issues, ...). Tested by that
    // panel's own visibility, not by stack indexes, so Enter follows up
    // wherever the transcript is shown from, exactly like the "add" button it
    // mirrors. Shared by the key handler and updateQuickAddEnterTarget so the
    // two can never drift apart.
    bool quickAddShouldFollowUpAgent() const;
    void updateIssueAgentUi(const Issue &issue);
    // Issue #145: populate the issue detail's "Files changed" tab from a linked
    // pull request's patch or a linked agent session's branch diff, and show or
    // hide the tab depending on whether such a source exists.
    void refreshIssueFilesPanel(const Issue &issue);
    void renderIssueDiff(int issueNumber, const QByteArray &patch,
                         const QString &dir, const QString &base);
    // Stamp the issue list's "Files" column with one small icon per attached
    // issue file. Filenames live in the tooltip; sorting uses the attachment
    // count.
    void populateIssueFilesCell(int row, const Issue &issue);
    // Small rounded avatar for an issue assignee (or assigned agent), shown to
    // the left of the title in the issue list. Deterministic per name (a
    // procedural face, matching the contributor avatars) and cached so a full
    // table rebuild stays cheap.
    QIcon issueAssigneeAvatar(const QString &name);
    QHash<QString, QIcon> m_assigneeAvatarCache; // assignee name -> avatar icon
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
    // Stop the currently selected run: abort it if it's executing, or drop it
    // from the queue if it hasn't started yet. Records the run as Cancelled.
    void stopSelectedRun();
    // Skip the currently selected run before it executes: drop it from the queue
    // (or decline it while it's awaiting approval) and record it as Skipped.
    void skipSelectedRun();
    // Start a new coding agent to fix the selected (failed) run, on its own
    // branch/PR like any other ad-hoc agent run (adhoc #114).
    void fixSelectedRunWithAgent(const QString &provider, const QString &model);
    // adhoc #306: if kAutoFixFailuresSetting is on (the default) and run's
    // branch still has an agent session attached, send the failure straight
    // back to that same session instead of waiting for a manual "Fix with
    // agent" click or starting a brand-new agent. No-op if no session ever
    // worked on this branch, or that session is still active.
    void maybeAutoFixFailedRun(const ActionRun &run);
    // Delete every run currently shown in the Runs list (its meta + log on
    // disk); skips any run that's still in flight. Prompts for confirmation.
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
    // Apply a controller-written generation without restarting the headless
    // node, then poll gateway-managed sources into their isolated Actions
    // mirrors. Neither path changes the gateway's serving hook/object store.
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
    // The labels this node answers to when a workflow declares `runs-on:` — its
    // machine node name, its mirror-executor node name, the platform, and any
    // extra labels the operator typed in Settings. A workflow dedicated to
    // another node is never queued here, so a mesh can pin tests to one machine,
    // Cloudflare deploys to a mirror and iOS builds to a Mac.
    QStringList actionNodeLabels() const;
    // Human-readable "this workflow belongs to <node>" text for logs and the UI.
    QString workflowDedicationLabel(const ActionWorkflow &workflow) const;
    void processActionQueue();
    // An encrypted repository is served out of a temporary materialization whose
    // directory is recreated by every sealing pass and deleted as soon as the
    // replacement is installed. A run checks out of, clones from, and lands
    // release artifacts into that directory for its whole lifetime, so a routine
    // re-seal (publishing a release triggers one) can delete the mirror out from
    // under an in-flight build: `git -C <mirror> worktree add` then fails with
    // "cannot change to '/tmp/ForkMesh-XXXXXX/repository.git'" (adhoc #314).
    // Returns the live materialization for this repository — writing its current
    // path into `mirrorPath` when the record lagged behind a re-seal — and keeps
    // that directory alive for as long as the caller holds the returned handle.
    // Null for a plain durable mirror, which needs no pinning.
    std::shared_ptr<void> pinActionMirror(const RepositoryRecord &repo,
                                          QString *mirrorPath) const;
    // Drop the pin taken for `runId`, first carrying any release artifacts the
    // run landed into the mirror that is serving the repository now: a re-seal
    // during a long build leaves the run writing its binaries into a directory
    // that is about to be deleted along with the pin.
    void releaseActionMirrorPin(int runId);
    // The runner currently executing `runId`, or nullptr if no runner is. Used
    // to target stop()/abort at the exact run rather than a single global runner.
    ActionRunner *runnerForRun(int runId) const;
    // A freshly created run makes any earlier not-yet-finished run of the same
    // workflow (same owner/name/workflowPath) moot: it was going to build an
    // older commit anyway. Aborts those (Running via the runner, Queued/
    // AwaitingApproval by dropping them) and logs why, so the queue never
    // wastes time on superseded work.
    void cancelSupersededRuns(const ActionRun &newRun);
    void onRunLog(int runId, const QString &text);
    void notifyActionEvent(const QString &title, const QString &body,
                           bool warning); // tray alert gated by the run-alert setting
    void addNotification(const QString &title, const QString &body,
                         bool warning = false, int runId = -1);
    // Overload that records where a notification's row should jump to when its
    // row is double-clicked on the Notifications page (issue #292).
    void addNotification(const QString &title, const QString &body, bool warning,
                         const NotificationLink &link);
    // Open the screen/item a notification points at (issue/PR/discussion/commit).
    void openNotificationLink(const NotificationLink &link);
    void showNotifications();
    // Notifications live in their own top-level section: a sortable table
    // (buildNotificationsSection is declared with the other section builders).
    void refreshNotificationsTable();
    void updateNotificationButton();
    // Show/hide the small top-bar rebuild+restart button per the opt-in setting.
    void updateNavRebuildButton();
    // Reposition the floating "Log" button to the live-log strip's corner.
    void positionFloatingLogButton();
    int pendingActionCount() const;
    void openActionRunFromNotification(int runId);
    // Show a desktop notification with both a title and body, using notify-send
    // when available (reliable on Linux) and falling back to the tray icon.
    // `icon` is a freedesktop icon name (e.g. "emblem-default").
    void postNotification(const QString &title, const QString &body,
                          bool warning = false, const QString &icon = QString());
    void onRunStatusChanged(int runId, const QString &status);
    void onRunFinished(int runId, bool ok);
    // A release run committed new artifact metadata into the working copy; publish
    // it (sync the served mirror) and refresh the Releases panel if it's open.
    void onReleaseMetadataLanded(int runId);
    void refreshActionsTable();
    void showLatestVisibleActionRun();
    void showRun(int runId);
    void approveSelectedRun();
    void rejectSelectedRun();
    ActionRun *findRun(int runId);
    // Whether a queued run's `needs:` workflows have already succeeded for the
    // same commit. Waiting runs stay queued; blocked ones are skipped.
    ActionNeeds::State actionRunNeedsState(int runId, QString *detail);
    // Log "waiting for X" once per queued run, not on every queue sweep.
    void noteActionRunWaiting(int runId, const QString &detail);
    int repoIndexFor(const QString &owner, const QString &name) const;
    // Settings: global variables/secrets editor.
    void reloadVariablesTable();
    void addOrEditVariable(bool editSelected);
    void deleteSelectedVariable();
    void exportVariables();
    void importVariables();
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
    // Debounced refreshOpenRepoDetail(): coalesces a burst of push events into a
    // single refresh so the heavyweight reload doesn't run once per event.
    void scheduleOpenRepoDetailRefresh();
    void updateRepoCodeSize();
    void updateRepoCommitCount();
    void updateRepoIssueCount();
    void updateRepoDiscussionCount();
    void updateRepoPullCount();
    void loadRepoOverview(const QString &path);
    void showRepoOverview();
    void showRepoEditor();
    void showRepoCoveExplorer();
    // The commit history lives inside the Code overview (no top-bar tab): the
    // commit strip's "N Commits" button toggles the area under the latest-commit
    // bar between the file browser and the commits panel.
    void showOverviewCommits();
    void showOverviewFiles();
    void showOverviewBranches();
    void showOverviewWorktrees();
    // Keep the left activity rail's Code/Git checked states in step with what
    // the repo detail view is showing (adhoc #357).
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
    // IDE-style right-click menu on the file-explorer tree, and the file
    // operations it drives. New/rename/delete commit directly to the default
    // branch and need a working tree we own; copy-path/reveal work on any local
    // repo. closeRepoFileTabsUnder discards editor tabs for a gone path.
    void showRepoFileTreeMenu(const QPoint &pos);
    void newRepoFileEntry(const QString &parentDir, bool folder);
    void renameRepoFileEntry(const QString &path, bool isDir);
    void deleteRepoFileEntry(const QString &path, bool isDir);
    void closeRepoFileTabsUnder(const QString &path, bool isDir);
    // Shared setup for a direct file operation: requires a clean working tree,
    // checks out the default branch, and returns the working-tree dir (empty and
    // a notice on failure) with *base set to that branch.
    QString prepareRepoFileOp(QString *base);
    // Commit follow-up shared by the file operations: re-point to the branch,
    // refresh the open repo, and rebuild the explorer tree in place.
    void finishRepoFileOp(const QString &base);
    void openRepoFile(const QString &path);
    void openRepoReadme(); // open the repo's README in a file tab (default view)
    void updateRepoFileSaveActions();
    void saveCurrentRepoFile(bool createPull);
    // Flip the current Markdown file tab between its source and a rendered preview.
    void toggleRepoFileMarkdownPreview();
    // Pop up the commit history for one repo file: a list of the commits that
    // touched it, each showing that commit's diff for the file.
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
    QStringList repoBranches() const;
    // The two reads behind repoBranches()/repoDefaultBranch() with no GUI state
    // of their own, so worker threads can run them too (adhoc #420).
    static QStringList listRepoBranches(const QString &dir);
    static QString chooseDefaultBranch(const QStringList &branches,
                                       const QString &configured,
                                       const QString &dir,
                                       const QString &checkedOut);
    QString repoDefaultBranch(const QStringList &branches) const;
    // Cheap default-branch lookup for hot UI paths (e.g. selecting an agent
    // session) that must NOT pay for repoBranches()'s `--sort=-committerdate`,
    // which reads every branch tip and can block the UI for hundreds of ms on
    // repos with many agent branches.
    QString repoDefaultBranchFast() const;
    QWidget *buildBranchesTab();
    // Everything the branches table is built from. The git half is gathered on a
    // worker thread (readBranchesPanelGit) so opening the panel — or landing on a
    // branch from an agent/PR link — never waits on git (adhoc #420); the GUI half
    // is snapshotted before the worker starts.
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
        // branch/ref -> (ahead, behind) vs the default branch. A missing entry
        // means the counts are unknown, so the row shows no ahead/behind status.
        QHash<QString, QPair<int, int>> localAheadBehind;
        QHash<QString, QPair<int, int>> remoteAheadBehind;
        QHash<QString, QString> worktrees; // branch -> linked worktree path
    };
    // Runs on a worker thread: fills the git-derived half of `data`.
    static BranchesPanelData readBranchesPanelGit(BranchesPanelData data);
    // Builds the table rows from a gathered snapshot (GUI thread, no git).
    void renderBranchesPanel(const BranchesPanelData &data);
    void loadBranchesPanel();
    QWidget *buildWorktreesTab();
    void loadWorktreesPanel();
    // Give the just-opened repo-detail tab's primary list table keyboard focus so
    // the user can arrow up/down through its rows immediately, without clicking a
    // row first. `id` is the m_repoDetailStack index switched to; tabs without a
    // navigable table (Code, Settings, …) are skipped.
    void focusRepoDetailTable(int id);
    // Run `git <args>` in `dir` without blocking the event loop: the QProcess is
    // parented to this window and self-deletes, and onDone(ok, stdout) runs on the
    // main thread once it finishes. Used for UI-thread git calls that were
    // freezing the window via waitForFinished() (see StallWatchdog reports).
    void runGitDetached(const QString &dir, const QStringList &args,
                        std::function<void(bool, const QByteArray &)> onDone);
    // Run blocking work (disk reads, parsing — anything but GUI or git-helper
    // state) on a detached worker thread, then deliver its result to `apply` on
    // the GUI thread. The companion to runGitDetached for non-git work, and the
    // building block for keeping clicks instant: capture the work's inputs BY
    // VALUE — nothing shared with the GUI thread may be touched inside `work`.
    // If the window is destroyed before delivery, the queued apply is discarded.
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
    // Bumped on every loadWorktreesPanel() rebuild so the async per-row `git
    // status` callbacks can drop their result if the table was rebuilt meanwhile.
    int m_worktreeStatusGen = 0;
    // Open the Worktrees tab and select the row for a branch (used by the
    // clickable worktree-location link in the agent session header — issue #265).
    void switchToWorktree(const QString &branch);
    // Open the Branches tab and select the row for a branch, previewing its diff
    // (used by the clickable branch-name link in the agent session header —
    // adhoc #123).
    void switchToBranch(const QString &branch);
    // Select the worktrees-table row whose branch matches, repopulating the diff
    // pane and detail buttons. Returns false if no such row exists. Used to keep
    // the selection on the worktree being acted on after loadWorktreesPanel()
    // rebuilds the table (which would otherwise clear it — issue #272).
    bool selectWorktreeRow(const QString &branch);
    // Same for the branches table: select the row whose name matches (which fires
    // currentCellChanged -> showBranchDiff). Returns false if no such row exists.
    bool selectBranchRow(const QString &branch);
    // Explain that a branch link pointed at a branch this repository doesn't have.
    void reportBranchNotFound(const QString &branch);
    void showWorktreeDiff(const QString &branch, const QString &worktreePath);
    // Merge a worktree's branch into the default branch. On success the now-merged
    // worktree and its branch are removed (the work is preserved in the merge
    // commit); pass its folder so it can be. deleteAgent=true additionally tears
    // down the agent session(s) that produced the branch.
    void mergeWorktreeIntoMain(const QString &branch,
                               const QString &worktreePath = QString(),
                               bool deleteAgent = false);
    // Merge the default branch into a worktree's branch, run inside that worktree,
    // so it picks up the latest from main without leaving its folder. baseArg lets a
    // caller name the base branch explicitly; callers that leave it empty fall back
    // to the open repo detail's default branch. The agent detail page must pass it,
    // since its session's repo isn't necessarily the one open in the detail view
    // (adhoc #28).
    void updateWorktreeFromMain(const QString &worktreePath, const QString &branch,
                                const QString &baseArg = QString());
    // Open the shared merge editor over the worktree's currently-unmerged files
    // (a conflicted merge in progress), letting the user resolve and commit them.
    // Reused by "Update from main" when it conflicts and by the detail panel's
    // "Resolve conflicts" button. On commit the merge is finished; on cancel the
    // merge is aborted. Returns true iff the merge was committed.
    bool editWorktreeConflicts(const QString &worktreePath, const QString &branch,
                               const QString &base);
    // Detail-panel action: resolve the conflicts in the selected worktree.
    void resolveWorktreeConflicts();
    // Stage everything in a worktree and commit it under a message the user types,
    // so its in-progress changes can be committed without leaving the app.
    void commitWorktreeChanges(const QString &worktreePath, const QString &branch);
    // Remove a worktree's folder (git worktree remove --force). confirm=true asks
    // first; the post-merge cleanup calls it silently. alsoDeleteBranch deletes the
    // now-orphaned branch too (the default for the Worktrees-tab "Remove" action and
    // the post-merge cleanup, whose work is already preserved in the merge commit).
    // async=true runs the (slow, recursive) folder delete off the UI thread so the
    // window stays clickable; the branch delete + panel refresh follow in a callback.
    // onDone, if set, runs after that cleanup succeeds (lets the post-merge flow set
    // its own final notice once the worktree is actually gone).
    void removeWorktree(const QString &worktreePath, const QString &branch,
                        bool confirm, bool alsoDeleteBranch = true,
                        bool async = false, std::function<void()> onDone = {});
    // Filesystem path of the worktree currently checked out to `branch` (other
    // than the main checkout), or empty if none. Lets the agent detail resolve a
    // session's worktree folder from its branch.
    QString worktreePathForBranch(const QString &repoPath,
                                  const QString &branch) const;
    // True if `branch` is a local branch in `repoPath`. Lets the delete paths skip
    // a `git branch -D` (and its noisy "branch not found" error) when the branch was
    // already gone — the desired end state either way.
    bool localBranchExists(const QString &repoPath, const QString &branch) const;
    void showBranchDiff(const QString &branch);
    // Paint the branch detail bar from already-gathered counts, and let auto-pull
    // decide once the bar reflects them (the counts arrive off-thread now).
    void applyBranchDetailActions(const QString &branch, const QString &base,
                                  int behind, int ahead, bool hasConflict);
    void maybeAutoPullBranch(const QString &branch);
    // Bumped per branch selection so a detail-bar read that lands late is dropped.
    int m_branchDetailActionsGen = 0;
    // Render the branch diff for whichever scope is selected in m_branchScopeList
    // (whole branch vs base, the worktree's uncommitted changes, or one commit).
    void renderBranchScopeDiff();
    // Filesystem path whose uncommitted changes belong to `branch`: its dedicated
    // worktree, or the main checkout when `branch` is the one checked out there.
    // Empty when the branch is checked out nowhere (so it can't be dirty).
    QString branchWorkDir(const QString &branch) const;
    // Render an already-captured patch into the branch diff view + changed-files
    // list (shared by every scope above). `viewedContext` scopes the per-file
    // "viewed" toggles; `emptyMessage` shows when the patch has no changes.
    void renderBranchDiffPatch(const QString &patch, const QString &emptyMessage,
                               const QString &viewedContext);
    // Rebuild the sticky-bar file-span map from whatever is currently in the
    // branch diff document (called once a streamed render has fully landed).
    void rebuildBranchDiffSpans();
    // Open the selected branch's working directory in VSCodium: its dedicated
    // worktree if it has one, otherwise the repo's main checkout.
    void openBranchInCodium(const QString &branch);
    // Refresh the detail-pane action bar (Open in Codium / Pull / Fix with agent /
    // Create PR / Merge to main) for the currently selected branch.
    void updateBranchDetailActions(const QString &branch);
    void createPullFromBranch(const QString &branch);
    void onBranchDiffAnchorClicked(const QUrl &url);
    void updateBranchDiffSticky();
    QString diffViewedScope(const QString &context) const;
    QSet<QString> loadDiffViewed(const QString &context) const;
    void setDiffViewed(const QString &context, const QString &path, bool viewed);
    // Merge the default branch into `branch` so it catches up with main.
    void updateBranchFromBase(const QString &branch);
    // Bring `branch` up to date with base via the interactive merge editor,
    // resolving conflicts by hand. Reached from the "Merge editor" button.
    void openBranchMergeEditor(const QString &branch);
    // Merge the default branch into every branch that's behind it in one pass;
    // clean merges land via plumbing (no checkout), conflicts are reported so the
    // list can surface them and offer "Fix with agent". Runs without a
    // confirmation prompt; the "Pull into all" button spins while it works.
    void pullBaseIntoAllBranches();
    // Merge the default branch into `branch` and have a low-cost model resolve any
    // conflicts, committing the merge onto the branch (watched on the Agents tab).
    // `model` (adhoc #60) picks which model the chosen provider runs as; empty
    // falls back to the provider's low-cost default.
    void fixBranchConflictsWithAgent(const QString &branch, const QString &provider,
                                     const QString &model = QString());
    void promptNewBranch();
    void deleteBranch(const QString &branch);
    // Delete a remote-tracking branch on its origin (git push <remote> --delete),
    // then prune the stale remote-tracking ref. `branch` is the "<remote>/<ref>"
    // shown in the Branches table.
    void deleteRemoteBranch(const QString &branch);
    // The branch listed next to `branch` in the Branches table (the row below it,
    // else the row above), used to pick the post-delete selection (adhoc #256).
    QString neighbourBranchInList(const QString &branch) const;
    // Row `branch` occupies in the Branches table, or -1 when it isn't listed.
    int branchRowInList(const QString &branch) const;
    // Leave an animated check in the row `branch` occupied, instead of moving the
    // selection to another branch, now that "Merge & delete all" has removed it
    // (adhoc #15). Cleared by clearMergedBranchFlash() — on the next branch the
    // user selects, or after kBranchMergedFlashMs.
    void flashMergedBranchRow(const QString &branch);
    void clearMergedBranchFlash();
    // Delete every branch that is fully merged into the default branch (0 behind
    // and 0 ahead of it), skipping the default and the checked-out branch.
    void deleteMergedBranches();
    void deleteSelectedBranches();
    QWidget *buildReleasesTab();
    void loadReleasesPanel();
    void promptNewRelease();
    // Ask the chosen agent (Codex / OpenAI API / Claude API / Claude Code) to
    // write GitHub-style release notes from the commits since `prevTag`, then
    // drop the result into the draft dialog's Notes box. Async: the reply may
    // land after the dialog is dismissed, so the widgets are held via QPointer.
    void generateReleaseNotesWithAgent(const QString &dir, const QString &prevTag,
                                       const QString &newTag,
                                       const QString &targetRef,
                                       const QString &provider,
                                       const QString &modelChoice,
                                       QPlainTextEdit *notesEdit,
                                       QPushButton *button);
    void pruneReleaseArtifactsForCurrentRepo(const QString &releaseTag);
    // Deletes every other release tag in this repo's history, keeping only
    // `keepTag` (the one just published). Best-effort like the artifact prune
    // above: logs and surfaces a notice on partial failure rather than
    // aborting the whole publish.
    void pruneReleaseTagsForCurrentRepo(const QString &keepTag);
    // Push a just-published release to the repo's fediverse followers via the
    // relay's owner-signed POST /ap-publish (releases never pass through the
    // signed inboxes, so the relay can't federate them on its own).
    void announceReleaseOnFediverse(const QString &tag, const QString &title,
                                    const QString &notes);
    // Open a release's full notes + the diff since the previous release.
    void showReleaseDetail(const QString &tag);
    // Per-repo Artifacts tab (adhoc #98): the release binaries this node hosts in
    // its content-addressed store (forkmesh-releases/sha256/…). Lists each blob
    // with its release name/tag, checksum and on-disk size, and lets it be deleted
    // to reclaim space.
    QWidget *buildArtifactsTab();
    void loadArtifactsPanel();
    void deleteArtifact(const QString &hash, const QString &label);
    QWidget *buildMirrorNodesTab();
    // Per-repo Shortcuts tab (adhoc #118): quick-launch entries stored as plain
    // files in the checkout's .forkmesh/shortcuts/ folder (shell scripts today;
    // prompts/skills ride along as editable text), so they version and sync with
    // the repo. Each file is a clickable card — a script runs through bash with
    // its output streamed live into the page's log pane; other kinds open in
    // the editor. New / edit / delete round out the CRUD.
    QWidget *buildShortcutsTab();
    void loadShortcutsPanel();
    QString shortcutsDirPath() const; // <working tree>/.forkmesh/shortcuts, "" without one
    void runShortcut(const QString &filePath);
    void stopShortcut();
    // Create (empty filePath) or edit a shortcut via a name + content dialog.
    void openShortcutEditor(const QString &filePath);
    void deleteShortcut(const QString &filePath);
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
    void reloadRepoRemotesTable();
    void promptAddRepoRemote();
    void promptEditRepoRemote();
    void deleteSelectedRepoRemote();
    // Choose an existing local Git working copy and save it as the open repo's
    // fork location, so future syncs mirror from that local checkout.
    void promptSetRepoForkLocation();
    // Set repo-local git user.name/user.email from the ForkMesh username and a
    // generic ForkMesh email address.
    void setRepoGitIdentityFromForkMesh();
    // Set "run actions on push" for the open repo and keep both toggles in sync.
    void setRepoActionsEnabled(bool on);
    // Enable or disable secret-scanning push protection for the open repo.
    void setRepoSecretScanningEnabled(bool on);
    // Switch a single workflow (by path) on or off for the open repo, persist it,
    // and refresh the manual-run bar so a disabled workflow can't be run by hand.
    void setWorkflowDisabled(const QString &path, bool disabled);
    // Whether `path` is switched off for the open repo.
    bool isWorkflowDisabled(const QString &path) const;
    void loadMirrorNodesPanel();
    // Spin the caution/error status lights on Mirror-nodes rows (adhoc #230);
    // updateMirrorNodeLightTimer() keeps the timer running only while a row's
    // light is actually spinning, so an all-green table never ticks.
    void animateMirrorNodeLights();
    void updateMirrorNodeLightTimer();
    void requestMirrorNodesRefresh();
    void onMirrorRefreshRequested(const QString &source,
                                  const QString &requesterName);
    // Fetch the worker's catalog mirror list for a repo group so the owner sees
    // every published mirror, not just nodes live in the chat room (issue #223).
    void fetchCatalogMirrors(const QString &owner, const QString &repo,
                             const QString &source);
    // Fetch the worker's per-artifact release download counts (logged each time
    // /releases/blob/sha256/<hash> streams a binary out), so the Releases tab can
    // show how many times each artifact has been downloaded.
    void fetchReleaseDownloadCounts(const QString &owner, const QString &repo,
                                    const QString &source);
    // Mirror the repo's release artifacts: pull any content-addressed binary blobs
    // this node doesn't already hold (the metadata is in git, the bytes are not —
    // issue #304) into its release store, so a mirror can serve downloads too and
    // not just clones (adhoc #77). Called after a successful sync of a real mirror.
    void replicateReleaseArtifacts(int index);
    // Download the next missing release blob in `pending` (hash -> "owner/name" to
    // pull it from) from the relay's public content-addressed route, verify its
    // sha256, store it in the mirror's release CAS, then recurse to the rest.
    void downloadNextReleaseBlob(int index, const QString &mirrorPath,
                                 QMap<QString, QString> pending);
    void deleteTag(const QString &tag);
    bool repoHasWorkingTree() const;
    void loadFileSearchIndex();
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
    // --- Browser-style back / forward navigation, sat just left of the search box.
    // A history of "places" (section + open repo + repo tab) is recorded as you
    // move around; Back and Forward walk it without recording new entries.
    QWidget *createNavHistoryButtons();        // build the Back / Forward pair
    void scheduleNavRecord();                  // queue a debounced location capture
    void recordNavLocation();                  // snapshot the current place onto the trail
    void restoreNavEntry(int index);           // navigate to a recorded place
    struct NavPlace;
    void applyNavDetailTab(const NavPlace &place); // re-select a recorded repo tab
    void navigateBack();
    void navigateForward();
    void updateNavHistoryButtons();            // enable/disable per trail position
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
    // Index-based core shared with the relay-sync path (website gear-icon
    // edits arriving as aboutUpdate in GET /api/sync).
    bool applyRepoAboutMetadataAt(int index, const QString &about,
                                  const QString &websiteInput,
                                  QString *error = nullptr);
    // The relay's public About/fediverse endpoint for one repo
    // (GET = branding + follower list + federation switches; POST = save).
    QUrl repoAboutApiUrl(const QString &owner, const QString &name) const;
    // Push the per-repo ActivityPub switches (federate / broadcastEvents /
    // acceptComments) to the relay, proven by the owner key (no browser
    // session on the desktop). Best-effort fire-and-forget.
    void saveRepoFediverseSettings(const QString &owner, const QString &name,
                                   bool federate, bool broadcastEvents,
                                   bool acceptComments);
    void loadCommits();
    // Fill the Files/+/− columns of the commit list from `git log --numstat`,
    // which diffs every commit in the window and is the slow part of a load. Runs
    // deferred (after loadCommits has painted the rows) so the list itself appears
    // instantly; `loadGen` tags it against m_commitsLoadGen so a fast follow-up
    // reload discards a stale fill landing late.
    void fillCommitStats(int loadGen);
    // Infinite scroll: when the list is scrolled to the bottom and more history
    // exists, deepen the window (m_commitsLimit) and rebuild, preserving scroll.
    void loadMoreCommits();
    // VS-Code-style in-place expansion: clicking a commit row inserts one child
    // row per file it touched (clicking again collapses them). File rows open
    // the commit's diff scrolled to that file.
    void toggleCommitFilesRows(int row);
    void collapseAllCommitFileRows();
    // Rebuild the Summary item's hover box (author / date / hash / files /
    // adds / dels / checks) from the row's hidden metadata cells.
    void updateCommitRowHover(int row);
    // "N commits pending sync" header above the list: expands into the files
    // those commits touch, one expandable entry per pending commit.
    void updateCommitsUnsyncedFilesPanel();
    // Fetch: refresh this repo's refs from the network (mirror + any upstream
    // remote) without touching the working tree. Pull: fast-forward the working
    // tree from its upstream (or the served mirror when no upstream is set).
    void fetchCurrentRepo();
    void pullCurrentRepo();
    // --- Source Control panel (working-tree changes) at the top of the Commits
    // tab: stage/unstage/discard/commit, view per-file diffs, and draft the
    // commit message (or an X post) with Claude/OpenAI.
    QWidget *buildSourceControlPanel();
    void refreshSourceControl();             // re-scan `git status` into the tree
    void refreshSourceControl(bool force);   // force refresh path bypassing cache short-circuit
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
    // Commit the staged changes, then publish/push them (see pushCurrentRepoUpstream).
    void scmCommitAndPush();
    // One click: stage every change (git add -A), commit, then publish/push.
    void scmStageAllCommitAndPush();
    // Shared commit body for both buttons above. Returns true once a commit lands
    // so "Commit & push" only pushes after a successful commit.
    bool performScmCommit();
    // Bring a file's section of the combined working-tree diff into view.
    void showScmDiff(const QString &path, bool staged, bool untracked);
    // "Open Changes" for a whole group: jump the combined diff to the first
    // staged (or first unstaged/untracked) file.
    void showScmDiffAll(bool staged);
    // Render every working-tree change into one scrollable diff (adhoc #399),
    // caching the per-file anchors/labels the sticky header and the read-progress
    // tracking need. Skips the (expensive) re-layout when nothing changed.
    void renderScmCombinedDiff();
    // Build the sticky header overlay + scroll wiring for the combined diff.
    // Called once, right after m_scmDiff is constructed.
    void setupScmDiffPane();
    // Scroll the combined diff so this file's section sits at the top. Staged and
    // unstaged copies of one path render as separate sections; `staged` picks it.
    void scrollScmDiffToFile(const QString &path, bool staged);
    // Follow the combined diff's scroll: keep the sticky header on the topmost
    // visible file, advance its read-progress chart / percentage, and select that
    // file in the tree. Cheap (no re-render); runs on every scroll tick.
    void updateScmDiffScrollState();
    // Position the sticky header across the top of the changes diff viewport.
    void layoutScmStickyHeader();
    // Walk the rendered combined diff once, caching each file header's absolute y
    // into m_scmFileTops so the per-tick sticky update stays cheap.
    void computeScmFileTops();
    // Debounced off the changes diff scrollbar: check off every file that has been
    // scrolled all the way through as "Viewed" and re-render (collapsing them).
    void applyScmAutoMarkViewedOnScroll();
    // Select a file in the changes tree without scrolling the diff back to it
    // (used while the selection follows the scroll).
    void selectScmFileInTree(const QString &path, bool staged);
    // The changes-tree row for a path on the staged / unstaged side, or nullptr.
    QTreeWidgetItem *scmFindItem(const QString &path, bool staged) const;
    // Refresh the "N of M files viewed" counter above the changes tree.
    void updateScmViewedCount();
    // "viewed:" toggles inside the combined working-tree diff.
    void onScmDiffAnchorClicked(const QUrl &url);
    // QSettings context (see loadDiffViewed) for the working-tree diff.
    static QString scmViewedContext();
    // Walk the working-tree changes with the up/down buttons: every file shares
    // one scrollable view, so this just jumps to the next (+1) / previous (-1)
    // hunk, crossing file boundaries on its own.
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
    // Cheaply update action/check glyphs without rebuilding repo views. Used by
    // action status changes, which can fire while a long test process is still
    // streaming output.
    void refreshCommitBarStatusGlyph();
    void refreshCommitTableStatusGlyphs();
    // Spin the Actions tab label while a run for the open repo is active.
    void updateActionsTabIndicator();
    // Previous finished run's duration for the same workflow, shown beside a
    // queued/running run (0 = no prior run to estimate from).
    qint64 estimatedRunDurationMs(const ActionRun &run) const;
    // Keep the running-session spinner timer alive/dead for the Agents table
    // (adhoc #178 removed the Agents tab and its floating spinner overlay, but
    // the table's own running-row glyph + elapsed-time cell still animate).
    void updateAgentsTabIndicator();
    // Re-render commit check glyphs in whichever repo-detail tab is visible.
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
    // Handles the dependency card's per-row "Run scan" button: rescans either a
    // single manifest (when manifestPath is non-empty) or all manifests off the
    // GUI thread (it shells out to git and reads files, which can take a moment
    // on a large repo) so the button/progress-bar busy state actually animates
    // instead of freezing the window mid-scan.
    void runRepoDependencyScan(const QString &manifestPath = QString());
    void refreshRepoQuality();
    // Open a repo file in the editor and highlight/centre the given 1-based
    // line (used by the Security and Quality findings tables).
    void openRepoFileAtLine(const QString &path, int line);
    void loadRepoInsights();
    // Insights "Contributors & activity": right-click a row to reassign that
    // author's commits to a different identity (rewrites history via
    // git filter-branch) to fix attribution.
    void showInsightsContributorMenu(const QPoint &pos);
    void reassignContributorIdentity(const QString &oldName);
    // Clicking a contributor's name or commit count on the Insights tab jumps to
    // the Commits tab with the list filtered to that author (drives m_commitSearch).
    void openCommitsForContributor(const QString &author);
    void setRepoBranch(const QString &branch);
    QString repoHeadBranch() const;          // the checked-out branch (HEAD)
    void refreshCommitsBranchButton();       // commits-page branch indicator/menu
    void createAndCheckoutBranch();          // "Create new branch…"
    QString currentRef() const;
    QString repoGitDir() const;
    QString iconsDir() const;
    QIcon iconForFile(const QString &fileName) const;
    QIcon iconForDir(bool opened) const;
    void startRefreshSpin();
    void stopRefreshSpin();
    // Small inline spinner shown next to the commit's "files changed" heading
    // while showCommit reads and renders the diff (a big commit can take a second
    // or two), so the click shows progress instead of looking frozen.
    void startCommitDiffSpin();
    void stopCommitDiffSpin();
    // Generic click feedback for any Refresh button: briefly spins its icon, then
    // restores it. addRefreshSpin wires it onto a button's clicked signal.
    void spinRefreshButton(QPushButton *button);
    void addRefreshSpin(QPushButton *button);
    // Open-ended variant for a button whose work runs synchronously for an
    // unknown span: start spinning on click, stop (restoring the icon) when the
    // work finishes. Pair these around a GitKeepAlive scope so it animates.
    void startButtonSpin(QPushButton *button);
    void stopButtonSpin(QPushButton *button);
    // Spin the icon of whichever button kicked off a rebuild/restart, as live
    // feedback while the (async, multi-step) restart runs. The app relaunches on
    // success, so the spin only needs stopping on the failure paths, where
    // stopRestartSpin() restores the icon. Tracking a single button keeps the
    // centralised failure handler (runUpdateStep) agnostic to which one it was.
    void startRestartSpin(QPushButton *button);
    void stopRestartSpin();
    // Flips an in-progress restart spin between the refresh-arrows look (a
    // rebuild actually running) and a spinning hourglass (queued behind other
    // agent actions, not doing anything itself yet).
    void setRestartSpinHourglass(bool hourglass);
    // Busy feedback for switching nodes in the top nav: the node button shows a
    // spinner and the heavy repo load reports each step to the log. nodeSwitchStep
    // logs the step and, mid-switch, yields the event loop so the spinner animates.
    void startNodeSwitchSpin();
    void stopNodeSwitchSpin();
    void startRepoSwitchSpin();
    void stopRepoSwitchSpin();
    void nodeSwitchStep(const QString &what);
    void finishLoadStepTiming();
    // Live, visible progress for a repo/node load: a blue pill in the top bar
    // (where the breadcrumb is) naming the current step, e.g. "Loading commit
    // history…". Persistent until the next showLoadStatus / flashMessage clears it.
    void showLoadStatus(const QString &what);

    // If this node owns a writable working-tree copy of the same logical repo as
    // `repo` (same owner/name), returns that record; else returns `repo`. Lets the
    // source-of-truth author issues/PRs even when a read-only preview of their own
    // repo is the one currently selected.
    const RepositoryRecord &writableRecordFor(const RepositoryRecord &repo) const;

    // Git directory agents run against for `repo`: our working-tree checkout when
    // we host it, otherwise the bare network mirror so a node that only mirrors a
    // repo can still run agents on it (worktrees/diffs/PR patches build off this).
    // Empty when neither exists. (adhoc #191)
    QString repoAgentGitDir(const RepositoryRecord &repo) const;

    // Issues tab
    int issuesRepoIndex() const;                 // selected repo, or -1
    IssueStore issueStoreForCurrentRepo() const; // build a store for that repo
    void refreshIssuesRepoCombo();
    void reloadIssues();        // load issues + label/milestone filters from the store
    void reloadIssuesInBackground();
    void applyLoadedIssues(const QString &signature, QList<Issue> issues,
                           QList<IssueLabel> labels,
                           QList<IssueMilestone> milestones);
    // Splice a just-created issue into m_currentIssues and redisplay it, without
    // the full loadAll() reloadIssues() would do — that re-reads every issue
    // file in the repo, which is what made the redirect to the new issue feel
    // laggy on repos with a lot of issues.
    void appendCreatedIssue(const IssueStore &store, const Issue &issue);
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
    // Quick-add image attachment (issue #79): pick or paste an image in the footer
    // quick-add bar. In "No issue" mode the path is sent to the agent in its
    // prompt; otherwise the image is attached to the created issue.
    void attachQuickAddImage();
    bool tryPasteImageIntoQuickAdd();
    void queueQuickAddImage(const QString &path);
    void removeQuickAddImage(const QString &path);
    void clearQuickAddImages();
    void updateQuickAddImageButton();
    // Rebuilds the row of attachment chips (thumbnail + an "x" to remove each)
    // shown next to the paperclip once images are queued.
    void rebuildQuickAddAttachChips();
    // Clicking a chip's thumbnail (issue #319) opens the original image full-size
    // in a lightbox dialog.
    void showQuickAddImageDetail(const QString &path);
    // Screenshot button (next to the rebuild/restart button): drops a full-screen
    // overlay so you can drag a rectangle anywhere on the computer, then queues the
    // captured region as a quick-add attachment.
    void captureScreenRegion();
    // Pencil button (next to the screenshot button): drops a full-screen overlay
    // you can draw on freehand anywhere on the computer. Nothing is captured — it's
    // a throwaway scratch layer for pointing things out. Esc dismisses it.
    void startScreenDraw();
    // Voice input: when whisper.cpp is installed (from Settings) a mic button
    // appears beside the prompt box. It is push-to-talk: press and hold to
    // record from the microphone, release to stop and transcribe the audio into
    // the prompt locally.
    void startVoiceCapture();
    void stopVoiceCapture();
    // Press-to-talk dictation into an arbitrary text box (the footer prompt or any
    // comment composer). `button` is the mic that was pressed, so its icon swaps to
    // red while recording and the transcript lands in `target`.
    void startVoiceCaptureFor(QPlainTextEdit *target, QPushButton *button);
    // Origin-bound, loopback-only control bridge used by the browser World to
    // drive this same local recorder/transcriber. It never accepts audio.
    void initializeWorldSpeechBridge();
    void createWorldSpeechPairing();
    void revokeWorldSpeechPairing();
    void cancelWorldVoiceCapture();
    // Jump to Settings and land on the Voice tab — used when the mic is
    // clicked before speech-to-text is set up (adhoc #132).
    void openVoiceSettings();
    // Build a reusable push-to-talk mic bound to a comment composer; it speaks into
    // the composer's source editor and is registered so updateVoiceInputButton()
    // keeps its visibility/idle look in sync with the voice-engine install state.
    QPushButton *makeVoiceButton(MarkdownEditor *composer);
    void startVoiceTranscription(bool finalPass);
    void applyVoiceTranscript(const QString &text, bool finalPass);
    void updateVoiceInputButton();
    // Live mic-level meter (adhoc #10): sample the growing capture and drive the
    // bar beside the mic so you can see audio is coming in while you talk.
    void updateVoiceLevelMeter();
    void stopVoiceLevelMeter();
    // Processing ring shown around the active mic while a released clip is still
    // being transcribed (adhoc #18): show encircles m_voiceActiveButton, hide
    // removes it when the final transcription pass settles.
    void showVoiceTranscribeSpinner();
    void hideVoiceTranscribeSpinner();
    // Settings "Test mic" (adhoc #14): record from the chosen mic and drive a level
    // bar so you can confirm the device is captured before relying on dictation.
    // Independent of whisper — purely a microphone check.
    void toggleMicTest();
    void updateMicTestMeter();
    void stopMicTest();
    // Settings: download, build and provision whisper.cpp for local dictation.
    void installWhisperCpp();
    // Settings: provision NVIDIA Parakeet (a local Python env) for dictation.
    void installParakeet();
    // Install whichever engine the Settings selector currently points at.
    void installVoiceEngine();
    void refreshWhisperStatus();
    // Quick-add prompt history (adhoc #200): remember each sent prompt and let
    // Up/Down walk back through them in the footer bar. direction < 0 is Up
    // (older), > 0 is Down (newer); returns true when the key was consumed.
    void recordQuickAddHistory(const QString &text);
    bool navigateQuickAddHistory(int direction);
    // Footer slash-actions menu (adhoc #116): the "/" button left of the Agent
    // checkbox opens a filterable popup mirroring the Claude Code extension's
    // actions menu (Context/Model sections plus the CLI's own slash commands,
    // pulled live from `claude` via a control-protocol initialize probe).
    void openQuickAddSlashActions();
    void populateSlashActionsList();
    void moveSlashActionsSelection(int delta);
    void activateSlashActionRow(QWidget *row);
    void refreshClaudeSlashCommands();
    void mentionProjectFileInQuickAdd();
    // Show the transparent public community reward pool. The pool key is not
    // available to the Worker and user wallets always remain self-custodial.
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
    // The issue listed just after `number` in the table's current visual order
    // (falls back to the one before it when closing the last row); -1 if none.
    int nextVisibleIssueAfter(int number) const;
    void deleteCurrentIssue();
    // Runs the destructive history-rewriting delete on a worker thread so the UI
    // stays responsive while git filter-branch/gc churn.
    void deleteCurrentIssueWithHistory(int number);
    void editIssueLabels();
    void editIssueMilestone();
    // Planned start/end dates (issue #384): opens the inline two-QDateEdit
    // editor in the sidebar's "Dates" row; save writes a signed "dates" event.
    void editIssueDates();
    void saveIssueDatesInline();
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
    // Migration-only no-op that explains why new issue bounty escrow is retired.
    void editIssueBounty();
    // Migration-only no-op; no pledge, wallet, or transfer is created.
    void bountyAllOpenIssues(double amountUsd);
    // One-shot triage: give every open issue with no priority an MVP/Phase-2
    // label and an initial priority (votes/age heuristic), and estimate each
    // issue's progress from whether the work landed (closed / merged PR / agent).
    void reprioritizeBacklog();
    // Issue #286: ask the default agent to reorder the open backlog from the
    // project's README. Reads the README, sends it plus the open issues to the
    // configured provider with the editable Settings prompt, and rewrites each
    // issue's priority from the returned ranking.
    void prioritizeIssuesFromReadme();
    // Adhoc #139: ask the picked agent to judge how complete/actionable each open
    // issue is (clear problem, enough detail, acceptance criteria) using the
    // README for context, then show the verdict per issue in a report dialog.
    void analyzeIssueCompleteness();
    // README markdown for the currently selected issues repo (work tree first,
    // then a `git show HEAD:README*` fallback). Empty when none is found.
    QString currentRepoReadme() const;
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

    // Projects tab (issue #384): repo-level projects that group issues, carry
    // start/end dates and an optional milestone, and render as a list or a
    // Gantt timeline. Backed by ProjectStore (.forkmesh/projects/).
    QWidget *buildProjectsSection();
    ProjectStore projectStoreForCurrentRepo() const; // build a store for that repo
    void reloadProjects();      // load projects (+ issues, for progress) from the store
    void refreshProjectList();  // apply the status filter into the table
    void refreshProjectGantt(); // rebuild the Gantt rows from the loaded data
    void showProject(int number); // render the selected project's detail pane
    void promptNewProject();
    void editProjectLinkedIssues(); // multi-select dialog over the repo's issues
    void setProjectInlineNotice(const QString &message, bool error = false);
    // Percent complete for a project: closed share of its linked issues, else
    // (when only a milestone is linked) the closed share of that milestone.
    int projectProgressPercent(const Project &project) const;

    QUrl issuesApiUrl(const RepositoryRecord &repo) const;
    // Owner-encrypted agent relay (adhoc #182): clear session content remains
    // on the desktop; the relay stores only recipient-sealed snapshots/prompts.
    QUrl agentsApiUrl(const RepositoryRecord &repo) const;
    // Push a full-replace snapshot of every owned/hosted repo's local agent
    // sessions to the website. Called on a periodic timer and, debounced, right
    // after a session's status changes.
    void pushAgentSessionsSnapshot();
    void pushAgentSessionsForRepo(RepositoryRecord repo, QList<AgentSession> sessions);
    // Register this device's public hybrid recipient bundle and enforce the
    // repository's mandatory owner-only agent policy before any snapshot or
    // prompt drain reaches the relay.
    void ensureAgentE2EEControlPlane(
        RepositoryRecord repo, std::function<void(bool ok)> onDone);
    bool loadOwnerEncryptionIdentity(MirrorCrypto::Identity *identity,
                                     QString *error = nullptr) const;
    // Arms/re-arms a short debounce timer that calls pushAgentSessionsSnapshot()
    // once it fires, so a burst of status flips coalesces into one request.
    void scheduleAgentSessionsPush();
    // Periodic drain of prompts an owner-key-capable client queued for this
    // node's agent sessions.
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
    void reportOrgAgentJob(const RepositoryRecord &repo,
                           const QJsonObject &job,
                           const QString &securityVerdict,
                           const QString &status,
                           int localAgentId = 0,
                           const QString &reason = QString(),
                           const QString &result = QString());
    void reportCompletedOrgAgentJobs();
    // Deliver one queued website prompt to the matching session via
    // sendPromptToAgentSession (steers it live, or resumes it if stopped), or
    // log + drop it if no local session with that id exists.
    void deliverQueuedAgentPrompt(int sessionId, const QString &text);
    // Start a new ad-hoc agent for a repo from a website "new agent" prompt
    // (adhoc #266): the top-of-list web composer queues these with agentId "new".
    void startWebNewAgentForRepo(const RepositoryRecord &repo, const QString &task,
                                 const QString &providerOverride = QString(),
                                 const QStringList &images = QStringList());
    // Decode pasted/attached screenshot data: URLs queued with a website "new
    // agent" prompt (adhoc #78) to image files on disk; returns their paths.
    QStringList saveWebAgentImages(const RepositoryRecord &repo,
                                   const QStringList &images);
    // Private-repo collaborator ACL (issue #9): share/unshare a private repo with
    // other accounts and list current collaborators.
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
    // Compatibility method that refuses new funding for legacy Worker-held
    // bounty escrow.
    void showBountyQrDialog(const RepositoryRecord &repo, int number,
                            const QString &uri, const QString &address,
                            double amountUsd, const QString &amountSol,
                            const QString &kind = QString(),
                            const QString &payee = QString());
    // Compatibility method that explains the legacy bounty-wallet migration;
    // it performs no network action and never exposes a deposit address.
    void showBountyWalletDialog();
    void submitIssueCommentToInbox(const QString &body,
                                   const QStringList &attachmentSrcPaths = {},
                                   const QStringList &attachmentPlaceholders = {});
    // Mirror node path: file a signed "assignees" event to the source of truth's
    // inbox so the looper's claim on an issue reaches the owner and syncs back to
    // every mirror (adhoc #38).
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
    // Drain one repo's issue inbox. Owners consume their queue; eligible public
    // mirrors materialize it without consuming the source-of-truth delivery.
    // `interactive` shows inline notices for a manual "Sync inbox". Repo is
    // taken by value so the async reply can't dangle.
    void drainIssuesInboxFor(RepositoryRecord repo, bool interactive);
    void pollMirrorIssueInboxes();
    void drainPullsInboxFor(RepositoryRecord repo, bool interactive);
    // Notify the local user when they're @mentioned in one of this repo's issues
    // or pull requests. Each node scans its own synced copy, so the mentioned
    // user's node is what alerts them. Deduped and seeded via QSettings so we
    // never repeat an alert or backfill a freshly-cloned repo's history.
    void scanRepoMentionsFor(const RepositoryRecord &repo);
    // Match @mentions against issues/PRs/commit-comments already loaded off the UI
    // thread (see scanRepoMentionsFor) and raise notifications. Runs on the main
    // thread so it can touch QSettings and the notification UI.
    void applyRepoMentions(
        const RepositoryRecord &repo, const QList<Issue> &allIssues,
        const QList<PullRequest> &allPulls,
        const QList<QPair<QString, QList<CommitComment>>> &allCommitComments);
    // Periodically pull every owned repo's inboxes so the source of truth picks
    // up issues/PRs/comments filed by other nodes without a manual sync.
    void pollOwnedInboxes();
    // Bounded relay sync: scheduleRelaySync() coalesces explicit refresh
    // requests into one signed GET /api/sync that returns every owned repo's
    // pending control-plane changes. The normal timer provides the fallback
    // and no per-repository socket is opened.
    void scheduleRelaySync();
    void performRelaySync();
    // Merge one repo's pending payload from /api/sync (or a per-topic drain
    // reply) into the local stores, ack the inbox, and raise notifications.
    // Shared by the drain* fetchers and the /api/sync dispatcher.
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
    void applyCommitInboxPayload(const RepositoryRecord &repo,
                                 const QJsonArray &pending, bool interactive);
    void applyAgentPromptsPayload(const RepositoryRecord &repo,
                                  const QJsonArray &prompts);
    void acknowledgeAgentPrompts(const RepositoryRecord &repo,
                                 const QList<qint64> &queueIds);
    // owner/ts/sig query params carrying the forkmesh-issues-pull-v1 drain
    // token — the shared auth for inbox GET/DELETE and /api/sync.
    QUrlQuery signedInboxQuery(const QString &owner) const;
    // #368: identity key backup/export/import UI + first-run "back up" nag.
    void backUpIdentityKey();
    void refreshIdentityBackupNag();
    // Explain provider-owned, per-device Claude login and the owner-sealed
    // shared-workspace boundary. No credential export/import controls exist.
    void showClaudeCodeDeviceSetup();
    void chooseAvatar();
    void setSettingsAvatar(const QByteArray &pngData);
    // Effective avatar bytes: the uploaded/generated one, or a deterministic
    // generated identicon when the user hasn't set one.
    QByteArray effectiveAvatar();
    QByteArray effectiveUserAvatar();
    // Persist the local avatar to the account record (POST /api/accounts/profile)
    // so the web dashboard shows the same picture. No-op without an authenticated
    // session token or a local avatar to upload.
    void pushAccountAvatar();
    // The reverse direction: adopt the picture the account carries on the web
    // (seen in the public user directory) so the desktop avatar matches it.
    void adoptWebAccountAvatar(const QByteArray &png);
    void updateAvatarButton();
    void updateUserAvatarButton();
    void refreshIssueComposerAvatar();
    // Builds a small "identity" row (self avatar + current username) shown above
    // compose inputs so it's clear who is about to post. When verb is set the
    // text reads e.g. "Filing as <b>alice</b>"; otherwise just the username.
    QWidget *makeComposerIdentity(QLabel **outAvatar = nullptr,
                                  const QString &verb = QString());
    void logout();
    // Sign in to an existing ForkMesh user account (email + password) from
    // Settings, without leaving the app. Prompts for the username, then runs the
    // email/password login flow. A user account can own many nodes.
    void loginToUserAccount();
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
    // The persisted history (up to kNetworkLogLimit lines of colored HTML) is
    // rendered lazily on the first visit to the Log section instead of in
    // buildLogSection() — the full render cost ~300ms of the constructor. Live
    // logSystem() lines still append immediately; the first visit's rebuild
    // re-renders the whole buffer in order, so nothing is lost or reordered.
    bool m_networkLogViewStale = false;
    // Quick category filter + on-disk persistence for the network log so the
    // history survives a restart and can be narrowed to one event type. Filter
    // chips are rebuilt as new categories appear.
    QString logBadgeFor(const QString &storedLine) const; // category of a line
    QString logAccentFor(const QString &storedLine) const; // badge colour of a line
    void rebuildLogFilterButtons(); // (re)build the category chip row
    void rebuildNetworkLogView();   // re-render the log honoring m_logFilter
    QString networkLogPath() const; // on-disk path for the persisted log
    void loadNetworkLog();          // restore log history at startup
    void saveNetworkLog();          // rewrite (and trim) the on-disk log
    // Only the newest kNetworkLogSegmentSize matching lines are rendered up
    // front; m_logRenderFrom is the m_networkLog index of the oldest line
    // currently shown (0 once every matching line has been loaded). Reaching
    // the top of the scroll area loads the next older segment.
    int m_logRenderFrom = 0;
    // Guards against the scrollbar's valueChanged firing (and re-entering the
    // loader) while rebuildNetworkLogView()/loadOlderNetworkLogSegment() are
    // themselves mutating the document — clear()/insertHtml() can transiently
    // report the scrollbar at its minimum mid-edit.
    bool m_logViewMutating = false;
    // True while the view is showing the "No X events recorded." placeholder for
    // a filter that currently matches nothing (see rebuildNetworkLogView).
    bool m_logFilterEmptyNotice = false;
    void loadOlderNetworkLogSegment();
    void onNetworkLogScrolled(int value);
    // Compact success/failure banner pinned to the top of the footer's mini-log
    // panel, beside the log lines it explains. Auto-clears after a few seconds.
    // `clickHref` makes the whole toast a clickable link routed by the
    // m_topMessage linkActivated handler (e.g. "fm:agent:<id>" to jump to a
    // waiting agent). Empty = a plain, non-clickable toast.
    void flashMessage(const QString &text, bool error = false,
                      const QString &clickHref = QString());
    void dismissTopMessage(); // hide the top toast and its Copy / dismiss buttons
    void advanceTopMessageQueue(); // show the next queued error, or dismiss if none left
    void renderTopMessageCountdown(); // (re)paint the toast with its seconds-left suffix
    void renderTopMessage(); // (re)paint the toast, elided or expanded in place
    void positionTopMessageOverlay(); // size + anchor the floating expanded-toast panel
    // False while the footer (and with it the mini-log the toast is docked in) is
    // hidden — the Git workspace does that. Messages then float in the overlay
    // instead of vanishing.
    bool topMessageDockVisible() const;
    MessageRow *addMessageRow(const ChatMessage &message);
    void renderConversationRows(); // rebuilds rows in place; caller handles scrolling
    void rebuildConversationView();
    void scrollToBottom();
    void setChannels(const QStringList &channels);
    void setRoster(const QList<MemberInfo> &members);
    // Route this node's one-time "just joined" greeting to its identity-specific
    // welcome room.
    // room. Only a brand-new identity announces (gated by a sentinel file next
    // to the identity key), so the network sees a single join line with no
    // per-peer duplicates, and no re-announce on a settings-only reset (#192).
    QString welcomeChannelForIdentity() const;
    void maybeAnnounceWelcome();
    void removeChatMember(const QString &id, const QString &name);
    // A conversation key is either a channel ("#general") or a direct chat
    // ("@<peerId>").
    void switchConversation(const QString &conversation);
    void openDirectChat(const QString &peerId, const QString &peerName);
    void refreshChannelList();
    void refreshDmList();
    // Rebuild the current-room members popup from the live roster, enriched by
    // matching account-directory records.
    void refreshChatMembers();
    void refreshChatUserDirectory();
    void mergeChatUserDirectory(const QJsonArray &users);
    // Profile popup for a row in the chat users column: who they are, when
    // they joined, their nodes, and extra account info fetched on demand.
    void showChatUserProfile(const MemberInfo &member, const QStringList &nodeLines);
    void promptAddChannel();
    // Create an invite-only room (see ServerNode::createPrivateChannel) and start
    // in it. Its name is remembered so it survives a reconnect/restart.
    void promptAddPrivateChannel();
    // Offer the online members as invitees for the current private room.
    void promptInviteToPrivateChannel();
    // Confirm, then delete a room from this node (right-click a room in the
    // sidebar). Local only — the room stays for everyone else.
    void promptDeleteRoom(const QString &channel);
    // Pop a small emoji grid under `anchor`; picking one inserts it into the
    // composer at the caret.
    void showEmojiPicker(QWidget *anchor);
    void insertEmojiIntoComposer(const QString &emoji);
    // Re-create the private rooms we own/were invited to after a fresh connect,
    // since the backend clears its channel set each session.
    void restorePrivateChannels();
    // Save m_privateChannels to QSettings so they outlive a restart.
    void persistPrivateChannels();
    void sendCurrentMessage();
    // ForkBot bridge: the bot has no room connection of its own — whichever
    // client SENDS a message mentioning @forkbot asks the relay's
    // /api/forkbot/chat and relays the reply into the room (the web chat
    // surfaces do exactly the same, so this makes ForkBot answer desktop
    // users too). Author-side only, so two clients never double-trigger it.
    void maybeAskForkbot(const QString &conversation, const QString &text);
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
    // Drops messages past kChatMessageRetentionMs (7 days) from local history,
    // so a node left running that long doesn't keep showing/serving messages
    // the relay has already dropped.
    void pruneExpiredChatHistory();
    // Avatars are cached to disk per peer (keyed by node id) so they survive a
    // restart and stay visible for peers who are currently offline — otherwise
    // an avatar only lives as long as the sender keeps re-broadcasting it.
    QString avatarCachePath(const QString &peerId) const;
    void loadCachedAvatars();
    void loadRepositories();
    // An account/owner rename re-derives a record's <owner>-<name>.git mirror
    // path while the bare mirror stays on disk under its old name; the working
    // copy's push remote and post-receive hook keep feeding the old directory,
    // but workflow discovery, publishing, and the attested state hash all read
    // the missing new path — pushes stop kicking off actions and the node
    // advertises stale state (adhoc #227). Adopt the mirror the working copy
    // actually pushes into by moving it to the recorded path (or repointing
    // the record at it when the move fails). Returns true when the record was
    // modified and needs saving.
    bool reconcileMirrorPath(RepositoryRecord &repo);
    // Point a record at the temporary materialization now serving it after a
    // sealing pass. Release artifact blobs live beside the git data instead of
    // in it, so they are carried into the replacement directory first —
    // otherwise every re-seal silently drops the binaries this node hosts.
    void adoptMaterializedMirror(RepositoryRecord &repo,
                                 const QString &repositoryPath);
    void saveRepositories() const;
    void refreshRepositoryList();
    // Node handles offered by the @-mention autocomplete in comment editors:
    // every node the relay knows about (connected, discovered, mirroring) plus
    // the authors of the currently loaded issues/PRs (contributors who may be
    // offline). Sorted, de-duplicated, and cheap to build from in-memory state.
    QStringList mentionCandidateNames() const;
    void promptAddRepository();
    // Create a brand-new Git repository. Opens a single screen that collects the
    // name, a description, an optional first prompt, and a README choice, then
    // runs `git init -b main`, seeds the repo, and mirrors + publishes it under
    // this node like a local repo.
    void createNewRepository();
    // Core of createNewRepository(), separated from its dialog so it can be
    // driven by tests. `dest` must be an existing empty directory; on success it
    // is git-init'd on main, optionally seeded with a README and
    // .forkmesh/info.json description, initial-committed, registered, published,
    // and (when firstPrompt is non-empty) has its first issue filed. Returns the
    // new repository index, or -1 with a message in *error on failure.
    // isPrivate keeps the repo out of the public catalog from the start.
    int provisionNewRepository(const QString &dest, const QString &name,
                               const QString &description,
                               const QString &firstPrompt, bool addReadme,
                               bool isPrivate, QString *error);
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
    // Source-of-truth propagation to SSH-fed headless mirrors: push the served
    // bare mirror's heads+tags to every ssh:// remote configured on the working
    // copy (e.g. the ssh.<worker> gateway feeding mirror2/mirror3). Async and
    // best-effort; without it those mirrors only advance on a manual push.
    void pushToSshMirrorRemotes(int index);
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
    // Second half of syncRepository: spawn the async fetch/clone once the
    // off-thread pre-fetch prep (refs digest + origin set-url) has finished.
    void startSyncFetch(int index, bool quiet, bool hasMirror,
                        const QStringList &args, const QString &beforeDigest,
                        const QString &beforeHeadCommit);
    void autoSyncMirrors();
    // Periodic-timer wrapper for autoSyncMirrors(): skips the round while the
    // relay host sits in BackoffNetworkAccessManager's 429/5xx cooldown, since
    // the git fetch subprocesses bypass that manager entirely. Explicit "sync
    // now" paths (headlessSyncNow) call autoSyncMirrors() directly, ungated.
    void autoSyncMirrorsIfRelayHealthy();
    // Roster-driven catch-up: when a peer advertises a commit our mirror lacks,
    // pull it immediately instead of waiting for the next auto-sync tick.
    void syncMirrorsBehindRoster();
    // After a local change to a repo (new/updated issue, PR, comment, merge),
    // push it to the bare mirror and tell peers immediately instead of waiting
    // for the three-minute auto-sync, so counts and content converge right away.
    void propagateRepoUpdate(int index);
    // A peer announced it refreshed "owner/name" from source; notify if we
    // mirror the same repo. `commit` is the new HEAD it advanced to.
    void onPeerMirrorUpdated(const QString &ownerName, const QString &peerName,
                             const QString &commit);
    // A peer reported it finished pulling "owner/name" up to `commit` — the
    // round-trip acknowledgement, surfaced quietly if we hold the same repo.
    void onPeerMirrorSynced(const QString &ownerName, const QString &peerName,
                            const QString &commit);
    // Patch a peer's advertised HEAD for the "owner/name" mirror group in the
    // live roster to `commit` the instant it reports it (mirror-update / synced
    // ack), so the Mirror nodes panel converges immediately instead of waiting
    // for that peer's next hello. Returns true if any advert changed.
    bool applyPeerMirrorCommit(const QString &ownerName, const QString &peerName,
                               const QString &commit);
    // A peer opened an encrypted cove. If this node created it (creatorKey matches
    // our identity) and the opener's signature checks out, raise a notification.
    void onCoveOpened(const QString &creatorKey, const QString &coveId,
                      const QString &coveName, const QString &openerKey,
                      const QString &openerName, qint64 ts, const QString &signature);
    // A peer granted us (inviteeAccount matches our account) access to an
    // account-scoped cove. Raise a notification so we know to go look.
    void onCoveInvited(const QString &inviteeAccount, const QString &coveId,
                       const QString &coveName, const QString &inviterName, qint64 ts);
    void quickRebuildRestart();
    // If a rebuild & restart was queued behind running agents, kick it off once
    // the fleet has gone idle. Called from the agent status/finished handlers.
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
    // Live relay event channel (ForkMeshNodes DO): pushed event frames run
    // scheduleRelaySync() the moment the relay records a change, so the
    // m_inboxPollTimer HTTPS poll is only the reconnect-gap safety net.
    void startNodeEventSocket();
    void stopNodeEventSocket();
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
    QByteArray privateReplicaAuthorization(
        const RepositoryRecord &repo) const;
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
    // Full-height, app-wide activity rail. Primary section buttons are created
    // by buildBreadcrumb(), then placed here by buildChatPage(); repository-only
    // tools (currently Git) are inserted when the lazy repo detail is built.
    QVBoxLayout *m_appNavigationRailLayout = nullptr;
    QSystemTrayIcon *m_trayIcon;

    // Configured mainnode relays (switched via the top-bar relay dropdown).
    QList<ServerConfig> m_servers;
    int m_activeServer = 0;
    QHash<QString, QPixmap> m_faviconCache; // host -> favicon
    QSet<QString> m_faviconFetching;        // hosts with an in-flight favicon GET
    QSet<QString> m_faviconMissing;         // hosts whose favicon GET failed once

    // Optional public payout-address banner (hidden outside explicit settings).
    QWidget *m_solanaBanner = nullptr;
    QLabel *m_solanaBannerLabel = nullptr;
    // Payout-setting check: validates only the public address and existing
    // locally signable node identity. It never asks for a deposit or claims to
    // prove control of the external wallet.
    QWidget *m_walletVerifyBanner = nullptr;
    QWidget *buildWalletVerifyNotice();
    void updateWalletVerifyNotice();

    // Top breadcrumb bar (active server favicon + server > section).
    QLabel *m_breadcrumb = nullptr;
    // Top-bar relay switcher: a "favicon  domain ▾ count" dropdown button
    // (search/switch/add relays).
    QPushButton *m_relayMenuButton = nullptr;
    // Spinning-radar + latency readout sitting on the window-chrome line just
    // left of the CPU/MEM/DISK sparklines: probes the active relay once a
    // minute and shows the round-trip time (e.g. "33ms") centered in the dish,
    // turning into a red alert when the relay doesn't answer. Held as a
    // QWidget* and poked via static_cast (concrete RelayRadarWidget is private to
    // MainWindow.cpp).
    QWidget *m_relayRadar = nullptr;
    QTimer *m_relayLatencyTimer = nullptr; // one-minute relay-latency probe
    bool m_relayProbeInFlight = false;     // guard against overlapping probes
    qint64 m_lastWsLatencySampleMs = 0;    // when the room socket last ponged
    int m_relayProbeFailures = 0;          // consecutive failed probes; the radar
                                           // only flips to "offline" after the
                                           // second miss (one blip isn't an outage)
    int m_relayProbeElevated = 0;          // consecutive elevated-latency samples;
                                           // backs off the confirm re-probe so a
                                           // persistently-slow link isn't polled
                                           // every second forever (adhoc #74)
    // "Node" / "Repo" captions before each top-bar dropdown (the relay
    // switcher shows its favicon in the dropdown itself instead of a caption).
    QLabel *m_nodeLabel = nullptr;
    QLabel *m_repoLabel = nullptr;
    QLabel *m_navNodeName = nullptr;     // "user/node" beside the balance
    QLabel *m_navSolanaBalance = nullptr;
    // Super-tiny Claude Code and Codex usage charts in the top-right cluster
    // (issue #266): two horizontal bars (5-hour + weekly) sitting beside the
    // public-wallet balance/avatar. Held as QWidget* and poked via static_cast,
    // since their concrete type (TokenUsageMiniChart) is private to MainWindow.cpp.
    QWidget *m_navTokenUsage = nullptr;
    QWidget *m_navCodexUsage = nullptr;
    // Reward-availability cluster, now living in the node profile panel right
    // under Mirror reward settings: a clear on/off switch (ToggleSwitch, private to
    // MainWindowChat.cpp) that takes this node offline (stops serving + the
    // reward heartbeat), a status label spelling out on/off, a clear
    // "may be eligible; selection not guaranteed" / "offline; not publishing
    // eligibility" status line, and a live "online Xh Ym" uptime readout.
    // m_nodeOffline is persisted so a
    // node the user deliberately took offline stays offline across restarts.
    QAbstractButton *m_nodeOnlineToggle = nullptr;
    QLabel *m_nodeOnlineStatusLabel = nullptr;
    QLabel *m_nodeRewardStatus = nullptr;
    QLabel *m_nodeUptimeLabel = nullptr;
    QWidget *m_profileOnlineSection = nullptr; // wraps the switch + status lines
    bool m_nodeOffline = false;
    // Cached balance + fiat rates so cycling the currency view reuses what we
    // already fetched instead of re-querying getBalance / the price API each
    // click (which used to rate-limit and leave the figure stuck).
    qint64 m_navSolanaLamports = -1; // last known balance, -1 = not yet fetched
    // getBalance is only issued on hover (see refreshNavSolanaBalance); these
    // track when the cached figure was fetched and whether a query is already
    // out, so re-entering the label doesn't queue a second RPC.
    qint64 m_navSolanaFetchedMs = 0;
    bool m_navSolanaFetchInFlight = false;
    // cur -> {rate, attemptedMs}; a 0 rate records a failed attempt so the
    // price API is backed off rather than re-asked on every render.
    QHash<QString, QPair<double, qint64>> m_navFiatRates;
    bool m_navFiatFetchInFlight = false;
    // The web user's public profile is authoritative for the top-bar wallet.
    // A local node setting is used only until that user profile has resolved.
    QString m_webSolanaAccount;
    QString m_webSolanaAddress;
    bool m_webSolanaKnown = false;
    bool m_webSolanaFetchInFlight = false;
    qint64 m_webSolanaFetchedMs = 0;
    QTimer *m_webSolanaTimer = nullptr;
    QPushButton *m_chatButton = nullptr; // top-bar chat toggle (next to the bell)
    QLabel *m_chatUnreadBadge = nullptr; // red unread-count badge over the chat button
    // "Agents (N)" and its live fleet matrix, both on the window-chrome line
    // immediately left of the Back/Forward buttons.
    QPushButton *m_agentsNavButton = nullptr;
    AgentDotMatrix *m_agentDotMatrix = nullptr;
    // Last status tally rendered into the matrix's tooltip, so the scanner tick
    // can skip rebuilding an unchanged string ~20x a second.
    QString m_agentDotTooltipKey;
    // Small connection status dot painted over the top-right avatar (green
    // online / amber connecting / grey offline), replacing the old text pill.
    QLabel *m_connectionDot = nullptr;
    QString m_connectionStatusColor;      // last dot colour (skip redundant repaints)
    QLabel *m_topMessage = nullptr;       // compact centered success/failure toast text
    QFrame *m_topMessageContainer = nullptr; // bordered pill wrapping the text + Expand/Copy/✕
    QTimer *m_topMessageTimer = nullptr;  // auto-clears the centered toast
    QPushButton *m_topMessageCopy = nullptr; // copy-to-clipboard for error toasts
    QPushButton *m_topMessageClose = nullptr; // dismiss "x" for persistent error toasts
    QPushButton *m_topMessageExpand = nullptr; // expand/collapse a truncated toast in place
    QFrame *m_topMessageOverlay = nullptr; // floats the expanded full text on top of the layout
    QLabel *m_topMessageOverlayText = nullptr; // wrapped full-message label inside the overlay
    QString m_topMessageRaw;              // plain text of the current toast, for copy
    QString m_topMessageBaseHtml;         // toast HTML without the countdown suffix
    QString m_topMessageHref;             // when set, the toast is a clickable link (routed by linkActivated)
    int m_topMessageSecondsLeft = 0;      // seconds before an auto-dismiss toast fades
    // Pending error messages that arrived while another error toast was already
    // counting down. A burst of quick failures (e.g. retries) would otherwise
    // stomp each other before any could be read; queuing gives each its own
    // full countdown once the current one finishes (see advanceTopMessageQueue).
    QStringList m_topMessageQueue;
    bool m_topMessageError = false;       // current toast is a failure (red) vs success (green)
    bool m_topMessageElided = false;      // current toast was truncated (Expand reveals it inline)
    bool m_topMessageExpanded = false;    // user expanded the truncated toast to its full text
    bool m_repoPinMismatch = false;       // true when the open repo's served refs no longer match the relay's pinned hash (adhoc #65)
    QHash<QString, qint64> m_repoPinAutoHealAtMs; // owner/name -> last automatic pin re-attest (rate-limits the source-of-truth auto-heal in refreshRepoPinBanner)

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
    // Scrollable live log pinned to the bottom of the window: shows as many recent
    // lines as fit tall, with a scrollbar so earlier history can be scrolled back
    // to. Streams every logSystem()/appendUpdateLog() line, including the
    // session-start/session-end/rebuild markers. A QTextEdit (not the plain
    // variant) so each line can lead with the site favicon <img> the full Log
    // view uses — QPlainTextEdit drops images (adhoc #436).
    QTextEdit *m_footerUpdateLog = nullptr;
    // Whole mini-log/background/agent-prompt footer. The focused Git workspace
    // hides it to give the changes list and diff the full window height.
    QWidget *m_footerDock = nullptr;
    // Background-activity strip, wedged between the live log and the prompt. One
    // row per open *kind* of work, not per ticket: dozens of concurrent git reads
    // collapse into a single "git ×12" line, so the strip stays readable and the
    // widget churn stays flat no matter how busy the app gets.
    QFrame *m_backgroundQueue = nullptr;
    QLabel *m_backgroundQueueTitle = nullptr;
    // Dimmed "idle" placeholder shown in place of the rows while nothing is in
    // flight — the panel is permanent, so its body is never empty (adhoc #419).
    QLabel *m_backgroundQueueIdleLabel = nullptr;
    QWidget *m_backgroundQueueRowsHost = nullptr;
    QVBoxLayout *m_backgroundQueueRowsLayout = nullptr;
    QScrollArea *m_backgroundQueueScroll = nullptr;
    QHash<QString, QWidget *> m_backgroundTaskRows;      // word -> row
    QHash<QString, QLabel *> m_backgroundTaskSpinners;   // word -> spinner glyph
    QHash<QString, QLabel *> m_backgroundTaskLabels;     // word -> "git ×3"
    QHash<QString, int> m_backgroundTaskCounts;          // word -> open tickets
    QHash<QString, qint64> m_backgroundTaskSince;        // word -> first ticket ms
    QHash<QString, QString> m_backgroundTaskDetails;     // word -> newest note
    QHash<quint64, QString> m_backgroundTaskWords;       // ticket -> word
    QHash<QString, bool> m_backgroundTaskHadUiBlocking;  // word -> any UI scope
    // Pending log outcome per kind: one tally for asynchronous/worker work (✓)
    // and one for a group that included GUI-thread blocking work (✕), so a burst
    // of same-kind tickets becomes one accurate summary instead of hundreds.
    struct BackgroundOutcomeTally {
        int runs = 0;
        qint64 longestMs = 0;
        qint64 firstAt = 0;
        QString detail;
    };
    QHash<QString, BackgroundOutcomeTally> m_backgroundTaskDone; // word -> ✓
    QHash<QString, BackgroundOutcomeTally> m_backgroundTaskUiBlocking; // word -> ✕
    QTimer *m_backgroundTaskSpinTimer = nullptr;
    int m_backgroundTaskRowHeight = 18;
    int m_backgroundTaskSpinFrame = 0;
    int m_backgroundTaskIdleTicks = 0;
    // Set while a root-launched "Update, rebuild & restart" is running so build
    // steps and the relaunch run as this non-root user. Empty = run in-process.
    QString m_updateAsUser;

    // Chat widgets
    QLabel *m_statusLine;
    QLabel *m_channelTitle;
    QLabel *m_encryptionLabel;
    QPushButton *m_inviteButton = nullptr; // "Invite" — shown only in private rooms
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
    QPushButton *m_reposNavButton = nullptr; // network-wide "Repos" section
    QPushButton *m_settingsNavButton = nullptr; // Settings button on the repo header row
    QPushButton *m_logNavButton = nullptr; // full Log destination in the left rail
    QPushButton *m_floatingLogButton = nullptr; // retired; retained for safe resize no-op
    QPushButton *m_controlNodeNavButton = nullptr; // local control-node operations
    QPushButton *m_hostsNavButton = nullptr;  // "Hosts" top-nav button (adhoc #263)
    QPushButton *m_nodesNavButton = nullptr;  // "Nodes" top-nav button (adhoc #9)
    QPushButton *m_relaysNavButton = nullptr; // "Relays" top-nav button
    QPushButton *m_networkNavButton = nullptr; // "Network" diagnostics top-nav button
    QPushButton *m_navRebuildButton = nullptr; // small rebuild+restart button (opt-in)
    QPushButton *m_navScreenshotButton = nullptr; // drag-a-region screenshot -> prompt
    QPushButton *m_navDrawButton = nullptr; // pencil -> draw freehand on the screen
    QPushButton *m_navResizeButton = nullptr; // snap window to a common minimal size
    QPushButton *m_restartSpinButton = nullptr; // button whose icon spins mid-restart
    // A manual rebuild & restart was requested while an agent was still running:
    // hold it until the fleet goes idle, then fire automatically (adhoc #75).
    bool m_rebuildRestartQueued = false;
    // Rechecks the queued rebuild every few seconds while it waits — a safety net
    // for agent-completion paths that miss their maybeStartQueuedRebuild() call
    // (adhoc #104/#111/#116/#134/#143 each found one more).
    QTimer *m_rebuildQueuePollTimer = nullptr;
    QTableWidget *m_networkReposTable = nullptr;
    QLabel *m_networkReposStatus = nullptr;
    QPushButton *m_networkReposRefreshButton = nullptr;
    int m_networkReposLoadGen = 0;
    QJsonArray m_networkReposLastPayload; // regroup when user/node ownership arrives
    // Opaque private archive ids are delivered only in an authenticated,
    // ACL-filtered catalog response. They let the client use a name-free
    // /api/private-replicas/<id> URL; no private owner/repository identity is
    // placed in an edge-visible path or query string.
    QHash<QString, QString> m_privateCatalogAccessIds; // owner/name -> 64 hex
    // Local control-node section. The Cloudflare token exists only in the
    // password edit/process environment and the short-lived redaction copy;
    // unlike public deployment fields, it is never written to QSettings.
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
    QCheckBox *m_cloudflareConnectCheck = nullptr;
    QPushButton *m_cloudflareDryRunButton = nullptr;
    QPushButton *m_cloudflareDeployButton = nullptr;
    QPushButton *m_cloudflareCancelButton = nullptr;
    QPlainTextEdit *m_controlNodeOutput = nullptr;
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
    QTimer *m_controlNodeRefreshTimer = nullptr;
    QTimer *m_directMirrorRegistrationTimer = nullptr;
    // Full encrypted-archive authentication hashes hundreds of megabytes for a
    // large mirror. Keep it off the GUI thread and let the Control page render
    // the most recent completed snapshot.
    QHash<QString, bool> m_controlMirrorReadyCache;
    bool m_controlMirrorProbeInFlight = false;
    qint64 m_controlMirrorProbeCompletedAtMs = 0;
    QString m_controlPermissionsSignature;
    // Community reward pool: no private material is held in these widgets or
    // members. Only the vault's public address, public chain intents, and public
    // submitted transaction identifiers are retained in memory/settings.
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
    // Hosts section widgets (adhoc #263): one-host install form + live log.
    QLineEdit *m_hostIpEdit = nullptr;
    QLineEdit *m_hostUserEdit = nullptr;
    QLineEdit *m_hostPassEdit = nullptr;
    QLineEdit *m_hostNameEdit = nullptr;
    // Optional SSH/sudo passwords are session-only. Legacy QSettings values are
    // migrated here once and immediately removed from persistent settings.
    QHash<QString, QString> m_hostSessionPasswords;
    // Direct-upload install (adhoc #67): stream this app's own release binary
    // to the host over the SSH session instead of the host downloading the
    // release from the relay.
    QCheckBox *m_hostUploadBinaryCheck = nullptr;
    QPushButton *m_hostAddButton = nullptr;
    QPushButton *m_hostInstallButton = nullptr;
    // Bulk published-binary install (adhoc #257): every saved host downloads
    // the current checksum-verified release and reports its installed version.
    QPushButton *m_hostInstallAllButton = nullptr;
    // Bulk uninstall + reinstall from binary (adhoc #258).
    QPushButton *m_hostReinstallAllButton = nullptr;
    // Bulk update from latest source (adhoc): rebuild + restart every saved host
    // straight from source, no release publish required.
    QPushButton *m_hostUpdateAllSourceButton = nullptr;
    QLabel *m_hostInstallStatus = nullptr;
    QPlainTextEdit *m_hostInstallLog = nullptr;
    // ANSI parser state for the live install log: a carry buffer holding an
    // escape sequence split across read chunks, plus the current SGR style
    // (foreground as 0xRRGGBB, -1 = theme default).
    QString m_hostInstallLogCarry;
    int m_hostInstallLogFg = -1;
    bool m_hostInstallLogBold = false;
    // Bounded tail of the raw (pre-ANSI-parsing) ssh output for the current
    // install/uninstall run, used only to classify a failed exit code into an
    // actionable hint (e.g. a firewall-blocked connection timeout).
    QString m_hostInstallRawTail;
    // Set by a caller that drives repeated install attempts (the Vultr
    // auto-provision retry loop) so the next run appends under this banner
    // instead of wiping the window's transcript of the earlier attempts
    // (adhoc #342). Consumed — and cleared — by runHostInstall.
    QString m_hostInstallAttemptBanner;
    // One-line summary of the most recent failed install run, so a retry loop
    // can record why each attempt failed.
    QString m_hostInstallLastFailure;
    QTableWidget *m_hostsTable = nullptr;
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
    // One-click Vultr mirror provisioning (adhoc #315). The API key is read
    // from the field (or a stored VULTR_API_KEY device variable) per run and
    // deliberately has no persistent member.
    QLineEdit *m_vultrApiKeyEdit = nullptr;
    QLineEdit *m_vultrNameEdit = nullptr;
    // Opt-in (default on): after ForkMesh installs, also install the Claude
    // Code and Codex CLIs on the new mirror and copy this device's provider
    // logins to it, so the node can run agent sessions right away (adhoc #418).
    QCheckBox *m_vultrAgentClisCheck = nullptr;
    QPushButton *m_vultrCreateButton = nullptr;
    QLabel *m_vultrStatus = nullptr;
    bool m_vultrProvisionActive = false;
    int m_vultrPollCount = 0;        // instance boot polls used this run
    int m_vultrInstallAttempts = 0;  // SSH install attempts used this run
    // Flipped once an attempt fails because no online node is mirroring the
    // repo yet (the freshly-created instance has nothing to clone/download
    // from) — every later attempt this run then uploads this app's own
    // release binary directly over the SSH session instead, which needs no
    // mirror at all.
    bool m_vultrInstallUseLocalBinary = false;
    // Snapshot of the "also install the agent CLIs" checkbox for this run, so
    // toggling it mid-provision cannot change what the run does.
    bool m_vultrInstallAgentClis = false;
    QString m_vultrDnsHostname;      // Cloudflare name provisioned this run
    // Non-secret billing/provenance facts captured from Vultr's selected plan
    // and created instance. These are persisted with the saved Host row so an
    // operator can identify the plan and expected monthly cost later.
    QJsonObject m_vultrHostMetadata;
    // "Attempt N of M at HH:mm:ss — outcome" per install attempt this run, so
    // the window can show what every attempt did instead of only the last one.
    QStringList m_vultrInstallAttemptLog;
    // Installer link-code detection (adhoc #53): rolling tail of the install
    // output so the "Link code: NNNNNN" line survives chunk splits, and a
    // per-run guard so the link popup opens once.
    QString m_hostInstallLinkTail;
    bool m_hostLinkPrompted = false;

    // Parallel fleet deploy (adhoc): when an "all hosts" action runs, every
    // saved host is deployed simultaneously, each streaming into its OWN pane in
    // a split live-output grid instead of taking turns in the single log above.
    // HostDeploySession (defined with the fleet-deploy methods above) carries
    // each host's process, output pane and per-host render/link state.
    QLabel *m_hostDeployLabel = nullptr;      // "Live output — per host" header
    QWidget *m_hostDeployPanel = nullptr;     // container holding the per-host panes
    QGridLayout *m_hostDeployGrid = nullptr;  // lays the panes out roughly square
    QList<HostDeploySession *> m_hostDeploySessions;
    int m_hostDeployRemaining = 0;            // sessions still running
    int m_hostDeployFailed = 0;               // sessions that finished with an error

    // Relays section: live list of configured relays with status / latency / version.
    QTableWidget *m_relaysTable = nullptr;
    QLabel *m_relaysStatus = nullptr;       // "Probing N relays…" / last-refreshed line
    QPushButton *m_relaysRefreshButton = nullptr;
    int m_relayProbesInFlight = 0;          // outstanding /api/version probes
    // Nodes section (adhoc #9): sortable directory of known nodes + a detail panel.
    QTableWidget *m_nodesTable = nullptr;
    QLabel *m_nodesStatus = nullptr;            // "N nodes · M online" summary line
    QPushButton *m_nodesRefreshButton = nullptr;
    QScrollArea *m_nodeDetailScroll = nullptr;  // detail panel for the selected node
    // Node names (lowercased) the relay currently reports online — an update
    // channel or a fresh signed heartbeat. Merged into the Nodes page's status so
    // headless mirror nodes that serve via the relay (but never join this
    // client's chat room) show online instead of permanently offline.
    QSet<QString> m_relayOnlineNodes;
    qint64 m_relayOnlineNodesFetchedMs = 0; // throttle between relay fetches
    // True once /api/network/stats has answered at least once, so the Nodes page
    // knows the relay's authoritative live set is available. Before the first
    // reply we fall back to the encrypted roster's presence flag; after it, the
    // relay is trusted over a possibly-stale roster entry (adhoc #43).
    bool m_relayOnlineNodesFetched = false;
    // Request firewall section: whitelist controls plus recent allow/deny
    // decisions. This is separate from m_firewallBanner, which is the older
    // inbound-peer troubleshooting banner inside Chat.
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
    QHBoxLayout *m_repoHeaderLeft = nullptr; // left cluster of the repo header row
    int m_repoPinCheckIndex = -1;            // repo index an in-flight pin check belongs to
    // One row per repo of the selected node, shown in the repo dropdown.
    struct RepoMenuEntry {
        QString label;
        QIcon icon;
        int index = -1;     // m_repositories index; -2 = advertised mirror
        QString advertised; // ownerName when index == -2
        QString detail;     // tooltip explaining what distinguishes this entry
    };
    QList<RepoMenuEntry> m_repoMenuEntries;
    QListWidget *m_dmList;
    // Current-room members live in an on-demand popup opened from the chat
    // header. The public account directory enriches those rows but does not add
    // people who are absent from the active room.
    QPushButton *m_chatMembersButton = nullptr;
    QVBoxLayout *m_chatMembersLayout = nullptr;
    QLabel *m_chatMembersHeading = nullptr;
    QWidget *m_firewallBanner;
    QLabel *m_firewallBannerLabel;
    QPushButton *m_firewallAllowButton;
    QString m_firewallPrivilegedCommand;
    // In-transcript "N unread messages" strip with a jump-to-newest arrow that
    // marks every conversation read in one click. Hidden when nothing is unread.
    QWidget *m_chatUnreadBanner = nullptr;
    QLabel *m_chatUnreadBannerLabel = nullptr;
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
    // Cached popup view for the mention completer. QCompleter::popup() lazily
    // constructs its QListView on first call, and that construction pumps
    // widget-init events through our app-wide event filter — so calling popup()
    // from inside eventFilter re-enters during construction and recurses until
    // the stack overflows (SIGSEGV). Compare against this cached pointer instead;
    // it stays null until the popup is fully built (adhoc #220).
    QAbstractItemView *m_mentionCompleterPopup = nullptr;

    // Settings section widgets
    QLineEdit *m_settingsNameEdit = nullptr;        // Username (the account)
    QLineEdit *m_settingsMachineNodeEdit = nullptr; // this machine's node name
    QLineEdit *m_settingsNodeLabelsEdit = nullptr;  // extra Actions `runs-on:` labels
    QLineEdit *m_settingsSolanaEdit = nullptr; // #66: node Solana address in Settings
    QLabel *m_settingsEmailLabel = nullptr;
    QLabel *m_settingsEmailVerifiedBadge = nullptr;
    QLabel *m_settingsAvatarPreview = nullptr;
    QLabel *m_identityBackupNag = nullptr; // #368: "back up your key" warning
    QTextBrowser *m_settingsLog = nullptr;
    QPushButton *m_logScrollLockButton = nullptr;
    bool m_logScrollLocked = false;
    QHBoxLayout *m_logFilterRow = nullptr;    // chip row above the network log
    QButtonGroup *m_logFilterGroup = nullptr; // exclusive group for filter chips
    QString m_logFilter;                      // active category badge ("" = All)
    QPushButton *m_rebuildButton = nullptr;
    QLabel *m_rebuildStatus = nullptr;
    QLineEdit *m_mirrorRootEdit = nullptr;
    QLineEdit *m_previewCacheRootEdit = nullptr;
    // Settings -> Data tab: storage breakdown table and backup/cleanup status.
    QTableWidget *m_dataDirTable = nullptr;
    QLabel *m_dataStatus = nullptr;
    // Settings -> Data tab: the hourly backup panel.
    QTableWidget *m_backupTable = nullptr;
    QCheckBox *m_backupEnabledCheck = nullptr;
    QSpinBox *m_backupKeepSpin = nullptr;
    QLabel *m_backupStatus = nullptr;
    QPushButton *m_backupNowButton = nullptr;
    QTimer *m_backupTimer = nullptr;     // hourly tick
    QProcess *m_backupProcess = nullptr; // the in-flight `tar` (one at a time)
    // Import-a-repo (GitHub/GitLab) controls.
    QLineEdit *m_importUrlEdit = nullptr;
    QPushButton *m_importButton = nullptr;
    QLabel *m_importStatus = nullptr;
    QCheckBox *m_autostartCheck = nullptr;
    QLabel *m_autostartInfo = nullptr;
    QPushButton *m_autostartRemoveButton = nullptr;
    QComboBox *m_themeCombo = nullptr;
    // Default coding-agent provider for new assignments; seeds the quick-add and
    // issue-detail provider pickers. Codex | OpenAI API | Claude API | Claude Code.
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
    QPlainTextEdit *m_prioritizePromptEdit = nullptr;
    QTimer *m_mirrorSyncTimer = nullptr;
    QTimer *m_inboxPollTimer = nullptr; // slow fallback tick for performRelaySync()
    // Coalesces control-channel "event" frames into one /api/sync.
    QTimer *m_relaySyncDebounce = nullptr;
    // False after the relay 404s /api/sync (older worker): fall back to the
    // legacy per-topic polling until the app talks to an upgraded relay again.
    bool m_relaySyncSupported = true;
    bool m_relaySyncInFlight = false;
    QSet<QString> m_mirrorIssueIntakeInFlight;
    QTimer *m_autoUpdateTimer = nullptr; // periodic check for maybeAutoUpdate()
    bool m_autoUpdateChecking = false;   // a background "git fetch" check is in flight

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
    // Two-line wrapping prompt box (adhoc #12): a QPlainTextEdit, not a single
    // line, so a typed prompt actually shows on two lines. Enter sends,
    // Shift+Enter inserts a newline; Up/Down still walk the prompt history.
    QPlainTextEdit *m_issueQuickAdd = nullptr;
    QLabel *m_quickAddCharCount = nullptr; // characters left in the title (max 16000)
    // Agent/model chooser (adhoc #29): also carries a "Manual (create issue)"
    // entry that replaces the old Agent / Create-issue checkboxes — picking it
    // files an issue from the prompt instead of starting an agent.
    QComboBox *m_quickAddAgentProvider = nullptr;
    // Prompt-row model chooser (adhoc #261/#349): Claude Code gets the live
    // Claude model list; Codex gets an editable OpenAI model list.
    QComboBox *m_quickAddClaudeModel = nullptr;
    // Permission-mode chooser (issue #348): Ask before edits/Edit automatically/
    // Plan mode/Auto mode, styled like the provider/model combos beside it and
    // backed by the same kClaudeAutoModeSetting as the agent composer's toggle.
    QComboBox *m_quickAddModeSelector = nullptr;
    QCheckBox *m_quickAddCreatePr = nullptr;    // request PR from quick-add agent
    // Up-pointing paper-airplane stacked above the normal send icon (adhoc #99):
    // sends the typed prompt as a follow-up message to the currently-selected
    // agent session instead of the quick-add issue/new-agent flow.
    QPushButton *m_quickAddSendToAgentButton = nullptr;
    // Plain "start a new agent" send button next to it (adhoc #89): tracked as a
    // member (rather than a local in setupQuickAdd) so updateQuickAddEnterTarget
    // can restyle it as the two selected/deselected agent detail changes which of
    // the two buttons Enter actually triggers.
    QPushButton *m_quickAddSendButton = nullptr;
    // Small green "Enter" badge (adhoc #89), shown on the "new" send button
    // when Enter currently activates it. The "add" (follow-up) button has no
    // such badge — it's only ever the Enter target while the Agents tab
    // itself is on screen, so the button's own green outline is enough.
    QLabel *m_quickAddSendEnterBadge = nullptr;
    QPushButton *m_quickAddImageButton = nullptr; // attach an image (issue #79)
    QStringList m_quickAddImages;               // image paths queued for next send
    QWidget *m_quickAddAttachStrip = nullptr;   // chips w/ thumbnail + "x" remove
    // Slash-actions menu (adhoc #116): the "/" button left of the Agent checkbox
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
    // "Agents:" status strip above the footer prompt (adhoc #111): a clickable
    // label plus one small colored dot per known agent session. The label opens
    // the Agents tab; each dot opens that session directly.
    QWidget *m_agentStatusRow = nullptr;
    QPushButton *m_agentStatusLabel = nullptr;
    QWidget *m_agentStatusIconsHost = nullptr;
    QHBoxLayout *m_agentStatusIconsLayout = nullptr;
    // "N more" button on the right of the strip (adhoc #115): replaces the old
    // horizontal scrollbar. Shown only when there are more sessions than fit in
    // the capped icon row; clicking it jumps to the Agents tab.
    QPushButton *m_agentStatusMoreButton = nullptr;
    // Voice input (whisper.cpp): the mic button is hidden until whisper.cpp is
    // installed. While recording, m_voiceRecordProc captures a temp WAV which
    // m_voiceTranscribeProc transcribes — once when recording stops, and live on
    // m_voiceLiveTimer ticks so dictated words appear in the prompt box as you
    // talk. m_voiceInsertPos/Len mark the span those live passes own so each
    // refresh replaces only the dictation, never the user's own text.
    QPushButton *m_quickAddMicButton = nullptr;
    // Auto-send toggle beside the mic (adhoc #45): when checked, the footer prompt
    // is submitted (same as Enter/Send) as soon as a voice dictation finishes its
    // final transcription, so you can dictate-and-go without reaching for the keyboard.
    QCheckBox *m_quickAddVoiceAutoSubmit = nullptr;
    // "YOLO" toggle beside it (adhoc #12): when checked, every agent started while
    // it is on merges its own branch into the default branch the moment its run
    // finishes successfully, instead of waiting for a pull-request review. Read at
    // launch time and stamped onto the session (AgentSession::yolo), so flipping it
    // later never changes what an already-running agent will do.
    QCheckBox *m_quickAddYolo = nullptr;
    // "Task" toggle beside it (adhoc #18): when checked, every agent started while
    // it is on also opens an organization task for the run. Read at launch time and
    // stamped onto the session (AgentSession::orgTask), so unticking it later never
    // orphans the task an already-running agent is going to close out. On by
    // default, unlike YOLO: opening a task changes nothing about the run itself.
    QCheckBox *m_quickAddTask = nullptr;
    // Dictation can target any text box, not just the footer prompt: m_voiceTargetEdit
    // is the box the current capture writes into and m_voiceActiveButton the mic that
    // started it (so its icon swaps to red while recording). m_voiceIdlePlaceholder is
    // the target's own placeholder, restored when dictation ends. m_voiceButtons holds
    // every comment-composer mic so updateVoiceInputButton() can sync their visibility.
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
    // Smoothness guards for the live preview: m_voiceLastTranscribeSize is the WAV
    // size at the last transcription pass so a tick is skipped when no new audio was
    // captured (no point reloading whisper to redo the same clip); m_voiceLastPreview
    // is the text the box currently shows so an unchanged result doesn't re-insert
    // and churn the cursor.
    qint64 m_voiceLastTranscribeSize = 0;
    QString m_voiceLastPreview;
    // Hosted-World voice control. The bridge stores only hashed, expiring
    // capabilities and listens on loopback; the hidden draft edit lets the
    // existing local Whisper/Parakeet pipeline feed transcript text back without
    // adding any browser audio transport.
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
    // Live input-level meter shown beside the mic while recording. m_voiceLevelTimer
    // samples the fresh tail of the WAV every ~80 ms; m_voiceLevelPos tracks the byte
    // offset already metered so each tick only reads the newly-captured samples.
    QProgressBar *m_voiceLevelMeter = nullptr;
    QTimer *m_voiceLevelTimer = nullptr;
    qint64 m_voiceLevelPos = 0;
    // A rotating "processing ring" (RingSpinner) overlaid around the active mic
    // button while a released clip is still being transcribed, so the post-release
    // wait reads as "still transcribing". Created lazily and reparented onto whichever
    // mic started the capture; shown/positioned by showVoiceTranscribeSpinner().
    QWidget *m_voiceTranscribeSpinner = nullptr;
    // The whisper.cpp download/build process kicked off from Settings; kept on the
    // window so closing Settings mid-install doesn't kill it.
    QProcess *m_whisperInstallProc = nullptr;
    QLabel *m_whisperStatusLabel = nullptr;      // Settings install-status line
    QPushButton *m_whisperInstallButton = nullptr;
    QComboBox *m_whisperModelCombo = nullptr;
    // Voice engine selector + Parakeet provisioning (the option to use Parakeet
    // instead of whisper.cpp). m_parakeetInstallProc is the venv/pip build, kept on
    // the window so closing Settings mid-install doesn't kill it.
    QComboBox *m_voiceEngineCombo = nullptr;
    QComboBox *m_parakeetModelCombo = nullptr;
    QProcess *m_parakeetInstallProc = nullptr;
    QComboBox *m_voiceDeviceCombo = nullptr;     // mic to capture from (adhoc #10)
    // Settings tab widget + the index of its Voice tab, so openVoiceSettings()
    // can land the mic-not-set-up click directly on that tab (adhoc #132).
    QTabWidget *m_settingsTabs = nullptr;
    int m_voiceSettingsTabIndex = -1;
    // Settings > Profile tab (adhoc #274): hosts the node profile panel — the
    // avatar/power-switch page that used to only open from the avatar button.
    QWidget *m_settingsProfileHost = nullptr;
    int m_profileSettingsTabIndex = -1;
    // Settings "Test mic" (adhoc #14): a self-contained mic check. m_voiceTestProc
    // records the chosen device to m_voiceTestWavPath; m_voiceTestTimer samples its
    // growing tail (from byte offset m_voiceTestPos) to drive m_voiceTestMeter.
    QPushButton *m_voiceTestMicButton = nullptr;
    QProgressBar *m_voiceTestMeter = nullptr;
    QProcess *m_voiceTestProc = nullptr;
    QTimer *m_voiceTestTimer = nullptr;
    QString m_voiceTestWavPath;
    qint64 m_voiceTestPos = 0;
    bool m_voiceTestRecording = false;
    // Shell-style history for the footer quick-add bar (adhoc #200): pressing Up
    // recalls the last prompt sent so it can be fired again. Newest entry last;
    // m_quickAddHistoryIndex is the entry currently shown while navigating, or -1
    // when editing the live draft (which is stashed in m_quickAddDraft).
    QStringList m_quickAddHistory;
    int m_quickAddHistoryIndex = -1;
    QString m_quickAddDraft;
    // Set while navigateQuickAddHistory() programmatically replaces the field's
    // text, so the textChanged handler doesn't mistake the recall for a manual
    // edit and reset the history position.
    bool m_quickAddHistoryNavigating = false;
    // In the bottom status bar: the git identity (name <email>) configured for
    // the repo currently open in the detail view. Updated by openRepoDetail.
    QLabel *m_footerGitIdentity = nullptr;
    // Right of the status bar: where the running executable lives on disk, so
    // it is obvious which build/checkout the open window came from.
    QLabel *m_statusAppPath = nullptr;
    // Footer diagnostics: live CPU/memory readout + UI-stall watchdog state.
    QPushButton *m_footerDiagnostics = nullptr;
    // Live one-per-second moving sparklines for CPU, host memory and disk
    // usage (adhoc #17), shown in the footer beside the diagnostics. Held as
    // QWidget* and poked via static_cast since ResourceSparkline is private to
    // MainWindowChat.cpp.
    QWidget *m_cpuChart = nullptr;
    QWidget *m_memChart = nullptr;
    QWidget *m_diskChart = nullptr;
    StallWatchdog *m_stallWatchdog = nullptr;
    QTimer *m_diagTimer = nullptr;
    int m_stallCount = 0;
    QStringList m_stallLog;          // recent stalls, each with its backtrace
    QString m_stallLogPath;          // durable on-disk stall log
    // Stall signatures already handed to an agent this session, so a recurring
    // freeze doesn't spawn a fresh agent task every time it fires (adhoc #205).
    QSet<QString> m_autoFiledStallSignatures;
    bool m_highMemoryAlertArmed = true;
    QPointer<QDialog> m_highMemoryDialog;
    QTableWidget *m_highMemoryProcessTable = nullptr;
    QLabel *m_highMemoryProcessStatus = nullptr;
    QPointer<QProcess> m_highMemoryProcessQuery;
    qulonglong m_diagLastCpuTicks = 0;
    qint64 m_diagLastCpuMs = 0;

    // Repo detail view
    int m_repoDetailIndex = -1;
    QButtonGroup *m_issueTabGroup = nullptr; // Issues / Milestones / Labels tabs
    QButtonGroup *m_repoDetailTabs = nullptr;
    // Repository chrome is hidden while the activity-rail Git workspace is
    // active so source control can use the page's full height.
    QWidget *m_repoDetailChrome = nullptr;   // repo actions + repository tabs
    QWidget *m_repoFilesModeBar = nullptr;   // Code overview / Explorer toggles
    QWidget *m_repoOverviewChrome = nullptr; // branch toolbar + commit strip
    // Thin activity rail down the repo detail page's left edge (adhoc #357):
    // Code (file browser) and Git (current changes) entries. The Git one carries
    // the working-tree change-count badge and spins while a sync is in flight;
    // updateRepoActivityRail keeps their checked state in step with the view.
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
    QTextBrowser *m_worktreeDiffView = nullptr;
    QListWidget *m_worktreeFileList = nullptr;
    QLabel *m_worktreeFilesSummary = nullptr;
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
    // Checkbox that drops .gitignored paths from the scan (adhoc #197).
    QCheckBox *m_sizeMapHideIgnored = nullptr;
    // Folder picked with "Choose folder…" so the map can size any directory on
    // disk, not just this repository's working copy. Empty means "the working
    // copy"; the reset button clears it back to that.
    QString m_sizeMapRootOverride;
    QLabel *m_sizeMapRootLabel = nullptr;
    QPushButton *m_sizeMapResetRoot = nullptr;
    QString m_sizeMapScannedPath;
    bool m_sizeMapScanning = false;
    int m_sizeMapScanEpoch = 0;
    // Container holding one StorageMiniMap per mounted filesystem; refilled on
    // every rescan so mounts appearing or vanishing are picked up.
    QWidget *m_sizeMapVolumesBox = nullptr;
    QPushButton *m_repoProjectsTab = nullptr; // handle for the Projects (N) badge
    QLabel *m_repoVisibilityHint = nullptr; // explains the current visibility
    QTableWidget *m_branchesTable = nullptr;
    // Splitter holding the branches table + changed-files/scope lists + diff view.
    // loadBranchesPanel() freezes it while it tears down and rebuilds the rows so
    // the panes don't flash blank during a refresh/merge (adhoc #256).
    QWidget *m_branchesSplit = nullptr;
    QLabel *m_branchesSummary = nullptr;
    QPushButton *m_branchPullAllButton = nullptr; // "Pull <base> into all" header action
    // When checked, a successful "Merge to main" auto-runs "Pull <base> into all"
    // so the remaining branches catch up with the merge (adhoc #250).
    QCheckBox *m_branchAutoPullAllCheck = nullptr;
    QPushButton *m_branchDeleteMergedButton = nullptr; // "Delete merged" header action
    QListWidget *m_branchFileList = nullptr;    // changed-files list beside the diff
    QLabel *m_branchFilesSummary = nullptr;     // "N files changed" header
    // Scope selector above the changed-files list: "All changes", the branch's
    // uncommitted working-tree changes (when its checkout is dirty), and one row
    // per commit the branch adds over base. Selecting a row re-renders the diff
    // for just that scope (issue: show commits + uncommitted changes, diffable).
    QListWidget *m_branchScopeList = nullptr;
    QLabel *m_branchScopeLabel = nullptr;
    QTextBrowser *m_branchDiffView = nullptr;
    QString m_branchDiffBranch;
    // Branch that auto-pull has already been attempted for (see showBranchDiff),
    // so a declined stash prompt or an aborted merge doesn't re-nag every time the
    // panel happens to rebuild while the same branch is still selected. Cleared
    // implicitly by simply differing once a different branch is selected.
    QString m_branchAutoPullAttempted;
    // Bumped each time a branch is selected / a scope diff is requested so the
    // off-thread git reads that build the scope list and render the diff can drop
    // their result if the user has since switched branch or scope (issue #353 —
    // showBranchDiff/renderBranchScopeDiff shelled git on the GUI thread).
    int m_branchScopeLoadGen = 0;
    int m_branchScopeDiffGen = 0;
    // Branch merge-conflict probes. `git merge-tree` costs ~0.5-1s per branch on a
    // busy repo, so running one per row inline froze the branches panel for
    // seconds on every rebuild — and one lands after every delete/merge/pull
    // (adhoc #416). The rows paint immediately without the flag and the probes run
    // on a worker thread instead; each verdict is memoised by the exact commit
    // pair it merged (key "<git dir>\n<base sha>\n<branch sha>"), so later
    // rebuilds and the detail pane reuse it rather than re-shelling git.
    QHash<QString, bool> m_branchConflictCache;
    QSet<QString> m_branchConflictProbes; // branches a worker is probing right now
    // Probe the given branches (pairs of branch name + cache key) for conflicts
    // with `base` off the GUI thread, painting each verdict into the table when
    // it lands.
    void startBranchConflictProbes(const QString &dir, const QString &base,
                                   const QList<QPair<QString, QString>> &probes);
    // The last patch rendered into the branch-diff pane (with its empty-state
    // message), cached so the per-file "Viewed" toggle can re-render synchronously
    // — the toggle changes only the viewed set, not the patch, so it must not pay
    // for (nor race) the now-async git read in renderBranchScopeDiff. Invalidated
    // when a new branch is selected.
    QByteArray m_branchDiffLastPatch;
    QString m_branchDiffLastEmpty;
    bool m_branchDiffLastValid = false;
    // "viewed" key for whatever scope the diff pane currently shows (whole branch,
    // a commit, or the uncommitted changes); set by renderBranchDiffPatch so the
    // per-file Viewed toggle persists against the right scope, not always "all".
    QString m_branchDiffViewedContext;
    QLabel *m_branchDiffSticky = nullptr;
    QList<QPair<int, QString>> m_branchDiffFileSpans;
    // Ordered file paths of the diff currently in the branch view, so the
    // sticky-bar span map can be rebuilt once the whole diff has landed (the
    // render is progressive — see renderDiffStreamed, adhoc #51/#421).
    QStringList m_branchDiffFilePaths;
    QPushButton *m_branchesDeleteSelBtn = nullptr;
    // Detail-pane action bar above the branch diff: acts on the selected branch
    // (m_branchDiffBranch), mirroring the worktrees tab. Their enabled/tooltip
    // state is refreshed in updateBranchDetailActions() as the selection changes.
    QLabel *m_branchDetailLabel = nullptr;      // "<branch> · N behind · M ahead"
    QPushButton *m_branchOpenCodiumButton = nullptr; // "Open in Codium" (VSCodium)
    QPushButton *m_branchMergeEditorButton = nullptr; // "Merge editor" (resolve by hand)
    QPushButton *m_branchPullButton = nullptr;  // "Pull <base>" into the branch
    QPushButton *m_branchFixButton = nullptr;   // "Fix with agent" (conflicts only)
    // Sit beside the Fix button (adhoc #56): one dropdown picks the agent/provider
    // (Claude / OpenAI / Claude Code), the other the model it runs. The button then
    // resolves with whatever the two combos currently show.
    QComboBox *m_branchFixAgentCombo = nullptr;
    QComboBox *m_branchFixModelCombo = nullptr;
    QPushButton *m_branchPrButton = nullptr;    // "Create PR" from the branch
    QPushButton *m_branchMergeButton = nullptr; // "Merge to main"
    // "Merge & delete all": the same merge, then tears down everything the branch
    // owned — its agent session(s), the branch itself and its worktree (adhoc #428).
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
    // Animates the spinning caution/error status lights in the Mirror nodes
    // table (adhoc #230); only ticks while at least one row's light spins.
    QTimer *m_nodeLightTimer = nullptr;
    int m_nodeLightFrame = 0;
    // Coalesces the heavy tail of onRequestServed (stats save + full repo-list
    // rebuild) so a clone/browse burst costs one refresh per second, not one per
    // served request.
    QTimer *m_requestServedFlushTimer = nullptr;
    // Coalesces roster-driven Mirror-nodes panel rebuilds (they shell git).
    QTimer *m_mirrorPanelRosterTimer = nullptr;
    // Commit hash -> subject/author/date, so the Mirror-nodes panel's per-row
    // lookup doesn't re-shell `git show` on every roster-driven rebuild. Only
    // used for peers that don't advertise the identity themselves.
    QHash<QString, CommitIdentity> m_commitIdentityCache;
    // "Reset integrity pin" action, shown in the Mirror nodes header only when
    // this node is the source of truth (the owner holding the working copy).
    QPushButton *m_mirrorResetPinButton = nullptr;
    // GitHub-style repo page: header actions, tabs, branch/search, About sidebar.
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
    QPushButton *m_toolbarCommitsButton = nullptr; // -> commits panel, next to Branches/Tags
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
    // True while runRepoDependencyScan()'s off-thread rescan is in flight; drives
    // the dependency card's "Scanning…" button/progress-bar state.
    bool m_repoSecurityScanRunning = false;
    // Path of the manifest being scanned (empty if scanning all manifests); used
    // to highlight which "Run scan" button is active during a per-manifest scan.
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
    // MCP connector page (Settings → MCP tab).
    QLabel *m_mcpStatusLabel = nullptr;
    QLineEdit *m_mcpTokenEdit = nullptr;
    QPlainTextEdit *m_mcpConfigEdit = nullptr;
    QPushButton *m_mcpGenerateButton = nullptr;
    QPushButton *m_mcpRevokeButton = nullptr;
    QPushButton *m_mcpTestButton = nullptr;
    QLabel *m_mcpTestLabel = nullptr;
    // The probe subprocess for "Test connection". Owned so a second click (or
    // closing the app) never leaves a stray python3 behind.
    QProcess *m_mcpTestProcess = nullptr;
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
    // Bumped on every loadCommits() so a still-pending background stat fill (see
    // fillCommitStats) from a superseded load drops itself instead of writing
    // mismatched Files/+/− counts into the new rows.
    int m_commitsLoadGen = 0;
    // Short-lived cache for repoBranches() — avoids re-running `git branch` on
    // every loadCommits() call (e.g. on each search keystroke). Keyed by dir;
    // expires after 5 s so the button menu stays fresh after branch operations.
    mutable QStringList m_branchesCache;
    mutable QString m_branchesCacheDir;
    mutable qint64 m_branchesCacheTime = 0;
    QLineEdit *m_commitSearch = nullptr;       // filter the commit list by hash/summary
    // Top-bar "search everything" box and its floating results dropdown. The popup
    // is parented to the window (not the short top bar) so it isn't clipped, and is
    // NoFocus so clicking a result doesn't steal the keyboard from the search box.
    QLineEdit *m_globalSearch = nullptr;
    QListWidget *m_globalSearchPopup = nullptr;
    QTimer *m_globalSearchTimer = nullptr;     // debounce keystrokes before rebuilding
    // Back / forward navigation trail (left of the search box). Each entry is a
    // place we landed on: the top-level section index, the repo open in the
    // detail panel (-1 = none), and which repo tab (Code / Commits / Issues /
    // Pulls / …) was showing, so a click onto any of them is its own step that
    // Back / Forward can return to. detailTab is -1 outside the Code section.
    struct NavPlace {
        int section = 0;
        int repoIndex = -1;
        int detailTab = -1;
        bool operator==(const NavPlace &o) const
        {
            return section == o.section && repoIndex == o.repoIndex &&
                   detailTab == o.detailTab;
        }
    };
    QPushButton *m_navBackButton = nullptr;
    QPushButton *m_navForwardButton = nullptr;
    QList<NavPlace> m_navHistory;
    int m_navHistoryIndex = -1;     // current position in m_navHistory
    bool m_navRestoring = false;    // suppress recording while replaying the trail
    bool m_navRecordPending = false; // a debounced capture is already queued
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
    // Expandable file view under the banner: one entry per pending commit, its
    // children the files that commit touches. Toggled by the banner's
    // "Show files" link; the expanded state survives reloads.
    QTreeWidget *m_commitsUnsyncedFiles = nullptr;
    bool m_commitsUnsyncedExpanded = false;
    QStringList m_commitsUnsyncedHashes; // pending commits, newest first
    // File-row click: open the commit's diff scrolled to this file once the
    // async detail load lands (renderCommitDetail consumes it).
    QString m_pendingCommitFileScroll;
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
    // Source Control panel (left side of the Commits tab).
    static constexpr int kCommitWorkspaceChangesPage = 0;
    static constexpr int kCommitWorkspaceCommitPage = 1;
    QWidget *m_scmPanel = nullptr;
    QWidget *m_scmControlsPanel = nullptr;
    QTreeWidget *m_scmTree = nullptr;
    QPlainTextEdit *m_scmMessage = nullptr; // compact two-line commit/post draft
    QTextBrowser *m_scmDiff = nullptr;
    QLabel *m_scmCountLabel = nullptr;
    QLabel *m_scmViewedLabel = nullptr;  // "3 of 26 files viewed"
    QPushButton *m_scmAutoViewedButton = nullptr; // auto-mark-viewed-on-scroll
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
    // Combined working-tree diff (adhoc #399): every changed file lives in one
    // scrollable view, so reviewing is a single scroll and each file checks itself
    // off as "Viewed" once its end has passed the viewport bottom.
    // Section key is "s|<path>" / "u|<path>" — a path modified *and* staged
    // renders twice, once per side — in rendered (top-to-bottom) order.
    QString m_scmCombinedPatch;      // cached raw patch behind the render
    int m_scmCombinedStagedFiles = 0; // how many of its files are the staged half
    bool m_scmPatchValid = false;    // false until the patch is (re-)read from git
    QStringList m_scmSectionKeys;
    QStringList m_scmSectionAnchors; // "file-N" per section, aligned to the keys
    QStringList m_scmSectionPaths;   // repo-relative path per section
    QList<int> m_scmFileTops;        // cached absolute y of each section header
    QHash<QString, QString> m_scmStickyLabelHtml; // section key -> sticky label
    QString m_scmDiffRenderKey;      // skip the re-layout when nothing changed
    QFrame *m_scmStickyHeader = nullptr;
    QLabel *m_scmStickyPath = nullptr;
    PacmanProgress *m_scmStickyPacman = nullptr;
    QLabel *m_scmStickyPercent = nullptr; // "42%" read-through of this file
    QPushButton *m_scmStickyViewed = nullptr;
    QString m_scmStickySection;      // section key shown in the sticky header
    QTimer *m_scmAutoViewedDebounce = nullptr;
    // Set while the tree selection is following the diff scroll, so
    // currentItemChanged doesn't bounce the diff back to the file header.
    bool m_scmSuppressFileScroll = false;
    // Commits tab: a stack flipping between the list and a per-commit diff view.
    QStackedWidget *m_commitsStack = nullptr;
    QLabel *m_commitTitle = nullptr;
    QLabel *m_commitMeta = nullptr;
    QLabel *m_commitMessage = nullptr;
    QLabel *m_commitFilesSummary = nullptr;
    QListWidget *m_commitFileList = nullptr;
    QTextBrowser *m_commitDiffView = nullptr;
    QWidget *m_commitDiffSpinner = nullptr; // inline spinner by the files heading
    // showCommit() async-load generation: each click bumps it; stale callbacks
    // from a superseded load compare and bail (last click wins).
    int m_commitLoadGen = 0;
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
    // Re-entrancy guard: loadRepoOverview's git reads pump the event loop
    // (GitKeepAlive), so a queued slot serviced mid-load could call back in and
    // interleave a second rebuild with the first.
    bool m_overviewLoading = false;
    int m_treeLoadedForIndex = -1;          // repo whose explorer tree is built
    QLabel *m_commitBar = nullptr;
    QPushButton *m_historyButton = nullptr;
    // Area under the latest-commit bar: 0 = crumb + file list + README,
    // 1 = the commits panel (list + diff), toggled by m_historyButton.
    QStackedWidget *m_overviewBodyStack = nullptr;
    QLabel *m_overviewCrumb = nullptr;
    QString m_commitBarStatusHash;
    QString m_commitBarBodyHtml;
    // Code overview file list: a small table per directory (name, size bar, last
    // commit, when). Rows are cached so re-sorting doesn't re-shell out to git.
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
    // Sorting is driven by clicking the overview table's column headers.
    QString m_overviewSortKey = QStringLiteral("name");
    bool m_overviewSortDesc = false;
    void populateOverviewTree(); // (re)fill m_overviewList from m_overviewRows
    QTextBrowser *m_readmeView = nullptr;
    QTreeWidget *m_repoFileTree = nullptr;
    QTabWidget *m_repoFileTabs = nullptr;
    QPushButton *m_repoFileCommitButton = nullptr;
    QPushButton *m_repoFilePullButton = nullptr;
    QPushButton *m_repoFileHistoryButton = nullptr;
    QPushButton *m_repoFilePreviewButton = nullptr; // toggle markdown source/render
    QHash<QString, QWidget *> m_openFileTabs; // repo-relative path -> editor tab
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
    // Exponential backoff for the app's periodic network pollers (owner-inbox
    // drains, node heartbeat, live Claude usage, mirror discovery) so a relay
    // that is offline or rate-limiting (HTTP 429) stops getting hammered on
    // every timer tick. Keyed per endpoint/channel; see NetworkBackoff.h.
    NetworkBackoff m_pollBackoff;
    // Issue #346: a refill notification waiting to ride the next signed
    // heartbeat (kept as flags, not fired directly, so it retries on the
    // periodic heartbeat timer if the immediate send fails).
    bool m_pendingCreditsRefilled5h = false;
    bool m_pendingCreditsRefilledWeekly = false;

    // --- Cove (encrypted vault) UI + session state ----------------------------
    QWidget *m_coveSection = nullptr;        // repo Settings "Coves" group
    QListWidget *m_coveList = nullptr;       // coves in the open repo (lock state)
    QLineEdit *m_covePasswordEdit = nullptr; // per-repo unlock password field
    QPushButton *m_covePwRevealBtn = nullptr;// reveal pw (source-of-truth only)
    QCheckBox *m_coveAutoOpenCheck = nullptr;// per-repo auto-open toggle
    QLabel *m_coveEmptyHint = nullptr;
    QLineEdit *m_coveGlobalPasswordEdit = nullptr; // global Settings password
    QCheckBox *m_coveGlobalAutoOpenCheck = nullptr;
    // Cove id -> the password that unlocked it this session (memory only). Lets
    // "auto-show" reveal a cove without re-prompting and re-encrypt on save.
    QHash<QString, QString> m_coveSessionPasswords;
    // "coveId\x1fpassword" pairs that failed to unlock, so the (deliberately
    // slow) key derivation is never re-paid for a known-bad candidate.
    QSet<QString> m_coveFailedUnlocks;

    // Pull requests tab
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
    // Shown alongside "Fix with agent" only when an agent session authored this
    // PR's branch: continues that same session rather than spinning up a fresh,
    // isolated conflict-only run.
    QPushButton *m_pullFixConflictsButton = nullptr;
    // AI code review (adhoc #82): "Review with AI" reads the PR's diff and posts
    // line-anchored review threads — findings with a safe mechanical fix carry a
    // committable suggestion patch. "Fix all with AI" hands every unresolved
    // review thread to a Claude Code agent that edits the PR's branch directly.
    QPushButton *m_pullReviewAiButton = nullptr;
    QPushButton *m_pullFixAllAiButton = nullptr;
    QPushButton *m_pullEditFileButton = nullptr; // edit selected file on PR branch
    QPushButton *m_pullDeleteFileButton = nullptr; // delete selected file on PR branch
    QPushButton *m_pullCloseButton = nullptr;
    QPushButton *m_pullReopenButton = nullptr;
    // Mirror-node-only: re-deliver this PR to the repo owner's inbox so it reaches
    // the source of truth even while that node is offline (the relay holds it).
    QPushButton *m_pullSendToSourceButton = nullptr;
    QPushButton *m_pullDeleteButton = nullptr;
    QPushButton *m_pullDeleteBranchButton = nullptr; // delete the PR and its head branch
    QPushButton *m_pullMergeDeleteButton = nullptr;  // merge, then delete the PR + branch
    QPushButton *m_pullPreviewButton = nullptr;      // build the PR and launch the app
    QDialog *m_pullPreviewDialog = nullptr;          // live build log for the preview
    bool m_pullDeleteConfirmPending = false;
    bool m_pullDeleteInProgress = false; // a deletePull worker thread is running
    QListWidget *m_pullFiles = nullptr;
    // Files-changed authorship filter (issue #365): All / Agent-authored /
    // Human-authored, driven by m_pullFileAuthorship. Hidden unless the PR mixes
    // agent and human commits.
    QComboBox *m_pullFileAuthorFilter = nullptr;
    // current PR: file path -> true when an agent-stamped commit touched it.
    QHash<QString, bool> m_pullFileAuthorship;
    QPushButton *m_pullPrevButton = nullptr; // jump to previous change in the PR
    QPushButton *m_pullNextButton = nullptr; // jump to next change in the PR
    QTextBrowser *m_pullDiff = nullptr;
    // Find-in-diff bar for the Files-changed pane (issue #333): a Ctrl+F/Esc
    // toggled bar that highlights every match in m_pullDiff and steps between
    // them, similar to a browser's in-page search.
    QWidget *m_pullDiffSearchBar = nullptr;
    QLineEdit *m_pullDiffSearchInput = nullptr;
    QLabel *m_pullDiffSearchCount = nullptr;
    // Matches for the current search term, recomputed whenever the term or the
    // rendered diff changes (a re-render replaces the document, invalidating
    // any previously-found cursors).
    QList<QTextCursor> m_pullDiffSearchMatches;
    int m_pullDiffSearchIndex = -1;
    // Signature (stylesheet + html) of what m_pullDiff currently shows, so
    // renderPullDiff can skip the costly QTextDocument table re-layout when a
    // refresh/poll re-renders the same file with unchanged content. Cleared
    // whenever the widget is set to something other than a rendered diff.
    QString m_pullDiffRenderKey;
    // current PR: file path -> the "file-N" HTML anchor in the all-files diff,
    // so selecting a file in the list (or Prev/Next) can scroll straight to it.
    QHash<QString, QString> m_pullFileAnchors;
    // Same files, in the order they appear in the rendered diff, so auto-mark-
    // viewed-on-scroll can tell which files are above/below the current file.
    QStringList m_pullFileOrder;
    // Set while the file list is being re-selected to follow the diff scroll, so
    // currentItemChanged doesn't scroll the diff back to the file header.
    bool m_pullSuppressFileScroll = false;
    // "Auto-mark viewed" toggle + its scroll debounce (issue: mark files viewed
    // while scrolling the PR diff, mirroring GitHub's same-named setting).
    QPushButton *m_pullAutoViewedButton = nullptr;
    QTimer *m_pullAutoViewedDebounce = nullptr;
    // Sticky diff header overlay (adhoc #56): floats a copy of the current
    // file's header at the top of the scrolling diff so the filename / +/- stat
    // / Viewed controls stay visible, with a Pac-Man progress chart that fills
    // as the file scrolls past and auto-checks Viewed once the bottom is seen.
    QFrame *m_pullStickyHeader = nullptr;
    QLabel *m_pullStickyPath = nullptr;
    PacmanProgress *m_pullStickyPacman = nullptr;
    QLabel *m_pullStickyPercent = nullptr;
    QPushButton *m_pullStickyViewed = nullptr;
    QString m_pullStickyFile; // file path currently shown in the sticky header
    // path -> compact rich-text label (icon + dir/name + +/-) for that header.
    QHash<QString, QString> m_pullStickyLabelHtml;
    // Absolute document y-position of each file header, aligned to
    // m_pullFileOrder (-1 if not located). Cached because locating anchors walks
    // the whole document, which is too heavy to redo on every scroll tick; the
    // diff has word-wrap off, so these stay put until the next re-render clears
    // the cache. Filled lazily by computePullFileTops().
    QList<int> m_pullFileTops;
    int m_diffFontPt = 12; // diff viewer text size (the +/- zoom control)
    // Every diff viewer registered for shared text-size zoom (issue #254), so a
    // +/- click or Ctrl+wheel can re-render them all at the new size.
    QList<QTextEdit *> m_diffViews;
    // Scroll position to put back once a zoom re-render's streamed diff is
    // complete: right after the first paint the document is still short, so the
    // reader's place would clamp away (adhoc #421).
    QHash<QTextEdit *, int> m_diffRestoreScroll;
    QPushButton *m_pullSplitButton = nullptr; // toggle unified <-> side-by-side
    QListWidget *m_pullCommitsList = nullptr;  // commits that make up the PR
    // PR detail sub-tabs: Conversation / Commits / Checks / Files changed /
    // Badge.
    QButtonGroup *m_pullSubTabs = nullptr;
    QStackedWidget *m_pullSubStack = nullptr;
    QPushButton *m_pullTabConversation = nullptr;
    QPushButton *m_pullTabCommits = nullptr;
    QPushButton *m_pullTabChecks = nullptr;
    QPushButton *m_pullTabFiles = nullptr;
    QPushButton *m_pullTabBadge = nullptr;
    // Badge tab (adhoc #44): the PR's visual fingerprint — same design the
    // relay attaches to federated PR-opened notes and the web dashboard shows.
    PullBadgeWidget *m_pullBadgeWidget = nullptr;
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
    // Cache backing m_pullConflictByNumber so reloadPulls() doesn't re-spawn the
    // `git apply --check` dry-run for every open PR on each call (a push, a
    // search, merging another PR all trigger reloadPulls and otherwise block the
    // UI for seconds). Invalidated when the base tip moves; each per-PR entry
    // carries the patch fingerprint that produced it so an edited patch re-checks.
    QString m_pullConflictCacheBaseTip;
    // Per-PR dry-run apply result. fingerprint pins it to the patch that produced
    // it; conflictFiles is carried so updatePullActionState() can reuse this entry
    // for the current PR instead of re-spawning `git apply --check` (which blocked
    // the UI for ~1.6s on every pull selection).
    struct PullConflictEntry {
        QString fingerprint;
        bool conflict = false;
        QStringList conflictFiles;
    };
    QHash<int, PullConflictEntry> m_pullConflictCache;
    // Generation counter: each reloadPulls() bumps it so any in-flight async
    // conflict pass aborts once the repo/list it was started for has changed.
    quint64 m_pullConflictGen = 0;
    quint64 m_pullLoadGen = 0;
    bool m_pullBackgroundLoadInFlight = false;
    bool m_pullBackgroundReloadQueued = false;
    // (PR number, patch fingerprint) pairs whose dry-run apply is still pending,
    // drained one per event-loop turn by processPendingPullConflicts() so a cold
    // cache never blocks the GUI in a single sweep.
    QList<QPair<int, QString>> m_pendingPullConflictChecks;
    // True while a conflict dry-run runs on a worker thread. The drain launches
    // one `git apply --check` at a time off the GUI thread (the slow cold-cache
    // run used to block the event loop for ~1.5s); this guard keeps a second
    // drain from starting a concurrent worker before the first finishes.
    bool m_pullConflictCheckInFlight = false;
    // size:hash of a PR patch, used to invalidate a cached PullConflictEntry when
    // the patch changes. Shared by reloadPulls() and updatePullActionState().
    static QString pullPatchFingerprint(const QString &patch);
    int m_currentPullNumber = -1;

    // Actions (CI on push to the mirror)
    struct AppNotification {
        QString title;
        QString body;
        qint64 timestampMs = 0;
        bool warning = false;
        int runId = -1;
        NotificationLink link; // double-click destination (issue #292)
    };
    ActionStore *m_actionStore = nullptr;
    // A pool of runners so independent workflows (e.g. the Android build, the CI
    // tests and the Cloudflare deploy triggered by one push) execute in parallel
    // instead of queueing behind one another. All bookkeeping stays on the Qt
    // main thread; only the child processes each runner drives run concurrently.
    QList<ActionRunner *> m_actionRunners;
    QFileSystemWatcher *m_actionSpoolWatcher = nullptr;
    QList<ActionRun> m_actionRuns;   // loaded history, newest first
    QList<int> m_actionQueue;        // run ids queued for execution
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
    QPushButton *m_notificationButton = nullptr;
    QLabel *m_notificationRailBadge = nullptr;
    QTableWidget *m_notificationsTable = nullptr; // sortable Notifications page
    int m_selectedRunId = -1;
    QListWidget *m_actionWorkflowList = nullptr; // available actions (left column)
    QString m_selectedWorkflowFilter;            // workflow path filter, empty = all
    QList<ActionWorkflow> m_repoWorkflows;       // parsed workflows for the open repo
    // Coalesces push-driven refreshOpenRepoDetail() calls: a burst of pushes
    // (a sync, an agent committing) otherwise re-runs the whole heavyweight
    // refresh — git log, per-PR apply checks, branch reload — once per event,
    // serially blocking the UI. The timer collapses a burst into one refresh.
    QTimer *m_openRepoRefreshTimer = nullptr;
    QTimer *m_agentsSpinTimer = nullptr;         // animates the Agents tab while running
    int m_agentsSpinFrame = 0;
    QTimer *m_agentStatusSpinTimer = nullptr;    // animates the footer "Agents:" strip
    int m_agentStatusSpinFrame = 0;
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
    // Manual ("workflow_dispatch") run controls, shown atop the detail pane only
    // when the selected workflow opts in. The branch combo defaults to "main".
    QWidget *m_actionManualRunBar = nullptr;
    QPushButton *m_actionManualRunButton = nullptr;
    QComboBox *m_actionRunBranchCombo = nullptr;
    // Re-queues the selected run with its original workflow/commit/ref.
    QPushButton *m_actionRerunButton = nullptr;
    // Stops the selected run while it's still queued or executing.
    QPushButton *m_actionStopButton = nullptr;
    // Skips the selected run while it's still pending (queued or awaiting approval).
    QPushButton *m_actionSkipButton = nullptr;
    // Copies the selected run's full log to the clipboard.
    QPushButton *m_actionCopyLogButton = nullptr;
    // "Fix with agent" (adhoc #114): only shown for a failed run. Starts a new
    // coding agent on its own branch/PR, same as any other ad-hoc agent run, with
    // the failing run's log as its task.
    QPushButton *m_actionFixButton = nullptr;
    QComboBox *m_actionFixAgentCombo = nullptr;
    QComboBox *m_actionFixModelCombo = nullptr;
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
    QCheckBox *m_secretScanCheck = nullptr;
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
    QLabel *m_repoForkLocation = nullptr;
    QLabel *m_repoMirrorLocation = nullptr;
    QTableWidget *m_repoRemotesTable = nullptr;
    // Agent sessions assigned from issues.
    AgentStore *m_agentStore = nullptr;
    // Pool of agent runners so sessions execute in parallel (one process each)
    // instead of being serialized through a single runner.
    QList<AgentRunner *> m_agentRunners;
    QList<AgentSession> m_agentSessions;
    QList<int> m_agentQueue;
    // Guards scheduleAgentQueuePump()'s zero-timer against piling up one pump
    // per status/reload hook in a burst.
    bool m_agentQueuePumpScheduled = false;
    // True while runDeferredStartup() drains the sessions initAgents() re-queued
    // after an app restart: resumed runs must NOT jump to the Agents tab the way
    // a fresh user-driven start does. At startup that jump forced a full cold
    // openRepoDetail() before the first frame (~2s of git reads), which the
    // last-repo restore then redid from scratch moments later.
    bool m_agentQuietResume = false;
    // Sessions restored from the startup queue stay quiet through asynchronous
    // worktree preparation. m_agentQuietResume only covers the synchronous
    // queue drain; without this per-session marker its later continuation could
    // still switch to Agents after the flag had already been cleared.
    QSet<int> m_startupQuietAgentSessions;
    int m_selectedAgentSessionId = -1;
    // The session whose detail page last reset the Agent|Files tab selection. Used
    // so showAgentSession() lands on the Agent tab when a *different* session is
    // opened, without yanking the user off Files changed on a plain refresh of the
    // same session (adhoc #189).
    int m_agentDetailTabSession = -1;

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
        // Review-fix mode (adhoc #82): the Claude Code run edits the PR's branch
        // to address review findings instead of resolving conflict markers.
        // startPullAgentEdit opened the branch; finish commits every edit via
        // finishPullAgentEdit. The prompt (built from the unresolved threads by
        // fixCurrentPullFindingsWithAgent) replaces the conflict prompt.
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
    // In-flight AI code review (adhoc #82, see reviewCurrentPullWithAi): one model
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
    // One-click "Apply fix & commit" on a review thread's suggestion patch.
    void applyPullSuggestionFix(const QString &threadId);
    // "Fix all with AI": hand every unresolved review thread to a Claude Code
    // agent that edits the PR's branch (reuses the m_aiFix machinery in
    // agentEdit mode).
    void fixCurrentPullFindingsWithAgent();
    QTableWidget *m_agentTable = nullptr;
    // Free-text filter over the session list: matches issue number/title,
    // provider, status and PR. Empty shows everything (issue #82).
    QLineEdit *m_agentSearch = nullptr;
    // Compose row at the top of the session list (adhoc #234): type a prompt,
    // pick a repo and an agent provider, and start an ad-hoc agent right there
    // without going through the footer quick-add bar.
    QComboBox *m_agentComposeRepo = nullptr;
    QLineEdit *m_agentComposePrompt = nullptr;
    QComboBox *m_agentComposeProvider = nullptr;
    QPushButton *m_agentComposeButton = nullptr;
    void startAgentFromComposer();
    QWidget *m_agentDetail = nullptr; // collapsible detail panel (hidden until a row is picked)
    // "Hide detail" toggle: when checked the detail panel stays hidden even with a
    // row selected, so the session list spans the full tab width (issue #54).
    QPushButton *m_agentHideDetailButton = nullptr;
    bool m_agentDetailHidden = false;
    QLabel *m_agentTitle = nullptr;
    QLabel *m_agentStatusPill = nullptr; // connected/working/done status
    QLabel *m_agentMeta = nullptr;
    QLabel *m_agentNetPanel = nullptr;   // live API-traffic graphic
    // Permission-mode selector in the detail header (adhoc #26): change the
    // selected agent's mode in place ("Ask before edits" / "Edit automatically"
    // / "Plan mode" / "Auto mode"). Persists onto the session and, for a live
    // Codex session, retargets the next turn so the switch takes effect without
    // needing to retype the mode in the composer.
    QComboBox *m_agentModeSelector = nullptr;
    QPushButton *m_agentViewPrButton = nullptr;
    // "Create linked issue" — shown for ad-hoc sessions with no issue yet, so the
    // run can be promoted to a tracked issue from the detail header (adhoc #189).
    QPushButton *m_agentCreateIssueButton = nullptr;
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
    // adhoc #201: search-the-transcript box in the output toggle row, with a
    // "3/12" match counter and prev/next steppers over the highlighted hits.
    QLineEdit *m_transcriptSearch = nullptr;
    QLabel *m_transcriptSearchCount = nullptr;
    QPushButton *m_transcriptSearchPrev = nullptr;
    QPushButton *m_transcriptSearchNext = nullptr;
    QListWidget *m_agentFilesList = nullptr;     // files edited in this session
    QWidget *m_agentFilesPanel = nullptr;        // wraps the list + heading
    // Issue #131: the output area is split into two tabs — "Agent" (the
    // transcript/terminal/log) and "Files changed (N)" (the edited-files list, a
    // diff viewer and the per-session worktree actions). The files-tab header
    // carries the changed-file count.
    QTabWidget *m_agentDetailTabs = nullptr;
    int m_agentFilesTabIndex = -1;               // tab index of "Files changed"
    QTextBrowser *m_agentDiffView = nullptr;     // diff viewer in the files tab
    forkmesh::ui::DiffFileNavigator *m_agentDiffNav = nullptr; // sticky header + scroll<->select
    QLabel *m_agentFilesChangedSummary = nullptr; // "N files changed" line
    QLabel *m_agentCommitsHeading = nullptr;     // "Commits" heading over the list
    QListWidget *m_agentCommitsList = nullptr;   // this branch's commits, ahead of base
    // The session whose rich (icon + per-file +/-) diff list is currently on
    // screen. Once renderAgentDiff() has drawn it, refreshAgentFilesPanel() skips
    // the plain placeholder rebuild so the panel stops flashing between the two
    // views on every transcript turn (adhoc #260).
    int m_agentDiffRenderedSession = -1;
    // The HTML last handed to m_agentDiffView->setHtml() for that session. The
    // Files-changed diff re-renders on a 400ms timer for every transcript burst
    // while an agent streams; re-running QTextEdit::setHtml() when the rendered
    // diff is byte-identical just re-freezes the UI for seconds with no visible
    // change, so we skip the setHtml when this matches.
    QString m_agentDiffLastHtml;
    QPushButton *m_agentMergeButton = nullptr;   // worktree: merge into main
    QPushButton *m_agentMergeDeleteButton = nullptr; // merge + delete agent too
    QPushButton *m_agentUpdateButton = nullptr;  // worktree: update from main
    QPushButton *m_agentWtDeleteButton = nullptr; // worktree: delete worktree+branch
    QTimer *m_agentHourlyTimer = nullptr;        // refreshes spend + files hourly
    // adhoc #182: relay owner-encrypted session snapshots and drain encrypted
    // steering prompts. m_agentSyncDebounceTimer is a singleShot
    // re-armed after a status flip so a burst of updates coalesces into one push.
    QTimer *m_agentSyncPushTimer = nullptr;
    QTimer *m_agentSyncDebounceTimer = nullptr;
    // Last snapshot body POSTed per owner/name: the periodic push skips the
    // network write when the sessions payload hasn't changed (idle nodes used
    // to re-upload an identical snapshot every 30s).
    QHash<QString, QByteArray> m_lastAgentPushPayload;
    // Owner-E2EE policy setup is cached per relay/repository/key.  A policy
    // rejection clears the ready entry and the next safety-net tick performs
    // an idempotent key/policy registration before retrying.
    QSet<QString> m_agentE2EEReady;
    QSet<QString> m_agentE2EEInFlight;
    QSet<QString> m_orgAgentJobsInFlight;
    // local AgentSession id -> start-job transport needed to publish terminal
    // status through the same authenticated lease after the run finishes.
    QHash<int, QJsonObject> m_orgAgentBindings;
    // Each running CLI session has its own worktree, transport, and buffered
    // events, so output never leaks across providers or sessions.
    QHash<int, ClaudeStreamSession *> m_streamSessions;
    QHash<int, CodexAppServerSession *> m_codexStreams;
    QHash<int, QList<QJsonObject>> m_streamEvents;
    // Which stream session's transcript is currently built into m_agentTranscript,
    // and how many events were rendered. showAgentSession() is hit on every
    // reloadAgents()/detail refresh; without this guard each one tore down and
    // rebuilt the whole transcript widget tree, freezing the UI for seconds. Set
    // to -1 whenever the shared view is repurposed (external render / re-run).
    int m_renderedTranscriptSession = -1;
    int m_renderedTranscriptCount = -1;
    // How many of the selected session's oldest events are currently NOT
    // rendered as transcript rows (only folded into the token/cost totals via
    // accumulateStatsOnly) — i.e. still hidden behind the "Load earlier events"
    // notice. loadEarlierTranscriptEvents() shrinks this as batches are
    // revealed; renderTranscriptForSession() resets it on every full rebuild.
    int m_transcriptSkipped = 0;
    // Which *external* session's transcript is built into the shared view, so
    // showAgentSession can skip the full 400 KB tail re-read/rebuild when the
    // session is unchanged (reloadAgents re-shows the open session constantly).
    int m_renderedExternalSession = -1;
    // Which session's raw log is currently laid into m_agentLog, and its text,
    // so setAgentLogText() can skip the costly re-layout when nothing changed.
    int m_agentLogSession = -1;
    QString m_agentLogText;
    QHash<int, QString> m_streamRaw;
    QHash<int, qint64> m_sessionTokens; // live token total per session, for the list
    // Night-rider scanner lights: per-session sweep state keyed by sessionId (so
    // it survives full table rebuilds) and the timer that animates the active ones.
    QHash<int, AgentScannerState> m_scannerStates;
    QTimer *m_scannerTimer = nullptr;
    // Cached agents-list diff summaries keyed by sessionId (issue #170). Cold
    // values are computed on a worker; table painting only reads this map.
    QHash<int, AgentDiffStat> m_agentDiffStats;
    // Per-session fingerprint of the inputs the cached AgentDiffStat was computed
    // from (status/branch/merge/finish + the base tip). reloadAgents() flips
    // m_agentDiffRefreshPending; the next refreshAgentTable() drops only the
    // entries whose fingerprint changed (issue #289).
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
    void notifyAgentWaiting(int sessionId, bool needsPermission);
    void markAgentSessionRunning(int sessionId);
    QHash<int, QStringList> m_streamFiles;
    QHash<int, QString> m_streamWorktree;        // sessionId -> worktree path
    // In-flight worktree teardown threads (cleanupStreamWorktree), keyed by
    // sessionId. A resume of the same session must wait for its prior teardown to
    // finish before re-creating the worktree — the two run `git worktree` on the
    // same repo and would otherwise race, leaving `claude --resume` in the main
    // checkout with a red "0 turns" error on the first Add (adhoc #84).
    QHash<int, QThread *> m_worktreeTeardown;
    // User messages queued while a session has no live CLI (typed after the task
    // finished, or while it resumes after a ForkMesh restart). Flushed as
    // follow-up turns the next time the session's stream starts.
    QHash<int, QStringList> m_streamPending;
    // sessionWorkdir() cache for *reloaded* sessions (not in m_streamWorktree):
    // resolving their worktree shells `git worktree list`, which the Files-changed
    // panel drove on every transcript turn — a per-event subprocess that stalled
    // the GUI thread (adhoc #247). A session's branch->worktree binding is fixed
    // for its lifetime, so cache it; re-resolve only if the path was since removed.
    QHash<int, QString> m_sessionWorkdirCache;   // sessionId -> resolved worktree ("" = none)
    // A message typed into the composer for a session whose process isn't live:
    // it restarts the session and this is folded into the resumed run's prompt as
    // a steering instruction (the composer is always typeable — adhoc #177).
    QHash<int, QString> m_pendingSteerMessage;
    // Snapshot of each live stream session, captured at launch so transcript
    // events can be persisted to disk without depending on m_agentSessions
    // (which doesn't yet hold a freshly created ad-hoc session). Issue #41.
    QHash<int, AgentSession> m_streamSessionInfo;
    // customPrompt, when non-empty, is used as the agent's task verbatim (the
    // ad-hoc "start a new agent" composer, issue #273) instead of the prompt
    // derived from `issue`.
    void startCliTranscript(AgentSession &session, const Issue &issue,
                            const QString &repoPath,
                            const QString &customPrompt = QString());
    // Auto model mode (adhoc #91): resolve the "auto" sentinel to a concrete
    // model before launching the CLI. A routed choice recorded earlier in this
    // session's transcript is reused; otherwise a local heuristic pass runs,
    // then a triage ladder that asks Haiku whether it can handle the task and
    // escalates Haiku → Sonnet → Opus → Fable until a model is confident (a
    // rung may also name the right model directly). Each decision is explained
    // in the transcript via "_local_notice" events. `launch` receives the
    // chosen model id; `live` guards against the session being stopped while
    // the asynchronous triage runs.
    void resolveAutoClaudeModel(int sessionId, const QString &task,
                                const QString &workdir, ClaudeStreamSession *live,
                                std::function<void(const QString &)> launch);
    void runClaudeAutoTriageRung(int sessionId, int rung, bool errorsOnly,
                                 const QString &task, const QString &workdir,
                                 ClaudeStreamSession *live,
                                 std::function<void(const QString &)> launch);
    void applyTranscriptEvent(int sessionId, const QJsonObject &ev);
    // The Claude CLI stamps every stream-json event with the conversation's
    // session_id. The most recent one identifies the conversation to `--resume`
    // so a stopped agent is picked up with its full context (adhoc #182).
    QString lastClaudeSessionId(int sessionId) const;
    QString lastCodexThreadId(int sessionId) const;
    void renderTranscriptForSession(int sessionId);
    // Slice the next batch of earlier events off the selected session's
    // already-in-memory buffer (m_streamEvents; loadEvents() reads the whole
    // events.jsonl up front, so this never touches disk) and hand it to
    // m_agentTranscript->prependEarlierEvents() — driven by the view's
    // loadEarlierRequested() signal (button click or scroll-near-top).
    void loadEarlierTranscriptEvents();
    // Re-run the transcript search box's query against the freshly-rebuilt view
    // (adhoc #201), so highlights survive a session switch / re-render.
    void reapplyTranscriptSearch();
    // Refresh the edited-files panel. The in-memory tool-call files render
    // immediately; the working-tree `git diff` augmentation is coalesced and run
    // off the event loop (see scheduleAgentFilesDiff) so a streaming agent can't
    // freeze the UI by re-spawning `git diff` on every transcript event.
    void refreshAgentFilesPanel(int sessionId);
    void populateAgentFilesPanel(int sessionId, const QStringList &diffFiles);
    void scheduleAgentFilesDiff(int sessionId);
    // Everything the Files-changed tab needs from git, gathered by four parallel
    // *async* subprocesses (see scheduleAgentFilesDiff) so renderAgentDiff() runs
    // no git at all. It used to shell four synchronous reads per render; each one
    // pumped the event loop under GitKeepAlive mid-render, re-entering the render
    // and stacking multi-second stalls (the renderAgentDiff<-renderAgentDiff
    // frames all over ~/.forkmesh/diagnostics/stalls.log).
    struct AgentDiffProbe {
        QByteArray patch;        // git diff <base>
        bool patchOk = false;    // that read succeeded (else keep the old view)
        QSet<QString> uncommitted; // paths with working-tree changes / untracked
        QStringList commitLines; // "abc1234 subject" per commit ahead of base
        int behind = 0;          // commits the base branch has that we don't
        int pending = 0;         // async probes still in flight
    };
    // Render the session's full diff (vs its base ref) into the Files-changed tab's
    // viewer, rebuild the file list with per-file +/- counts and anchors, and stamp
    // the changed-file count onto the tab header. Pure UI: all git data arrives
    // pre-gathered in the probe.
    void renderAgentDiff(int sessionId, const AgentDiffProbe &probe);
    void updateAgentFilesTabState(int sessionId);
    QString sessionBaseRef(int sessionId);
    QString sessionBaseBranch(int sessionId);
    // The commit a session's diff is measured *from*: the merge-base of the base
    // branch and the worktree HEAD (so files that arrived by merging the base
    // branch *into* the agent branch don't count), falling back to the captured
    // base commit. `dir` is the worktree the git probes run in.
    QString sessionDiffBase(int sessionId, const QString &dir);
    QTimer *m_agentFilesDiffTimer = nullptr; // debounces the async working-tree diff
    void maybeCreatePullForStreamSession(int sessionId);
    // Land a finished agent session's change as a pull request (adhoc #25). On the
    // source of truth (we own the repo with a working tree) the PR is created and
    // committed locally. On a mirror node we can't write the owner's repo, so the
    // PR is signed and delivered to the owner's relay inbox instead — so a looper
    // running on a mirror still gets its work to the source of truth. `commits` is
    // the optional format-patch mbox the owner replays to preserve authorship;
    // pass empty when none is available (the owner synthesizes one from the patch).
    // Takes the session by value: creating the PR pumps the GUI event loop, and a
    // reloadAgents() during the pump would leave a reference dangling (adhoc #149).
    void landAgentPullForSession(AgentSession session, const QString &patch,
                                 const QString &commits);
    bool isStreamTranscriptSession(int sessionId) const;
    // Lazily restore a session's persisted transcript events from disk (issue
    // #41) so the rich transcript survives an app restart even after the live
    // stream object is gone. No-op for sessions already in memory or with no
    // persisted events. Synchronous — only for paths that need the result in
    // hand (the resume path); browsing clicks use the async variant below.
    void ensureStreamEventsLoaded(int sessionId);
    // Non-blocking variant for showAgentSession: returns true when the events
    // are already in memory; otherwise parses events.jsonl on a worker thread
    // and re-shows the session when it lands, so clicking between agents never
    // waits on disk. Known-empty sessions are remembered and not re-probed.
    bool ensureStreamEventsLoadedAsync(int sessionId);
    QSet<int> m_streamEventsLoading; // async loads in flight
    QSet<int> m_streamEventsAbsent;  // probed: nothing persisted on disk
    // Stop a live Claude Code stream session (the Stop button). stop() emits no
    // `finished`, so transition the session to Stopped and refresh here.
    // refreshUi=false skips the agent-table reload + transcript re-render for
    // callers that are about to delete the session and reload anyway (delete
    // paths), so the heavy refresh doesn't run twice and stall the UI.
    void stopStreamSession(int sessionId, bool refreshUi = true);
    // Drop every in-memory buffer/guard keyed by a session id when the session is
    // deleted. AgentStore::nextId() reuses the highest deleted id (it's maxId+1
    // over the surviving on-disk dirs), so a fresh session can inherit a just-
    // deleted one's number. Without this purge the new run picked up the old
    // session's cached events — ensureStreamEventsLoaded() saw them and treated
    // the brand-new prompt as a *resume* of the deleted conversation, showing its
    // transcript and --resume-ing its dead Claude session id so the new prompt
    // never actually ran.
    void purgeSessionState(int sessionId);
    // Working directory for a session: its worktree if it has one, else the repo.
    QString sessionWorkdir(int sessionId);
    // A session's dedicated worktree path ("" when its branch has no separate
    // worktree / is the main checkout), resolved without re-shelling `git worktree
    // list` on every click. Sessions launched this run know it from
    // m_streamWorktree; reloaded ones resolve once via git and cache it for the
    // session's lifetime (m_sessionWorkdirCache). See the definition for why the
    // click path leaned on this (adhoc #78).
    QString cachedSessionWorktree(int sessionId, const QString &repoLocal,
                                  const QString &branch);
    // Remove the isolated worktree a stream session ran in (if any) and prune the
    // registration, freeing its branch so the PR's branch can be checked out in
    // the main repo. `git worktree remove` keeps the branch ref itself, so the
    // pull request still resolves. No-op for sessions without a worktree.
    void cleanupStreamWorktree(int sessionId);
    // "YOLO" auto-merge (adhoc #12): land a finished session's branch in its
    // repo's default branch without a review step. No-op unless the session was
    // started with the quick-add YOLO toggle on and finished successfully.
    void maybeAutoMergeForSession(int sessionId);

    // ---- Organization tasks for prompted runs (adhoc #18) ------------------
    // A prompt typed here starts an agent locally; with the composer's "Task"
    // toggle on it also opens a task in the organization so the run is visible
    // beyond this desktop. The task carries the run's provenance — the bot that
    // launched it, the bot that reported it finished, and the model, permission
    // mode, and reasoning strength it used.
    //
    // Both calls are best-effort and fire-and-forget: the relay being down, the
    // account not being an organization member, or there being no signed-in
    // account at all never blocks or fails the agent run itself.
    QString agentBotLabel(const QString &provider) const;
    // Snapshot the composer's strength (reasoning effort) for a new session.
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
    // POST /api/tasks for a freshly created session and record the id it gets
    // back on the session. No-op unless session.orgTask is set.
    void openOrgTaskForSession(const AgentSession &session);
    // POST /api/tasks/<id>/complete once the run reaches a terminal status,
    // stamping the finishing bot. No-op without an org task, while the run is
    // still going, or once finishedByBot is already set.
    void completeOrgTaskForSession(int sessionId);
    // Apply an org-task field update to the live session and persist it. The
    // network callbacks run after event-loop turns that can rebuild
    // m_agentSessions, so they re-look-up by id rather than hold a pointer.
    void recordOrgTaskFields(int sessionId, const QString &taskId,
                             const QString &finishedByBot);

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
    void deleteExternalSession(int sessionId); // kill the CLI process, then unsurface
    int externalTempIdFor(const QString &uuid);
    bool isExternalSession(int sessionId) const { return sessionId <= kExternalIdBase; }
    bool externalIsLive(const QString &uuid) const; // still in the detected set
    void renderExternalTranscript(int sessionId, bool full);
    void stopExternalSession(int sessionId); // signal the external CLI process to quit
    void killExternalSessionPids(const QList<qint64> &pids); // SIGTERM, then SIGKILL fallback
    QSet<QString> m_externalStopped;         // uuids we've stopped — keep them idle
    QSet<QString> ownStreamCwds() const;    // dirs ForkMesh's own streams drive
    // Epoch-ms of the last live provider model-list fetch (see
    // refreshClaudeModelCombo). Throttles re-fetches so repeated hovers of the
    // top-bar usage chart don't hammer /v1/models.
    qint64 m_claudeModelsFetchedMs = 0;
    // The `data` array from the last successful /v1/models fetch, cached so a
    // model combo built after the fetch still gets the live line-up merged in
    // even while the re-fetch throttle is armed.
    QJsonArray m_liveClaudeModels;
    // Start an issue-less coding agent from the quick-add bar (issue #299) in
    // repoIndex's checkout with `task` as its prompt. Returns the new session id
    // (>0) or 0 if it could not start.
    int startAdHocAgentForRepo(int repoIndex, const QString &task,
                               const QString &provider, bool createPr,
                               const QString &model = QString());
    // Save a clipboard image to a stable temp file so a launched agent can read it
    // by path. Used by the quick-add image paste/attach path (issue #79).
    QString saveNewAgentPromptImage(const QImage &image);
    QPushButton *m_agentStopButton = nullptr;
    // Above the session list: stop every running agent and cancel the queue
    // (adhoc #433).
    QPushButton *m_agentStopAllButton = nullptr;
    QPushButton *m_agentDeleteButton = nullptr;
    QPushButton *m_agentDeleteAllButton = nullptr; // delete agent + worktree + branch
    // Above the session list: wipe every merged session's worktree, branch and
    // agent in one batch (adhoc #235).
    QPushButton *m_agentDeleteMergedButton = nullptr;
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
    // Spinning refresh (rebuild) button in the nav rail.
    QPushButton *m_refreshButton = nullptr;
    QTimer *m_refreshSpinTimer = nullptr;
    int m_refreshAngle = 0;
    // Commits-page branch indicator + checkout switcher.
    QPushButton *m_commitsBranchButton = nullptr;
    // Commits-page Fetch (refresh refs from the network) and Pull (fast-forward
    // the working tree) buttons, VS-Code style.
    QPushButton *m_commitsFetchButton = nullptr;
    QPushButton *m_commitsPullButton = nullptr;
    // Infinite-scroll paging for the commit list: how many commits are currently
    // loaded, whether older history remains, and a re-entrancy guard.
    int m_commitsLimit = 300;
    bool m_commitsHasMore = false;
    bool m_commitsLoadingMore = false;
    // While a commit search is active, the lazily-paged window is deepened to the
    // whole history so the filter spans every commit (incl. by hash); cleared back
    // to the paged window when the search box empties.
    bool m_commitsShowingAll = false;
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
    // A branches-panel git snapshot is being read on a worker thread. Reloads
    // arriving meanwhile set m_branchesPanelReloadQueued instead of starting a
    // second read, so a busy agent fleet can't pile up workers (adhoc #420).
    bool m_branchesPanelLoading = false;
    bool m_branchesPanelReloadQueued = false;
    // Bumped per load so a snapshot that lands after a newer one is dropped.
    int m_branchesPanelGen = 0;
    // Git dir the table's rows were built for, so switchToBranch only trusts the
    // rows on screen when they belong to the repo it's selecting into.
    QString m_branchesPanelDir;
    // Branch switchToBranch asked for that wasn't on screen yet: selected (or
    // reported as missing) once the pending rebuild lands.
    QString m_branchesPanelPendingSelect;
    // adhoc #15: the branch "Merge & delete all" just removed, the row it held and
    // the repo it belonged to. While these are set, renderBranchesPanel rebuilds
    // that row as an animated check instead of sliding the selection onto a
    // neighbouring branch and rendering a diff the user never asked for.
    QString m_branchMergedFlashBranch;
    QString m_branchMergedFlashDir;
    int m_branchMergedFlashRow = -1;
    // How long the check outlives the merge. It never moves the selection by
    // itself: expiring only means the next natural rebuild of the panel drops the
    // row, so a long-idle Branches tab eventually returns to normal.
    static constexpr int kBranchMergedFlashMs = 20000;
    // Re-entrancy guard for loadMirrorNodesPanel: its synchronous git reads pump
    // the event loop, so a queued roster/mirror callback could start a second
    // pass that appends its own rows on top of the half-built table — every node
    // listed twice (adhoc #375).
    bool m_mirrorNodesPanelLoading = false;
    bool m_agentMergeStateRefreshing = false; // refreshAgentMergeState worker in flight
    // Shared re-entrancy guard for the two heavy periodic refreshes
    // (refreshOpenRepoDetail + refreshRepositoryList): each runs synchronous git
    // reads under a GitKeepAlive that pumps the event loop, so a second one firing
    // during that pump would nest its git work and compound into a GUI stall
    // (adhoc #247). The second one defers instead.
    bool m_heavyRefreshInFlight = false;
    bool m_repoListRefreshQueued = false; // a refreshRepositoryList deferred past a pump
    // Signature of the inputs to the advertised per-repo mirror stats
    // (mirrorPath|lastSyncMs|localPath|worktrees-dir mtime, per non-preview repo).
    // Those ~9 git subprocesses per repo froze the GUI thread when re-run on every
    // onRequestServed (adhoc #83); skip the rebuild while the signature is
    // unchanged. Reset by attachBackend so a freshly attached backend is re-pushed.
    QString m_mirrorAdvertSig;
    QString m_mirrorAdvertInputSig;
    bool m_mirrorAdvertRefreshInFlight = false;
    qint64 m_mirrorAdvertCompletedAtMs = 0;
    void refreshMirrorAdverts();
    int m_repoOpenPending = -1;        // repo index queued by openRepoDetailDeferred
    // True while a user-driven repo load (a node switch or opening a repo) runs,
    // so nodeSwitchStep narrates progress for both, not just node switches.
    bool m_repoLoadActive = false;
    // True while the blue progress pill (showLoadStatus) owns the top-bar toast,
    // so the load can clear it on finish without stomping a real success/error.
    bool m_loadStatusShowing = false;
    // Per-step timing for the node-switch / repo-open narration: when a new step
    // starts, nodeSwitchStep logs how long the previous one took, so the system
    // log shows a real-time breakdown of where a slow switch spends its time.
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
    // Planned start/end dates row (issue #384): read-only value + an inline
    // editor of two QDateEdits, each toggled by a "no date" enable checkbox.
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
    // Twin of m_issueNewButton pinned to the top of the issue-list pane so "New
    // issue" is reachable without first selecting an issue (adhoc #11).
    QPushButton *m_issueListNewButton = nullptr;
    QPushButton *m_issueSyncButton = nullptr;
    QPushButton *m_issuePrioritizeButton = nullptr;
    // Agent picker sitting next to "Prioritize from README" so the run can use
    // any provider, not just the saved default. Seeded from the default agent.
    QComboBox *m_issuePrioritizeAgentCombo = nullptr;
    bool m_prioritizeInFlight = false;
    // Adhoc #139: "Analyze completeness" button next to "Prioritize from README".
    // Shares the agent picker above; guarded by its own in-flight flag.
    QPushButton *m_issueCompletenessButton = nullptr;
    bool m_completenessInFlight = false;
    // Issue looper (adhoc #92): runs the default agent on every open issue in
    // turn. m_looperSessionId is the session currently being watched; when it
    // finishes the looper starts the next open issue.
    bool m_looperActive = false;
    int m_looperSessionId = 0;
    QString m_looperProvider;
    int m_looperCurrentIssue = 0;
    QString m_looperCurrentTitle;
    // Compact looper toggle inline in the Issues heading row, next to "New
    // issue" (adhoc #130/#354): a switch + "looper #N" label that both shows
    // and controls the loop, with a neon-green segment circling its border
    // while on. Held as a QWidget* because the concrete LooperToggle type
    // lives in the .cpp; downcast there.
    // m_looperRepoSlug ("owner/name") records which repo the loop is bound to so
    // a restart resumes it on the same repo.
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
    // Issue #145: top tabs (Issue | Files changed) on the issue detail, mirroring
    // the agent detail. The Files changed tab appears only when the issue has a
    // linked branch (agent session) or pull request, and shows that diff.
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
    // Set just before a reload that closes the viewed issue: refreshIssueList
    // selects this issue (the next one in the list) once the table is rebuilt, so
    // closing an issue advances to the next instead of lingering on it (adhoc #249).
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

    // Projects tab widgets + state (issue #384).
    QTableWidget *m_projectTable = nullptr;
    QStackedWidget *m_projectViewStack = nullptr; // 0 list table, 1 Gantt
    QComboBox *m_projectStatusFilter = nullptr;   // Open | Closed | All
    // The Gantt chart (a ProjectGantt, kept as a QWidget* since that type is
    // only included by MainWindowProjects.cpp) and its scroll host.
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

    // Node profile panel widgets + the node it currently shows.
    QWidget *m_nodeProfilePanel = nullptr;
    QWidget *m_nodeProfileSectionHost = nullptr; // full-page home (section 10)
    QPushButton *m_profileCloseButton = nullptr; // hidden while shown as a tab
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
    // "USER ACCOUNT" section (self only): shows whether this node is linked to a
    // user and offers "Log in as a user" to attach it. m_nodeOwnerUser holds the
    // owning user's name (empty = unlinked), learned from account lookups.
    QWidget *m_profileAccountSection = nullptr;
    QLabel *m_profileAccountLabel = nullptr;  // "NODES (n)" / "USER ACCOUNT" header
    QLabel *m_profileAccountStatus = nullptr; // link-state text; hidden once linked
    QListWidget *m_profileUserNodesList = nullptr;
    QPushButton *m_profileLinkUserButton = nullptr;
    // "Link this node to your account" next to the node ID: browser-based
    // linking via a node-signed grant URL (adhoc #120).
    QPushButton *m_profileLinkBrowserButton = nullptr;
    int m_linkGrantPollsLeft = 0; // post-browser-link polling countdown
    // Owner at the moment the browser was opened: the grant overrides any
    // existing link, so "done" = the owner CHANGED, not just became non-empty.
    QString m_linkGrantBaselineOwner;
    QString m_nodeOwnerUser;
    // Nodes linked to this account's user (learned from account lookups): shown
    // in the "USER ACCOUNT" section so a user can see their whole fleet. When
    // this node is itself the user account it's the account's own nodes list;
    // when this node is a child, it's the owning user's nodes (siblings + self).
    QStringList m_profileLinkedNodes;
    bool m_profileIsUserAccount = false; // this account has login creds (a user)
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
    // Admin-only "Take ownership" request on another node's profile (adhoc #141).
    QPushButton *m_profileTakeOwnershipButton = nullptr;
    // Mirror reward settings: opt-in CTA shown under the username on your own
    // profile. Reports configuration, never guaranteed earnings.
    QPushButton *m_profileGetPaidButton = nullptr;
    // Self-only actions pinned to the top of the node profile panel.
    QWidget *m_profileSelfActions = nullptr;
    QPushButton *m_profileRebuildButton = nullptr;
    QPushButton *m_profileUpdateButton = nullptr;
    QString m_profileNodeId;
    QString m_profileNodeName;
    QString m_profileSolanaValue;

    QStringList m_channels;
    // World virtual-office channel conversations, merged into the sidebar
    // beside the mesh rooms and carried by OfficeChannelMirror (adhoc #412).
    QStringList m_officeConversations;
    // Invite-only rooms this node owns or was invited to. Badged in the sidebar
    // and persisted so they reappear after a reconnect (the backend clears its
    // channel set each session). See promptAddPrivateChannel / inviteToChannel.
    QSet<QString> m_privateChannels;
    QList<RepositoryRecord> m_repositories;
    // Coalesced catalog writes. Many repo events can ask to publish the same
    // repo in the same second; keep one timer/request per repo and one real POST
    // cadence per account so the relay's catalog write cooldown is respected.
    QHash<QString, QTimer *> m_catalogPublishTimers; // owner/name -> timer
    QHash<QString, qint64> m_catalogPublishOwnerLastAttemptMs; // owner -> ms
    QSet<QString> m_catalogPublishInFlight;      // owner/name
    QSet<QString> m_catalogPublishQueued;        // owner/name dirtied mid-flight
    QSet<QString> m_catalogPublishDialogQueued;  // owner/name wants UI feedback
    // Consecutive retryable (429/5xx) publish failures per repo, so the retry
    // delay can escalate exponentially instead of hammering an overloaded relay
    // every 60s forever. Reset to 0 on a successful publish.
    QHash<QString, int> m_catalogPublishConsecutiveFailures; // owner/name -> n
    // Fingerprint (sha256 of the record minus volatile fields) of the last
    // successfully published catalog record per repo, and when it was sent.
    // publishRepositoryNow() skips the network write when nothing the catalog
    // shows has changed — roster flicker used to republish an identical record
    // every ~30s, hammering the relay's D1 for no reader-visible difference.
    QHash<QString, QByteArray> m_catalogPublishedFingerprint; // owner/name -> hash
    QHash<QString, qint64> m_catalogPublishedFingerprintAtMs; // owner/name -> ms
    // A private catalog write is permitted only after this process has
    // idempotently registered its public hybrid key and repository privacy
    // policy with an authenticated owner session.
    QSet<QString> m_privateControlReady;
    QSet<QString> m_privateControlInFlight;
    // Public-repository contribution snapshots are expensive Git reads. This
    // bounded state coalesces one worker per immutable repo state, remembers a
    // requested publish dialog, and applies separate success/error TTLs.
    RepoContributionPublicationCache m_contributionPublicationCache;
    QHash<QString, QString> m_catalogContributionScanKey;
    QHash<QString, QString> m_catalogContributionSnapshotKey;
    QHash<QString, QString> m_catalogContributionDependencyFingerprint;
    QHash<QString, QString> m_catalogContributionPreparedScanKey;
    QHash<QString, QString> m_catalogContributionPreparedSnapshotKey;
    QList<RepoHost *> m_repoHosts;
    QSet<QString> m_repoHostKeys; // empty compatibility state; sockets retired
    NodeEventSocket *m_nodeEventSocket = nullptr; // relay push -> /api/sync
    // Keeps authorized, owner-only private repository materializations alive
    // only for this app process. They are removed recursively on destruction
    // and their paths are never written to settings.
    QHash<QString, std::shared_ptr<PrivateMirrorMaterialization>>
        m_privateMirrorMaterializations; // opaque replica id -> temp repo
    QHash<QString, std::shared_ptr<PublicMirrorMaterialization>>
        m_publicMirrorMaterializations; // age archive id -> temp repo
    QList<MemberInfo> m_homeRoster;
    QHash<QString, MemberInfo> m_chatDirectoryUsers; // lowercased user -> profile
    bool m_chatDirectoryFetchInFlight = false;
    qint64 m_chatDirectoryFetchedMs = 0;
    // Repeating directory poll so brand-new signups appear in the users column
    // without a reconnect (adhoc #209); the endpoint is edge-cached server-side.
    QTimer *m_chatDirectoryTimer = nullptr;
    bool m_chatDirectoryLoaded = false; // first fill done (may legitimately be empty)
    QSet<QString> m_removedPeerIds;  // IDs explicitly removed via removeChatMember
    // When each roster peer was last seen live, so guests and World visitors
    // (throwaway browser sessions) are forgotten after ChatVisitorPresence::
    // kVisitorIdleMs of silence instead of being retained as dead offline rows
    // the way a real node or account is (adhoc #404). Pruned to the current
    // roster on every update, so it can't outgrow it.
    QHash<QString, qint64> m_peerLastSeenMs;
    // True once this node has posted (or confirmed it already posted) its one-time
    // welcome greeting this run, so the per-roster check stays cheap (issue #192).
    bool m_welcomeAnnounced = false;
    // Catalog-backed mirror list (issue #223): the worker's /mirrors payload for
    // the repo group currently shown in the mirror-nodes panel, merged in so a
    // mirror that isn't live in the chat room is still listed for the owner.
    QString m_catalogMirrorsSource;        // "owner/name" the cache holds
    QJsonArray m_catalogMirrorsCache;      // last /mirrors payload's "mirrors"
    QString m_catalogMirrorsFetchSource;   // source the last fetch was kicked for
    qint64 m_catalogMirrorsFetchedMs = 0;  // throttle: last fetch kick time
    // Per-artifact release download counts for the repo currently shown in the
    // Releases panel (sha256 -> times downloaded), from the worker's
    // /releases/downloads endpoint.
    QString m_releaseDownloadsSource;      // "owner/name" the cache holds
    QHash<QString, int> m_releaseDownloadsCache; // sha256 -> download count
    QString m_releaseDownloadsFetchSource; // source the last fetch was kicked for
    qint64 m_releaseDownloadsFetchedMs = 0; // throttle: last fetch kick time
    // "owner/name" -> { times served through the mainnode, clones }.
    QHash<QString, QPair<int, int>> m_repoStats;
    QSet<int> m_syncingRepos;
    QSet<int> m_pushingRepos;
    // "owner/name" repos with an SSH mirror push in flight (pushToSshMirrorRemotes),
    // so overlapping sync completions can't stack pushes to the same gateway.
    QSet<QString> m_sshMirrorPushing;
    // A sync can finish while that asynchronous push is still transferring.
    // Remember it instead of dropping it: once every gateway attempt finishes,
    // push the newest served snapshot again so moving refs converge exactly.
    QSet<QString> m_sshMirrorPushPending;
    // "owner/name" of repos whose @mention scan is running on a worker thread, so
    // a second sync/inbox drain doesn't kick a duplicate scan (and double-notify)
    // while the first is still loading issues/PRs off the UI thread.
    QSet<QString> m_mentionScanInFlight;
    QString m_currentConversation;
    // Per-conversation message log and the live rows for the open conversation.
    QHash<QString, QList<ChatMessage>> m_history;
    QSet<QString> m_historyIds; // message ids already in m_history (dedup)
    QTimer *m_chatSaveTimer = nullptr;
    QTimer *m_chatExpiryTimer = nullptr; // periodic pruneExpiredChatHistory()
    QHash<QString, MessageRow *> m_visibleRows; // messageId -> row (current conv)
    // messageId -> emoji -> reactor display names.
    QHash<QString, QMap<QString, QStringList>> m_reactions;
    QHash<QString, QPixmap> m_avatars;          // senderId -> avatar
    QHash<QString, QString> m_dmNames;          // peerId -> display name
    QHash<QString, QHash<QString, QString>> m_typing; // conversation -> peerId -> name
    QStringList m_networkLog;
    QSet<QString> m_logFilterCategories;        // badges that currently have a chip
    int m_networkLogDiskLines = 0;              // lines written to the on-disk log
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
    QString m_lastChatDisplayName;
    QByteArray m_lastChatAvatar;
    QString m_typingConversation;
    QTimer *m_typingStopTimer;
    QTimer *m_homeStatsTimer = nullptr;
    // Polls the open repo's uncommitted-file count into the activity rail badge
    // (see refreshRepoChangeBadge).
    QTimer *m_repoChangeBadgeTimer = nullptr;
    qint64 m_connectedAtMs = 0;
    qint64 m_totalConnectionMs = 0;
    // Until this moment, "node connected" alerts are suppressed: the roster
    // arrives incrementally right after we connect, so without a grace window
    // every node that was already online would pop a notification on startup.
    qint64 m_nodeAlertGraceUntilMs = 0;

    // Registered account/node identity for this session.
    bool m_accountAuthenticated = false;
    QString m_accountName;
    // Session token minted by /api/accounts/login, used to authenticate
    // profile writes (e.g. persisting the chosen avatar to the account record
    // so the web dashboard shows the same picture the desktop app does).
    QString m_accountSessionToken;
    bool m_accountSolanaVerified = false;
    bool m_accountDesktopCapable = false;
    // Compatibility tier label: account signup/activation is free and separate
    // from any voluntary reward-pool contribution.
    QString m_accountTier = QStringLiteral("free");
    QTimer *m_heartbeatTimer = nullptr;
    bool m_isAdmin = false;
    QTimer *m_adminPollTimer = nullptr;
    QStringList m_seenPendingUsers;
    // Shared room-chat key fetched from the relay (server-derived from DATA_KEY),
    // replacing the old public app-wide constant. Empty until fetched.
    QString m_roomPassphrase;
    // Last website-claim confirmation code already shown (adhoc #53), so the
    // per-minute heartbeat doesn't reopen the popup for the same claim.
    QString m_lastClaimCodeShown;
    // Admin claiming this node last shown for an ownership-transfer prompt
    // (adhoc #141), so the per-minute heartbeat doesn't reopen the dialog
    // while the request is still pending a decision.
    QString m_lastOwnershipTransferAdminShown;
    // Accepted peer mirror requests (issue #385) delivered on the heartbeat:
    // ids we've already started mirroring this session (so we don't re-clone),
    // and ids still awaiting acknowledgement to the relay on the next beat.
    QSet<QString> m_handledMirrorRequests;
    QStringList m_pendingMirrorRequestAcks;
    // User avatar in the top-right account cluster (opens the signed-in/linked
    // user account); the connection dot lives on it too now that the separate
    // node avatar button is gone.
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
