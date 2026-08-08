// MainWindowActions: MainWindow feature methods, split out of MainWindow.cpp.
// Actions (CI on push), notifications, and variables/secrets settings.
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

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

constexpr int kActionStatusIconPx = 14;

constexpr int kMaxLocalPings = 100;

constexpr qint64 kPingDedupeWindowMs = 5000;
constexpr int kPingDedupeChars = 100;
constexpr int kPingDedupeMinChars = 40;

bool sameAlertText(const QString &left, const QString &right)
{
    auto key = [](const QString &text) {
        QString flat = text.simplified();
        flat.remove(QChar(0x2026)); // …
        flat.remove(QStringLiteral("..."));
        return flat.simplified().left(kPingDedupeChars);
    };
    const QString a = key(left);
    const QString b = key(right);
    if (a.isEmpty() || b.isEmpty())
        return false;
    const qsizetype shared =
        std::min<qsizetype>({a.size(), b.size(), kPingDedupeMinChars});
    return a.left(shared) == b.left(shared);
}

QString actionRunStatusIconName(const QString &status)
{
    if (status == ActionStatus::Running)
        return QStringLiteral("sync");
    if (status == ActionStatus::Queued)
        return QStringLiteral("history");
    if (status == ActionStatus::AwaitingApproval)
        return QStringLiteral("alert");
    if (status == ActionStatus::Success)
        return QStringLiteral("check-circle");
    if (status == ActionStatus::Failed || status == ActionStatus::Rejected)
        return QStringLiteral("x");
    if (status == ActionStatus::Cancelled)
        return QStringLiteral("circle-slash");
    if (status == ActionStatus::Skipped)
        return QStringLiteral("stop");
    return QStringLiteral("terminal");
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

// Settings key holding the served mirror's default-branch tip this node has
// already queued push workflows for. Keyed by repository, not by mirror path:
// an encrypted mirror is re-materialized into a fresh temporary directory on
// every seal, so the path is not stable but the commit is.
QString servedActionHeadKey(const RepositoryRecord &repo)
{
    return QStringLiteral("actions/servedHeads/") +
           QString::fromLatin1(
               QCryptographicHash::hash(
                   (repo.owner + QLatin1Char('/') + repo.name).toUtf8(),
                   QCryptographicHash::Sha256)
                   .toHex());
}

QString gitHeadRef(const QString &repository)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QStringLiteral("git"),
                  {QStringLiteral("--git-dir"), repository,
                   QStringLiteral("symbolic-ref"), QStringLiteral("--quiet"),
                   QStringLiteral("HEAD")});
    if (!process.waitForStarted(2000) ||
        !process.waitForFinished(5000) ||
        process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        process.kill();
        return {};
    }
    const QString ref =
        QString::fromUtf8(process.readAllStandardOutput().left(512)).trimmed();
    return kExternalActionRef.match(ref).hasMatch() ? ref : QString();
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

    for (const RepositoryRecord &repo : std::as_const(m_repositories))
        autoApproveAwaitingRuns(repo);

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
        return; // mirror not cloned yet; installed on the next sync
    const QString hooksDir = repo.mirrorPath + QStringLiteral("/hooks");
    QDir().mkpath(hooksDir);

    // A small POSIX-sh post-receive hook: it appends one event file per push to
    // the spool dir (atomically via a.tmp rename) for the app to pick up.
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
    if (deferredOutOfKeepAlivePump(m_actionSpoolSweepPending,
                                   [this] { scanActionSpool(); }))
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
    QSet<int> reattested;
    QHash<int, QString> pushedCommits;
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
                    !r.mirrorPath.trimmed().isEmpty()) {
                    if (directMirrorGatewayConfigured()) {
                        QString gatewayError;
                        if (!rebuildDirectMirrorGatewayConfiguration(
                                &gatewayError, true)) {
                            logSystem(
                                QStringLiteral(
                                    "Direct gateway refresh after pushed refs "
                                    "failed: %1")
                                    .arg(gatewayError));
                        }
                    }
                    publishRepository(idx, false);
                }
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
        const int pushedIndex = repoIndexFor(owner, name);
        if (pushedIndex >= 0)
            pushedCommits.insert(pushedIndex, commit.toLower());
        enqueuePushEvent(owner, name, commit, ref);
    }
    scanServedMirrorHeads(pushedCommits);
    processActionQueue();
    updateMirrorActionsRuntimeState();
}

void MainWindow::scanServedMirrorHeads(const QHash<int, QString> &pushedCommits)
{
    if (!m_actionStore)
        return;
    QSettings settings;
    for (int index = 0; index < m_repositories.size(); ++index) {
        const RepositoryRecord repo = m_repositories.at(index);
        if (repo.previewOnly || !repo.actionsEnabled ||
            repo.externallyManagedActions ||
            repo.localPath.trimmed().isEmpty() ||
            repo.mirrorPath.trimmed().isEmpty() ||
            !QDir(repo.mirrorPath).exists())
            continue;
        const QString ref = gitHeadRef(repo.mirrorPath);
        if (ref.isEmpty())
            continue;
        const QString commit = gitCommitAt(repo.mirrorPath, ref);
        if (commit.isEmpty())
            continue; // mid-reseal materialization, or an unborn branch
        const QString key = servedActionHeadKey(repo);
        const QString previous =
            settings.value(key).toString().trimmed().toLower();
        if (previous == commit)
            continue;
        settings.setValue(key, commit);
        if (previous.isEmpty())
            continue;
        if (pushedCommits.value(index) == commit)
            continue;
        enqueuePushEvent(repo.owner, repo.name, commit, ref);
    }
    settings.sync();
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

    const QString explicitKey =
        owner + QLatin1Char('\x1f') + name + QLatin1Char('\x1f') +
        commit.trimmed().toLower();
    if (m_explicitActionPushes.remove(explicitKey)) {
        logSystem(QStringLiteral(
                      "Actions: checks for %1/%2 @ %3 were already queued by "
                      "pull-request creation.")
                      .arg(owner, name, commit.left(8)));
        return;
    }

    if (!repo.actionsEnabled)
        return; // push detection only; no workflow execution for this repo

    queueWorkflowsForCommit(repoIndex, owner, name, commit, ref);
}

