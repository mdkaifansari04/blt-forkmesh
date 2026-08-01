






#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"
#include "MirrorActionsConfiguration.h"
#include "MirrorActionsSummary.h"
#include "PrivateMirrorRuntime.h"
#include "PublicMirrorRuntime.h"

#include <QCryptographicHash>
#include <QSaveFile>

using namespace forkmesh::ui;



namespace {

QString actionRunLogPath(const ActionRun &run)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/actions/runs/") + run.repoKey() + QLatin1Char('/') +
           QString::number(run.id) + QStringLiteral("/log.txt");
}



const char kCommitSignalMarker[] =
    "# forkmesh-commit-signal: spools a commit event so mirrors are told to "
    "update instantly";

const QRegularExpression kExternalActionCommit(
    QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$"));
const QRegularExpression kExternalActionRef(
    QStringLiteral("^refs/heads/(?!/)(?!.*(?:\\.\\.|//))"
                   "[A-Za-z0-9._/-]{1,120}(?<!/)$"));

QString externalActionHeadKey(const RepositoryRecord &repo)
{
    return QStringLiteral("actions/externalHeads/") +
           QString::fromLatin1(
               QCryptographicHash::hash(
                   (repo.owner + QLatin1Char('/') + repo.name +
                    QLatin1Char('/') + repo.externalActionsRef)
                       .toUtf8(),
                   QCryptographicHash::Sha256)
                   .toHex());
}

QString gitCommitAt(const QString &repository, const QString &ref)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(
        QStringLiteral("git"),
        {QStringLiteral("--git-dir"), repository,
         QStringLiteral("rev-parse"), QStringLiteral("--verify"),
         ref + QStringLiteral("^{commit}")});
    if (!process.waitForStarted(2000) ||
        !process.waitForFinished(5000) ||
        process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        process.kill();
        return {};
    }
    const QString commit =
        QString::fromUtf8(process.readAllStandardOutput().left(256))
            .trimmed()
            .toLower();
    return kExternalActionCommit.match(commit).hasMatch()
               ? commit
               : QString();
}

bool fetchExternalActionRef(const RepositoryRecord &repo)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(
        QStringLiteral("git"),
        {QStringLiteral("--git-dir"), repo.mirrorPath,
         QStringLiteral("fetch"), QStringLiteral("--no-tags"),
         repo.externalActionsSource,
         QStringLiteral("+") + repo.externalActionsRef +
             QLatin1Char(':') + repo.externalActionsRef});
    if (!process.waitForStarted(2000) ||
        !process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

bool bindActionRepositoryState(ActionRun *run, const QString &mirror,
                               QString *error = nullptr)
{
    if (!run)
        return false;
    return ActionStore::repositoryStateDigest(
        mirror, run->commit, &run->repositoryTree, &run->executionDigest,
        error);
}

QString workflowContentAt(const QString &mirror, const QString &commit,
                          const QString &path)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QStringLiteral("git"),
                  {QStringLiteral("-C"), mirror, QStringLiteral("show"),
                   commit + QLatin1Char(':') + path});
    if (!process.waitForStarted(3000) ||
        !process.waitForFinished(30000) ||
        process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

}

void MainWindow::initActions()
{
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/actions");
    m_actionStore = new ActionStore(root);




    const int concurrency = qBound(2, QThread::idealThreadCount(), 4);
    for (int i = 0; i < concurrency; ++i) {
        auto *runner = new ActionRunner(m_actionStore, this);
        connect(runner, &ActionRunner::logLine, this, &MainWindow::onRunLog);
        connect(runner, &ActionRunner::statusChanged, this,
                &MainWindow::onRunStatusChanged);
        connect(runner, &ActionRunner::finished, this,
                &MainWindow::onRunFinished);
        connect(runner, &ActionRunner::releaseMetadataLanded, this,
                &MainWindow::onReleaseMetadataLanded);
        m_actionRunners.append(runner);
    }

    m_actionRuns = m_actionStore->loadAllRuns();


    for (int i = 0; i < m_actionRuns.size(); ++i) {
        ActionRun &run = m_actionRuns[i];
        if (run.status == ActionStatus::Running) {
            const qint64 interruptedAt = QDateTime::currentMSecsSinceEpoch();
            m_actionStore->appendLog(
                run,
                QString::fromUtf8(
                    "\n==> \xE2\x9A\xA0 INTERRUPTED: ForkMesh exited while this "
                    "run was still running. Check the main Log view (and stalls "
                    "log) for the app-side failure.\n"));
            run.status = ActionStatus::Failed;
            run.finishedAtMs = interruptedAt;
            m_actionStore->saveRun(run);
            logSystem(QStringLiteral(
                          "Actions: run #%1 \"%2\" for %3/%4 @ %5 was "
                          "interrupted because ForkMesh exited while it was "
                          "running. Run log: %6")
                          .arg(run.id)
                          .arg(run.workflowName, run.owner, run.name,
                               run.commit.left(8),
                               actionRunLogPath(run)));
        } else if (run.status == ActionStatus::Queued) {
            m_actionQueue.append(run.id);
        }
    }

    installAllPushHooks();

    m_actionSpoolWatcher = new QFileSystemWatcher(this);
    m_actionSpoolWatcher->addPath(m_actionStore->spoolDir());
    connect(m_actionSpoolWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &) { scanActionSpool(); });




    auto *poll = new QTimer(this);
    poll->setInterval(4000);
    connect(poll, &QTimer::timeout, this, &MainWindow::scanActionSpool);
    poll->start();


    scanActionSpool();
    processActionQueue();
    refreshActionsTable();
    updateNotificationButton();
}

void MainWindow::ensurePushHook(const RepositoryRecord &repo) const
{
    if (repo.previewOnly || repo.externallyManagedActions)
        return;
    if (!m_actionStore || repo.mirrorPath.isEmpty())
        return;
    if (!QDir(repo.mirrorPath).exists())
        return;
    const QString hooksDir = repo.mirrorPath + QStringLiteral("/hooks");
    QDir().mkpath(hooksDir);



    const QString spool = m_actionStore->spoolDir();
    const QString script = QStringLiteral(
        "#!/bin/sh\n"
        "spool='%1'\n"
        "mkdir -p \"$spool\"\n"
        "f=\"$spool/$(date +%s)-$$.push\"\n"
        "{\n"
        "  echo 'owner %2'\n"
        "  echo 'name %3'\n"
        "  echo 'mirror %4'\n"
        "  while read old new ref; do echo \"ref $old $new $ref\"; done\n"
        "} > \"$f.tmp\" && mv \"$f.tmp\" \"$f\"\n")
        .arg(spool, repo.owner, repo.name, repo.mirrorPath);

    QFile f(hooksDir + QStringLiteral("/post-receive"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(script.toUtf8());
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                     QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                     QFileDevice::ExeGroup | QFileDevice::ReadOther |
                     QFileDevice::ExeOther);









    if (!repo.localPath.trimmed().isEmpty() &&
        QFileInfo::exists(repo.localPath + QStringLiteral("/.git"))) {
        QByteArray current;
        runGitCapture(repo.localPath,
                      {QStringLiteral("remote"), QStringLiteral("get-url"),
                       QStringLiteral("--push"), QStringLiteral("origin")},
                      &current, nullptr);
        if (QString::fromUtf8(current).trimmed() != repo.mirrorPath)
            runGitCapture(repo.localPath,
                          {QStringLiteral("remote"), QStringLiteral("set-url"),
                           QStringLiteral("--push"), QStringLiteral("origin"),
                           repo.mirrorPath},
                          nullptr, nullptr);
    }

    ensureCommitSignalHook(repo);
}

void MainWindow::ensureCommitSignalHook(const RepositoryRecord &repo) const
{









    if (repo.previewOnly || !m_actionStore)
        return;
    const QString localPath = repo.localPath.trimmed();


    if (localPath.isEmpty() ||
        !QFileInfo(localPath + QStringLiteral("/.git")).isDir())
        return;
    const QString hooksDir = localPath + QStringLiteral("/.git/hooks");
    QDir().mkpath(hooksDir);

    const QString spool = m_actionStore->spoolDir();
    const QString script =
        QStringLiteral("#!/bin/sh\n"
                       "%1\n"
                       "spool='%2'\n"
                       "mkdir -p \"$spool\"\n"
                       "f=\"$spool/$(date +%s)-$$.commit\"\n"
                       "{\n"
                       "  echo 'owner %3'\n"
                       "  echo 'name %4'\n"
                       "} > \"$f.tmp\" && mv \"$f.tmp\" \"$f\"\n")
            .arg(QLatin1String(kCommitSignalMarker), spool, repo.owner,
                 repo.name);



    for (const char *hook : {"post-commit", "post-merge"}) {
        const QString path = hooksDir + QLatin1Char('/') + QLatin1String(hook);


        QFile existing(path);
        if (existing.exists()) {
            if (!existing.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QString body = QString::fromUtf8(existing.readAll());
            existing.close();
            if (!body.contains(QLatin1String(kCommitSignalMarker)))
                continue;
            if (body == script)
                continue;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            continue;
        f.write(script.toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                         QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                         QFileDevice::ExeGroup | QFileDevice::ReadOther |
                         QFileDevice::ExeOther);
    }
}

void MainWindow::removePushHook(const RepositoryRecord &repo) const
{
    if (!repo.externallyManagedActions && !repo.mirrorPath.isEmpty())
        QFile::remove(repo.mirrorPath + QStringLiteral("/hooks/post-receive"));


    const QString localPath = repo.localPath.trimmed();
    if (localPath.isEmpty())
        return;
    for (const char *hook : {"post-commit", "post-merge"}) {
        const QString path = localPath + QStringLiteral("/.git/hooks/") +
                             QLatin1String(hook);
        QFile f(path);
        if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString body = QString::fromUtf8(f.readAll());
        f.close();
        if (body.contains(QLatin1String(kCommitSignalMarker)))
            QFile::remove(path);
    }
}

void MainWindow::installAllPushHooks() const
{



    for (const RepositoryRecord &repo : m_repositories)
        if (!repo.previewOnly)
            ensurePushHook(repo);
}

int MainWindow::repoIndexFor(const QString &owner, const QString &name) const
{
    for (int i = 0; i < m_repositories.size(); ++i)
        if (!m_repositories.at(i).previewOnly &&
            m_repositories.at(i).owner == owner &&
            m_repositories.at(i).name == name)
            return i;
    return -1;
}

ActionRun *MainWindow::findRun(int runId)
{
    for (ActionRun &run : m_actionRuns)
        if (run.id == runId)
            return &run;
    return nullptr;
}

void MainWindow::scanActionSpool()
{
    if (!m_actionStore)
        return;
    syncMirrorActionsConfiguration();
    scanExternalActionsSources();
    QDir dir(m_actionStore->spoolDir());







    const QStringList commitFiles =
        dir.entryList({QStringLiteral("*.commit")}, QDir::Files, QDir::Name);
    QSet<int> propagated;
    for (const QString &file : commitFiles) {
        const QString full = dir.filePath(file);
        QFile f(full);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString text = QString::fromUtf8(f.readAll());
        f.close();

        QString owner, name;
        const QStringList lines =
            text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("owner ")))
                owner = line.mid(6).trimmed();
            else if (line.startsWith(QLatin1String("name ")))
                name = line.mid(5).trimmed();
        }
        const int idx = repoIndexFor(owner, name);
        if (idx < 0 ||
            m_repositories.at(idx).localPath.trimmed().isEmpty()) {
            QFile::remove(full);
            continue;
        }




        if (m_syncingRepos.contains(idx))
            continue;
        QFile::remove(full);
        if (propagated.contains(idx))
            continue;
        propagated.insert(idx);
        propagateRepoUpdate(idx);
    }

    const QStringList files =
        dir.entryList({QStringLiteral("*.push")}, QDir::Files, QDir::Name);


    QSet<int> reattested;
    for (const QString &file : files) {
        const QString full = dir.filePath(file);
        QFile f(full);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString text = QString::fromUtf8(f.readAll());
        f.close();
        QFile::remove(full);

        QString owner, name, ref, commit;
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("owner ")))
                owner = line.mid(6).trimmed();
            else if (line.startsWith(QLatin1String("name ")))
                name = line.mid(5).trimmed();
            else if (line.startsWith(QLatin1String("ref "))) {
                const QStringList p =
                    line.mid(4).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (p.size() >= 3 &&
                    p.at(2).startsWith(QLatin1String("refs/heads/"))) {
                    commit = p.at(1);
                    ref = p.at(2);
                }
            }
        }









        if (!owner.isEmpty() && !name.isEmpty()) {
            const int idx = repoIndexFor(owner, name);
            if (idx >= 0 && !reattested.contains(idx)) {
                reattested.insert(idx);
                const RepositoryRecord &r = m_repositories.at(idx);
                if (!r.previewOnly && r.publishToNetwork &&
                    !r.mirrorPath.trimmed().isEmpty())
                    publishRepository(idx, false);








                if (!r.previewOnly && m_backend)
                    m_backend->notifyMirrorUpdated(
                        catalogOwner(r) + "/" +
                            repoSegment(r.name, QStringLiteral("repository")),
                        commit);





                pushToSshMirrorRemotes(idx);
            }
        }

        if (owner.isEmpty() || name.isEmpty() || commit.isEmpty())
            continue;
        if (commit.count(QLatin1Char('0')) == commit.size())
            continue;
        enqueuePushEvent(owner, name, commit, ref);
    }
    processActionQueue();
    updateMirrorActionsRuntimeState();
}

void MainWindow::syncMirrorActionsConfiguration()
{
    QSettings settings;
    const QString generation =
        settings
            .value(QString::fromLatin1(
                forkmesh::mirror_actions::kGenerationSetting))
            .toString()
            .trimmed();
    if (generation.isEmpty() ||
        generation == m_mirrorActionsConfigGeneration)
        return;
    static const QRegularExpression validGeneration(
        QStringLiteral("^[a-f0-9]{32}$"));
    if (!validGeneration.match(generation).hasMatch())
        return;





    loadRepositories();
    installAllPushHooks();
    const bool enabled =
        settings
            .value(QString::fromLatin1(
                       forkmesh::mirror_actions::kEnabledSetting),
                   false)
            .toBool();
    for (RepositoryRecord &repo : m_repositories) {
        if (repo.externallyManagedActions)
            repo.actionsEnabled = enabled;
    }
    m_mirrorActionsConfigGeneration = generation;
    m_mirrorActionsRuntimeState.clear();
    m_mirrorActionsRuntimeStateWrittenAtMs = 0;
    m_mirrorActionsSummaryAttemptedAtMs = 0;
    scheduleMirrorActionsSummary(0);
    logSystem(
        QStringLiteral("Actions: mirror executor configuration %1.")
            .arg(enabled ? QStringLiteral("enabled")
                         : QStringLiteral("disabled")));
}

void MainWindow::scanExternalActionsSources()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastExternalActionsScanMs < 3500)
        return;
    m_lastExternalActionsScanMs = now;
    QSettings settings;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (!repo.externallyManagedActions || !repo.actionsEnabled ||
            repo.previewOnly ||
            !QFileInfo(repo.externalActionsSource).isDir() ||
            QFileInfo(repo.externalActionsSource).isSymLink() ||
            !QFileInfo(repo.mirrorPath).isDir() ||
            QFileInfo(repo.mirrorPath).isSymLink() ||
            !kExternalActionRef.match(repo.externalActionsRef).hasMatch()) {
            continue;
        }
        const QString sourceCommit =
            gitCommitAt(repo.externalActionsSource,
                        repo.externalActionsRef);
        if (sourceCommit.isEmpty())
            continue;
        const QString key = externalActionHeadKey(repo);
        const QString previous =
            settings.value(key).toString().trimmed().toLower();
        if (previous.isEmpty()) {


            settings.setValue(key, sourceCommit);
            continue;
        }
        if (previous == sourceCommit)
            continue;
        if (!fetchExternalActionRef(repo) ||
            gitCommitAt(repo.mirrorPath, repo.externalActionsRef) !=
                sourceCommit) {
            continue;
        }
        settings.setValue(key, sourceCommit);
        enqueuePushEvent(repo.owner, repo.name, sourceCommit,
                         repo.externalActionsRef);
    }
    settings.sync();
}

void MainWindow::updateMirrorActionsRuntimeState()
{
    if (m_mirrorActionsConfigGeneration.isEmpty())
        return;
    QSettings settings;
    const bool enabled =
        settings
            .value(QString::fromLatin1(
                       forkmesh::mirror_actions::kEnabledSetting),
                   false)
            .toBool();
    bool running = false;
    for (const ActionRunner *runner : std::as_const(m_actionRunners)) {
        if (runner && runner->busy()) {
            running = true;
            break;
        }
    }
    const QString state =
        !enabled ? QStringLiteral("disabled")
                 : running ? QStringLiteral("running")
                           : QStringLiteral("enabled");
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_mirrorActionsSummaryAttemptedAtMs >=
        3 * 60 * 1000) {
        scheduleMirrorActionsSummary(0);
    }
    const bool changed = state != m_mirrorActionsRuntimeState;
    if (!changed &&
        now - m_mirrorActionsRuntimeStateWrittenAtMs < 3 * 60 * 1000)
        return;
    const QString path =
        settings.value(QStringLiteral("actions/mirrorStatePath"))
            .toString()
            .trimmed();
    const QString node =
        settings
            .value(QString::fromLatin1(
                forkmesh::mirror_actions::kNodeSetting))
            .toString()
            .trimmed();
    if (!forkmesh::mirror_actions::writeStateFile(
            path, node, state, now))
        return;
    m_mirrorActionsRuntimeState = state;
    m_mirrorActionsRuntimeStateWrittenAtMs = now;

#if defined(Q_OS_UNIX)




    if (changed) {
        QProcess::startDetached(
            QStringLiteral("/usr/bin/systemctl"),
            {QStringLiteral("--no-block"), QStringLiteral("start"),
             QStringLiteral("forkmesh-mirror-renew.service")});
    }
#endif
}

