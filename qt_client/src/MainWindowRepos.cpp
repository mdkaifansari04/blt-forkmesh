// MainWindowRepos: MainWindow feature methods, split out of MainWindow.cpp.
// Repositories list and repo settings.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"

using namespace forkmesh::ui;

// ------------------------------------------------------------- repositories

QString MainWindow::repositoryMirrorRoot() const
{
    const QString configured =
        QSettings().value(kMirrorRootSetting).toString().trimmed();
    if (!configured.isEmpty())
        return configured;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/mirrors";
}

QString MainWindow::repositoryPreviewRoot() const
{
    const QString configured =
        QSettings().value(kPreviewCacheRootSetting).toString().trimmed();
    if (!configured.isEmpty())
        return configured;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/repo-preview-cache";
}

QString MainWindow::repositoryPreviewPath(const QString &owner,
                                          const QString &name) const
{
    return repositoryPreviewRoot() + "/" +
           repoSegment(owner, QStringLiteral("owner")) + "-" +
           repoSegment(name, QStringLiteral("repository")) + ".git";
}

QString MainWindow::repositoryNetworkCloneUrl(const QString &owner,
                                              const QString &name) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/" + repoSegment(owner, QStringLiteral("owner")) + "/" +
                repoSegment(name, QStringLiteral("repository")));
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString();
}

void MainWindow::changeMirrorLocation()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Choose where to store mirrored repositories",
        repositoryMirrorRoot());
    if (chosen.isEmpty())
        return;
    QSettings().setValue(kMirrorRootSetting, chosen);
    if (m_mirrorRootEdit)
        m_mirrorRootEdit->setText(chosen);
    logSystem("Mirror storage folder set to " + chosen +
              " (applies to newly added repositories).");
    QMessageBox::information(
        this, "Mirror storage",
        "New mirrors will be stored in:\n" + chosen +
            "\n\nExisting mirrors stay where they are. A local fork will push "
            "into its repository's mirror here.");
}

void MainWindow::changePreviewCacheLocation()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Choose where to cache repository previews",
        repositoryPreviewRoot());
    if (chosen.isEmpty())
        return;
    QSettings().setValue(kPreviewCacheRootSetting, chosen);
    if (m_previewCacheRootEdit)
        m_previewCacheRootEdit->setText(chosen);
    logSystem("Preview cache folder set to " + chosen + ".");
    QMessageBox::information(
        this, "Preview cache",
        "Temporary repository previews will be stored in:\n" + chosen +
            "\n\nExisting preview caches stay where they are.");
}

QString MainWindow::repositoryChannel(const RepositoryRecord &repo) const
{
    // Repo activity is routed to the single shared #general channel rather than a
    // per-repo room, so the network doesn't fragment into a room per node/repo.
    Q_UNUSED(repo);
    return QStringLiteral("#general");
}

QString MainWindow::repositorySource(const RepositoryRecord &repo) const
{
    return repo.localPath.trimmed().isEmpty() ? repo.cloneUrl.trimmed()
                                             : repo.localPath.trimmed();
}

QStringList MainWindow::viewAuthGitArgs(const RepositoryRecord &repo,
                                        const QString &source) const
{
    if (!repo.isPrivate)
        return {};
    const QUrl src(source);
    // Only ever attach the token to mainnode requests, and only for https/http
    // (a local working-copy path has no host and needs no token).
    if (!src.isValid() || src.host().isEmpty() ||
        src.host().compare(catalogApiUrl().host(), Qt::CaseInsensitive) != 0)
        return {};
    // Sign the exact owner/name the relay parses from this same URL path so the
    // canonical string matches on both sides.
    const QStringList segs =
        src.path().split('/', Qt::SkipEmptyParts);
    if (segs.size() < 2)
        return {};
    const QString owner = segs.at(segs.size() - 2);
    const QString name = segs.at(segs.size() - 1);
    if (!m_profileIdentity.isValid())
        return {};
    const QString viewer = accountOwner();
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    // Two read paths (issue #9): when this node owns the repo it signs the owner
    // view token (Basic username = owner); when it's a collaborator the repo was
    // shared with, it signs the share-view token with its OWN key (Basic username
    // = this node's account) so the relay verifies against the grantee's key and
    // checks the share ACL. The username tells the relay which path to take.
    const bool asOwner = viewer.isEmpty() || viewer == owner;
    const QString user = asOwner ? owner : viewer;
    const QByteArray canonical =
        asOwner
            ? ("forkmesh-view-v1\n" + owner + "\n" + name + "\n" + ts).toUtf8()
            : ("forkmesh-share-view-v1\n" + viewer + "\n" + owner + "\n" + name +
               "\n" + ts).toUtf8();
    const QString password = ts + "." + m_profileIdentity.signData(canonical);
    const QByteArray basic =
        (user + ":" + password).toUtf8().toBase64();
    return {QStringLiteral("-c"),
            QStringLiteral("http.extraHeader=Authorization: Basic ") +
                QString::fromLatin1(basic)};
}

void MainWindow::loadRepositories()
{
    m_repositories.clear();
    QSettings settings;
    const int count = settings.beginReadArray(kRepositoriesArray);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        RepositoryRecord repo;
        repo.owner = settings.value("owner").toString();
        repo.name = settings.value("name").toString();
        repo.description = settings.value("description").toString();
        repo.cloneUrl = settings.value("cloneUrl").toString();
        repo.localPath = settings.value("localPath").toString();
        repo.solanaAddress = settings.value("solanaAddress").toString();
        repo.mirrorPath = settings.value("mirrorPath").toString();
        repo.publishToNetwork = settings.value("publishToNetwork").toBool();
        repo.isPrivate = settings.value("isPrivate").toBool();
        // Default off for mirrored repos (owner isn't this node); on for repos
        // this node owns. Explicitly stored values always win.
        repo.actionsEnabled =
            settings.value("actionsEnabled", repo.owner == accountOwner())
                .toBool();
        repo.secretScanningEnabled =
            settings.value("secretScanningEnabled", true).toBool();
        repo.disabledWorkflows = settings.value("disabledWorkflows").toStringList();
        repo.hostedSinceMs = settings.value("hostedSinceMs").toLongLong();
        repo.lastSyncMs = settings.value("lastSyncMs").toLongLong();
        repo.publishedAtMs = settings.value("publishedAtMs").toLongLong();
        if (!repo.name.isEmpty() && !repositorySource(repo).isEmpty())
            m_repositories.append(repo);
    }
    settings.endArray();
    loadRepoStats();
}

void MainWindow::saveRepositories() const
{
    QSettings settings;
    const int permanentCount =
        int(std::count_if(m_repositories.cbegin(), m_repositories.cend(),
                          [](const RepositoryRecord &repo) {
                              return !repo.previewOnly;
                          }));
    settings.beginWriteArray(kRepositoriesArray, permanentCount);
    int saved = 0;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.previewOnly)
            continue;
        settings.setArrayIndex(saved++);
        settings.setValue("owner", repo.owner);
        settings.setValue("name", repo.name);
        settings.setValue("description", repo.description);
        settings.setValue("cloneUrl", repo.cloneUrl);
        settings.setValue("localPath", repo.localPath);
        settings.setValue("solanaAddress", repo.solanaAddress);
        settings.setValue("mirrorPath", repo.mirrorPath);
        settings.setValue("publishToNetwork", repo.publishToNetwork);
        settings.setValue("isPrivate", repo.isPrivate);
        settings.setValue("actionsEnabled", repo.actionsEnabled);
        settings.setValue("secretScanningEnabled", repo.secretScanningEnabled);
        settings.setValue("disabledWorkflows", repo.disabledWorkflows);
        settings.setValue("hostedSinceMs", repo.hostedSinceMs);
        settings.setValue("lastSyncMs", repo.lastSyncMs);
        settings.setValue("publishedAtMs", repo.publishedAtMs);
    }
    settings.endArray();
}

QStringList MainWindow::mentionCandidateNames() const
{
    QSet<QString> seen;
    QStringList names;
    auto add = [&seen, &names](const QString &raw) {
        const QString n = raw.trimmed();
        if (n.isEmpty() || seen.contains(n.toLower()))
            return;
        seen.insert(n.toLower());
        names.append(n);
    };

    // Every node the relay knows about: connected, discovered, and any that
    // advertise mirroring/sharing a repo. This is the bulk of the list and is
    // already in memory, so building it is cheap.
    for (const MemberInfo &m : m_homeRoster)
        add(m.name);

    // Contributors to the issues/PRs currently loaded for this repo, so authors
    // who opened/commented but aren't online right now are still suggestible.
    for (const Issue &iss : m_currentIssues) {
        add(iss.authorName);
        for (const IssueEvent &ev : iss.events)
            add(ev.authorName);
    }
    for (const PullRequest &pr : m_currentPulls) {
        add(pr.authorName);
        for (const PullEvent &ev : pr.events)
            add(ev.authorName);
    }

    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });
    return names;
}

void MainWindow::refreshRepositoryList()
{
    // Re-entrancy guard (adhoc #247): the periodic m_homeStatsTimer fires this once
    // a minute, which can land inside another heavy refresh's GitKeepAlive pump.
    // Running the per-repo git reads (mirror head/commit/size) plus
    // updateRepoPushButton nested in that pump stacks synchronous git work and
    // stalls the GUI. Coalesce + defer to a fresh event-loop turn instead; the
    // deferred call re-checks the guard and re-arms if the pump is still active.
    if (m_heavyRefreshInFlight) {
        if (!m_repoListRefreshQueued) {
            m_repoListRefreshQueued = true;
            QTimer::singleShot(250, this, [this] {
                m_repoListRefreshQueued = false;
                refreshRepositoryList();
            });
        }
        return;
    }
    const ScopedFlag refreshGuard(m_heavyRefreshInFlight);

    m_repoMenuEntries.clear();
    m_nodeMenuEntries.clear();

    // Repos grouped by node (owner).
    QHash<QString, QList<int>> reposByNode;
    for (int i = 0; i < m_repositories.size(); ++i)
        reposByNode[m_repositories.at(i).owner].append(i);

    // Node order: connected nodes first (the old leaderboard ranking — you, then
    // online, then by name), then any repo owners that aren't connected. Each
    // node is shown with its repos nested underneath.
    struct NodeInfo {
        bool inRoster = false;
        bool online = false;
        bool self = false;
        QString solana;
        QString balance;
        QString platform;
        QStringList mirrors;
    };
    QHash<QString, NodeInfo> nodes;
    QStringList nodeOrder;
    QList<MemberInfo> ranked = m_homeRoster;
    std::sort(ranked.begin(), ranked.end(),
              [](const MemberInfo &a, const MemberInfo &b) {
                  if (a.self != b.self)
                      return a.self;
                  if (a.online != b.online)
                      return a.online;
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    for (const MemberInfo &m : std::as_const(ranked)) {
        if (m.name.isEmpty())
            continue;
        if (!nodes.contains(m.name)) {
            NodeInfo ni;
            ni.inRoster = true;
            ni.online = m.online;
            ni.self = m.self;
            ni.solana = m.solanaAddress.trimmed();
            if (ni.self && ni.solana.isEmpty())
                ni.solana = savedSolanaAddress();
            ni.balance = m.solanaBalance.trimmed();
            ni.platform = m.platform;
            ni.mirrors = m.mirrors;
            nodes.insert(m.name, ni);
            nodeOrder.append(m.name);
        } else {
            NodeInfo &ni = nodes[m.name];
            if (m.online)
                ni.online = true;
            if (ni.platform.isEmpty())
                ni.platform = m.platform;
            if (!m.mirrors.isEmpty())
                ni.mirrors = m.mirrors;
        }
    }
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        const QString owner = repo.owner;
        if (!nodes.contains(owner)) {
            if (repo.previewOnly)
                continue;
            nodes.insert(owner, NodeInfo());
            nodeOrder.append(owner);
        }
    }

    // Repos already shown locally by "owner/name", so advertised mirrors are
    // not duplicated.
    QSet<QString> shownLocalRepoKeys;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        const QString key = repo.owner + "/" + repo.name;
        shownLocalRepoKeys.insert(key);
    }
    QSet<QString> shownAdvertised; // dedupe a repo advertised by several nodes

    // --- Nodes dropdown: one entry per node for the top-bar node switcher. The
    // OS badge (Linux/Windows/mac) signals online (tinted) vs offline (grey).
    bool selectedStillExists = false;
    for (const QString &node : std::as_const(nodeOrder)) {
        const NodeInfo info = nodes.value(node);
        NodeMenuEntry entry;
        entry.name = node;
        entry.platform = info.platform;
        entry.online = info.inRoster && info.online;
        entry.self = info.self;
        entry.repoCount = reposByNode.value(node).size();
        m_nodeMenuEntries.append(entry);
        if (node == m_selectedNode)
            selectedStillExists = true;
    }

    // Default the selection to the first node (the ranking puts you first) when
    // nothing is selected yet or the previously-selected node went away.
    if (!selectedStillExists)
        m_selectedNode = nodeOrder.isEmpty() ? QString() : nodeOrder.first();
    updateNodeSwitcher();

    // --- Repositories dropdown: the repos owned by the selected node, plus any
    // repos that node advertises mirroring that we don't already have.
    const NodeInfo selInfo = nodes.value(m_selectedNode);
    for (int i : reposByNode.value(m_selectedNode)) {
        const RepositoryRecord &repo = m_repositories.at(i);
        const bool online = repo.publishedAtMs > 0;
        QString label = repo.name;
        if (repo.previewOnly)
            label += "  \xC2\xB7 preview";
        else if (!repo.publishToNetwork)
            label += "  \xC2\xB7 local only";
        else if (repo.isPrivate && !accountOwner().isEmpty() &&
                 repo.owner != accountOwner())
            // A private repo we don't own can only be here because its owner
            // shared it with us (issue #9).
            label += "  \xC2\xB7 shared";
        else
            label += repo.isPrivate ? "  \xC2\xB7 private"
                                    : "  \xC2\xB7 public";
        if (m_syncingRepos.contains(i))
            label += repo.previewOnly ? "  \xC2\xB7 caching" : "  \xC2\xB7 syncing";
        RepoMenuEntry entry;
        entry.label = label;
        entry.index = i;
        // A repo glyph: green when published+online on the web, grey otherwise.
        entry.icon = themedOcticon(
            repo.previewOnly ? QStringLiteral("cloud") : QStringLiteral("repo"),
            repo.previewOnly ? QColor("#58a6ff")
                             : (repo.publishToNetwork && online ? QColor("#2ea043")
                                                                : QColor("#6e7681")),
            14);
        m_repoMenuEntries.append(entry);
    }
    for (const QString &ownerName : selInfo.mirrors) {
        if (shownLocalRepoKeys.contains(ownerName) ||
            shownAdvertised.contains(ownerName))
            continue;
        shownAdvertised.insert(ownerName);
        RepoMenuEntry entry;
        entry.label = QString::fromUtf8("\xE2\x86\x93 ") + ownerName +
                      QString::fromUtf8("   \xC2\xB7 browse");
        entry.index = -2; // advertised mirror marker
        entry.advertised = ownerName;
        entry.icon = themedOcticon("cloud", QColor("#58a6ff"), 14);
        m_repoMenuEntries.append(entry);
    }
    updateRepoSwitcher();
    updateRepoPushButton();
    updateRepoDetailStatus();
    updateRepoActionMenus();
    updateHomeStats();

    // Auto-select the selected node's first repository when nothing is open yet
    // (e.g. on a fresh install where repos arrive asynchronously) so the user
    // lands on real content instead of an empty panel. A node switch runs its own
    // first-repo open, so skip while one is in flight. Held off until deferred
    // startup so it never races the last-open-repository restore. When the node
    // has no repos at all, clear the panel — updateRepoSwitcher hides the section.
    int firstRepo = -1;
    for (const RepoMenuEntry &entry : std::as_const(m_repoMenuEntries))
        if (entry.index >= 0) {
            firstRepo = entry.index;
            break;
        }
    if (m_deferredStartupRun && m_pendingRestoreRepoIndex < 0 &&
        !m_nodeSwitching && !m_repoDetailLoading) {
        if (m_repoDetailIndex < 0 && firstRepo >= 0)
            openRepoDetailDeferred(firstRepo);
        else if (firstRepo < 0 && m_repoDetailIndex >= 0)
            clearRepoDetail();
    }

    // Advertise our own mirrors so other nodes can see and mirror them too.
    // Advertise under the SAME owner/name the live host tunnel and catalog
    // register with (catalogOwner + canonical name), not the raw repo.owner.
    // Peers turn the advertised string straight into a clone URL, which the
    // worker routes to the DO keyed host:<owner>/<name>. If we advertised
    // repo.owner while the host socket connected as catalogOwner, the peer hit
    // a DO with no host attached and got a 503 — surfaced as "Sync deferred,
    // host temporarily unavailable" even though we were online and serving.
    if (m_backend) {
        // Building the adverts shells ~9 git subprocesses per repo (head, commit,
        // size, issue/pull/discussion/commit/branch counts, worktree count). None of
        // that changes when we merely serve a request, yet refreshRepositoryList runs
        // on every onRequestServed and a 1-minute timer, so recomputing it every time
        // blocked the GUI thread for seconds (adhoc #83). A mirror's stats only move
        // when it is re-synced (repo.lastSyncMs) and the worktree count only when a
        // worktree is added/removed (the .git/worktrees dir mtime) — so skip the whole
        // rebuild while that signature is unchanged, and when it did change run the git
        // reads under GitKeepAlive so the window keeps breathing.
        QString advertSig;
        for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
            if (repo.previewOnly)
                continue;
            advertSig +=
                repo.mirrorPath + QLatin1Char('|') +
                QString::number(repo.lastSyncMs) + QLatin1Char('|') + repo.localPath +
                QLatin1Char('|') +
                QString::number(
                    QFileInfo(repo.localPath + QStringLiteral("/.git/worktrees"))
                        .lastModified()
                        .toMSecsSinceEpoch()) +
                QLatin1Char('\n');
        }
        if (advertSig != m_mirrorAdvertSig) {
        GitKeepAlive keepAlive;
        QList<MirrorAdvert> ours;
        for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
            if (repo.previewOnly)
                continue;
            MirrorAdvert advert;
            advert.ownerName = catalogOwner(repo) + "/" +
                               repoSegment(repo.name, QStringLiteral("repository"));
            // Shared upstream identity: every node mirroring the same source repo
            // carries the same "<sourceOwner>/name", so the mirror-nodes view can
            // group them even though each advertises its own clone (catalog) owner.
            advert.source = repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
                            repoSegment(repo.name, QStringLiteral("repository"));
            // Advertise the HEAD this node currently holds so peers can see how
            // fresh our mirror is relative to theirs.
            advert.branch = mirrorHeadBranch(repo.mirrorPath);
            advert.commit = mirrorBranchCommit(repo.mirrorPath, advert.branch);
            advert.updatedMs = repo.lastSyncMs;
            // On-disk mirror size so peers can show how much data we're holding.
            advert.sizeBytes = mirrorRepoSizeBytes(repo.mirrorPath);
            // Issues we're mirroring, so peers can show the count per node.
            advert.issueCount = mirrorIssueCount(repo.mirrorPath, advert.branch);
            // More tallies the Mirror nodes view shows per node: history depth,
            // branch/PR/discussion counts, and our live worktree (agent task) count.
            advert.commitCount = mirrorCommitCount(repo.mirrorPath, advert.branch);
            advert.branchCount = mirrorBranchCount(repo.mirrorPath);
            advert.pullCount = mirrorPullCount(repo.mirrorPath, advert.branch);
            advert.discussionCount =
                mirrorDiscussionCount(repo.mirrorPath, advert.branch);
            advert.worktreeCount = mirrorWorktreeCount(repo.localPath);
            // Release artifacts we're actually hosting for download (issue #304 CAS
            // blobs), so peers can see which nodes can serve a binary.
            advert.artifactCount = mirrorArtifactCount(repo.mirrorPath);
            ours.append(advert);
        }
        m_backend->setMirroredRepos(ours);
        m_mirrorAdvertSig = advertSig;
        }
    }
}

