// MainWindowReleases: MainWindow feature methods, split out of MainWindow.cpp.
// Releases panel and mirror nodes.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"

using namespace forkmesh::ui;

namespace {

// Bake a release tag's version into the Qt client's source version — the
// project(ForkMesh VERSION X.Y.Z ...) line in qt_client/CMakeLists.txt that
// every "ForkMesh v" FORKMESH_VERSION display reads — and commit it, so cutting
// release vX.Y.Z immediately updates the version the app reports.
//
// Called BEFORE the release tag is created (see promptNewRelease), so the
// tagged commit itself declares the release version: building straight from the
// tag reports X.Y.Z. Previously the header was only bumped by
// ActionRunner::landVersionHeader AFTER a build node finished publishing the
// binary, which (a) left the tagged commit reading the previous version and
// (b) never happened at all if no build node ran. The release.yml workflow's
// in-worktree sed and -DFORKMESH_VERSION_OVERRIDE stamp still cover the published binary;
// landVersionHeader becomes a no-op once the header is already in sync here.
//
// Only a clean MAJOR.MINOR.PATCH tag bumps the header; a pre-release (-rc1) or
// non-semver tag is left alone so the source version never jumps ahead to a
// version that hasn't shipped. Returns true when a bump was committed.
bool bumpQtVersionForRelease(const QString &workTree, const QString &tag)
{
    if (workTree.isEmpty())
        return false;
    QString version = tag.trimmed();
    if (version.startsWith(QLatin1Char('v')))
        version = version.mid(1);
    static const QRegularExpression semver(
        QStringLiteral("\\A[0-9]+\\.[0-9]+\\.[0-9]+\\z"));
    if (!semver.match(version).hasMatch())
        return false;

    const QString rel = QStringLiteral("qt_client/CMakeLists.txt");

    // Don't touch a header the user is already editing — a path-scoped commit
    // would otherwise sweep their pending edits in with the version bump.
    QByteArray pending;
    if (!runGitCapture(workTree, {"status", "--porcelain", "--", rel}, &pending,
                       nullptr) ||
        !QString::fromUtf8(pending).trimmed().isEmpty())
        return false;

    QFile file(workTree + QLatin1Char('/') + rel);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QString text = QString::fromUtf8(file.readAll());
    file.close();

    // The version sits on a single line: project(ForkMesh VERSION X.Y.Z ...).
    static const QRegularExpression versionLine(
        QStringLiteral("^(project\\(ForkMesh VERSION )([0-9]+\\.[0-9]+\\.[0-9]+)"),
        QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = versionLine.match(text);
    if (!m.hasMatch() || m.captured(2) == version)
        return false; // header missing here, or already at the release version

    text.replace(m.capturedStart(2), m.capturedLength(2), version);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    const bool wrote = file.write(text.toUtf8()) >= 0;
    file.close();
    if (!wrote)
        return false;

    // Commit just the version header, under the same release-bot identity
    // ActionRunner uses when it lands release metadata.
    return runGitCapture(
        workTree,
        {"-c", QStringLiteral("user.email=actions@forkmesh.local"), "-c",
         QStringLiteral("user.name=ForkMesh Actions"), "commit", "-m",
         QStringLiteral("release: bump version header to %1").arg(version), "--",
         rel},
        nullptr, nullptr);
}

} // namespace

// ---- Releases panel --------------------------------------------------------

QWidget *MainWindow::buildReleasesTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel("Releases");
    heading->setObjectName("channelTitle");
    m_releasesSummary = new QLabel;
    m_releasesSummary->setObjectName("statusLine");
    auto *newButton = new QPushButton("Draft a release");
    newButton->setObjectName("primaryButton");
    newButton->setCursor(Qt::PointingHandCursor);
    setOcticon(newButton, "tag", 16);
    connect(newButton, &QPushButton::clicked, this, &MainWindow::promptNewRelease);
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::loadReleasesPanel);
    addRefreshSpin(refreshButton);
    headerRow->addWidget(heading);
    headerRow->addWidget(m_releasesSummary);
    headerRow->addStretch();
    headerRow->addWidget(refreshButton);
    headerRow->addWidget(newButton);
    layout->addLayout(headerRow);

    m_releasesTable = new QTableWidget(0, 5);
    m_releasesTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_releasesTable);
    m_releasesTable->setHorizontalHeaderLabels({"Tag", "Released", "Release notes", "Artifacts", ""});
    m_releasesTable->verticalHeader()->setVisible(false);
    m_releasesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_releasesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_releasesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_releasesTable->setShowGrid(false);
    m_releasesTable->setWordWrap(false);
    QHeaderView *rh = m_releasesTable->horizontalHeader();
    rh->setHighlightSections(false);
    rh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(2, QHeaderView::Stretch);
    rh->setSectionResizeMode(3, QHeaderView::Stretch);
    rh->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_releasesTable);
    // itemActivated (rather than cellDoubleClicked) so pressing Enter on the
    // keyboard-focused row opens the release too, matching the arrow-key
    // navigation the tab supports (adhoc #183). Opening a row shows the
    // release's full notes and the diff since the previous release (issue #284);
    // a button there still browses the repo at the tag.
    connect(m_releasesTable, &QTableWidget::itemActivated, this,
            [this](QTableWidgetItem *item) {
                QTableWidgetItem *it =
                    item ? m_releasesTable->item(item->row(), 0) : nullptr;
                if (it && !it->text().trimmed().isEmpty())
                    showReleaseDetail(it->text().trimmed());
            });
    layout->addWidget(m_releasesTable, 1);
    return page;
}