void MainWindow::scheduleMirrorActionsSummary(int delayMs)
{
    if (m_mirrorActionsConfigGeneration.isEmpty() || !m_actionStore)
        return;
    if (!m_mirrorActionsSummaryTimer) {
        m_mirrorActionsSummaryTimer = new QTimer(this);
        m_mirrorActionsSummaryTimer->setSingleShot(true);
        connect(m_mirrorActionsSummaryTimer, &QTimer::timeout, this,
                &MainWindow::writeMirrorActionsSummary);
    }
    const int boundedDelay = qBound(0, delayMs, 3000);
    if (m_mirrorActionsSummaryTimer->isActive()) {
        const int remaining = m_mirrorActionsSummaryTimer->remainingTime();
        if (remaining >= 0 && remaining <= boundedDelay)
            return;
    }
    m_mirrorActionsSummaryTimer->start(boundedDelay);
}

void MainWindow::writeMirrorActionsSummary()
{
    if (m_mirrorActionsConfigGeneration.isEmpty() || !m_actionStore)
        return;
    QSettings settings;
    const QString path =
        settings
            .value(QString::fromLatin1(
                forkmesh::mirror_actions::kSummaryPathSetting))
            .toString()
            .trimmed();
    const QString node =
        settings
            .value(QString::fromLatin1(
                forkmesh::mirror_actions::kNodeSetting))
            .toString()
            .trimmed();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_mirrorActionsSummaryAttemptedAtMs = now;
    forkmesh::mirror_actions::writeSummaryFile(
        path, node, m_actionRuns, *m_actionStore, now);
}

void MainWindow::enqueuePushEvent(const QString &owner, const QString &name,
                                  const QString &commit, const QString &ref)
{
    const int repoIndex = repoIndexFor(owner, name);
    if (repoIndex < 0)
        return;
    const RepositoryRecord repo = m_repositories.at(repoIndex);


    const QString branch = ref.startsWith(QLatin1String("refs/heads/"))
                               ? ref.mid(11)
                               : ref;
    QString subject;
    {
        QProcess s;
        s.start(QStringLiteral("git"),
                {QStringLiteral("-C"), repo.mirrorPath, QStringLiteral("show"),
                 QStringLiteral("-s"), QStringLiteral("--format=%s"), commit});
        s.waitForFinished(5000);
        subject = QString::fromUtf8(s.readAllStandardOutput()).trimmed();
    }


    logSystem(QString::fromUtf8("Push to %1/%2 on %3 \xE2\x86\x92 %4%5")
                  .arg(owner, name, branch, commit.left(8),
                       subject.isEmpty()
                           ? QString()
                           : QString::fromUtf8(" \xE2\x80\x94 ") + subject));



    if (QSettings().value(kPushAlertSetting, false).toBool()) {
        const QString body =
            QString::fromUtf8("%1/%2 \xC2\xB7 %3 \xC2\xB7 %4%5")
                .arg(owner, name, branch, commit.left(8),
                     subject.isEmpty() ? QString()
                                       : QStringLiteral("\n") + subject);
        postNotification(QStringLiteral("Push received"), body,
                         false, QStringLiteral("emblem-synchronizing"));
    }





    if (repoIndex == m_repoDetailIndex)
        scheduleOpenRepoDetailRefresh();

    if (!repo.actionsEnabled)
        return;

    queueWorkflowsForCommit(repoIndex, owner, name, commit, ref);
}





void MainWindow::queueWorkflowsForCommit(int repoIndex, const QString &owner,
                                         const QString &name, const QString &commit,
                                         const QString &ref,
                                         WorkflowTrigger trigger)
{
    if (repoIndex < 0 || repoIndex >= m_repositories.size() || !m_actionStore)
        return;




    const RepositoryRecord repo = m_repositories.at(repoIndex);





    if (!QDir(repo.mirrorPath).exists()) {
        logSystem(QStringLiteral(
                      "Actions: served mirror %1 for %2/%3 is missing \xE2\x80\x94 "
                      "cannot look up workflows at %4.")
                      .arg(repo.mirrorPath, owner, name, commit.left(8)));
        return;
    }





    if (trigger == WorkflowTrigger::Push) {
        QProcess names;
        names.start(QStringLiteral("git"),
                    {QStringLiteral("-C"), repo.mirrorPath,
                     QStringLiteral("diff-tree"), QStringLiteral("--no-commit-id"),
                     QStringLiteral("--name-only"), QStringLiteral("-r"), commit});
        names.waitForFinished(10000);
        const QStringList changed =
            QString::fromUtf8(names.readAllStandardOutput())
                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const auto isMetadataPath = [](const QString &p) {
            return p.startsWith(QLatin1String(".forkmesh/issues/")) ||
                   p.startsWith(QLatin1String("pulls/")) ||
                   p.startsWith(QLatin1String(".forkmesh/commits/"));
        };
        if (!changed.isEmpty() &&
            std::all_of(changed.cbegin(), changed.cend(), isMetadataPath)) {
            logSystem(QString::fromUtf8(
                          "Actions: %1/%2 @ %3 only touches issues/PRs \xE2\x80\x94 "
                          "skipping workflows.")
                          .arg(owner, name, commit.left(8)));
            return;
        }
    }


    QProcess ls;
    ls.start(QStringLiteral("git"),
             {QStringLiteral("-C"), repo.mirrorPath, QStringLiteral("ls-tree"),
              QStringLiteral("-r"), QStringLiteral("--name-only"), commit,
              QStringLiteral("--"), QStringLiteral(".forkmesh")});
    ls.waitForFinished(10000);
    const QStringList paths = QString::fromUtf8(ls.readAllStandardOutput())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    bool added = false;
    for (const QString &path : paths) {
        if (!(path.endsWith(QLatin1String(".yml")) ||
              path.endsWith(QLatin1String(".yaml"))))
            continue;
        QProcess show;
        show.start(QStringLiteral("git"),
                   {QStringLiteral("-C"), repo.mirrorPath, QStringLiteral("show"),
                    commit + QLatin1Char(':') + path});
        show.waitForFinished(10000);
        if (show.exitCode() != 0)
            continue;
        const QString content = QString::fromUtf8(show.readAllStandardOutput());
        const ActionWorkflow wf = ActionFile::parse(path, content);
        const bool matchesTrigger = trigger == WorkflowTrigger::Release
                                        ? wf.triggersOnRelease()
                                        : wf.triggersOnPush();
        if (!wf.valid || !matchesTrigger)
            continue;
        if (repo.disabledWorkflows.contains(path)) {
            logSystem(QString::fromUtf8("Actions: \xE2\x80\x9C%1\xE2\x80\x9D is "
                                        "disabled for %2/%3 \xE2\x80\x94 skipping.")
                          .arg(wf.name, owner, name));
            continue;
        }



        if (!workflowRunsOnThisNode(repo, wf)) {
            logSystem(QString::fromUtf8(
                          "Actions: \xE2\x80\x9C%1\xE2\x80\x9D is dedicated to "
                          "%2 \xE2\x80\x94 this node (%3) is skipping it.")
                          .arg(wf.name, workflowDedicationLabel(repo, wf),
                               actionNodeLabels().join(QStringLiteral(", "))));
            continue;
        }

        ActionRun run;
        run.owner = owner;
        run.name = name;
        run.workflowPath = path;
        run.workflowName = wf.name;
        run.workflowContent = content;
        run.commit = commit;
        run.ref = ref;
        QString snapshotError;
        if (!bindActionRepositoryState(&run, repo.mirrorPath,
                                       &snapshotError)) {
            logSystem(QStringLiteral(
                          "Actions: could not bind \"%1\" to repository state "
                          "for %2/%3 @ %4: %5")
                          .arg(wf.name, owner, name, commit.left(8),
                               snapshotError));
            continue;
        }



        const bool approved = ActionStore::isApproved(
            run.repoKey(), path, content, run.repositoryTree,
            run.executionDigest);
        run.status =
            approved ? ActionStatus::Queued : ActionStatus::AwaitingApproval;

        const ActionRun created = m_actionStore->createRun(run);
        m_actionRuns.prepend(created);
        scheduleMirrorActionsSummary(0);
        cancelSupersededRuns(created);
        if (approved)
            m_actionQueue.append(created.id);
        else
            addNotification(QStringLiteral("Action waiting for approval"),
                            QString::fromUtf8("%1 \xC2\xB7 %2/%3 at %4")
                                .arg(wf.name, owner, name, commit.left(8)),
                            false, created.id);
        added = true;
        logSystem(QStringLiteral("Actions: %1 \"%2\" for %3/%4 @ %5")
                      .arg(approved ? QStringLiteral("queued")
                                    : QStringLiteral("awaiting approval of"),
                           wf.name, owner, name, commit.left(8)));
    }
    if (added) {


        refreshActionsTable();
        if (m_actionWorkflowList && repoIndex == m_repoDetailIndex)
            refreshRepoActions();
        updateNotificationButton();
        processActionQueue();
    } else {
        const QString event = trigger == WorkflowTrigger::Release
                                  ? QStringLiteral("release")
                                  : QStringLiteral("push");
        logSystem(QString::fromUtf8(
                      "Actions: no .forkmesh/ workflow with 'on: %1' at %2 for "
                      "%3/%4 \xE2\x80\x94 nothing to run.")
                      .arg(event, commit.left(8), owner, name));
    }
}

void MainWindow::scheduleOpenRepoDetailRefresh()
{
    if (!m_openRepoRefreshTimer) {
        m_openRepoRefreshTimer = new QTimer(this);
        m_openRepoRefreshTimer->setSingleShot(true);
        m_openRepoRefreshTimer->setInterval(300);
        connect(m_openRepoRefreshTimer, &QTimer::timeout, this,
                &MainWindow::refreshOpenRepoDetail);
    }
    m_openRepoRefreshTimer->start();
}

void MainWindow::refreshOpenRepoDetail()
{
    if (m_openRepoRefreshTimer)
        m_openRepoRefreshTimer->stop();
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;






    if (m_heavyRefreshInFlight) {
        scheduleOpenRepoDetailRefresh();
        return;
    }
    const ScopedFlag refreshGuard(m_heavyRefreshInFlight);





    QPointer<QWidget> typingFocus;
    if (QWidget *focused = QApplication::focusWidget()) {
        if ((qobject_cast<QLineEdit *>(focused) ||
             qobject_cast<QPlainTextEdit *>(focused) ||
             qobject_cast<QTextEdit *>(focused)) &&
            focused->window() == this)
            typingFocus = focused;
    }





    const int visibleTab =
        m_repoDetailStack ? m_repoDetailStack->currentIndex() : -1;







    const bool refsJustArrived = m_repoBranch.isEmpty();
    if (refsJustArrived || visibleTab == 0 ||
        (m_branchesTabIndex >= 0 && visibleTab == m_branchesTabIndex))
        loadBranchesAndTags();
    if (m_commitsTable && m_commitsTable->isVisibleTo(this))
        loadCommits();
    if (visibleTab == 3)
        reloadAgents();
    if (refsJustArrived)
        refreshRepoTabCounts();
    else if (visibleTab == 2)
        reloadIssuesInBackground();


    reloadPullsInBackground();


    if (visibleTab == 6)
        refreshRepoActions();


    updateRepoDetailStatus();
    updateRepoActionMenus();
    updateRepoCodeSize();
    refreshRepoSyncIndicators();
    refreshRepoChangeBadge();
    refreshRepoPinBanner();
    m_treeLoadedForIndex = -1;
    if (m_filesStack && m_filesStack->currentIndex() == 2)
        loadCoveExplorer();
    if (visibleTab == 0)
        loadRepoOverview(m_overviewPath);


    if (m_mirrorNodesTable && m_mirrorNodesTable->isVisible())
        loadMirrorNodesPanel();



    if (typingFocus && typingFocus->isVisibleTo(this) &&
        typingFocus->isEnabled() && QApplication::focusWidget() != typingFocus)
        typingFocus->setFocus(Qt::OtherFocusReason);
}

QStringList MainWindow::actionNodeLabels() const
{
    QSettings settings;
    return ActionFile::nodeLabels(
        machineNodeName(),
        settings
            .value(QString::fromLatin1(
                forkmesh::mirror_actions::kNodeSetting))
            .toString(),
        settings.value(QString::fromLatin1(kActionNodeLabelsSetting))
            .toString());
}

QString MainWindow::workflowNodePin(const RepositoryRecord &repo,
                                    const QString &path) const
{
    return ActionFile::pinnedNode(repo.workflowNodes, path);
}

QStringList
MainWindow::workflowRunsOnLabels(const RepositoryRecord &repo,
                                 const ActionWorkflow &workflow) const
{


    const QString pin = workflowNodePin(repo, workflow.path);
    if (!pin.isEmpty())
        return QStringList{pin};
    return workflow.runsOn;
}

bool MainWindow::workflowRunsOnThisNode(const RepositoryRecord &repo,
                                        const ActionWorkflow &workflow) const
{
    ActionWorkflow probe;
    probe.runsOn = workflowRunsOnLabels(repo, workflow);
    return probe.runsOnNode(actionNodeLabels());
}

QString MainWindow::workflowDedicationLabel(const RepositoryRecord &repo,
                                            const ActionWorkflow &workflow) const
{
    return workflowRunsOnLabels(repo, workflow).join(QStringLiteral(", "));
}

QStringList MainWindow::actionNodeCandidates() const
{
    QStringList out;
    const auto add = [&out](const QString &raw) {
        const QString node = raw.trimmed();
        if (node.isEmpty() || out.contains(node, Qt::CaseInsensitive))
            return;
        out.append(node);
    };
    add(machineNodeName());
    for (const MemberInfo &node : m_homeRoster)
        add(node.nodeName);


    for (const ActionWorkflow &wf : m_repoWorkflows) {
        for (const QString &label : wf.runsOn) {
            if (label.compare(QLatin1String("any"), Qt::CaseInsensitive) != 0)
                add(label);
        }
    }
    return out;
}

void MainWindow::saveActionNodeLabels(const QString &labels)
{
    const QStringList clean = ActionFile::parseLabelList(labels);
    QSettings settings;
    if (clean.isEmpty())
        settings.remove(QString::fromLatin1(kActionNodeLabelsSetting));
    else
        settings.setValue(QString::fromLatin1(kActionNodeLabelsSetting),
                          clean.join(QStringLiteral(", ")));
    logSystem(QStringLiteral("Actions: this node answers to %1.")
                  .arg(actionNodeLabels().join(QStringLiteral(", "))));

    if (m_actionWorkflowList)
        refreshRepoActions();
}

std::shared_ptr<void> MainWindow::pinActionMirror(const RepositoryRecord &repo,
                                                  QString *mirrorPath) const
{
    const auto publicPin =
        m_publicMirrorMaterializations.value(repo.publicArchiveId);
    if (publicPin && publicPin->isValid()) {
        if (mirrorPath)
            *mirrorPath = publicPin->repositoryPath();
        return publicPin;
    }
    const auto privatePin =
        m_privateMirrorMaterializations.value(repo.privateReplicaId);
    if (privatePin && privatePin->isValid()) {
        if (mirrorPath)
            *mirrorPath = privatePin->repositoryPath();
        return privatePin;
    }
    return {};
}

void MainWindow::releaseActionMirrorPin(int runId)
{
    const ActionMirrorPin pin = m_actionMirrorPins.take(runId);
    if (!pin.materialization)
        return;




    const int index = repoIndexFor(pin.owner, pin.name);
    if (index < 0)
        return;
    const int carried =
        carryMirrorReleaseCas(pin.path, m_repositories.at(index).mirrorPath);
    if (carried > 0)
        logSystem(QStringLiteral(
                      "Actions: moved %1 release artifact blob(s) from run #%2 "
                      "into the mirror now serving %3/%4.")
                      .arg(carried)
                      .arg(runId)
                      .arg(pin.owner, pin.name));
}

ActionNeeds::State MainWindow::actionRunNeedsState(int runId, QString *detail)
{
    const ActionRun *run = findRun(runId);
    if (!run)
        return ActionNeeds::State::Ready;
    const ActionWorkflow wf =
        ActionFile::parse(run->workflowPath, run->workflowContent);
    if (wf.needs.isEmpty())
        return ActionNeeds::State::Ready;
    return ActionNeeds::resolve(*run, wf.needs, m_actionRuns, detail);
}

void MainWindow::noteActionRunWaiting(int runId, const QString &detail)
{
    if (m_actionWaitingRuns.contains(runId))
        return;
    m_actionWaitingRuns.insert(runId);
    const ActionRun *run = findRun(runId);
    if (!run)
        return;
    logSystem(QString::fromUtf8(
                  "Actions: \xE2\x80\x9C%1\xE2\x80\x9D for %2/%3 @ %4 is "
                  "waiting \xE2\x80\x94 %5.")
                  .arg(run->workflowName, run->owner, run->name,
                       run->commit.left(8), detail));
}