void MainWindow::mirrorAdvertisedRepo(const QString &ownerName)
{
    const int slash = ownerName.indexOf('/');
    if (slash <= 0)
        return;
    const QString owner = ownerName.left(slash);
    const QString name = ownerName.mid(slash + 1);
    int previewIndex = -1;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &r = m_repositories.at(i);
        if (r.owner != owner || r.name != name)
            continue;
        if (r.previewOnly) {
            previewIndex = i;
            continue;
        }
        QMessageBox::information(this, "Mirror",
                                 "You already mirror this repository.");
        return;
    }
    if (previewIndex >= 0) {
        mirrorPreviewRepository(previewIndex);
        return;
    }
    const QString cloneUrl = repositoryNetworkCloneUrl(owner, name);
    if (cloneUrl.isEmpty())
        return;

    if (QMessageBox::question(
            this, "Mirror it too",
            QStringLiteral("Mirror %1 into your local mirrors?\n\nIt will be cloned "
                           "from %2.")
                .arg(ownerName, cloneUrl)) != QMessageBox::Yes)
        return;

    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.cloneUrl = cloneUrl;
    repo.publishToNetwork = true;
    // Mirrored repos start with actions off; opt in per repo on Settings.
    repo.actionsEnabled = false;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(owner, QStringLiteral("owner")) + "-" +
                      repoSegment(name, QStringLiteral("repository")) + ".git";
    m_repositories.append(repo);
    saveRepositories();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));
    refreshRepositoryList();
    logSystem("Mirroring " + ownerName + " from " + cloneUrl);
    syncRepository(m_repositories.size() - 1); // clone from the network mirror
}

void MainWindow::previewAdvertisedRepo(const QString &ownerName)
{
    const int slash = ownerName.indexOf('/');
    if (slash <= 0)
        return;
    const QString owner = ownerName.left(slash);
    const QString name = ownerName.mid(slash + 1);

    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.owner == owner && repo.name == name) {
            openRepoDetail(i);
            if (repo.previewOnly && !m_syncingRepos.contains(i) &&
                !QDir(repo.mirrorPath).exists())
                syncRepository(i);
            return;
        }
    }

    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.cloneUrl = repositoryNetworkCloneUrl(owner, name);
    repo.previewOnly = true;
    // Mirrored repos start with actions off (also applies once promoted).
    repo.actionsEnabled = false;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryPreviewPath(owner, name);
    m_repositories.append(repo);
    const int index = m_repositories.size() - 1;
    refreshRepositoryList();
    logSystem("Preview: caching " + ownerName + " from " + repo.cloneUrl);
    openRepoDetail(index);
    syncRepository(index);
}

void MainWindow::mirrorPreviewRepository(int index)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord preview = m_repositories.at(index);
    if (!preview.previewOnly)
        return;

    for (int i = 0; i < m_repositories.size(); ++i) {
        if (i == index)
            continue;
        const RepositoryRecord &repo = m_repositories.at(i);
        if (!repo.previewOnly && repo.owner == preview.owner &&
            repo.name == preview.name) {
            QMessageBox::information(
                this, "Mirror repository",
                QStringLiteral("You already mirror %1/%2.")
                    .arg(preview.owner, preview.name));
            return;
        }
    }

    const QString permanentPath = repositoryMirrorRoot() + "/" +
                                  repoSegment(preview.owner, QStringLiteral("owner")) +
                                  "-" +
                                  repoSegment(preview.name,
                                              QStringLiteral("repository")) +
                                  ".git";
    const QString source =
        (!preview.mirrorPath.isEmpty() && QDir(preview.mirrorPath).exists())
            ? preview.mirrorPath
            : preview.cloneUrl;
    if (source.isEmpty()) {
        setRepoDetailNotice("Preview is not cached yet; try again after it loads.",
                            true);
        return;
    }

    if (!QDir().mkpath(QFileInfo(permanentPath).absolutePath())) {
        QMessageBox::warning(this, "Mirror repository",
                             "Could not create " +
                                 QFileInfo(permanentPath).absolutePath());
        return;
    }

    if (QDir(permanentPath).exists()) {
        RepositoryRecord &repo = m_repositories[index];
        repo.previewOnly = false;
        repo.publishToNetwork = true;
        repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
        repo.mirrorPath = permanentPath;
        if (repo.lastSyncMs <= 0)
            repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
        saveRepositories();
        ensurePushHook(repo);
        if (m_backend)
            m_backend->addChannel(repositoryChannel(repo));
        publishRepository(index, false);
        startRepoHosts();
        refreshRepositoryList();
        openRepoDetail(index);
        setRepoDetailNotice("Mirroring " + repo.owner + "/" + repo.name + ".");
        return;
    }

    m_syncingRepos.insert(index);
    refreshRepositoryList();
    setRepoDetailNotice("Creating permanent mirror...");
    logSystem(QStringLiteral("Mirror: promoting preview %1/%2 from %3 to %4.")
                  .arg(preview.owner, preview.name, source, permanentPath));

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index, permanentPath](int exitCode,
                                                  QProcess::ExitStatus status) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                m_syncingRepos.remove(index);
                if (index < 0 || index >= m_repositories.size()) {
                    refreshRepositoryList();
                    return;
                }
                RepositoryRecord &repo = m_repositories[index];
                if (status == QProcess::NormalExit && exitCode == 0) {
                    repo.previewOnly = false;
                    repo.publishToNetwork = true;
                    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
                    repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                    repo.mirrorPath = permanentPath;
                    saveRepositories();
                    ensurePushHook(repo);
                    if (m_backend)
                        m_backend->addChannel(repositoryChannel(repo));
                    publishRepository(index, false);
                    startRepoHosts();
                    refreshRepositoryList();
                    openRepoDetail(index);
                    logSystem("Mirror: added " + repo.owner + "/" + repo.name +
                              " from preview cache.");
                    setRepoDetailNotice("Mirroring " + repo.owner + "/" +
                                        repo.name + ".");
                } else {
                    refreshRepositoryList();
                    logSystem("Mirror: could not promote preview: " +
                              errors.right(300));
                    setRepoDetailNotice(
                        "Could not create mirror" +
                            (errors.isEmpty() ? QString() :
                                                ": " + errors.right(160)),
                        true);
                }
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, index](QProcess::ProcessError) {
                process->deleteLater();
                m_syncingRepos.remove(index);
                refreshRepositoryList();
                setRepoDetailNotice("Could not run git. Install Git and try again.",
                                    true);
            });
    process->start(QStringLiteral("git"),
                   {QStringLiteral("clone"), QStringLiteral("--mirror"), source,
                    permanentPath});
}

void MainWindow::promptAddRepository()
{
    // Simple flow: pick a local Git repository. Everything else is derived.
    // The folder is mirrored locally and only signed metadata is published to
    // the website; the .git data never leaves this machine.
    const QString path = QFileDialog::getExistingDirectory(
        this, "Choose a local Git repository to mirror and publish");
    if (path.isEmpty())
        return;

    const bool looksLikeGit =
        QDir(path).exists(".git") || QDir(path).exists("HEAD");
    if (!looksLikeGit) {
        QMessageBox::warning(
            this, "Add repository",
            "That folder is not a Git repository. Choose a folder created by "
            "\"git init\" or \"git clone\".");
        return;
    }

    RepositoryRecord repo;
    repo.localPath = path;
    repo.name = repoNameFromUrl(path);
    // Repos are namespaced under the single account name.
    repo.owner = accountOwner();
    repo.solanaAddress = savedSolanaAddress();
    // Selecting a local repo publishes it to the website so it shows up online
    // and others can discover and mirror it. No public clone URL is sent.
    repo.publishToNetwork = true;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(repo.owner, QStringLiteral("owner")) + "-" +
                      repoSegment(repo.name, QStringLiteral("repository")) + ".git";

    m_repositories.append(repo);
    saveRepositories();
    refreshRepositoryList();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));

    const QJsonObject metadata{{"owner", repo.owner},
                               {"name", repo.name},
                               {"channel", repositoryChannel(repo)},
                               {"mirrorPath", repo.mirrorPath},
                               {"hostedSince", QString::number(repo.hostedSinceMs)},
                               {"maintainer", m_profileIdentity.publicKey()}};
    logSystem("Repository: signed mirror metadata for " + repo.owner + "/" +
              repo.name + " with signature " +
              m_profileIdentity.signJson(metadata).left(16) + "...");
    publishRepository(m_repositories.size() - 1, false);
    syncRepository(m_repositories.size() - 1);
}

