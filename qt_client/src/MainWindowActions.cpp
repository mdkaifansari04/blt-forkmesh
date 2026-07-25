// MainWindowActions: MainWindow feature methods, split out of MainWindow.cpp.
// Actions (CI on push), notifications, and variables/secrets settings.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"
#include "MirrorActionsConfiguration.h"
#include "MirrorActionsSummary.h"

#include <QCryptographicHash>
#include <QSaveFile>

using namespace forkmesh::ui;

// ---- Actions (CI on push to the mirror) -----------------------------------

namespace {

QString actionRunLogPath(const ActionRun &run)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/actions/runs/") + run.repoKey() + QLatin1Char('/') +
           QString::number(run.id) + QStringLiteral("/log.txt");
}

// First line (after the shebang) of the working-copy commit-signal hooks we
// install; install/remove only ever touch a hook file carrying this marker.
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

} // namespace

void MainWindow::initActions()
{
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/actions");
    m_actionStore = new ActionStore(root);
    // Size the runner pool so independent workflows run in parallel without
    // swamping a small node: cap it to the machine's cores, but keep at least
    // two so a heavy build (e.g. the Flutter Android APK) never single-handedly
    // blocks the CI suite or the Cloudflare deploy behind it.
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
    // A run still marked Running was interrupted by a previous shutdown; it can't
    // resume, so record it as failed. Re-queue anything that was only queued.
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

    // Fallback poll: QFileSystemWatcher can miss rapid create+rename events, so
    // also sweep the spool on a short interval. The watcher keeps it snappy; the
    // poll guarantees a push is never silently dropped.
    auto *poll = new QTimer(this);
    poll->setInterval(4000);
    connect(poll, &QTimer::timeout, this, &MainWindow::scanActionSpool);
    poll->start();

    // Catch pushes that landed while we were closed, then drain the queue.
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
        return; // mirror not cloned yet; installed on the next sync
    const QString hooksDir = repo.mirrorPath + QStringLiteral("/hooks");
    QDir().mkpath(hooksDir);

    // A small POSIX-sh post-receive hook: it appends one event file per push to
    // the spool dir (atomically via a .tmp rename) for the app to pick up.
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

    // Point the working copy's push URL at the local bare mirror so a plain
    // `git push origin ...` from the terminal lands directly in the served
    // mirror (whose post-receive hook above runs actions + re-attests), instead
    // of round-tripping through the relay. The relay now also accepts
    // git-receive-pack over the tunnel (issue #358) for pushes from other
    // machines; this local shortcut just avoids the network hop for the owner on
    // this box. The fetch URL is left alone so the owner can still pull.
    // Best-effort.
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
    // A commit made straight in the source-of-truth working copy (terminal,
    // IDE, a coding agent — including linked worktrees, which run the main
    // checkout's hooks) never touches the served bare mirror, so it only
    // reached the mirror — and the "mirror-update" broadcast to peers — at the
    // next periodic auto-sync tick. These hooks spool a ".commit" event the
    // moment HEAD moves; scanActionSpool turns it into propagateRepoUpdate,
    // which syncs the bare mirror and pushes the ephemeral "mirror-update"
    // frame out over the relay websocket so every mirror node fetches
    // immediately instead of waiting for its next heartbeat.
    if (repo.previewOnly || !m_actionStore)
        return;
    const QString localPath = repo.localPath.trimmed();
    // Only a normal checkout (.git as a directory) gets the hooks; a gitfile
    // checkout (linked worktree/submodule) runs its parent's hooks anyway.
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

    // post-commit covers plain commits; post-merge covers `git pull`/merges,
    // which do not run post-commit.
    for (const char *hook : {"post-commit", "post-merge"}) {
        const QString path = hooksDir + QLatin1Char('/') + QLatin1String(hook);
        // The working copy is user territory (unlike the app-managed bare
        // mirror): never clobber a hook we didn't write ourselves.
        QFile existing(path);
        if (existing.exists()) {
            if (!existing.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QString body = QString::fromUtf8(existing.readAll());
            existing.close();
            if (!body.contains(QLatin1String(kCommitSignalMarker)))
                continue;
            if (body == script)
                continue; // already current
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
    // Also drop the working-copy commit-signal hooks — but only ours (marker
    // check), never a hook the user wrote.
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
    // Install the hook on every mirror, not just actions-enabled ones: it only
    // writes a spool event, which we also use to refresh the open Code view in
    // real time. Workflow execution is still gated on actionsEnabled.
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

    // ".commit" events: the working copy's post-commit/post-merge hook saw HEAD
    // move (a commit landed on the source of truth outside the app — terminal,
    // IDE, or an agent worktree). propagateRepoUpdate syncs the served bare
    // mirror from the working copy and, on a detected change, broadcasts the
    // ephemeral "mirror-update" websocket frame so every mirror node fetches
    // right now instead of at its next heartbeat.
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
            QFile::remove(full); // stale event for a repo we no longer hold
            continue;
        }
        // A sync already in flight can't pick up a commit that lands mid-fetch:
        // leave the event in the spool so the fallback poll retries it once the
        // repo is released, instead of silently dropping the update until the
        // next periodic tick.
        if (m_syncingRepos.contains(idx))
            continue;
        QFile::remove(full);
        if (propagated.contains(idx))
            continue; // a burst of commits needs only one sync
        propagated.insert(idx);
        propagateRepoUpdate(idx);
    }

    const QStringList files =
        dir.entryList({QStringLiteral("*.push")}, QDir::Files, QDir::Name);
    // Re-attest the integrity pin at most once per repo per sweep, even if several
    // pushes spooled.
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
        // A push landed directly on our served bare mirror (terminal/IDE
        // `git push origin`, or an agent), advancing the refs the relay hands out.
        // The relay pins an owner-signed hash of those refs and rejects any clone
        // that doesn't match it, so re-attest now to keep the pin in step with what
        // we serve. The fetch-based autoSync path re-publishes on its own, but a
        // direct push leaves the mirror already current — so autoSync sees no change
        // and never refreshes the pin; this closes that gap. Runs for any ref change
        // (branch, tag, or deletion), so it can't reuse the refs/heads-only `commit`
        // captured above for action triggering.
        if (!owner.isEmpty() && !name.isEmpty()) {
            const int idx = repoIndexFor(owner, name);
            if (idx >= 0 && !reattested.contains(idx)) {
                reattested.insert(idx);
                const RepositoryRecord &r = m_repositories.at(idx);
                if (!r.previewOnly && r.publishToNetwork &&
                    !r.mirrorPath.trimmed().isEmpty())
                    publishRepository(idx, false);
                // Tell connected peers that also mirror this repo that it just
                // advanced, the same ephemeral "mirror-update" frame
                // syncRepository broadcasts for a fetch-detected change (see
                // MainWindow::syncRepository/onPeerMirrorUpdated). A push that
                // lands directly on this served bare mirror never goes through
                // syncRepository, so without this, peers would only notice at
                // their next 15-minute auto-sync tick instead of converging in
                // seconds.
                if (!r.previewOnly && m_backend)
                    m_backend->notifyMirrorUpdated(
                        catalogOwner(r) + "/" +
                            repoSegment(r.name, QStringLiteral("repository")),
                        commit);
            }
        }
        // Skip events with no branch update or a branch deletion (all-zero SHA).
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

    // The short-lived helper may have added the gateway-backed Actions mirror
    // while this daemon was already running. Reload the durable repository
    // records once per exact generation; externally-managed entries explicitly
    // skip ensurePushHook(), preserving the serving repository's refresh hook.
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
            // Enabling Actions starts from "now"; it never unexpectedly runs an
            // old push that happened before the operator opted in.
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
    // The existing renew unit validates and signs the state as the dedicated
    // mirror service account before publication. --no-block avoids holding the
    // GUI/event loop; an ordinary desktop without that unit simply ignores
    // this best-effort trigger.
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

    // Short branch name + the pushed commit's subject, for the log and alert.
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

    // Always note the push in the network log.
    logSystem(QString::fromUtf8("Push to %1/%2 on %3 \xE2\x86\x92 %4%5")
                  .arg(owner, name, branch, commit.left(8),
                       subject.isEmpty()
                           ? QString()
                           : QString::fromUtf8(" \xE2\x80\x94 ") + subject));

    // Optional desktop alert with the push details (off by default; opt in from
    // Settings → Notifications).
    if (QSettings().value(kPushAlertSetting, false).toBool()) {
        const QString body =
            QString::fromUtf8("%1/%2 \xC2\xB7 %3 \xC2\xB7 %4%5")
                .arg(owner, name, branch, commit.left(8),
                     subject.isEmpty() ? QString()
                                       : QStringLiteral("\n") + subject);
        postNotification(QStringLiteral("Push received"), body,
                         false, QStringLiteral("emblem-synchronizing"));
    }

    // Live refresh: if this repo's detail view is open, reflect the new commit.
    // Debounced — a single push often arrives as several ref updates, and a sync
    // or an agent commit can fire a burst; coalescing avoids running the whole
    // heavyweight refresh (git log, per-PR apply checks) once per event.
    if (repoIndex == m_repoDetailIndex)
        scheduleOpenRepoDetailRefresh();

    if (!repo.actionsEnabled)
        return; // push detection only; no workflow execution for this repo

    queueWorkflowsForCommit(repoIndex, owner, name, commit, ref);
}

// Enqueue every .forkmesh/ workflow present at `commit` for owner/name whose
// `on:` matches `trigger`. Shared by the push handler, the PR "Run checks"
// button, and the Releases panel so all reuse the same metadata-skip, approval,
// and queueing rules.
void MainWindow::queueWorkflowsForCommit(int repoIndex, const QString &owner,
                                         const QString &name, const QString &commit,
                                         const QString &ref,
                                         WorkflowTrigger trigger)
{
    if (repoIndex < 0 || repoIndex >= m_repositories.size() || !m_actionStore)
        return;
    // Copy, don't reference: cancelSupersededRuns() below pumps the event loop
    // (ActionRunner::stop → QProcess::waitForFinished), and a nested refresh can
    // reassign m_repositories — a reference would dangle for the loop's later
    // iterations (adhoc #119).
    const RepositoryRecord repo = m_repositories.at(repoIndex);

    // Every git read below runs inside the served mirror. If its directory is
    // gone (e.g. the record's path diverged from the on-disk mirror), each read
    // fails silently and this would end with the misleading "no .forkmesh/
    // workflow with 'on: push'" log line — say what is actually wrong instead.
    if (!QDir(repo.mirrorPath).exists()) {
        logSystem(QStringLiteral(
                      "Actions: served mirror %1 for %2/%3 is missing \xE2\x80\x94 "
                      "cannot look up workflows at %4.")
                      .arg(repo.mirrorPath, owner, name, commit.left(8)));
        return;
    }

    // Metadata-only pushes (issues, pull requests, commit comments) shouldn't
    // trigger CI: they carry no code change. List the pushed commit's files and
    // bail if every one lives under a metadata folder. A release is an explicit,
    // intentional publish, so it skips this guard and runs regardless.
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

    // List .forkmesh/*.yml|*.yaml at the pushed commit without checking it out.
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
        // Even a release drafted by the owner requires review of the exact
        // repository snapshot. An unchanged YAML cannot silently run a changed
        // helper script, Makefile, package hook, or dependency.
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
        // Make the new run(s) visible immediately if this repo's Actions tab is
        // the one on screen.
        refreshActionsTable();
        if (m_actionWorkflowList && repoIndex == m_repoDetailIndex)
            refreshRepoActions();
        updateNotificationButton();
        processActionQueue(); // a manual run isn't driven by the push pipeline
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
    m_openRepoRefreshTimer->start(); // restart: collapses a burst into one refresh
}

void MainWindow::refreshOpenRepoDetail()
{
    if (m_openRepoRefreshTimer)
        m_openRepoRefreshTimer->stop(); // a direct refresh subsumes any pending one
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    // Re-entrancy guard (adhoc #247): this fires on a debounce timer and from many
    // push/sync paths, each running synchronous git reads under a GitKeepAlive that
    // pumps the event loop. A second heavy refresh firing *during* that pump (the
    // periodic refreshRepositoryList, or this timer again) would nest its git work
    // inside the first one's pump and compound into a multi-second stall. Re-arm the
    // debounce so it runs on a fresh event-loop turn once the in-flight one unwinds.
    if (m_heavyRefreshInFlight) {
        scheduleOpenRepoDetailRefresh();
        return;
    }
    const ScopedFlag refreshGuard(m_heavyRefreshInFlight);
    // Don't yank the keyboard out of the prompt while the user is mid-sentence
    // (adhoc #160): this heavy rebuild — tables, transcripts, the file view —
    // runs on every code sync and can move focus off whatever text field is
    // being typed into, stopping the user mid-keystroke. Remember which text
    // input held focus and, if the rebuild stole it, hand it straight back.
    QPointer<QWidget> typingFocus;
    if (QWidget *focused = QApplication::focusWidget()) {
        if ((qobject_cast<QLineEdit *>(focused) ||
             qobject_cast<QPlainTextEdit *>(focused) ||
             qobject_cast<QTextEdit *>(focused)) &&
            focused->window() == this)
            typingFocus = focused;
    }
    // Re-read the branch tip, commit list, About sidebar and the current file
    // view so a freshly pushed commit shows without reopening the repo.
    loadBranchesAndTags();
    loadCommits();
    reloadAgents();
    // Issues and pull requests live on refs/heads, so a mirror fetch already
    // brought any new ones along with the code; re-read them so a mirror node
    // reflects fresh issues/PRs (and review conversations) without reopening.
    reloadIssues();
    reloadPulls();
    // Re-read .forkmesh/ workflows so the "Actions (N)" badge tracks any added
    // or removed workflows a sync may have brought in.
    refreshRepoActions();
    // loadCommits() above already refreshed the Insights counts if that tab is on
    // screen; off-screen it reloads when next opened, so no extra pass here.
    updateRepoDetailStatus();
    updateRepoActionMenus();
    updateRepoCodeSize();
    updateRepoPushButton();
    refreshRepoPinBanner(); // a sync may have advanced refs past the pinned hash
    m_treeLoadedForIndex = -1; // force the explorer tree to rebuild on next use
    if (m_filesStack && m_filesStack->currentIndex() == 2)
        loadCoveExplorer();
    loadRepoOverview(m_overviewPath);
    // Rebuild the Mirror nodes view (cheap, roster-based) so its tab count badge
    // stays current even when that tab isn't the one on screen.
    loadMirrorNodesPanel();
    // Restore focus to the field the user was typing in if the rebuild moved it
    // (adhoc #160). Only when it's still alive, on-screen and editable, and only
    // if focus actually drifted — so we never fight a focus the user just moved.
    if (typingFocus && typingFocus->isVisibleTo(this) &&
        typingFocus->isEnabled() && QApplication::focusWidget() != typingFocus)
        typingFocus->setFocus(Qt::OtherFocusReason);
}

void MainWindow::processActionQueue()
{
    if (m_actionRunners.isEmpty())
        return;
    // Fill every idle runner from the queue so independent workflows overlap.
    // Each finished() re-enters here to top the pool back up.
    while (!m_actionQueue.isEmpty()) {
        ActionRunner *idle = nullptr;
        for (ActionRunner *runner : m_actionRunners) {
            if (!runner->busy()) {
                idle = runner;
                break;
            }
        }
        if (!idle)
            return; // pool saturated; a finishing run will resume the queue
        const int runId = m_actionQueue.takeFirst();
        ActionRun *run = findRun(runId);
        if (!run || run->status != ActionStatus::Queued)
            continue;
        const int repoIndex = repoIndexFor(run->owner, run->name);
        if (repoIndex < 0) {
            run->status = ActionStatus::Failed;
            m_actionStore->saveRun(*run);
            scheduleMirrorActionsSummary(0);
            continue;
        }
        const QString mirror = m_repositories.at(repoIndex).mirrorPath;
        const QString workTree = m_repositories.at(repoIndex).localPath;
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
            // Approval is checked immediately before execution, not just when
            // the push was queued. Missing legacy fields, mirror tampering, or
            // any changed repository input fails closed into human review.
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
        // start() emits statusChanged synchronously (which reloads m_actionRuns),
        // so copy the run out first and don't touch the pointer afterwards.
        const ActionRun snapshot = *run;
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
    // Snapshot the matching run ids before acting on any of them. Cancelling a
    // Running run calls ActionRunner::stop(), which spins a nested event loop
    // (QProcess::waitForFinished) that can synchronously deliver the process's
    // finished() signal → onRunStatusChanged/onRunFinished, both of which do
    // `m_actionRuns = m_actionStore->loadAllRuns()`. Reassigning the vector
    // mid-iteration would invalidate a range-for reference into it and crash on
    // the next comparison, so we re-find each run by id instead of holding a
    // reference across that reentrancy.
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
            continue; // reloaded away underneath us
        const ActionRun run = *found; // copy: acting below may reload m_actionRuns
        if (run.status == ActionStatus::Running) {
            if (ActionRunner *runner = runnerForRun(run.id)) {
                logSystem(QStringLiteral(
                              "Actions: aborting \"%1\" for %2/%3 @ %4 \xE2\x80\x94 "
                              "superseded by a newer run of the same workflow.")
                              .arg(run.workflowName, run.owner, run.name,
                                   run.commit.left(8)));
                runner->stop(); // records Cancelled once torn down
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
    // ActionRunner has already redacted configured variable values before it
    // appends this line to ActionStore. Batch live-tail publication to at most
    // once every two seconds even when a process emits thousands of chunks.
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
    }
    if (runId == m_selectedRunId)
        showRun(runId); // finished: reload the complete log from disk
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
    // The release workflow staged the artifact bytes into the served CAS and the
    // runner committed the tiny .forkmesh/releases/ manifest into the working copy. Refresh
    // the served mirror before publishing so install.sh and the website read the
    // same release metadata the catalog advertises.
    logSystem(QStringLiteral(
                  "Release: refreshing artifact metadata for %1/%2 online.")
                  .arg(run->owner, run->name));
    publishRepositoryAfterMirrorRefresh(index, /*showDialogOnError=*/false);
    // If this repo's Releases panel is on screen, refresh it so the freshly
    // attached artifacts appear without a manual reload.
    if (index == m_repoDetailIndex && m_releasesTabIndex >= 0 &&
        m_repoDetailStack &&
        m_repoDetailStack->currentIndex() == m_releasesTabIndex)
        loadReleasesPanel();
    updateRepoPushButton();
}

// After action-run state changes, keep an open PR's Checks tab and the inline
// Conversation summary current without waiting for a re-select.
void MainWindow::refreshOpenPullChecks()
{
    if (m_currentPullNumber < 0)
        return;
    for (const PullRequest &it : std::as_const(m_currentPulls)) {
        if (it.number != m_currentPullNumber)
            continue;
        // Snapshot before rendering: each render call pumps the event loop
        // (runIdsForPull → git reads), which can re-enter reloadPulls() and
        // reassign m_currentPulls — the loop reference would dangle before the
        // next call (adhoc #119 SIGSEGV).
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
    // The in-app Notifications page always logs the event above; the noisy
    // desktop toast is what these modes gate. "none" silences it entirely,
    // "failed" lets only failures through (warning == true).
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
    item.timestampMs = QDateTime::currentMSecsSinceEpoch();
    m_notifications.prepend(item);
    while (m_notifications.size() > 100)
        m_notifications.removeLast();
    updateNotificationButton();
    // Keep the open Notifications page live as new alerts arrive.
    if (m_notificationsTable && m_sectionStack &&
        m_sectionStack->currentIndex() == 3)
        refreshNotificationsTable();
}

void MainWindow::addNotification(const QString &title, const QString &body,
                                 bool warning, const NotificationLink &link)
{
    AppNotification item;
    item.title = title;
    item.body = body;
    item.warning = warning;
    item.link = link;
    item.timestampMs = QDateTime::currentMSecsSinceEpoch();
    m_notifications.prepend(item);
    while (m_notifications.size() > 100)
        m_notifications.removeLast();
    updateNotificationButton();
    if (m_notificationsTable && m_sectionStack &&
        m_sectionStack->currentIndex() == 3)
        refreshNotificationsTable();
}

// Jump to the screen/item a notification points at: open the owning repo, switch
// to the right tab and select the issue / PR / discussion / commit (issue #292).
void MainWindow::openNotificationLink(const NotificationLink &link)
{
    if (!link.isValid())
        return;
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
        selectTab(2); // Issues
        if (link.number > 0)
            showIssue(link.number);
    } else if (link.kind == QLatin1String("pull")) {
        if (link.number > 0)
            showPull(link.number); // selects the Pull requests tab itself
        else
            selectTab(4);
    } else if (link.kind == QLatin1String("discussion")) {
        selectTab(5); // Discussions
        if (link.number > 0)
            showDiscussion(link.number);
    } else if (link.kind == QLatin1String("commit")) {
        showOverviewCommits(); // the commits panel inside the Code overview
        if (!link.ref.isEmpty())
            showCommit(link.ref);
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

void MainWindow::updateNotificationButton()
{
    if (!m_notificationButton)
        return;
    const int pending = pendingActionCount();
    // Icon-only bell (adhoc #137): the pending count rides on the tooltip and the
    // amber "alert" accent below rather than a "•" appended to a text label.
    m_notificationButton->setToolTip(
        pending > 0
            ? QStringLiteral("%1 action(s) waiting for approval").arg(pending)
            : QStringLiteral("Notifications"));
    // The button keeps its "topNavButton" identity (so it stays uniform and
    // shows its checked state); the pending-approval accent rides on a dynamic
    // property instead of swapping the object name.
    m_notificationButton->setProperty("alert", pending > 0);
    m_notificationButton->style()->unpolish(m_notificationButton);
    m_notificationButton->style()->polish(m_notificationButton);
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

// ---- Notifications (its own sortable-table section) ------------------------

namespace {
// A table item that sorts by an epoch-millis value held in Qt::UserRole while
// displaying a human-friendly date, so the "When" column orders chronologically
// instead of lexicographically.
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
} // namespace

QWidget *MainWindow::buildNotificationsSection()
{
    auto *page = new QWidget;

    auto *title = new QLabel(QStringLiteral("Notifications"));
    title->setObjectName("settingsTitle");

    auto *refreshButton = new QPushButton(QStringLiteral("Refresh"));
    refreshButton->setObjectName("repoAction");
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshNotificationsTable);
    addRefreshSpin(refreshButton);

    // Fires a real desktop toast (notify-send / tray) and logs it to the page,
    // so the user can confirm notifications are wired up and visible on their
    // desktop without waiting for a real event.
    auto *testButton = new QPushButton(QStringLiteral("Test"));
    testButton->setObjectName("repoAction");
    testButton->setCursor(Qt::PointingHandCursor);
    setOcticon(testButton, "bell", 16);
    testButton->setToolTip(
        QStringLiteral("Send a test desktop notification"));
    connect(testButton, &QPushButton::clicked, this, [this] {
        const QString body = QStringLiteral(
            "This is a test notification from ForkMesh — "
            "desktop alerts are working.");
        addNotification(QStringLiteral("Test notification"), body, false);
        postNotification(QStringLiteral("Test notification"), body, false,
                         QStringLiteral("emblem-default"));
    });

    auto *clearButton = new QPushButton(QStringLiteral("Clear"));
    clearButton->setObjectName("repoAction");
    clearButton->setCursor(Qt::PointingHandCursor);
    setOcticon(clearButton, "trash", 16);
    clearButton->setToolTip(QStringLiteral("Dismiss all past notifications"));
    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_notifications.clear();
        updateNotificationButton();
        refreshNotificationsTable();
    });

    auto *header = new QHBoxLayout;
    header->setContentsMargins(16, 12, 16, 4);
    header->addWidget(title);
    header->addStretch();
    header->addWidget(testButton);
    header->addWidget(refreshButton);
    header->addWidget(clearButton);

    // GitHub-ish columns; the table is sortable by clicking a header section.
    m_notificationsTable = new QTableWidget(0, 4);
    installColumnHeaderMenu(m_notificationsTable); // 3-dots per-column menu (issue #318)
    m_notificationsTable->setObjectName("issueTable"); // reuse the table styling
    m_notificationsTable->setHorizontalHeaderLabels(
        {QStringLiteral("Type"), QStringLiteral("Title"),
         QStringLiteral("Detail"), QStringLiteral("When")});
    m_notificationsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_notificationsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_notificationsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_notificationsTable->verticalHeader()->setVisible(false);
    m_notificationsTable->setSortingEnabled(true);
    m_notificationsTable->setAlternatingRowColors(true);
    m_notificationsTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_notificationsTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    m_notificationsTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_notificationsTable->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_notificationsTable);
    m_notificationsTable->horizontalHeader()->setSortIndicator(
        3, Qt::DescendingOrder); // newest first by default
    m_notificationsTable->setToolTip(
        QStringLiteral("Double-click a row to open the related issue, pull "
                       "request, discussion, commit or action."));
    connect(m_notificationsTable, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem *item) {
                if (!item)
                    return;
                QTableWidgetItem *first = m_notificationsTable->item(item->row(), 0);
                if (!first)
                    return;
                // Approval rows route to their run; everything else carries a
                // NotificationLink to the screen/item it's about (issue #292).
                const int runId = first->data(Qt::UserRole).toInt();
                if (runId > 0) {
                    openActionRunFromNotification(runId);
                    return;
                }
                const QVariant nav = first->data(Qt::UserRole + 1);
                if (nav.canConvert<NotificationLink>())
                    openNotificationLink(qvariant_cast<NotificationLink>(nav));
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
    // Disable sorting while repopulating so rows aren't reordered mid-insert.
    TableRepaintGuard repaintGuard(m_notificationsTable);
    m_notificationsTable->setSortingEnabled(false);
    m_notificationsTable->setRowCount(0);

    auto addRow = [this](const QString &type, const QString &titleText,
                         const QString &detail, qint64 whenMs, int runId,
                         bool warning, const NotificationLink &link) {
        const int row = m_notificationsTable->rowCount();
        m_notificationsTable->insertRow(row);

        auto *typeItem = new QTableWidgetItem(type);
        typeItem->setData(Qt::UserRole, runId);
        // Carry the double-click destination (issue #292) on the row's first
        // cell; the handler reads it back to open the related screen/item.
        if (link.isValid())
            typeItem->setData(Qt::UserRole + 1, QVariant::fromValue(link));
        auto *titleItem = new QTableWidgetItem(titleText);
        auto *detailItem = new QTableWidgetItem(detail);
        // Sorts chronologically (by epoch millis) while showing a friendly date.
        auto *whenItem = new TimestampItem(
            whenMs > 0 ? formatRepoDate(whenMs) : QString(), whenMs);

        if (warning) {
            const QColor red("#f85149");
            for (QTableWidgetItem *it : {typeItem, titleItem, detailItem,
                                         static_cast<QTableWidgetItem *>(whenItem)})
                it->setForeground(red);
        }
        m_notificationsTable->setItem(row, 0, typeItem);
        m_notificationsTable->setItem(row, 1, titleItem);
        m_notificationsTable->setItem(row, 2, detailItem);
        m_notificationsTable->setItem(row, 3, whenItem);
    };

    for (const ActionRun &run : std::as_const(m_actionRuns)) {
        if (run.status != ActionStatus::AwaitingApproval)
            continue;
        addRow(QStringLiteral("Approval"), run.workflowName,
               QStringLiteral("%1/%2 at %3")
                   .arg(run.owner, run.name, run.commit.left(8)),
               run.createdAtMs, run.id, false, NotificationLink());
    }
    for (const AppNotification &notice : std::as_const(m_notifications)) {
        addRow(notice.warning ? QStringLiteral("Alert") : QStringLiteral("Info"),
               notice.title, notice.body, notice.timestampMs, notice.runId,
               notice.warning, notice.link);
    }

    m_notificationsTable->setSortingEnabled(true);
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
    // Prefer notify-send: many Linux desktops don't render the body of a
    // QSystemTrayIcon message (they fall back to just the app name), but the
    // libnotify daemon shows the summary, body and icon reliably.
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
    // The table lives inside one repo's Actions tab, so only show that repo's
    // runs, optionally narrowed to the workflow selected in the left column.
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
        // Flag failed runs so the delegate draws a red outline around the row.
        wfItem->setData(ActionFailureBorderDelegate::ActionFailedRole,
                        run.status == ActionStatus::Failed);
        auto *statusItem = new QTableWidgetItem(actionStatusText(run.status));
        statusItem->setForeground(actionStatusColor(run.status));
        // Show a human-friendly relative time ("5m ago") in the column, with the
        // exact date/time kept on hover.
        QString when;
        if (run.createdAtMs > 0) {
            const QString rel = formatShortRelativeTime(run.createdAtMs / 1000);
            when = rel == QStringLiteral("now") ? rel
                                                : rel + QStringLiteral(" ago");
        }
        // For a run that's still queued or running, tack on how long the same
        // workflow took last time it ran, so the "When" column shows the target
        // the shrinking line above the tab is counting down against (adhoc #105).
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

        // How long the run took, wall-clock from when it actually started to when
        // it finished. Only finished runs have both timestamps; queued/running
        // rows leave this blank (the "When" column already shows their progress).
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

    // Aggregate every run for this repo whose commit matches `sha` (one is a
    // prefix of the other, since the log uses short hashes and runs store full
    // SHAs). Running/queued wins, then any failure, then success.
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
                              "title='Checks running'>\xE2\x97\x90</span>"); // ◐
    case 2:
        return QString::fromUtf8(" <span style='color:#f85149' "
                              "title='Checks failed'>\xE2\x9C\x95</span>"); // ✕
    case 1:
        return QString::fromUtf8(" <span style='color:#3fb950' "
                              "title='Checks passed'>\xE2\x9C\x93</span>"); // ✓
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
            continue; // expanded file rows carry the same sha — commits only
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
        // The check state is part of the summary's hover box; rebuild it.
        updateCommitRowHover(row);
    }
}

void MainWindow::refreshCommitStatusGlyphs()
{
    if (!m_repoDetailStack)
        return;
    switch (m_repoDetailStack->currentIndex()) {
    case 0: // Code overview: refresh the latest-commit strip
        refreshCommitBarStatusGlyph();
        break;
    case 1: // Commits list
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

    // The tab label just carries the workflow count; the live activity readout is
    // now the floating strip of growing bars above the tab (updateActionStrip()).
    const int workflows =
        m_actionWorkflowList ? qMax(0, m_actionWorkflowList->count() - 1) : 0;
    tab->setText(QStringLiteral("Actions (%1)").arg(formatCount(workflows)));
    updateActionStrip();
}

void MainWindow::ensureActionStrip()
{
    if (m_actionStrip || !m_repoActionsTab)
        return;
    // The repo-detail page itself, not m_repoDetailStack->parentWidget(): the
    // stack now lives inside its own QScrollArea (688850a7), so its parent is
    // that scroll's viewport. These bars float over the meta band just above the
    // tab row, so they must be parented to the page — anchoring them to the
    // viewport pushes them into the scrolled body, away from the tabs.
    QWidget *page = m_repoDetailSection;
    if (!page)
        return;
    // Parented to the repo-detail page so the bars can float over the meta band
    // just above the Actions tab without being clipped to the tab-bar scroll
    // area's viewport.
    m_actionStrip = new QWidget(page);
    m_actionStrip->setObjectName("actionStrip");
    // The strip and its rows must stay hit-testable: Qt skips a
    // WA_TransparentForMouseEvents widget *and its whole subtree* when picking a
    // click receiver, which would swallow the clicks the per-run boxes rely on
    // (see updateActionStrip). One border drawn here (rather than one per row,
    // adhoc #112) makes several queued/running actions read as a single box
    // holding multiple lines instead of a stack of separate boxes.
    m_actionStrip->setAttribute(Qt::WA_StyledBackground, true);
    // One thin-bordered "main bar" that holds the stacked run lines; colours
    // follow the active theme so the strip sits flush with the light UI instead
    // of showing as a dark box with unreadable white text (adhoc #118).
    const bool dark = currentThemeIsDark();
    m_actionStrip->setStyleSheet(
        QStringLiteral("#actionStrip { background-color: %1; "
                        "border: 1px solid %2; border-radius: 8px; }")
            .arg(dark ? "#161b22" : "#ffffff", dark ? "#30363d" : "#d0d7de"));
    auto *col = new QVBoxLayout(m_actionStrip);
    col->setContentsMargins(8, 6, 8, 6);
    col->setSpacing(4);
    m_actionStripCol = col;
    m_actionStrip->hide();
}

void MainWindow::updateActionStrip()
{
    ensureActionStrip();
    if (!m_actionStrip || !m_actionStripCol)
        return;

    // This repo's runs that are queued or running: one line each (adhoc #105).
    // Queued runs haven't started their clock, so their line stays full until the
    // runner picks them up and they begin counting down.
    QList<const ActionRun *> live;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const QString owner = m_repositories.at(m_repoDetailIndex).owner;
        const QString name = m_repositories.at(m_repoDetailIndex).name;
        for (const ActionRun &run : m_actionRuns)
            if (run.owner == owner && run.name == name &&
                (run.status == ActionStatus::Running ||
                 run.status == ActionStatus::Queued))
                live.append(&run);
    }

    const bool active = !live.isEmpty() && m_repoActionsTab->isVisible();
    if (!active) {
        if (m_actionStripTimer)
            m_actionStripTimer->stop();
        m_actionStripIds.clear();
        m_actionStrip->hide();
        return;
    }

    // Rebuild the boxes only when the set of running runs changes, so an existing
    // box keeps draining smoothly instead of snapping back to full each tick.
    QList<int> ids;
    for (const ActionRun *r : std::as_const(live))
        ids.append(r->id);
    if (ids != m_actionStripIds) {
        m_actionStripIds = ids;
        while (QLayoutItem *item = m_actionStripCol->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
        for (const ActionRun *r : std::as_const(live)) {
            // Each run is one fixed box whose coloured lines drain down toward the
            // estimated duration (see ActionEstimateBox). startedAtMs is set once
            // the runner picks the run up; fall back to createdAtMs.
            // Only a running run counts down; a queued one keeps a full line.
            const qint64 started =
                r->status != ActionStatus::Running ? 0
                : r->startedAtMs > 0               ? r->startedAtMs
                                                   : r->createdAtMs;
            auto *box = new ActionEstimateBox;
            box->configure(r->workflowName.trimmed().isEmpty()
                               ? QStringLiteral("workflow")
                               : r->workflowName.trimmed(),
                           started, estimatedRunDurationMs(*r));
            // The box is a live link: clicking it jumps to this run's output. It
            // tags itself with the run id for MainWindow's event filter.
            box->setProperty("actionRunId", r->id);
            box->setToolTip(QStringLiteral("View this run's live output"));
            box->installEventFilter(this);
            m_actionStripCol->addWidget(box);
        }
    } else {
        // Same runs: refresh each box's estimate (a run may have just finished
        // and set a fresh baseline) and repaint the drain level.
        int i = 0;
        for (const ActionRun *r : std::as_const(live)) {
            // Only ActionEstimateBox widgets populate this layout, and the box is
            // a plain QWidget (no Q_OBJECT), so a static_cast is safe here.
            auto *box = static_cast<ActionEstimateBox *>(
                m_actionStripCol->itemAt(i++)->widget());
            if (!box)
                continue;
            // Only a running run counts down; a queued one keeps a full line.
            const qint64 started =
                r->status != ActionStatus::Running ? 0
                : r->startedAtMs > 0               ? r->startedAtMs
                                                   : r->createdAtMs;
            box->configure(r->workflowName.trimmed().isEmpty()
                               ? QStringLiteral("workflow")
                               : r->workflowName.trimmed(),
                           started, estimatedRunDurationMs(*r));
        }
    }

    positionActionStrip();
    m_actionStrip->show();
    m_actionStrip->raise();

    // A modest tick both drains the boxes (refreshing their estimate and
    // repainting) and keeps the strip pinned above the tab as the window moves or
    // the tab bar reflows.
    if (!m_actionStripTimer) {
        m_actionStripTimer = new QTimer(this);
        connect(m_actionStripTimer, &QTimer::timeout, this,
                &MainWindow::updateActionStrip);
    }
    if (!m_actionStripTimer->isActive())
        m_actionStripTimer->start(250);
}

// Duration of the previous finished run of the same workflow, used as the
// estimate the strip box counts down against. 0 when there's no prior run to go
// on (a fresh workflow, or none has completed yet), which leaves the box full.
qint64 MainWindow::estimatedRunDurationMs(const ActionRun &run) const
{
    qint64 best = 0;
    qint64 newest = 0;
    for (const ActionRun &r : m_actionRuns) {
        if (r.id == run.id || r.owner != run.owner || r.name != run.name ||
            r.workflowPath != run.workflowPath)
            continue;
        if (r.startedAtMs <= 0 || r.finishedAtMs <= r.startedAtMs)
            continue; // never actually ran to completion
        if (r.finishedAtMs > newest) {
            newest = r.finishedAtMs;
            best = r.finishedAtMs - r.startedAtMs;
        }
    }
    return best;
}

void MainWindow::positionActionStrip()
{
    if (!m_actionStrip || !m_actionStripCol || !m_repoActionsTab)
        return;
    QWidget *page = m_actionStrip->parentWidget();
    if (!page)
        return;

    // Span the Actions tab exactly so the box never bleeds over the neighbouring
    // Security tab (adhoc #105): the outer box is fixed to the tab's width, and
    // each line is inset by the box's own margins (adhoc #112) rather than
    // stretched edge-to-edge.
    const int tabWidth = qMax(1, m_repoActionsTab->width());
    const QMargins cm = m_actionStripCol->contentsMargins();
    const int rowWidth = qMax(1, tabWidth - cm.left() - cm.right());
    const int rows = m_actionStripCol->count();
    int h = cm.top() + cm.bottom();
    for (int i = 0; i < rows; ++i) {
        auto *box = m_actionStripCol->itemAt(i)->widget();
        if (!box)
            continue;
        box->setFixedWidth(rowWidth);
        h += box->height() + (i > 0 ? m_actionStripCol->spacing() : 0);
    }
    if (rows <= 0)
        return;

    m_actionStrip->resize(tabWidth, h);

    const QPoint tl = m_repoActionsTab->mapTo(page, QPoint(0, 0));
    int x = tl.x();
    int y = tl.y() - h - 1;
    if (y < 0)
        y = 0;
    if (x + tabWidth > page->width())
        x = qMax(0, page->width() - tabWidth);
    m_actionStrip->move(x, y);
    m_actionStrip->raise();
}

// Float the "Sync" button in the band just above the Code tab, raised
// one above the tab bar. As an overlay it occupies no layout space, so toggling
// it never shifts the tabs or page content.
void MainWindow::positionRepoPushButton()
{
    if (!m_repoPushButton || !m_repoCodeTab)
        return;
    // The repo-detail page itself, not m_repoDetailStack->parentWidget(): the
    // stack now lives inside its own QScrollArea (688850a7), so its parent is
    // that scroll's viewport. These bars float over the meta band just above the
    // tab row, so they must be parented to the page — anchoring them to the
    // viewport pushes them into the scrolled body, away from the tabs.
    QWidget *page = m_repoDetailSection;
    if (!page)
        return;
    if (m_repoPushButton->parentWidget() != page)
        m_repoPushButton->setParent(page); // hides it; reveal() re-shows
    const int w = m_repoPushButton->sizeHint().width();
    const int h = m_repoPushButton->sizeHint().height();
    const QPoint tl = m_repoCodeTab->mapTo(page, QPoint(0, 0));
    int x = tl.x();
    int y = tl.y() - h - 1; // the meta band above the tab row
    if (y < 0)
        y = 0;
    if (x + w > page->width())
        x = qMax(0, page->width() - w);
    m_repoPushButton->setGeometry(x, y, w, h);
    m_repoPushButton->raise();

    // The eye icon rides just to the right of Sync, same row, same reveal.
    if (m_repoPushEyeButton) {
        if (m_repoPushEyeButton->parentWidget() != page)
            m_repoPushEyeButton->setParent(page);
        const int ew = m_repoPushEyeButton->sizeHint().width();
        const int eh = m_repoPushEyeButton->sizeHint().height();
        int ex = x + w + 4;
        int ey = y + (h - eh) / 2;
        if (ex + ew > page->width())
            ex = qMax(0, page->width() - ew);
        m_repoPushEyeButton->setGeometry(ex, ey, ew, eh);
        m_repoPushEyeButton->raise();
    }
}

void MainWindow::updateAgentsTabIndicator()
{
    // adhoc #178 removed the repo-detail Agents tab and its floating spinner
    // overlay (redundant with the footer "Agents:" strip and issue/PR links),
    // but the Agents table's own running-row Status glyph + elapsed-time cell
    // still need a live tick, scoped to the currently-open repo like before.
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
        connect(m_agentsSpinTimer, &QTimer::timeout, this, [this] {
            m_agentsSpinFrame = (m_agentsSpinFrame + 1) % 10;
            animateRunningAgentIcons(); // spin the running rows' Status glyph
        });
    }
    if (!m_agentsSpinTimer->isActive())
        m_agentsSpinTimer->start(120);
}

// Anchor the looper toggle in the meta band just above the Issues tab (adhoc
// #130), mirroring positionRepoPushButton over Code. It stays visible the
// whole time a repo detail page is open — off (grey switch) or on (green switch
// + travelling neon loop, naming the live issue). A modest timer keeps it
// pinned over the tab as the window resizes or the tabs reflow.
void MainWindow::positionLooperToggle()
{
    if (!m_looperToggle || !m_repoIssuesTab)
        return;
    // The repo-detail page itself, not m_repoDetailStack->parentWidget(): the
    // stack now lives inside its own QScrollArea (688850a7), so its parent is
    // that scroll's viewport. These bars float over the meta band just above the
    // tab row, so they must be parented to the page — anchoring them to the
    // viewport pushes them into the scrolled body, away from the tabs.
    QWidget *page = m_repoDetailSection;
    if (!page)
        return;
    if (m_looperToggle->parentWidget() != page)
        m_looperToggle->setParent(page); // hides it; shown again just below
    const int w = m_looperToggle->sizeHint().width();
    const int h = m_looperToggle->sizeHint().height();
    const QPoint tl = m_repoIssuesTab->mapTo(page, QPoint(0, 0));
    int x = tl.x() + (m_repoIssuesTab->width() - w) / 2;
    int y = tl.y() - h - 1; // the meta band above the tab row
    if (y < 0)
        y = 0;
    if (x + w > page->width())
        x = qMax(0, page->width() - w);
    m_looperToggle->setGeometry(x, y, w, h);
    // Only show it while the repo-detail page is the one on screen; otherwise the
    // overlay would float over whatever section replaced it.
    const bool onPage = page->isVisible();
    m_looperToggle->setVisible(onPage);
    if (onPage)
        m_looperToggle->raise();
    // Keep a single low-rate timer running so the toggle re-anchors as the window
    // resizes or the tabs reflow, and reappears when the user returns to the
    // repo-detail page. Started once; the per-tick visibility check above is what
    // hides/shows it, so it never needs stopping.
    if (!m_looperToggleTimer) {
        m_looperToggleTimer = new QTimer(this);
        connect(m_looperToggleTimer, &QTimer::timeout, this,
                &MainWindow::positionLooperToggle);
        m_looperToggleTimer->start(400);
    }
}

// Anchor the live mirror-activity dot strip in the meta band just above the
// Mirror nodes tab (adhoc #197), mirroring positionLooperToggle over Issues. It
// shows only while a repo-detail page is open and at least one node is active;
// loadMirrorNodesPanel feeds it the roster, the timer keeps it pinned.
void MainWindow::positionMirrorActivityStrip()
{
    auto *strip = static_cast<MirrorActivityStrip *>(m_mirrorActivityStrip);
    if (!strip || !m_repoMirrorsTab)
        return;
    // The repo-detail page itself, not m_repoDetailStack->parentWidget(): the
    // stack now lives inside its own QScrollArea (688850a7), so its parent is
    // that scroll's viewport. These bars float over the meta band just above the
    // tab row, so they must be parented to the page — anchoring them to the
    // viewport pushes them into the scrolled body, away from the tabs.
    QWidget *page = m_repoDetailSection;
    if (!page)
        return;
    if (strip->parentWidget() != page)
        strip->setParent(page); // hides it; shown again just below
    const int w = strip->preferredWidth();
    const int h = strip->minimumHeight(); // its fixed strip height
    const QPoint tl = m_repoMirrorsTab->mapTo(page, QPoint(0, 0));
    int x = tl.x();
    int y = tl.y() - h - 1; // the meta band above the tab row
    if (y < 0)
        y = 0;
    if (x + w > page->width())
        x = qMax(0, page->width() - w);
    strip->setGeometry(x, y, w, h);
    // Visible only on the repo-detail page and when there's at least one active
    // node — an empty strip would just be a gap floating over the tab.
    const bool onPage = page->isVisible() && !strip->isEmpty();
    strip->setVisible(onPage);
    if (onPage)
        strip->raise();
    // One low-rate timer re-anchors the strip as the window resizes or the tabs
    // reflow, and reapplies the visibility check above; never needs stopping.
    if (!m_mirrorActivityStripTimer) {
        m_mirrorActivityStripTimer = new QTimer(this);
        connect(m_mirrorActivityStripTimer, &QTimer::timeout, this,
                &MainWindow::positionMirrorActivityStrip);
        m_mirrorActivityStripTimer->start(400);
    }
}

// Anchor the current-release pill in the meta band just above the Releases tab
// (adhoc #69), mirroring positionMirrorActivityStrip over Mirror nodes. It shows
// only while a repo-detail page is open and there is a release to name; the tag
// scan feeds its text, the timer keeps it pinned as the window reflows.
void MainWindow::positionReleaseStrip()
{
    if (!m_releaseStrip || !m_repoReleasesTab)
        return;
    // The repo-detail page itself, not m_repoDetailStack->parentWidget(): the
    // stack now lives inside its own QScrollArea (688850a7), so its parent is
    // that scroll's viewport. These bars float over the meta band just above the
    // tab row, so they must be parented to the page — anchoring them to the
    // viewport pushes them into the scrolled body, away from the tabs.
    QWidget *page = m_repoDetailSection;
    if (!page)
        return;
    if (m_releaseStrip->parentWidget() != page)
        m_releaseStrip->setParent(page); // hides it; shown again just below
    const int w = m_releaseStrip->sizeHint().width();
    const int h = m_releaseStrip->sizeHint().height();
    const QPoint tl = m_repoReleasesTab->mapTo(page, QPoint(0, 0));
    int x = tl.x();
    int y = tl.y() - h - 1; // the meta band above the tab row
    if (y < 0)
        y = 0;
    if (x + w > page->width())
        x = qMax(0, page->width() - w);
    m_releaseStrip->setGeometry(x, y, w, h);
    // Visible only on the repo-detail page and when there's a release to name —
    // an empty pill would just be a floating box over the tab.
    const bool onPage = page->isVisible() && !m_releaseStrip->text().isEmpty();
    m_releaseStrip->setVisible(onPage);
    if (onPage)
        m_releaseStrip->raise();
    // One low-rate timer re-anchors the pill as the window resizes or the tabs
    // reflow, and reapplies the visibility check above; never needs stopping.
    if (!m_releaseStripTimer) {
        m_releaseStripTimer = new QTimer(this);
        connect(m_releaseStripTimer, &QTimer::timeout, this,
                &MainWindow::positionReleaseStrip);
        m_releaseStripTimer->start(400);
    }
}

// Persist the looper's running state so a restart resumes the loop on the same
// repo with the same provider (adhoc #125). Called from updateIssueLooperButton,
// the single funnel for every looper state change.
void MainWindow::persistLooperState()
{
    QSettings settings;
    settings.setValue(kLooperActiveSetting, m_looperActive);
    settings.setValue(kLooperProviderSetting, m_looperProvider);
    settings.setValue(kLooperRepoSetting, m_looperRepoSlug);
}

// On startup, after the last repository has been restored, resume the loop if it
// was running on that repo when we quit (adhoc #125). Scoped to the open repo:
// the loop drives agents on the currently-open repo's issues, so resuming on a
// different repo would be surprising. The agent that was running at quit is gone,
// so looperStartNext() simply picks the next un-attempted issue.
void MainWindow::maybeRestoreIssueLooper()
{
    if (m_looperActive)
        return; // already looping this session
    QSettings settings;
    if (!settings.value(kLooperActiveSetting, false).toBool())
        return;
    const QString slug = settings.value(kLooperRepoSetting).toString();
    const int idx = issuesRepoIndex(); // the repo the looper would actually drive
    if (slug.isEmpty() || idx < 0 || idx >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);
    if (slug != repo.owner + QLatin1Char('/') + repo.name)
        return;
    if (repoAgentGitDir(repo).isEmpty())
        return; // need a working tree or mirror to run agents (adhoc #191)
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

QList<ActionWorkflow>
MainWindow::availableWorkflowsForRepo(const RepositoryRecord &repo) const
{
    QList<ActionWorkflow> out;
    // Prefer reading the bare mirror's default branch (HEAD); fall back to a
    // local working tree if one is configured.
    if (!repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists()) {
        QProcess ls;
        ls.start(QStringLiteral("git"),
                 {QStringLiteral("-C"), repo.mirrorPath, QStringLiteral("ls-tree"),
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
                       {QStringLiteral("-C"), repo.mirrorPath,
                        QStringLiteral("show"), QStringLiteral("HEAD:") + path});
            show.waitForFinished(8000);
            if (show.exitCode() != 0)
                continue;
            out.append(ActionFile::parse(
                path, QString::fromUtf8(show.readAllStandardOutput())));
        }
    }
    if (out.isEmpty() && !repo.localPath.isEmpty())
        out = ActionFile::parseWorkflowsInDir(repo.localPath);
    return out;
}

void MainWindow::refreshRepoActions()
{
    if (!m_actionWorkflowList)
        return;
    QSignalBlocker block(m_actionWorkflowList);
    m_actionWorkflowList->clear();

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        if (m_repoActionsTab)
            m_repoActionsTab->setText(QStringLiteral("Actions (0)"));
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
    m_repoWorkflows = wfs; // cache so the manual-run bar can look workflows up
    for (const ActionWorkflow &wf : wfs) {
        auto *item =
            new QListWidgetItem(wf.name);
        item->setData(Qt::UserRole, wf.path);
        QStringList triggers;
        if (wf.triggersOnPush())
            triggers << QStringLiteral("on: push");
        if (wf.triggersOnRelease())
            triggers << QStringLiteral("on: release");
        if (wf.allowsManualRun())
            triggers << QStringLiteral("manual");
        // Valid workflows get a checkbox so the owner can switch each one off
        // individually; unchecking skips it on push and hides its manual-run bar.
        if (wf.valid) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(repo.disabledWorkflows.contains(wf.path)
                                    ? Qt::Unchecked
                                    : Qt::Checked);
        }
        item->setToolTip(wf.valid
                             ? wf.path + (triggers.isEmpty()
                                              ? QString()
                                              : QStringLiteral("  (") +
                                                    triggers.join(QStringLiteral(", ")) +
                                                    QStringLiteral(")")) +
                                   QStringLiteral("\nUntick to disable this workflow.")
                             : wf.path + QStringLiteral("  — ") + wf.error);
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
    if (m_repoActionsTab)
        m_repoActionsTab->setText(QStringLiteral("Actions (%1)")
                                      .arg(formatCount(qMax(0, m_actionWorkflowList->count() - 1))));
}

void MainWindow::updateManualRunBar()
{
    if (!m_actionManualRunBar)
        return;
    // Find the selected workflow and whether it opted into manual runs.
    const ActionWorkflow *wf = nullptr;
    for (const ActionWorkflow &w : std::as_const(m_repoWorkflows)) {
        if (w.valid && w.path == m_selectedWorkflowFilter) {
            wf = &w;
            break;
        }
    }
    const bool show =
        wf && wf->allowsManualRun() && !isWorkflowDisabled(wf->path);
    m_actionManualRunBar->setVisible(show);
    if (!show)
        return;
    m_actionManualRunButton->setText(
        QString::fromUtf8("Run \xE2\x80\x9C%1\xE2\x80\x9D").arg(wf->name));

    // Populate the branch list from the repo's mirror, keeping the user's choice
    // (or defaulting to main) selected.
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
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
    // Prefer the user's prior choice, else main, else the first branch.
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

    // Resolve the branch to a commit in the mirror (accept short or full ref).
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

    // Read the workflow exactly as it exists at that commit (its content is the
    // approval/diff unit), so a manual run honours the same approval gate.
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

    // Show this workflow's runs and select the new one so its log/approval is
    // immediately visible.
    refreshActionsTable();
    showRun(created.id);
    updateNotificationButton();
    processActionQueue();
}

// Build a unified diff between the previously approved workflow (`prior`, empty
// if the workflow has never been approved) and the `incoming` content of a push,
// so the approval view can render it with the shared diff renderer instead of
// dumping both versions as plain text. `path` only labels the diff headers.
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

    // git labels the diff with the temp paths; rewrite the header lines so the
    // renderer (and its file list) shows the real workflow path instead.
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
        // The split/unified preference is shared with the other diff views, so
        // reflect its current value before rendering.
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

    // Clone the run's identity, workflow content, commit and ref so it executes
    // exactly what ran before. Approval still applies: if that content is no
    // longer approved it waits for review rather than running silently.
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

    // Executing right now: ask the runner to abort it. stop() blocks briefly
    // while the process tears down, then ActionRunner::finished fires and
    // onRunFinished refreshes the UI and drains the queue — so don't touch the
    // run here beyond logging the intent.
    if (run->status == ActionStatus::Running) {
        if (ActionRunner *runner = runnerForRun(run->id)) {
            logSystem(QStringLiteral("Actions: stopping \"%1\" for %2/%3.")
                          .arg(run->workflowName, run->owner, run->name));
            runner->stop();
        }
        return;
    }

    // Still only queued: it never started, so just drop it from the queue and
    // mark it Cancelled.
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

    // Skip only applies before a run starts: a queued run is dropped from the
    // queue, an awaiting-approval run is declined outright. Either way it never
    // executes and is recorded as Skipped (distinct from a Cancelled stop).
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

    // Bound the log excerpt in the prompt — a full build log can run to
    // thousands of lines, and a small model's context window would choke on it.
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
        startAdHocAgentForRepo(repoIndex, prompt, provider, /*createPr=*/true, model);
    if (sessionId > 0)
        flashMessage(QStringLiteral("Started a %1 agent to fix \"%2\".")
                         .arg(agentProviderName(provider), run->workflowName));
}

void MainWindow::clearActionRuns()
{
    if (!m_actionStore || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const QString owner = m_repositories.at(m_repoDetailIndex).owner;
    const QString name = m_repositories.at(m_repoDetailIndex).name;

    // Collect exactly the runs the list is showing (this repo, optionally
    // narrowed to the selected workflow), skipping any still queued or running
    // so we never delete a run out from under the runner.
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

    // Far left: the actions available in this repo (.forkmesh/ workflows).
    auto *wfPane = new QWidget;
    // Give the workflow-name column a bit more room to open than before.
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
            });
    // Ticking/unticking a workflow's checkbox switches it on/off for this repo.
    // Refreshes block this signal, so it only fires on real user toggles.
    connect(m_actionWorkflowList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
                if (!item || !(item->flags() & Qt::ItemIsUserCheckable))
                    return;
                const QString path = item->data(Qt::UserRole).toString();
                setWorkflowDisabled(path, item->checkState() != Qt::Checked);
            });

    // Enable/disable actions for this repo, right here on the Actions tab.
    m_actionsEnabledCheck = new QCheckBox("Run actions on push");
    m_actionsEnabledCheck->setToolTip(
        "When a fork pushes to this repo's local mirror, run its .forkmesh/ "
        "workflows. Changed workflows still require approval below before they "
        "run.");
    connect(m_actionsEnabledCheck, &QCheckBox::toggled, this,
            [this](bool on) { setRepoActionsEnabled(on); });

    auto *wfLayout = new QVBoxLayout(wfPane);
    wfLayout->setContentsMargins(16, 22, 8, 22);
    wfLayout->setSpacing(8);
    wfLayout->addWidget(wfHeading);
    wfLayout->addWidget(wfHint);
    wfLayout->addWidget(m_actionsEnabledCheck);
    wfLayout->addWidget(m_actionWorkflowList, 1);

    // Middle: the run list for the selected workflow (or all).
    auto *listPane = new QWidget;
    // Extra room for the runs table: the Workflow column is now twice as wide
    // (see setColumnWidth below) so the pane needs to grow with it.
    listPane->setMinimumWidth(475);
    auto *heading = new QLabel("Runs");
    heading->setObjectName("channelTitle");
    // Clear button on the Runs header row: wipes the run history shown below
    // (meta + logs on disk), keeping any run that's still in flight.
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
    installColumnHeaderMenu(m_actionsTable); // 3-dots per-column menu (issue #318)
    m_actionsTable->setObjectName("issueTable");
    m_actionsTable->setHorizontalHeaderLabels(
        {"Workflow", "Status", "When", "Duration"});
    m_actionsTable->horizontalHeader()->setStretchLastSection(true);
    m_actionsTable->horizontalHeader()->setHighlightSections(false);
    // Give the Workflow column twice the default width so names like
    // "Deploy Cloudflare" aren't truncated to "Deploy Cl...".
    m_actionsTable->setColumnWidth(0, 200);
    m_actionsTable->verticalHeader()->setVisible(false);
    m_actionsTable->setShowGrid(false);
    m_actionsTable->setWordWrap(false);
    m_actionsTable->setAlternatingRowColors(true);
    makeColumnsResizable(m_actionsTable); // spreadsheet-style draggable columns (#263)
    m_actionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_actionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_actionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Outline failed runs in red right in the list (adhoc #62), in place of the
    // banner that used to be pinned across the top of the tab.
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

    // Right: run detail (header, optional approval, log).
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
    // Toggle the approval diff between side-by-side and unified, sharing the same
    // persisted preference as the commit and pull-request diff views.
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
        showRun(m_selectedRunId); // re-render the diff in the new layout
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

    // Manual-run bar: appears at the top of the detail pane only for workflows
    // that declare `on: workflow_dispatch`. Lets the user trigger a run by hand
    // on a chosen branch (defaults to main).
    m_actionManualRunBar = new QWidget;
    m_actionManualRunButton = new QPushButton("Run workflow");
    m_actionManualRunButton->setObjectName("primaryButton");
    m_actionManualRunButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_actionManualRunButton, "rocket", 16);
    m_actionRunBranchCombo = new QComboBox;
    m_actionRunBranchCombo->setEditable(true); // allow any ref, not just listed
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

    // Rerun: re-queue the selected run as-is (same workflow content, commit and
    // ref). Sits beside the run title; hidden until a run is selected.
    m_actionRerunButton = new QPushButton("Rerun");
    m_actionRerunButton->setObjectName("ghostButton");
    m_actionRerunButton->setProperty("buttonSize", "sm");
    m_actionRerunButton->setCursor(Qt::PointingHandCursor);
    m_actionRerunButton->setToolTip("Run this action again with the same commit");
    setOcticon(m_actionRerunButton, "sync", 16);
    m_actionRerunButton->hide();
    connect(m_actionRerunButton, &QPushButton::clicked, this,
            &MainWindow::rerunSelectedRun);

    // Stop: abort the selected run while it's still queued or executing. Sits
    // beside Rerun; only shown for a run that's actually in flight.
    m_actionStopButton = new QPushButton("Stop");
    m_actionStopButton->setObjectName("dangerButton");
    m_actionStopButton->setProperty("buttonSize", "sm");
    m_actionStopButton->setCursor(Qt::PointingHandCursor);
    m_actionStopButton->setToolTip("Stop this run");
    setOcticon(m_actionStopButton, "stop", 16);
    m_actionStopButton->hide();
    connect(m_actionStopButton, &QPushButton::clicked, this,
            &MainWindow::stopSelectedRun);

    // Skip: drop a still-pending run before it executes. Sits beside Stop; only
    // shown for a run that hasn't started (queued or awaiting approval).
    m_actionSkipButton = new QPushButton("Skip");
    m_actionSkipButton->setObjectName("ghostButton");
    m_actionSkipButton->setProperty("buttonSize", "sm");
    m_actionSkipButton->setCursor(Qt::PointingHandCursor);
    m_actionSkipButton->setToolTip("Skip this run without executing it");
    setOcticon(m_actionSkipButton, "circle-slash", 16);
    m_actionSkipButton->hide();
    connect(m_actionSkipButton, &QPushButton::clicked, this,
            &MainWindow::skipSelectedRun);

    // Copy log: drop the selected run's full log on the clipboard. Sits beside
    // Rerun and shares its visible-when-a-run-is-selected lifecycle.
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

    // Fix with agent: only relevant for a failed run (showRun() hides it
    // otherwise). Starts a brand-new ad-hoc agent — its own worktree/branch/PR,
    // same as any other agent run — with the failing run's log as its task. The
    // agent and model are chosen in the two dropdowns beside it (adhoc #114).
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

    // Agent dropdown: which provider fixes the run. Data values match the
    // strings startAdHocAgentForRepo/agentConfigForProvider expect ("claude" is
    // the Claude API).
    m_actionFixAgentCombo = new QComboBox;
    m_actionFixAgentCombo->setObjectName("issueControlSm");
    m_actionFixAgentCombo->setCursor(Qt::PointingHandCursor);
    m_actionFixAgentCombo->setToolTip("Which agent fixes this run");
    m_actionFixAgentCombo->addItem(QStringLiteral("Claude"), QStringLiteral("claude"));
    m_actionFixAgentCombo->addItem(QStringLiteral("OpenAI"), QStringLiteral("openai"));
    m_actionFixAgentCombo->addItem(QStringLiteral("Claude Code"),
                                   QStringLiteral("claude-code"));
    m_actionFixAgentCombo->hide();
    // Start on the user's configured default agent (Settings -> Agents), same as
    // the branch "Fix with agent" bar.
    {
        const QString def = defaultAgentProvider();
        const QString want = def == QLatin1String("claude-api")
                                 ? QStringLiteral("claude")
                                 : def;
        const int idx = m_actionFixAgentCombo->findData(want);
        m_actionFixAgentCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    // Model dropdown: refilled to match the selected agent (e.g. Opus / Sonnet /
    // Haiku for Claude).
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

    // Failed runs are flagged red in the runs list itself (the table's
    // ActionFailureBorderDelegate outlines them), so a failure (e.g. a Cloudflare
    // deploy that errored out) stands out on the individual run rather than in a
    // banner pinned across the top of the tab (adhoc #62).
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter, 1);
    return page;
}

// ---- Settings: variables / secrets ----------------------------------------

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
        // Show the value in clear text when revealed; otherwise mask it. The
        // real text is always kept in UserRole for editing.
        auto *valueItem = new QTableWidgetItem(
            m_varsRevealed ? it.value()
                           : QString(qMin(it.value().size(), 24), QChar(0x2022)));
        valueItem->setData(Qt::UserRole, it.value());
        // When revealed, the value is click-to-copy; hint at it.
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
    // "Edit…" (and double-click) operate on the highlighted row; "Add…" always
    // starts blank. Resolve the row to edit from the current row, falling back
    // to the selection so either way of picking a row works.
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
    // Variables are written directly in add/edit/delete; nothing to flush here.
}
