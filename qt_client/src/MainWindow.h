#pragma once

#include "ChatBackend.h"
#include "DiscussionInboxBackoff.h"
#include "NetworkBackoff.h"
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
#include "RepoSecurity.h"

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
#include <QSet>
#include <QTextCursor>
#include <QThread>
#include <QUrl>

#include <functional>
#include <limits>

class MessageRow;
class MarkdownEditor;
class TerminalWidget;
class ClaudeIdeBridge;
class ClaudeStreamSession;
class StallWatchdog;
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
class QPlainTextEdit;
class QImage;
class QProgressBar;
class QPropertyAnimation;
class QPushButton;
class QScrollArea;
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
class QHBoxLayout;
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
    bool publishToNetwork = false;
    // Private repo: hidden from the public catalog and readable (browse/clone
    // through the relay) only with an owner-key-signed forkmesh-view-v1 token, so
    // only this node's key holder can reach it. Still published/hosted otherwise.
    bool isPrivate = false;
    // Run .forkmesh/ workflows when a fork pushes to this repo's bare mirror.
    // Enabled by default; can be turned off per repo on the Actions tab. Pushed
    // workflow changes still require explicit approval before they run.
    bool actionsEnabled = true;
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
    void testPublishRepository(int index) { publishRepository(index, false); }
    void testStartRepoHosts() { startRepoHosts(); }
    void testStopRepoHosts() { stopRepoHosts(); }
    void testShowPublishBar(bool on);
    int testRepoTabContentTop(); // y of the tab content within the window
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
    // Provider id (openai/claude-api/claude-code) currently selected in each
    // agent-assignment picker, plus a way to drive the Settings "Default agent"
    // combo as a user would, so tests can assert the default seeds/updates them.
    QString testQuickAddAgentProvider() const;
    QString testIssueAgentProvider() const;
    void testSetDefaultAgentProvider(const QString &provider);
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
    // Focus the worktrees table and deliver an Up/Down key press, returning the
    // branch that ends up selected so a test can prove keyboard arrow keys move
    // the selection (and drive the detail pane) like a click does.
    QString testArrowOnWorktrees(bool down);
    // Open a repo-detail tab exactly as a user clicking its nav button would, so a
    // test can prove the tab switch hands keyboard focus to that tab's list.
    int testWorktreesTabIndex() const { return m_worktreesTabIndex; }
    void testClickRepoDetailTab(int id);
    // Activation-independent: is the worktrees table the focus widget of its
    // window? (hasFocus() also requires the window to be active, which an
    // offscreen test window isn't.)
    bool testWorktreesTableHasKeyboardFocus() const;
    // Same coverage for the Releases and Mirror-nodes tabs, whose list tables
    // also grab keyboard focus on open so Up/Down arrows (and Enter to open)
    // work without a click first (adhoc #183).
    int testReleasesTabIndex() const { return m_releasesTabIndex; }
    int testMirrorNodesTabIndex() const { return m_mirrorNodesTabIndex; }
    bool testReleasesTableHasKeyboardFocus() const;
    bool testMirrorNodesTableHasKeyboardFocus() const;
    // The Mirror nodes rows as "name-cell-text|node-id", so a test can prove a
    // node that re-registered under a new key shows exactly one row (adhoc #46).
    Q_INVOKABLE QStringList testMirrorNodeRows() const;
    // Rebuild the Branches panel, then read back the Worktree column (column 3)
    // for `branch`, so a test can prove the branches list surfaces the worktree a
    // branch is checked out in (issue #172).
    void testReloadBranchesPanel() { loadBranchesPanel(); }
    QString testBranchWorktreePath(const QString &branch) const;
    // Inject an agent session so a test can prove the branches list surfaces the
    // issue/agent a branch is attached to (adhoc #191).
    void testAddAgentSession(const AgentSession &session)
    {
        m_agentSessions.append(session);
    }
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
    // is key-bound the relay accepts this node's catalog writes and host tokens,
    // so the mirror finally registers in the database and shows on the repo page.
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
    void showAdminVerifyDialog();
    bool adminVerifyEmail(const QString &target);
    void verifyWallet();
    QUrl accountsApiUrl(const QString &leaf) const;
    QJsonObject postAccountSync(const QString &leaf, const QJsonObject &body,
                                int *status);
    QJsonObject getAccountSync(const QString &leaf, int *status);
    QString accountOwner() const; // the registered account name (repo namespace)
    void applyAccountEmailVerified(const QString &accountName, bool verified);
    bool accountEmailVerified(const QString &accountName) const;
    QString settingsAccountName() const;
    void refreshSettingsEmailVerifiedBadge();
    // The owner a repo is published/browsed under on the website. Must match the
    // owner the live host tunnel registers with, or the website can't find the
    // host. Mirrors the fallback used when publishing.
    QString catalogOwner(const RepositoryRecord &repo) const;
    void runQuickUpdate();
    // Pull a fresh copy from the install URL (the live hosted mirror), then
    // rebuild and relaunch. Installs into the invoking non-root user's home even
    // when ForkMesh itself is running as root.
    void updateRebuildRestart();
    // Settings → "Automatically update ForkMesh": periodic, quiet check for a new
    // tagged release on the update remote (ordinary commits on main don't count).
    // Only ever triggers updateRebuildRestart() when one is actually found, and
    // never while an agent is running.
    void maybeAutoUpdate();
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
    // (Re)apply the always-on footer log line's inline stylesheet for the active
    // theme, tinting the text by the current line's tone. Called on theme switch.
    void styleFooterUpdateLog();
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
    void probeRelayLatency();      // measure round-trip to the active relay (radar)
    void initRelayReachabilityWatch(); // OS reachability → instant radar flips
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
    // Git-derived inputs to the "Sync" button. Computing them shells several
    // rev-list/rev-parse subprocesses on the working copy + served mirror, so it runs
    // off the GUI thread (computeRepoPushState) and the result is painted back on the
    // main thread (applyRepoPushButtonState) — see updateRepoPushButton.
    struct RepoPushState {
        bool valid = false;       // repo has a local working tree (.git)
        bool relay = false;       // publishes to a served mirror / ForkMesh relay
        int unpublished = 0;      // commits not yet folded into the served mirror
        int behind = 0;           // incoming commits to pull (drives the ⇅ arrow)
        bool hasUpstream = false; // tracks a real upstream remote (non-relay)
        int ahead = 0;            // commits ahead of that upstream
        QString upstreamRef;      // the @{upstream} name (for the non-relay tooltip)
        // Rich-tooltip detail for the pending sync (computed off the GUI thread):
        // the commits about to go out and the aggregate line-change diffstat, plus a
        // human-readable name for where they're headed.
        struct PendingCommit {
            QString hash;    // short hash
            QString subject; // first line of the commit message
            int added = 0;   // lines added by this commit
            int removed = 0; // lines removed by this commit
        };
        QString target;               // sync destination ("origin/main", served mirror)
        QList<PendingCommit> commits; // pending commits, newest first (capped)
        int extraCommits = 0;         // pending commits beyond the capped list
        int added = 0;                // total lines added across the whole range
        int removed = 0;              // total lines removed across the whole range
    };
    // Thread-safe (reads only the passed-in record + free git helpers + QSettings);
    // never touches m_repositories or a widget, so it is safe to run on a worker.
    RepoPushState computeRepoPushState(const RepositoryRecord &repo) const;
    // Fill the rich-tooltip commit list + line diffstat for the pending range that
    // ends at HEAD and starts just after `base` (empty base = from the root commit).
    // Static: shells git on the passed-in path only, so it runs on the worker too.
    static void collectPushDetail(const QString &localPath, const QString &base,
                                  RepoPushState *st);
    void applyRepoPushButtonState(int index, const RepoPushState &state);
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
    // Show/hide the "clones are being rejected" warning on the owner's node when the
    // relay's pinned stateHash no longer matches the refs this node serves. The
    // warning surfaces as the top-bar notification toast (with Reset / Why links),
    // not an in-page banner.
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
    // Take this node online / offline from the top-bar toggle. Offline stops the
    // reward heartbeat and live repo serving (so the node stops collecting
    // rewards) while leaving the user in the app; online resumes both.
    void setNodeOffline(bool offline);
    // Refresh the top-bar reward toggle, status line and "online Xh" uptime.
    void updateNodeOnlineControls();
    // Bottom quick-add issue bar (the network log now lives in its own section).
    QWidget *buildNetworkLogDock();
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
    // Opt-in crash/stall telemetry upload (issue #354). No-op unless the user
    // enabled kUploadTelemetrySetting. On startup, reads the not-yet-uploaded
    // tail of ~/.forkmesh/diagnostics/crashes.log and stalls.log, scrubs repo
    // names / filesystem paths out, and POSTs a size-capped, anonymized payload
    // (app version, OS, node hash) to /api/telemetry. Best-effort and silent.
    void maybeUploadDiagnostics();
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
    // Node profile: full-page centered section (index 9 in m_sectionStack).
    QWidget *buildNodeProfileSection();
    QWidget *buildNodeProfilePanel(); // builds inner scroll area; called by buildNodeProfileSection
    void showNodeProfile(const QString &nodeId, const QString &nodeName);
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
    QWidget *buildNotificationsSection();
    // Network leaderboards (issue #11): fetched from /api/network/leaderboards.
    QWidget *buildLeaderboardsSection();
    void refreshLeaderboards();
    void populateLeaderboards(const QJsonObject &data);

    // Hosts (adhoc #263): SSH into a remote machine and run the ForkMesh
    // installer over a plain shell (sshpass + ssh), streaming the live session
    // output. Once the install finishes the new node joins the network and shows
    // up in the per-repo Mirror nodes list on its own.
    QWidget *buildHostsSection();
    // forceUploadBinary bypasses the "Upload the release from this app"
    // checkbox (used by the per-row / install-all-from-binary buttons, which
    // are always direct-upload regardless of the form's checkbox state).
    // onFinished, if given, is called once with whether the install succeeded
    // — used to chain installs when running against every saved host.
    // reinstall passes FORKMESH_REINSTALL=1 to the hosted installer so it wipes
    // the host's existing install + data before installing fresh (adhoc #258).
    void runHostInstall(bool forceUploadBinary = false,
                        std::function<void(bool)> onFinished = {},
                        bool reinstall = false);
    // SSH into a saved host and run the hosted uninstaller (uninstall.sh),
    // which removes the ForkMesh binary, launcher and ALL of that host's data.
    void runHostUninstall();
    // Direct-upload install (adhoc #257) against every saved host, one at a
    // time: loads each row into the form and runs runHostInstall(true, ...),
    // chaining to the next host once the previous one finishes.
    void runHostInstallAllFromBinary();
    void installNextHostFromBinary(QList<int> remainingRows);
    // Uninstall + reinstall from binary (adhoc #258) against every saved host,
    // one at a time: each host wipes its existing install + data and then
    // installs a fresh copy from this app's binary, re-minting a link code so it
    // re-attaches to this account.
    void runHostReinstallAllFromBinary();
    void reinstallNextHostFromBinary(QList<int> remainingRows);
    void appendHostInstallLog(const QString &text);
    // Save the host's server info (name/IP/user/password) from the form without running
    // the installer, so the details are remembered up front and the installer
    // can be run against the saved host later.
    void addHostFromForm();
    void rememberHost(const QString &name, const QString &ip, const QString &user,
                      const QString &pass, const QString &status = QStringLiteral("installed"));
    void refreshHostsTable();
    // Reload a saved host's server info (name/IP/user/password) from the table back into
    // the install form so the installer can be re-run against it.
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

    // Repo detail view (files + issues tabs), opened by clicking a repository.
    QWidget *buildRepoDetailSection();
    QWidget *buildRepoFilesPanel();
    QWidget *buildRepoOverviewPage();
    QWidget *buildRepoEditorPage();
    QWidget *buildRepoCommitsTab();
    void showCommit(const QString &hash); // open the commit diff detail view
    // showCommit's synchronous tail: pure widget population once the async
    // `show -s` metadata and `diff -M` patch are both in hand.
    void renderCommitDetail(const QString &dir, const QString &hash,
                            const QStringList &metaFields,
                            const QByteArray &patchRaw);
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
                             const QString &authorId = QString());
    QWidget *buildAboutSidebar();
    QWidget *buildRepoSecurityTab();
    QWidget *buildRepoQualityTab();
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
    void refreshPullList();
    void showPull(int number);
    // Files-changed authorship filter: show only agent- or human-authored files
    // in the current PR, driven by m_pullFileAuthorFilter (issue #365).
    void applyPullFileAuthorFilter();
    void renderPullReviewSummary(const PullRequest &pr);
    // Render every changed file of the current PR into one continuously
    // scrollable diff view (issue #250), so the reviewer can scroll the whole PR
    // and the file list / Prev-Next jump between files.
    void renderPullDiff();
    // Scroll the all-files diff so the given file's section is at the top.
    void scrollPullDiffToFile(const QString &filePath);
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
    // change can re-render it in place without re-running its renderer.
    void setDiffHtml(QTextEdit *view, const QString &html);
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
    void renderPullCommits(const PullRequest &pr);  // commits that make up the PR
    void renderPullChecks(const PullRequest &pr);   // action runs for the PR's commits
    void renderPullChecksSummary(const PullRequest &pr); // inline conversation card
    void showPullCheckLog(int runId);               // load a run's log into the panel
    QStringList pullCommitShas(const PullRequest &pr) const; // base..head SHAs
    QList<int> runIdsForPull(const PullRequest &pr) const;   // matching action runs
    void runChecksForCurrentPull();                 // enqueue workflows at PR head
    // Check out the PR's head into a throwaway worktree, build the ForkMesh app
    // from it, and launch the freshly built binary as an isolated preview node so
    // the reviewer can try the change running before merging (issue #214).
    void buildAndPreviewCurrentPull();
    void updatePullSubTabCounts(const PullRequest &pr);
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
    // After a merge, mint the escrow deposit address and show the funding QR for
    // any (pledged-but-unpaid) bounty on the issues this PR closes. Bounties are
    // added to issues without paying up front; merge is when they get funded.
    void fundBountiesForMergedPull(const PullRequest &pr);
    // Issue #347: when the per-PR bounty setting is on, reward every merged pull
    // request's author with the configured fixed bounty — either by showing a
    // funding QR (perPr mode) or auto-paying from the inbuilt wallet (wallet
    // mode). Independent of whether the PR closes a bountied issue.
    void autoBountyForMergedPull(const PullRequest &pr);
    // Poll a bounty escrow after merge; once funded the worker splits it to the
    // author + treasury, and this records the paid state on the issue. kind ""
    // is an issue bounty; "pr" is a per-pull-request bounty (issue #347).
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
    // Files-changed + branch ahead/behind summary for a session's Diff cell
    // (issue #170), computed against the given git dir / base branch and memoised
    // in m_agentDiffStats. Both git args are hoisted by the caller so the per-row
    // loop doesn't re-resolve them.
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
    // also picks up merges synced from peers or done by hand);
    // agentSessionLandedInBase() answers the question for one session.
    bool markAgentSessionsMerged(int prNumber, const QString &branch);
    void refreshAgentMergeState();
    // dir = the repo's git dir, base = its default branch — resolved once by the
    // caller and passed in so a whole-list refresh doesn't re-shell `git branch`
    // (etc.) per session.
    bool agentSessionLandedInBase(const AgentSession &session, const QString &dir,
                                  const QString &base) const;
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
    // Funnel for every looper state change: refresh the floating toggle above
    // the Issues tab and persist the running state so the loop resumes after a
    // restart (adhoc #130, #125).
    void updateIssueLooperButton();
    void positionLooperToggle();
    // Anchor the live mirror-activity dot strip just above the Mirror nodes tab
    // (adhoc #197), mirroring positionLooperToggle over Issues.
    void positionMirrorActivityStrip();
    // Anchor the current-release pill just above the Releases tab (adhoc #69),
    // mirroring positionMirrorActivityStrip over Mirror nodes.
    void positionReleaseStrip();
    void persistLooperState();
    void maybeRestoreIssueLooper();
    void continueSelectedAgentSession();
    // Same as continueSelectedAgentSession, but for an arbitrary session id —
    // used to resume a session steered from the website (adhoc #182) without
    // disturbing whatever session is currently selected in the UI.
    void continueAgentSession(int sessionId);
    // Ask the given session's agent to merge base and resolve conflicts, then
    // resume it — the action behind the "Fix conflicts with agent" button.
    // Shared by that button (selected session) and the auto-fix setting below
    // (any idle session, not necessarily the selected one).
    void fixAgentConflictsWithAgent(int sessionId);
    // If kAutoFixAgentConflictsSetting is on and `stat` says session's branch
    // conflicts with base, automatically triggers fixAgentConflictsWithAgent().
    // De-duped per session so a conflict that persists across a failed retry
    // isn't retried forever; the guard clears once the conflict is gone.
    void maybeAutoFixAgentConflict(const AgentSession &session,
                                   const AgentDiffStat &stat);
    // Steer m_selectedAgentSessionId with a follow-up message. Shared by the
    // agent detail composer's Send button and the footer quick-add's up-arrow
    // ("send to the visible agent") button.
    void sendPromptToSelectedAgent(const QString &prompt);
    // Same as sendPromptToSelectedAgent, but for an arbitrary session id
    // (adhoc #182: the website can steer any of this node's agent sessions).
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
    bool deleteStoredAgentSession(int sessionId);
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
    // OAuth usage endpoint. No background timer drives this (adhoc #76) — it
    // only runs right after a prompt is sent (bumpClaudeCodeUsage) or when the
    // user hovers the top-bar chart to check the current figures.
    void refreshClaudeCodeUsage();
    // Ask the provider which models this account can drive right now
    // (GET /v1/models) and merge them into the composer's per-session model
    // picker, so the dropdown reflects the live line-up (new releases appear
    // without an app update). Best-effort: on any failure the static defaults
    // from populateClaudeModelCombo() stand.
    void refreshClaudeModelCombo();
    // Refresh usage now and again a few seconds later. Use this the moment a new
    // agent starts or a prompt is sent: at that instant no tokens have been
    // consumed yet, so an immediate refresh still shows the pre-start figure —
    // the delayed follow-up catches the first turn's usage.
    void bumpClaudeCodeUsage();
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
    void processAgentQueue();
    // Returns the pooled runner currently executing sessionId, or nullptr.
    AgentRunner *runnerForSession(int sessionId) const;
    // Returns an idle pooled runner, creating (and wiring) a new one if needed.
    AgentRunner *acquireAgentRunner();
    // True while any pooled runner is executing a session.
    bool anyAgentRunning() const;
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
    void updateIssueAgentUi(const Issue &issue);
    // Issue #145: populate the issue detail's "Files changed" tab from a linked
    // pull request's patch or a linked agent session's branch diff, and show or
    // hide the tab depending on whether such a source exists.
    void refreshIssueFilesPanel(const Issue &issue);
    void renderIssueDiff(int issueNumber, const QByteArray &patch,
                         const QString &dir, const QString &base);
    // adhoc #151: stamp the issue list's "Files" column for issues whose work
    // lives in a linked agent worktree branch or pull request. Mirrors
    // refreshIssueFilesPanel's source preference. The worktree count is computed
    // async (git diff --name-only against the session base) and cached per issue,
    // so a re-sort/rebuild can show it immediately; applyIssueFilesCount relocates
    // the row by issue number when the async result lands.
    void populateIssueFilesCell(int row, const Issue &issue);
    void setIssueFilesCell(QTableWidgetItem *item, int count,
                           const QString &source);
    void applyIssueFilesCount(int issueNumber, int count, const QString &source);
    QHash<int, int> m_issueFilesChangedCounts; // issue number -> files changed
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
    // Start a new coding agent to fix the selected (failed) run, on its own
    // branch/PR like any other ad-hoc agent run (adhoc #114).
    void fixSelectedRunWithAgent(const QString &provider, const QString &model);
    // Delete every run currently shown in the Runs list (its meta + log on
    // disk); skips any run that's still in flight. Prompts for confirmation.
    void clearActionRuns();
    void initActions();                  // store/runner/watcher, load history, hooks
    void ensurePushHook(const RepositoryRecord &repo) const;
    void removePushHook(const RepositoryRecord &repo) const;
    void installAllPushHooks() const;
    void scanActionSpool();              // read *.push events, enqueue runs
    void enqueuePushEvent(const QString &owner, const QString &name,
                          const QString &commit, const QString &ref);
    void processActionQueue();
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
    int repoIndexFor(const QString &owner, const QString &name) const;
    // Settings: global variables/secrets editor.
    void reloadVariablesTable();
    void addOrEditVariable(bool editSelected);
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
    // The commit history lives inside the Code overview (no top-bar tab): the
    // commit strip's "N Commits" button toggles the area under the latest-commit
    // bar between the file browser and the commits panel.
    void showOverviewCommits();
    void showOverviewFiles();
    void showOverviewBranches();
    void loadRepoFileTree();
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
    QString repoDefaultBranch(const QStringList &branches) const;
    QWidget *buildBranchesTab();
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
    // Stream the next batch of queued per-file diff blocks into the branch diff
    // view off the event loop (progressive render of a large commit/branch diff,
    // adhoc #51). `gen` is the render generation it belongs to: a stale batch from
    // a superseded scope selection bails. Reschedules itself until drained.
    void appendBranchDiffBlocks(int gen);
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
    // The branch listed next to `branch` in the Branches table (the row below it,
    // else the row above), used to pick the post-delete selection (adhoc #256).
    QString neighbourBranchInList(const QString &branch) const;
    // Delete every branch that is fully merged into the default branch (0 behind
    // and 0 ahead of it), skipping the default and the checked-out branch.
    void deleteMergedBranches();
    void deleteSelectedBranches();
    QWidget *buildReleasesTab();
    void loadReleasesPanel();
    void promptNewRelease();
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
    void downloadNextReleaseBlob(const QString &mirrorPath,
                                 QMap<QString, QString> pending);
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
    void positionRepoPushButton(); // float "Sync" just above the Code tab
    void updateActionStrip();    // build/show/hide the bars for in-flight runs
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
    // Busy feedback for the commits-page Refresh button: rotates its icon while a
    // reload runs (kept visible briefly after, since the reload is near-instant).
    void startCommitsRefreshSpin();
    void stopCommitsRefreshSpin();
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
    // The issue listed just after `number` in the table's current visual order
    // (falls back to the one before it when closing the last row); -1 if none.
    int nextVisibleIssueAfter(int number) const;
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
    QUrl issuesApiUrl(const RepositoryRecord &repo) const;
    QUrl bountyApiUrl(const RepositoryRecord &repo) const;
    // Website agent view (adhoc #182): the repo owner watches this node's
    // Claude Code agent sessions and can steer a running one from the browser.
    QUrl agentsApiUrl(const RepositoryRecord &repo) const;
    // Push a full-replace snapshot of every owned/hosted repo's local agent
    // sessions to the website. Called on a periodic timer and, debounced, right
    // after a session's status changes.
    void pushAgentSessionsSnapshot();
    void pushAgentSessionsForRepo(RepositoryRecord repo, QList<AgentSession> sessions);
    // Arms/re-arms a short debounce timer that calls pushAgentSessionsSnapshot()
    // once it fires, so a burst of status flips coalesces into one request.
    void scheduleAgentSessionsPush();
    // Periodic drain of prompts the website owner queued for this node's agent
    // sessions (steering a running one from the browser).
    void drainAgentPrompts();
    void drainAgentPromptsFor(RepositoryRecord repo);
    // Deliver one queued website prompt to the matching session via
    // sendPromptToAgentSession (steers it live, or resumes it if stopped), or
    // log + drop it if no local session with that id exists.
    void deliverQueuedAgentPrompt(int sessionId, const QString &text);
    // Start a new ad-hoc agent for a repo from a website "new agent" prompt
    // (adhoc #266): the top-of-list web composer queues these with agentId "new".
    void startWebNewAgentForRepo(const RepositoryRecord &repo, const QString &task,
                                 const QString &providerOverride = QString());
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
                            double amountUsd, const QString &amountSol,
                            const QString &kind = QString());
    // Issue #347: fetch/mint the owner's inbuilt bounty wallet and show its
    // deposit address, QR and live balance so the owner can pre-fund it.
    void showBountyWalletDialog();
    void submitIssueCommentToInbox(const QString &body);
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
                               std::function<void(bool ok, const QString &error)> onDone = {});
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
    // #368: identity key backup/export/import UI + first-run "back up" nag.
    void backUpIdentityKey();
    void refreshIdentityBackupNag();
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
    void rebuildLogFilterButtons(); // (re)build the category chip row
    void rebuildNetworkLogView();   // re-render the log honoring m_logFilter
    QString networkLogPath() const; // on-disk path for the persisted log
    void loadNetworkLog();          // restore log history at startup
    void saveNetworkLog();          // rewrite (and trim) the on-disk log
    // Compact, centered success/failure banner shown in the top bar between the
    // breadcrumb and the notifications bell. Auto-clears after a few seconds.
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
    MessageRow *addMessageRow(const ChatMessage &message);
    void renderConversationRows(); // rebuilds rows in place; caller handles scrolling
    void rebuildConversationView();
    void scrollToBottom();
    void setChannels(const QStringList &channels);
    void setRoster(const QList<MemberInfo> &members);
    // Post this node's one-time "just joined" greeting to the shared #welcome
    // room. Only a brand-new identity announces (gated by a sentinel file next
    // to the identity key), so the network sees a single join line with no
    // per-peer duplicates, and no re-announce on a settings-only reset (#192).
    void maybeAnnounceWelcome();
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
    // Create an invite-only room (see ServerNode::createPrivateChannel) and start
    // in it. Its name is remembered so it survives a reconnect/restart.
    void promptAddPrivateChannel();
    // Offer the online members as invitees for the current private room.
    void promptInviteToPrivateChannel();
    // Re-create the private rooms we own/were invited to after a fresh connect,
    // since the backend clears its channel set each session.
    void restorePrivateChannels();
    // Save m_privateChannels to QSettings so they outlive a restart.
    void persistPrivateChannels();
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
    // Second half of syncRepository: spawn the async fetch/clone once the
    // off-thread pre-fetch prep (refs digest + origin set-url) has finished.
    void startSyncFetch(int index, bool quiet, bool hasMirror,
                        const QStringList &args, const QString &beforeDigest,
                        const QString &beforeHeadCommit);
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
    // Tiny spinning-radar + latency readout sitting just left of the relay name:
    // probes the active relay once a minute and shows the round-trip time (e.g.
    // "33ms"), turning into a red alert when the relay doesn't answer. Held as a
    // QWidget* and poked via static_cast (concrete RelayRadarWidget is private to
    // MainWindow.cpp).
    QWidget *m_relayRadar = nullptr;
    QTimer *m_relayLatencyTimer = nullptr; // one-minute relay-latency probe
    bool m_relayProbeInFlight = false;     // guard against overlapping probes
    int m_relayProbeFailures = 0;          // consecutive failed probes; the radar
                                           // only flips to "offline" after the
                                           // second miss (one blip isn't an outage)
    int m_relayProbeElevated = 0;          // consecutive elevated-latency samples;
                                           // backs off the confirm re-probe so a
                                           // persistently-slow link isn't polled
                                           // every second forever (adhoc #74)
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
    // Reward-availability cluster, now living in the node profile panel right
    // under "Get paid to mirror": a clear on/off switch (ToggleSwitch, private to
    // MainWindowChat.cpp) that takes this node offline (stops serving + the
    // reward heartbeat), a status label spelling out on/off, a clear
    // "available for rewards" / "offline · not collecting rewards" status line,
    // and a live "online Xh Ym" uptime readout. m_nodeOffline is persisted so a
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
    QHash<QString, QPair<double, qint64>> m_navFiatRates; // cur -> {rate, fetchedMs}
    QPushButton *m_chatButton = nullptr; // top-bar chat toggle (next to the bell)
    QLabel *m_chatUnreadBadge = nullptr; // red unread-count badge over the chat button
    QPushButton *m_agentsNavButton = nullptr; // top-bar shortcut to the Agents tab, between Repo and Chat
    // Small connection status dot painted over the top-right avatar (green
    // online / amber connecting / grey offline), replacing the old text pill.
    QLabel *m_connectionDot = nullptr;
    QString m_connectionStatusColor;      // last dot colour (skip redundant repaints)
    QLabel *m_topMessage = nullptr;       // compact centered success/failure toast
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
    // Scrollable live log pinned to the bottom of the window: shows as many recent
    // lines as fit tall, with a scrollbar so earlier history can be scrolled back
    // to. Streams every logSystem()/appendUpdateLog() line, including the
    // session-start/session-end/rebuild markers.
    QPlainTextEdit *m_footerUpdateLog = nullptr;
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
    QPushButton *m_settingsNavButton = nullptr; // Settings button on the repo header row
    QPushButton *m_logNavButton = nullptr; // "Log" button in the persistent top nav
    QPushButton *m_leaderboardNavButton = nullptr; // "Leaderboards" top-nav button
    QPushButton *m_hostsNavButton = nullptr;  // "Hosts" top-nav button (adhoc #263)
    QPushButton *m_relaysNavButton = nullptr; // "Relays" top-nav button
    QPushButton *m_navRebuildButton = nullptr; // small rebuild+restart button (opt-in)
    QPushButton *m_navScreenshotButton = nullptr; // drag-a-region screenshot -> prompt
    QPushButton *m_navDrawButton = nullptr; // pencil -> draw freehand on the screen
    QPushButton *m_restartSpinButton = nullptr; // button whose icon spins mid-restart
    QWidget *m_leaderboardsContent = nullptr; // container repopulated on refresh
    QLabel *m_leaderboardsStatus = nullptr;   // loading / error / empty notice
    // Hosts section widgets (adhoc #263): one-host install form + live log.
    QLineEdit *m_hostIpEdit = nullptr;
    QLineEdit *m_hostUserEdit = nullptr;
    QLineEdit *m_hostPassEdit = nullptr;
    QLineEdit *m_hostNameEdit = nullptr;
    // Direct-upload install (adhoc #67): stream this app's own release binary
    // to the host over the SSH session instead of the host downloading the
    // release from the relay.
    QCheckBox *m_hostUploadBinaryCheck = nullptr;
    QPushButton *m_hostAddButton = nullptr;
    QPushButton *m_hostInstallButton = nullptr;
    // Bulk direct-upload install (adhoc #257): runs the upload-binary install
    // against every saved host, one after another.
    QPushButton *m_hostInstallAllButton = nullptr;
    // Bulk uninstall + reinstall from binary (adhoc #258).
    QPushButton *m_hostReinstallAllButton = nullptr;
    QLabel *m_hostInstallStatus = nullptr;
    QPlainTextEdit *m_hostInstallLog = nullptr;
    // ANSI parser state for the live install log: a carry buffer holding an
    // escape sequence split across read chunks, plus the current SGR style
    // (foreground as 0xRRGGBB, -1 = theme default).
    QString m_hostInstallLogCarry;
    int m_hostInstallLogFg = -1;
    bool m_hostInstallLogBold = false;
    QTableWidget *m_hostsTable = nullptr;
    QProcess *m_hostInstallProcess = nullptr; // running ssh install session, if any
    // Installer link-code detection (adhoc #53): rolling tail of the install
    // output so the "Link code: NNNNNN" line survives chunk splits, and a
    // per-run guard so the link popup opens once.
    QString m_hostInstallLinkTail;
    bool m_hostLinkPrompted = false;
    // Relays section: live list of configured relays with status / latency / version.
    QTableWidget *m_relaysTable = nullptr;
    QLabel *m_relaysStatus = nullptr;       // "Probing N relays…" / last-refreshed line
    QPushButton *m_relaysRefreshButton = nullptr;
    int m_relayProbesInFlight = 0;          // outstanding /api/version probes
    QHBoxLayout *m_repoHeaderLeft = nullptr; // left cluster of the repo header row
    QPushButton *m_repoPushButton = nullptr; // "Publish N" button shown above the tab bar
    QPushButton *m_repoPushEyeButton = nullptr; // eye icon beside Sync -> commits panel
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
    QLabel *m_settingsEmailLabel = nullptr;
    QLabel *m_settingsEmailVerifiedBadge = nullptr;
    QLabel *m_settingsAvatarPreview = nullptr;
    QLabel *m_identityBackupNag = nullptr; // #368: "back up your key" warning
    QPlainTextEdit *m_settingsLog = nullptr;
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
    QPlainTextEdit *m_prioritizePromptEdit = nullptr;
    QTimer *m_mirrorSyncTimer = nullptr;
    QTimer *m_inboxPollTimer = nullptr; // background drain of owned repo inboxes
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
    QCheckBox *m_quickAddAssignAgent = nullptr; // assign a coding agent on add
    QComboBox *m_quickAddAgentProvider = nullptr;
    // Claude model chooser (adhoc #261): pick opus/sonnet/etc. for Claude Code
    // quick-add runs. Only meaningful for the "Claude Code" provider, so it's
    // shown/hidden as the provider selection changes.
    QComboBox *m_quickAddClaudeModel = nullptr;
    // Permission-mode chooser (issue #348): Ask before edits/Edit automatically/
    // Plan mode/Auto mode, styled like the provider/model combos beside it and
    // backed by the same kClaudeAutoModeSetting as the agent composer's toggle.
    QComboBox *m_quickAddModeSelector = nullptr;
    QCheckBox *m_quickAddCreatePr = nullptr;    // request PR from quick-add agent
    // "Create issue" toggle (adhoc #99): off by default (remembered via
    // kQuickAddCreateIssueSetting) — unchecked means the typed prompt starts an
    // agent directly and skips filing an issue at all.
    QCheckBox *m_quickAddCreateIssue = nullptr;
    // Up-pointing paper-airplane stacked above the normal send icon (adhoc #99):
    // sends the typed prompt as a follow-up message to the currently-selected
    // agent session instead of the quick-add issue/new-agent flow.
    QPushButton *m_quickAddSendToAgentButton = nullptr;
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
    // Centered in the footer: the git identity (name <email>) configured for the
    // repo currently open in the detail view. Updated by openRepoDetail.
    QLabel *m_footerGitIdentity = nullptr;
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
    qulonglong m_diagLastCpuTicks = 0;
    qint64 m_diagLastCpuMs = 0;

    // Repo detail view
    int m_repoDetailIndex = -1;
    QButtonGroup *m_issueTabGroup = nullptr; // Issues / Milestones / Labels tabs
    QButtonGroup *m_repoDetailTabs = nullptr;
    QPushButton *m_repoCodeTab = nullptr;
    QPushButton *m_repoIssuesTab = nullptr;
    QPushButton *m_repoPullsTab = nullptr;
    QPushButton *m_repoDiscussionsTab = nullptr;
    QPushButton *m_repoActionsTab = nullptr;
    // Floating strip of thin bars above the Actions tab — one per in-flight run,
    // each labelled with the workflow name and growing to the right the longer
    // its run has been going. Hidden when nothing is running.
    QWidget *m_actionStrip = nullptr;
    QVBoxLayout *m_actionStripCol = nullptr;
    QList<int> m_actionStripIds; // running run ids currently shown (skip rebuilds)
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
    // Bumped each time a branch is selected / a scope diff is requested so the
    // off-thread git reads that build the scope list and render the diff can drop
    // their result if the user has since switched branch or scope (issue #353 —
    // showBranchDiff/renderBranchScopeDiff shelled git on the GUI thread).
    int m_branchScopeLoadGen = 0;
    int m_branchScopeDiffGen = 0;
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
    // Progressive-render state for a large branch/commit diff: it is split into
    // per-file HTML blocks and appended a batch at a time off the event loop so
    // the GUI thread never blocks laying it all out at once (adhoc #51; same
    // freeze the cap in issue #187 guarded against). m_branchDiffRenderGen is
    // bumped on every render so a queued batch from a superseded scope selection
    // bails instead of writing into the now-current diff. m_branchDiffFilePaths
    // holds the ordered file paths so the sticky-bar span map can be rebuilt once
    // the whole diff has landed.
    QStringList m_branchDiffPendingBlocks;
    QStringList m_branchDiffFilePaths;
    int m_branchDiffRenderGen = 0;
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
    // Live activity strip floating just above the Mirror nodes tab: a dot per
    // active node that flashes green when it serves a clone, orange when it
    // serves browsing. Held as a QWidget* (concrete MirrorActivityStrip is
    // private to MainWindow.cpp). m_mirrorActivityStripTimer keeps it anchored
    // over the tab as the window reflows (mirrors the looper toggle, adhoc #197).
    QWidget *m_mirrorActivityStrip = nullptr;
    QTimer *m_mirrorActivityStripTimer = nullptr;
    // Coalesces the heavy tail of onRequestServed (stats save + full repo-list
    // rebuild) so a clone/browse burst costs one refresh per second, not one per
    // served request.
    QTimer *m_requestServedFlushTimer = nullptr;
    // Coalesces roster-driven Mirror-nodes panel rebuilds (they shell git).
    QTimer *m_mirrorPanelRosterTimer = nullptr;
    // Commit hash -> subject, so the Mirror-nodes panel's per-row tooltip lookup
    // doesn't re-shell `git show` on every roster-driven rebuild.
    QHash<QString, QString> m_commitSubjectCache;
    // Current-release pill floating just above the Releases tab (adhoc #69):
    // shows the newest tag so the current release is visible from any tab. Its
    // text is set from the tag scan; m_releaseStripTimer keeps it anchored as the
    // window reflows (mirrors the mirror-activity strip).
    QLabel *m_releaseStrip = nullptr;
    QTimer *m_releaseStripTimer = nullptr;
    // "Reset integrity pin" action, shown in the Mirror nodes header only when
    // this node is the source of truth (the owner holding the working copy).
    QPushButton *m_mirrorResetPinButton = nullptr;
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
    QPushButton *m_toolbarCommitsButton = nullptr; // -> commits panel, next to Branches/Tags
    // Persistent segmented toggle, always visible above the Code page, that
    // switches between the GitHub-style overview and the explorer/editor view.
    QPushButton *m_filesModeOverviewButton = nullptr; // -> code overview
    QPushButton *m_filesModeExplorerButton = nullptr; // -> explorer/editor
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
    QPushButton *m_aboutEditButton = nullptr;
    QLabel *m_aboutText = nullptr;
    QLabel *m_aboutTopics = nullptr;
    QLabel *m_aboutFiles = nullptr;
    QLabel *m_releaseHeader = nullptr;
    QLabel *m_releaseRow = nullptr;
    QLabel *m_langBar = nullptr;
    QLabel *m_langLegend = nullptr;
    QLabel *m_filesCountHeader = nullptr;
    QLabel *m_filesCountRow = nullptr;
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
    int m_diffFontPt = 12; // diff viewer text size (the +/- zoom control)
    // Every diff viewer registered for shared text-size zoom (issue #254), so a
    // +/- click or Ctrl+wheel can re-render them all at the new size.
    QList<QTextEdit *> m_diffViews;
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
    QTimer *m_repoPushTimer = nullptr;           // keeps "Sync" pinned over Code
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
    QTextEdit *m_actionDiff = nullptr;
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
    // Agent sessions assigned from issues.
    AgentStore *m_agentStore = nullptr;
    // Pool of agent runners so sessions execute in parallel (one process each)
    // instead of being serialized through a single runner.
    QList<AgentRunner *> m_agentRunners;
    QList<AgentSession> m_agentSessions;
    QList<int> m_agentQueue;
    // True while runDeferredStartup() drains the sessions initAgents() re-queued
    // after an app restart: resumed runs must NOT jump to the Agents tab the way
    // a fresh user-driven start does. At startup that jump forced a full cold
    // openRepoDetail() before the first frame (~2s of git reads), which the
    // last-repo restore then redid from scratch moments later.
    bool m_agentQuietResume = false;
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
    // adhoc #182: push this node's agent sessions to the website + drain any
    // steering prompts queued there. m_agentSyncDebounceTimer is a singleShot
    // re-armed after a status flip so a burst of updates coalesces into one push.
    QTimer *m_agentSyncPushTimer = nullptr;
    QTimer *m_agentSyncDebounceTimer = nullptr;
    QTimer *m_agentPromptDrainTimer = nullptr;
    // Each running Claude Code session has its own worktree + stream + buffered
    // events, so their output never leaks across sessions; the transcript view is
    // repainted from the selected session's buffer.
    QHash<int, ClaudeStreamSession *> m_streamSessions;
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
    // Cached agents-list diff summaries keyed by sessionId (issue #170), so the
    // search-as-you-type refresh reuses them instead of re-shelling git per row.
    // No longer wiped wholesale on reloadAgents(): a plain tab switch (issue #289)
    // re-validates each entry against m_agentDiffSig and only re-shells the rows
    // whose state actually moved, so an idle Issues→Agents switch runs zero git.
    QHash<int, AgentDiffStat> m_agentDiffStats;
    // Per-session fingerprint of the inputs the cached AgentDiffStat was computed
    // from (status/branch/merge/finish + the base tip). reloadAgents() flips
    // m_agentDiffRefreshPending; the next refreshAgentTable() drops only the
    // entries whose fingerprint changed (issue #289).
    QHash<int, QString> m_agentDiffSig;
    bool m_agentDiffRefreshPending = false;
    // Sessions maybeAutoFixAgentConflict() has already auto-triggered a fix for.
    // Prevents an unresolved conflict from re-queuing the agent on every refresh;
    // cleared once the session's AgentDiffStat stops reporting conflicted.
    QSet<int> m_agentAutoFixAttempted;
    // Re-entrancy guard for refreshAgentTable(): its cold-cache Diff cells shell
    // git and pump the event loop (GitKeepAlive), so a queued slot can re-enter
    // and corrupt the half-built table unless we skip the nested rebuild.
    bool m_agentTableRefreshing = false;
    QHash<int, QString> m_lastAssistantText; // last assistant prose, for waiting/question
    void notifyAgentWaiting(int sessionId, bool needsPermission);
    void markAgentSessionRunning(int sessionId);
    QHash<int, QStringList> m_streamFiles;
    QHash<int, QString> m_streamWorktree;        // sessionId -> worktree path
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
    void startClaudeCodeTranscript(AgentSession &session, const Issue &issue,
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
    void landAgentPullForSession(AgentSession &session, const QString &patch,
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
    // refreshClaudeModelCombo). Throttles re-fetches so browsing sessions doesn't
    // hit /v1/models on every click while still keeping the list current.
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
    QPushButton *m_agentFixConflictsButton = nullptr;
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
    bool m_branchesPanelLoading = false; // re-entrancy guard for loadBranchesPanel
    bool m_agentMergeStateRefreshing = false; // re-entrancy guard, refreshAgentMergeState
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
    // Compact looper toggle floating just above the Issues tab (adhoc #130): a
    // switch + "looper #N" label that both shows and controls the loop, with a
    // neon-green segment circling its border while on. Held as a QWidget* because
    // the concrete LooperToggle type lives in the .cpp; downcast there.
    // m_looperToggleTimer keeps it anchored over the tab as the window reflows.
    // m_looperRepoSlug ("owner/name") records which repo the loop is bound to so
    // a restart resumes it on the same repo.
    QWidget *m_looperToggle = nullptr;
    QTimer *m_looperToggleTimer = nullptr;
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
    QComboBox *m_issueAgentProvider = nullptr;   // OpenAI API | Claude API
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
    // Content signature (issues/ subtree oid + repo path) of the data the issue
    // list was last built from. reloadIssues() fires on every push/sync — i.e.
    // every agent commit — but a code-only commit doesn't touch issues/, so this
    // lets it skip re-reading git and tearing down/rebuilding the issue rows when
    // nothing changed (a rebuild mid-interaction drops the click/keystroke the user
    // aimed at a row or the search box). Empty = "unknown", never skip.
    QString m_issuesLoadedSig;
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
    // "USER ACCOUNT" section (self only): shows whether this node is linked to a
    // user and offers "Log in as a user" to attach it. m_nodeOwnerUser holds the
    // owning user's name (empty = unlinked), learned from account lookups.
    QWidget *m_profileAccountSection = nullptr;
    QLabel *m_profileAccountStatus = nullptr;
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
    // Invite-only rooms this node owns or was invited to. Badged in the sidebar
    // and persisted so they reappear after a reconnect (the backend clears its
    // channel set each session). See promptAddPrivateChannel / inviteToChannel.
    QSet<QString> m_privateChannels;
    QList<RepositoryRecord> m_repositories;
    QList<RepoHost *> m_repoHosts;
    QList<MemberInfo> m_homeRoster;
    QSet<QString> m_removedPeerIds;  // IDs explicitly removed via removeChatMember
    // True once this node has posted (or confirmed it already posted) its one-time
    // #welcome greeting this run, so the per-roster check stays cheap (issue #192).
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
    // Last push state computed for m_pushStateIndex, so updateRepoPushButton can
    // paint the "Sync" button instantly from cache (e.g. flip to "Syncing
    // changes…" the moment Sync is clicked) while a worker recomputes off-thread.
    RepoPushState m_pushState;
    int m_pushStateIndex = -1;        // repo index m_pushState describes (-1 = none)
    bool m_pushStateInFlight = false; // a recompute worker is currently running
    bool m_pushStatePending = false;  // another recompute was requested mid-flight
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
    // Last website-claim confirmation code already shown (adhoc #53), so the
    // per-minute heartbeat doesn't reopen the popup for the same claim.
    QString m_lastClaimCodeShown;
    // Admin claiming this node last shown for an ownership-transfer prompt
    // (adhoc #141), so the per-minute heartbeat doesn't reopen the dialog
    // while the request is still pending a decision.
    QString m_lastOwnershipTransferAdminShown;
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