void MainWindow::createNewRepository()
{
    // Ask for a name, then a parent folder, and `git init` a fresh empty repo
    // there. From there it's mirrored + published exactly like promptAddRepository.
    bool ok = false;
    const QString rawName =
        QInputDialog::getText(this, "New repository", "Repository name:",
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || rawName.isEmpty())
        return;

    // Keep the on-disk folder name in step with the published name: both go
    // through repoSegment so a slash or odd character can't escape the path.
    const QString name = repoSegment(rawName, QStringLiteral("repository"));
    if (repoIndexFor(accountOwner(), name) >= 0) {
        QMessageBox::warning(
            this, "New repository",
            QStringLiteral("You already have a repository named \"%1\".").arg(name));
        return;
    }

    const QString parent = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose where to create \"%1\"").arg(name),
        QDir::homePath());
    if (parent.isEmpty())
        return; // cancelled

    const QString dest = QDir(parent).filePath(name);
    if (QDir(dest).exists() && !QDir(dest).isEmpty()) {
        QMessageBox::warning(
            this, "New repository",
            QStringLiteral("%1 already exists and is not empty. Choose another "
                           "name or location.")
                .arg(dest));
        return;
    }
    if (!QDir().mkpath(dest)) {
        QMessageBox::warning(this, "New repository",
                             QStringLiteral("Could not create %1.").arg(dest));
        return;
    }

    // `git init -b main` gives the new repo a conventional default branch so the
    // first push lands on refs/heads/main like everywhere else.
    QProcess git;
    git.setWorkingDirectory(dest);
    git.start(QStringLiteral("git"),
              {QStringLiteral("init"), QStringLiteral("-b"), QStringLiteral("main")});
    git.waitForFinished(30000);
    if (git.exitStatus() != QProcess::NormalExit || git.exitCode() != 0) {
        const QString err =
            QString::fromUtf8(git.readAllStandardError()).trimmed();
        QMessageBox::warning(
            this, "New repository",
            QStringLiteral("git init failed: %1").arg(err.right(200)));
        logSystem("New repository: git init failed in " + dest + ": " +
                  err.right(300));
        return;
    }

    RepositoryRecord repo;
    repo.localPath = dest;
    repo.name = name;
    repo.owner = accountOwner();
    repo.solanaAddress = savedSolanaAddress();
    repo.publishToNetwork = true;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(repo.owner, QStringLiteral("owner")) + "-" +
                      repoSegment(repo.name, QStringLiteral("repository")) + ".git";

    m_repositories.append(repo);
    saveRepositories();
    refreshRepositoryList();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));
    const int index = m_repositories.size() - 1;
    publishRepository(index, false);
    syncRepository(index);
    logSystem("New repository: created " + repo.owner + "/" + repo.name + " in " +
              dest + ".");
    flashMessage(QStringLiteral("Created %1/%2.").arg(repo.owner, repo.name));
}

QStringList MainWindow::importAuthGitArgs(const QString &url) const
{
    const QString host = QUrl(url).host().toLower();
    QString token, user;
    if (host.contains(QLatin1String("github"))) {
        token = QSettings().value(kGithubTokenSetting).toString().trimmed();
        user = QStringLiteral("x-access-token"); // GitHub PATs auth as this user
    } else if (host.contains(QLatin1String("gitlab"))) {
        token = QSettings().value(kGitlabTokenSetting).toString().trimmed();
        user = QStringLiteral("oauth2"); // GitLab PATs auth as oauth2:<token>
    }
    if (token.isEmpty())
        return {};
    // Carry the token in a one-shot Authorization header rather than baking it
    // into the cloned repo's origin URL, so the secret is never persisted on disk.
    const QByteArray basic = (user + ":" + token).toUtf8().toBase64();
    return {QStringLiteral("-c"),
            QStringLiteral("http.extraHeader=Authorization: Basic ") +
                QString::fromLatin1(basic)};
}

void MainWindow::importRemoteRepository()
{
    if (!m_importUrlEdit || !m_importButton)
        return;
    const QString url = m_importUrlEdit->text().trimmed();
    auto setStatus = [this](const QString &text, bool error) {
        if (!m_importStatus)
            return;
        m_importStatus->setText(text);
        m_importStatus->setStyleSheet(error ? QStringLiteral("color:#f85149;")
                                            : QString());
        m_importStatus->setVisible(!text.isEmpty());
    };

    const QUrl parsed(url);
    if (url.isEmpty() || !parsed.isValid() ||
        (parsed.scheme() != QLatin1String("https") &&
         parsed.scheme() != QLatin1String("http"))) {
        setStatus("Enter an https URL to a GitHub or GitLab repository.", true);
        return;
    }

    const QString name = repoNameFromUrl(url);
    if (repoIndexFor(accountOwner(), name) >= 0) {
        setStatus(QStringLiteral("You already have a repository named \"%1\".")
                      .arg(name),
                  true);
        return;
    }

    // Clone into a folder the user picks, so the working copy lives where they
    // expect it (and can be opened in their own editor), defaulting to home.
    const QString parent = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose where to clone \"%1\"").arg(name),
        QDir::homePath());
    if (parent.isEmpty())
        return; // cancelled
    const QString dest = QDir(parent).filePath(name);
    if (QDir(dest).exists() && !QDir(dest).isEmpty()) {
        setStatus(QStringLiteral("%1 already exists and is not empty. Choose "
                                 "another location.")
                      .arg(dest),
                  true);
        return;
    }

    m_importButton->setEnabled(false);
    m_importUrlEdit->setEnabled(false);
    setStatus(QStringLiteral("Cloning %1 into %2…").arg(url, dest), false);
    logSystem("Import: cloning " + url + " into " + dest + ".");

    const QStringList args =
        importAuthGitArgs(url) + QStringList{"clone", url, dest};
    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("git"));
    process->setArguments(args);
    connect(process, &QProcess::finished, this,
            [this, process, dest, name, url, setStatus](int exitCode,
                                                        QProcess::ExitStatus) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                if (m_importButton)
                    m_importButton->setEnabled(true);
                if (m_importUrlEdit)
                    m_importUrlEdit->setEnabled(true);

                if (exitCode != 0) {
                    // The token (if any) is only ever a header, so it can't leak
                    // into this stderr; still, keep the tail short and readable.
                    setStatus(QStringLiteral("Clone failed: %1")
                                  .arg(errors.right(200)),
                              true);
                    logSystem("Import: clone failed for " + url + ": " +
                              errors.right(300));
                    return;
                }

                // Mirror & publish the freshly cloned working copy under this
                // node, exactly like adding a local repository.
                RepositoryRecord repo;
                repo.localPath = dest;
                repo.name = name;
                repo.owner = accountOwner();
                repo.solanaAddress = savedSolanaAddress();
                repo.publishToNetwork = true;
                repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
                repo.mirrorPath =
                    repositoryMirrorRoot() + "/" +
                    repoSegment(repo.owner, QStringLiteral("owner")) + "-" +
                    repoSegment(repo.name, QStringLiteral("repository")) + ".git";

                m_repositories.append(repo);
                saveRepositories();
                refreshRepositoryList();
                if (m_backend)
                    m_backend->addChannel(repositoryChannel(repo));
                const int index = m_repositories.size() - 1;
                publishRepository(index, false);
                syncRepository(index);

                setStatus(QStringLiteral("Imported %1/%2 — mirroring and "
                                         "publishing now.")
                              .arg(repo.owner, repo.name),
                          false);
                logSystem("Import: cloned " + url + " as " + repo.owner + "/" +
                          repo.name + ".");
                if (m_importUrlEdit)
                    m_importUrlEdit->clear();
            });
    process->start();
}

QString MainWindow::repositoryWebUrl(const RepositoryRecord &repo) const
{
    // Clean repository route on the public website, derived from the same host
    // that serves the catalog API. Static Assets routes this to the catalog SPA.
    // Key it by the repo's OWN owner — the node that actually hosts it (and the
    // one selected in the top bar when viewing it) — not this node's account.
    // Using catalogOwner here would point every repo at the local account and
    // open the wrong node's page for repos mirrored from other nodes.
    QUrl url = catalogApiUrl();
    url.setPath("/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")));
    url.setFragment(QString());
    return url.toString();
}

void MainWindow::updateRepoActionMenus()
{
    if (!m_mirrorMenu || !m_sourceMenu)
        return;
    m_mirrorMenu->clear();
    m_sourceMenu->clear();

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        for (QMenu *menu : {m_mirrorMenu, m_sourceMenu}) {
            QAction *empty = menu->addAction("No repository selected");
            empty->setEnabled(false);
        }
        return;
    }

    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const bool hasMirror = !repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists();
    const bool online = !repo.previewOnly && repo.publishedAtMs > 0;
    const QString webUrl = repositoryWebUrl(repo);

    // Mirror owns network availability, public browse URL, local storage and
    // destructive removal. This keeps those details next to the action they
    // describe instead of permanently expanding the repository header.
    if (repo.previewOnly) {
        m_mirrorMenu->addSection(hasMirror ? "PREVIEW CACHED" : "PREVIEW PENDING");
        QAction *keep = m_mirrorMenu->addAction("Keep as a local mirror");
        connect(keep, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex >= 0)
                mirrorPreviewRepository(m_repoDetailIndex);
        });
    } else {
        m_mirrorMenu->addSection("MIRROR STATUS");
        if (online) {
            QAction *browse = m_mirrorMenu->addAction(
                QStringLiteral("Online ") + QChar(0x00B7) +
                " browsable at " + webUrl);
            browse->setToolTip(webUrl);
            connect(browse, &QAction::triggered, this,
                    [webUrl] { QDesktopServices::openUrl(QUrl(webUrl)); });
        } else {
            QAction *status = m_mirrorMenu->addAction(
                repo.publishToNetwork ? "Publishing to ForkMesh..." : "Local only");
            status->setEnabled(false);
        }
        m_mirrorMenu->addSection("MIRROR LOCATION");
        QAction *location = m_mirrorMenu->addAction(
            hasMirror ? repo.mirrorPath : "Mirror has not been created yet");
        location->setEnabled(false);
        QAction *sync = m_mirrorMenu->addAction(hasMirror ? "Sync mirror now"
                                                           : "Create mirror now");
        connect(sync, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex >= 0)
                syncRepository(m_repoDetailIndex);
        });
        if (hasMirror) {
            QAction *copy = m_mirrorMenu->addAction("Copy mirror location");
            connect(copy, &QAction::triggered, this, [this] {
                if (m_repoDetailIndex < 0 ||
                    m_repoDetailIndex >= m_repositories.size())
                    return;
                QApplication::clipboard()->setText(
                    m_repositories.at(m_repoDetailIndex).mirrorPath);
                setRepoDetailNotice("Copied mirror location.");
            });
        }
        m_mirrorMenu->addSeparator();
        QAction *remove = m_mirrorMenu->addAction("Delete mirror...");
        connect(remove, &QAction::triggered, this, &MainWindow::deleteCurrentMirror);
    }

    const bool hasWorktree = !repo.localPath.isEmpty() && QDir(repo.localPath).exists();

    // Source holds distribution and Git-remote details.
    m_sourceMenu->addSection("LOCAL REMOTE");
    QAction *remote = m_sourceMenu->addAction(
        hasMirror ? repo.mirrorPath : "Sync first to create a local remote");
    remote->setEnabled(false);
    if (hasMirror) {
        QAction *copyRemote = m_sourceMenu->addAction("Copy local remote");
        connect(copyRemote, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex < 0 ||
                m_repoDetailIndex >= m_repositories.size())
                return;
            const QString path = m_repositories.at(m_repoDetailIndex).mirrorPath;
            QApplication::clipboard()->setText(path);
            setRepoDetailNotice("Copied local remote path.");
            logSystem("Copied local remote path to clipboard: " + path);
        });
        QAction *copyCommand = m_sourceMenu->addAction(
            "Copy git remote add command");
        connect(copyCommand, &QAction::triggered, this, [this] {
            if (m_repoDetailIndex < 0 ||
                m_repoDetailIndex >= m_repositories.size())
                return;
            const QString path = m_repositories.at(m_repoDetailIndex).mirrorPath;
            QApplication::clipboard()->setText(
                QStringLiteral("git remote add forkmesh \"") + path + "\"");
            setRepoDetailNotice("Copied git remote add command.");
        });
    }
    m_sourceMenu->addSeparator();
    QAction *zip = m_sourceMenu->addAction("Download ZIP");
    zip->setEnabled(hasMirror || hasWorktree);
    connect(zip, &QAction::triggered, this, &MainWindow::downloadCurrentRepoZip);
}