void MainWindow::queueWorkflowsForCommit(int repoIndex, const QString &owner,
                                         const QString &name, const QString &commit,
                                         const QString &ref,
                                         WorkflowTrigger trigger)
{
    if (repoIndex < 0 || repoIndex >= m_repositories.size() || !m_actionStore)
        return;
    const QString mirrorPath = m_repositories.at(repoIndex).mirrorPath;
    const QString ownerKey = owner;
    const QString nameKey = name;
    const QString commitKey = commit;
    const QString refKey = ref;

    runOffThread<WorkflowScan>(
        [mirrorPath, commitKey, trigger]() -> WorkflowScan {
            WorkflowScan scan;
            if (!QDir(mirrorPath).exists()) {
                scan.mirrorMissing = true;
                return scan;
            }
            const QStringList base{QStringLiteral("-C"), mirrorPath};
            auto capture = [&base](const QStringList &args) -> QString {
                QProcess process;
                process.setProcessChannelMode(QProcess::SeparateChannels);
                process.start(QStringLiteral("git"), base + args);
                if (!process.waitForStarted(3000) ||
                    !process.waitForFinished(10000)) {
                    process.kill();
                    process.waitForFinished(1000);
                    return {};
                }
                if (process.exitCode() != 0)
                    return {};
                return QString::fromUtf8(process.readAllStandardOutput());
            };

            if (trigger == WorkflowTrigger::Push) {
                const QStringList changed =
                    capture({QStringLiteral("diff-tree"),
                             QStringLiteral("--no-commit-id"),
                             QStringLiteral("--name-only"),
                             QStringLiteral("-r"), commitKey})
                        .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                const auto isMetadataPath = [](const QString &p) {
                    return p.startsWith(QLatin1String(".forkmesh/issues/")) ||
                           p.startsWith(QLatin1String(".forkmesh/pulls/")) ||
                           p.startsWith(QLatin1String(".forkmesh/commits/"));
                };
                if (!changed.isEmpty() &&
                    std::all_of(changed.cbegin(), changed.cend(),
                                isMetadataPath)) {
                    scan.metadataOnly = true;
                    return scan;
                }
            }

            const QStringList paths =
                capture({QStringLiteral("ls-tree"), QStringLiteral("-r"),
                         QStringLiteral("--name-only"), commitKey,
                         QStringLiteral("--"), QStringLiteral(".forkmesh")})
                    .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &path : paths) {
                if (!(path.endsWith(QLatin1String(".yml")) ||
                      path.endsWith(QLatin1String(".yaml"))))
                    continue;
                const QString content =
                    capture({QStringLiteral("show"),
                             commitKey + QLatin1Char(':') + path});
                if (content.isEmpty())
                    continue;
                scan.workflows.append({path, content});
            }
            if (!scan.workflows.isEmpty()) {
                QString tree, execution;
                ActionStore::repositoryStateDigest(mirrorPath, commitKey, &tree,
                                                   &execution, nullptr);
            }
            return scan;
        },
        [this, ownerKey, nameKey, commitKey, refKey, trigger](WorkflowScan scan) {
            applyWorkflowScan(ownerKey, nameKey, commitKey, refKey, trigger,
                              scan);
        });
}

void MainWindow::applyWorkflowScan(const QString &owner, const QString &name,
                                   const QString &commit, const QString &ref,
                                   WorkflowTrigger trigger,
                                   const WorkflowScan &scan)
{
    if (!m_actionStore)
        return;
    const int repoIndex = repoIndexFor(owner, name);
    if (repoIndex < 0 || repoIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(repoIndex);

    if (scan.mirrorMissing) {
        logSystem(QStringLiteral(
                      "Actions: served mirror %1 for %2/%3 is missing \xE2\x80\x94 "
                      "cannot look up workflows at %4.")
                      .arg(repo.mirrorPath, owner, name, commit.left(8)));
        return;
    }
    if (scan.metadataOnly) {
        logSystem(QString::fromUtf8(
                      "Actions: %1/%2 @ %3 only touches issues/PRs \xE2\x80\x94 "
                      "skipping workflows.")
                      .arg(owner, name, commit.left(8)));
        return;
    }

    bool added = false;
    for (const auto &entry : scan.workflows) {
        const QString &path = entry.first;
        const QString &content = entry.second;
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
        const bool approved = resolveActionApproval(repo, run);
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
    // Re-entrancy guard: this fires on a debounce timer and from many
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
        refreshRepoTabCounts(); // the repo only just materialized: every badge is empty
    else if (visibleTab == 2)
        reloadIssuesInBackground();
    reloadPullsInBackground();
    if (visibleTab == 6)
        refreshRepoActions();
    updateRepoDetailStatus();
    updateRepoActionMenus();
    updateRepoCodeSize();
    refreshRepoSyncIndicators();
    refreshRepoChangeBadge(); // a sync/merge/commit moves the working tree too
    refreshRepoPinBanner(); // a sync may have advanced refs past the pinned hash
    m_treeLoadedForIndex = -1; // force the explorer tree to rebuild on next use
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
    ActionWorkflow probe; // only runsOn takes part in the match
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
    add(machineNodeName()); // this machine first: the common answer
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
        return ActionNeeds::State::Ready; // vanished; the queue drops it
    const ActionWorkflow wf =
        ActionFile::parse(run->workflowPath, run->workflowContent);
    if (wf.needs.isEmpty())
        return ActionNeeds::State::Ready;
    return ActionNeeds::resolve(*run, wf.needs, m_actionRuns, detail);
}

void MainWindow::noteActionRunWaiting(int runId, const QString &detail)
{
    if (m_actionWaitingRuns.contains(runId))
        return; // already announced; the queue is swept every few seconds
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
            return; // pool saturated; a finishing run will resume the queue
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
            return; // everything queued is waiting on another workflow
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
        // Encrypted mirrors live in a temporary materialization that every
        // sealing pass replaces and deletes. Resolve the one that is live right
        // now and hold it open for the whole run, so a re-seal (publishing a
        // release kicks one off) cannot pull the checkout source out from under
        // an in-flight build.
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
                              false, run->id);
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
        const QString body =
            QString::fromUtf8("%1 \xC2\xB7 %2/%3")
                .arg(run->workflowName, run->owner, run->name);
        logSystem(QStringLiteral("Actions: %1 run #%2 \"%3\" for %4/%5 @ %6. "
                                 "Run log: %7")
                      .arg(ok ? QStringLiteral("succeeded")
                              : cancelled ? QStringLiteral("stopped")
                                          : QStringLiteral("failed"))
                      .arg(run->id)
                      .arg(run->workflowName, run->owner, run->name,
                           run->commit.left(8), actionRunLogPath(*run)));
        if (cancelled) {
            addNotification(title, body, false, run->id);
        } else {
            notifyActionEvent(title, body, !ok, run->id);
        }
        if (!ok && !cancelled)
            maybeAutoFixFailedRun(*run);
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
    logSystem(QStringLiteral(
                  "Release: refreshing artifact metadata for %1/%2 online.")
                  .arg(run->owner, run->name));
    publishRepositoryAfterMirrorRefresh(index, /*showDialogOnError=*/false);
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
        updatePullSubTabCounts(pr);
        return;
    }
}