void MainWindow::loadReleasesPanel()
{
    if (!m_releasesTable)
        return;
    TableRepaintGuard repaintGuard(m_releasesTable);
    m_releasesTable->setRowCount(0);
    const QString dir = repoGitDir();
    const bool writable = repoHasWorkingTree();

    // Release artifacts are published under releases/<channel>/release.json (the
    // channel is usually "latest", NOT the tag name — see releases/README.md and
    // tools/forkmesh-release-publish.sh). Each manifest records the tag it was cut
    // from in its "tag" field, so scan every channel manifest and key the asset
    // names by that tag. The Artifacts column then looks up each release row by
    // tag, instead of probing a releases/<tag>/ path that the publisher never
    // writes (which left the column always empty).
    // Each artifact name links to its live download on the relay's
    // content-addressed release endpoint (the exact URL install.sh fetches:
    // <relay>/api/repo/<owner>/<name>/releases/blob/sha256/<blob_sha256>), so a
    // release row's binaries can be pulled straight from the served mirror.
    QString relayHost =
        serverHost(QSettings().value(kServerUrlSetting).toString().trimmed());
    if (relayHost.isEmpty())
        relayHost = serverHost(kDefaultServerUrl);
    const bool localRelay = relayHost.startsWith(QStringLiteral("127.0.0.1")) ||
                            relayHost.startsWith(QStringLiteral("localhost"));
    const QString relayBase =
        relayHost.isEmpty()
            ? QString()
            : QStringLiteral("%1://%2").arg(
                  localRelay ? QStringLiteral("http") : QStringLiteral("https"),
                  relayHost);
    static const QRegularExpression sha256Re(QStringLiteral("\\A[0-9a-f]{64}\\z"));

    // Artifacts column holds rich-text links, so key it on the rendered HTML.
    QHash<QString, QString> artifactsByTag;
    // The "latest" channel is what install.sh actually downloads as the current
    // release, regardless of which tag it was cut from. Remember its rendered
    // artifacts (and source tag) so the newest release row can surface them even
    // when that release's own build hasn't published a manifest yet.
    QString latestChannelHtml;
    QString latestChannelTag;
    if (!dir.isEmpty()) {
        const QDir releasesDir(dir + QStringLiteral("/releases"));
        const QStringList channels =
            releasesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &channel : channels) {
            QFile releaseFile(
                releasesDir.filePath(channel + QStringLiteral("/release.json")));
            if (!releaseFile.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QJsonDocument doc = QJsonDocument::fromJson(releaseFile.readAll());
            releaseFile.close();
            if (!doc.isObject())
                continue;
            const QJsonObject obj = doc.object();
            const QString manifestTag =
                obj.value(QStringLiteral("tag")).toString().trimmed();
            if (manifestTag.isEmpty())
                continue;
            // The canonical owner/repo that staged the out-of-git blob; only that
            // node serves the content-addressed download.
            const QString manifestRepo =
                obj.value(QStringLiteral("repo")).toString().trimmed();
            QStringList assetLinks;
            const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
            for (const QJsonValue &asset : assets) {
                const QJsonObject a = asset.toObject();
                const QString name = a.value(QStringLiteral("name")).toString();
                if (name.isEmpty())
                    continue;
                const QString escaped = name.toHtmlEscaped();
                const QString hash =
                    a.value(QStringLiteral("blob_sha256")).toString().trimmed();
                const bool hashValid = sha256Re.match(hash).hasMatch();
                QString entry;
                if (!relayBase.isEmpty() && !manifestRepo.isEmpty() &&
                    hashValid) {
                    const QString url =
                        QStringLiteral(
                            "%1/api/repo/%2/releases/blob/sha256/%3")
                            .arg(relayBase, manifestRepo, hash);
                    entry =
                        QStringLiteral(
                            "<a href=\"%1\" style=\"color:#58a6ff;"
                            "text-decoration:none\">%2</a>")
                            .arg(url.toHtmlEscaped(), escaped);
                } else {
                    entry = escaped;
                }
                // Show the sha256 checksum next to each artifact so it can be
                // eyeballed against the value install.sh verifies. The full
                // 64-char digest would blow out the column width, so render an
                // abbreviated form and keep the full hash in the hover tooltip.
                if (hashValid) {
                    entry += QStringLiteral(
                                 " <span title=\"sha256:%1\" "
                                 "style=\"color:#8b949e;font-family:monospace;"
                                 "font-size:11px\">sha256:%2</span>")
                                 .arg(hash, hash.left(12));
                }
                assetLinks.append(entry);
            }
            if (!assetLinks.isEmpty()) {
                const QString joined = assetLinks.join(QStringLiteral(", "));
                artifactsByTag.insert(manifestTag, joined);
                if (channel.compare(QStringLiteral("latest"),
                                    Qt::CaseInsensitive) == 0) {
                    latestChannelHtml = joined;
                    latestChannelTag = manifestTag;
                }
            }
        }
    }

    int count = 0;
    QString currentTag;
    QByteArray out;
    if (!dir.isEmpty() &&
        runGitCapture(dir,
                      {"for-each-ref", "--sort=-creatordate", "refs/tags",
                       "--format=%(refname:short)%1f"
                       "%(creatordate:format:%Y-%m-%d %H:%M)%1f"
                       "%(contents:subject)"},
                      &out, nullptr)) {
        for (const QByteArray &line : out.split('\n')) {
            const QString text = QString::fromUtf8(line);
            if (text.trimmed().isEmpty())
                continue;
            const QStringList f = text.split(QLatin1Char('\x1f'));
            if (f.isEmpty())
                continue;
            const QString tag = f.value(0).trimmed();
            if (tag.isEmpty())
                continue;
            // Tags are sorted newest-first, so the first one is the current
            // release shown at the top of the panel header (issue #226).
            if (currentTag.isEmpty())
                currentTag = tag;
            const int row = m_releasesTable->rowCount();
            m_releasesTable->insertRow(row);
            auto *tagItem = new QTableWidgetItem(tag);
            tagItem->setIcon(themedOcticon("tag", QColor("#a371f7"), 14));
            m_releasesTable->setItem(row, 0, tagItem);
            m_releasesTable->setItem(row, 1, new QTableWidgetItem(f.value(1).trimmed()));
            m_releasesTable->setItem(row, 2, new QTableWidgetItem(f.value(2).trimmed()));

            // Artifacts for this tag come from the channel manifest scanned above
            // (keyed by the manifest's own "tag" field), not a releases/<tag>/ path.
            // The asset names are rendered as live-download links, so use a
            // rich-text label cell that opens the URL in the browser on click.
            QString artifactsHtml = artifactsByTag.value(tag);
            // The newest release should always show a downloadable artifact when
            // one exists. If this top row has no manifest of its own yet (its
            // build hasn't published, so the tag-keyed lookup is empty), fall back
            // to the "latest" channel's artifact — exactly what install.sh serves
            // — annotated with the tag it was actually cut from.
            if (artifactsHtml.isEmpty() && count == 0 &&
                !latestChannelHtml.isEmpty()) {
                artifactsHtml =
                    latestChannelHtml +
                    QStringLiteral(" <span style=\"color:#8b949e;"
                                   "font-size:11px\">latest &middot; %1</span>")
                        .arg(latestChannelTag.toHtmlEscaped());
            }
            if (artifactsHtml.isEmpty()) {
                m_releasesTable->setItem(row, 3, new QTableWidgetItem(QString()));
            } else {
                auto *artifacts = new QLabel(artifactsHtml);
                artifacts->setTextFormat(Qt::RichText);
                artifacts->setOpenExternalLinks(true);
                artifacts->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
                artifacts->setContentsMargins(6, 0, 6, 0);
                artifacts->setCursor(Qt::PointingHandCursor);
                artifacts->setStyleSheet(QStringLiteral("background:transparent;"));
                m_releasesTable->setCellWidget(row, 3, artifacts);
            }

            auto *del = new QPushButton;
            del->setObjectName("issueIconButton");
            del->setFlat(true);
            del->setCursor(Qt::PointingHandCursor);
            del->setIcon(themedOcticon("trash", QColor("#f85149"), 15));
            del->setIconSize(QSize(15, 15));
            del->setToolTip(QStringLiteral("Delete tag %1").arg(tag));
            del->setEnabled(writable);
            connect(del, &QPushButton::clicked, this, [this, tag] { deleteTag(tag); });
            m_releasesTable->setCellWidget(row, 4, del);
            ++count;
        }
    }
    // Header reads "Releases  <current tag> (N)" — the current release number on
    // top, then how many releases there are in parentheses to the right (#226).
    if (m_releasesSummary) {
        if (count == 0)
            m_releasesSummary->setText(QString());
        else if (currentTag.isEmpty())
            m_releasesSummary->setText(QStringLiteral("(%1)").arg(count));
        else
            m_releasesSummary->setText(
                QStringLiteral("%1 (%2)").arg(currentTag).arg(count));
    }
    if (m_repoReleasesTab)
        m_repoReleasesTab->setText(
            QStringLiteral("Releases (%1)").arg(formatCount(count)));
    if (count == 0) {
        m_releasesTable->insertRow(0);
        auto *empty = new QTableWidgetItem(
            "No releases yet. Draft one to tag a commit in the repository.");
        empty->setForeground(QColor("#8b949e"));
        m_releasesTable->setItem(0, 0, empty);
    }
}