void MainWindow::deleteCurrentMirror()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const int index = m_repoDetailIndex;
    const RepositoryRecord repo = m_repositories.at(index);
    const QString rawPath = repo.mirrorPath.trimmed();
    const QString rawWorktree = repo.localPath.trimmed();
    const QString path = rawPath.isEmpty() ? QString() : QDir::cleanPath(rawPath);
    const QString worktree = rawWorktree.isEmpty()
                                 ? QString()
                                 : QDir::cleanPath(rawWorktree);

    QString message = QStringLiteral(
        "Delete the local mirror for %1/%2?\n\nMirror: %3")
                          .arg(repo.owner, repo.name,
                               path.isEmpty() ? QStringLiteral("not created") : path);
    if (!repo.localPath.isEmpty())
        message += QStringLiteral("\n\nYour working directory will be kept:\n%1")
                       .arg(repo.localPath);
    if (!repo.previewOnly && repo.publishToNetwork)
        message += QStringLiteral("\n\nThe repository will also be removed from "
                                  "the public ForkMesh catalog.");
    if (QMessageBox::warning(this, "Delete mirror", message,
                             QMessageBox::Yes | QMessageBox::Cancel,
                             QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    if (!path.isEmpty() && path == worktree) {
        QMessageBox::warning(
            this, "Delete mirror",
            "The mirror path matches the working directory, so nothing was deleted.");
        return;
    }

    stopRepoHosts();
    if (!path.isEmpty() && QDir(path).exists() && !QDir(path).removeRecursively()) {
        startRepoHosts();
        QMessageBox::warning(this, "Delete mirror",
                             "Could not delete the mirror at:\n" + path);
        return;
    }
    if (!repo.previewOnly && repo.publishToNetwork)
        deleteCatalogRepository(catalogOwner(repo), repo.name);
    const bool canKeepLocalRecord = !repo.localPath.trimmed().isEmpty() ||
                                    !repo.cloneUrl.trimmed().isEmpty();
    if (!repo.previewOnly && canKeepLocalRecord) {
        // Delete the mirror, not the source checkout. Keep the repository in
        // the app as local-only so Mirror > Create mirror can publish it again.
        RepositoryRecord &local = m_repositories[index];
        local.publishToNetwork = false;
        local.publishedAtMs = 0;
        local.lastSyncMs = 0;
    } else {
        // A preview or independent bare-only fork has no separate source left
        // after its mirror is deleted, so its transient record goes too.
        m_repositories.removeAt(index);
    }
    saveRepositories();
    startRepoHosts();
    refreshRepositoryList();
    if (!repo.previewOnly && canKeepLocalRecord) {
        m_repoDetailIndex = index;
        updateRepoDetailStatus();
        updateRepoActionMenus();
    } else {
        m_repoDetailIndex = -1;
        if (!m_repositories.isEmpty())
            openRepoDetail(qMin(index, m_repositories.size() - 1));
        else if (m_repoDetailStack)
            m_repoDetailStack->setCurrentIndex(0); // Code (empty)
    }
    logSystem("Deleted local mirror for " + repo.owner + "/" + repo.name + ".");
    setRepoDetailNotice(canKeepLocalRecord
                            ? "Deleted mirror. The working directory was kept."
                            : "Deleted mirror.");
}

// Per-repo Settings tab: flip visibility (public/private) and delete the repo.
QWidget *MainWindow::buildRepoSettingsTab()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 22, 24, 22);
    outer->setSpacing(14);

    auto *heading = new QLabel("Repository settings");
    heading->setObjectName("channelTitle");
    outer->addWidget(heading);

    // --- Visibility -------------------------------------------------------
    auto *visHeading = new QLabel("Visibility");
    visHeading->setObjectName("sectionLabel");
    outer->addWidget(visHeading);

    m_repoPrivateCheck = new QCheckBox("Private repository");
    m_repoPrivateCheck->setCursor(Qt::PointingHandCursor);
    m_repoPrivateCheck->setToolTip(
        "Hide this repo from the public catalog and require your node's key to "
        "browse or clone it through the mainnode. Only you can read it.");
    connect(m_repoPrivateCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
            return;
        if (m_repositories[m_repoDetailIndex].isPrivate == on)
            return;
        m_repositories[m_repoDetailIndex].isPrivate = on;
        saveRepositories();
        logSystem(QStringLiteral("%1 %2/%3.")
                      .arg(on ? "Made private" : "Made public",
                           m_repositories.at(m_repoDetailIndex).owner,
                           m_repositories.at(m_repoDetailIndex).name));
        // Push the new visibility to the catalog and rebuild hosts so the relay's
        // is_private flag and the host registrations reflect the change at once.
        if (m_repositories.at(m_repoDetailIndex).publishToNetwork)
            publishRepository(m_repoDetailIndex, false);
        startRepoHosts();
        refreshRepositoryList();
        refreshRepoSettings();
    });
    outer->addWidget(m_repoPrivateCheck);

    m_repoVisibilityHint = new QLabel;
    m_repoVisibilityHint->setObjectName("statusLine");
    m_repoVisibilityHint->setWordWrap(true);
    outer->addWidget(m_repoVisibilityHint);

    outer->addSpacing(10);

    // --- Collaborators (private repos, issue #9) --------------------------
    // Share a private repo with other accounts: they see it in their catalog
    // once logged in and clone it with their own key. Only shown for a private
    // repo this node owns and has published (refreshRepoCollaborators toggles
    // visibility and loads the current list from the relay).
    m_collabSection = new QWidget;
    auto *collabLayout = new QVBoxLayout(m_collabSection);
    collabLayout->setContentsMargins(0, 0, 0, 0);
    collabLayout->setSpacing(8);

    auto *collabHeading = new QLabel("Collaborators");
    collabHeading->setObjectName("sectionLabel");
    collabLayout->addWidget(collabHeading);

    auto *collabHint = new QLabel(
        "Accounts you share this private repository with. They can see it in "
        "their catalog (once signed in) and clone it with their own key. The "
        "repo stays hidden from the public website.");
    collabHint->setObjectName("statusLine");
    collabHint->setWordWrap(true);
    collabLayout->addWidget(collabHint);

    m_collabList = new QListWidget;
    m_collabList->setObjectName("collabList");
    m_collabList->setMaximumHeight(140);
    collabLayout->addWidget(m_collabList);

    m_collabEmptyHint = new QLabel("No collaborators yet.");
    m_collabEmptyHint->setObjectName("statusLine");
    collabLayout->addWidget(m_collabEmptyHint);

    auto *collabRow = new QHBoxLayout;
    m_collabEdit = new QLineEdit;
    m_collabEdit->setPlaceholderText("account name to add");
    collabRow->addWidget(m_collabEdit, 1);
    auto *collabAddBtn = new QPushButton("Add");
    collabAddBtn->setProperty("buttonSize", "sm");
    collabAddBtn->setCursor(Qt::PointingHandCursor);
    collabRow->addWidget(collabAddBtn);
    auto *collabRemoveBtn = new QPushButton("Remove selected");
    collabRemoveBtn->setProperty("buttonSize", "sm");
    collabRemoveBtn->setCursor(Qt::PointingHandCursor);
    collabRow->addWidget(collabRemoveBtn);
    collabLayout->addLayout(collabRow);

    auto addCollab = [this] {
        if (m_collabEdit)
            addRepoCollaborator(m_collabEdit->text());
    };
    connect(collabAddBtn, &QPushButton::clicked, this, addCollab);
    connect(m_collabEdit, &QLineEdit::returnPressed, this, addCollab);
    connect(collabRemoveBtn, &QPushButton::clicked, this, [this] {
        if (m_collabList && m_collabList->currentItem())
            removeRepoCollaborator(m_collabList->currentItem()->text());
    });

    outer->addWidget(m_collabSection);
    outer->addSpacing(10);

    // --- Source -----------------------------------------------------------
    auto *sourceHeading = new QLabel("Source");
    sourceHeading->setObjectName("sectionLabel");
    outer->addWidget(sourceHeading);

    m_repoSourceEdit = new QLineEdit;
    m_repoSourceEdit->setPlaceholderText(
        "https://forkmesh.com/<node>/<owner>/<name>");
    m_repoSourceEdit->setToolTip(
        "The upstream clone URL this mirror was forked from. Edit it to repoint "
        "the mirror at a live node when the original location goes stale.");
    auto *sourceUpdateBtn = new QPushButton("Update");
    sourceUpdateBtn->setProperty("buttonSize", "sm");
    sourceUpdateBtn->setCursor(Qt::PointingHandCursor);
    connect(sourceUpdateBtn, &QPushButton::clicked, this,
            &MainWindow::updateRepoSource);
    connect(m_repoSourceEdit, &QLineEdit::returnPressed, this,
            &MainWindow::updateRepoSource);
    auto *sourceRow = new QHBoxLayout;
    sourceRow->addWidget(m_repoSourceEdit, 1);
    sourceRow->addWidget(sourceUpdateBtn);
    outer->addLayout(sourceRow);

    m_repoSourceHint = new QLabel;
    m_repoSourceHint->setObjectName("statusLine");
    m_repoSourceHint->setWordWrap(true);
    outer->addWidget(m_repoSourceHint);

    outer->addSpacing(10);

    // --- Actions ----------------------------------------------------------
    auto *actionsHeading = new QLabel("Actions");
    actionsHeading->setObjectName("sectionLabel");
    outer->addWidget(actionsHeading);

    m_settingsActionsCheck = new QCheckBox("Run actions on push");
    m_settingsActionsCheck->setCursor(Qt::PointingHandCursor);
    m_settingsActionsCheck->setToolTip(
        "When a fork pushes to this repo's local mirror, run its .forkmesh/ "
        "workflows. Off by default for mirrored repos. Changed workflows still "
        "require your approval before they run.");
    connect(m_settingsActionsCheck, &QCheckBox::toggled, this,
            [this](bool on) { setRepoActionsEnabled(on); });
    outer->addWidget(m_settingsActionsCheck);

    auto *actionsHint = new QLabel(
        "Mirrored repositories start with actions disabled. Enable this only for "
        "repos whose workflows you trust to run on this node.");
    actionsHint->setObjectName("statusLine");
    actionsHint->setWordWrap(true);
    outer->addWidget(actionsHint);

    outer->addSpacing(10);

    // --- Secret scanning --------------------------------------------------
    auto *secretHeading = new QLabel("Secret scanning");
    secretHeading->setObjectName("sectionLabel");
    outer->addWidget(secretHeading);

    m_secretScanCheck = new QCheckBox("Block push if secrets are detected");
    m_secretScanCheck->setCursor(Qt::PointingHandCursor);
    m_secretScanCheck->setToolTip(
        "Before each push ForkMesh scans the commits being pushed for API keys, "
        "private keys, and other high-confidence secrets. If any are found you "
        "will be warned and can cancel or push anyway.");
    connect(m_secretScanCheck, &QCheckBox::toggled, this,
            [this](bool on) { setRepoSecretScanningEnabled(on); });
    outer->addWidget(m_secretScanCheck);

    auto *secretHint = new QLabel(
        "Detects GitHub tokens, AWS access keys, Slack tokens, and PEM private "
        "keys. Rotate any exposed credentials immediately.");
    secretHint->setObjectName("statusLine");
    secretHint->setWordWrap(true);
    outer->addWidget(secretHint);

    outer->addSpacing(10);

    // --- Coves (encrypted vaults) -----------------------------------------
    outer->addWidget(buildCoveSection());

    outer->addSpacing(10);

    // --- Danger zone ------------------------------------------------------
    auto *dangerHeading = new QLabel("Danger zone");
    dangerHeading->setObjectName("sectionLabel");
    outer->addWidget(dangerHeading);

    auto *deleteHint = new QLabel(
        "Delete this node's mirror of the repository. If you have a local "
        "working directory it is kept and the repo stays as local-only; "
        "otherwise the repository is removed from this node. Published repos "
        "are also removed from the public ForkMesh catalog.");
    deleteHint->setObjectName("statusLine");
    deleteHint->setWordWrap(true);
    outer->addWidget(deleteHint);

    auto *deleteBtn = new QPushButton("Delete repository");
    deleteBtn->setObjectName("dangerButton");
    deleteBtn->setProperty("buttonSize", "sm");
    deleteBtn->setCursor(Qt::PointingHandCursor);
    setOcticon(deleteBtn, "trash", 16);
    connect(deleteBtn, &QPushButton::clicked, this, &MainWindow::deleteCurrentMirror);
    auto *deleteRow = new QHBoxLayout;
    deleteRow->addWidget(deleteBtn);
    deleteRow->addStretch();
    outer->addLayout(deleteRow);

    outer->addStretch();
    return page;
}

void MainWindow::setRepoActionsEnabled(bool on)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    if (m_repositories[m_repoDetailIndex].actionsEnabled == on)
        return;
    m_repositories[m_repoDetailIndex].actionsEnabled = on;
    saveRepositories();
    // The hook stays installed regardless (it powers the live Code refresh);
    // just make sure it exists when enabling.
    ensurePushHook(m_repositories.at(m_repoDetailIndex));
    logSystem(QStringLiteral("Actions %1 for %2/%3.")
                  .arg(on ? "enabled" : "disabled",
                       m_repositories.at(m_repoDetailIndex).owner,
                       m_repositories.at(m_repoDetailIndex).name));
    // Keep both toggles (Actions tab + Settings tab) in sync.
    if (m_actionsEnabledCheck) {
        QSignalBlocker block(m_actionsEnabledCheck);
        m_actionsEnabledCheck->setChecked(on);
    }
    if (m_settingsActionsCheck) {
        QSignalBlocker block(m_settingsActionsCheck);
        m_settingsActionsCheck->setChecked(on);
    }
}

void MainWindow::setRepoSecretScanningEnabled(bool on)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    if (m_repositories[m_repoDetailIndex].secretScanningEnabled == on)
        return;
    m_repositories[m_repoDetailIndex].secretScanningEnabled = on;
    saveRepositories();
    logSystem(QStringLiteral("Secret scanning push protection %1 for %2/%3.")
                  .arg(on ? "enabled" : "disabled",
                       m_repositories.at(m_repoDetailIndex).owner,
                       m_repositories.at(m_repoDetailIndex).name));
    if (m_secretScanCheck) {
        QSignalBlocker block(m_secretScanCheck);
        m_secretScanCheck->setChecked(on);
    }
}

bool MainWindow::isWorkflowDisabled(const QString &path) const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return false;
    return m_repositories.at(m_repoDetailIndex).disabledWorkflows.contains(path);
}

