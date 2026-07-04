// MainWindowActions: MainWindow feature methods, split out of MainWindow.cpp.
// Actions (CI on push), notifications, and variables/secrets settings.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"

using namespace forkmesh::ui;

// ---- Actions (CI on push to the mirror) -----------------------------------

namespace {

} // namespace

void MainWindow::initActions()
{
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/actions");
    m_actionStore = new ActionStore(root);
    m_actionRunner = new ActionRunner(m_actionStore, this);
    connect(m_actionRunner, &ActionRunner::logLine, this, &MainWindow::onRunLog);
    connect(m_actionRunner, &ActionRunner::statusChanged, this,
            &MainWindow::onRunStatusChanged);
    connect(m_actionRunner, &ActionRunner::finished, this,
            &MainWindow::onRunFinished);
    connect(m_actionRunner, &ActionRunner::releaseMetadataLanded, this,
            &MainWindow::onReleaseMetadataLanded);

    m_actionRuns = m_actionStore->loadAllRuns();
    // A run still marked Running was interrupted by a previous shutdown; it can't
    // resume, so record it as failed. Re-queue anything that was only queued.
    for (int i = 0; i < m_actionRuns.size(); ++i) {
        ActionRun &run = m_actionRuns[i];
        if (run.status == ActionStatus::Running) {
            run.status = ActionStatus::Failed;
            m_actionStore->saveRun(run);
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
    if (repo.previewOnly)
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
}

void MainWindow::removePushHook(const RepositoryRecord &repo) const
{
    if (repo.mirrorPath.isEmpty())
        return;
    QFile::remove(repo.mirrorPath + QStringLiteral("/hooks/post-receive"));
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
    QDir dir(m_actionStore->spoolDir());
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
    const RepositoryRecord &repo = m_repositories.at(repoIndex);

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
            return p.startsWith(QLatin1String("issues/")) ||
                   p.startsWith(QLatin1String("pulls/")) ||
                   p.startsWith(QLatin1String("commits/"));
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
        // A release the owner explicitly drafted from the "Draft a release →
        // Publish release" dialog is a deliberate, already-authorized action on
        // their own repo, so its build/publish workflow runs without a separate
        // approval step. Otherwise the tag is created but the artifact never
        // gets built: the run sits silently in AwaitingApproval whenever
        // release.yml's content differs from the last-approved copy (e.g. after
        // the workflow itself is edited), so the published binary keeps lagging
        // the latest tag. Only promptNewRelease passes the Release trigger —
        // pushed/PR workflows still go through approval.
        const bool approved =
            trigger == WorkflowTrigger::Release ||
            ActionStore::isApproved(run.repoKey(), path, content);
        run.status =
            approved ? ActionStatus::Queued : ActionStatus::AwaitingApproval;

        const ActionRun created = m_actionStore->createRun(run);
        m_actionRuns.prepend(created);
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
    loadAboutSidebar();
    // loadCommits() above already refreshed the Insights counts if that tab is on
    // screen; off-screen it reloads when next opened, so no extra pass here.
    updateRepoDetailStatus();
    updateRepoActionMenus();
    updateRepoCodeSize();
    updateRepoPushButton();
    refreshRepoPinBanner(); // a sync may have advanced refs past the pinned hash
    m_treeLoadedForIndex = -1; // force the explorer tree to rebuild on next use
    loadRepoOverview(m_overviewPath);
    // Rebuild the Mirror nodes view (cheap, roster-based) so its tab count badge
    // stays current even when that tab isn't the one on screen.
    loadMirrorNodesPanel();
}

void MainWindow::processActionQueue()
{
    if (!m_actionRunner || m_actionRunner->busy())
        return;
    while (!m_actionQueue.isEmpty()) {
        const int runId = m_actionQueue.takeFirst();
        ActionRun *run = findRun(runId);
        if (!run || run->status != ActionStatus::Queued)
            continue;
        const int repoIndex = repoIndexFor(run->owner, run->name);
        if (repoIndex < 0) {
            run->status = ActionStatus::Failed;
            m_actionStore->saveRun(*run);
            continue;
        }
        const QString mirror = m_repositories.at(repoIndex).mirrorPath;
        const QString workTree = m_repositories.at(repoIndex).localPath;
        const ActionWorkflow wf =
            ActionFile::parse(run->workflowPath, run->workflowContent);
        if (!wf.valid) {
            run->status = ActionStatus::Failed;
            m_actionStore->saveRun(*run);
            continue;
        }
        // start() emits statusChanged synchronously (which reloads m_actionRuns),
        // so copy the run out first and don't touch the pointer afterwards.
        const ActionRun snapshot = *run;
        m_actionRunner->start(snapshot, wf, mirror, workTree,
                              ActionStore::variables());
        return; // one run at a time; finished() drives the next
    }
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
            if (m_actionRunner && m_actionRunner->currentRunId() == run.id) {
                logSystem(QStringLiteral(
                              "Actions: aborting \"%1\" for %2/%3 @ %4 \xE2\x80\x94 "
                              "superseded by a newer run of the same workflow.")
                              .arg(run.workflowName, run.owner, run.name,
                                   run.commit.left(8)));
                m_actionRunner->stop(); // records Cancelled once torn down
            }
        } else if (run.status == ActionStatus::Queued ||
                   run.status == ActionStatus::AwaitingApproval) {
            const bool wasPending = run.status == ActionStatus::AwaitingApproval;
            m_actionQueue.removeAll(run.id);
            if (ActionRun *live = findRun(runId)) {
                live->status = ActionStatus::Cancelled;
                live->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
                m_actionStore->saveRun(*live);
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
    if (runId != m_selectedRunId || !m_actionLog)
        return;
    m_actionLog->moveCursor(QTextCursor::End);
    m_actionLog->insertPlainText(text);
    m_actionLog->moveCursor(QTextCursor::End);
}

void MainWindow::onRunStatusChanged(int runId, const QString &status)
{
    m_actionRuns = m_actionStore->loadAllRuns();
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
}

void MainWindow::onRunFinished(int runId, bool ok)
{
    m_actionRuns = m_actionStore->loadAllRuns();
    refreshActionsTable();
    refreshCommitStatusGlyphs();
    updateNotificationButton();
    if (const ActionRun *run = findRun(runId)) {
        const bool cancelled = run->status == ActionStatus::Cancelled;
        const QString title = ok ? QStringLiteral("Action succeeded")
                                 : cancelled ? QStringLiteral("Action stopped")
                                             : QStringLiteral("Action failed");
        notifyActionEvent(title,
                          QString::fromUtf8("%1 \xC2\xB7 %2/%3")
                              .arg(run->workflowName, run->owner, run->name),
                          !ok && !cancelled);
    }
    if (runId == m_selectedRunId)
        showRun(runId); // finished: reload the complete log from disk
    refreshOpenPullChecks();
    processActionQueue();
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
    // runner committed the tiny releases/ manifest into the working copy. Publish
    // it so the served mirror carries the metadata (install.sh reads it over the
    // git proxy) and the catalog reflects the new commit.
    logSystem(QStringLiteral(
                  "Release: published artifact metadata for %1/%2 to the mirror.")
                  .arg(run->owner, run->name));
    publishRepository(index, /*showDialogOnError=*/false);
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
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (pr.number != m_currentPullNumber)
            continue;
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
    m_notificationButton->setText(pending > 0
                                      ? QStringLiteral("Notifications •")
                                      : QStringLiteral("Notifications"));
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
        auto *whenItem = new QTableWidgetItem(when);
        if (run.createdAtMs > 0)
            whenItem->setToolTip(QDateTime::fromMSecsSinceEpoch(run.createdAtMs)
                                     .toString(QStringLiteral("MMM d  hh:mm")));

        m_actionsTable->setItem(row, 0, wfItem);
        m_actionsTable->setItem(row, 1, statusItem);
        m_actionsTable->setItem(row, 2, whenItem);
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

void MainWindow::refreshCommitStatusGlyphs()
{
    if (!m_repoDetailStack)
        return;
    switch (m_repoDetailStack->currentIndex()) {
    case 0: // Code overview: refresh the latest-commit strip
        // The check-status glyph can change while HEAD stays put, so bypass the
        // unchanged-overview cache and force the strip to re-render.
        m_overviewLoadedKey.clear();
        loadRepoOverview(m_overviewPath);
        break;
    case 1: // Commits list
        loadCommits();
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
    QWidget *tabBar = m_repoActionsTab->parentWidget();
    QWidget *page = tabBar ? tabBar->parentWidget() : nullptr;
    if (!page)
        return;
    // Parented to the repo-detail page so the bars can float over the meta band
    // just above the Actions tab without being clipped to the tab button.
    m_actionStrip = new QWidget(page);
    m_actionStrip->setObjectName("actionStrip");
    // The strip and its rows must stay hit-testable: Qt skips a
    // WA_TransparentForMouseEvents widget *and its whole subtree* when picking a
    // click receiver, which would swallow the clicks the per-run boxes rely on
    // (see updateActionStrip). The strip paints nothing of its own, so an opaque
    // overlay here looks identical while letting the bars act as live links.
    auto *col = new QVBoxLayout(m_actionStrip);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    m_actionStripCol = col;
    m_actionStrip->hide();
}

void MainWindow::updateActionStrip()
{
    ensureActionStrip();
    if (!m_actionStrip || !m_actionStripCol)
        return;

    // This repo's runs that are actually executing (queued ones haven't started
    // the clock yet, so they don't get a growing bar).
    QList<const ActionRun *> live;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const QString owner = m_repositories.at(m_repoDetailIndex).owner;
        const QString name = m_repositories.at(m_repoDetailIndex).name;
        for (const ActionRun &run : m_actionRuns)
            if (run.owner == owner && run.name == name &&
                run.status == ActionStatus::Running)
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

    // Rebuild the bars only when the set of running runs changes, so an existing
    // bar keeps growing smoothly instead of snapping back to its base each tick.
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
            // Each run is one row: a bordered name box that grows to the right,
            // with its elapsed time sitting just outside the box on the right.
            auto *row = new QWidget;
            auto *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(8);

            auto *box = new QLabel(r->workflowName.trimmed().isEmpty()
                                       ? QStringLiteral("workflow")
                                       : r->workflowName.trimmed());
            box->setObjectName("actionStripBox");
            // When the box should have started growing from. startedAtMs is set
            // once the runner picks the run up; fall back to createdAtMs.
            box->setProperty("startedAtMs",
                             static_cast<qlonglong>(r->startedAtMs > 0
                                                        ? r->startedAtMs
                                                        : r->createdAtMs));
            box->setFixedHeight(22);
            box->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            box->setStyleSheet(
                "#actionStripBox{color:#000;background:transparent;"
                "border:1px solid #000;border-radius:4px;padding:0 9px;"
                "font-size:12px;font-weight:600;}");

            auto *time = new QLabel;
            time->setObjectName("actionStripTime");
            time->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            time->setStyleSheet("#actionStripTime{color:#000;background:transparent;"
                                "font-size:11px;}");

            // The box and its timer are live links: clicking either jumps to
            // this run's output. They tag themselves with the run id and let
            // MainWindow's event filter handle the click.
            for (QLabel *hit : {box, time}) {
                hit->setProperty("actionRunId", r->id);
                hit->setCursor(Qt::PointingHandCursor);
                hit->setToolTip(QStringLiteral("View this run's live output"));
                hit->installEventFilter(this);
            }

            h->addWidget(box);
            h->addWidget(time);
            h->addStretch();
            m_actionStripCol->addWidget(row);
        }
    }

    positionActionStrip();
    m_actionStrip->show();
    m_actionStrip->raise();

    // A modest tick both grows the bars and keeps the strip pinned above the tab
    // as the window moves or the tab bar reflows.
    if (!m_actionStripTimer) {
        m_actionStripTimer = new QTimer(this);
        connect(m_actionStripTimer, &QTimer::timeout, this,
                &MainWindow::positionActionStrip);
    }
    if (!m_actionStripTimer->isActive())
        m_actionStripTimer->start(250);
}

void MainWindow::positionActionStrip()
{
    if (!m_actionStrip || !m_actionStripCol || !m_repoActionsTab)
        return;
    QWidget *page = m_actionStrip->parentWidget();
    if (!page)
        return;

    // mm:ss, rolling over to h:mm:ss past the hour.
    auto fmtElapsed = [](qint64 secs) {
        const qint64 m = secs / 60, s = secs % 60;
        if (m >= 60)
            return QStringLiteral("%1:%2:%3")
                .arg(m / 60)
                .arg(m % 60, 2, 10, QLatin1Char('0'))
                .arg(s, 2, 10, QLatin1Char('0'));
        return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
    };

    // Each box's width tracks how long its run has been going: a couple of pixels
    // per elapsed second on top of a base that always fits the workflow name. The
    // elapsed time rides just outside the box on the right.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int rows = m_actionStripCol->count();
    int widest = 0, h = 0;
    for (int i = 0; i < rows; ++i) {
        auto *row = m_actionStripCol->itemAt(i)->widget();
        if (!row)
            continue;
        auto *box = row->findChild<QLabel *>(QStringLiteral("actionStripBox"));
        auto *time = row->findChild<QLabel *>(QStringLiteral("actionStripTime"));
        if (!box || !time)
            continue;
        const qint64 started = box->property("startedAtMs").toLongLong();
        const qint64 elapsedS =
            started > 0 ? qMax<qint64>(0, (now - started) / 1000) : 0;
        // Width that always fits the name: the text advance plus the box chrome
        // (9px QSS padding + 1px border on each side = 20px) and a few extra
        // pixels of slack so bold glyphs — which fontMetrics tends to slightly
        // under-measure — don't get clipped at the edges.
        const int base = box->fontMetrics().horizontalAdvance(box->text()) + 28;
        const int boxW = qBound(base, base + static_cast<int>(elapsedS) * 2, 380);
        box->setFixedWidth(boxW);
        time->setText(fmtElapsed(elapsedS));
        row->setFixedHeight(box->height());
        const int rowW = boxW + m_actionStripCol->spacing() +
                         time->sizeHint().width() + 8;
        widest = qMax(widest, rowW);
        h += box->height() + (i > 0 ? m_actionStripCol->spacing() : 0);
    }
    if (widest <= 0)
        return;

    m_actionStrip->resize(widest, h);

    const QPoint tl = m_repoActionsTab->mapTo(page, QPoint(0, 0));
    int x = tl.x();
    int y = tl.y() - h - 1;
    if (y < 0)
        y = 0;
    if (x + widest > page->width())
        x = qMax(0, page->width() - widest);
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
    QWidget *tabBar = m_repoCodeTab->parentWidget();
    QWidget *page = tabBar ? tabBar->parentWidget() : nullptr;
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
    QWidget *tabBar = m_repoIssuesTab->parentWidget();
    QWidget *page = tabBar ? tabBar->parentWidget() : nullptr;
    if (!page)
        return;
    if (m_looperToggle->parentWidget() != page)
        m_looperToggle->setParent(page); // hides it; shown again just below
    const int w = m_looperToggle->sizeHint().width();
    const int h = m_looperToggle->sizeHint().height();
    const QPoint tl = m_repoIssuesTab->mapTo(page, QPoint(0, 0));
    int x = tl.x();
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
    QWidget *tabBar = m_repoMirrorsTab->parentWidget();
    QWidget *page = tabBar ? tabBar->parentWidget() : nullptr;
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
    QWidget *tabBar = m_repoReleasesTab->parentWidget();
    QWidget *page = tabBar ? tabBar->parentWidget() : nullptr;
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
    const bool approved = ActionStore::isApproved(run.repoKey(), path, content);
    run.status = approved ? ActionStatus::Queued : ActionStatus::AwaitingApproval;

    const ActionRun created = m_actionStore->createRun(run);
    m_actionRuns.prepend(created);
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
        m_actionLog->setPlainText(m_actionStore->readLog(*run));
        m_actionLog->moveCursor(QTextCursor::End);
    }
}

void MainWindow::approveSelectedRun()
{
    ActionRun *run = findRun(m_selectedRunId);
    if (!run || run->status != ActionStatus::AwaitingApproval)
        return;
    ActionStore::approve(run->repoKey(), run->workflowPath, run->workflowContent);
    run->status = ActionStatus::Queued;
    m_actionStore->saveRun(*run);
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
    const bool approved =
        ActionStore::isApproved(run.repoKey(), run.workflowPath, run.workflowContent);
    run.status = approved ? ActionStatus::Queued : ActionStatus::AwaitingApproval;

    const ActionRun created = m_actionStore->createRun(run);
    m_actionRuns.prepend(created);
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
        if (m_actionRunner && m_actionRunner->currentRunId() == run->id) {
            logSystem(QStringLiteral("Actions: stopping \"%1\" for %2/%3.")
                          .arg(run->workflowName, run->owner, run->name));
            m_actionRunner->stop();
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
        logSystem(QStringLiteral("Actions: cancelled queued \"%1\" for %2/%3.")
                      .arg(run->workflowName, run->owner, run->name));
        m_actionRuns = m_actionStore->loadAllRuns();
        refreshActionsTable();
        showRun(m_selectedRunId);
        updateNotificationButton();
    }
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

    m_actionsTable = new QTableWidget(0, 3);
    installColumnHeaderMenu(m_actionsTable); // 3-dots per-column menu (issue #318)
    m_actionsTable->setObjectName("issueTable");
    m_actionsTable->setHorizontalHeaderLabels({"Workflow", "Status", "When"});
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
    m_actionApprovalBanner->setWordWrap(true);
    m_actionApprovalBanner->setStyleSheet(
        "color:#d29922; background:#1c1908; border:1px solid #3a3416; "
        "border-radius:6px; padding:8px;");
    m_actionApprovalBanner->hide();

    m_actionDiff = new QTextEdit;
    m_actionDiff->setReadOnly(true);
    m_actionDiff->setLineWrapMode(QTextEdit::NoWrap);
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
    refreshClaudeModelCombo();
    connect(m_actionFixAgentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (m_actionFixAgentCombo && m_actionFixModelCombo) {
                    fillAgentFixModelCombo(
                        m_actionFixModelCombo,
                        m_actionFixAgentCombo->currentData().toString());
                    refreshClaudeModelCombo();
                }
            });

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->addWidget(m_actionRunTitle);
    titleRow->addStretch();
    titleRow->addWidget(m_actionStopButton);
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