void MainWindow::processActionQueue()
{
    if (m_actionRunners.isEmpty())
        return;


    if (!m_actionWaitingRuns.isEmpty()) {
        const QSet<int> queued(m_actionQueue.cbegin(), m_actionQueue.cend());
        m_actionWaitingRuns.intersect(queued);
    }


    while (!m_actionQueue.isEmpty()) {
        ActionRunner *idle = nullptr;
        for (ActionRunner *runner : m_actionRunners) {
            if (!runner->busy()) {
                idle = runner;
                break;
            }
        }
        if (!idle)
            return;




        int queueIndex = -1;
        ActionNeeds::State needsState = ActionNeeds::State::Ready;
        QString needsDetail;
        for (int i = 0; i < m_actionQueue.size(); ++i) {
            QString detail;
            const ActionNeeds::State state =
                actionRunNeedsState(m_actionQueue.at(i), &detail);
            if (state == ActionNeeds::State::Waiting) {
                noteActionRunWaiting(m_actionQueue.at(i), detail);
                continue;
            }
            queueIndex = i;
            needsState = state;
            needsDetail = detail;
            break;
        }
        if (queueIndex < 0)
            return;
        const int runId = m_actionQueue.takeAt(queueIndex);
        m_actionWaitingRuns.remove(runId);
        ActionRun *run = findRun(runId);
        if (!run || run->status != ActionStatus::Queued)
            continue;
        if (needsState == ActionNeeds::State::Blocked) {
            run->status = ActionStatus::Skipped;
            run->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
            m_actionStore->saveRun(*run);
            scheduleMirrorActionsSummary(0);
            logSystem(QString::fromUtf8(
                          "Actions: skipped \xE2\x80\x9C%1\xE2\x80\x9D for "
                          "%2/%3 @ %4 \xE2\x80\x94 it needs %5.")
                          .arg(run->workflowName, run->owner, run->name,
                               run->commit.left(8), needsDetail));
            continue;
        }
        const int repoIndex = repoIndexFor(run->owner, run->name);
        if (repoIndex < 0) {
            run->status = ActionStatus::Failed;
            m_actionStore->saveRun(*run);
            scheduleMirrorActionsSummary(0);
            continue;
        }
        QString mirror = m_repositories.at(repoIndex).mirrorPath;
        const QString workTree = m_repositories.at(repoIndex).localPath;





        const std::shared_ptr<void> mirrorPin =
            pinActionMirror(m_repositories.at(repoIndex), &mirror);
        ActionRun verified = *run;
        QString snapshotError;
        const QString repositoryWorkflow =
            workflowContentAt(mirror, verified.commit,
                              verified.workflowPath);
        if (repositoryWorkflow.isNull() ||
            repositoryWorkflow != verified.workflowContent ||
            !bindActionRepositoryState(&verified, mirror, &snapshotError) ||
            verified.repositoryTree != run->repositoryTree ||
            verified.executionDigest != run->executionDigest ||
            !ActionStore::isApproved(
                verified.repoKey(), verified.workflowPath,
                verified.workflowContent, verified.repositoryTree,
                verified.executionDigest)) {



            run->status = ActionStatus::AwaitingApproval;
            m_actionStore->saveRun(*run);
            scheduleMirrorActionsSummary(0);
            logSystem(QStringLiteral(
                          "Actions: blocked \"%1\" for %2/%3 @ %4 because its "
                          "approved repository state could not be reverified%5.")
                          .arg(run->workflowName, run->owner, run->name,
                               run->commit.left(8),
                               snapshotError.isEmpty()
                                   ? QString()
                                   : QStringLiteral(": ") + snapshotError));
            continue;
        }
        const ActionWorkflow wf =
            ActionFile::parse(run->workflowPath, run->workflowContent);
        if (!wf.valid) {
            run->status = ActionStatus::Failed;
            m_actionStore->saveRun(*run);
            scheduleMirrorActionsSummary(0);
            continue;
        }



        if (!workflowRunsOnThisNode(m_repositories.at(repoIndex), wf)) {
            run->status = ActionStatus::Skipped;
            m_actionStore->saveRun(*run);
            scheduleMirrorActionsSummary(0);
            logSystem(QString::fromUtf8(
                          "Actions: skipped \xE2\x80\x9C%1\xE2\x80\x9D for "
                          "%2/%3 \xE2\x80\x94 it is dedicated to %4.")
                          .arg(run->workflowName, run->owner, run->name,
                               workflowDedicationLabel(
                                   m_repositories.at(repoIndex), wf)));
            continue;
        }


        const ActionRun snapshot = *run;
        if (mirrorPin)
            m_actionMirrorPins.insert(
                runId, {mirrorPin, mirror, snapshot.owner, snapshot.name});
        idle->start(snapshot, wf, mirror, workTree, ActionStore::variables());
    }
}

ActionRunner *MainWindow::runnerForRun(int runId) const
{
    for (ActionRunner *runner : m_actionRunners)
        if (runner->currentRunId() == runId)
            return runner;
    return nullptr;
}

void MainWindow::cancelSupersededRuns(const ActionRun &newRun)
{








    QList<int> supersededIds;
    for (const ActionRun &run : m_actionRuns) {
        if (run.id == newRun.id || run.owner != newRun.owner ||
            run.name != newRun.name || run.workflowPath != newRun.workflowPath)
            continue;
        if (run.status == ActionStatus::Running ||
            run.status == ActionStatus::Queued ||
            run.status == ActionStatus::AwaitingApproval)
            supersededIds.append(run.id);
    }

    for (int runId : supersededIds) {
        const ActionRun *found = findRun(runId);
        if (!found)
            continue;
        const ActionRun run = *found;
        if (run.status == ActionStatus::Running) {
            if (ActionRunner *runner = runnerForRun(run.id)) {
                logSystem(QStringLiteral(
                              "Actions: aborting \"%1\" for %2/%3 @ %4 \xE2\x80\x94 "
                              "superseded by a newer run of the same workflow.")
                              .arg(run.workflowName, run.owner, run.name,
                                   run.commit.left(8)));
                runner->stop();
            }
        } else if (run.status == ActionStatus::Queued ||
                   run.status == ActionStatus::AwaitingApproval) {
            const bool wasPending = run.status == ActionStatus::AwaitingApproval;
            m_actionQueue.removeAll(run.id);
            if (ActionRun *live = findRun(runId)) {
                live->status = ActionStatus::Cancelled;
                live->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
                m_actionStore->saveRun(*live);
                scheduleMirrorActionsSummary(0);
            }
            logSystem(QStringLiteral(
                          "Actions: cancelled %1 \"%2\" for %3/%4 @ %5 \xE2\x80\x94 "
                          "superseded by a newer run of the same workflow.")
                          .arg(wasPending ? QStringLiteral("pending")
                                          : QStringLiteral("queued"),
                               run.workflowName, run.owner, run.name,
                               run.commit.left(8)));
        }
    }
}

void MainWindow::onRunLog(int runId, const QString &text)
{



    scheduleMirrorActionsSummary(2000);
    if (runId != m_selectedRunId || !m_actionLog)
        return;
    m_actionLog->moveCursor(QTextCursor::End);
    m_actionLog->insertPlainText(displaySafePlainLog(text));
    m_actionLog->moveCursor(QTextCursor::End);
}

void MainWindow::onRunStatusChanged(int runId, const QString &status)
{
    m_actionRuns = m_actionStore->loadAllRuns();
    scheduleMirrorActionsSummary(0);
    refreshActionsTable();
    refreshCommitStatusGlyphs();
    if (runId == m_selectedRunId && m_actionRunMeta) {
        if (const ActionRun *run = findRun(runId)) {
            m_actionRunMeta->setText(
                QString::fromUtf8("%1/%2 \xC2\xB7 %3 \xC2\xB7 %4")
                    .arg(run->owner, run->name, run->commit.left(8),
                         actionStatusText(status)));
        }
    }
    updateNotificationButton();
    refreshOpenPullChecks();
    if (status == ActionStatus::Running) {
        if (const ActionRun *run = findRun(runId))
            notifyActionEvent(QStringLiteral("Action started"),
                              QString::fromUtf8("%1 \xC2\xB7 %2/%3")
                                  .arg(run->workflowName, run->owner, run->name),
                              false);
    }
    updateMirrorActionsRuntimeState();
}

void MainWindow::onRunFinished(int runId, bool ok)
{



    releaseActionMirrorPin(runId);
    m_actionRuns = m_actionStore->loadAllRuns();
    scheduleMirrorActionsSummary(0);
    refreshActionsTable();
    refreshCommitStatusGlyphs();
    updateNotificationButton();
    if (const ActionRun *run = findRun(runId)) {
        const bool cancelled = run->status == ActionStatus::Cancelled;
        const QString title = ok ? QStringLiteral("Action succeeded")
                                 : cancelled ? QStringLiteral("Action stopped")
                                             : QStringLiteral("Action failed");
        logSystem(QStringLiteral("Actions: %1 run #%2 \"%3\" for %4/%5 @ %6. "
                                 "Run log: %7")
                      .arg(ok ? QStringLiteral("succeeded")
                              : cancelled ? QStringLiteral("stopped")
                                          : QStringLiteral("failed"))
                      .arg(run->id)
                      .arg(run->workflowName, run->owner, run->name,
                           run->commit.left(8), actionRunLogPath(*run)));
        notifyActionEvent(title,
                          QString::fromUtf8("%1 \xC2\xB7 %2/%3")
                              .arg(run->workflowName, run->owner, run->name),
                          !ok && !cancelled);
        if (!ok && !cancelled)
            maybeAutoFixFailedRun(*run);
    }
    if (runId == m_selectedRunId)
        showRun(runId);
    refreshOpenPullChecks();
    processActionQueue();
    updateMirrorActionsRuntimeState();
}

void MainWindow::onReleaseMetadataLanded(int runId)
{
    const ActionRun *run = findRun(runId);
    if (!run)
        return;
    const int index = repoIndexFor(run->owner, run->name);
    if (index < 0)
        return;




    logSystem(QStringLiteral(
                  "Release: refreshing artifact metadata for %1/%2 online.")
                  .arg(run->owner, run->name));
    publishRepositoryAfterMirrorRefresh(index,  false);


    if (index == m_repoDetailIndex && m_releasesTabIndex >= 0 &&
        m_repoDetailStack &&
        m_repoDetailStack->currentIndex() == m_releasesTabIndex)
        loadReleasesPanel();
    refreshRepoSyncIndicators();
}



void MainWindow::refreshOpenPullChecks()
{
    if (m_currentPullNumber < 0)
        return;
    for (const PullRequest &it : std::as_const(m_currentPulls)) {
        if (it.number != m_currentPullNumber)
            continue;




        const PullRequest pr = it;
        renderPullChecks(pr);
        renderPullChecksSummary(pr);
        renderPullReviewSummary(pr);
        updatePullSubTabCounts(pr);
        return;
    }
}

void MainWindow::notifyActionEvent(const QString &title, const QString &body,
                                   bool warning)
{
    addNotification(title, body, warning);



    const QString mode = actionAlertMode();
    if (mode == QLatin1String("none"))
        return;
    if (mode == QLatin1String("failed") && !warning)
        return;
    const QString icon = warning ? QStringLiteral("dialog-error")
                         : title.contains("started")
                             ? QStringLiteral("system-run")
                             : QStringLiteral("emblem-default");
    postNotification(title, body, warning, icon);
}

void MainWindow::addNotification(const QString &title, const QString &body,
                                 bool warning, int runId)
{
    AppNotification item;
    item.title = title;
    item.body = body;
    item.warning = warning;
    item.runId = runId;
    if (runId > 0)
        item.kind = QStringLiteral("action");
    recordNotification(item);
}

void MainWindow::addNotification(const QString &title, const QString &body,
                                 bool warning, const NotificationLink &link)
{
    addNotification(title, body, warning, link, QString(), QString());
}

void MainWindow::addNotification(const QString &title, const QString &body,
                                 bool warning, const NotificationLink &link,
                                 const QString &kind, const QString &actor)
{
    AppNotification item;
    item.title = title;
    item.body = body;
    item.warning = warning;
    item.link = link;
    item.kind = kind;
    item.actor = actor;
    recordNotification(item);
}





void MainWindow::recordNotification(AppNotification item)
{
    item.id = m_nextNotificationId++;
    if (item.timestampMs <= 0)
        item.timestampMs = QDateTime::currentMSecsSinceEpoch();
    if (item.kind.isEmpty())
        item.kind = item.link.isValid() ? item.link.kind
                                        : QStringLiteral("desktop");
    if (item.repo.isEmpty() && !item.link.owner.isEmpty())
        item.repo = item.link.owner + QLatin1Char('/') + item.link.name;
    m_notifications.prepend(item);
    while (m_notifications.size() > 100)
        m_notifications.removeLast();
    updateNotificationButton();
    flashNotification(item);
    refreshLogEventList();

    if (m_notificationsTable && m_sectionStack &&
        m_sectionStack->currentIndex() == 3)
        refreshNotificationsTable();
}







void MainWindow::flashNotification(const AppNotification &item)
{
    QString text = item.title.simplified();
    const QString detail = item.body.simplified();
    if (!detail.isEmpty())
        text += QString::fromUtf8(" \xE2\x80\x94 ") + detail;
    if (text.isEmpty())
        return;
    if (item.warning) {
        flashMessage(text, true);
        flashErrorBorder();
    } else if (topMessageBusy()) {
        queueTopMessage(text, false);
    } else {
        flashMessage(text, false);
    }
}





void MainWindow::flashErrorBorder()
{
    if (!m_errorBorderOverlay) {


        class ErrorBorderWidget : public QWidget
        {
        public:
            explicit ErrorBorderWidget(QWidget *parent) : QWidget(parent)
            {
                setAttribute(Qt::WA_TransparentForMouseEvents);
                setAttribute(Qt::WA_NoSystemBackground);
                setAttribute(Qt::WA_TranslucentBackground);
            }

        protected:
            void paintEvent(QPaintEvent *) override
            {
                QPainter painter(this);
                painter.setRenderHint(QPainter::Antialiasing, false);


                QPen pen(QColor(248, 81, 73, 184), 3);
                pen.setJoinStyle(Qt::MiterJoin);
                painter.setPen(pen);
                painter.drawRect(rect().adjusted(1, 1, -2, -2));
                for (int step = 1; step <= 6; ++step) {
                    const int inset = 2 + step * 3;
                    QPen glow(QColor(248, 81, 73, 56 - step * 8), 3);
                    glow.setJoinStyle(Qt::MiterJoin);
                    painter.setPen(glow);
                    painter.drawRect(
                        rect().adjusted(inset, inset, -inset - 1, -inset - 1));
                }
            }
        };
        m_errorBorderOverlay = new ErrorBorderWidget(this);
        m_errorBorderTimer = new QTimer(this);
        m_errorBorderTimer->setSingleShot(true);
        connect(m_errorBorderTimer, &QTimer::timeout, this, [this] {
            if (m_errorBorderOverlay)
                m_errorBorderOverlay->hide();
        });
    }
    m_errorBorderOverlay->setGeometry(rect());
    m_errorBorderOverlay->show();
    m_errorBorderOverlay->raise();
    m_errorBorderTimer->start(1500);
}



void MainWindow::openNotificationLink(const NotificationLink &link)
{
    if (!link.isValid())
        return;
    if (link.kind == QLatin1String("chat")) {


        showChatView();
        if (!link.ref.isEmpty())
            switchConversation(link.ref);
        return;
    }
    const int index = repoIndexFor(link.owner, link.name);
    if (index < 0) {
        flashMessage(QStringLiteral("That repository isn't on this node anymore."),
                     true);
        return;
    }
    openRepoDetail(index);
    auto selectTab = [this](int tab) {
        if (m_repoDetailTabs && m_repoDetailTabs->button(tab))
            m_repoDetailTabs->button(tab)->setChecked(true);
        if (m_repoDetailStack)
            m_repoDetailStack->setCurrentIndex(tab);
    };
    if (link.kind == QLatin1String("issue")) {
        selectTab(2);
        if (link.number > 0)
            showIssue(link.number);
    } else if (link.kind == QLatin1String("pull")) {
        if (link.number > 0)
            showPull(link.number);
        else
            selectTab(4);
    } else if (link.kind == QLatin1String("discussion")) {
        selectTab(5);
        if (link.number > 0)
            showDiscussion(link.number);
    } else if (link.kind == QLatin1String("commit")) {
        showOverviewCommits();
        if (!link.ref.isEmpty())
            showCommit(link.ref);
    } else if (link.kind == QLatin1String("release")) {
        if (m_releasesTabIndex >= 0)
            selectTab(m_releasesTabIndex);
    }


}

int MainWindow::pendingActionCount() const
{
    int count = 0;
    for (const ActionRun &run : m_actionRuns)
        if (run.status == ActionStatus::AwaitingApproval)
            ++count;
    return count;
}






void MainWindow::refreshActionRunStrip()
{
    if (!m_actionRunStrip)
        return;
    QVector<ActionRunStrip::Cell> cells;
    QStringList lines;
    for (const ActionRun &run : std::as_const(m_actionRuns)) {
        if (cells.size() >= ActionRunStrip::kMaxCells)
            break;
        ActionRunStrip::Cell cell;
        cell.runId = run.id;
        cell.color = actionStatusColor(run.status);
        cell.running = run.status == ActionStatus::Running;
        cells.append(cell);
        lines << QStringLiteral("%1 \xE2\x80\x94 %2 (%3)")
                     .arg(run.workflowName.isEmpty() ? run.workflowPath
                                                     : run.workflowName,
                          actionStatusText(run.status), run.name);
    }
    m_actionRunStrip->setCells(cells);
    m_actionRunStrip->setVisible(!cells.isEmpty());
    if (cells.isEmpty()) {
        m_actionRunStripTooltipKey.clear();
        return;
    }


    const QString key = lines.join(QLatin1Char('\n'));
    if (key == m_actionRunStripTooltipKey)
        return;
    m_actionRunStripTooltipKey = key;
    m_actionRunStrip->setToolTip(
        QStringLiteral("%1 most recent action run%2, newest first\n%3\n"
                       "Click a square to open that run.")
            .arg(cells.size())
            .arg(cells.size() == 1 ? QString() : QStringLiteral("s"), key));
}

void MainWindow::updateNotificationButton()
{
    refreshActionRunStrip();
    if (!m_notificationButton)
        return;
    const int approvals = pendingActionCount();


    const int pending = approvals + m_webAlertsUnread;


    QStringList tips;
    if (approvals > 0)
        tips << QStringLiteral("%1 action(s) waiting for approval").arg(approvals);
    if (m_webAlertsUnread > 0)
        tips << QStringLiteral("%1 unread website ping(s)").arg(m_webAlertsUnread);
    m_notificationButton->setToolTip(
        tips.isEmpty() ? QStringLiteral("Pings")
                       : tips.join(QString::fromUtf8(" \xC2\xB7 ")));



    if (auto *railButton =
            dynamic_cast<ActivityRailButton *>(m_notificationButton)) {
        railButton->setAlertTint(pending > 0);
        railButton->setBadgeCount(pending);
    }
}