void MainWindow::setWorkflowDisabled(const QString &path, bool disabled)
{
    if (path.isEmpty() || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    QStringList &off = m_repositories[m_repoDetailIndex].disabledWorkflows;
    if (disabled == off.contains(path))
        return; // already in the desired state
    if (disabled)
        off.append(path);
    else
        off.removeAll(path);
    saveRepositories();
    logSystem(QStringLiteral("Actions: workflow %1 %2 for %3/%4.")
                  .arg(path, disabled ? QStringLiteral("disabled")
                                      : QStringLiteral("enabled"),
                       m_repositories.at(m_repoDetailIndex).owner,
                       m_repositories.at(m_repoDetailIndex).name));
    // A disabled workflow can't be triggered by hand either.
    updateManualRunBar();
}

void MainWindow::refreshRepoSettings()
{
    // Reload the cove list up front so it stays in sync even when the visibility
    // hints below take one of this function's early returns.
    rebuildRepoCovesList();
    const bool haveRepo =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size();
    if (m_repoPrivateCheck) {
        QSignalBlocker block(m_repoPrivateCheck);
        m_repoPrivateCheck->setEnabled(haveRepo);
        m_repoPrivateCheck->setChecked(
            haveRepo && m_repositories.at(m_repoDetailIndex).isPrivate);
    }
    if (m_settingsActionsCheck) {
        QSignalBlocker block(m_settingsActionsCheck);
        m_settingsActionsCheck->setEnabled(haveRepo);
        m_settingsActionsCheck->setChecked(
            haveRepo && m_repositories.at(m_repoDetailIndex).actionsEnabled);
    }
    if (m_secretScanCheck) {
        QSignalBlocker block(m_secretScanCheck);
        m_secretScanCheck->setEnabled(haveRepo);
        m_secretScanCheck->setChecked(
            !haveRepo || m_repositories.at(m_repoDetailIndex).secretScanningEnabled);
    }
    // Show/hide + reload the collaborator list for the open repo (issue #9).
    refreshRepoCollaborators();
    if (m_repoSourceEdit) {
        QSignalBlocker block(m_repoSourceEdit);
        m_repoSourceEdit->setEnabled(haveRepo);
        m_repoSourceEdit->setText(
            haveRepo ? m_repositories.at(m_repoDetailIndex).cloneUrl.trimmed()
                     : QString());
    }
    if (m_repoSourceHint) {
        if (!haveRepo) {
            m_repoSourceHint->clear();
        } else {
            const RepositoryRecord &r = m_repositories.at(m_repoDetailIndex);
            if (!r.localPath.trimmed().isEmpty())
                m_repoSourceHint->setText(
                    "This repo is backed by a local working copy at " +
                    r.localPath.trimmed() +
                    "; syncs read from there. The clone URL above is the "
                    "published/fork location.");
            else if (r.cloneUrl.trimmed().isEmpty())
                m_repoSourceHint->setText(
                    "No upstream set — this node hosts the repo directly.");
            else
                m_repoSourceHint->setText(
                    "The mirror fetches from this URL. Update it to repoint the "
                    "fork at a different node, then sync to pull from it.");
        }
    }
    if (!m_repoVisibilityHint)
        return;
    if (!haveRepo) {
        m_repoVisibilityHint->clear();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (!repo.publishToNetwork)
        m_repoVisibilityHint->setText(
            "This repository is local only — it isn't published to the network "
            "yet. The visibility choice applies once you publish it.");
    else if (repo.isPrivate)
        m_repoVisibilityHint->setText(
            "Private: hidden from the public catalog. Only this node's key can "
            "browse or clone it through the mainnode.");
    else
        m_repoVisibilityHint->setText(
            "Public: listed in the catalog and anyone can browse or clone it "
            "through the mainnode.");
}

void MainWindow::updateRepoSource()
{
    if (!m_repoSourceEdit)
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const QString newUrl = m_repoSourceEdit->text().trimmed();
    RepositoryRecord &repo = m_repositories[m_repoDetailIndex];
    if (newUrl == repo.cloneUrl.trimmed()) {
        refreshRepoSettings();
        return;
    }
    repo.cloneUrl = newUrl;
    saveRepositories();
    // Repoint the bare mirror's origin so the next sync fetches from the new
    // location. A repo backed by a local working copy fetches from that path
    // instead (see repositorySource()), so leave its remote alone.
    if (repo.localPath.trimmed().isEmpty() && !newUrl.isEmpty() &&
        !repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists())
        runGitCapture(repo.mirrorPath,
                      {QStringLiteral("remote"), QStringLiteral("set-url"),
                       QStringLiteral("origin"), newUrl},
                      nullptr, nullptr);
    logSystem(QStringLiteral("Source for %1/%2 set to %3.")
                  .arg(repo.owner, repo.name,
                       newUrl.isEmpty() ? QStringLiteral("(none)") : newUrl));
    refreshRepoSettings();
}

void MainWindow::updateRepoDetailStatus()
{
    if (!m_repoDetailStatus)
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        m_repoDetailStatus->clear();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString repoKey = repo.owner + "/" + repo.name;
    const QPair<int, int> stats = m_repoStats.value(repoKey);
    if (repo.previewOnly) {
        const bool cached =
            !repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists();
        QStringList bits;
        bits << (cached ? QStringLiteral("<b>Preview cached</b>")
                        : QStringLiteral("<b>Preview cache pending</b>"));
        bits << QStringLiteral("<b>Temporary</b>");
        bits << QStringLiteral("<b>%1</b> served").arg(stats.first);
        bits << QStringLiteral("<b>%1</b> clone%2")
                    .arg(stats.second)
                    .arg(stats.second == 1 ? QString()
                                           : QStringLiteral("s"));
        QString details = bits.join(QString::fromUtf8(" \xC2\xB7 "));
        details += QString::fromUtf8("<br><span style='color:#8b949e'>Cache %1 "
                                  "\xC2\xB7 Last refresh %2</span>")
                       .arg(repo.mirrorPath.toHtmlEscaped(),
                            formatRepoDate(repo.lastSyncMs));
        m_repoDetailStatus->setText(details);
        return;
    }
    QStringList bits;
    bits << QStringLiteral("<b>%1</b> served").arg(stats.first);
    bits << QStringLiteral("<b>%1</b> clone%2")
                .arg(stats.second)
                .arg(stats.second == 1 ? QString() : QStringLiteral("s"));

    QString details = bits.join(QString::fromUtf8(" \xC2\xB7 "));
    details += QString::fromUtf8("<br><span style='color:#8b949e'>Hosted since %1 "
                              "\xC2\xB7 Last sync %2</span>")
                   .arg(formatRepoDate(repo.hostedSinceMs),
                        formatRepoDate(repo.lastSyncMs));
    m_repoDetailStatus->setText(details);
}

QUrl MainWindow::catalogApiUrl() const
{
    QUrl url(canonicalServerUrl(m_serverUrlEdit ? m_serverUrlEdit->text() : QString()));
    if (!url.isValid() || url.host().isEmpty())
        url = QUrl(kDefaultServerUrl);
    if (url.scheme() == "ws")
        url.setScheme(QStringLiteral("http"));
    else if (url.scheme() == "wss")
        url.setScheme(QStringLiteral("https"));
    url.setPath(QStringLiteral("/api/repositories"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void MainWindow::deleteCatalogRepository(const QString &owner, const QString &name)
{
    const QString safeOwner = repoSegment(owner, QStringLiteral("owner"));
    const QString safeName = repoSegment(name, QStringLiteral("repository"));
    if (safeOwner.isEmpty() || safeName.isEmpty())
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        logSystem("Catalog: could not load identity to remove old website entry.");
        return;
    }

    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-catalog-delete-v1\n" + safeOwner + "\n" + safeName + "\n" + ts)
            .toUtf8();
    QUrl url = catalogApiUrl();
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("owner"), safeOwner);
    query.addQueryItem(QStringLiteral("name"), safeName);
    query.addQueryItem(QStringLiteral("ts"), ts);
    query.addQueryItem(QStringLiteral("sig"), m_profileIdentity.signData(canonical));
    url.setQuery(query);

    QNetworkReply *reply = m_networkAccess->deleteResource(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, safeOwner, safeName] {
                reply->deleteLater();
                if (reply->error() == QNetworkReply::NoError) {
                    logSystem("Catalog: removed old website entry " + safeOwner +
                              "/" + safeName + ".");
                } else {
                    logSystem("Catalog: could not remove old website entry " +
                              safeOwner + "/" + safeName + ": " +
                              reply->errorString());
                }
            });
}

void MainWindow::migrateReposForProfileName(const QString &oldOwner,
                                            const QString &newOwner)
{
    const QString oldName = repoSegment(oldOwner, QStringLiteral("owner"));
    const QString newName = repoSegment(newOwner, QStringLiteral("owner"));
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName)
        return;

    QList<int> toRepublish;
    bool changed = false;
    for (int i = 0; i < m_repositories.size(); ++i) {
        RepositoryRecord &repo = m_repositories[i];
        if (repo.previewOnly)
            continue;
        const QString repoName = repoSegment(repo.name, QStringLiteral("repository"));
        const bool wasPublished = repo.publishToNetwork || repo.publishedAtMs > 0;
        if (wasPublished)
            deleteCatalogRepository(oldName, repoName);
        if (repo.owner == oldName || wasPublished) {
            repo.owner = newName;
            changed = true;
            if (repo.publishToNetwork)
                toRepublish.append(i);
        }
    }

    if (!changed)
        return;
    saveRepositories();
    installAllPushHooks();
    refreshRepositoryList();
    startRepoHosts();
    for (int index : std::as_const(toRepublish))
        publishRepository(index, false);
    logSystem("Renamed local published repositories from " + oldName + " to " +
              newName + ".");
}

QUrl MainWindow::hostWsUrl(const RepositoryRecord &repo) const
{
    QUrl url(canonicalServerUrl(m_serverUrlEdit ? m_serverUrlEdit->text() : QString()));
    if (!url.isValid() || url.host().isEmpty())
        url = QUrl(kDefaultServerUrl);
    if (url.scheme() == "http")
        url.setScheme(QStringLiteral("ws"));
    else if (url.scheme() == "https")
        url.setScheme(QStringLiteral("wss"));
    url.setPath("/api/repo/" + catalogOwner(repo) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/host");
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void MainWindow::stopRepoHosts()
{
    for (RepoHost *host : std::as_const(m_repoHosts)) {
        host->stop();
        host->deleteLater();
    }
    m_repoHosts.clear();
}

void MainWindow::startRepoHosts()
{
    // One live host per published repository that already has a local mirror.
    // Rebuilt from scratch so adding/removing repos stays simple.
    stopRepoHosts();
    if (!hasActiveAccountSession())
        return;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly || !repo.publishToNetwork || repo.mirrorPath.isEmpty() ||
            !QDir(repo.mirrorPath).exists())
            continue;
        auto *host = new RepoHost(catalogOwner(repo), repo.name, repo.mirrorPath,
                                  hostWsUrl(repo), this);
        // Sign a fresh host-auth token on every (re)connect so the relay can
        // verify this node holds the owner account's key before it may host.
        const QString tokenOwner = catalogOwner(repo);
        const QString tokenRepo = repo.name;
        host->setTokenProvider([this, tokenOwner, tokenRepo]() -> QString {
            const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
            const QByteArray canonical =
                ("forkmesh-host-v1\n" + tokenOwner + "\n" + tokenRepo + "\n" + ts)
                    .toUtf8();
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("ts"), ts);
            q.addQueryItem(QStringLiteral("sig"), m_profileIdentity.signData(canonical));
            return q.query();
        });
        connect(host, &RepoHost::log, this, &MainWindow::logSystem);
        connect(host, &RepoHost::requestServed, this, &MainWindow::onRequestServed);
        host->start();
        m_repoHosts.append(host);
    }
}

void MainWindow::onRequestServed(const QString &owner, const QString &name, bool clone)
{
    QPair<int, int> &stats = m_repoStats[owner + "/" + name];
    stats.first += 1; // served through the mainnode
    if (clone)
        stats.second += 1; // git clone
    // Only the activity-strip pulse is per-event; everything else below is
    // coalesced. A clone/browse burst fires this slot dozens of times a second,
    // and re-running the full repository-list rebuild (per-repo git reads) plus a
    // QSettings write for each one stalled the GUI for seconds (stall log:
    // refreshRepositoryList <- onRequestServed).
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        // Flash our own dot on the Mirror nodes activity strip: green when we
        // just served a clone, orange when we served codebase browsing/fetches.
        if (m_mirrorActivityStrip && catalogOwner(repo) == owner &&
            repo.name == name)
            static_cast<MirrorActivityStrip *>(m_mirrorActivityStrip)
                ->pulse(m_profileIdentity.publicKey(), clone);
    }
    if (!m_requestServedFlushTimer) {
        m_requestServedFlushTimer = new QTimer(this);
        m_requestServedFlushTimer->setSingleShot(true);
        m_requestServedFlushTimer->setInterval(1000);
        connect(m_requestServedFlushTimer, &QTimer::timeout, this, [this] {
            saveRepoStats();
            refreshRepositoryList();
            if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
                updateRepoDetailStatus();
            // Hosting stats live in the node profile; keep them current while
            // it is open.
            if (m_nodeProfilePanel && m_nodeProfilePanel->isVisible())
                refreshProfileHostingStats();
        });
    }
    // Not restarted while pending: under continuous traffic the flush still
    // lands once a second instead of being pushed out forever.
    if (!m_requestServedFlushTimer->isActive())
        m_requestServedFlushTimer->start();
}

void MainWindow::loadRepoStats()
{
    m_repoStats.clear();
    const QJsonObject obj =
        QJsonDocument::fromJson(
            QSettings().value(QStringLiteral("repositories/stats")).toByteArray())
            .object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        m_repoStats.insert(it.key(),
                           {entry.value("served").toInt(),
                            entry.value("clones").toInt()});
    }
}