QWidget *MainWindow::buildMirrorNodesTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel("Mirror nodes");
    heading->setObjectName("channelTitle");
    m_mirrorNodesSummary = new QLabel;
    m_mirrorNodesSummary->setObjectName("statusLine");
    // Re-attest the relay's integrity pin to the refs we currently serve. Only the
    // source of truth (the owner holding the working copy) can do this, so the
    // button stays hidden until loadMirrorNodesPanel() finds we are that node.
    m_mirrorResetPinButton = new QPushButton("Reset integrity pin");
    m_mirrorResetPinButton->setObjectName("ghostButton");
    m_mirrorResetPinButton->setCursor(Qt::PointingHandCursor);
    m_mirrorResetPinButton->setToolTip(QStringLiteral(
        "Re-sign the refs this node serves and overwrite the relay's integrity "
        "pin, so clones work again after the served refs have moved on."));
    setOcticon(m_mirrorResetPinButton, "shield-check", 16);
    m_mirrorResetPinButton->hide();
    connect(m_mirrorResetPinButton, &QPushButton::clicked, this,
            &MainWindow::resetRepoPin);
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this,
            &MainWindow::loadMirrorNodesPanel);
    addRefreshSpin(refreshButton);
    headerRow->addWidget(heading);
    headerRow->addWidget(m_mirrorNodesSummary);
    headerRow->addStretch();
    headerRow->addWidget(m_mirrorResetPinButton);
    headerRow->addWidget(refreshButton);
    layout->addLayout(headerRow);

    auto *blurb = new QLabel(
        "Nodes across the network that keep a live mirror of this repository. "
        "Each node serves clones and browsing from its own copy; the commit and "
        "sync time show how fresh that copy is.");
    blurb->setObjectName("statusLine");
    blurb->setWordWrap(true);
    layout->addWidget(blurb);

    // The live activity dots no longer sit in this page as a "Live ›" row; they
    // float just above the Mirror nodes tab instead (adhoc #197). The strip is
    // created with the tab row and anchored by positionMirrorActivityStrip.

    m_mirrorNodesTable = new QTableWidget(0, 11);
    m_mirrorNodesTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_mirrorNodesTable);
    m_mirrorNodesTable->setHorizontalHeaderLabels(
        {"Node", "Latest commit", "Synced", "Size", "Issues", "CPU", "RAM",
         "Disk", "Platform", "Version", "Node id"});
    m_mirrorNodesTable->verticalHeader()->setVisible(false);
    m_mirrorNodesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mirrorNodesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mirrorNodesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mirrorNodesTable->setShowGrid(false);
    m_mirrorNodesTable->setWordWrap(false);
    m_mirrorNodesTable->setSortingEnabled(true);
    m_mirrorNodesTable->sortByColumn(0, Qt::AscendingOrder); // source of truth first
    QHeaderView *mh = m_mirrorNodesTable->horizontalHeader();
    mh->setHighlightSections(false);
    mh->setSectionResizeMode(0, QHeaderView::Stretch);          // Node
    mh->setSectionResizeMode(1, QHeaderView::ResizeToContents); // Latest commit
    mh->setSectionResizeMode(2, QHeaderView::ResizeToContents); // Synced
    mh->setSectionResizeMode(3, QHeaderView::ResizeToContents); // Size
    mh->setSectionResizeMode(4, QHeaderView::ResizeToContents); // Issues
    mh->setSectionResizeMode(5, QHeaderView::ResizeToContents); // CPU (bar)
    mh->setSectionResizeMode(6, QHeaderView::ResizeToContents); // RAM (bar)
    mh->setSectionResizeMode(7, QHeaderView::ResizeToContents); // Disk (bar)
    mh->setSectionResizeMode(8, QHeaderView::ResizeToContents); // Platform
    mh->setSectionResizeMode(9, QHeaderView::ResizeToContents); // Version
    mh->setSectionResizeMode(10, QHeaderView::ResizeToContents); // Node id
    makeColumnsResizable(m_mirrorNodesTable);
    // Synced column draws a pac-man countdown for behind nodes; a 1s timer
    // repaints the column so the chart animates while the panel is visible.
    m_mirrorNodesTable->setItemDelegateForColumn(
        2, new MirrorSyncDelegate(m_mirrorNodesTable));
    // CPU / RAM / disk columns render as little usage bars (details on hover).
    auto *resourceBars = new ResourceBarDelegate(m_mirrorNodesTable);
    for (int col : {5, 6, 7})
        m_mirrorNodesTable->setItemDelegateForColumn(col, resourceBars);
    auto *pacmanTick = new QTimer(m_mirrorNodesTable);
    pacmanTick->setInterval(1000);
    connect(pacmanTick, &QTimer::timeout, m_mirrorNodesTable, [this] {
        if (!m_mirrorNodesTable->isVisible())
            return;
        // Only the Synced column animates, so repaint just its cells rather than
        // the whole viewport. A full viewport()->update() re-ran the row's
        // HoverRowDelegate for every other column each second, painting cells
        // nothing had changed and stalling the GUI thread on big node lists
        // (adhoc #238); this mirrors the per-cell scanner repaint (onScannerTick).
        for (int r = 0; r < m_mirrorNodesTable->rowCount(); ++r) {
            if (m_mirrorNodesTable->item(r, 2))
                m_mirrorNodesTable->update(
                    m_mirrorNodesTable->model()->index(r, 2));
        }
    });
    pacmanTick->start();
    // Double-click a node row to open its profile.
    // itemActivated (rather than cellDoubleClicked) so Enter opens the selected
    // node's profile, matching the tab's arrow-key navigation (adhoc #183).
    connect(m_mirrorNodesTable, &QTableWidget::itemActivated, this,
            [this](QTableWidgetItem *item) {
                QTableWidgetItem *it =
                    item ? m_mirrorNodesTable->item(item->row(), 0) : nullptr;
                if (it) {
                    const QString nid = it->data(Qt::UserRole).toString();
                    if (!nid.isEmpty())
                        showNodeProfile(nid, it->text());
                }
            });
    layout->addWidget(m_mirrorNodesTable, 1);
    return page;
}