void MainWindow::notifyActionEvent(const QString &title, const QString &body,
                                   bool warning, int runId)
{
    addNotification(title, body, warning, runId);
    const QString mode = actionAlertMode();
    if (mode == QLatin1String("none"))
        return;
    if (title == QLatin1String("Action started") &&
        !QSettings().value(kActionAlertStartedSetting, false).toBool())
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

qint64 MainWindow::recordNotification(AppNotification item)
{
    if (item.title.simplified().isEmpty() && item.body.simplified().isEmpty())
        return 0;
    item.id = m_nextNotificationId++;
    if (item.timestampMs <= 0)
        item.timestampMs = QDateTime::currentMSecsSinceEpoch();
    if (item.kind.isEmpty())
        item.kind = item.link.isValid() ? item.link.kind
                                        : QStringLiteral("desktop");
    if (item.repo.isEmpty() && !item.link.owner.isEmpty())
        item.repo = item.link.owner + QLatin1Char('/') + item.link.name;
    if (!item.syncDecided) {
        QString reason;
        item.sync = initialPingSync(item, &reason);
        item.syncReason = reason;
    }
    m_notifications.prepend(item);
    trimLocalPings();
    updateNotificationButton();
    scheduleNotificationJournalSave();
    if (!item.quiet)
        flashNotification(item);
    if (m_notificationsTable && m_sectionStack &&
        m_sectionStack->currentIndex() == 3)
        refreshNotificationsTable();
    return item.id;
}

void MainWindow::trimLocalPings()
{
    while (m_notifications.size() > kMaxLocalPings) {
        int victim = -1;
        for (int i = m_notifications.size() - 1; i >= 0; --i) {
            const AppNotification &item = m_notifications.at(i);
            if (!item.warning || !forkmesh::pingSyncIsUnsynced(item.sync)) {
                victim = i;
                break;
            }
        }
        m_notifications.removeAt(victim >= 0 ? victim
                                             : m_notifications.size() - 1);
    }
}

// What a ping's cloud state is the moment it is filed, before anything has been
// attempted.
// Only a failure has anywhere to go: it becomes a signed report in the relay's
// error log (reportUserVisibleError), which is how an error on a headless node
// or an unwatched machine is ever seen. Everything else is this machine's own
// history and says so, rather than implying a sync that was never going to
// happen. An alert raised with no relay to reach, or no account to sign with,
// is Offline: nothing was sent and nothing is queued, and the Pings page is the
// only record there will ever be of it.
// Deliberately never Pending: only the reporter knows whether a report is
// actually going out for this row, and it moves the row there itself. A ping
// nothing reports — a quiet OS notification, an alert raised with the in-app
// cards switched off — would otherwise sit at "Syncing…" forever waiting on a
// request nobody sent.
forkmesh::PingSync MainWindow::initialPingSync(const AppNotification &item,
                                               QString *reasonOut) const
{
    auto answer = [reasonOut](forkmesh::PingSync state, const QString &why) {
        if (reasonOut)
            *reasonOut = why;
        return state;
    };
    if (!item.warning)
        return answer(forkmesh::PingSync::LocalOnly,
                      QStringLiteral("a desktop event, not a failure — the "
                                     "cloud is never told about it"));
    if (!QSettings()
             .value(forkmesh::kReportUserVisibleErrorsSetting, true)
             .toBool())
        return answer(forkmesh::PingSync::LocalOnly,
                      QStringLiteral("error reporting is turned off for this "
                                     "node (Settings \xE2\x86\x92 Diagnostics)"));
    if (!m_networkAccess)
        return answer(forkmesh::PingSync::Offline,
                      QStringLiteral("this node has no network access, so "
                                     "nothing was sent"));
    const QString owner = accountOwner();
    if (owner.isEmpty() || !m_profileIdentity.isValid()
        || !hasOwnerSigningCapability(owner))
        return answer(forkmesh::PingSync::Offline,
                      QStringLiteral("no signed-in account to sign a report "
                                     "with, so nothing was sent"));
    return answer(forkmesh::PingSync::LocalOnly,
                  QStringLiteral("kept on this machine unless it is reported "
                                 "to the relay"));
}

void MainWindow::setPingSync(qint64 pingId, forkmesh::PingSync state,
                             const QString &reason)
{
    if (pingId <= 0)
        return;
    for (AppNotification &item : m_notifications) {
        if (item.id != pingId)
            continue;
        if (item.sync == state && item.syncReason == reason)
            return;
        item.sync = state;
        item.syncReason = reason;
        scheduleNotificationJournalSave();
        if (m_notificationsTable && m_sectionStack &&
            m_sectionStack->currentIndex() == 3)
            refreshNotificationsTable();
        return;
    }
}

#ifdef FORKMESH_WINDOW_TESTS
QString MainWindow::testPingStatusFor(const QString &needle)
{
    constexpr int kTitleColumn = 2;
    constexpr int kStatusColumn = 6;
    if (m_notificationsTable) {
        refreshNotificationsTable();
        for (int row = 0; row < m_notificationsTable->rowCount(); ++row) {
            QTableWidgetItem *title =
                m_notificationsTable->item(row, kTitleColumn);
            QTableWidgetItem *status =
                m_notificationsTable->item(row, kStatusColumn);
            if (title && status && title->text().contains(needle))
                return status->text();
        }
        return {};
    }
    for (const AppNotification &item : std::as_const(m_notifications)) {
        if (item.title.contains(needle) || item.body.contains(needle))
            return forkmesh::pingSyncLabel(item.sync);
    }
    return {};
}
#endif


QString MainWindow::notificationJournalPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/pings.json");
}