void MainWindow::saveRepoStats() const
{
    QJsonObject obj;
    for (auto it = m_repoStats.constBegin(); it != m_repoStats.constEnd(); ++it) {
        obj.insert(it.key(), QJsonObject{{"served", it.value().first},
                                         {"clones", it.value().second}});
    }
    QSettings().setValue(QStringLiteral("repositories/stats"),
                         QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void MainWindow::publishRepository(int index, bool showDialogOnError)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    if (m_repositories.at(index).previewOnly)
        return;
    RepositoryRecord &repo = m_repositories[index];
    if (!hasActiveAccountSession()) {
        // Publishing/hosting is the opt-in, paid side of the app. Point the user
        // at the "Get paid to mirror" button on their node profile rather than
        // failing silently; the core flow (clone, mirror, issues, PRs) is
        // unaffected by staying opted out.
        const QString message = QStringLiteral(
            "Mirroring this repo locally needs nothing extra. To host it on the "
            "network and get paid, open your node profile and choose \"Get paid "
            "to mirror\".");
        logSystem(message);
        if (showDialogOnError)
            flashMessage(message, /*error=*/true);
        return;
    }
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        logSystem("Catalog: could not load identity for repository publishing.");
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Always publish under the registered account name so the catalog dedups by
    // account/name (one entry per fork) and the server can verify ownership.
    // catalogOwner() is shared with the live host tunnel so the website browses
    // the same owner the host registers under.
    const QString owner = catalogOwner(repo);
    const QString name = repoSegment(repo.name, QStringLiteral("repository"));
    const QString updatedAt = QString::number(now);
    // A stable identity for the logical repo: its first (root) commit, shared by
    // every node mirroring it. The network page groups mirrors by this so the same
    // repo under different owners shows as one card. Not part of the signature.
    QString rootCommit;
    {
        const QString gitDir =
            (!repo.localPath.trimmed().isEmpty() && QDir(repo.localPath).exists(".git"))
                ? repo.localPath
                : repo.mirrorPath;
        // Resolve the earliest root commit. Prefer HEAD; but a bare mirror cloned
        // from the relay can carry an unset/dangling HEAD (the relay serves
        // git-upload-pack without advertising a symref HEAD), so "rev-list ... HEAD"
        // fails and leaves rootCommit empty. A wrong/empty root drops that mirror
        // into a different group key (worker repo_mirror_group_key), so the owner's
        // mirror-nodes panel never lists it next to the source of truth — the node
        // shows up on the mirror but not on the source (issue #243, adhoc #134).
        auto firstRoot = [&](const QStringList &args) -> QString {
            QByteArray out;
            if (gitDir.trimmed().isEmpty() ||
                !runGitCapture(gitDir, args, &out, nullptr))
                return QString();
            const QStringList roots =
                QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
            return roots.isEmpty() ? QString() : roots.last().trimmed();
        };
        rootCommit = firstRoot({"rev-list", "--max-parents=0", "HEAD"});
        if (rootCommit.isEmpty()) {
            // HEAD is unset. Compute the root of the branch this mirror actually
            // serves (mirrorHeadBranch: the same default branch the source's HEAD
            // points to), NOT a blind "--all" walk. This repo has several root
            // commits — one per independent history the mirror also holds (agent
            // branches, imported subtrees). "rev-list --max-parents=0 --all" returns
            // ALL of them and "roots.last()" would pick whichever an unrelated
            // history contributes, so the mirror published a different rootCommit
            // than the source and landed in its own group (adhoc #134). Resolving
            // the served branch yields the source's root regardless of HEAD's state.
            const QString branch = mirrorHeadBranch(gitDir);
            if (!branch.isEmpty())
                rootCommit = firstRoot({"rev-list", "--max-parents=0", branch});
        }
        // Last resort (no HEAD and no resolvable served branch, e.g. a truly empty
        // ref set): fall back to --all so a single-root mirror still groups.
        if (rootCommit.isEmpty())
            rootCommit = firstRoot({"rev-list", "--max-parents=0", "--all"});
    }
    // Owner-signed fingerprint of the refs this node serves (sha256 over the
    // canonical heads+tags advertisement). The relay pins this and refuses to
    // serve any mirror whose live advertisement doesn't hash to it, so a tampered
    // or rolled-back mirror can never be cloned. MUST match the worker's
    // advertised_refs_canonical(): "<sha> <refname>" lines for refs/heads/* and
    // refs/tags/* only, sorted, joined by '\n'.
    const QString stateHash = mirrorStateHash(repo.mirrorPath);
    // Repository details (about text + website) live in the committed
    // .forkmesh/info.json, the single source of truth (issue #232). Prefer it over
    // the locally-cached record fields so the website's About panel is filled from
    // info.json even for a mirror that cloned the repo but never had its
    // description typed in locally (adhoc #86).
    QString publishedWebsite;
    QString publishedDescription = repo.description;
    if (!repo.localPath.trimmed().isEmpty()) {
        QFile file(QDir(repo.localPath).filePath(kRepoInfoPath));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject info = QJsonDocument::fromJson(file.readAll()).object();
            publishedWebsite = info.value(QStringLiteral("website")).toString();
            const QString about =
                info.value(QStringLiteral("about")).toString().trimmed();
            if (!about.isEmpty())
                publishedDescription = about;
        }
    }
    if (index == m_repoDetailIndex) {
        if (publishedWebsite.isEmpty())
            publishedWebsite = m_repoInfo.website;
        if (publishedDescription.trimmed().isEmpty() &&
            !m_repoInfo.about.trimmed().isEmpty())
            publishedDescription = m_repoInfo.about;
    }
    // Node facts the live Mirror nodes view shows per node (latest commit, issue
    // count, platform, version, node id). Published alongside the mirror so those
    // columns stay populated for a node that's offline or only intermittently in
    // the room — otherwise a catalog-backed row falls back to em-dashes for
    // everything but sync time and size (adhoc #56). The HEAD/issue figures mirror
    // the live advert (setMirroredRepos); platform/version/id come from our own
    // roster entry (the same values makeMessage broadcasts).
    const QString headBranch = mirrorHeadBranch(repo.mirrorPath);
    const QString headCommit = mirrorBranchCommit(repo.mirrorPath, headBranch);
    const int issueCount = mirrorIssueCount(repo.mirrorPath, headBranch);
    const int commitCount = mirrorCommitCount(repo.mirrorPath, headBranch);
    const int branchCount = mirrorBranchCount(repo.mirrorPath);
    const int pullCount = mirrorPullCount(repo.mirrorPath, headBranch);
    const int discussionCount = mirrorDiscussionCount(repo.mirrorPath, headBranch);
    const int worktreeCount = mirrorWorktreeCount(repo.localPath);
    const int artifactCount = mirrorArtifactCount(repo.mirrorPath);
    QString selfPlatform, selfVersion, selfNodeId;
    for (const MemberInfo &member : std::as_const(m_homeRoster)) {
        if (member.self) {
            selfPlatform = member.platform;
            selfVersion = member.version;
            selfNodeId = member.id;
            break;
        }
    }
    // Clone / website-serve tallies this node has accumulated for the repo. The
    // stored pair is (total served, clones); publish clones and website (browse)
    // serves separately so the Mirror nodes view can show each node's contribution
    // even while it's offline (the counters are otherwise purely local). Keyed the
    // same way onRequestServed writes them: catalogOwner()/raw repo name.
    const QPair<int, int> serveStats =
        m_repoStats.value(owner + "/" + repo.name);
    const int clonesServed = serveStats.second;
    const int websiteServed = qMax(0, serveStats.first - serveStats.second);
    QJsonObject metadata{{"owner", owner},
                         {"name", name},
                         {"commit", headCommit},
                         {"branch", headBranch},
                         {"issueCount", QString::number(issueCount)},
                         {"commitCount", QString::number(commitCount)},
                         {"branchCount", QString::number(branchCount)},
                         {"pullCount", QString::number(pullCount)},
                         {"discussionCount", QString::number(discussionCount)},
                         {"worktreeCount", QString::number(worktreeCount)},
                         {"artifactCount", QString::number(artifactCount)},
                         {"platform", selfPlatform},
                         {"version", selfVersion},
                         {"nodeId", selfNodeId},
                         {"clonesServed", QString::number(clonesServed)},
                         {"websiteServed", QString::number(websiteServed)},
                         {"description", publishedDescription},
                         {"website", publishedWebsite},
                         {"cloneUrl", repo.cloneUrl},
                         {"solana", repo.solanaAddress},
                         {"channel", repositoryChannel(repo)},
                         {"hostedSince", QString::number(repo.hostedSinceMs)},
                         {"lastSync", QString::number(repo.lastSyncMs)},
                         {"updatedAt", updatedAt},
                         {"rootCommit", rootCommit},
                         // On-disk mirror size so the network page can show how
                         // much data each owner/repo is hosting. Not signed.
                         {"sizeBytes",
                          QString::number(mirrorRepoSizeBytes(repo.mirrorPath))},
                         {"visibility", repo.isPrivate
                                            ? QStringLiteral("private")
                                            : QStringLiteral("public")},
                         {"source", repo.localPath.trimmed().isEmpty()
                                        ? QStringLiteral("remote-clone")
                                        : QStringLiteral("local-node")},
                         {"maintainer", m_profileIdentity.publicKey()}};
    metadata.insert("signature", m_profileIdentity.signJson(metadata));
    // The server verifies this against the account's registered pubkey: only the
    // account key holder can write its namespace (prevents impersonation/dups).
    const QByteArray catalogCanonical =
        ("forkmesh-catalog-v1\n" + owner + "\n" + name + "\n" + updatedAt).toUtf8();
    metadata.insert("catalogSig", m_profileIdentity.signData(catalogCanonical));
    // Attest the served refs so the relay can detect a tampered/stale mirror.
    // Signed with the same account key the relay verifies for the catalog write.
    if (!stateHash.isEmpty()) {
        metadata.insert("stateHash", stateHash);
        const QByteArray stateCanonical =
            ("forkmesh-repostate-v1\n" + owner + "\n" + name + "\n" + stateHash +
             "\n" + updatedAt)
                .toUtf8();
        metadata.insert("stateSig", m_profileIdentity.signData(stateCanonical));
    }

    QNetworkRequest request(catalogApiUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply =
        m_networkAccess->post(request, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    logSystem("Catalog: publishing " + repo.owner + "/" + repo.name + " to " +
              request.url().toString() + ".");

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, index, showDialogOnError] {
                const QByteArray body = reply->readAll();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QNetworkReply::NetworkError error = reply->error();
                reply->deleteLater();

                if (index < 0 || index >= m_repositories.size())
                    return;

                RepositoryRecord &repo = m_repositories[index];
                if (error == QNetworkReply::NoError && status >= 200 && status < 300) {
                    repo.publishToNetwork = true;
                    repo.publishedAtMs = QDateTime::currentMSecsSinceEpoch();
                    saveRepositories();
                    refreshRepositoryList();
                    logSystem("Catalog: published " + repo.owner + "/" +
                              repo.name + " to forkmesh.com.");
                    if (showDialogOnError)
                        flashMessage("Published " + repo.owner + "/" + repo.name +
                                     " to forkmesh.com.");
                    // The pin now reflects our served refs again; clear any stale
                    // "clones are being rejected" banner for the open repo.
                    if (index == m_repoDetailIndex)
                        refreshRepoPinBanner();
                    return;
                }

                const QString contentType =
                    reply->header(QNetworkRequest::ContentTypeHeader).toString();
                QString detail = QString::fromUtf8(body).trimmed();
                if (contentType.contains("text/html", Qt::CaseInsensitive) ||
                    detail.startsWith("<!doctype", Qt::CaseInsensitive) ||
                    detail.startsWith("<html", Qt::CaseInsensitive)) {
                    detail = status == 429 ? "rate limited" : "unexpected HTML response";
                } else {
                    detail = detail.left(500);
                }
                const QString message =
                    "Catalog publish failed for " + repo.owner + "/" + repo.name +
                    (status > 0 ? " (HTTP " + QString::number(status) + ")" :
                                  QString()) +
                    (detail.isEmpty() ? QString() : ": " + detail);
                logSystem(message);
                if (showDialogOnError)
                    flashMessage(message, /*error=*/true);
            });
}

namespace {

// A cheap digest of all refs in a bare mirror, so an automatic fetch can tell
// whether the owner's repo actually changed before announcing/republishing.
QString mirrorRefsDigest(const QString &mirrorPath)
{
    if (!QDir(mirrorPath).exists())
        return QString();
    QProcess p;
    p.start("git", {"-C", mirrorPath, "for-each-ref",
                    "--format=%(objectname) %(refname)"});
    if (!p.waitForFinished(5000))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}

void repairMirrorHead(const QString &mirrorPath, const QString &sourcePath)
{
    QString preferred;
    if (QDir(sourcePath).exists(QStringLiteral(".git"))) {
        QProcess source;
        source.start("git", {"-C", sourcePath, "symbolic-ref", "--short", "HEAD"});
        if (source.waitForFinished(5000) && source.exitCode() == 0)
            preferred = QString::fromUtf8(source.readAllStandardOutput()).trimmed();
    }

    const QString current = mirrorHeadBranch(mirrorPath);
    QStringList candidates{preferred, QStringLiteral("main"),
                           QStringLiteral("master"), current};
    QString branch;
    for (const QString &candidate : std::as_const(candidates)) {
        if (!candidate.isEmpty() &&
            !mirrorBranchCommit(mirrorPath, candidate).isEmpty()) {
            branch = candidate;
            break;
        }
    }
    if (branch.isEmpty()) {
        QProcess refs;
        refs.start("git", {"-C", mirrorPath, "for-each-ref",
                           "--format=%(refname:short)", "--sort=-committerdate",
                           "--count=1", "refs/heads/"});
        if (refs.waitForFinished(5000) && refs.exitCode() == 0)
            branch = QString::fromUtf8(refs.readAllStandardOutput()).trimmed();
    }
    if (branch.isEmpty() || branch == current)
        return;

    QProcess setHead;
    setHead.start("git", {"-C", mirrorPath, "symbolic-ref", "HEAD",
                          "refs/heads/" + branch});
    setHead.waitForFinished(5000);
}

// Whether a failed mirror clone/fetch is a momentary host/relay hiccup that the
// next auto-sync will simply retry, rather than a real, persistent problem. Two
// families qualify: connectivity failures (HTTP 5xx, resets, DNS) and — the case
// that surfaced on fresh installs cloning a large repo — a truncated pack, where
// the streaming host tunnel gets cut mid-transfer and git reports "unexpected
// disconnect while reading sideband packet" / "early EOF" / "fetch-pack: invalid
// index-pack output". A partial clone leaves no mirror behind, so autoSyncMirrors
// re-attempts it; classifying it transient keeps that self-healing quiet instead
// of raising a scary permanent red error over what a retry fixes.
bool isTransientSyncError(const QString &errors)
{
    return errors.contains(QStringLiteral("HTTP 50")) ||
           errors.contains(QStringLiteral("RPC failed")) ||
           errors.contains(QStringLiteral("curl 22")) ||
           errors.contains(QStringLiteral("502")) ||
           errors.contains(QStringLiteral("503")) ||
           errors.contains(QStringLiteral("504")) ||
           errors.contains(QStringLiteral("Could not resolve"),
                           Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("Couldn't connect"),
                           Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("Connection reset"),
                           Qt::CaseInsensitive) ||
           // Truncated pack over the streaming clone tunnel.
           errors.contains(QStringLiteral("early EOF"), Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("unexpected disconnect"),
                           Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("sideband"), Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("index-pack"), Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("fetch-pack"), Qt::CaseInsensitive) ||
           errors.contains(QStringLiteral("remote end hung up"),
                           Qt::CaseInsensitive);
}

} // namespace

void MainWindow::autoSyncMirrors()
{
    // Retry the flagship-repo bootstrap here too, not just the one-shot timer
    // shortly after launch: if the catalog wasn't reachable yet at that single
    // attempt (network still coming up right after a fresh install, relay
    // momentarily down), a long-running node — especially a headless daemon
    // that rarely restarts — would otherwise never end up mirroring the
    // project repo or joining the mirror network until its next relaunch.
    // ensureFlagshipRepo() is idempotent (no-op once the repo is present).
    ensureFlagshipRepo();

    // Quietly refresh every repo's mirror so it tracks the owner's repo.
    for (int i = 0; i < m_repositories.size(); ++i) {
        if (!m_syncingRepos.contains(i) &&
            !m_repositories.at(i).previewOnly &&
            !repositorySource(m_repositories.at(i)).isEmpty())
            syncRepository(i, /*quiet=*/true);
    }

    // While we're online, keep every repo we're the source of truth for pinned to
    // the refs it actually serves. Runs on the same cadence as the mirror sync so
    // a source repo whose pin drifted (and whose detail the owner never opened)
    // heals on its own instead of leaving clones rejected with a failing integrity
    // pin until a manual reset.
    reattestStalePins();
}

void MainWindow::syncMirrorsBehindRoster()
{
    // A peer just (re-)advertised its mirror set via hello. For every repo we
    // mirror, if any online peer advertises a commit our bare mirror does not
    // contain, pull it now rather than waiting for the 5-minute auto-sync. This
    // backstops notifyMirrorUpdated (which is ephemeral and missed if we were
    // offline/just connected): the moment the roster shows the source moved, we
    // converge. syncRepository fetches refs/heads/* + refs/tags/*, so issue/PR
    // and commit-comment changes (which live on refs/heads) come along too.
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.previewOnly || m_syncingRepos.contains(i))
            continue;
        if (repositorySource(repo).isEmpty())
            continue; // we are the source — nothing upstream to pull
        if (repo.mirrorPath.trimmed().isEmpty() || !QDir(repo.mirrorPath).exists())
            continue; // no local mirror yet; the periodic clone handles the first
        // Group every node's mirror of this repo by its shared upstream identity
        // (with a clone-name / legacy fallback for older peers), exactly as
        // loadMirrorNodesPanel does.
        const QString canonical =
            catalogOwner(repo) + "/" +
            repoSegment(repo.name, QStringLiteral("repository"));
        const QString source = repoSegment(repo.owner, QStringLiteral("owner")) +
                               "/" + repoSegment(repo.name, QStringLiteral("repository"));
        const QString legacy = repo.owner + "/" + repo.name;
        bool behind = false;
        for (const MemberInfo &node : std::as_const(m_homeRoster)) {
            if (node.self || !node.online)
                continue;
            for (const MirrorAdvert &m : node.mirrorDetails) {
                if (m.source != source && m.ownerName != canonical &&
                    m.ownerName != legacy)
                    continue;
                // A peer advertises a commit our mirror lacks → we are behind.
                if (!m.commit.isEmpty() &&
                    !runGitCapture(repo.mirrorPath,
                                   {QStringLiteral("cat-file"), QStringLiteral("-e"),
                                    m.commit + QStringLiteral("^{commit}")},
                                   nullptr, nullptr))
                    behind = true;
                break; // one advert per node for this repo
            }
            if (behind)
                break;
        }
        if (behind)
            syncRepository(i, /*quiet=*/true);
    }
}