void MainWindow::loadMirrorNodesPanel()
{
    if (!m_mirrorNodesTable)
        return;
    TableRepaintGuard repaintGuard(m_mirrorNodesTable);
    m_mirrorNodesTable->setSortingEnabled(false);
    m_mirrorNodesTable->setRowCount(0);

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        if (m_mirrorNodesSummary)
            m_mirrorNodesSummary->clear();
        if (m_mirrorResetPinButton)
            m_mirrorResetPinButton->hide();
        if (m_mirrorActivityStrip) {
            static_cast<MirrorActivityStrip *>(m_mirrorActivityStrip)->setNodes({});
            positionMirrorActivityStrip(); // hides the now-empty strip
        }
        m_mirrorNodesTable->setSortingEnabled(true);
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    // This node's own clone identity (catalog/host owner) for this repo.
    const QString canonical =
        catalogOwner(repo) + "/" +
        repoSegment(repo.name, QStringLiteral("repository"));
    // The SHARED upstream identity — the same on every node mirroring this repo,
    // which is what actually groups them. The node whose clone identity equals
    // this source is the source of truth (the owner).
    const QString source = repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
                           repoSegment(repo.name, QStringLiteral("repository"));
    const QString sourceOwner = source.section('/', 0, 0);
    const QString legacy = repo.owner + "/" + repo.name;
    // Our own mirror, used to resolve a peer's advertised commit to its subject.
    const QString localMirror = repo.mirrorPath;

    // For our own row, read the HEAD straight from the local mirror so it always
    // reflects the latest sync without waiting for a roster round-trip.
    MirrorAdvert selfAdvert;
    selfAdvert.ownerName = canonical;
    selfAdvert.source = source;
    selfAdvert.branch = mirrorHeadBranch(localMirror);
    selfAdvert.commit = mirrorBranchCommit(localMirror, selfAdvert.branch);
    selfAdvert.updatedMs = repo.lastSyncMs;
    selfAdvert.sizeBytes = mirrorRepoSizeBytes(localMirror);
    selfAdvert.issueCount = mirrorIssueCount(localMirror, selfAdvert.branch);

    // If we are the source of truth, our working copy can be ahead of the bare
    // mirror we serve (e.g. a comment was just committed and the mirror fetch
    // hasn't run/finished). Count those un-mirrored commits so the self row can
    // show a live "↑N to push" badge the moment a change is made.
    int pendingPush = 0;
    if (repoHasWorkingTree() && !repo.localPath.trimmed().isEmpty() &&
        !selfAdvert.commit.isEmpty()) {
        QByteArray out;
        if (runGitCapture(repo.localPath,
                          {QStringLiteral("rev-list"), QStringLiteral("--count"),
                           selfAdvert.commit + QStringLiteral("..HEAD")},
                          &out, nullptr))
            pendingPush = QString::fromUtf8(out).trimmed().toInt();
    }

    // Resolve a node's advert for this repo: the shared source identity groups
    // every mirror, with a clone-name fallback for older peers, and our own row
    // always reads the live local HEAD via selfAdvert.
    auto matchAdvert = [&](const MemberInfo &node,
                           bool &namedOnly) -> const MirrorAdvert * {
        const MirrorAdvert *advert = nullptr;
        for (const MirrorAdvert &m : node.mirrorDetails) {
            if (m.source == source || m.ownerName == canonical ||
                m.ownerName == legacy) {
                advert = &m;
                break;
            }
        }
        namedOnly = !advert && (node.mirrors.contains(canonical) ||
                                node.mirrors.contains(legacy));
        if (node.self && (advert || namedOnly || !repo.previewOnly))
            advert = &selfAdvert; // always prefer our live local HEAD for our row
        return advert;
    };

    // The reference HEAD a node must match to count as "in sync": the source of
    // truth's commit if it advertises one, else the freshest-synced commit in the
    // group. Nodes whose commit differs are behind and get a heartbeat countdown.
    QString sourceCommit;
    QString newestCommit;
    qint64 newestMs = -1;
    for (const MemberInfo &node : std::as_const(m_homeRoster)) {
        bool namedOnly = false;
        const MirrorAdvert *advert = matchAdvert(node, namedOnly);
        if (!advert || advert->commit.isEmpty())
            continue;
        if (advert->ownerName == source || node.name == sourceOwner)
            sourceCommit = advert->commit;
        if (advert->updatedMs > newestMs) {
            newestMs = advert->updatedMs;
            newestCommit = advert->commit;
        }
    }
    const QString referenceCommit =
        !sourceCommit.isEmpty() ? sourceCommit : newestCommit;

    int count = 0;
    qint64 totalBytes = 0;     // data mirrored across every node in this group
    qint64 maxRepoBytes = 0;   // best (largest, == most complete) copy seen
    bool weAreSource = false;  // this node holds the source-of-truth copy
    int outOfSyncPeers = 0;    // other nodes whose served state != the source
    // Names already shown from the live chat roster, so the catalog-backed merge
    // below (issue #223) doesn't list a node twice when it's also present in chat.
    QSet<QString> shownNames;
    // One activity dot per active node, fed to the live strip atop the panel.
    QVector<MirrorActivityStrip::Dot> activityDots;
    for (const MemberInfo &node : std::as_const(m_homeRoster)) {
        bool namedOnly = false;
        const MirrorAdvert *advert = matchAdvert(node, namedOnly);
        if (!advert && !namedOnly)
            continue;
        shownNames.insert(node.name.trimmed().toLower());

        // The source of truth: the node whose clone identity equals the shared
        // source (the owner advertises ownerName == source); also match by name.
        const bool isSource =
            (advert && advert->ownerName == source) || node.name == sourceOwner;

        const int row = m_mirrorNodesTable->rowCount();
        m_mirrorNodesTable->insertRow(row);

        // Node: green/grey dot + name (+ "you") (+ source-of-truth tag).
        const bool online = node.self ? (m_backend != nullptr) : node.online;
        if (online)
            activityDots.append({node.id, node.name, true, node.self});
        auto *nameItem = new SortTableWidgetItem(
            node.name + (node.self ? QStringLiteral("  (you)") : QString()) +
            (isSource ? QString::fromUtf8("  \xE2\x98\x85 source of truth")
                      : QString()));
        nameItem->setIcon(themedOcticon(
            "broadcast", QColor(online ? "#3fb950" : "#8b949e"), 14));
        nameItem->setData(Qt::UserRole, node.id);
        // Source-of-truth rows sort to the top (★ < letters), then by name.
        nameItem->setData(kTableSortRole,
                          (isSource ? QStringLiteral("0") : QStringLiteral("1")) +
                              node.name.toLower());
        nameItem->setToolTip(isSource
                                 ? QString::fromUtf8("Source of truth \xC2\xB7 %1")
                                       .arg(online ? "online" : "offline")
                                 : (online ? "Online now" : "Offline"));
        m_mirrorNodesTable->setItem(row, 0, nameItem);

        // Latest commit: short hash + branch; tooltip carries the subject/date
        // when we hold the same commit in our own mirror.
        QString commitText = QString::fromUtf8("\xE2\x80\x94");
        QString commitTip;
        if (advert && !advert->commit.isEmpty()) {
            commitText = advert->commit.left(10);
            if (!advert->branch.isEmpty())
                commitText += "  (" + advert->branch + ")";
            commitTip = advert->commit;
            QByteArray subject;
            if (!localMirror.isEmpty() &&
                runGitCapture(localMirror,
                              {"show", "-s", "--format=%s", advert->commit},
                              &subject, nullptr)) {
                const QString s = QString::fromUtf8(subject).trimmed();
                if (!s.isEmpty())
                    commitTip = s + "\n" + advert->commit;
            }
        }
        auto *commitItem = new QTableWidgetItem(commitText);
        commitItem->setToolTip(commitTip);
        m_mirrorNodesTable->setItem(row, 1, commitItem);

        // Synced: relative time since the node last fetched from source.
        const qint64 syncedSecs = advert ? advert->updatedMs / 1000 : 0;
        auto *syncedItem = new SortTableWidgetItem(
            syncedSecs > 0 ? formatShortRelativeTime(syncedSecs) + " ago"
                           : QString::fromUtf8("\xE2\x80\x94"));
        syncedItem->setData(kTableSortRole, double(syncedSecs));
        if (syncedSecs > 0)
            syncedItem->setToolTip(
                QDateTime::fromSecsSinceEpoch(syncedSecs).toString(Qt::ISODate));
        // Behind-but-online node: tag the cell so MirrorSyncDelegate draws a
        // pac-man counting down to its next heartbeat/re-sync. In-sync and
        // offline rows carry no anchor and render as plain text.
        const bool behind = online && advert && !advert->commit.isEmpty() &&
                            !referenceCommit.isEmpty() &&
                            advert->commit != referenceCommit;
        if (behind) {
            syncedItem->setData(kPacmanAnchorRole,
                                static_cast<qlonglong>(advert->updatedMs));
            syncedItem->setToolTip(QString::fromUtf8(
                "Behind the source \xC2\xB7 catches up at its next heartbeat"));
        }
        // Our own row, when the working copy holds commits the served mirror
        // doesn't yet: surface the pending push count instead of the sync time.
        if (node.self && pendingPush > 0) {
            syncedItem->setText(QString::fromUtf8("\xE2\x86\x91 %1 to push")
                                    .arg(pendingPush));
            syncedItem->setToolTip(
                QString::fromUtf8("%1 local commit%2 not yet copied to this "
                                  "node's served mirror")
                    .arg(pendingPush)
                    .arg(pendingPush == 1 ? "" : "s"));
        }
        m_mirrorNodesTable->setItem(row, 2, syncedItem);

        // Tally for the owner-only alert below: are we the source of truth, and
        // how many other nodes are serving a state that doesn't match it.
        if (node.self && isSource)
            weAreSource = true;
        if (!node.self && behind)
            ++outOfSyncPeers;

        // Size: how much data this node holds for this repo (its bare mirror's
        // on-disk object size). Sorts numerically; an em-dash for older peers
        // that don't advertise a size yet.
        const qint64 nodeBytes = advert ? advert->sizeBytes : 0;
        auto *sizeItem = new SortTableWidgetItem(
            nodeBytes > 0 ? formatByteSize(nodeBytes)
                          : QString::fromUtf8("\xE2\x80\x94"));
        sizeItem->setData(kTableSortRole, double(nodeBytes));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_mirrorNodesTable->setItem(row, 3, sizeItem);
        if (nodeBytes > 0) {
            totalBytes += nodeBytes;
            maxRepoBytes = qMax(maxRepoBytes, nodeBytes);
        }

        // Issues: how many issues this node is mirroring (advertised per node).
        // Sorts numerically; an em-dash for older peers that don't advertise it.
        const int nodeIssues = advert ? advert->issueCount : -1;
        auto *issuesItem = new SortTableWidgetItem(
            nodeIssues >= 0 ? QString::number(nodeIssues)
                            : QString::fromUtf8("\xE2\x80\x94"));
        issuesItem->setData(kTableSortRole, double(nodeIssues));
        issuesItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (nodeIssues >= 0)
            issuesItem->setToolTip(
                QString::fromUtf8("Mirroring %1 issue%2")
                    .arg(nodeIssues)
                    .arg(nodeIssues == 1 ? "" : "s"));
        m_mirrorNodesTable->setItem(row, 4, issuesItem);

        // CPU / RAM / disk usage bars (hover for the underlying figures). The
        // telemetry is per-node, advertised in the node's heartbeats; peers that
        // don't advertise it (older builds) leave the bars as an em-dash.
        m_mirrorNodesTable->setItem(row, 5, makeCpuUsageCell(node.cpuPercent));
        m_mirrorNodesTable->setItem(
            row, 6, makeByteUsageCell("RAM", node.memUsedBytes, node.memTotalBytes));
        m_mirrorNodesTable->setItem(
            row, 7,
            makeByteUsageCell("Disk", node.diskUsedBytes, node.diskTotalBytes));

        m_mirrorNodesTable->setItem(
            row, 8,
            new QTableWidgetItem(node.platform.isEmpty()
                                     ? QString::fromUtf8("\xE2\x80\x94")
                                     : node.platform));
        m_mirrorNodesTable->setItem(
            row, 9,
            new QTableWidgetItem(node.version.isEmpty()
                                     ? QString::fromUtf8("\xE2\x80\x94")
                                     : node.version));
        auto *idItem = new QTableWidgetItem(
            node.id.left(12) + (node.id.size() > 12 ? QString::fromUtf8("\xE2\x80\xA6")
                                                    : QString()));
        idItem->setToolTip(node.id);
        m_mirrorNodesTable->setItem(row, 10, idItem);
        ++count;
    }

    // --- Catalog-backed mirrors (issue #223) --------------------------------
    // The loop above only sees nodes currently live in the chat room, so a
    // mirror with intermittent presence is invisible to the owner. Supplement
    // with the worker's /mirrors list — every node that has published a mirror
    // record for this source — adding any not already shown from the roster.
    if (m_catalogMirrorsSource == source) {
        for (const QJsonValue &value : std::as_const(m_catalogMirrorsCache)) {
            const QJsonObject m = value.toObject();
            const QString nodeName = m.value("node").toString().trimmed();
            if (nodeName.isEmpty() || shownNames.contains(nodeName.toLower()))
                continue;
            shownNames.insert(nodeName.toLower());
            const bool isSource =
                nodeName.compare(sourceOwner, Qt::CaseInsensitive) == 0;
            const bool online =
                m.value("status").toString() == QLatin1String("online");
            if (online)
                activityDots.append(
                    {m.value("id").toString(), nodeName, true, false});
            const int row = m_mirrorNodesTable->rowCount();
            m_mirrorNodesTable->insertRow(row);
            auto *nameItem = new SortTableWidgetItem(
                nodeName + (isSource
                                ? QString::fromUtf8("  \xE2\x98\x85 source of truth")
                                : QString()));
            nameItem->setIcon(themedOcticon(
                "broadcast", QColor(online ? "#3fb950" : "#8b949e"), 14));
            nameItem->setData(kTableSortRole,
                              (isSource ? QStringLiteral("0") : QStringLiteral("1")) +
                                  nodeName.toLower());
            nameItem->setToolTip(
                online
                    ? QStringLiteral("Online now")
                    : QStringLiteral("Published mirror \xC2\xB7 not in the live room"));
            m_mirrorNodesTable->setItem(row, 0, nameItem);
            m_mirrorNodesTable->setItem(
                row, 1, new QTableWidgetItem(QString::fromUtf8("\xE2\x80\x94")));
            const qint64 syncedSecs = qint64(m.value("lastSync").toDouble()) / 1000;
            auto *syncedItem = new SortTableWidgetItem(
                syncedSecs > 0 ? formatShortRelativeTime(syncedSecs) + " ago"
                               : QString::fromUtf8("\xE2\x80\x94"));
            syncedItem->setData(kTableSortRole, double(syncedSecs));
            if (syncedSecs > 0)
                syncedItem->setToolTip(
                    QDateTime::fromSecsSinceEpoch(syncedSecs).toString(Qt::ISODate));
            m_mirrorNodesTable->setItem(row, 2, syncedItem);
            const qint64 nodeBytes = qint64(m.value("sizeBytes").toDouble());
            auto *sizeItem = new SortTableWidgetItem(
                nodeBytes > 0 ? formatByteSize(nodeBytes)
                              : QString::fromUtf8("\xE2\x80\x94"));
            sizeItem->setData(kTableSortRole, double(nodeBytes));
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_mirrorNodesTable->setItem(row, 3, sizeItem);
            if (nodeBytes > 0) {
                totalBytes += nodeBytes;
                maxRepoBytes = qMax(maxRepoBytes, nodeBytes);
            }
            // Catalog-only mirrors aren't live in the room, so we don't have their
            // advertised issue count or resource telemetry: those columns stay
            // unknown (em-dash), like the other catalog-derived rows.
            m_mirrorNodesTable->setItem(
                row, 4, new QTableWidgetItem(QString::fromUtf8("\xE2\x80\x94")));
            for (int col : {5, 6, 7})
                m_mirrorNodesTable->setItem(row, col,
                                            makeResourceBarCell(-1, QString()));
            for (int col : {8, 9, 10})
                m_mirrorNodesTable->setItem(
                    row, col,
                    new QTableWidgetItem(QString::fromUtf8("\xE2\x80\x94")));
            ++count;
        }
    }
    // Refresh the catalog mirror list (throttled per source); the async reply
    // re-renders this panel so newly-discovered mirrors appear without a restart.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_catalogMirrorsFetchSource != source ||
        nowMs - m_catalogMirrorsFetchedMs > 15000) {
        m_catalogMirrorsFetchSource = source;
        m_catalogMirrorsFetchedMs = nowMs;
        fetchCatalogMirrors(sourceOwner,
                            repoSegment(repo.name, QStringLiteral("repository")),
                            source);
    }

    m_mirrorNodesTable->setSortingEnabled(true);

    if (m_mirrorActivityStrip) {
        static_cast<MirrorActivityStrip *>(m_mirrorActivityStrip)
            ->setNodes(activityDots);
        // Re-anchor over the Mirror nodes tab and (re)size to the new dot count.
        positionMirrorActivityStrip();
    }

    if (m_mirrorNodesSummary) {
        // "· 3 nodes mirroring owner/repo · 12.4 MB each · 37.1 MB total"
        QString text = QString::fromUtf8("\xC2\xB7 %1 node%2 mirroring %3")
                           .arg(count)
                           .arg(count == 1 ? "" : "s")
                           .arg(source);
        if (maxRepoBytes > 0)
            text += QString::fromUtf8(" \xC2\xB7 %1 each \xC2\xB7 %2 total")
                        .arg(formatByteSize(maxRepoBytes),
                             formatByteSize(totalBytes));
        // Only the source of truth is told when a mirror node's served state has
        // drifted from canonical — the mirror itself stays unaware (its pin is
        // not its concern). A behind-but-online mirror is flagged here so the
        // owner can see something is off even before opening the table.
        if (weAreSource && outOfSyncPeers > 0)
            text += QString::fromUtf8(
                        " \xC2\xB7 <span style='color:#f85149'>\xE2\x9A\xA0 %1 "
                        "mirror node%2 out of sync</span>")
                        .arg(outOfSyncPeers)
                        .arg(outOfSyncPeers == 1 ? "" : "s");
        // Local commits not yet copied into the mirror we serve (a just-made
        // comment/commit), shown until the background fetch catches the mirror up.
        if (pendingPush > 0)
            text += QString::fromUtf8(
                        " \xC2\xB7 <span style='color:#d29922'>\xE2\x86\x91 %1 "
                        "to push</span>")
                        .arg(pendingPush);
        m_mirrorNodesSummary->setTextFormat(Qt::RichText);
        m_mirrorNodesSummary->setText(text);
    }
    // "Reset integrity pin" is the source of truth's concern alone: only the
    // node holding the working copy can re-attest the relay's pin. Mirror nodes
    // (even on the owner's own account) never get the button — a stale pin there
    // is the source of truth's problem to fix (see refreshRepoPinBanner).
    if (m_mirrorResetPinButton)
        m_mirrorResetPinButton->setVisible(weAreSource && repoHasWorkingTree());

    if (m_repoMirrorsTab)
        m_repoMirrorsTab->setText(QStringLiteral("Mirror nodes (%1)").arg(formatCount(count)));
    if (count == 0) {
        m_mirrorNodesTable->insertRow(0);
        auto *empty = new QTableWidgetItem(
            "No other nodes are advertising a mirror of this repository yet.");
        empty->setForeground(QColor("#8b949e"));
        m_mirrorNodesTable->setItem(0, 0, empty);
    }
}