void MainWindow::openActionRunFromNotification(int runId)
{
    const ActionRun *run = findRun(runId);
    if (!run)
        return;
    const int index = repoIndexFor(run->owner, run->name);
    if (index < 0)
        return;
    openRepoDetail(index);
    if (m_repoDetailTabs && m_repoDetailTabs->button(6))
        m_repoDetailTabs->button(6)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(6);
    refreshRepoActions();
    if (m_actionsTable) {
        for (int row = 0; row < m_actionsTable->rowCount(); ++row) {
            QTableWidgetItem *item = m_actionsTable->item(row, 0);
            if (item && item->data(Qt::UserRole).toInt() == runId) {
                m_actionsTable->selectRow(row);
                break;
            }
        }
    }
    showRun(runId);
}



namespace {


constexpr int kNotificationColumns = 9;




class TimestampItem : public QTableWidgetItem
{
public:
    explicit TimestampItem(const QString &text, qint64 ms)
        : QTableWidgetItem(text)
    {
        setData(Qt::UserRole, static_cast<qlonglong>(ms));
    }
    bool operator<(const QTableWidgetItem &other) const override
    {
        return data(Qt::UserRole).toLongLong() <
               other.data(Qt::UserRole).toLongLong();
    }
};







NotificationLink webAlertLink(const QJsonObject &alert)
{
    const QString repo =
        alert.value(QStringLiteral("repo")).toString().trimmed();
    const int slash = repo.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash + 1 >= repo.size())
        return {};
    const QJsonObject meta = alert.value(QStringLiteral("meta")).toObject();
    const int number = meta.value(QStringLiteral("number")).toInt();


    QString source = meta.value(QStringLiteral("source")).toString().trimmed();
    if (source.isEmpty())
        source = alert.value(QStringLiteral("source")).toString().trimmed();
    const QString kind =
        alert.value(QStringLiteral("kind")).toString().trimmed();
    NotificationLink link;
    link.owner = repo.left(slash);
    link.name = repo.mid(slash + 1);
    link.number = number;
    if (number > 0 &&
        (source == QLatin1String("issue") ||
         source == QLatin1String("issue_assigned") ||
         source == QLatin1String("bounty"))) {

        link.kind = QStringLiteral("issue");
    } else if (number > 0 && source == QLatin1String("pull")) {
        link.kind = QStringLiteral("pull");
    } else if (number > 0 && source == QLatin1String("discussion")) {
        link.kind = QStringLiteral("discussion");
    } else if (source == QLatin1String("release") ||
               kind == QLatin1String("release_published")) {
        link.kind = QStringLiteral("release");
    } else if (source == QLatin1String("host") ||
               source == QLatin1String("repository_import") ||
               kind == QLatin1String("repo_shared") ||
               kind == QLatin1String("mirror_request") ||
               kind == QLatin1String("pending_inbox") ||
               kind == QLatin1String("pull_submitted")) {



        link.kind = QStringLiteral("repo");
    }
    return link;
}




QString webPingKindLabel(const QString &kind)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("mention"), QStringLiteral("Mention")},
        {QStringLiteral("subscribed"), QStringLiteral("Thread reply")},
        {QStringLiteral("pull_submitted"), QStringLiteral("Pull request")},
        {QStringLiteral("issue_assigned"), QStringLiteral("Issue assigned")},
        {QStringLiteral("repo_shared"), QStringLiteral("Repo shared")},
        {QStringLiteral("bounty_funded"), QStringLiteral("Bounty funded")},
        {QStringLiteral("bounty_paid"), QStringLiteral("Bounty paid")},
        {QStringLiteral("release_published"), QStringLiteral("Release")},
        {QStringLiteral("host_online"), QStringLiteral("Host online")},
        {QStringLiteral("host_offline"), QStringLiteral("Host offline")},
        {QStringLiteral("credits_refilled"), QStringLiteral("Credits")},
        {QStringLiteral("pending_inbox"), QStringLiteral("Inbox item")},
        {QStringLiteral("mirror_request"), QStringLiteral("Mirror request")},
        {QStringLiteral("pending_reward"), QStringLiteral("Reward")},
        {QStringLiteral("org_succession"), QStringLiteral("Org succession")},
        {QStringLiteral("repository_hosted"), QStringLiteral("Repo hosted")},
        {QStringLiteral("organization_task_started"), QStringLiteral("Org task")},
        {QStringLiteral("organization_task_activity"), QStringLiteral("Org task")},
        {QStringLiteral("error_group"), QStringLiteral("Error group")},
    };
    return labels.value(kind, QStringLiteral("Web"));
}
}

QWidget *MainWindow::buildNotificationsSection()
{
    auto *page = new QWidget;

    auto *title = new QLabel(QStringLiteral("Pings"));
    title->setObjectName("settingsTitle");

    auto *refreshButton = new QPushButton(QStringLiteral("Refresh"));
    refreshButton->setObjectName("repoAction");
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this, [this] {


        refreshWebAlerts(true);
        refreshNotificationsTable();
    });
    addRefreshSpin(refreshButton);




    auto *testButton = new QPushButton(QStringLiteral("Test"));
    testButton->setObjectName("repoAction");
    testButton->setCursor(Qt::PointingHandCursor);
    setOcticon(testButton, "bell", 16);
    testButton->setToolTip(
        QStringLiteral("Send a test desktop ping"));
    connect(testButton, &QPushButton::clicked, this, [this] {
        const QString body = QStringLiteral(
            "This is a test ping from ForkMesh — "
            "desktop pings are working.");
        addNotification(QStringLiteral("Test ping"), body, false);
        postNotification(QStringLiteral("Test ping"), body, false,
                         QStringLiteral("emblem-default"));
    });




    auto *deleteButton = new QPushButton(QStringLiteral("Delete"));
    deleteButton->setObjectName("repoAction");
    deleteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(deleteButton, "x", 16);
    deleteButton->setToolTip(
        QStringLiteral("Delete the selected pings (Del)"));
    connect(deleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteSelectedNotifications);

    auto *clearButton = new QPushButton(QStringLiteral("Clear"));
    clearButton->setObjectName("repoAction");
    clearButton->setCursor(Qt::PointingHandCursor);
    setOcticon(clearButton, "trash", 16);
    clearButton->setToolTip(QStringLiteral(
        "Dismiss all past pings and mark website pings read"));
    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_notifications.clear();
        markWebAlertsRead();
        updateNotificationButton();
        refreshLogEventList();
        refreshNotificationsTable();
    });

    auto *header = new QHBoxLayout;
    header->setContentsMargins(16, 12, 16, 4);
    header->addWidget(title);
    header->addStretch();
    header->addWidget(testButton);
    header->addWidget(refreshButton);
    header->addWidget(deleteButton);
    header->addWidget(clearButton);



    m_notificationsTable = new QTableWidget(0, kNotificationColumns);
    installColumnHeaderMenu(m_notificationsTable);
    m_notificationsTable->setObjectName("issueTable");
    m_notificationsTable->setHorizontalHeaderLabels(
        {QStringLiteral("Type"), QStringLiteral("Kind"),
         QStringLiteral("Title"), QStringLiteral("Detail"),
         QStringLiteral("Repository"), QStringLiteral("From"),
         QStringLiteral("Status"), QStringLiteral("When"),
         QStringLiteral("Link")});
    m_notificationsTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_notificationsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_notificationsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_notificationsTable->verticalHeader()->setVisible(false);
    m_notificationsTable->setSortingEnabled(true);
    m_notificationsTable->setAlternatingRowColors(true);
    m_notificationsTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_notificationsTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    m_notificationsTable->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::Stretch);
    for (int column : {0, 1, 4, 5, 6, 7, 8})
        m_notificationsTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_notificationsTable);
    m_notificationsTable->horizontalHeader()->setSortIndicator(
        7, Qt::DescendingOrder);
    m_notificationsTable->setToolTip(
        QStringLiteral("Click a row to open the related issue, pull "
                       "request, discussion, commit, chat or action. Del (or "
                       "right-click) removes the selected rows."));




    connect(m_notificationsTable, &QTableWidget::itemClicked, this,
            [this](QTableWidgetItem *item) {
                if (!item ||
                    (QGuiApplication::keyboardModifiers() &
                     (Qt::ControlModifier | Qt::ShiftModifier)))
                    return;
                openNotificationRow(item->row());
            });



    auto *deleteShortcut =
        new QShortcut(QKeySequence::Delete, m_notificationsTable);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteShortcut, &QShortcut::activated, this,
            &MainWindow::deleteSelectedNotifications);
    connect(m_notificationsTable, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QTableWidgetItem *item = m_notificationsTable->itemAt(pos);


                if (item && !item->isSelected())
                    m_notificationsTable->selectRow(item->row());
                QMenu menu(this);
                QAction *open = item ? menu.addAction(QStringLiteral("Open"))
                                     : nullptr;
                QAction *copy = item ? menu.addAction(QStringLiteral("Copy row"))
                                     : nullptr;
                if (item)
                    menu.addSeparator();
                QAction *remove = menu.addAction(QStringLiteral("Delete"));
                remove->setEnabled(
                    !m_notificationsTable->selectionModel()->selectedRows()
                         .isEmpty());
                QAction *chosen =
                    menu.exec(m_notificationsTable->viewport()->mapToGlobal(pos));
                if (!chosen)
                    return;
                if (chosen == remove) {
                    deleteSelectedNotifications();
                } else if (chosen == open) {
                    openNotificationRow(item->row());
                } else if (chosen == copy) {
                    QStringList cells;
                    for (int column = 0; column < kNotificationColumns; ++column)
                        if (QTableWidgetItem *cell =
                                m_notificationsTable->item(item->row(), column))
                            cells << cell->text();
                    QGuiApplication::clipboard()->setText(
                        cells.join(QStringLiteral("\t")));
                }
            });

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addLayout(header);
    layout->addWidget(m_notificationsTable, 1);

    refreshNotificationsTable();
    return page;
}

void MainWindow::refreshNotificationsTable()
{
    if (!m_notificationsTable)
        return;

    TableRepaintGuard repaintGuard(m_notificationsTable);
    m_notificationsTable->setSortingEnabled(false);
    m_notificationsTable->setRowCount(0);






    struct NotificationRow {
        QString type;
        QString kind;
        QString title;
        QString detail;
        QString repo;
        QString actor;
        QString status;
        qint64 whenMs = 0;
        QString link;
        int runId = -1;
        bool warning = false;
        NotificationLink destination;
        QString href;
        qint64 localId = 0;
        QString webId;
    };
    auto addRow = [this](const NotificationRow &data) {
        const int row = m_notificationsTable->rowCount();
        m_notificationsTable->insertRow(row);

        auto *typeItem = new QTableWidgetItem(data.type);
        typeItem->setData(Qt::UserRole, data.runId);
        if (data.destination.isValid())
            typeItem->setData(Qt::UserRole + 1,
                              QVariant::fromValue(data.destination));
        if (!data.href.isEmpty())
            typeItem->setData(Qt::UserRole + 2, data.href);
        if (data.localId > 0)
            typeItem->setData(Qt::UserRole + 3,
                              static_cast<qlonglong>(data.localId));
        if (!data.webId.isEmpty())
            typeItem->setData(Qt::UserRole + 4, data.webId);


        auto *whenItem = new TimestampItem(
            data.whenMs > 0 ? formatRepoDate(data.whenMs) : QString(),
            data.whenMs);
        QList<QTableWidgetItem *> cells{
            typeItem,
            new QTableWidgetItem(data.kind),
            new QTableWidgetItem(data.title),
            new QTableWidgetItem(data.detail),
            new QTableWidgetItem(data.repo),
            new QTableWidgetItem(data.actor),
            new QTableWidgetItem(data.status),
            whenItem,
            new QTableWidgetItem(data.link)};
        const QColor red("#f85149");
        for (int column = 0; column < cells.size(); ++column) {
            QTableWidgetItem *cell = cells.at(column);


            if (!cell->text().isEmpty())
                cell->setToolTip(cell->text());
            if (data.warning)
                cell->setForeground(red);
            m_notificationsTable->setItem(row, column, cell);
        }
    };

    for (const ActionRun &run : std::as_const(m_actionRuns)) {
        if (run.status != ActionStatus::AwaitingApproval)
            continue;
        NotificationRow data;
        data.type = QStringLiteral("Approval");
        data.kind = QStringLiteral("action");
        data.title = run.workflowName;
        data.detail = QStringLiteral("%1/%2 at %3")
                          .arg(run.owner, run.name, run.commit.left(8));
        data.repo = run.owner + QLatin1Char('/') + run.name;
        data.status = QStringLiteral("Awaiting approval");
        data.whenMs = run.createdAtMs;
        data.link = QStringLiteral("run #%1").arg(run.id);
        data.runId = run.id;
        addRow(data);
    }
    for (const AppNotification &notice : std::as_const(m_notifications)) {
        NotificationRow data;
        data.type = notice.warning ? QStringLiteral("Alert")
                                   : QStringLiteral("Info");
        data.kind = notice.kind;
        data.title = notice.title;
        data.detail = notice.body;
        data.repo = notice.repo;
        data.actor = notice.actor;
        data.status = QStringLiteral("Desktop");
        data.whenMs = notice.timestampMs;
        data.link = notificationLinkLabel(notice.link);
        data.runId = notice.runId;
        data.warning = notice.warning;
        data.destination = notice.link;
        data.localId = notice.id;
        addRow(data);
    }


    for (const QJsonValue &value : std::as_const(m_webAlerts)) {
        const QJsonObject alert = value.toObject();
        const bool unread =
            alert.value(QStringLiteral("readAt")).toDouble() <= 0;
        const QString kind =
            alert.value(QStringLiteral("kind")).toString().trimmed();
        const QString title =
            alert.value(QStringLiteral("title")).toString().trimmed();
        const QString body =
            alert.value(QStringLiteral("body")).toString().trimmed();
        const QString repo =
            alert.value(QStringLiteral("repo")).toString().trimmed();
        const QString href =
            alert.value(QStringLiteral("href")).toString().trimmed();
        const QString webUrl =
            href.startsWith(QLatin1Char('/'))
                ? catalogApiUrl().resolved(QUrl(href)).toString()
                : QString();
        NotificationRow data;
        data.type = webPingKindLabel(kind);
        data.kind = kind;


        data.title = (unread ? QString::fromUtf8("\xE2\x97\x8F ") : QString()) +
                     (title.isEmpty() ? QStringLiteral("Website ping") : title);
        data.detail = body;
        data.repo = repo;
        data.actor = alert.value(QStringLiteral("actor")).toString().trimmed();
        data.status = unread ? QStringLiteral("Unread")
                             : QStringLiteral("Read");
        data.whenMs = qint64(alert.value(QStringLiteral("ts")).toDouble());
        data.link = href;

        data.warning = kind == QLatin1String("error_group");
        data.destination = webAlertLink(alert);
        data.href = webUrl;
        data.webId = alert.value(QStringLiteral("id")).toString().trimmed();
        addRow(data);
    }

    m_notificationsTable->setSortingEnabled(true);
}



QString MainWindow::notificationLinkLabel(const NotificationLink &link)
{
    if (!link.isValid())
        return {};
    if (link.kind == QLatin1String("chat"))
        return link.ref;
    QString label = link.owner.isEmpty()
                        ? link.kind
                        : link.owner + QLatin1Char('/') + link.name;
    if (link.number > 0)
        label += QLatin1Char('#') + QString::number(link.number);
    else if (!link.ref.isEmpty())
        label += QLatin1Char('@') + link.ref.left(8);
    return label;
}




void MainWindow::openNotificationRow(int row)
{
    if (!m_notificationsTable || row < 0)
        return;
    QTableWidgetItem *first = m_notificationsTable->item(row, 0);
    if (!first)
        return;
    const int runId = first->data(Qt::UserRole).toInt();
    if (runId > 0) {
        openActionRunFromNotification(runId);
        return;
    }
    const QVariant nav = first->data(Qt::UserRole + 1);
    if (nav.canConvert<NotificationLink>() &&
        qvariant_cast<NotificationLink>(nav).isValid()) {
        openNotificationLink(qvariant_cast<NotificationLink>(nav));
        return;
    }
    const QString href = first->data(Qt::UserRole + 2).toString();
    if (!href.isEmpty())
        QDesktopServices::openUrl(QUrl(href));
}




void MainWindow::deleteSelectedNotifications()
{
    if (!m_notificationsTable || !m_notificationsTable->selectionModel())
        return;
    QSet<qlonglong> localIds;
    QStringList webIds;
    int approvals = 0;
    const QModelIndexList rows =
        m_notificationsTable->selectionModel()->selectedRows();
    for (const QModelIndex &index : rows) {
        QTableWidgetItem *first = m_notificationsTable->item(index.row(), 0);
        if (!first)
            continue;
        const qlonglong localId = first->data(Qt::UserRole + 3).toLongLong();
        const QString webId = first->data(Qt::UserRole + 4).toString();
        if (localId > 0)
            localIds.insert(localId);
        else if (!webId.isEmpty())
            webIds << webId;
        else
            ++approvals;
    }
    if (localIds.isEmpty() && webIds.isEmpty()) {
        if (approvals > 0)
            flashMessage(QStringLiteral(
                             "Approvals clear themselves once the run is "
                             "approved or rejected."),
                         false);
        return;
    }
    if (!localIds.isEmpty()) {
        auto stale = [&localIds](const AppNotification &notice) {
            return localIds.contains(static_cast<qlonglong>(notice.id));
        };
        m_notifications.erase(std::remove_if(m_notifications.begin(),
                                             m_notifications.end(), stale),
                              m_notifications.end());
    }
    for (const QString &alertId : std::as_const(webIds))
        deleteWebAlert(alertId);
    updateNotificationButton();
    refreshLogEventList();
    refreshNotificationsTable();
}