void MainWindow::propagateRepoUpdate(int index)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(index);
    // Only the node holding the working copy is the source of truth that can
    // push its mirror forward; previews and pure mirrors just pull.
    if (repo.previewOnly || repo.localPath.trimmed().isEmpty())
        return;
    if (m_syncingRepos.contains(index))
        return;
    // Repaint the "Sync" button's pending count right away rather than waiting on
    // the mirror-fetch round-trip below (prep thread + fetch subprocess +
    // housekeeping thread) to reach refreshRepositoryList's updateRepoPushButton()
    // call: that left the button visibly lagging the "N commits not yet synced"
    // banner, which loadCommits() already paints synchronously the instant a
    // commit lands.
    if (index == m_repoDetailIndex)
        updateRepoPushButton();
    // syncRepository fetches the bare mirror from the local working copy, so the
    // just-committed issue/PR lands in the mirror. On a detected change it
    // refreshes the open detail (updating the Issues/PR counts) and broadcasts
    // notifyMirrorUpdated, which mirroring peers act on via onPeerMirrorUpdated —
    // converging everyone in seconds rather than at the next 5-minute tick.
    syncRepository(index, /*quiet=*/true);
    // The mirror fetch above is asynchronous; until it finishes our working copy
    // is ahead of the bare mirror we serve. Refresh the Mirror nodes panel now so
    // it surfaces the pending "↑N to push" state the instant the comment/commit
    // lands, rather than only after the fetch completes — but only while that panel
    // is actually on screen. It shells several synchronous git reads (mirror
    // HEAD/commit/size, issue count, the ahead-count walk) that would lag the commit
    // for nothing when the user is on another tab; switching to the tab reloads it
    // (see the repo-detail tab handler), so a hidden panel stays correct.
    if (index == m_repoDetailIndex && m_mirrorNodesTable &&
        m_mirrorNodesTable->isVisible())
        loadMirrorNodesPanel();
}

void MainWindow::onPeerMirrorUpdated(const QString &ownerName,
                                     const QString &peerName)
{
    // Only surface it if we keep a real mirror of this repo (browse-only
    // previews don't count) — otherwise the peer's update isn't relevant here.
    // Match on the canonical advertised owner/name (the same string peers send)
    // so a repo we host under catalogOwner still lines up with the notification.
    int matchIndex = -1;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.previewOnly)
            continue;
        const QString canonical =
            catalogOwner(repo) + "/" +
            repoSegment(repo.name, QStringLiteral("repository"));
        if (canonical == ownerName || (repo.owner + "/" + repo.name) == ownerName) {
            matchIndex = i;
            break;
        }
    }
    if (matchIndex < 0)
        return;

    const QString who =
        peerName.trimmed().isEmpty() ? QStringLiteral("A peer") : peerName.trimmed();
    const QString msg = who + " updated the mirror of " + ownerName +
                        " from its source.";
    logSystem(msg);
    flashMessage(msg);

    // Converge promptly: pull the peer's advance into our own mirror now instead
    // of waiting for the next 5-minute auto-sync. This fetches refs/heads/* and
    // refs/tags/*, so issues and pull requests (which live on refs/heads) come
    // along with the code. Quiet so it doesn't spam unless something changed.
    if (!m_syncingRepos.contains(matchIndex))
        syncRepository(matchIndex, /*quiet=*/true);
    if (notifyEnabled(kMirrorUpdateAlertSetting) && m_trayIcon &&
        QSystemTrayIcon::supportsMessages())
        m_trayIcon->showMessage("ForkMesh — mirror updated", msg,
                                QSystemTrayIcon::Information, 6000);
}

void MainWindow::scanRepoMentionsFor(const RepositoryRecord &repo)
{
    // Only meaningful once we have a handle to match "@name" against.
    if (!isValidNodeName(accountNameFromInput(m_userName, QString())))
        return;
    if (repo.owner.isEmpty() || repo.name.isEmpty())
        return;

    const QString repoKey = repo.owner + "/" + repo.name;

    // Loading every issue, pull request and commit-comment thread off disk (each a
    // parse of many small files) is the heavy part: on a large repo — the flagship
    // forkmesh project runs to hundreds of issues/PRs — it froze the UI every time
    // a sync or inbox drain finished, which is what fired this scan. Do that I/O on
    // a worker thread, then match @mentions and raise notifications back on the main
    // thread. The stores are copied by value and only read on the worker (no event
    // signing), the same off-thread pattern deleteIssue uses for its git work.
    if (m_mentionScanInFlight.contains(repoKey))
        return; // a scan for this repo is already loading; don't double-notify
    m_mentionScanInFlight.insert(repoKey);

    auto loadedIssues = std::make_shared<QList<Issue>>();
    auto loadedPulls = std::make_shared<QList<PullRequest>>();
    auto loadedComments =
        std::make_shared<QList<QPair<QString, QList<CommitComment>>>>();
    IssueStore issueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity,
                          m_userName);
    PullStore pullStore(repo.localPath, repo.mirrorPath, &m_profileIdentity,
                        m_userName);
    CommitCommentStore commentStore(repo.localPath, repo.mirrorPath,
                                    &m_profileIdentity, m_userName);
    QThread *worker = QThread::create(
        [issueStore, pullStore, commentStore, loadedIssues, loadedPulls,
         loadedComments]() mutable {
            *loadedIssues = issueStore.loadAll();
            *loadedPulls = pullStore.loadAll();
            *loadedComments = commentStore.loadAll();
        });
    connect(worker, &QThread::finished, this,
            [this, worker, repo, repoKey, loadedIssues, loadedPulls,
             loadedComments]() {
                worker->deleteLater();
                m_mentionScanInFlight.remove(repoKey);
                applyRepoMentions(repo, *loadedIssues, *loadedPulls,
                                  *loadedComments);
            });
    worker->start();
}

void MainWindow::applyRepoMentions(
    const RepositoryRecord &repo, const QList<Issue> &allIssues,
    const QList<PullRequest> &allPulls,
    const QList<QPair<QString, QList<CommitComment>>> &allCommitComments)
{
    const QString repoKey = repo.owner + "/" + repo.name;
    QSettings settings;
    const QStringList seenList =
        settings.value(QStringLiteral("mentions/seen")).toStringList();
    QSet<QString> seen(seenList.cbegin(), seenList.cend());
    QStringList seededRepos =
        settings.value(QStringLiteral("mentions/seededRepos")).toStringList();
    // The first time we scan a repo, silently record its existing mentions so a
    // fresh clone's back-history doesn't fire a flood of stale alerts; only
    // mentions that appear afterwards notify.
    const bool seeding = !seededRepos.contains(repoKey);
    const QString myKey = m_profileIdentity.publicKey();

    bool dirty = false;
    // Raise (and record) one mention alert. stableKey dedups across scans;
    // humanLocator is the "issue #12" / "PR #4" / "commit abc1234" phrase shown.
    auto notifyMention = [&](const QString &stableKey, const QString &authorKey,
                             const QString &authorName, const QString &text,
                             const QString &humanLocator,
                             const NotificationLink &link) {
        if (text.isEmpty() || !textMentionsNodeName(text, m_userName))
            return;
        if (!authorKey.isEmpty() && authorKey == myKey)
            return; // your own writing doesn't mention "you"
        if (seen.contains(stableKey))
            return;
        seen.insert(stableKey);
        dirty = true;
        if (seeding)
            return; // recorded, but no alert for pre-existing history
        const QString who = authorName.trimmed().isEmpty()
                                ? QStringLiteral("Someone")
                                : authorName.trimmed();
        QString snippet = text.simplified();
        if (snippet.size() > 160)
            snippet = snippet.left(157) + QString::fromUtf8("\xE2\x80\xA6");
        const QString body =
            QString::fromUtf8("%1 mentioned you in %2 %3: \xE2\x80\x9C%4\xE2\x80\x9D")
                .arg(who, repoKey, humanLocator, snippet);
        if (notifyEnabled(kMentionAlertSetting)) {
            QApplication::alert(this, 0);
            postNotification(who + QStringLiteral(" mentioned you"), body);
        }
        addNotification(QStringLiteral("Mention"), body, false, link);
    };
    // Issues/PRs: preserve the existing "<repo>#<kind><number>:<eventId>" dedup
    // key (so upgrades don't re-fire historical mentions) and "<kind> #<n>"
    // wording exactly.
    auto consider = [&](const QString &kind, int number, const QString &eventId,
                        const QString &authorKey, const QString &authorName,
                        const QString &text, const QString &context) {
        const QString key = QStringLiteral("%1#%2%3:%4")
                                .arg(repoKey, kind)
                                .arg(number)
                                .arg(eventId);
        NotificationLink link;
        link.kind = kind; // "issue" | "pull" — matches openNotificationLink
        link.owner = repo.owner;
        link.name = repo.name;
        link.number = number;
        notifyMention(key, authorKey, authorName, text,
                      context + QStringLiteral("#") + QString::number(number),
                      link);
    };

    for (const Issue &issue : allIssues) {
        for (const IssueEvent &ev : issue.events) {
            if (ev.type != QLatin1String("open") &&
                ev.type != QLatin1String("comment") &&
                ev.type != QLatin1String("edit"))
                continue;
            const QString text = (ev.title + QStringLiteral("\n") + ev.body).trimmed();
            consider(QStringLiteral("issue"), issue.number, ev.id, ev.author,
                     ev.authorName, text, QStringLiteral("issue "));
        }
    }

    for (const PullRequest &pr : allPulls) {
        const QString openText =
            (pr.title + QStringLiteral("\n") + pr.description).trimmed();
        consider(QStringLiteral("pull"), pr.number, QStringLiteral("open"), pr.author,
                 pr.authorName, openText, QStringLiteral("PR "));
        for (const PullEvent &ev : pr.events)
            consider(QStringLiteral("pull"), pr.number, ev.id, ev.author,
                     ev.authorName, ev.body, QStringLiteral("PR "));
    }

    // Commit comments: per-commit conversations keyed by SHA (no number). Scan
    // every commented commit so an @mention in a commit thread notifies too.
    for (const auto &thread : allCommitComments) {
        const QString &sha = thread.first;
        for (const CommitComment &c : thread.second) {
            NotificationLink link;
            link.kind = QStringLiteral("commit");
            link.owner = repo.owner;
            link.name = repo.name;
            link.ref = sha;
            notifyMention(QStringLiteral("%1#commit%2:%3").arg(repoKey, sha, c.id),
                          c.author, c.authorName, c.body,
                          QStringLiteral("commit %1").arg(sha.left(8)), link);
        }
    }

    if (dirty) {
        const QStringList keys(seen.cbegin(), seen.cend());
        settings.setValue(QStringLiteral("mentions/seen"), keys);
    }
    if (seeding) {
        seededRepos.append(repoKey);
        settings.setValue(QStringLiteral("mentions/seededRepos"), seededRepos);
    }
}

void MainWindow::syncRepository(int index, bool quiet)
{
    if (index < 0 || index >= m_repositories.size() ||
        m_syncingRepos.contains(index))
        return;

    RepositoryRecord &repo = m_repositories[index];
    const bool preview = repo.previewOnly;
    if (!QDir().mkpath(QFileInfo(repo.mirrorPath).absolutePath())) {
        if (!quiet)
            QMessageBox::warning(this, "Sync repository",
                                 "Could not create " +
                                     QFileInfo(repo.mirrorPath).absolutePath());
        return;
    }

    const bool hasMirror = QDir(repo.mirrorPath).exists();
    const QString source = repositorySource(repo);

    // A repository we publish and host ourselves, with no separate upstream
    // working copy, IS the source of truth. Re-fetching it would loop back
    // through the relay to our own host tunnel and fail (HTTP 5xx), so there is
    // nothing to sync.
    if (!preview && hasMirror && repo.publishToNetwork &&
        repo.localPath.trimmed().isEmpty() && repo.owner == accountOwner()) {
        if (!quiet)
            flashMessage(QStringLiteral("Nothing to sync for %1/%2 — this node "
                                        "hosts it directly.")
                             .arg(repo.owner, repo.name));
        return;
    }

    // Mirror only the stable namespaces (branches + tags). Tool-managed refs
    // like refs/codex/* churn constantly on active repos: a client that wants a
    // ref which vanished between the advertisement and the pack negotiation gets
    // "not our ref" and the whole upload-pack fails (HTTP 502 -> "host
    // temporarily unavailable"). Issues and pull requests live in refs/heads, so
    // limiting to heads/tags keeps everything we serve while dropping the churn.
    static const QStringList kStableRefspecs = {
        QStringLiteral("+refs/heads/*:refs/heads/*"),
        QStringLiteral("+refs/tags/*:refs/tags/*")};
    // For our own private repo hosted through the mainnode, clone/fetch must carry
    // an owner-key-signed view token; viewAuthGitArgs returns the "-c
    // http.extraHeader=..." prefix (empty for public repos or non-mainnode sources)
    // generated fresh so the short-lived token never goes stale in stored config.
    const QStringList authArgs = viewAuthGitArgs(repo, source);
    const QStringList args =
        authArgs +
        (hasMirror ? QStringList{"-C", repo.mirrorPath, "fetch", "--prune",
                                "origin"} +
                        kStableRefspecs
                  : QStringList{"clone", "--bare", source, repo.mirrorPath});
    const QString mirrorPath = repo.mirrorPath;

    // Flag the repo "syncing" and reflect it in the UI right away — before any git
    // subprocess runs — so clicking "Sync" flips the button to "Syncing…"
    // instantly and never blocks the GUI thread. The insert also guards
    // re-entrancy so a concurrent auto-sync can't start a second fetch on this repo.
    m_syncingRepos.insert(index);
    refreshRepositoryList();
    if (!quiet) {
        const QString prefix =
            preview ? QStringLiteral("Preview cache: ")
                    : QStringLiteral("Mirror: ");
        logSystem(prefix +
                  (hasMirror ? QStringLiteral("fetching ") : QStringLiteral("cloning ")) +
                  repo.owner + "/" + repo.name + " from " + source + ".");
    }

    // Read the mirror's pre-fetch refs digest + HEAD and re-point origin at the
    // live source off the GUI thread. Each is a git subprocess that blocks for up
    // to 5s on a busy mirror (the for-each-ref digest over hundreds of issue/PR
    // refs is the slow one), and running them here froze the window every time a
    // sync *started* — the mirror image of the post-fetch housekeeping below,
    // which already runs on a worker for exactly this reason. The worker only
    // touches the mirror through path strings (never m_repositories or a widget);
    // the async fetch is kicked off back on the main thread once it finishes.
    auto beforeDigest = std::make_shared<QString>();
    auto beforeHeadCommit = std::make_shared<QString>();
    QThread *prep = QThread::create(
        [mirrorPath, source, hasMirror, beforeDigest, beforeHeadCommit] {
            *beforeDigest = mirrorRefsDigest(mirrorPath);
            *beforeHeadCommit =
                mirrorBranchCommit(mirrorPath, mirrorHeadBranch(mirrorPath));
            // Track the live source: an owned repo with a local working copy
            // should fetch from that copy, not from a stale relay URL baked into
            // origin at clone time (which can return HTTP 5xx through the host
            // tunnel).
            if (hasMirror && !source.isEmpty())
                runGitCapture(mirrorPath,
                              {QStringLiteral("remote"), QStringLiteral("set-url"),
                               QStringLiteral("origin"), source},
                              nullptr, nullptr);
        });
    connect(prep, &QThread::finished, this,
            [this, prep, index, quiet, hasMirror, args, beforeDigest,
             beforeHeadCommit] {
                prep->deleteLater();
                if (index < 0 || index >= m_repositories.size()) {
                    m_syncingRepos.remove(index);
                    refreshRepositoryList();
                    return;
                }
                startSyncFetch(index, quiet, hasMirror, args, *beforeDigest,
                               *beforeHeadCommit);
            });
    prep->start();
}