void MainWindow::fetchCatalogMirrors(const QString &owner, const QString &repo,
                                     const QString &source)
{
    // The live chat roster only shows nodes currently present in the room, so a
    // mirror with intermittent presence is invisible to the owner (issue #223).
    // The worker's public /mirrors endpoint lists every node that has published
    // a mirror record for this repo group; we cache the result and merge it into
    // loadMirrorNodesPanel(). Public read — no auth token required.
    if (!m_networkAccess || owner.isEmpty() || repo.isEmpty())
        return;
    QUrl url = catalogApiUrl(); // same host/scheme as the catalog
    url.setPath(QStringLiteral("/api/repo/%1/%2/mirrors")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(owner)),
                         QString::fromUtf8(QUrl::toPercentEncoding(repo))));
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, source]() {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        const QJsonObject resp = QJsonDocument::fromJson(body).object();
        if (!resp.value("ok").toBool())
            return;
        m_catalogMirrorsSource = source;
        m_catalogMirrorsCache = resp.value("mirrors").toArray();
        // Re-render only if the user is still viewing this repo group, so the
        // freshly discovered mirrors show up without waiting for a roster tick.
        if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
            const RepositoryRecord &r = m_repositories.at(m_repoDetailIndex);
            const QString cur =
                repoSegment(r.owner, QStringLiteral("owner")) + "/" +
                repoSegment(r.name, QStringLiteral("repository"));
            if (cur == source)
                loadMirrorNodesPanel();
        }
    });
}