void MainWindow::scheduleNotificationJournalSave()
{
    if (!m_notificationJournalTimer) {
        m_notificationJournalTimer = new QTimer(this);
        m_notificationJournalTimer->setSingleShot(true);
        m_notificationJournalTimer->setInterval(1500);
        connect(m_notificationJournalTimer, &QTimer::timeout, this,
                &MainWindow::saveNotificationJournal);
    }
    m_notificationJournalTimer->start();
}

void MainWindow::saveNotificationJournal()
{
    const QString path = notificationJournalPath();
    if (path.isEmpty())
        return;
    QJsonArray rows;
    for (const AppNotification &item : std::as_const(m_notifications)) {
        QJsonObject obj{
            {QStringLiteral("title"), item.title},
            {QStringLiteral("body"), item.body},
            {QStringLiteral("ts"), item.timestampMs},
            {QStringLiteral("warning"), item.warning},
            {QStringLiteral("kind"), item.kind},
            {QStringLiteral("actor"), item.actor},
            {QStringLiteral("repo"), item.repo},
            {QStringLiteral("sync"), forkmesh::pingSyncToken(item.sync)},
            {QStringLiteral("syncReason"), item.syncReason}};
        if (item.link.isValid()) {
            obj.insert(QStringLiteral("link"),
                       QJsonObject{
                           {QStringLiteral("kind"), item.link.kind},
                           {QStringLiteral("owner"), item.link.owner},
                           {QStringLiteral("name"), item.link.name},
                           {QStringLiteral("number"), item.link.number},
                           {QStringLiteral("ref"), item.link.ref}});
        }
        rows.append(obj);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(rows).toJson(QJsonDocument::Compact));
    file.commit();
}

void MainWindow::loadNotificationJournal()
{
    const QString path = notificationJournalPath();
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray rows =
        QJsonDocument::fromJson(file.readAll()).array();
    QList<AppNotification> restored;
    for (const QJsonValue &value : rows) {
        const QJsonObject obj = value.toObject();
        AppNotification item;
        item.id = m_nextNotificationId++;
        item.title = obj.value(QStringLiteral("title")).toString();
        item.body = obj.value(QStringLiteral("body")).toString();
        item.timestampMs = qint64(obj.value(QStringLiteral("ts")).toDouble());
        item.warning = obj.value(QStringLiteral("warning")).toBool();
        item.kind = obj.value(QStringLiteral("kind")).toString();
        item.actor = obj.value(QStringLiteral("actor")).toString();
        item.repo = obj.value(QStringLiteral("repo")).toString();
        item.sync = forkmesh::pingSyncFromToken(
            obj.value(QStringLiteral("sync")).toString());
        item.syncReason = obj.value(QStringLiteral("syncReason")).toString();
        const forkmesh::PingSync settled =
            forkmesh::pingSyncAfterRestart(item.sync);
        if (settled != item.sync) {
            item.sync = settled;
            item.syncReason = forkmesh::pingSyncRestartReason();
        }
        const QJsonObject link = obj.value(QStringLiteral("link")).toObject();
        if (!link.isEmpty()) {
            item.link.kind = link.value(QStringLiteral("kind")).toString();
            item.link.owner = link.value(QStringLiteral("owner")).toString();
            item.link.name = link.value(QStringLiteral("name")).toString();
            item.link.number = link.value(QStringLiteral("number")).toInt();
            item.link.ref = link.value(QStringLiteral("ref")).toString();
        }
        if (item.title.isEmpty() && item.body.isEmpty())
            continue;
        restored.append(item);
    }
    m_notifications.append(restored);
    trimLocalPings();
    updateNotificationButton();
    if (m_notificationsTable)
        refreshNotificationsTable();
}

void MainWindow::flashNotification(const AppNotification &item)
{
    if (!QSettings().value(kInAppNotificationsSetting, true).toBool())
        return;
    QString text = item.title.simplified();
    const QString detail = item.body.simplified();
    if (!detail.isEmpty())
        text += QString::fromUtf8(" \xE2\x80\x94 ") + detail; // —
    if (text.isEmpty())
        return;
    const int duration = QSettings()
                             .value(kInAppNotificationDurationSetting, 5)
                             .toInt();
    const qint64 previousPingToast = m_pingToastId;
    m_pingToastId = item.id;
    const QPixmap previousPendingAvatar = m_pendingToastAvatar;
    m_pendingToastAvatar = pingActorAvatar(item);
    if (item.warning) {
        flashMessage(text, true, QString(), duration, item.kind, item.runId);
        flashErrorBorder();
    } else {
        flashMessage(text, false, QString(), duration, item.kind, item.runId);
    }
    m_pendingToastAvatar = previousPendingAvatar;
    m_pingToastId = previousPingToast;
}

QPixmap MainWindow::pingActorAvatar(const AppNotification &item) const
{
    if (item.kind != QLatin1String("chat"))
        return QPixmap();
    if (item.actor.trimmed().isEmpty() && item.actorId.trimmed().isEmpty())
        return QPixmap();
    return chatActorAvatar(item.actorId, item.actor, kToastAvatarPx);
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
    m_errorBorderTimer->start(1500); // world-admin-error-arrival's 1.5s
}

