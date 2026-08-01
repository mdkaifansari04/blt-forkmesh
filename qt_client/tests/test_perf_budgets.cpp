










#include "../src/MainWindow.h"
#include "../src/MainWindowInternal.h"
#include "../src/ActionTelemetry.h"
#include "../src/BackgroundActivity.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextEdit>

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





qint64 measureTabSwitch(MainWindow &window, int tabId)
{
    QElapsedTimer timer;
    timer.start();
    window.testClickRepoDetailTab(tabId);
    for (int i = 0; i < 10; ++i)
        QApplication::processEvents();
    return timer.elapsed();
}

}

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
    QTemporaryDir runtimeDir;
    if (!runtimeDir.isValid()) {
        qCritical("FAIL: could not create temporary runtime directory");
        return 1;
    }
    qputenv("XDG_RUNTIME_DIR", runtimeDir.path().toUtf8());
    qputenv("GSETTINGS_BACKEND", QByteArrayLiteral("memory"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir.path());




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





    QString hugeDiffHtml =
        QStringLiteral("<a name=\"file-0\"></a><div class='fileblock'>"
                       "<div class='fileheader'>large/generated.json</div>"
                       "<table class='difftable'>");
    const QString row =
        QStringLiteral("<tr><td class='ln'>123</td><td class='code add'>"
                       "+&quot;generated-key&quot;: &quot;generated-value&quot;"
                       "</td></tr>");
    hugeDiffHtml.reserve(4 * 1024 * 1024);
    while (hugeDiffHtml.size() < 4 * 1024 * 1024)
        hugeDiffHtml += row;
    hugeDiffHtml += QStringLiteral("</table></div>");
    QTextEdit hugeDiffView;
    hugeDiffView.setObjectName(QStringLiteral("perfHugeDiff"));
    const qint64 diffBudgetMs =
        envBudgetMs("FORKMESH_PERF_HUGE_DIFF_BUDGET_MS", 150);
    QElapsedTimer hugeDiffTimer;
    hugeDiffTimer.start();
    forkmesh::ui::renderDiffStreamed(
        &hugeDiffView, hugeDiffHtml,
        forkmesh::ui::diffStyleSheet());
    QApplication::processEvents();
    checkBudget(QStringLiteral("single-file 4 MiB diff first paint"),
                hugeDiffTimer.elapsed(), diffBudgetMs);
    if (!hugeDiffView.toPlainText().contains(
            QStringLiteral("content omitted"))) {
        qCritical("FAIL: oversized diff was not replaced by a bounded preview");
        ++failures;
    }





    const qint64 tabBudgetMs = envBudgetMs("FORKMESH_PERF_TABSWITCH_BUDGET_MS", 150);

    QTemporaryDir repoDir;
    if (initGitRepo(repoDir)) {
        const int idx =
            window.testAddLocalRepository("perf", "budgetrepo", repoDir.path());
        QElapsedTimer repoOpenTimer;
        repoOpenTimer.start();
        window.testOpenRepository(idx);
        QApplication::processEvents();
        checkBudget(QStringLiteral("repository open first paint"),
                    repoOpenTimer.elapsed(),
                    envBudgetMs("FORKMESH_PERF_REPO_OPEN_BUDGET_MS", 250));

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



    const QString actionLog = dataDir.filePath(QStringLiteral("actions.jsonl"));
    forkmesh::ActionTelemetry::initialize(actionLog);
    const quint64 probe = forkmesh::BackgroundActivity::begin(
        QStringLiteral("test"), QStringLiteral("perf telemetry probe"),
        forkmesh::ActionTelemetry::Execution::Worker);
    forkmesh::BackgroundActivity::end(probe, QStringLiteral("succeeded"));
    forkmesh::ActionTelemetry::shutdown();
    QFile actionFile(actionLog);
    const QByteArray actionBytes =
        actionFile.open(QIODevice::ReadOnly) ? actionFile.readAll() : QByteArray();
    if (actionBytes.count("\"detail\":\"perf telemetry probe\"") == 2 &&
        actionBytes.contains("\"event\":\"started\"") &&
        actionBytes.contains("\"event\":\"finished\"") &&
        actionBytes.contains("\"outcome\":\"succeeded\"")) {
        qInfo("PASS: asynchronous action journal records start, finish, duration, "
              "execution context and outcome");
    } else {
        qCritical("FAIL: asynchronous action journal did not flush a complete "
                  "start/finish pair");
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}