void MainWindow::promptNewRelease()
{
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice("This is a read-only mirror; releases can't be created here.",
                            true);
        return;
    }
    const QString dir = repoGitDir();
    const QStringList branches = repoBranches();
    const QString target = m_repoBranch.isEmpty() ? repoDefaultBranch(branches)
                                                  : m_repoBranch;

    // Auto-fill the tag and title from the previous release so a typical
    // patch bump is one click away. Grab the newest tag (by creation date)
    // and its subject (the release title baked into the annotated tag).
    QString prevTag, prevTitle;
    {
        QByteArray out;
        if (!dir.isEmpty() &&
            runGitCapture(dir,
                          {"for-each-ref", "--sort=-creatordate", "--count=1",
                           "--format=%(refname:short)%09%(contents:subject)",
                           "refs/tags"},
                          &out, nullptr)) {
            const QString line = QString::fromUtf8(out).trimmed();
            const int tab = line.indexOf('\t');
            if (tab >= 0) {
                prevTag = line.left(tab).trimmed();
                prevTitle = line.mid(tab + 1).trimmed();
            } else {
                prevTag = line;
            }
        }
    }
    // Suggest the next tag by incrementing the last run of digits in the
    // previous tag (v0.5.1 -> v0.5.2, v1.0.0-rc1 -> v1.0.0-rc2). Falls back
    // to an empty suggestion when there's no prior release to bump.
    QString suggestedTag, suggestedTitle;
    if (!prevTag.isEmpty()) {
        int end = -1;
        for (int i = prevTag.size() - 1; i >= 0; --i) {
            if (prevTag.at(i).isDigit()) {
                end = i;
                break;
            }
        }
        if (end >= 0) {
            int start = end;
            while (start > 0 && prevTag.at(start - 1).isDigit())
                --start;
            bool ok = false;
            const qulonglong n =
                prevTag.mid(start, end - start + 1).toULongLong(&ok);
            if (ok)
                suggestedTag = prevTag.left(start) + QString::number(n + 1) +
                               prevTag.mid(end + 1);
        }
        if (!suggestedTag.isEmpty()) {
            // Carry the previous title's pattern forward, swapping in the new
            // tag where the old one appeared (titles are usually just the tag).
            if (prevTitle.isEmpty() || prevTitle == prevTag)
                suggestedTitle = suggestedTag;
            else if (prevTitle.contains(prevTag))
                suggestedTitle = QString(prevTitle).replace(prevTag, suggestedTag);
            else
                suggestedTitle = prevTitle;
        }
    }

    // GitHub-style "draft a release": tag name, target ref, and release notes.
    QDialog dialog(this);
    dialog.setWindowTitle("Draft a new release");
    auto *form = new QFormLayout(&dialog);
    auto *tagEdit = new QLineEdit;
    tagEdit->setPlaceholderText("v1.0.0");
    if (!suggestedTag.isEmpty())
        tagEdit->setText(suggestedTag);
    auto *targetEdit = new QComboBox;
    targetEdit->addItems(branches);
    const int targetIdx = targetEdit->findText(target);
    if (targetIdx >= 0)
        targetEdit->setCurrentIndex(targetIdx);
    auto *titleEdit = new QLineEdit;
    titleEdit->setPlaceholderText("Release title (optional)");
    if (!suggestedTitle.isEmpty())
        titleEdit->setText(suggestedTitle);
    auto *notesEdit = new QPlainTextEdit;
    notesEdit->setPlaceholderText("Describe this release...");
    notesEdit->setMinimumHeight(120);
    form->addRow("Tag", tagEdit);
    form->addRow("Target", targetEdit);
    form->addRow("Title", titleEdit);
    form->addRow("Notes", notesEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Publish release");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Pre-select the suggested tag so it can be accepted as-is or typed over.
    tagEdit->setFocus();
    tagEdit->selectAll();
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString tag = tagEdit->text().trimmed();
    const QString targetRef = targetEdit->currentText().trimmed();
    if (tag.isEmpty()) {
        setRepoDetailNotice("A release needs a tag name.", true);
        return;
    }
    QString message = titleEdit->text().trimmed();
    const QString notes = notesEdit->toPlainText().trimmed();
    if (message.isEmpty())
        message = tag;
    if (!notes.isEmpty())
        message += "\n\n" + notes;

    // Stamp the released version into the Qt client's committed version header
    // and commit it BEFORE tagging, so the tagged commit declares the release
    // version and the app reports it. Lands even when no build node is online to
    // run the release workflow; the workflow's landVersionHeader then no-ops.
    // Only when releasing the checked-out branch's tip, so the bump commit
    // advances the ref the tag will point at instead of landing on an unrelated
    // branch (the target can differ from what's checked out).
    QByteArray headBranch;
    if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &headBranch,
                      nullptr) &&
        QString::fromUtf8(headBranch).trimmed() == targetRef &&
        bumpQtVersionForRelease(dir, tag))
        logSystem(
            QStringLiteral("Bumped ForkMesh version header to match %1.").arg(tag));

    // Annotated tag so the release notes live in the repo's git history.
    QString err;
    if (!runGitCapture(dir, {"tag", "-a", tag, targetRef, "-m", message}, nullptr,
                       &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not create the release tag." : err,
                            true);
        return;
    }
    logSystem(QStringLiteral("Git: tagged release %1 at %2.").arg(tag, targetRef));
    setRepoDetailNotice(QStringLiteral("Published release %1.").arg(tag));
    loadBranchesAndTags();

    // Trigger any `on: release` workflow (e.g. .forkmesh/release.yml, which
    // builds and publishes the desktop binary for this platform). Resolve the
    // tag's target to a concrete commit the runner can check out, and pass the
    // tag ref so the run reports against refs/tags/<tag> and the workflow sees
    // FORKMESH_TAG.
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        if (repo.actionsEnabled) {
            QByteArray tip;
            if (runGitCapture(dir, {"rev-parse", "--verify", tag + "^{commit}"},
                              &tip, nullptr) &&
                !tip.trimmed().isEmpty()) {
                queueWorkflowsForCommit(m_repoDetailIndex, repo.owner, repo.name,
                                        QString::fromUtf8(tip).trimmed(),
                                        QStringLiteral("refs/tags/") + tag,
                                        WorkflowTrigger::Release);
            }
        }
    }
}