void MainWindow::deleteWebAlert(const QString &alertId)
{
    const QString id = alertId.trimmed().toLower();


    QJsonArray remaining;
    for (const QJsonValue &value : std::as_const(m_webAlerts)) {
        const QJsonObject alert = value.toObject();
        if (alert.value(QStringLiteral("id")).toString().trimmed().toLower() == id) {
            if (alert.value(QStringLiteral("readAt")).toDouble() <= 0 &&
                m_webAlertsUnread > 0)
                --m_webAlertsUnread;
            continue;
        }
        remaining.append(value);
    }
    m_webAlerts = remaining;
    if (!m_networkAccess || id.isEmpty())
        return;
    const QString node = accountOwner().trimmed().toLower();
    if (node.isEmpty())
        return;
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/notifications"));
    url.setQuery(QString());
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/json"));
    if (!authenticateOrgTaskRequest(url, request, kAccountAlertDeleteProof, id))
        return;
    const QJsonObject body{{QStringLiteral("node"), node},
                           {QStringLiteral("id"), id}};
    QNetworkReply *reply = m_networkAccess->sendCustomRequest(
        request, QByteArrayLiteral("DELETE"),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 ||
            status >= 300) {
            flashMessage(QStringLiteral(
                             "Couldn't delete that website ping (%1).")
                             .arg(status),
                         true);

            m_webAlertsFetchedAtMs = 0;
        }
    });
}





void MainWindow::refreshWebAlerts(bool force)
{
    if (!m_networkAccess || m_webAlertsLoading)
        return;
    const QString node = accountOwner().trimmed().toLower();
    if (node.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();


    if (!force && m_webAlertsFetchedAtMs > 0 &&
        now - m_webAlertsFetchedAtMs < 300000)
        return;

    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/notifications"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("node"), node);
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("40"));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(15000);
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/json"));
    if (!authenticateOrgTaskRequest(url, request, kAccountAlertListProof,
                                    QString()))
        return;
    m_webAlertsLoading = true;
    m_webAlertsFetchedAtMs = now;
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_webAlertsLoading = false;
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 ||
            status >= 300)
            return;
        const QJsonObject payload =
            QJsonDocument::fromJson(reply->readAll()).object();
        m_webAlerts = payload.value(QStringLiteral("notifications")).toArray();
        m_webAlertsUnread = payload.value(QStringLiteral("unread")).toInt();
        updateNotificationButton();




        for (const QJsonValue &value : std::as_const(m_webAlerts)) {
            const QJsonObject alert = value.toObject();
            if (alert.value(QStringLiteral("kind")).toString().trimmed() !=
                    QLatin1String("error_group") ||
                alert.value(QStringLiteral("readAt")).toDouble() > 0)
                continue;
            const QString id =
                alert.value(QStringLiteral("id")).toString().trimmed();
            if (id.isEmpty() || m_flashedWebAlertIds.contains(id))
                continue;
            m_flashedWebAlertIds.insert(id);
            const QString title =
                alert.value(QStringLiteral("title")).toString().trimmed();
            AppNotification ping;
            ping.title = title.isEmpty()
                             ? QStringLiteral("New error group on the relay")
                             : title;
            ping.body = alert.value(QStringLiteral("body")).toString().trimmed();
            ping.warning = true;
            ping.kind = QStringLiteral("error_group");
            flashNotification(ping);
        }


        if (m_notificationsTable && m_sectionStack &&
            m_sectionStack->currentIndex() == 3)
            refreshNotificationsTable();
    });
}

void MainWindow::markWebAlertsRead()
{
    if (!m_networkAccess || m_webAlerts.isEmpty() || m_webAlertsUnread <= 0)
        return;
    const QString node = accountOwner().trimmed().toLower();
    if (node.isEmpty())
        return;
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/notifications"));
    url.setQuery(QString());
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/json"));


    if (!authenticateOrgTaskRequest(url, request, kAccountAlertReadProof,
                                    QString()))
        return;
    const QJsonObject body{{QStringLiteral("node"), node},
                           {QStringLiteral("all"), true}};
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 ||
            status >= 300)
            return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int index = 0; index < m_webAlerts.size(); ++index) {
            QJsonObject alert = m_webAlerts.at(index).toObject();
            if (alert.value(QStringLiteral("readAt")).toDouble() <= 0) {
                alert.insert(QStringLiteral("readAt"), double(now));
                m_webAlerts.replace(index, alert);
            }
        }
        m_webAlertsUnread = 0;
        updateNotificationButton();
        if (m_notificationsTable && m_sectionStack &&
            m_sectionStack->currentIndex() == 3)
            refreshNotificationsTable();
    });
}

void MainWindow::showNotifications()
{
    showSection(3);
}

void MainWindow::postNotification(const QString &title, const QString &body,
                                  bool warning, const QString &icon)
{
    const QString iconName =
        !icon.isEmpty() ? icon
                        : (warning ? QStringLiteral("dialog-error")
                                   : QStringLiteral("dialog-information"));
#if defined(Q_OS_LINUX)



    static const QString notifySend =
        QStandardPaths::findExecutable(QStringLiteral("notify-send"));
    if (!notifySend.isEmpty()) {
        const QStringList args = {
            QStringLiteral("-a"), QStringLiteral("ForkMesh"),
            QStringLiteral("-i"), iconName,
            QStringLiteral("-u"),
            warning ? QStringLiteral("critical") : QStringLiteral("normal"),
            title, body};
        if (QProcess::startDetached(notifySend, args))
            return;
    }
#endif
    if (m_trayIcon && QSystemTrayIcon::isSystemTrayAvailable())
        m_trayIcon->showMessage(
            title, body,
            warning ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information,
            6000);
}

void MainWindow::refreshActionsTable()
{
    if (!m_actionsTable)
        return;


    QString owner, name;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        owner = m_repositories.at(m_repoDetailIndex).owner;
        name = m_repositories.at(m_repoDetailIndex).name;
    }

    QSignalBlocker block(m_actionsTable);
    TableRepaintGuard repaintGuard(m_actionsTable);
    m_actionsTable->setRowCount(0);
    for (const ActionRun &run : m_actionRuns) {
        if (run.owner != owner || run.name != name)
            continue;
        if (!m_selectedWorkflowFilter.isEmpty() &&
            run.workflowPath != m_selectedWorkflowFilter)
            continue;
        const int row = m_actionsTable->rowCount();
        m_actionsTable->insertRow(row);

        auto *wfItem = new QTableWidgetItem(run.workflowName);
        wfItem->setData(Qt::UserRole, run.id);

        wfItem->setData(ActionFailureBorderDelegate::ActionFailedRole,
                        run.status == ActionStatus::Failed);
        auto *statusItem = new QTableWidgetItem(actionStatusText(run.status));
        statusItem->setForeground(actionStatusColor(run.status));


        QString when;
        if (run.createdAtMs > 0) {
            const QString rel = formatShortRelativeTime(run.createdAtMs / 1000);
            when = rel == QStringLiteral("now") ? rel
                                                : rel + QStringLiteral(" ago");
        }



        if (run.status == ActionStatus::Queued ||
            run.status == ActionStatus::Running) {
            const qint64 last = estimatedRunDurationMs(run);
            if (last > 0) {
                const QString lastStr =
                    QStringLiteral("last %1").arg(formatDuration(last));
                when = when.isEmpty() ? lastStr
                                      : when + QStringLiteral(" \xC2\xB7 ") + lastStr;
            }
        }
        auto *whenItem = new QTableWidgetItem(when);
        if (run.createdAtMs > 0)
            whenItem->setToolTip(QDateTime::fromMSecsSinceEpoch(run.createdAtMs)
                                     .toString(QStringLiteral("MMM d  hh:mm")));




        QString durationText;
        if (run.startedAtMs > 0 && run.finishedAtMs >= run.startedAtMs)
            durationText = formatDuration(run.finishedAtMs - run.startedAtMs);
        auto *durationItem = new QTableWidgetItem(durationText);

        m_actionsTable->setItem(row, 0, wfItem);
        m_actionsTable->setItem(row, 1, statusItem);
        m_actionsTable->setItem(row, 2, whenItem);
        m_actionsTable->setItem(row, 3, durationItem);
        if (run.id == m_selectedRunId)
            m_actionsTable->selectRow(row);
    }
    updateActionsTabIndicator();
}

void MainWindow::showLatestVisibleActionRun()
{
    if (!m_actionsTable || m_actionsTable->rowCount() == 0) {
        showRun(-1);
        return;
    }

    int targetRow = 0;
    int targetRunId = -1;
    for (int row = 0; row < m_actionsTable->rowCount(); ++row) {
        QTableWidgetItem *item = m_actionsTable->item(row, 0);
        if (!item)
            continue;
        const int runId = item->data(Qt::UserRole).toInt();
        if (targetRunId < 0) {
            targetRow = row;
            targetRunId = runId;
        }
        const ActionRun *run = findRun(runId);
        if (run && run->status == ActionStatus::Running) {
            targetRow = row;
            targetRunId = runId;
            break;
        }
    }

    if (targetRunId < 0) {
        showRun(-1);
        return;
    }

    m_actionsTable->selectRow(targetRow);
    showRun(targetRunId);
}

int MainWindow::commitStatusCode(const QString &sha) const
{
    if (sha.isEmpty() || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return 0;
    const QString owner = m_repositories.at(m_repoDetailIndex).owner;
    const QString name = m_repositories.at(m_repoDetailIndex).name;




    bool running = false, failed = false, success = false;
    for (const ActionRun &run : m_actionRuns) {
        if (run.owner != owner || run.name != name || run.commit.isEmpty())
            continue;
        if (!(run.commit.startsWith(sha) || sha.startsWith(run.commit)))
            continue;
        if (run.status == ActionStatus::Running ||
            run.status == ActionStatus::Queued ||
            run.status == ActionStatus::AwaitingApproval)
            running = true;
        else if (run.status == ActionStatus::Failed ||
                 run.status == ActionStatus::Rejected)
            failed = true;
        else if (run.status == ActionStatus::Success)
            success = true;
    }
    if (running)
        return 3;
    if (failed)
        return 2;
    if (success)
        return 1;
    return 0;
}

QString MainWindow::commitStatusGlyph(const QString &sha) const
{
    switch (commitStatusCode(sha)) {
    case 3:
        return QString::fromUtf8(" <span style='color:#58a6ff' "
                              "title='Checks running'>\xE2\x97\x90</span>");
    case 2:
        return QString::fromUtf8(" <span style='color:#f85149' "
                              "title='Checks failed'>\xE2\x9C\x95</span>");
    case 1:
        return QString::fromUtf8(" <span style='color:#3fb950' "
                              "title='Checks passed'>\xE2\x9C\x93</span>");
    default:
        return QString();
    }
}

void MainWindow::refreshCommitBarStatusGlyph()
{
    if (!m_commitBar || m_commitBarStatusHash.isEmpty() ||
        m_commitBarBodyHtml.isEmpty())
        return;
    m_commitBar->setText(commitStatusGlyph(m_commitBarStatusHash) +
                         m_commitBarBodyHtml);
}

void MainWindow::refreshCommitTableStatusGlyphs()
{
    if (!m_commitsTable)
        return;
    for (int row = 0; row < m_commitsTable->rowCount(); ++row) {
        QTableWidgetItem *summary = m_commitsTable->item(row, kCommitSummaryCol);
        if (!summary || summary->data(kCommitRowKindRole).toInt() != 0)
            continue;
        const QString sha = summary->data(Qt::UserRole).toString();
        if (sha.isEmpty())
            continue;
        switch (commitStatusCode(sha)) {
        case 1:
            summary->setIcon(themedOcticon("check-circle", QColor("#3fb950"), 14));
            break;
        case 2:
            summary->setIcon(themedOcticon("x", QColor("#f85149"), 14));
            break;
        case 3:
            summary->setIcon(themedOcticon("sync", QColor("#58a6ff"), 14));
            break;
        default:
            summary->setIcon(QIcon());
            break;
        }

        updateCommitRowHover(row);
    }
}

void MainWindow::refreshCommitStatusGlyphs()
{
    if (!m_repoDetailStack)
        return;
    switch (m_repoDetailStack->currentIndex()) {
    case 0:
        refreshCommitBarStatusGlyph();
        break;
    case 1:
        refreshCommitTableStatusGlyphs();
        break;
    default:
        break;
    }
}

void MainWindow::updateActionsTabIndicator()
{
    QAbstractButton *tab = m_repoDetailTabs ? m_repoDetailTabs->button(6) : nullptr;
    if (!tab)
        return;











    tab->setText(QStringLiteral("Actions (%1)")
                     .arg(formatCount(m_repoWorkflows.size())));
}




qint64 MainWindow::estimatedRunDurationMs(const ActionRun &run) const
{
    qint64 best = 0;
    qint64 newest = 0;
    for (const ActionRun &r : m_actionRuns) {
        if (r.id == run.id || r.owner != run.owner || r.name != run.name ||
            r.workflowPath != run.workflowPath)
            continue;
        if (r.startedAtMs <= 0 || r.finishedAtMs <= r.startedAtMs)
            continue;
        if (r.finishedAtMs > newest) {
            newest = r.finishedAtMs;
            best = r.finishedAtMs - r.startedAtMs;
        }
    }
    return best;
}

void MainWindow::updateAgentsTabIndicator()
{




    QString owner, name;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        owner = m_repositories.at(m_repoDetailIndex).owner;
        name  = m_repositories.at(m_repoDetailIndex).name;
    }
    bool anyRunning = !m_externalClaude.isEmpty();
    if (!anyRunning) {
        for (const AgentSession &s : std::as_const(m_agentSessions)) {
            if (s.owner == owner && s.name == name && !s.merged &&
                s.status == AgentStatus::Running) {
                anyRunning = true;
                break;
            }
        }
    }
    if (!anyRunning) {
        if (m_agentsSpinTimer)
            m_agentsSpinTimer->stop();
        return;
    }
    if (!m_agentsSpinTimer) {
        m_agentsSpinTimer = new QTimer(this);
        connect(m_agentsSpinTimer, &QTimer::timeout, this,
                &MainWindow::animateRunningAgentIcons);
    }
    if (!m_agentsSpinTimer->isActive())
        m_agentsSpinTimer->start(kAgentSpinTickMs);
}









void MainWindow::persistLooperState()
{
    QSettings settings;
    settings.setValue(kLooperActiveSetting, m_looperActive);
    settings.setValue(kLooperProviderSetting, m_looperProvider);
    settings.setValue(kLooperRepoSetting, m_looperRepoSlug);
}






void MainWindow::maybeRestoreIssueLooper()
{
    if (m_looperActive)
        return;
    QSettings settings;
    if (!settings.value(kLooperActiveSetting, false).toBool())
        return;
    const QString slug = settings.value(kLooperRepoSetting).toString();
    const int idx = issuesRepoIndex();
    if (slug.isEmpty() || idx < 0 || idx >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);
    if (slug != repo.owner + QLatin1Char('/') + repo.name)
        return;
    if (repoAgentGitDir(repo).isEmpty())
        return;
    m_looperActive = true;
    m_looperProvider = settings.value(kLooperProviderSetting).toString();
    if (m_looperProvider.isEmpty())
        m_looperProvider = defaultAgentProvider();
    m_looperRepoSlug = slug;
    updateIssueLooperButton();
    setIssueInlineNotice(
        QString::fromUtf8("Resumed the issue looper with %1 after restart\xE2\x80\xA6")
            .arg(agentProviderName(m_looperProvider)));
    looperStartNext();
}




static QList<ActionWorkflow> workflowsInRepoPaths(const QString &mirrorPath,
                                                  const QString &localPath)
{
    QList<ActionWorkflow> out;


    if (!mirrorPath.isEmpty() && QDir(mirrorPath).exists()) {
        QProcess ls;
        ls.start(QStringLiteral("git"),
                 {QStringLiteral("-C"), mirrorPath, QStringLiteral("ls-tree"),
                  QStringLiteral("-r"), QStringLiteral("--name-only"),
                  QStringLiteral("HEAD"), QStringLiteral("--"),
                  QStringLiteral(".forkmesh")});
        ls.waitForFinished(8000);
        const QStringList paths = QString::fromUtf8(ls.readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &path : paths) {
            if (!(path.endsWith(QLatin1String(".yml")) ||
                  path.endsWith(QLatin1String(".yaml"))))
                continue;
            QProcess show;
            show.start(QStringLiteral("git"),
                       {QStringLiteral("-C"), mirrorPath,
                        QStringLiteral("show"), QStringLiteral("HEAD:") + path});
            show.waitForFinished(8000);
            if (show.exitCode() != 0)
                continue;
            out.append(ActionFile::parse(
                path, QString::fromUtf8(show.readAllStandardOutput())));
        }
    }
    if (out.isEmpty() && !localPath.isEmpty())
        out = ActionFile::parseWorkflowsInDir(localPath);
    return out;
}

QList<ActionWorkflow>
MainWindow::availableWorkflowsForRepo(const RepositoryRecord &repo) const
{
    return workflowsInRepoPaths(repo.mirrorPath, repo.localPath);
}






void MainWindow::reloadWorkflowCountInBackground()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const int repoIndex = m_repoDetailIndex;
    const int generation = ++m_workflowCountLoadGen;

    const RepositoryRecord &repo = m_repositories.at(repoIndex);
    const QString mirrorPath = repo.mirrorPath;
    const QString localPath = repo.localPath;
    runOffThread<QList<ActionWorkflow>>(
        [mirrorPath, localPath] {
            return workflowsInRepoPaths(mirrorPath, localPath);
        },
        [this, repoIndex, generation](QList<ActionWorkflow> loaded) {
            if (generation != m_workflowCountLoadGen ||
                repoIndex != m_repoDetailIndex)
                return;
            m_repoWorkflows = std::move(loaded);
            updateActionsTabIndicator();
        });
}