void MainWindow::flashCelebrationBorder()
{
    if (!m_celebrationBorderOverlay) {
        class CelebrationBorderWidget : public QWidget
        {
        public:
            explicit CelebrationBorderWidget(QWidget *parent) : QWidget(parent)
            {
                setAttribute(Qt::WA_TransparentForMouseEvents);
                setAttribute(Qt::WA_NoSystemBackground);
                setAttribute(Qt::WA_TranslucentBackground);
                setObjectName(QStringLiteral("celebrationBorderOverlay"));
            }

        protected:
            void paintEvent(QPaintEvent *) override
            {
                QPainter painter(this);
                painter.setRenderHint(QPainter::Antialiasing, false);
                QPen pen(QColor(63, 185, 80, 190), 3);
                pen.setJoinStyle(Qt::MiterJoin);
                painter.setPen(pen);
                painter.drawRect(rect().adjusted(1, 1, -2, -2));
                for (int step = 1; step <= 6; ++step) {
                    const int inset = 2 + step * 3;
                    QPen glow(QColor(63, 185, 80, 58 - step * 8), 3);
                    glow.setJoinStyle(Qt::MiterJoin);
                    painter.setPen(glow);
                    painter.drawRect(
                        rect().adjusted(inset, inset, -inset - 1, -inset - 1));
                }
            }
        };
        m_celebrationBorderOverlay = new CelebrationBorderWidget(this);
        m_celebrationBorderTimer = new QTimer(this);
        m_celebrationBorderTimer->setSingleShot(true);
        connect(m_celebrationBorderTimer, &QTimer::timeout, this, [this] {
            if (m_celebrationBorderOverlay)
                m_celebrationBorderOverlay->hide();
        });
    }
    m_celebrationBorderOverlay->setGeometry(rect());
    m_celebrationBorderOverlay->show();
    m_celebrationBorderOverlay->raise();
    m_celebrationBorderTimer->start(2000);
}

void MainWindow::startRestartCautionFlash()
{
    if (!m_restartCautionBorderOverlay) {
        class RestartCautionBorderWidget : public QWidget
        {
        public:
            explicit RestartCautionBorderWidget(QWidget *parent) : QWidget(parent)
            {
                setAttribute(Qt::WA_TransparentForMouseEvents);
                setAttribute(Qt::WA_NoSystemBackground);
                setAttribute(Qt::WA_TranslucentBackground);
                setObjectName(QStringLiteral("restartCautionBorderOverlay"));
            }

        protected:
            void paintEvent(QPaintEvent *) override
            {
                QPainter painter(this);
                painter.setRenderHint(QPainter::Antialiasing, false);
                QPen pen(QColor(210, 153, 34, 220), 3);
                pen.setJoinStyle(Qt::MiterJoin);
                painter.setPen(pen);
                painter.drawRect(rect().adjusted(1, 1, -2, -2));
                for (int step = 1; step <= 5; ++step) {
                    const int inset = 2 + step * 3;
                    QPen glow(QColor(210, 153, 34, 62 - step * 10), 3);
                    glow.setJoinStyle(Qt::MiterJoin);
                    painter.setPen(glow);
                    painter.drawRect(
                        rect().adjusted(inset, inset, -inset - 1, -inset - 1));
                }
            }
        };
        m_restartCautionBorderOverlay = new RestartCautionBorderWidget(this);
        m_restartCautionBorderTimer = new QTimer(this);
        connect(m_restartCautionBorderTimer, &QTimer::timeout, this, [this] {
            if (!m_restartCautionBorderOverlay)
                return;
            m_restartCautionBorderOverlay->setVisible(
                !m_restartCautionBorderOverlay->isVisible());
            if (m_restartCautionBorderOverlay->isVisible())
                m_restartCautionBorderOverlay->raise();
        });
    }
    m_restartCautionBorderOverlay->setGeometry(rect());
    m_restartCautionBorderOverlay->show();
    m_restartCautionBorderOverlay->raise();
    m_restartCautionBorderTimer->start(650);
}

void MainWindow::stopRestartCautionFlash()
{
    if (m_restartCautionBorderTimer)
        m_restartCautionBorderTimer->stop();
    if (m_restartCautionBorderOverlay)
        m_restartCautionBorderOverlay->hide();
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
        showOverviewCommits(); // the universal Git workspace
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
    updateChromeDotDivider(); // the runs' hairline follows the strip itself
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
        m_repoDetailTabs->button(6)->click();
    else if (m_repoDetailStack) {
        ensureRepoDetailTabBuilt(6);
        m_repoDetailStack->setCurrentIndex(6);
    }
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
    return link; // invalid (no kind) when nothing above matched
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
        {QStringLiteral("operational_alert"), QStringLiteral("System alert")},
    };
    return labels.value(kind, QStringLiteral("Web"));
}

