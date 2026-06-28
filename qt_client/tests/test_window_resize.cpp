#include "../src/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QCheckBox>
#include <QDebug>
#include <QElapsedTimer>
#include <QMenu>
#include <QProcess>
#include <QSemaphore>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>

#include <algorithm>
#include <atomic>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition) {
        qInfo("PASS: %s", qPrintable(what));
    } else {
        qCritical("FAIL: %s", qPrintable(what));
        ++failures;
    }
}

QString widgetPath(QWidget *widget)
{
    QStringList parts;
    for (QWidget *current = widget; current; current = current->parentWidget()) {
        QString part = QString::fromLatin1(current->metaObject()->className());
        if (!current->objectName().isEmpty())
            part += QStringLiteral("#") + current->objectName();
        parts.prepend(part);
    }
    return parts.join(QStringLiteral(" > "));
}

void dumpTallMinimums(QWidget &root)
{
    struct Entry {
        int effectiveHeight = 0;
        int minimumHintHeight = 0;
        int explicitMinimumHeight = 0;
        QString path;
    };

    QList<Entry> entries;
    const QList<QWidget *> widgets = root.findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        const int minimumHintHeight = widget->minimumSizeHint().height();
        const int explicitMinimumHeight = widget->minimumHeight();
        const int effectiveHeight =
            std::max(minimumHintHeight, explicitMinimumHeight);
        if (effectiveHeight < 120)
            continue;
        entries.append({effectiveHeight, minimumHintHeight, explicitMinimumHeight,
                        widgetPath(widget)});
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) {
                  return a.effectiveHeight > b.effectiveHeight;
              });

    const int limit = std::min<int>(entries.size(), 18);
    for (int i = 0; i < limit; ++i) {
        const Entry &entry = entries.at(i);
        qInfo().noquote()
            << QString("MIN %1px hint=%2 explicit=%3 %4")
                   .arg(entry.effectiveHeight)
                   .arg(entry.minimumHintHeight)
                   .arg(entry.explicitMinimumHeight)
                   .arg(entry.path);
    }
}

QCheckBox *findCheckBox(QWidget &root, const QString &text)
{
    const QList<QCheckBox *> boxes = root.findChildren<QCheckBox *>();
    for (QCheckBox *box : boxes) {
        if (box->text() == text)
            return box;
    }
    return nullptr;
}

QPushButton *findButtonStartingWith(QWidget &root, const QString &prefix)
{
    const QList<QPushButton *> buttons = root.findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text().startsWith(prefix))
            return button;
    }
    return nullptr;
}

QString quotedCommand(const QString &program, const QStringList &arguments)
{
    QStringList parts{program};
    parts += arguments;
    return parts.join(QLatin1Char(' '));
}

bool runProcessChecked(const QString &program, const QStringList &arguments,
                       const QString &workingDir)
{
    QProcess process;
    if (!workingDir.isEmpty())
        process.setWorkingDirectory(workingDir);
    process.start(program, arguments);
    if (!process.waitForFinished(10000)) {
        qCritical().noquote()
            << "FAIL: timed out:" << quotedCommand(program, arguments);
        ++failures;
        return false;
    }
    if (process.exitCode() != 0) {
        qCritical().noquote()
            << "FAIL:" << quotedCommand(program, arguments)
            << QString::fromUtf8(process.readAllStandardError()).trimmed();
        ++failures;
        return false;
    }
    return true;
}

bool runGitChecked(const QString &repoDir, const QStringList &arguments)
{
    return runProcessChecked(QStringLiteral("git"), arguments, repoDir);
}

bool initGitRepo(QTemporaryDir &repo)
{
    if (!repo.isValid()) {
        qCritical("FAIL: could not create temporary git repository");
        ++failures;
        return false;
    }

    return runGitChecked(repo.path(), {"init"}) &&
           runGitChecked(repo.path(), {"config", "user.email", "tests@forkmesh.local"}) &&
           runGitChecked(repo.path(), {"config", "user.name", "ForkMesh Tests"}) &&
           runGitChecked(repo.path(), {"checkout", "-b", "main"}) &&
           runGitChecked(repo.path(), {"commit", "--allow-empty", "-m", "initial"});
}