void MainWindow::startSyncFetch(int index, bool quiet, bool hasMirror,
                                const QStringList &args,
                                const QString &beforeDigest,
                                const QString &beforeHeadCommit)
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index, quiet, beforeDigest, beforeHeadCommit,
             hasMirror](
                int exitCode, QProcess::ExitStatus) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();

                if (index < 0 || index >= m_repositories.size()) {
                    m_syncingRepos.remove(index);
                    refreshRepositoryList();
                    return;
                }

                RepositoryRecord &repo = m_repositories[index];
                if (exitCode == 0) {
                    // The post-fetch mirror housekeeping all shells out to git:
                    // repairing the bare repo's symbolic HEAD (so smart-HTTP clones
                    // check out real content), pruning churning tool refs left by
                    // older --mirror clones (so peers don't hit "not our ref"), and
                    // reading the refs back to tell whether the owner's repo
                    // actually changed. On a large or busy mirror that is hundreds
                    // of milliseconds of subprocess spawns, and running it here on
                    // the GUI thread froze the window every time a sync finished.
                    // Do that git work on a worker thread — it only touches the
                    // on-disk mirror through these path strings, never m_repositories
                    // or any widget — then apply the results back on the main thread,
                    // the same off-thread pattern scanRepoMentionsFor/deleteIssue
                    // use. The repo stays flagged "syncing" until the housekeeping
                    // finishes so a concurrent auto-sync can't race it on the same
                    // mirror.
                    const QString mirrorPath = repo.mirrorPath;
                    const QString localPath = repo.localPath;
                    auto afterDigest = std::make_shared<QString>();
                    auto headBranch = std::make_shared<QString>();
                    auto headCommit = std::make_shared<QString>();
                    QThread *worker = QThread::create(
                        [mirrorPath, localPath, afterDigest, headBranch,
                         headCommit] {
                            repairMirrorHead(mirrorPath, localPath);
                            pruneNonStableMirrorRefs(mirrorPath);
                            *afterDigest = mirrorRefsDigest(mirrorPath);
                            *headBranch = mirrorHeadBranch(mirrorPath);
                            *headCommit =
                                mirrorBranchCommit(mirrorPath, *headBranch);
                        });
                    connect(worker, &QThread::finished, this,
                            [this, worker, index, quiet, hasMirror, beforeDigest,
                             beforeHeadCommit, afterDigest, headBranch,
                             headCommit] {
                        worker->deleteLater();
                        m_syncingRepos.remove(index);
                        if (index < 0 || index >= m_repositories.size()) {
                            refreshRepositoryList();
                            return;
                        }
                        RepositoryRecord &repo = m_repositories[index];
                        const bool stillPreview = repo.previewOnly;
                        // Did the owner's repo actually change?
                        const bool changed =
                            !hasMirror || *afterDigest != beforeDigest;
                        repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                        if (!stillPreview)
                            saveRepositories();
                        refreshRepositoryList();
                        // Now that the bare mirror exists, (re)install the push
                        // hook so local pushes are detected (actions + refresh).
                        if (!stillPreview)
                            ensurePushHook(repo);
                        // A fresh install's first sync of the flagship repo (flagged
                        // by ensureFlagshipRepo, adhoc #113): open it now that the
                        // clone landed, instead of leaving the user on an empty list.
                        if (!stillPreview && !m_pendingAutoOpenRepoKey.isEmpty() &&
                            m_pendingAutoOpenRepoKey.compare(
                                repo.owner + "/" + repo.name, Qt::CaseInsensitive) == 0) {
                            m_pendingAutoOpenRepoKey.clear();
                            // On a fresh install, land on the #welcome chat, not the
                            // Code view. Just select the repo internally (so the repo
                            // switcher shows "forkmesh") and refresh the UI; don't open
                            // the detail view which would load and show the Agents tab.
                            m_repoDetailIndex = index;
                            refreshRepositoryList();
                            QTimer::singleShot(0, this, [this] {
                                showChatView();
                                switchConversation(kWelcomeChannel);
                            });
                        }
                        if (changed && hasMirror && !stillPreview &&
                            m_actionStore && !headBranch->isEmpty() &&
                            !headCommit->isEmpty() &&
                            *headCommit != beforeHeadCommit) {
                            enqueuePushEvent(repo.owner, repo.name, *headCommit,
                                             "refs/heads/" + *headBranch);
                        }
                        // If this repo's detail is open, reflect the new commits.
                        if (changed && index == m_repoDetailIndex)
                            refreshOpenRepoDetail();
                        // Newly-synced issues/PRs may @mention the local user.
                        if (changed && !stillPreview)
                            scanRepoMentionsFor(repo);
                        // Tell connected peers that also mirror this repo that it
                        // advanced from its source of truth. Only for real mirrors
                        // that already existed (an actual update, not a first clone).
                        if (changed && hasMirror && !stillPreview && m_backend)
                            m_backend->notifyMirrorUpdated(
                                catalogOwner(repo) + "/" +
                                repoSegment(repo.name,
                                            QStringLiteral("repository")));
                        // Quiet auto-syncs only speak up when something changed.
                        if (!quiet || changed) {
                            logSystem((stillPreview ? QStringLiteral("Preview cache: cached ")
                                                    : QStringLiteral("Mirror: synced ")) +
                                      repo.owner + "/" + repo.name + " into " +
                                      repo.mirrorPath + ".");
                        }
                        if (!quiet) {
                            flashMessage(stillPreview
                                             ? "Cached preview for " + repo.owner + "/" +
                                                   repo.name +
                                                   (changed ? QString()
                                                            : " (already up to date)")
                                             : "Synced " + repo.owner + "/" + repo.name +
                                                   (changed ? QString()
                                                            : " (already up to date)"));
                        }
                        if (!stillPreview && repo.publishToNetwork &&
                            (changed || !quiet)) {
                            publishRepository(index, false);
                            // Serve this repo's files live to the web now that a
                            // mirror exists (pure live tunnel, nothing uploaded).
                            startRepoHosts();
                        }
                        // Also mirror the repo's release artifacts: pull any binary
                        // blobs we don't yet hold into our content-addressed store so
                        // this node can serve downloads too, not just clones (adhoc
                        // #77). Only on an actual change or a manual sync, so quiet
                        // auto-syncs don't re-scan an up-to-date store every tick.
                        if (!stillPreview && hasMirror && (changed || !quiet))
                            replicateReleaseArtifacts(index);
                    });
                    worker->start();
                } else {
                    m_syncingRepos.remove(index);
                    refreshRepositoryList();
                    // Relay/host hiccups (HTTP 5xx, RPC failed, connection
                    // resets) and truncated packs from the streaming clone
                    // tunnel are transient: the host serving this repo is
                    // momentarily unavailable and the next sync will retry. Log
                    // them quietly rather than raising a persistent red error.
                    const bool transient = isTransientSyncError(errors);
                    logSystem((repo.previewOnly ? QStringLiteral("Preview cache: sync failed for ")
                                                : QStringLiteral("Mirror: sync failed for ")) +
                              repo.owner + "/" +
                              repo.name +
                              (transient ? QStringLiteral(" (host temporarily "
                                                          "unavailable): ")
                                         : QStringLiteral(": ")) +
                              errors.right(300));
                    if (!quiet && transient) {
                        flashMessage(QStringLiteral("Sync deferred for %1/%2 — host "
                                                    "temporarily unavailable.")
                                         .arg(repo.owner, repo.name),
                                     /*error=*/false);
                    } else if (!quiet) {
                        flashMessage(
                            (repo.previewOnly ? QStringLiteral("Preview failed for ")
                                              : QStringLiteral("Sync failed for ")) +
                                repo.owner + "/" + repo.name +
                                (errors.isEmpty() ? QString() :
                                                    ": " + errors.right(160)),
                            /*error=*/true);
                    }
                }
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, index, quiet] {
                process->deleteLater();
                m_syncingRepos.remove(index);
                refreshRepositoryList();
                if (!quiet)
                    flashMessage(
                        "Could not run git. Install Git and try again.",
                        /*error=*/true);
            });
    process->start("git", args);
}

// ------------------------------------------------------------------ settings

void MainWindow::onProfileNameChanged(const QString &name)
{
    const QString trimmed = accountNameFromInput(name, QString());
    if (trimmed.isEmpty()) {
        if (m_settingsNameEdit)
            m_settingsNameEdit->setText(m_userName);
        return;
    }
    if (m_settingsNameEdit && m_settingsNameEdit->text() != trimmed)
        m_settingsNameEdit->setText(trimmed);
    if (m_nameEdit && m_nameEdit->text() != trimmed)
        m_nameEdit->setText(trimmed);
    if (trimmed == m_userName)
        return;
    const QString oldOwner =
        accountNameFromInput(m_accountName.isEmpty() ? m_userName : m_accountName,
                             QStringLiteral("owner"));
    m_userName = trimmed;
    m_accountName = trimmed;
    saveProfileName(trimmed);
    migrateReposForProfileName(oldOwner, trimmed);
    if (m_backend)
        m_backend->setUserName(trimmed);
    refreshSettingsEmailVerifiedBadge();
    refreshRepositoryList();
    logSystem("Name changed to " + trimmed + ".");
}

void MainWindow::onAvatarChosen(const QByteArray &pngData)
{
    m_userAvatar = pngData;
    QSettings().setValue(kAvatarSetting, pngData);
    if (m_backend)
        m_backend->setAvatar(pngData);
    updateAvatarButton();
}

void MainWindow::logout()
{
    // Drop the signed-in account (admin/heartbeat state) so the user can log
    // back in, then tear the session down to the setup screen.
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    if (m_adminPollTimer)
        m_adminPollTimer->stop();
    m_accountAuthenticated = false;
    m_accountTier = QStringLiteral("free");
    m_accountSolanaVerified = false;
    m_isAdmin = false;
    m_seenPendingUsers.clear();
    m_accountName.clear();
    QSettings().remove(kAccountNameSetting);
    refreshSettingsEmailVerifiedBadge();
    leaveSession();
}

void MainWindow::uninstallForkMesh()
{
    const QString sourceDir = QStringLiteral(FORKMESH_SOURCE_DIR);
    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);

    // Every directory ForkMesh owns: per-app data, local data, cache, the
    // QSettings config dir, and — for a from-checkout build — the source/build
    // tree the running binary lives in.
    QStringList dirs;
    auto addDir = [&dirs](const QString &d) {
        if (!d.isEmpty() && QDir(d).exists() && !dirs.contains(d))
            dirs << QDir(d).absolutePath();
    };
    addDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    addDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    addDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    addDir(QFileInfo(QSettings().fileName()).absolutePath());
    addDir(sourceDir);
    // The IDE-extension handoff dir and the curl-installer's source checkout
    // (lowercase "forkmesh") aren't covered by the standard locations above.
    addDir(QDir::homePath() + QStringLiteral("/.forkmesh"));
    addDir(dataHome + QStringLiteral("/forkmesh"));

    // Loose files: the login-autostart entry, the installed desktop launcher,
    // the curl-installer binary, and every hicolor icon bucket install.sh wrote.
    QStringList files;
    auto addFile = [&files](const QString &f) {
        if (!f.isEmpty() && QFileInfo::exists(f) && !files.contains(f))
            files << f;
    };
    addFile(autostartDesktopPath());
    addFile(QDir::homePath() + QStringLiteral("/.local/bin/forkmesh"));
    addFile(dataHome + QStringLiteral("/applications/forkmesh.desktop"));
    addFile(dataHome + QStringLiteral("/icons/forkmesh.png"));
    QDirIterator iconIt(dataHome + QStringLiteral("/icons/hicolor"),
                        {QStringLiteral("forkmesh.png")}, QDir::Files,
                        QDirIterator::Subdirectories);
    while (iconIt.hasNext())
        addFile(iconIt.next());

    // ---- confirmation: a detailed warning, then a typed phrase -------------
    QString detail = QStringLiteral(
        "This permanently and irreversibly erases ForkMesh from this computer, "
        "including:\n\n"
        "  •  every mirrored repository\n"
        "  •  this node's identity key (your account cannot be recovered)\n"
        "  •  all chat history, settings and caches\n"
        "  •  the desktop launcher and icons\n"
        "  •  the ForkMesh program files\n\nFolders removed:\n");
    for (const QString &d : std::as_const(dirs))
        detail += "    " + d + "/\n";
    detail += QStringLiteral("\nForkMesh will quit when it is done.");

    QMessageBox box(QMessageBox::Warning, QStringLiteral("Uninstall ForkMesh"),
                    detail, QMessageBox::Cancel, this);
    QPushButton *go =
        box.addButton(QStringLiteral("Uninstall…"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != go)
        return;

    bool ok = false;
    const QString typed = QInputDialog::getText(
        this, QStringLiteral("Confirm uninstall"),
        QStringLiteral("Type DELETE to permanently erase ForkMesh:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || typed.trimmed().compare(QStringLiteral("DELETE"),
                                       Qt::CaseInsensitive) != 0)
        return;

    // ---- stop live services so nothing rewrites files during the wipe -----
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    if (m_adminPollTimer)
        m_adminPollTimer->stop();
    stopRepoHosts();
    if (m_backend) {
        m_backend->disconnect(this);
        m_backend->shutdown();
        m_backend->deleteLater();
        m_backend = nullptr;
    }

    const QStringList all = dirs + files;
#if defined(Q_OS_UNIX)
    // The source/build tree holds the binary we're running from, and the
    // settings file may still be open, so hand the whole removal to a detached
    // shell that waits for us to exit first. POSIX keeps a deleted-but-open
    // file alive until close, but a detached `rm` after we quit is the robust,
    // cross-shell way to be sure every byte is gone.
    auto shQuote = [](const QString &s) {
        return QLatin1Char('\'') + QString(s).replace(QStringLiteral("'"),
                                                      QStringLiteral("'\\''")) +
               QLatin1Char('\'');
    };
    QStringList quoted;
    for (const QString &p : all)
        quoted << shQuote(p);
    // Leave the directory tree we're about to delete before quitting.
    QDir::setCurrent(QDir::homePath());
    const bool spawned = QProcess::startDetached(
        QStringLiteral("/bin/sh"),
        {QStringLiteral("-c"),
         QStringLiteral("sleep 1; rm -rf ") + quoted.join(QLatin1Char(' '))});
    if (!spawned) {
        // No shell to hand off to: delete in-process as a best effort.
        for (const QString &f : std::as_const(files))
            QFile::remove(f);
        for (const QString &d : std::as_const(dirs))
            QDir(d).removeRecursively();
    }
#else
    for (const QString &f : std::as_const(files))
        QFile::remove(f);
    for (const QString &d : std::as_const(dirs))
        QDir(d).removeRecursively();
#endif

    QCoreApplication::exit(0);
}