bool webPingIsRecovery(const QJsonObject &alert)
{
    const QString state = alert.value(QStringLiteral("meta"))
                              .toObject()
                              .value(QStringLiteral("state"))
                              .toString()
                              .trimmed()
                              .toLower();
    if (state == QLatin1String("up"))
        return true;
    if (state == QLatin1String("down"))
        return false;
    const QString title = alert.value(QStringLiteral("title")).toString();
    return title.contains(QLatin1String("recovered"), Qt::CaseInsensitive) ||
           title.contains(QLatin1String("back online"), Qt::CaseInsensitive);
}
} // namespace

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
        "Delete all past desktop and website pings"));
    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_notifications.clear();
        clearWebAlerts();
        updateNotificationButton();
        saveNotificationJournal(); // emptied on disk too, not just on screen
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
    m_notificationsTable->setObjectName("issueTable"); // reuse the table styling
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
        7, Qt::DescendingOrder); // newest first by default
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
        QString statusTooltip;
        qint64 whenMs = 0;
        QString link;
        int runId = -1;
        bool warning = false;
        bool good = false;
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
        const QColor green("#3fb950");
        constexpr int kStatusColumn = 6;
        for (int column = 0; column < cells.size(); ++column) {
            QTableWidgetItem *cell = cells.at(column);
            if (!cell->text().isEmpty())
                cell->setToolTip(cell->text());
            if (column == kStatusColumn && !data.statusTooltip.isEmpty())
                cell->setToolTip(data.statusTooltip);
            if (data.warning)
                cell->setForeground(red);
            else if (data.good)
                cell->setForeground(green);
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
        data.status = forkmesh::pingSyncLabel(notice.sync);
        data.statusTooltip =
            notice.syncReason.isEmpty()
                ? forkmesh::pingSyncDescription(notice.sync)
                : forkmesh::pingSyncDescription(notice.sync) +
                      QString::fromUtf8("\n\xE2\x80\x94 ") + notice.syncReason;
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
        const bool recovery = kind == QLatin1String("operational_alert") &&
                              webPingIsRecovery(alert);
        NotificationRow data;
        data.type = recovery ? QStringLiteral("System recovered")
                             : webPingKindLabel(kind);
        data.kind = kind;
        data.title = (unread ? QString::fromUtf8("\xE2\x97\x8F ") : QString()) +
                     (title.isEmpty() ? QStringLiteral("Website ping") : title);
        data.detail = body;
        data.repo = repo;
        data.actor = alert.value(QStringLiteral("actor")).toString().trimmed();
        data.status = unread ? QStringLiteral("Unread")
                             : QStringLiteral("Read");
        data.statusTooltip =
            QStringLiteral("Stored in your account's cloud ping inbox.");
        data.whenMs = qint64(alert.value(QStringLiteral("ts")).toDouble());
        data.link = href;
        data.warning = !recovery &&
                       (kind == QLatin1String("error_group") ||
                        kind == QLatin1String("operational_alert"));
        data.good = recovery;
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
    scheduleNotificationJournalSave();
    refreshNotificationsTable();
}

// Delete one mirrored website ping from this account's inbox on the relay.
// The desktop holds no session token, so the request is signed with the
// account key and a proof that binds the id being deleted.
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

// Pull this account's website alert inbox onto the Notifications page. The
// desktop normally holds no account session token (authenticateSilently proves
// the account key instead), so the read is signed exactly like the Tasks
// board's — see _account_alert_signed_session in the worker.
void MainWindow::refreshWebAlerts(bool force)
{
    if (!m_networkAccess || m_webAlertsLoading)
        return;
    const QString node = accountOwner().trimmed().toLower();
    if (node.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_webAlertsFetchedAtMs > 0 &&
        (!force || now - m_webAlertsFetchedAtMs < kWebAlertPushFloorMs))
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
        return; // neither a session nor this account's signing key
    m_webAlertsLoading = true;
    m_webAlertsFetchedAtMs = now;
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_webAlertsLoading = false;
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 ||
            status >= 300) {
            m_webAlertsFetchedAtMs = 0;
            return;
        }
        const QJsonObject payload =
            QJsonDocument::fromJson(reply->readAll()).object();
        m_webAlerts = payload.value(QStringLiteral("notifications")).toArray();
        m_webAlertsUnread = payload.value(QStringLiteral("unread")).toInt();
        updateNotificationButton();
        const bool loadingStartupBaseline = !m_webAlertsBaselineLoaded;
        m_webAlertsBaselineLoaded = true;
        for (const QJsonValue &value : std::as_const(m_webAlerts)) {
            const QJsonObject alert = value.toObject();
            const QString kind =
                alert.value(QStringLiteral("kind")).toString().trimmed();
            if ((kind != QLatin1String("error_group") &&
                 kind != QLatin1String("operational_alert")) ||
                alert.value(QStringLiteral("readAt")).toDouble() > 0)
                continue;
            const QString id =
                alert.value(QStringLiteral("id")).toString().trimmed();
            if (id.isEmpty() || m_flashedWebAlertIds.contains(id))
                continue;
            m_flashedWebAlertIds.insert(id);
            if (loadingStartupBaseline)
                continue;
            if (kind == QLatin1String("operational_alert") &&
                !QSettings().value(kSystemAlertSetting, true).toBool())
                continue;
            const QString title =
                alert.value(QStringLiteral("title")).toString().trimmed();
            const bool recovery = kind == QLatin1String("operational_alert") &&
                                  webPingIsRecovery(alert);
            AppNotification ping;
            ping.title = title.isEmpty()
                             ? (kind == QLatin1String("operational_alert")
                                    ? (recovery
                                           ? QStringLiteral("ForkMesh system recovered")
                                           : QStringLiteral("ForkMesh system alert"))
                                    : QStringLiteral("New error group on the relay"))
                             : title;
            ping.body = alert.value(QStringLiteral("body")).toString().trimmed();
            ping.warning = !recovery;
            ping.kind = kind;
            ping.id = -1;
            flashNotification(ping);
        }
        if (m_notificationsTable && m_sectionStack &&
            m_sectionStack->currentIndex() == 3)
            refreshNotificationsTable();
    });
}

void MainWindow::clearWebAlerts()
{
    m_webAlerts = QJsonArray();
    m_webAlertsUnread = 0;
    if (!m_networkAccess)
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
    // A bulk deletion has its own signed proof; a captured row-delete proof
    // cannot be replayed to empty the whole inbox.
    if (!authenticateOrgTaskRequest(url, request, kAccountAlertClearProof,
                                    QStringLiteral("all"))) {
        flashMessage(QStringLiteral(
                         "Couldn't clear website pings: sign in or unlock "
                         "this account's key."),
                     true);
        m_webAlertsFetchedAtMs = 0;
        return;
    }
    const QJsonObject body{{QStringLiteral("node"), node},
                           {QStringLiteral("all"), true}};
    QNetworkReply *reply = m_networkAccess->sendCustomRequest(
        request, QByteArrayLiteral("DELETE"),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 ||
            status >= 300) {
            flashMessage(QStringLiteral("Couldn't clear website pings (%1).")
                             .arg(status),
                         true);
            m_webAlertsFetchedAtMs = 0;
            return;
        }
    });
}

void MainWindow::showNotifications()
{
    showSection(3);
}