void MainWindow::updateWorkflowListItem(QListWidgetItem *item,
                                        const ActionWorkflow &wf,
                                        const RepositoryRecord &repo)
{
    if (!item)
        return;
    QStringList triggers;
    if (wf.triggersOnPush())
        triggers << QStringLiteral("on: push");
    if (wf.triggersOnRelease())
        triggers << QStringLiteral("on: release");
    if (wf.allowsManualRun())
        triggers << QStringLiteral("manual");



    const QStringList runsOn = workflowRunsOnLabels(repo, wf);
    item->setText(wf.name);
    item->setForeground(palette().color(QPalette::Active, QPalette::Text));
    if (!runsOn.isEmpty()) {
        const QString where = runsOn.join(QStringLiteral(", "));
        triggers << QStringLiteral("runs-on: ") + where;
        if (!workflowRunsOnThisNode(repo, wf)) {
            item->setText(wf.name + QString::fromUtf8("  \xC2\xB7  ") + where);
            item->setForeground(
                palette().color(QPalette::Disabled, QPalette::Text));
        }
    }


    if (wf.valid) {
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(repo.disabledWorkflows.contains(wf.path)
                                ? Qt::Unchecked
                                : Qt::Checked);
    }
    item->setToolTip(
        wf.valid ? wf.path +
                       (triggers.isEmpty()
                            ? QString()
                            : QStringLiteral("  (") +
                                  triggers.join(QStringLiteral(", ")) +
                                  QStringLiteral(")")) +
                       QStringLiteral("\nUntick to disable this workflow.")
                 : wf.path + QStringLiteral("  — ") + wf.error);
}

void MainWindow::refreshRepoActions()
{
    if (!m_actionWorkflowList)
        return;
    QSignalBlocker block(m_actionWorkflowList);
    m_actionWorkflowList->clear();

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        m_repoWorkflows.clear();
        updateActionsTabIndicator();
        refreshActionsTable();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);

    if (m_actionsEnabledCheck) {
        QSignalBlocker block(m_actionsEnabledCheck);
        m_actionsEnabledCheck->setChecked(repo.actionsEnabled);
    }

    auto *all = new QListWidgetItem(QStringLiteral("All workflows"));
    all->setData(Qt::UserRole, QString());
    m_actionWorkflowList->addItem(all);
    all->setSelected(true);

    const QList<ActionWorkflow> wfs = availableWorkflowsForRepo(repo);
    m_repoWorkflows = wfs;
    for (const ActionWorkflow &wf : wfs) {
        auto *item = new QListWidgetItem;
        item->setData(Qt::UserRole, wf.path);
        updateWorkflowListItem(item, wf, repo);
        m_actionWorkflowList->addItem(item);
    }
    if (wfs.isEmpty()) {
        auto *none = new QListWidgetItem(
            repo.actionsEnabled
                ? QStringLiteral("No workflows in .forkmesh/")
                : QStringLiteral("No workflows in .forkmesh/ (actions disabled)"));
        none->setFlags(Qt::NoItemFlags);
        m_actionWorkflowList->addItem(none);
    }

    m_selectedWorkflowFilter.clear();
    refreshActionsTable();
    showLatestVisibleActionRun();
    updateManualRunBar();
    refreshWorkflowNodeCombo();

    ++m_workflowCountLoadGen;
    updateActionsTabIndicator();
}

void MainWindow::refreshWorkflowNodeCombo()
{
    if (!m_actionNodeCombo)
        return;
    QSignalBlocker block(m_actionNodeCombo);
    m_actionNodeCombo->clear();
    const bool haveRepo = m_repoDetailIndex >= 0 &&
                          m_repoDetailIndex < m_repositories.size();
    m_actionNodeCombo->setEnabled(haveRepo);
    if (m_actionNodeLabel)
        m_actionNodeLabel->setEnabled(haveRepo);
    if (!haveRepo) {
        m_actionNodeCombo->addItem(QStringLiteral("Any node"), QString());
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);



    const ActionWorkflow *selected = nullptr;
    for (const ActionWorkflow &wf : std::as_const(m_repoWorkflows)) {
        if (wf.valid && wf.path == m_selectedWorkflowFilter) {
            selected = &wf;
            break;
        }
    }
    if (m_actionNodeLabel) {


        const QString caption =
            selected ? QString::fromUtf8("Run \xE2\x80\x9C%1\xE2\x80\x9D on")
                           .arg(QFontMetrics(m_actionNodeLabel->font())
                                    .elidedText(selected->name, Qt::ElideRight,
                                                140))
                     : QStringLiteral("Run every workflow on");
        m_actionNodeLabel->setText(caption);
        m_actionNodeLabel->setToolTip(
            selected ? QString::fromUtf8("Run \xE2\x80\x9C%1\xE2\x80\x9D on")
                           .arg(selected->name)
                     : QString());
    }




    QString pin;
    if (selected) {
        pin = workflowNodePin(repo, selected->path);
    } else {
        bool first = true;
        for (const ActionWorkflow &wf : std::as_const(m_repoWorkflows)) {
            if (!wf.valid)
                continue;
            const QString wfPin = workflowNodePin(repo, wf.path);
            if (first) {
                pin = wfPin;
                first = false;
            } else if (wfPin != pin) {
                pin.clear();
                break;
            }
        }
    }



    const QStringList declared = selected ? selected->runsOn : QStringList();
    m_actionNodeCombo->addItem(
        declared.isEmpty()
            ? QStringLiteral("Any node")
            : QStringLiteral("Workflow default (%1)")
                  .arg(declared.join(QStringLiteral(", "))),
        QString());
    const QString self = machineNodeName().trimmed();
    for (const QString &node : actionNodeCandidates()) {
        const bool isSelf = node.compare(self, Qt::CaseInsensitive) == 0;
        m_actionNodeCombo->addItem(
            isSelf ? QStringLiteral("%1 (this node)").arg(node) : node,
            node.toLower());
    }


    if (!pin.isEmpty() && m_actionNodeCombo->findData(pin) < 0)
        m_actionNodeCombo->addItem(
            QStringLiteral("%1 (offline)").arg(pin), pin);
    m_actionNodeCombo->setCurrentIndex(
        pin.isEmpty() ? 0 : qMax(0, m_actionNodeCombo->findData(pin)));
    m_actionNodeCombo->setToolTip(QStringLiteral(
        "Which node runs these actions. Picking a node here overrides the "
        "workflow's own runs-on: line for this repo, and stops this node "
        "queueing the workflow unless it is that node."));
}

void MainWindow::setWorkflowNode(const QString &node)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const QString label = node.trimmed().toLower();
    QStringList targets;
    if (m_selectedWorkflowFilter.isEmpty()) {
        for (const ActionWorkflow &wf : std::as_const(m_repoWorkflows))
            if (wf.valid)
                targets.append(wf.path);
    } else {
        targets.append(m_selectedWorkflowFilter);
    }
    if (targets.isEmpty())
        return;

    RepositoryRecord &repo = m_repositories[m_repoDetailIndex];
    const QStringList encoded =
        ActionFile::setPinnedNode(repo.workflowNodes, targets, label);
    if (encoded == repo.workflowNodes)
        return;
    repo.workflowNodes = encoded;
    saveRepositories();
    logSystem(QStringLiteral("Actions: %1 now %2 for %3/%4.")
                  .arg(m_selectedWorkflowFilter.isEmpty()
                           ? QStringLiteral("every workflow")
                           : m_selectedWorkflowFilter,
                       label.isEmpty()
                           ? QStringLiteral("runs wherever the workflow says")
                           : QStringLiteral("runs on ") + label,
                       repo.owner, repo.name));



    if (m_actionWorkflowList) {
        QSignalBlocker block(m_actionWorkflowList);
        const RepositoryRecord &saved = m_repositories.at(m_repoDetailIndex);
        for (int row = 0; row < m_actionWorkflowList->count(); ++row) {
            QListWidgetItem *item = m_actionWorkflowList->item(row);
            const QString path = item->data(Qt::UserRole).toString();
            if (path.isEmpty())
                continue;
            for (const ActionWorkflow &wf : std::as_const(m_repoWorkflows)) {
                if (wf.path != path)
                    continue;
                updateWorkflowListItem(item, wf, saved);
                break;
            }
        }
    }
    updateManualRunBar();
    refreshWorkflowNodeCombo();
}

void MainWindow::updateManualRunBar()
{
    if (!m_actionManualRunBar)
        return;

    const ActionWorkflow *wf = nullptr;
    for (const ActionWorkflow &w : std::as_const(m_repoWorkflows)) {
        if (w.valid && w.path == m_selectedWorkflowFilter) {
            wf = &w;
            break;
        }
    }
    const bool haveRepo =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size();
    const bool show = haveRepo && wf && wf->allowsManualRun() &&
                      !isWorkflowDisabled(wf->path);
    m_actionManualRunBar->setVisible(show);
    if (!show)
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    m_actionManualRunButton->setText(
        QString::fromUtf8("Run \xE2\x80\x9C%1\xE2\x80\x9D").arg(wf->name));


    const bool ours = workflowRunsOnThisNode(repo, *wf);
    m_actionManualRunButton->setEnabled(ours);
    m_actionManualRunButton->setToolTip(
        ours ? QString()
             : QString::fromUtf8("Dedicated to %1 \xE2\x80\x94 start this "
                                 "workflow from that node.")
                   .arg(workflowDedicationLabel(repo, *wf)));



    QStringList branches;
    if (!repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists()) {
        QProcess refs;
        refs.start(QStringLiteral("git"),
                   {QStringLiteral("-C"), repo.mirrorPath,
                    QStringLiteral("for-each-ref"),
                    QStringLiteral("--format=%(refname:short)"),
                    QStringLiteral("refs/heads")});
        refs.waitForFinished(8000);
        branches = QString::fromUtf8(refs.readAllStandardOutput())
                       .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    const QString previous = m_actionRunBranchCombo->currentText().trimmed();
    QSignalBlocker block(m_actionRunBranchCombo);
    m_actionRunBranchCombo->clear();
    m_actionRunBranchCombo->addItems(branches);

    const QString want = previous.isEmpty() ? QStringLiteral("main") : previous;
    int idx = m_actionRunBranchCombo->findText(want);
    if (idx < 0 && want != QStringLiteral("main"))
        idx = m_actionRunBranchCombo->findText(QStringLiteral("main"));
    if (idx >= 0)
        m_actionRunBranchCombo->setCurrentIndex(idx);
    else if (m_actionRunBranchCombo->count() > 0)
        m_actionRunBranchCombo->setCurrentIndex(0);
    else
        m_actionRunBranchCombo->setEditText(QStringLiteral("main"));
}

void MainWindow::runSelectedWorkflowManually()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    const QString path = m_selectedWorkflowFilter;
    if (path.isEmpty() || repo.mirrorPath.isEmpty())
        return;
    const QString branch = m_actionRunBranchCombo->currentText().trimmed();
    if (branch.isEmpty()) {
        flashMessage(QStringLiteral("Choose a branch to run on."));
        return;
    }


    auto revParse = [&](const QString &rev) {
        QProcess p;
        p.start(QStringLiteral("git"),
                {QStringLiteral("-C"), repo.mirrorPath, QStringLiteral("rev-parse"),
                 QStringLiteral("--verify"), QStringLiteral("%1^{commit}").arg(rev)});
        p.waitForFinished(8000);
        return p.exitCode() == 0
                   ? QString::fromUtf8(p.readAllStandardOutput()).trimmed()
                   : QString();
    };
    QString commit = revParse(branch);
    if (commit.isEmpty())
        commit = revParse(QStringLiteral("refs/heads/") + branch);
    if (commit.isEmpty()) {
        flashMessage(
            QStringLiteral("Branch not found in this repo's mirror: %1").arg(branch));
        return;
    }



    QProcess show;
    show.start(QStringLiteral("git"),
               {QStringLiteral("-C"), repo.mirrorPath, QStringLiteral("show"),
                commit + QLatin1Char(':') + path});
    show.waitForFinished(10000);
    if (show.exitCode() != 0) {
        flashMessage(QString::fromUtf8("\xE2\x80\x9C%1\xE2\x80\x9D doesn't exist on %2.")
                         .arg(path, branch));
        return;
    }
    const QString content = QString::fromUtf8(show.readAllStandardOutput());
    const ActionWorkflow wf = ActionFile::parse(path, content);
    if (!wf.valid) {
        flashMessage(QStringLiteral("Workflow is invalid: %1").arg(wf.error));
        return;
    }


    if (!workflowRunsOnThisNode(repo, wf)) {
        flashMessage(QString::fromUtf8(
                         "\xE2\x80\x9C%1\xE2\x80\x9D runs on %2 \xE2\x80\x94 "
                         "start it from that node.")
                         .arg(wf.name, workflowDedicationLabel(repo, wf)));
        return;
    }

    ActionRun run;
    run.owner = repo.owner;
    run.name = repo.name;
    run.workflowPath = path;
    run.workflowName = wf.name;
    run.workflowContent = content;
    run.commit = commit;
    run.ref = QStringLiteral("refs/heads/") + branch;
    QString snapshotError;
    if (!bindActionRepositoryState(&run, repo.mirrorPath, &snapshotError)) {
        flashMessage(QStringLiteral(
                         "Could not bind this run to the repository state: %1")
                         .arg(snapshotError));
        return;
    }
    const bool approved = ActionStore::isApproved(
        run.repoKey(), path, content, run.repositoryTree,
        run.executionDigest);
    run.status = approved ? ActionStatus::Queued : ActionStatus::AwaitingApproval;

    const ActionRun created = m_actionStore->createRun(run);
    m_actionRuns.prepend(created);
    scheduleMirrorActionsSummary(0);
    cancelSupersededRuns(created);
    if (approved)
        m_actionQueue.append(created.id);
    logSystem(QStringLiteral("Actions: manual %1 \"%2\" for %3/%4 on %5 @ %6")
                  .arg(approved ? QStringLiteral("run of")
                                : QStringLiteral("run awaiting approval of"),
                       wf.name, repo.owner, repo.name, branch, commit.left(8)));



    refreshActionsTable();
    showRun(created.id);
    updateNotificationButton();
    processActionQueue();
}





