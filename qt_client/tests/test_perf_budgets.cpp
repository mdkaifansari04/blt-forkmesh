// Performance budgets for the desktop client (issue #357): startup
// (MainWindow ctor -> first paint) and repo-detail tab switches were both
// found and fixed "by feel" (see the Commits-tab and lazy-list-tab work).
// This suite puts a machine-checked floor under both so the next regression
// fails CI instead of waiting to be noticed.
//
// Deliberately its own binary, separate from forkmesh-window-tests: that
// suite is known-flaky in some sandboxed environments, and a perf gate must
// be skippable per-environment (FORKMESH_SKIP_PERF_TESTS=1) without losing
// window-tests' functional coverage.

#include "../src/MainWindow.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {

int failures = 0;

qint64 envBudgetMs(const char *name, qint64 fallback)
{
    const QByteArray raw = qgetenv(name);
    if (raw.isEmpty())
        return fallback;
    bool ok = false;
    const qint64 value = raw.toLongLong(&ok);
    return (ok && value > 0) ? value : fallback;
}

void checkBudget(const QString &what, qint64 elapsedMs, qint64 budgetMs)
{
    if (elapsedMs <= budgetMs) {
        qInfo("PASS: %s took %lldms (budget %lldms)", qPrintable(what),
              static_cast<long long>(elapsedMs), static_cast<long long>(budgetMs));
    } else {
        qCritical("FAIL: %s took %lldms, over the %lldms budget", qPrintable(what),
                  static_cast<long long>(elapsedMs), static_cast<long long>(budgetMs));
        ++failures;
    }
}

bool runGitChecked(const QString &repoDir, const QStringList &arguments)
{
    QProcess process;
    process.setWorkingDirectory(repoDir);
    process.start(QStringLiteral("git"), arguments);
    if (!process.waitForFinished(10000))
        return false;
    return process.exitCode() == 0;
}

bool initGitRepo(QTemporaryDir &repo)
{
    if (!repo.isValid())
        return false;
    return runGitChecked(repo.path(), {"init", "-q"}) &&
           runGitChecked(repo.path(), {"config", "user.email", "perf@forkmesh.local"}) &&
           runGitChecked(repo.path(), {"config", "user.name", "ForkMesh Perf"}) &&
           runGitChecked(repo.path(), {"checkout", "-q", "-b", "main"}) &&
           runGitChecked(repo.path(), {"commit", "-q", "--allow-empty", "-m", "initial"});
}

// Drives the tab switch exactly as testClickRepoDetailTab does for a real
// click, then pumps the event loop the way the app's own paint cycle would,
// and reports the wall-clock cost of that switch (including any first-open
// lazy build/load the tab does).
qint64 measureTabSwitch(MainWindow &window, int tabId)
{
    QElapsedTimer timer;
    timer.start();
    window.testClickRepoDetailTab(tabId);
    for (int i = 0; i < 10; ++i)
        QApplication::processEvents();
    return timer.elapsed();
}

} // namespace

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsSet("FORKMESH_SKIP_PERF_TESTS")) {
        qInfo("SKIP: performance budget tests disabled via "
              "FORKMESH_SKIP_PERF_TESTS (the gate is meant to be skippable "
              "per-environment; forkmesh-tests/forkmesh-window-tests still "
              "run functional coverage)");
        return 0;
    }

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        qCritical("FAIL: could not create temporary settings directory");
        return 1;
    }
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir.path());

    // Same app-data isolation as forkmesh-window-tests: AppDataLocation reads
    // XDG_DATA_HOME at call time, so set it before QApplication (and thus
    // before MainWindow's initAgents()).
    QTemporaryDir dataDir;
    if (!dataDir.isValid()) {
        qCritical("FAIL: could not create temporary data directory");
        return 1;
    }
    qputenv("XDG_DATA_HOME", dataDir.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName("ForkMeshTests");
    app.setApplicationName("PerfBudgets");

    const QString appDataPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        qCritical("FAIL: could not resolve temporary app data directory");
        return 1;
    }
    QDir appDataDir(appDataPath);
    if (appDataDir.exists() && !appDataDir.removeRecursively()) {
        qCritical("FAIL: could not clear temporary app data directory");
        return 1;
    }

    // --- Startup: MainWindow ctor -> first paint, under the offscreen QPA
    // plugin this suite (like forkmesh-window-tests) runs on in CI. Target
    // from issue #357: < 1000ms; overridable per-environment for recalibration
    // without a recompile.
    const qint64 startupBudgetMs =
        envBudgetMs("FORKMESH_PERF_STARTUP_BUDGET_MS", 1000);
    QElapsedTimer startupTimer;
    startupTimer.start();
    MainWindow window;
    window.show();
    for (int i = 0; i < 10; ++i)
        QApplication::processEvents();
    checkBudget(QStringLiteral("startup (ctor -> first paint)"),
                startupTimer.elapsed(), startupBudgetMs);

    // --- Repo-detail tab switches, including each tab's first (lazy-build)
    // open -- the Commits tab and the "and more" list tabs (Worktrees,
    // Releases, Mirror nodes) were exactly the ones found slow by feel.
    // Target from issue #357: < 150ms; overridable per-environment.
    const qint64 tabBudgetMs = envBudgetMs("FORKMESH_PERF_TABSWITCH_BUDGET_MS", 150);

    QTemporaryDir repoDir;
    if (initGitRepo(repoDir)) {
        const int idx =
            window.testAddLocalRepository("perf", "budgetrepo", repoDir.path());
        window.testOpenRepository(idx);
        QApplication::processEvents();

        struct TabCase {
            int id;
            const char *label;
        };
        const QList<TabCase> cases = {
            {1, "Commits"},
            {2, "Issues"},
            {window.testWorktreesTabIndex(), "Worktrees"},
            {window.testReleasesTabIndex(), "Releases"},
            {window.testMirrorNodesTabIndex(), "Mirror nodes"},
        };
        for (const TabCase &tc : cases) {
            const qint64 elapsed = measureTabSwitch(window, tc.id);
            checkBudget(
                QStringLiteral("tab switch: %1").arg(QString::fromLatin1(tc.label)),
                elapsed, tabBudgetMs);
        }
    } else {
        qCritical("FAIL: could not set up a temporary repo for tab-switch timing");
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}