void MainWindow::postNotification(const QString &title, const QString &body,
                                  bool warning, const QString &icon)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString osText = (title + QLatin1Char(' ') + body).simplified();
    const bool alreadyFiled = std::any_of(
        m_notifications.cbegin(), m_notifications.cend(),
        [&](const AppNotification &filed) {
            if (now - filed.timestampMs > kPingDedupeWindowMs)
                return false;
            const QString filedText =
                (filed.title + QLatin1Char(' ') + filed.body).simplified();
            return sameAlertText(filedText, osText)
                   || sameAlertText(filed.body, body)
                   || sameAlertText(filed.title, body);
        });
    if (!alreadyFiled && !osText.isEmpty()) {
        AppNotification item;
        item.title = title;
        item.body = body;
        item.warning = warning;
        item.kind = QStringLiteral("desktop");
        item.quiet = true; // the OS is already showing it
        recordNotification(item);
    }
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
        wfItem->setIcon(themedOcticon(actionRunStatusIconName(run.status),
                                      actionStatusColor(run.status),
                                      kActionStatusIconPx));
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
    updateActionsSpinTimer();
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

    if (auto *b = dynamic_cast<VerticalIconButton *>(tab))
        b->setBadgeCount(m_repoWorkflows.size());
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
            continue; // never actually ran to completion
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
                &MainWindow::animateRunningAgentIcons); // refresh live row metadata
    }
    if (!m_agentsSpinTimer->isActive())
        m_agentsSpinTimer->start(kAgentSpinTickMs);
}

void MainWindow::updateActionsSpinTimer()
{
    if (!m_actionsTable)
        return;
    bool anyRunning = false;
    for (int r = 0; r < m_actionsTable->rowCount(); ++r) {
        QTableWidgetItem *item = m_actionsTable->item(r, 0);
        if (!item)
            continue;
        const ActionRun *run = findRun(item->data(Qt::UserRole).toInt());
        if (run && run->status == ActionStatus::Running) {
            anyRunning = true;
            break;
        }
    }
    if (!anyRunning) {
        if (m_actionsSpinTimer)
            m_actionsSpinTimer->stop();
        return;
    }
    if (!m_actionsSpinTimer) {
        m_actionsSpinTimer = new QTimer(this);
        connect(m_actionsSpinTimer, &QTimer::timeout, this,
                &MainWindow::animateRunningActionIcons);
    }
    if (!m_actionsSpinTimer->isActive())
        m_actionsSpinTimer->start(kAgentSpinTickMs);
}