static QString unifiedWorkflowDiff(const QString &prior, const QString &incoming,
                                   const QString &path)
{
    QTemporaryDir temp;
    if (!temp.isValid())
        return QString();

    const bool isNew = prior.isEmpty();
    const QString oldFile = temp.path() + QStringLiteral("/old");
    const QString newFile = temp.path() + QStringLiteral("/new");
    if (!isNew) {
        QFile f(oldFile);
        if (f.open(QIODevice::WriteOnly))
            f.write(prior.toUtf8());
    }
    {
        QFile f(newFile);
        if (f.open(QIODevice::WriteOnly))
            f.write(incoming.toUtf8());
    }

    const QByteArray out = gitCaptureStdout(
        temp.path(),
        {"diff", "--no-index", "--",
         isNew ? QStringLiteral("/dev/null") : oldFile, newFile});



    const QString shown = path.isEmpty() ? QStringLiteral("workflow") : path;
    QStringList lines = QString::fromUtf8(out).split(QLatin1Char('\n'));
    for (QString &line : lines) {
        if (line.startsWith(QLatin1String("diff --git ")))
            line = QStringLiteral("diff --git a/%1 b/%1").arg(shown);
        else if (line.startsWith(QLatin1String("--- ")))
            line = line.startsWith(QLatin1String("--- /dev/null"))
                       ? QStringLiteral("--- /dev/null")
                       : QStringLiteral("--- a/%1").arg(shown);
        else if (line.startsWith(QLatin1String("+++ ")))
            line = QStringLiteral("+++ b/%1").arg(shown);
    }
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::showRun(int runId)
{
    m_selectedRunId = runId;
    const ActionRun *run = findRun(runId);
    if (m_actionRerunButton)
        m_actionRerunButton->setVisible(run != nullptr);
    if (m_actionCopyLogButton)
        m_actionCopyLogButton->setVisible(run != nullptr);
    if (m_actionStopButton)
        m_actionStopButton->setVisible(run != nullptr &&
                                       (run->status == ActionStatus::Running ||
                                        run->status == ActionStatus::Queued));
    if (m_actionSkipButton)
        m_actionSkipButton->setVisible(run != nullptr &&
                                       (run->status == ActionStatus::Queued ||
                                        run->status == ActionStatus::AwaitingApproval));
    const bool fixable = run != nullptr && run->status == ActionStatus::Failed;
    if (m_actionFixButton)
        m_actionFixButton->setVisible(fixable);
    if (m_actionFixAgentCombo)
        m_actionFixAgentCombo->setVisible(fixable);
    if (m_actionFixModelCombo)
        m_actionFixModelCombo->setVisible(fixable);
    if (!run) {
        if (m_actionRunTitle)
            m_actionRunTitle->setText(QStringLiteral("Select a run"));
        if (m_actionRunMeta)
            m_actionRunMeta->clear();
        if (m_actionLog)
            m_actionLog->clear();
        if (m_actionApprovalBar)
            m_actionApprovalBar->hide();
        if (m_actionApprovalBanner)
            m_actionApprovalBanner->hide();
        if (m_actionDiff)
            m_actionDiff->hide();
        return;
    }

    if (m_actionRunTitle)
        m_actionRunTitle->setText(run->workflowName);
    if (m_actionRunMeta) {
        QString meta = QString::fromUtf8("%1/%2 \xC2\xB7 %3 \xC2\xB7 %4")
                           .arg(run->owner, run->name, run->commit.left(8),
                                actionStatusText(run->status));
        if (run->startedAtMs > 0 && run->finishedAtMs > run->startedAtMs)
            meta += QString::fromUtf8(" \xC2\xB7 %1s")
                        .arg((run->finishedAtMs - run->startedAtMs) / 1000);
        m_actionRunMeta->setText(meta);
    }

    const bool pending = run->status == ActionStatus::AwaitingApproval;
    if (pending && m_actionApprovalBanner) {
        const ActionWorkflow workflow =
            ActionFile::parse(run->workflowPath, run->workflowContent);
        QStringList graph;
        for (int index = 0; index < workflow.steps.size(); ++index) {
            const QString name = workflow.steps.at(index).name.trimmed();
            graph.append(QStringLiteral("%1. %2")
                             .arg(index + 1)
                             .arg(name.isEmpty()
                                      ? QStringLiteral("shell step")
                                      : name));
        }
        m_actionApprovalBanner->setText(
            QStringLiteral(
                "Review the complete execution graph before approving. "
                "Approval covers this exact repository state and is invalidated "
                "by any tracked-file change.\n\n"
                "Commit: %1\nTree: %2\nState SHA-256: %3\nWorkflow: %4\n"
                "Steps:\n%5\n\n"
                "The workflow diff below contains the exact shell commands. "
                "Secrets are exposed only after this bound approval.")
                .arg(run->commit, run->repositoryTree,
                     run->executionDigest, run->workflowPath,
                     graph.join(QLatin1Char('\n'))));
    }
    if (m_actionApprovalBanner)
        m_actionApprovalBanner->setVisible(pending);
    if (m_actionApprovalBar)
        m_actionApprovalBar->setVisible(pending);
    if (m_actionDiff)
        m_actionDiff->setVisible(pending);
    if (m_actionLog)
        m_actionLog->setVisible(!pending);

    if (pending && m_actionSplitButton) {


        m_actionSplitButton->setChecked(diffSplitPref());
        updateDiffSplitButton(m_actionSplitButton);
    }
    if (pending && m_actionDiff) {
        const QString prior =
            ActionStore::lastApprovedContent(run->repoKey(), run->workflowPath);
        const QString patch =
            unifiedWorkflowDiff(prior, run->workflowContent, run->workflowPath);
        QList<DiffFileEntry> files;
        QString html = renderDiffHtml(patch, files, QString(), QString(), QString());
        if (html.isEmpty())
            html = prior.isEmpty()
                       ? QStringLiteral(
                             "<p style='color:#8b949e'>This workflow has never "
                             "been approved.</p>")
                       : QStringLiteral(
                             "<p style='color:#8b949e'>No changes from the "
                             "approved workflow.</p>");
        setDiffHtml(m_actionDiff, html);
    } else if (m_actionLog) {
        m_actionLog->setPlainText(displaySafePlainLog(m_actionStore->readLog(*run)));
        m_actionLog->moveCursor(QTextCursor::End);
    }
}

void MainWindow::approveSelectedRun()
{
    ActionRun *run = findRun(m_selectedRunId);
    if (!run || run->status != ActionStatus::AwaitingApproval)
        return;
    const int repoIndex = repoIndexFor(run->owner, run->name);
    if (repoIndex < 0)
        return;
    const QString mirror = m_repositories.at(repoIndex).mirrorPath;
    ActionRun verified = *run;
    QString snapshotError;
    if (workflowContentAt(mirror, run->commit, run->workflowPath) !=
            run->workflowContent ||
        !bindActionRepositoryState(&verified, mirror, &snapshotError) ||
        verified.repositoryTree != run->repositoryTree ||
        verified.executionDigest != run->executionDigest) {
        flashMessage(QStringLiteral(
            "The repository state changed or could not be verified. Queue a "
            "fresh run before approving it."));
        return;
    }
    ActionStore::approve(run->repoKey(), run->workflowPath,
                         run->workflowContent, run->repositoryTree,
                         run->executionDigest);
    if (!ActionStore::isApproved(
            run->repoKey(), run->workflowPath, run->workflowContent,
            run->repositoryTree, run->executionDigest)) {
        flashMessage(QStringLiteral("The bound approval could not be saved."));
        return;
    }
    run->status = ActionStatus::Queued;
    m_actionStore->saveRun(*run);
    scheduleMirrorActionsSummary(0);
    m_actionQueue.append(run->id);
    logSystem(QStringLiteral("Actions: approved \"%1\" for %2/%3.")
                  .arg(run->workflowName, run->owner, run->name));
    m_actionRuns = m_actionStore->loadAllRuns();
    refreshActionsTable();
    showRun(m_selectedRunId);
    updateNotificationButton();
    processActionQueue();
}

void MainWindow::rejectSelectedRun()
{
    ActionRun *run = findRun(m_selectedRunId);
    if (!run || run->status != ActionStatus::AwaitingApproval)
        return;
    run->status = ActionStatus::Rejected;
    run->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_actionStore->saveRun(*run);
    scheduleMirrorActionsSummary(0);
    logSystem(QStringLiteral("Actions: rejected \"%1\" for %2/%3.")
                  .arg(run->workflowName, run->owner, run->name));
    m_actionRuns = m_actionStore->loadAllRuns();
    refreshActionsTable();
    showRun(m_selectedRunId);
    updateNotificationButton();
}

void MainWindow::rerunSelectedRun()
{
    const ActionRun *prev = findRun(m_selectedRunId);
    if (!prev || !m_actionStore)
        return;




    ActionRun run;
    run.owner = prev->owner;
    run.name = prev->name;
    run.workflowPath = prev->workflowPath;
    run.workflowName = prev->workflowName;
    run.workflowContent = prev->workflowContent;
    run.commit = prev->commit;
    run.ref = prev->ref;
    const int repoIndex = repoIndexFor(run.owner, run.name);
    if (repoIndex < 0)
        return;
    const ActionWorkflow rerunWorkflow =
        ActionFile::parse(run.workflowPath, run.workflowContent);
    if (!workflowRunsOnThisNode(m_repositories.at(repoIndex), rerunWorkflow)) {
        flashMessage(QString::fromUtf8(
                         "\xE2\x80\x9C%1\xE2\x80\x9D runs on %2 \xE2\x80\x94 "
                         "rerun it from that node.")
                         .arg(run.workflowName,
                              workflowDedicationLabel(
                                  m_repositories.at(repoIndex), rerunWorkflow)));
        return;
    }
    QString snapshotError;
    if (workflowContentAt(m_repositories.at(repoIndex).mirrorPath,
                          run.commit, run.workflowPath) !=
            run.workflowContent ||
        !bindActionRepositoryState(
            &run, m_repositories.at(repoIndex).mirrorPath,
            &snapshotError)) {
        flashMessage(QStringLiteral(
                         "Could not verify the repository state for this rerun: %1")
                         .arg(snapshotError));
        return;
    }
    const bool approved = ActionStore::isApproved(
        run.repoKey(), run.workflowPath, run.workflowContent,
        run.repositoryTree, run.executionDigest);
    run.status = approved ? ActionStatus::Queued : ActionStatus::AwaitingApproval;

    const ActionRun created = m_actionStore->createRun(run);
    m_actionRuns.prepend(created);
    scheduleMirrorActionsSummary(0);
    cancelSupersededRuns(created);
    if (approved)
        m_actionQueue.append(created.id);
    logSystem(QStringLiteral("Actions: rerun %1 \"%2\" for %3/%4 @ %5")
                  .arg(approved ? QStringLiteral("of")
                                : QStringLiteral("awaiting approval of"),
                       run.workflowName, run.owner, run.name, run.commit.left(8)));

    refreshActionsTable();
    showRun(created.id);
    updateNotificationButton();
    processActionQueue();
}

void MainWindow::stopSelectedRun()
{
    ActionRun *run = findRun(m_selectedRunId);
    if (!run)
        return;





    if (run->status == ActionStatus::Running) {
        if (ActionRunner *runner = runnerForRun(run->id)) {
            logSystem(QStringLiteral("Actions: stopping \"%1\" for %2/%3.")
                          .arg(run->workflowName, run->owner, run->name));
            runner->stop();
        }
        return;
    }



    if (run->status == ActionStatus::Queued) {
        m_actionQueue.removeAll(run->id);
        run->status = ActionStatus::Cancelled;
        run->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_actionStore->saveRun(*run);
        scheduleMirrorActionsSummary(0);
        logSystem(QStringLiteral("Actions: cancelled queued \"%1\" for %2/%3.")
                      .arg(run->workflowName, run->owner, run->name));
        m_actionRuns = m_actionStore->loadAllRuns();
        refreshActionsTable();
        showRun(m_selectedRunId);
        updateNotificationButton();
    }
}

void MainWindow::skipSelectedRun()
{
    ActionRun *run = findRun(m_selectedRunId);
    if (!run)
        return;




    if (run->status != ActionStatus::Queued &&
        run->status != ActionStatus::AwaitingApproval)
        return;

    m_actionQueue.removeAll(run->id);
    run->status = ActionStatus::Skipped;
    run->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_actionStore->saveRun(*run);
    scheduleMirrorActionsSummary(0);
    logSystem(QStringLiteral("Actions: skipped \"%1\" for %2/%3.")
                  .arg(run->workflowName, run->owner, run->name));
    m_actionRuns = m_actionStore->loadAllRuns();
    refreshActionsTable();
    showRun(m_selectedRunId);
    updateNotificationButton();
}

void MainWindow::fixSelectedRunWithAgent(const QString &provider, const QString &model)
{
    const ActionRun *run = findRun(m_selectedRunId);
    if (!run)
        return;
    const int repoIndex = repoIndexFor(run->owner, run->name);
    if (repoIndex < 0) {
        flashMessage("Can't find this run's repository.", true);
        return;
    }



    const QString log = m_actionStore ? m_actionStore->readLog(*run) : QString();
    constexpr int kMaxLogChars = 12000;
    const QString logTail =
        log.size() <= kMaxLogChars
            ? log
            : QStringLiteral("...(log truncated; showing the tail)...\n") +
                  log.right(kMaxLogChars);

    const QString prompt =
        QStringLiteral(
            "The \"%1\" CI workflow failed for %2/%3 (commit %4, ref %5). Find "
            "what broke and fix it so the workflow succeeds. Full run log:\n\n%6")
            .arg(run->workflowName, run->owner, run->name, run->commit.left(8),
                 run->ref, logTail);

    const int sessionId =
        startAdHocAgentForRepo(repoIndex, prompt, provider,  true, model);
    if (sessionId > 0)
        flashMessage(QStringLiteral("Started a %1 agent to fix \"%2\".")
                         .arg(agentProviderName(provider), run->workflowName));
}










void MainWindow::maybeAutoFixFailedRun(const ActionRun &run)
{
    if (!QSettings().value(kAutoFixFailuresSetting, true).toBool())
        return;
    const QString branch = run.ref.startsWith(QLatin1String("refs/heads/"))
                               ? run.ref.mid(11)
                               : run.ref;
    if (branch.isEmpty())
        return;
    int sessionId = 0;
    QString sessionProvider;
    for (const AgentSession &s : std::as_const(m_agentSessions)) {
        if (s.owner == run.owner && s.name == run.name &&
            s.branchName == branch && !isExternalSession(s.id)) {
            sessionId = s.id;
            sessionProvider = s.provider;
        }
    }
    if (sessionId <= 0)
        return;
    const AgentSession *session = findAgentSession(sessionId);
    if (!session || session->status == AgentStatus::Running ||
        session->status == AgentStatus::Queued ||
        session->status == AgentStatus::Waiting)
        return;

    const QString log = m_actionStore ? m_actionStore->readLog(run) : QString();
    constexpr int kMaxLogChars = 12000;
    const QString logTail =
        log.size() <= kMaxLogChars
            ? log
            : QStringLiteral("...(log truncated; showing the tail)...\n") +
                  log.right(kMaxLogChars);
    const QString prompt =
        QStringLiteral(
            "The \"%1\" CI workflow failed for %2/%3 (commit %4, ref %5). Find "
            "what broke and fix it so the workflow succeeds. Full run log:\n\n%6")
            .arg(run.workflowName, run.owner, run.name, run.commit.left(8),
                 run.ref, logTail);
    const QString workflowName = run.workflowName;

    m_pendingSteerMessage.insert(sessionId, prompt);
    if (sessionProvider == QLatin1String("claude-code") ||
        agentIsCodexProvider(sessionProvider))
        applyTranscriptEvent(
            sessionId,
            QJsonObject{{QStringLiteral("type"), QStringLiteral("_local_user")},
                        {QStringLiteral("text"), prompt}});
    continueAgentSession(sessionId);
    flashMessage(QStringLiteral("Sent \"%1\"'s failure back to its agent.")
                     .arg(workflowName));
}

void MainWindow::clearActionRuns()
{
    if (!m_actionStore || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const QString owner = m_repositories.at(m_repoDetailIndex).owner;
    const QString name = m_repositories.at(m_repoDetailIndex).name;




    QList<ActionRun> doomed;
    for (const ActionRun &run : std::as_const(m_actionRuns)) {
        if (run.owner != owner || run.name != name)
            continue;
        if (!m_selectedWorkflowFilter.isEmpty() &&
            run.workflowPath != m_selectedWorkflowFilter)
            continue;
        if (run.status == ActionStatus::Running ||
            run.status == ActionStatus::Queued)
            continue;
        doomed.append(run);
    }
    if (doomed.isEmpty()) {
        flashMessage(QStringLiteral("No finished runs to clear."));
        return;
    }

    if (QMessageBox::question(
            this, QStringLiteral("Clear runs"),
            QStringLiteral("Delete %1 run%2 from this list, including their "
                           "logs? This can't be undone.")
                .arg(doomed.size())
                .arg(doomed.size() == 1 ? QString() : QStringLiteral("s")),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes)
        return;

    for (const ActionRun &run : std::as_const(doomed))
        m_actionStore->deleteRun(run);

    m_actionRuns = m_actionStore->loadAllRuns();
    scheduleMirrorActionsSummary(0);
    if (!findRun(m_selectedRunId)) {
        m_selectedRunId = -1;
        showRun(-1);
    }
    refreshActionsTable();
    updateNotificationButton();
}

QWidget *MainWindow::buildRepoActionsTab()
{
    auto *page = new QWidget;


    auto *wfPane = new QWidget;

    wfPane->setMinimumWidth(320);
    wfPane->setMaximumWidth(440);
    auto *wfHeading = new QLabel("Workflows");
    wfHeading->setObjectName("sectionLabel");
    auto *wfHint = new QLabel(
        "Actions defined in .forkmesh/. They run when a fork pushes to this "
        "repo's mirror.");
    wfHint->setObjectName("statusLine");
    wfHint->setWordWrap(true);
    m_actionWorkflowList = new QListWidget;
    m_actionWorkflowList->setObjectName("actionWorkflowList");
    m_actionWorkflowList->setFrameShape(QFrame::NoFrame);
    m_actionWorkflowList->setAlternatingRowColors(true);
    connect(m_actionWorkflowList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                m_selectedWorkflowFilter =
                    item ? item->data(Qt::UserRole).toString() : QString();
                refreshActionsTable();
                showLatestVisibleActionRun();
                updateManualRunBar();
                refreshWorkflowNodeCombo();
            });


    connect(m_actionWorkflowList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
                if (!item || !(item->flags() & Qt::ItemIsUserCheckable))
                    return;
                const QString path = item->data(Qt::UserRole).toString();
                setWorkflowDisabled(path, item->checkState() != Qt::Checked);
            });


    m_actionsEnabledCheck = new QCheckBox("Run actions on push");
    m_actionsEnabledCheck->setToolTip(
        "When a fork pushes to this repo's local mirror, run its .forkmesh/ "
        "workflows. Changed workflows still require approval below before they "
        "run.");
    connect(m_actionsEnabledCheck, &QCheckBox::toggled, this,
            [this](bool on) { setRepoActionsEnabled(on); });




    m_actionNodeLabel = new QLabel("Run every workflow on");
    m_actionNodeLabel->setObjectName("statusLine");
    m_actionNodeCombo = new QComboBox;
    m_actionNodeCombo->setObjectName("actionNodeCombo");
    m_actionNodeCombo->setMinimumWidth(150);
    m_actionNodeCombo->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_actionNodeCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                if (index < 0 || !m_actionNodeCombo)
                    return;
                setWorkflowNode(m_actionNodeCombo->itemData(index).toString());
            });
    auto *nodeRow = new QHBoxLayout;
    nodeRow->setContentsMargins(0, 0, 0, 0);
    nodeRow->setSpacing(8);
    nodeRow->addWidget(m_actionNodeLabel);
    nodeRow->addWidget(m_actionNodeCombo, 1);

    auto *wfLayout = new QVBoxLayout(wfPane);
    wfLayout->setContentsMargins(16, 22, 8, 22);
    wfLayout->setSpacing(8);
    wfLayout->addWidget(wfHeading);
    wfLayout->addWidget(wfHint);
    wfLayout->addWidget(m_actionsEnabledCheck);
    wfLayout->addLayout(nodeRow);
    wfLayout->addWidget(m_actionWorkflowList, 1);


    auto *listPane = new QWidget;


    listPane->setMinimumWidth(475);
    auto *heading = new QLabel("Runs");
    heading->setObjectName("channelTitle");


    auto *clearRunsButton = new QPushButton("Clear");
    clearRunsButton->setObjectName("ghostButton");
    clearRunsButton->setProperty("buttonSize", "sm");
    clearRunsButton->setCursor(Qt::PointingHandCursor);
    clearRunsButton->setToolTip("Delete the runs listed here, including their logs");
    setOcticon(clearRunsButton, "trash", 16);
    connect(clearRunsButton, &QPushButton::clicked, this,
            &MainWindow::clearActionRuns);
    auto *runsHeaderRow = new QHBoxLayout;
    runsHeaderRow->setContentsMargins(0, 0, 0, 0);
    runsHeaderRow->addWidget(heading);
    runsHeaderRow->addStretch();
    runsHeaderRow->addWidget(clearRunsButton);
    auto *subtitle = new QLabel(
        "Changed workflows wait for your approval before they run.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    m_actionsTable = new QTableWidget(0, 4);
    installColumnHeaderMenu(m_actionsTable);
    m_actionsTable->setObjectName("issueTable");
    m_actionsTable->setHorizontalHeaderLabels(
        {"Workflow", "Status", "When", "Duration"});
    m_actionsTable->horizontalHeader()->setStretchLastSection(true);
    m_actionsTable->horizontalHeader()->setHighlightSections(false);


    m_actionsTable->setColumnWidth(0, 200);
    m_actionsTable->verticalHeader()->setVisible(false);
    m_actionsTable->setShowGrid(false);
    m_actionsTable->setWordWrap(false);
    m_actionsTable->setAlternatingRowColors(true);
    makeColumnsResizable(m_actionsTable);
    m_actionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_actionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_actionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);


    m_actionsTable->setItemDelegate(new ActionFailureBorderDelegate(m_actionsTable));
    connect(m_actionsTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const QModelIndexList rows =
            m_actionsTable->selectionModel()->selectedRows();
        if (rows.isEmpty())
            return;
        QTableWidgetItem *first = m_actionsTable->item(rows.first().row(), 0);
        if (first)
            showRun(first->data(Qt::UserRole).toInt());
    });

    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(12, 22, 12, 22);
    listLayout->setSpacing(8);
    listLayout->addLayout(runsHeaderRow);
    listLayout->addWidget(subtitle);
    listLayout->addWidget(m_actionsTable, 1);


    auto *detailPane = new QWidget;
    m_actionRunTitle = new QLabel("Select a run");
    m_actionRunTitle->setObjectName("channelTitle");
    m_actionRunMeta = new QLabel;
    m_actionRunMeta->setObjectName("statusLine");
    m_actionRunMeta->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_actionApprovalBanner = new QLabel(
        "This workflow is new or changed. Review the difference "
        "below, then Approve to run it (secrets are only exposed after approval).");
    m_actionApprovalBanner->setTextFormat(Qt::PlainText);
    m_actionApprovalBanner->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_actionApprovalBanner->setWordWrap(true);
    m_actionApprovalBanner->setStyleSheet(
        "color:#d29922; background:#1c1908; border:1px solid #3a3416; "
        "border-radius:6px; padding:8px;");
    m_actionApprovalBanner->hide();

    m_actionDiff = new QTextBrowser;
    m_actionDiff->setReadOnly(true);
    m_actionDiff->setOpenExternalLinks(false);
    m_actionDiff->setFontFamily(QStringLiteral("monospace"));
    m_actionDiff->hide();
    registerDiffView(m_actionDiff);

    m_actionApproveButton = new QPushButton("Approve & run");
    m_actionApproveButton->setObjectName("primaryButton");
    m_actionApproveButton->setCursor(Qt::PointingHandCursor);
    m_actionRejectButton = new QPushButton("Reject");
    m_actionRejectButton->setObjectName("dangerButton");
    m_actionRejectButton->setCursor(Qt::PointingHandCursor);
    connect(m_actionApproveButton, &QPushButton::clicked, this,
            &MainWindow::approveSelectedRun);
    connect(m_actionRejectButton, &QPushButton::clicked, this,
            &MainWindow::rejectSelectedRun);


    m_actionSplitButton = new QPushButton;
    m_actionSplitButton->setObjectName("ghostButton");
    m_actionSplitButton->setCursor(Qt::PointingHandCursor);
    m_actionSplitButton->setCheckable(true);
    m_actionSplitButton->setChecked(diffSplitPref());
    setOcticon(m_actionSplitButton, "diff", 16);
    updateDiffSplitButton(m_actionSplitButton);
    connect(m_actionSplitButton, &QPushButton::clicked, this, [this](bool on) {
        setDiffSplitPref(on);
        updateDiffSplitButton(m_actionSplitButton);
        showRun(m_selectedRunId);
    });
    m_actionApprovalBar = new QWidget;
    auto *approvalRow = new QHBoxLayout(m_actionApprovalBar);
    approvalRow->setContentsMargins(0, 0, 0, 0);
    approvalRow->addWidget(m_actionApproveButton);
    approvalRow->addWidget(m_actionRejectButton);
    approvalRow->addStretch();
    approvalRow->addWidget(m_actionSplitButton);
    m_actionApprovalBar->hide();

    m_actionLog = new QPlainTextEdit;
    m_actionLog->setReadOnly(true);
    m_actionLog->setObjectName("actionLog");
    applyLogFont(m_actionLog);
    m_actionLog->setLineWrapMode(QPlainTextEdit::NoWrap);
    new AgentLogHighlighter(m_actionLog->document());
    m_actionLog->setMaximumBlockCount(20000);




    m_actionManualRunBar = new QWidget;
    m_actionManualRunButton = new QPushButton("Run workflow");
    m_actionManualRunButton->setObjectName("primaryButton");
    m_actionManualRunButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_actionManualRunButton, "rocket", 16);
    m_actionRunBranchCombo = new QComboBox;
    m_actionRunBranchCombo->setEditable(true);
    m_actionRunBranchCombo->setMinimumWidth(160);
    m_actionRunBranchCombo->setToolTip("Branch to check out and run the workflow on");
    auto *branchLabel = new QLabel("on branch");
    branchLabel->setObjectName("statusLine");
    connect(m_actionManualRunButton, &QPushButton::clicked, this,
            &MainWindow::runSelectedWorkflowManually);
    auto *manualRow = new QHBoxLayout(m_actionManualRunBar);
    manualRow->setContentsMargins(0, 0, 0, 0);
    manualRow->setSpacing(8);
    manualRow->addWidget(m_actionManualRunButton);
    manualRow->addWidget(branchLabel);
    manualRow->addWidget(m_actionRunBranchCombo);
    manualRow->addStretch();
    m_actionManualRunBar->hide();



    m_actionRerunButton = new QPushButton("Rerun");
    m_actionRerunButton->setObjectName("ghostButton");
    m_actionRerunButton->setProperty("buttonSize", "sm");
    m_actionRerunButton->setCursor(Qt::PointingHandCursor);
    m_actionRerunButton->setToolTip("Run this action again with the same commit");
    setOcticon(m_actionRerunButton, "sync", 16);
    m_actionRerunButton->hide();
    connect(m_actionRerunButton, &QPushButton::clicked, this,
            &MainWindow::rerunSelectedRun);



    m_actionStopButton = new QPushButton("Stop");
    m_actionStopButton->setObjectName("dangerButton");
    m_actionStopButton->setProperty("buttonSize", "sm");
    m_actionStopButton->setCursor(Qt::PointingHandCursor);
    m_actionStopButton->setToolTip("Stop this run");
    setOcticon(m_actionStopButton, "stop", 16);
    m_actionStopButton->hide();
    connect(m_actionStopButton, &QPushButton::clicked, this,
            &MainWindow::stopSelectedRun);



    m_actionSkipButton = new QPushButton("Skip");
    m_actionSkipButton->setObjectName("ghostButton");
    m_actionSkipButton->setProperty("buttonSize", "sm");
    m_actionSkipButton->setCursor(Qt::PointingHandCursor);
    m_actionSkipButton->setToolTip("Skip this run without executing it");
    setOcticon(m_actionSkipButton, "circle-slash", 16);
    m_actionSkipButton->hide();
    connect(m_actionSkipButton, &QPushButton::clicked, this,
            &MainWindow::skipSelectedRun);



    m_actionCopyLogButton = new QPushButton("Copy log");
    m_actionCopyLogButton->setObjectName("ghostButton");
    m_actionCopyLogButton->setProperty("buttonSize", "sm");
    m_actionCopyLogButton->setCursor(Qt::PointingHandCursor);
    m_actionCopyLogButton->setToolTip("Copy this run's full log to the clipboard");
    setOcticon(m_actionCopyLogButton, "copy", 16);
    m_actionCopyLogButton->hide();
    connect(m_actionCopyLogButton, &QPushButton::clicked, this, [this] {
        const ActionRun *run = findRun(m_selectedRunId);
        const QString log = run && m_actionStore ? m_actionStore->readLog(*run)
                                                 : (m_actionLog ? m_actionLog->toPlainText()
                                                                : QString());
        if (log.isEmpty()) {
            flashMessage(QStringLiteral("No log to copy yet."));
            return;
        }
        QApplication::clipboard()->setText(log);
        flashMessage(QStringLiteral("Run log copied to the clipboard."));
    });





    m_actionFixButton = new QPushButton("Fix with agent");
    m_actionFixButton->setObjectName("ghostButton");
    m_actionFixButton->setProperty("buttonSize", "sm");
    m_actionFixButton->setCursor(Qt::PointingHandCursor);
    m_actionFixButton->setToolTip("Start a new coding agent to fix this failed run");
    setOcticon(m_actionFixButton, "rocket", 16);
    m_actionFixButton->hide();
    connect(m_actionFixButton, &QPushButton::clicked, this, [this] {
        const QString provider = m_actionFixAgentCombo
                                     ? m_actionFixAgentCombo->currentData().toString()
                                     : QStringLiteral("claude-code");
        const QString model = m_actionFixModelCombo
                                  ? m_actionFixModelCombo->currentData().toString()
                                  : QString();
        fixSelectedRunWithAgent(provider, model);
    });




    m_actionFixAgentCombo = new QComboBox;
    m_actionFixAgentCombo->setObjectName("issueControlSm");
    m_actionFixAgentCombo->setCursor(Qt::PointingHandCursor);
    m_actionFixAgentCombo->setToolTip("Which agent fixes this run");
    m_actionFixAgentCombo->addItem(QStringLiteral("Claude"), QStringLiteral("claude"));
    m_actionFixAgentCombo->addItem(QStringLiteral("OpenAI"), QStringLiteral("openai"));
    m_actionFixAgentCombo->addItem(QStringLiteral("CC"),
                                   QStringLiteral("claude-code"));
    m_actionFixAgentCombo->hide();


    {
        const QString def = defaultAgentProvider();
        const QString want = def == QLatin1String("claude-api")
                                 ? QStringLiteral("claude")
                                 : def;
        const int idx = m_actionFixAgentCombo->findData(want);
        m_actionFixAgentCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }



    m_actionFixModelCombo = new QComboBox;
    m_actionFixModelCombo->setObjectName("issueControlSm");
    m_actionFixModelCombo->setCursor(Qt::PointingHandCursor);
    m_actionFixModelCombo->setToolTip("Which model the agent uses");
    m_actionFixModelCombo->setProperty("claudeModelCombo", true);
    m_actionFixModelCombo->view()->installEventFilter(this);
    m_actionFixModelCombo->hide();
    fillAgentFixModelCombo(m_actionFixModelCombo,
                          m_actionFixAgentCombo->currentData().toString());
    applyLiveClaudeModelsToCombos();
    connect(m_actionFixAgentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (m_actionFixAgentCombo && m_actionFixModelCombo) {
                    fillAgentFixModelCombo(
                        m_actionFixModelCombo,
                        m_actionFixAgentCombo->currentData().toString());
                    applyLiveClaudeModelsToCombos();
                }
            });

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->addWidget(m_actionRunTitle);
    titleRow->addStretch();
    titleRow->addWidget(m_actionStopButton);
    titleRow->addWidget(m_actionSkipButton);
    titleRow->addWidget(m_actionCopyLogButton);
    titleRow->addWidget(m_actionFixButton);
    titleRow->addWidget(m_actionFixAgentCombo);
    titleRow->addWidget(m_actionFixModelCombo);
    titleRow->addWidget(m_actionRerunButton);

    auto *detailLayout = new QVBoxLayout(detailPane);
    detailLayout->setContentsMargins(12, 22, 24, 22);
    detailLayout->setSpacing(8);
    detailLayout->addWidget(m_actionManualRunBar);
    detailLayout->addLayout(titleRow);
    detailLayout->addWidget(m_actionRunMeta);
    detailLayout->addWidget(m_actionApprovalBanner);
    detailLayout->addWidget(m_actionApprovalBar);
    detailLayout->addWidget(m_actionDiff, 1);
    detailLayout->addWidget(m_actionLog, 2);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(wfPane);
    splitter->addWidget(listPane);
    splitter->addWidget(detailPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 0);
    splitter->setStretchFactor(2, 1);





    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter, 1);
    refreshWorkflowNodeCombo();
    return page;
}