void MainWindow::showReleaseDetail(const QString &tag)
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || tag.isEmpty())
        return;
    // Only real tags open a detail view — guards the empty-state placeholder row,
    // whose cell text isn't a tag.
    if (!runGitCapture(dir, {"rev-parse", "--verify", "--quiet",
                             QStringLiteral("refs/tags/") + tag},
                       nullptr, nullptr))
        return;

    // --- Tag metadata: full release notes, creation date+time, tagger. For an
    // annotated tag %(contents:*) is the tag message (the title + notes typed in
    // promptNewRelease); for a lightweight tag it falls back to the commit's.
    QByteArray meta;
    runGitCapture(dir,
                  {"for-each-ref",
                   "--format=%(creatordate:format:%Y-%m-%d %H:%M)%1f%(taggername)"
                   "%1f%(contents:subject)%1f%(contents:body)",
                   QStringLiteral("refs/tags/") + tag},
                  &meta, nullptr);
    const QStringList mf = QString::fromUtf8(meta).split(QLatin1Char('\x1f'));
    const QString when = mf.value(0).trimmed();
    const QString tagger = mf.value(1).trimmed();
    const QString subject = mf.value(2).trimmed();
    const QString body = mf.value(3).trimmed();

    // --- The previous release (next-older tag by creation date) is the diff
    // base, so the detail view shows "what changed since the last release" — the
    // same ordering the list uses, so it always lines up with the row above.
    QString prevTag;
    {
        QByteArray tagsOut;
        if (runGitCapture(dir,
                          {"for-each-ref", "--sort=-creatordate",
                           "--format=%(refname:short)", "refs/tags"},
                          &tagsOut, nullptr)) {
            const QStringList all =
                QString::fromUtf8(tagsOut).split(QLatin1Char('\n'));
            int idx = -1;
            for (int i = 0; i < all.size(); ++i)
                if (all.at(i).trimmed() == tag) {
                    idx = i;
                    break;
                }
            for (int j = idx + 1; idx >= 0 && j < all.size(); ++j) {
                const QString cand = all.at(j).trimmed();
                if (!cand.isEmpty()) {
                    prevTag = cand;
                    break;
                }
            }
        }
    }

    // --- The release diff: prevTag..tag (git resolves annotated tags to the
    // commits they point at). The very first release has no earlier tag, so the
    // diff section says so rather than dumping the whole tree.
    QString diffHtml;
    if (!prevTag.isEmpty()) {
        QByteArray patchRaw;
        runGitCapture(dir, {"diff", "-M", prevTag, tag}, &patchRaw, nullptr);
        QList<DiffFileEntry> files;
        diffHtml =
            renderDiffHtml(QString::fromUtf8(patchRaw), files, dir, prevTag, tag);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Release %1").arg(tag));
    dialog.resize(900, 640);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *title = new QLabel(
        QStringLiteral("<b style='font-size:16px'>%1</b>")
            .arg((subject.isEmpty() ? tag : subject).toHtmlEscaped()));
    title->setTextFormat(Qt::RichText);
    title->setWordWrap(true);
    layout->addWidget(title);

    QStringList metaBits;
    if (!when.isEmpty())
        metaBits << when;
    if (!tagger.isEmpty())
        metaBits << tagger;
    if (!metaBits.isEmpty()) {
        auto *metaLabel = new QLabel(metaBits.join(QString::fromUtf8(" \xC2\xB7 ")));
        metaLabel->setObjectName("statusLine");
        layout->addWidget(metaLabel);
    }

    // Release notes (the annotated tag's body, below its subject/title).
    if (!body.isEmpty()) {
        auto *notes = new QLabel(
            QStringLiteral("<span style='white-space:pre-wrap'>%1</span>")
                .arg(body.toHtmlEscaped()));
        notes->setTextFormat(Qt::RichText);
        notes->setWordWrap(true);
        notes->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(notes);
    }

    auto *diffHeading = new QLabel(
        prevTag.isEmpty()
            ? QStringLiteral("Changes")
            : QStringLiteral("Changes since %1").arg(prevTag.toHtmlEscaped()));
    diffHeading->setObjectName("channelTitle");
    layout->addWidget(diffHeading);

    auto *diff = new QTextBrowser;
    diff->setObjectName("diffView");
    diff->setOpenLinks(false); // read-only diff; don't navigate on anchor clicks
    diff->setLineWrapMode(QTextEdit::NoWrap);
    diff->document()->setDefaultStyleSheet(diffStyleSheet(m_diffFontPt));
    if (diffHtml.trimmed().isEmpty())
        diff->setHtml(
            prevTag.isEmpty()
                ? QStringLiteral(
                      "<p style='color:#8b949e'>This is the earliest release "
                      "\xE2\x80\x94 no previous release to diff against.</p>")
                : QStringLiteral(
                      "<p style='color:#8b949e'>No file changes between %1 and "
                      "%2.</p>")
                      .arg(prevTag.toHtmlEscaped(), tag.toHtmlEscaped()));
    else
        diff->setHtml(diffHtml);
    layout->addWidget(diff, 1);

    auto *buttons = new QDialogButtonBox;
    auto *browseBtn = buttons->addButton(QStringLiteral("Browse repo at this tag"),
                                         QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(browseBtn, &QPushButton::clicked, &dialog, [this, &dialog, tag] {
        dialog.accept();
        setRepoBranch(tag); // browse the repo's files at this tag
    });
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::deleteTag(const QString &tag)
{
    if (tag.isEmpty() || !repoHasWorkingTree())
        return;
    const QString dir = repoGitDir();
    if (QMessageBox::question(
            this, "Delete release",
            QStringLiteral("Delete release tag \"%1\"? This cannot be undone.").arg(tag),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QString err;
    if (!runGitCapture(dir, {"tag", "-d", tag}, nullptr, &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not delete the tag." : err, true);
        return;
    }
    logSystem(QStringLiteral("Git: deleted tag %1.").arg(tag));
    setRepoDetailNotice(QStringLiteral("Deleted release %1.").arg(tag));
    loadBranchesAndTags();
}