void MainWindow::animateRunningActionIcons()
{
    if (!m_actionsTable)
        return;
    ++m_actionSpinTicks;
    const qreal angle = qreal((m_actionSpinTicks * 11) % 360);
    QSignalBlocker block(m_actionsTable);
    for (int r = 0; r < m_actionsTable->rowCount(); ++r) {
        QTableWidgetItem *item = m_actionsTable->item(r, 0);
        if (!item)
            continue;
        const ActionRun *run = findRun(item->data(Qt::UserRole).toInt());
        if (!run || run->status != ActionStatus::Running)
            continue;
        const QPixmap spinning = rotatedTintedOcticonPixmap(
            actionRunStatusIconName(run->status), actionStatusColor(run->status),
            kActionStatusIconPx, angle);
        item->setIcon(QIcon(spinning));
        m_actionsTable->viewport()->update(m_actionsTable->visualItemRect(item));
    }
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
                return; // superseded, or the user moved to another repo
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
    if (m_actionsAutoApproveCheck) {
        QSignalBlocker block(m_actionsAutoApproveCheck);
        m_actionsAutoApproveCheck->setChecked(repo.actionsAutoApprove);
    }

    auto *all = new QListWidgetItem(QStringLiteral("All workflows"));
    all->setData(Qt::UserRole, QString());
    m_actionWorkflowList->addItem(all);
    all->setSelected(true);

    const QList<ActionWorkflow> wfs = availableWorkflowsForRepo(repo);
    m_repoWorkflows = wfs; // cache so the manual-run bar can look workflows up
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
    const bool approved = resolveActionApproval(repo, run);
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
        m_actionFixAgentCombo->setVisible(false);
    if (m_actionFixModelCombo)
        m_actionFixModelCombo->setVisible(false);
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

bool MainWindow::resolveActionApproval(const RepositoryRecord &repo,
                                       const ActionRun &run)
{
    const bool alreadyApproved = ActionStore::isApproved(
        run.repoKey(), run.workflowPath, run.workflowContent,
        run.repositoryTree, run.executionDigest);
    if (alreadyApproved || !repo.actionsAutoApprove)
        return alreadyApproved;

    ActionStore::approve(run.repoKey(), run.workflowPath, run.workflowContent,
                         run.repositoryTree, run.executionDigest);
    return ActionStore::isApproved(
        run.repoKey(), run.workflowPath, run.workflowContent,
        run.repositoryTree, run.executionDigest);
}

int MainWindow::autoApproveAwaitingRuns(const RepositoryRecord &repo)
{
    if (!m_actionStore || !repo.actionsEnabled || !repo.actionsAutoApprove)
        return 0;

    int queued = 0;
    for (ActionRun &run : m_actionRuns) {
        if (run.status != ActionStatus::AwaitingApproval ||
            run.owner != repo.owner || run.name != repo.name)
            continue;

        ActionRun verified = run;
        QString snapshotError;
        if (workflowContentAt(repo.mirrorPath, run.commit, run.workflowPath) !=
                run.workflowContent ||
            !bindActionRepositoryState(&verified, repo.mirrorPath,
                                       &snapshotError) ||
            verified.repositoryTree != run.repositoryTree ||
            verified.executionDigest != run.executionDigest) {
            logSystem(QStringLiteral(
                          "Actions: keeping \"%1\" for %2/%3 @ %4 awaiting "
                          "review because its saved repository state could not "
                          "be reverified%5.")
                          .arg(run.workflowName, run.owner, run.name,
                               run.commit.left(8),
                               snapshotError.isEmpty()
                                   ? QString()
                                   : QStringLiteral(": ") + snapshotError));
            continue;
        }
        if (!resolveActionApproval(repo, run)) {
            logSystem(QStringLiteral(
                          "Actions: keeping \"%1\" for %2/%3 @ %4 awaiting "
                          "review because its automatic approval could not be "
                          "saved.")
                          .arg(run.workflowName, run.owner, run.name,
                               run.commit.left(8)));
            continue;
        }

        run.status = ActionStatus::Queued;
        m_actionStore->saveRun(run);
        if (!m_actionQueue.contains(run.id))
            m_actionQueue.append(run.id);
        ++queued;
        logSystem(QStringLiteral(
                      "Actions: automatically approved \"%1\" for %2/%3 "
                      "@ %4.")
                      .arg(run.workflowName, run.owner, run.name,
                           run.commit.left(8)));
    }
    if (queued > 0)
        scheduleMirrorActionsSummary(0);
    return queued;
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
    const bool approved = resolveActionApproval(
        m_repositories.at(repoIndex), run);
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
        startAdHocAgentForRepo(repoIndex, prompt, provider, /*createPr=*/true, model);
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
            sessionId = s.id; // sessions are stored oldest-first; keep the last match
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

    queueAgentSteerMessage(sessionId, prompt);
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
                refreshWorkflowNodeCombo(); // the dropdown edits the selection
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
        "workflows. New snapshots run automatically when automatic approval "
        "is enabled below.");
    connect(m_actionsEnabledCheck, &QCheckBox::toggled, this,
            [this](bool on) { setRepoActionsEnabled(on); });

    m_actionsAutoApproveCheck = new QCheckBox("Automatically approve runs");
    m_actionsAutoApproveCheck->setToolTip(
        "Start workflows without waiting for an approval click. Approval still "
        "covers the exact pushed repository snapshot.");
    connect(m_actionsAutoApproveCheck, &QCheckBox::toggled, this,
            [this](bool on) { setRepoActionsAutoApprove(on); });

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
    wfLayout->addWidget(m_actionsAutoApproveCheck);
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
    makeColumnsResizable(m_actionsTable); // spreadsheet-style draggable columns (#263)
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

    m_actionFixButton = new QPushButton("Action detail");
    m_actionFixButton->setObjectName("ghostButton");
    m_actionFixButton->setProperty("buttonSize", "sm");
    m_actionFixButton->setCursor(Qt::PointingHandCursor);
    m_actionFixButton->setToolTip("Open the action run detail");
    setOcticon(m_actionFixButton, "rocket", 16);
    m_actionFixButton->hide();
    connect(m_actionFixButton, &QPushButton::clicked, this, [this] {
        if (const ActionRun *run = findRun(m_selectedRunId))
            openActionRunFromNotification(run->id);
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
    refreshWorkflowNodeCombo(); // never show the dropdown empty
    return page;
}

// ---- Settings: variables / secrets ----------------------------------------

void MainWindow::reloadVariablesList()
{
    if (!m_varsListLayout)
        return;
    const QMap<QString, QString> vars = ActionStore::variables();

    while (QLayoutItem *item = m_varsListLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
        const QString name = it.key();
        const QString value = it.value();

        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("variableCard"));
        card->setFrameShape(QFrame::StyledPanel);
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(8);

        auto *nameLabel = new QLabel(name);
        nameLabel->setObjectName(QStringLiteral("variableName"));
        nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        cardLayout->addWidget(nameLabel);

        auto *valueEdit = new QLineEdit(value);
        valueEdit->setReadOnly(true);
        valueEdit->setEchoMode(m_varsRevealed ? QLineEdit::Normal
                                              : QLineEdit::Password);
        valueEdit->setToolTip(m_varsRevealed
                                  ? QStringLiteral("Use Copy to copy this value")
                                  : QStringLiteral("Reveal values to view or copy them"));
        cardLayout->addWidget(valueEdit, 1);

        auto *editButton = new QPushButton(QStringLiteral("Edit\xE2\x80\xA6"));
        editButton->setObjectName(QStringLiteral("ghostButton"));
        editButton->setCursor(Qt::PointingHandCursor);
        connect(editButton, &QPushButton::clicked, this,
                [this, name] { addOrEditVariable(name); });
        cardLayout->addWidget(editButton);
        auto *copyButton = new QPushButton(QStringLiteral("Copy"));
        copyButton->setObjectName(QStringLiteral("ghostButton"));
        copyButton->setCursor(Qt::PointingHandCursor);
        copyButton->setEnabled(m_varsRevealed);
        copyButton->setToolTip(m_varsRevealed
                                   ? QStringLiteral("Copy this value to the clipboard")
                                   : QStringLiteral("Reveal values before copying"));
        connect(copyButton, &QPushButton::clicked, this, [this, value, name] {
            QApplication::clipboard()->setText(value);
            logSystem(QStringLiteral("Copied %1 to clipboard.").arg(name));
        });
        cardLayout->addWidget(copyButton);
        auto *deleteButton = new QPushButton(QStringLiteral("Delete"));
        deleteButton->setObjectName(QStringLiteral("ghostButton"));
        deleteButton->setCursor(Qt::PointingHandCursor);
        connect(deleteButton, &QPushButton::clicked, this,
                [this, name] { deleteVariable(name); });
        cardLayout->addWidget(deleteButton);
        m_varsListLayout->addWidget(card);
    }

    if (vars.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral(
            "No variables or secrets yet. Add one to make it available to every action run."));
        empty->setObjectName(QStringLiteral("statusLine"));
        empty->setWordWrap(true);
        m_varsListLayout->addWidget(empty);
    }
}

void MainWindow::addOrEditVariable(const QString &variableName)
{
    if (!m_varsListLayout)
        return;

    QString name, value;
    const bool editing = !variableName.isEmpty();
    if (editing) {
        name = variableName;
        value = ActionStore::variables().value(name);
    }

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
    reloadVariablesList();
}

void MainWindow::deleteVariable(const QString &name)
{
    if (name.isEmpty())
        return;
    QMap<QString, QString> vars = ActionStore::variables();
    vars.remove(name);
    ActionStore::setVariables(vars);
    reloadVariablesList();
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
    reloadVariablesList();

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
    reloadVariablesList();
}

void MainWindow::persistVariablesFromTable()
{
}