void MainWindow::reloadVariablesTable()
{
    if (!m_varsTable)
        return;
    const QMap<QString, QString> vars = ActionStore::variables();
    QSignalBlocker block(m_varsTable);
    TableRepaintGuard repaintGuard(m_varsTable);
    m_varsTable->setRowCount(0);
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
        const int row = m_varsTable->rowCount();
        m_varsTable->insertRow(row);
        m_varsTable->setItem(row, 0, new QTableWidgetItem(it.key()));


        auto *valueItem = new QTableWidgetItem(
            m_varsRevealed ? it.value()
                           : QString(qMin(it.value().size(), 24), QChar(0x2022)));
        valueItem->setData(Qt::UserRole, it.value());

        if (m_varsRevealed)
            valueItem->setToolTip("Click to copy to clipboard");
        m_varsTable->setItem(row, 1, valueItem);
    }
}

void MainWindow::addOrEditVariable(bool editSelected)
{
    if (!m_varsTable)
        return;

    QString name, value;



    int editRow = -1;
    if (editSelected) {
        editRow = m_varsTable->currentRow();
        if (editRow < 0) {
            const QList<QTableWidgetItem *> selected = m_varsTable->selectedItems();
            if (!selected.isEmpty())
                editRow = selected.first()->row();
        }
        if (editRow < 0 || !m_varsTable->item(editRow, 0)) {
            QMessageBox::information(this, "Edit variable",
                                    "Select a variable in the list to edit.");
            return;
        }
        name = m_varsTable->item(editRow, 0)->text();
        value = m_varsTable->item(editRow, 1)->data(Qt::UserRole).toString();
    }
    const bool editing = editRow >= 0;

    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, editing ? "Edit variable" : "Add variable",
        "Name (e.g. CLOUDFLARE_API_TOKEN):", QLineEdit::Normal, name, &ok);
    if (!ok || newName.trimmed().isEmpty())
        return;
    const QString newValue = QInputDialog::getText(
        this, editing ? "Edit variable" : "Add variable", "Value:",
        QLineEdit::Password, value, &ok);
    if (!ok)
        return;

    QMap<QString, QString> vars = ActionStore::variables();
    if (editing && newName.trimmed() != name)
        vars.remove(name);
    vars.insert(newName.trimmed(), newValue);
    ActionStore::setVariables(vars);
    reloadVariablesTable();
}

void MainWindow::deleteSelectedVariable()
{
    if (!m_varsTable)
        return;
    const QList<QTableWidgetItem *> selected = m_varsTable->selectedItems();
    if (selected.isEmpty())
        return;
    const QString name = m_varsTable->item(selected.first()->row(), 0)->text();
    QMap<QString, QString> vars = ActionStore::variables();
    vars.remove(name);
    ActionStore::setVariables(vars);
    reloadVariablesTable();
}

void MainWindow::exportVariables()
{
    const QMap<QString, QString> vars = ActionStore::variables();
    if (vars.isEmpty()) {
        QMessageBox::information(this, "Export variables",
                                 "There are no variables or secrets to export.");
        return;
    }

    const int confirm = QMessageBox::warning(
        this, "Export variables",
        "This export writes secret values in clear text. Keep the file private.",
        QMessageBox::Cancel | QMessageBox::Ok, QMessageBox::Cancel);
    if (confirm != QMessageBox::Ok)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, "Export variables / secrets", "forkmesh-variables.json",
        "ForkMesh variables (*.json);;All files (*)");
    if (path.isEmpty())
        return;

    QJsonObject variables;
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        variables.insert(it.key(), it.value());

    QJsonObject root;
    root.insert("kind", "forkmesh-action-variables-v1");
    root.insert("exportedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert("variables", variables);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Export failed",
                             "Could not write " + path);
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        QMessageBox::warning(this, "Export failed",
                             "Could not save " + path);
        return;
    }

    flashMessage(QStringLiteral("Exported %1 variable%2.")
                     .arg(vars.size())
                     .arg(vars.size() == 1 ? QString() : QStringLiteral("s")));
}

void MainWindow::importVariables()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Import variables / secrets", QString(),
        "ForkMesh variables (*.json);;JSON files (*.json);;All files (*)");
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Import failed",
                             "Could not read " + path);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(
            this, "Import failed",
            "That file is not valid variables JSON: " + parseError.errorString());
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonObject source =
        root.value("variables").isObject() ? root.value("variables").toObject()
                                           : root;
    QMap<QString, QString> imported;
    QStringList skipped;
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        const QString name = it.key().trimmed();
        if (name.isEmpty() || !it.value().isString()) {
            skipped.append(it.key());
            continue;
        }
        imported.insert(name, it.value().toString());
    }

    if (imported.isEmpty()) {
        QMessageBox::warning(this, "Import failed",
                             "No string variables were found in that file.");
        return;
    }

    QMap<QString, QString> next = ActionStore::variables();
    const bool hasExisting = !next.isEmpty();
    if (hasExisting) {
        QMessageBox choice(this);
        choice.setWindowTitle("Import variables");
        choice.setText("Import " + QString::number(imported.size()) +
                       " variable" + (imported.size() == 1 ? "" : "s") + "?");
        choice.setInformativeText(
            "Merge keeps existing variables and overwrites matching names. "
            "Replace clears the current list first.");
        QPushButton *mergeButton =
            choice.addButton("Merge", QMessageBox::AcceptRole);
        QPushButton *replaceButton =
            choice.addButton("Replace", QMessageBox::DestructiveRole);
        choice.addButton(QMessageBox::Cancel);
        choice.setDefaultButton(mergeButton);
        choice.exec();
        if (choice.clickedButton() == replaceButton)
            next.clear();
        else if (choice.clickedButton() != mergeButton)
            return;
    }

    for (auto it = imported.constBegin(); it != imported.constEnd(); ++it)
        next.insert(it.key(), it.value());
    ActionStore::setVariables(next);
    reloadVariablesTable();

    QString message = QStringLiteral("Imported %1 variable%2.")
                          .arg(imported.size())
                          .arg(imported.size() == 1 ? QString()
                                                    : QStringLiteral("s"));
    if (!skipped.isEmpty())
        message += QStringLiteral(" Skipped %1 non-string entr%2.")
                       .arg(skipped.size())
                       .arg(skipped.size() == 1 ? QStringLiteral("y")
                                                : QStringLiteral("ies"));
    flashMessage(message);
}

void MainWindow::toggleVariablesRevealed()
{
    m_varsRevealed = !m_varsRevealed;
    if (m_varsRevealButton)
        m_varsRevealButton->setText(m_varsRevealed ? "Hide" : "Reveal");
    reloadVariablesTable();
}

void MainWindow::persistVariablesFromTable()
{

}