MemberInfo testMember(const QString &id, const QString &name, bool self = false)
{
    MemberInfo member;
    member.id = id;
    member.name = name;
    member.self = self;
    member.online = true;
    return member;
}

void stopChildProcesses(QObject &root)
{
    const QList<QProcess *> processes = root.findChildren<QProcess *>();
    for (QProcess *process : processes) {
        if (process->state() == QProcess::NotRunning)
            continue;
        process->kill();
        process->waitForFinished(3000);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        qCritical("FAIL: could not create temporary settings directory");
        return 1;
    }
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir.path());

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName("ForkMeshTests");
    app.setApplicationName("WindowResize");

    MainWindow window;
    window.show();
    QApplication::processEvents();

    QTemporaryDir upstreamRepo;
    QTemporaryDir upstreamRemote;
    if (initGitRepo(upstreamRepo) && upstreamRemote.isValid()) {
        runProcessChecked(QStringLiteral("git"),
                          {"init", "--bare", upstreamRemote.path()}, QString());
        runGitChecked(upstreamRepo.path(),
                      {"remote", "add", "origin", upstreamRemote.path()});
        runGitChecked(upstreamRepo.path(), {"push", "-u", "origin", "main"});
        check(window.testQuickUpdatePullArguments(upstreamRepo.path()) ==
                  QStringList({"pull", "--ff-only"}),
              QStringLiteral("quick update keeps plain pull when upstream exists"));
    }

    QTemporaryDir noUpstreamRepo;
    if (initGitRepo(noUpstreamRepo)) {
        runGitChecked(noUpstreamRepo.path(), {"checkout", "-b", "feature/no-upstream"});
        check(window.testQuickUpdatePullArguments(noUpstreamRepo.path()) ==
                  QStringList({"pull", "--ff-only", "origin", "feature/no-upstream"}),
              QStringLiteral("quick update pulls origin/current-branch without upstream"));
    }

    QTemporaryDir detachedRepo;
    if (initGitRepo(detachedRepo)) {
        runGitChecked(detachedRepo.path(), {"checkout", "--detach", "HEAD"});
        check(window.testQuickUpdatePullArguments(detachedRepo.path()) ==
                  QStringList({"pull", "--ff-only", "origin", "HEAD"}),
              QStringLiteral("quick update pulls origin/HEAD in detached HEAD"));
    }

    // Issue #263: data-table columns are user-resizable — ResizeToContents
    // columns flip to draggable Interactive once rows arrive, keeping their
    // fitted widths, while Stretch and Fixed columns are left as configured.
    check(window.testColumnsBecomeResizable(),
          QStringLiteral("data-table content columns become drag-resizable"));

    // Issue #150: a conflicted PR's "Fix with agent" control is a single dropdown
    // that rolls the Claude API, OpenAI API and Claude Code resolvers into one
    // button instead of separate per-provider buttons. The Branches tab carries
    // its own "Fix with agent" button too (issue #116), so identify the PR one by
    // its distinctive three-resolver menu rather than by label alone.
    bool prFixMenuFound = false;
    for (QPushButton *fixButton : window.findChildren<QPushButton *>()) {
        if (!fixButton->text().startsWith(QStringLiteral("Fix with agent")) ||
            !fixButton->menu())
            continue;
        QStringList labels;
        for (QAction *action : fixButton->menu()->actions())
            labels << action->text();
        if (labels == QStringList({QStringLiteral("Claude API"),
                                   QStringLiteral("OpenAI API"),
                                   QStringLiteral("Claude Code")})) {
            prFixMenuFound = true;
            break;
        }
    }
    check(prFixMenuFound,
          QStringLiteral("PR 'Fix with agent' dropdown offers Claude API, OpenAI API "
                         "and Claude Code"));

    // Issue #268: dragging a column divider resizes like moving a margin — the
    // width comes from the immediate neighbour, not a far-off Stretch column, so
    // the divider tracks the cursor instead of snapping back.
    check(window.testMarginResize(),
          QStringLiteral("column drag trades width with its neighbour"));

    // Issue #33: the agents list lets the user drag column headers into a new
    // order, and resizing afterwards still trades width with the visual
    // neighbour so the divider keeps tracking the cursor.
    check(window.testAgentColumnsMovable(),
          QStringLiteral("agents list column headers are draggable/reorderable"));
    check(window.testMarginResizeAfterMove(),
          QStringLiteral("column drag trades with visual neighbour after a move"));

    window.testEnableSessionStartBypass(true);

    // No wallet, no signup: starting a node needs only a valid name. The core
    // flow never invokes the (opt-in) account/signup flow, and a fresh node drops
    // straight into the app shell without an account or a verified wallet.
    window.testSetSetupInputs(QStringLiteral("Alice-Node"),
                              QStringLiteral("SavedSolana111"));
    window.testSetAccountFlowResult(true); // would activate IF the flow ran
    window.testStartSession();
    check(window.testAccountFlowCalls() == 0,
          QStringLiteral("starting a node never runs the account flow"));
    check(window.testStackIndex() == 1,
          QStringLiteral("start enters the app with just a node name"));
    check(window.testUserName() == QStringLiteral("alice-node"),
          QStringLiteral("start uses the sanitized node name"));
    check(window.testAccountName() == QStringLiteral("alice-node"),
          QStringLiteral("start records the node owner name"));
    check(window.testSavedSolanaAddress() == QStringLiteral("SavedSolana111"),
          QStringLiteral("start preserves the saved Solana address"));
    check(!window.testAccountAuthenticated(),
          QStringLiteral("start does not require or fake an account"));

    // Crypto is strictly opt-in: the account/activate flow runs only when the user
    // explicitly opts in via "Get paid to mirror" (here the mocked account flow).
    // The payout address is already set, so no address prompt is triggered.
    window.testSetAccountFlowResult(true);
    window.testEnablePaidMirroring();
    check(window.testAccountFlowCalls() == 1,
          QStringLiteral("opting in runs the account flow exactly once"));
    check(window.testAccountAuthenticated(),
          QStringLiteral("opting in marks the account authenticated"));
    check(window.testAccountTier() == QStringLiteral("active"),
          QStringLiteral("opting in sets the account tier active"));

    QCheckBox *nodeConnectAlertCheck =
        findCheckBox(window, QStringLiteral("Show a system alert when a node connects"));
    check(nodeConnectAlertCheck != nullptr,
          QStringLiteral("node-connect system alert checkbox exists"));
    if (nodeConnectAlertCheck) {
        check(!nodeConnectAlertCheck->isChecked(),
              QStringLiteral("node-connect system alert checkbox is unchecked by default"));
    }

    QSettings().setValue(QStringLiteral("notifications/nodeConnect"), false);
    window.testSetNodeAlertGraceUntilMs(0);
    QList<MemberInfo> initialRoster;
    initialRoster.append(testMember(QStringLiteral("self-node"),
                                    QStringLiteral("Self Node"), true));
    initialRoster.append(testMember(QStringLiteral("existing-peer"),
                                    QStringLiteral("Existing Peer")));
    window.testSetRoster(initialRoster);
    check(!window.testNetworkLog().join(QLatin1Char('\n')).contains(
              QStringLiteral("Node connected")),
          QStringLiteral("initial roster fill does not log node connections"));

    QList<MemberInfo> updatedRoster = initialRoster;
    updatedRoster.append(testMember(QStringLiteral("new-peer"),
                                    QStringLiteral("New Peer")));
    window.testSetRoster(updatedRoster);
    const QString networkLog = window.testNetworkLog().join(QLatin1Char('\n'));
    check(networkLog.contains(QStringLiteral("Node connected")) &&
              networkLog.contains(QStringLiteral("New Peer")),
          QStringLiteral("newly online peer is logged when node-connect alerts are disabled"));

    std::atomic<int> historyDeleteCalls{0};
    QSemaphore historyDeleteStarted;
    QSemaphore finishHistoryDelete;
    QSemaphore historyDeleteFinished;
    window.testSetIssueHistoryDeleteRunner(
        [&](int, QString *) {
            historyDeleteCalls.fetch_add(1);
            historyDeleteStarted.release();
            finishHistoryDelete.acquire();
            historyDeleteFinished.release();
            return true;
        });
    window.testDeleteIssueWithHistory(141);
    check(historyDeleteStarted.tryAcquire(1, 1000),
          QStringLiteral("history delete starts on a worker thread"));
    window.testDeleteIssueWithHistory(141);
    const bool secondDeleteStarted = historyDeleteStarted.tryAcquire(1, 1000);
    check(!secondDeleteStarted && historyDeleteCalls.load() == 1,
          QStringLiteral("second history delete request is ignored while one is running"));
    finishHistoryDelete.release(secondDeleteStarted ? 2 : 1);
    const int expectedFinishes = secondDeleteStarted ? 2 : 1;
    for (int i = 0; i < expectedFinishes; ++i) {
        check(historyDeleteFinished.tryAcquire(1, 1000),
              QStringLiteral("history delete worker finishes"));
    }
    QElapsedTimer finishTimer;
    finishTimer.start();
    while (window.testIssueHistoryDeleteInProgress() && finishTimer.elapsed() < 1000)
        QApplication::processEvents();
    check(!window.testIssueHistoryDeleteInProgress(),
          QStringLiteral("history delete state clears after the worker finishes"));
    window.testSetIssueHistoryDeleteRunner({});

    constexpr int targetHeight = 520;
    window.resize(1060, targetHeight);
    QApplication::processEvents();

    const int minHintHeight = window.minimumSizeHint().height();
    const bool acceptsVerticalResize = window.height() <= targetHeight + 8;
    check(acceptsVerticalResize,
          QString("main window accepts runtime vertical resize to %1px "
                  "(actual %2px, minimum hint %3px)")
              .arg(targetHeight)
              .arg(window.height())
              .arg(minHintHeight));
    if (!acceptsVerticalResize)
        dumpTallMinimums(window);

    // Repro: opening a repo and toggling the publish/sync bar (as happens on a
    // push and when a mirror picks it up) must not grow the window on a small
    // screen.
    QTemporaryDir repoDir;
    QProcess::execute(QStringLiteral("git"), {"-C", repoDir.path(), "init", "-q"});
    QProcess::execute(QStringLiteral("git"),
                      {"-C", repoDir.path(), "config", "user.email", "a@b.c"});
    QProcess::execute(QStringLiteral("git"),
                      {"-C", repoDir.path(), "config", "user.name", "t"});
    {
        QFile f(repoDir.path() + QStringLiteral("/README.md"));
        if (f.open(QIODevice::WriteOnly)) { f.write("hi\n"); f.close(); }
    }
    QProcess::execute(QStringLiteral("git"), {"-C", repoDir.path(), "add", "-A"});
    QProcess::execute(QStringLiteral("git"),
                      {"-C", repoDir.path(), "commit", "-qm", "init"});
    const int repoIdx = window.testAddLocalRepository("me", "r", repoDir.path());
    window.testOpenRepository(repoIdx);
    QApplication::processEvents();

    // Issue #286: the "Prioritize from README" button must actually be on the
    // open issues view (not hidden, not pushed off the right edge of the panel).
    {
        window.resize(1100, 800);
        QApplication::processEvents();
        const bool shown = window.testShowRepoIssuesTab();
        QApplication::processEvents();
        QPushButton *pb =
            findButtonStartingWith(window, "Prioritize from README");
        const bool realized = pb && pb->isVisibleTo(&window);
        bool onScreen = false;
        if (realized) {
            const QPoint tl = pb->mapTo(&window, QPoint(0, 0));
            onScreen = tl.x() >= 0 && tl.x() + pb->width() <= window.width();
        }
        check(shown && realized && onScreen,
              QString("issue #286 prioritize button is visible on the issues "
                      "view (shown=%1 realized=%2 onScreen=%3)")
                  .arg(shown)
                  .arg(realized)
                  .arg(onScreen));
    }

    window.resize(480, 420);
    QApplication::processEvents();
    window.testShowPublishBar(false);
    QApplication::processEvents();
    const int topBarHidden = window.testRepoTabContentTop();
    const int hBarHidden = window.height();
    window.testShowPublishBar(true);
    QApplication::processEvents();
    const int topBarShown = window.testRepoTabContentTop();
    const int hBarShown = window.height();
    qInfo("REPRO mirror-nodes @480x420: tab-content top hidden=%d shown=%d ; window h hidden=%d shown=%d",
          topBarHidden, topBarShown, hBarHidden, hBarShown);
    // The publish/sync bar reserves its height even when hidden, so toggling it
    // (as a push then a mirror pickup does) must not shift the tab content beneath
    // it nor grow the window — what read as the view "resizing" on small screens.
    check(topBarHidden == topBarShown && hBarShown <= 420 + 8,
          QString("publish/sync bar toggling does not reflow the page "
                  "(tab-content top %1 -> %2, window height %3px)")
              .arg(topBarHidden).arg(topBarShown).arg(hBarShown));
    if (topBarHidden != topBarShown || hBarShown > 420 + 8)
        dumpTallMinimums(window);

    // issue #272: clicking "Update from main" rebuilds the worktrees panel. The
    // rebuild must keep the same worktree selected so its diff/detail pane stays
    // on screen instead of going blank.
    QTemporaryDir wtRepo;
    if (initGitRepo(wtRepo)) {
        runGitChecked(wtRepo.path(), {"branch", "feature/keep-selected"});
        const QString wtPath = wtRepo.path() + QStringLiteral("/wt-keep");
        runGitChecked(wtRepo.path(),
                      {"worktree", "add", wtPath, "feature/keep-selected"});
        // Put the worktree's branch one commit ahead of main so the ahead/behind
        // column has something non-trivial to report.
        runGitChecked(wtPath, {"commit", "--allow-empty", "-m", "ahead by one"});
        const int wtIdx =
            window.testAddLocalRepository("me", "wtrepo", wtRepo.path());
        window.testOpenRepository(wtIdx);
        QApplication::processEvents();
        window.testSwitchToWorktree(QStringLiteral("feature/keep-selected"));
        check(window.testSelectedWorktreeBranch() ==
                  QStringLiteral("feature/keep-selected"),
              QStringLiteral("selecting a worktree records it as the selection"));
        check(window.testWorktreeBranchLabel().contains(
                  QStringLiteral("feature/keep-selected")),
              QStringLiteral("the worktree detail shows which branch it's on"));
        check(window.testWorktreeBranchLabel().contains(QStringLiteral("wt-keep")),
              QStringLiteral("the worktree detail shows the worktree's location"));
        window.testReloadWorktreesPanel(); // what "Update from main" does after merging
        check(window.testSelectedWorktreeBranch() ==
                  QStringLiteral("feature/keep-selected"),
              QStringLiteral("reloading the worktrees panel keeps the selected "
                             "worktree instead of going blank (#272)"));
        // The ahead/behind column is filled by an async `git rev-list`; pump the
        // event loop until it lands, then check it reports "1 ahead" (↑1).
        QString abText;
        QElapsedTimer abTimer;
        abTimer.start();
        while (abTimer.elapsed() < 5000) {
            QApplication::processEvents();
            abText = window.testWorktreeAheadBehindText(
                QStringLiteral("feature/keep-selected"));
            if (!abText.isEmpty() && !abText.contains(QStringLiteral("checking")))
                break;
        }
        check(abText == QString::fromUtf8("\xE2\x86\x91""1"),
              QString("worktrees list shows the branch one commit ahead of main "
                      "(ahead/behind cell = %1)").arg(abText));

        // Opening the Worktrees tab the way a user does (clicking its nav button)
        // should hand keyboard focus to the table, so arrow keys work right away
        // without first clicking a row.
        window.testClickRepoDetailTab(window.testWorktreesTabIndex());
        QApplication::processEvents();
        check(window.testWorktreesTableHasKeyboardFocus(),
              QStringLiteral("opening the Worktrees tab focuses the table so the "
                             "arrow keys can move through its rows"));

        // With feature/keep-selected (the bottom row) selected, an Up arrow on the
        // table should move the selection to the *other* worktree row; Down stays
        // put because it's already the last row.
        window.testSwitchToWorktree(QStringLiteral("feature/keep-selected"));
        QApplication::processEvents();
        const QString afterUp = window.testArrowOnWorktrees(false);
        window.testSwitchToWorktree(QStringLiteral("feature/keep-selected"));
        QApplication::processEvents();
        const QString afterDown = window.testArrowOnWorktrees(true);
        qInfo("arrow nav: up->%s down->%s",
              qPrintable(afterUp), qPrintable(afterDown));
        check(!afterUp.isEmpty() &&
                  afterUp != QStringLiteral("feature/keep-selected"),
              QStringLiteral("arrow up moves the worktree selection to the row above"));
        check(afterDown == QStringLiteral("feature/keep-selected"),
              QStringLiteral("arrow down on the last worktree row stays put"));

        // issue #172: the Branches list must also surface the worktree a branch
        // is checked out in, so an agent's isolated working tree is visible
        // without a trip to the Worktrees tab.
        window.testReloadBranchesPanel();
        const QString listedWt =
            window.testBranchWorktreePath(QStringLiteral("feature/keep-selected"));
        check(listedWt.contains(QStringLiteral("wt-keep")),
              QString("branches list shows the worktree a branch is checked out "
                      "in (#172, worktree cell = %1)").arg(listedWt));
        // The default branch lives in the main checkout, not a linked worktree,
        // so its Worktree cell stays empty rather than pointing at the main tree.
        check(window.testBranchWorktreePath(QStringLiteral("main")).isEmpty(),
              QStringLiteral("a branch checked out in the main tree has an empty "
                             "Worktree cell (#172)"));

        // adhoc #191: the Branches list must also surface the issue/agent a branch
        // is attached to. An agent session bound to this repo's branch should
        // name its issue ("#N") in the Issue / Agent column; an ad-hoc session
        // (no issue) should read "Agent"; a plain branch stays empty.
        AgentSession issueSession;
        issueSession.id = 4242;
        issueSession.owner = QStringLiteral("me");
        issueSession.name = QStringLiteral("wtrepo");
        issueSession.branchName = QStringLiteral("feature/keep-selected");
        issueSession.issueNumber = 191;
        issueSession.issueTitle = QStringLiteral("show attachment in branches list");
        window.testAddAgentSession(issueSession);
        window.testReloadBranchesPanel();
        check(window.testBranchAttachmentText(
                  QStringLiteral("feature/keep-selected")) == QStringLiteral("#191"),
              QString("branches list names the issue a branch is attached to "
                      "(adhoc #191, cell = %1)")
                  .arg(window.testBranchAttachmentText(
                      QStringLiteral("feature/keep-selected"))));
        check(window.testBranchAttachmentText(QStringLiteral("main")).isEmpty(),
              QStringLiteral("a branch with no agent session has an empty "
                             "Issue / Agent cell (adhoc #191)"));

        // adhoc #185: the default branch must stay pinned to the top of the list.
        // feature/keep-selected was committed to more recently (it's a worktree one
        // commit ahead of main), so a plain committer-date sort would float it above
        // main; the panel must override that and list main first.
        const QStringList order = window.testBranchRowOrder();
        qInfo("branch row order: %s", qPrintable(order.join(QStringLiteral(", "))));
        check(!order.isEmpty() && order.first() == QStringLiteral("main"),
              QString("the default branch is pinned to the top of the branches "
                      "list (adhoc #185, first row = %1)")
                  .arg(order.isEmpty() ? QStringLiteral("<none>") : order.first()));
    }

    // adhoc #183/follow-up: the repo's default (merge-base) branch must stay
    // anchored to main and NOT follow the working tree's HEAD. Parking the
    // checkout on a feature branch — what the commits-area branch switcher or a
    // transient branches-page merge does — must never silently change the default
    // branch out from under the merge editor.
    {
        QTemporaryDir defaultBranchRepo;
        if (initGitRepo(defaultBranchRepo)) {
            // Leave HEAD on a feature branch, exactly as if the user had switched
            // to it in the commits area.
            runGitChecked(defaultBranchRepo.path(),
                          {"checkout", "-b", "feature/parked"});
            runGitChecked(defaultBranchRepo.path(),
                          {"commit", "--allow-empty", "-m", "work on feature"});
            const int idx = window.testAddLocalRepository("me", "dbrepo",
                                                          defaultBranchRepo.path());
            window.testOpenRepository(idx);
            QApplication::processEvents();
            check(window.testRepoDefaultBranch() == QStringLiteral("main"),
                  QString("default branch stays main while HEAD is parked on a "
                          "feature branch (got %1)")
                      .arg(window.testRepoDefaultBranch()));
        }
    }

    // issue #251: the Settings "Default agent" choice should seed the agent
    // pickers. A window built while the default is Claude Code must start both
    // the quick-add and issue-detail pickers there (not the OpenAI fallback),
    // and changing the default afterwards must update the live pickers.
    {
        QSettings().setValue(QStringLiteral("agents/defaultProvider"),
                             QStringLiteral("claude-code"));
        MainWindow seeded;
        seeded.show();
        QApplication::processEvents();
        check(seeded.testQuickAddAgentProvider() == QStringLiteral("claude-code") &&
                  seeded.testIssueAgentProvider() == QStringLiteral("claude-code"),
              QString("default agent seeds the pickers (quick-add %1, issue %2)")
                  .arg(seeded.testQuickAddAgentProvider(),
                       seeded.testIssueAgentProvider()));

        seeded.testSetDefaultAgentProvider(QStringLiteral("claude-api"));
        check(seeded.testQuickAddAgentProvider() == QStringLiteral("claude-api") &&
                  seeded.testIssueAgentProvider() == QStringLiteral("claude-api"),
              QString("changing the default updates the live pickers "
                      "(quick-add %1, issue %2)")
                  .arg(seeded.testQuickAddAgentProvider(),
                       seeded.testIssueAgentProvider()));
        stopChildProcesses(seeded);
    }

    // Issue #287: the headless "mirrors" view leads with this node's own CPU and
    // memory so an operator watching a durable daemon can see its load, and the
    // "status" view carries the same Load line.
    {
        const QStringList mirrorLines = window.headlessMirrorLines();
        check(!mirrorLines.isEmpty() &&
                  mirrorLines.first().startsWith(QStringLiteral("node load:")) &&
                  mirrorLines.first().contains(QStringLiteral("cpu")) &&
                  mirrorLines.first().contains(QStringLiteral("mem")),
              QStringLiteral("headless mirrors view leads with a cpu/memory load line"));

        bool statusHasLoad = false;
        for (const QString &line : window.headlessStatusLines()) {
            if (line.startsWith(QStringLiteral("Load:")) &&
                line.contains(QStringLiteral("cpu")) &&
                line.contains(QStringLiteral("mem"))) {
                statusHasLoad = true;
                break;
            }
        }
        check(statusHasLoad,
              QStringLiteral("headless status view includes a cpu/memory load line"));
    }

    // issue #154: references inside an issue/PR comment body become in-app links.
    {
        check(MainWindow::autolinkReferences(QStringLiteral("see #123 please")) ==
                  QStringLiteral("see [#123](forkmesh-ref:123) please"),
              QStringLiteral("autolink turns #123 into a ref link"));
        check(MainWindow::autolinkReferences(QStringLiteral("fixed in a1b2c3d.")) ==
                  QStringLiteral("fixed in [a1b2c3d](forkmesh-commit:a1b2c3d)."),
              QStringLiteral("autolink turns a pasted commit SHA into a commit link"));
        check(MainWindow::autolinkReferences(QStringLiteral("build 1234567 ok")) ==
                  QStringLiteral("build 1234567 ok"),
              QStringLiteral("autolink leaves a plain number (no a-f) untouched"));
        check(MainWindow::autolinkReferences(QStringLiteral("use `#5` token")) ==
                  QStringLiteral("use `#5` token"),
              QStringLiteral("autolink leaves references in inline code untouched"));
        check(MainWindow::autolinkReferences(QStringLiteral("```\n#5\n```")) ==
                  QStringLiteral("```\n#5\n```"),
              QStringLiteral("autolink leaves references in a fenced block untouched"));
        check(MainWindow::autolinkReferences(QStringLiteral("[#5](http://x)")) ==
                  QStringLiteral("[#5](http://x)"),
              QStringLiteral("autolink never nests inside an existing markdown link"));
        check(MainWindow::autolinkReferences(QStringLiteral("at http://x/#5 only")) ==
                  QStringLiteral("at http://x/#5 only"),
              QStringLiteral("autolink leaves a #fragment inside a URL untouched"));
        check(MainWindow::autolinkReferences(
                  QStringLiteral("ref forkmesh://issue/o/r/12#e3 here")) ==
                  QStringLiteral("ref <forkmesh://issue/o/r/12#e3> here"),
              QStringLiteral("autolink wraps a pasted forkmesh:// permalink as a link"));
        check(MainWindow::autolinkReferences(
                  QStringLiteral("see forkmesh://pull/o/r/7.")) ==
                  QStringLiteral("see <forkmesh://pull/o/r/7>."),
              QStringLiteral("autolink leaves trailing punctuation out of a permalink"));
    }

    // adhoc #191: the issue looper (and per-issue agent assignment) must work on
    // a node that only mirrors a repo it doesn't host. Such a repo has a bare
    // network mirror and no working tree, so the gate now resolves the bare mirror
    // as the git dir agents run against instead of refusing with "only the host".
    {
        QTemporaryDir mirrorDir;
        const bool madeBare =
            mirrorDir.isValid() &&
            runGitChecked(mirrorDir.path(), {"init", "--bare", "-q"});
        const int mirrorIdx =
            window.testAddPublishedRepository("someone", "theirrepo", mirrorDir.path());
        check(madeBare &&
                  window.testRepoAgentGitDir(mirrorIdx) == mirrorDir.path(),
              QStringLiteral("a mirror-only repo resolves its bare mirror as the "
                             "agent/looper git dir (adhoc #191)"));

        QTemporaryDir localRepo;
        const bool madeLocal = initGitRepo(localRepo);
        const int localIdx =
            window.testAddLocalRepository("me", "minerepo", localRepo.path());
        check(madeLocal &&
                  window.testRepoAgentGitDir(localIdx) == localRepo.path(),
              QStringLiteral("a hosted repo still resolves its working tree as the "
                             "agent/looper git dir"));

        QTemporaryDir emptyDir;
        const int noneIdx = window.testAddPublishedRepository(
            "someone", "uncached", emptyDir.path() + QStringLiteral("/missing.git"));
        check(window.testRepoAgentGitDir(noneIdx).isEmpty(),
              QStringLiteral("a repo with neither a working tree nor a cached "
                             "mirror has no agent/looper git dir"));
    }

    stopChildProcesses(window);
    return failures == 0 ? 0 : 1;
}
