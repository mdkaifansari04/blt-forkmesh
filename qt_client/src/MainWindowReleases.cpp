// MainWindowReleases: MainWindow feature methods, split out of MainWindow.cpp.
// Releases panel and mirror nodes.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"

#include <QVersionNumber>

using namespace forkmesh::ui;

namespace {

enum MirrorNodeColumn {
    MirrorNodeColNode = 0,
    MirrorNodeColOwner,
    MirrorNodeColCommit,
    MirrorNodeColSynced,
    MirrorNodeColSize,
    MirrorNodeColIssues,
    MirrorNodeColCommits,
    MirrorNodeColBranches,
    MirrorNodeColPulls,
    MirrorNodeColDiscussions,
    MirrorNodeColCpu,
    MirrorNodeColRam,
    MirrorNodeColDisk,
    MirrorNodeColPlatform,
    MirrorNodeColVersion,
    MirrorNodeColId,
    MirrorNodeColClones,
    MirrorNodeColWebsite,
    MirrorNodeColArtifacts,
    MirrorNodeColumnCount,
};

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

    m_releasesTable = new QTableWidget(0, 10);
    installColumnHeaderMenu(m_releasesTable); // 3-dots per-column menu (issue #318)
    m_releasesTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_releasesTable);
    m_releasesTable->setHorizontalHeaderLabels(
        {"Tag", "Commit", "Released", "Release notes", "Compare", "Artifacts",
         "Size", "SHA-256", "Downloads", ""});
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
    rh->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(3, QHeaderView::Stretch);
    rh->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(5, QHeaderView::Stretch);
    rh->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(8, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(9, QHeaderView::ResizeToContents);
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

// ---- Artifacts panel -------------------------------------------------------
//
// Release binaries live out of git in the node's content-addressed store
// (forkmesh-releases/sha256/<aa>/<hash>/data — see issue #304); the Releases tab
// only links to their downloads. This tab lists the blobs actually on disk with
// their size and the release they belong to, and lets each be deleted to reclaim
// space (adhoc #98). An orphaned blob — one no release manifest references — is
// surfaced explicitly, since those are the ones most worth pruning.

QWidget *MainWindow::buildArtifactsTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel("Artifacts");
    heading->setObjectName("channelTitle");
    m_artifactsSummary = new QLabel;
    m_artifactsSummary->setObjectName("statusLine");
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this,
            &MainWindow::loadArtifactsPanel);
    addRefreshSpin(refreshButton);
    headerRow->addWidget(heading);
    headerRow->addWidget(m_artifactsSummary);
    headerRow->addStretch();
    headerRow->addWidget(refreshButton);
    layout->addLayout(headerRow);

    auto *hint = new QLabel(
        "Release binaries this node hosts for download, stored out of git in its "
        "content-addressed release store. Deleting one frees its disk space; a peer "
        "that still holds it can re-seed this node on the next sync.");
    hint->setObjectName("statusLine");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_artifactsTable = new QTableWidget(0, 5);
    installColumnHeaderMenu(m_artifactsTable); // 3-dots per-column menu (issue #318)
    m_artifactsTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_artifactsTable);
    m_artifactsTable->setHorizontalHeaderLabels(
        {"Artifact", "Release", "Size", "Checksum", ""});
    m_artifactsTable->verticalHeader()->setVisible(false);
    m_artifactsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_artifactsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_artifactsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_artifactsTable->setShowGrid(false);
    m_artifactsTable->setWordWrap(false);
    QHeaderView *rh = m_artifactsTable->horizontalHeader();
    rh->setHighlightSections(false);
    rh->setSectionResizeMode(0, QHeaderView::Stretch);
    rh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    rh->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_artifactsTable);
    layout->addWidget(m_artifactsTable, 1);
    return page;
}

void MainWindow::loadArtifactsPanel()
{
    if (!m_artifactsTable)
        return;
    TableRepaintGuard repaintGuard(m_artifactsTable);
    m_artifactsTable->setRowCount(0);
    if (m_artifactsSummary)
        m_artifactsSummary->clear();
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const QString mirrorPath = m_repositories.at(m_repoDetailIndex).mirrorPath;
    // Deletion only touches the on-disk CAS in this node's own mirror, so it's
    // fine on mirror-only hosting nodes too (where reclaiming space matters most)
    // — gate on holding a writable local mirror, not on having a working tree.
    const bool writable = !m_repositories.at(m_repoDetailIndex).previewOnly &&
                          !mirrorPath.isEmpty() && QDir(mirrorPath).exists();

    const QList<MirrorReleaseBlob> blobs = mirrorReleaseBlobs(mirrorPath);

    // Map blob sha256 -> asset name / source tag from the release manifests git
    // already mirrors (.forkmesh/releases/<channel>/release.json on the served branch), the
    // same way replicateReleaseArtifacts reads them. A blob no manifest names is
    // an orphan and shown as such.
    QHash<QString, QString> nameByHash;
    QHash<QString, QString> tagByHash;
    const QString branch = mirrorHeadBranch(mirrorPath);
    QByteArray channelsOut;
    if (!blobs.isEmpty() && !branch.isEmpty() &&
        runGitCapture(mirrorPath,
                      {QStringLiteral("ls-tree"), QStringLiteral("-z"),
                       QStringLiteral("--name-only"),
                       branch + QStringLiteral(":.forkmesh/releases")},
                      &channelsOut, nullptr)) {
        for (const QByteArray &raw : channelsOut.split('\0')) {
            const QString channel = QString::fromUtf8(raw).trimmed();
            if (channel.isEmpty())
                continue;
            QByteArray manifestOut;
            if (!runGitCapture(mirrorPath,
                               {QStringLiteral("show"),
                                branch + QStringLiteral(":.forkmesh/releases/") + channel +
                                    QStringLiteral("/release.json")},
                               &manifestOut, nullptr))
                continue;
            const QJsonObject obj = QJsonDocument::fromJson(manifestOut).object();
            const QString tag = obj.value(QStringLiteral("tag")).toString().trimmed();
            const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
            for (const QJsonValue &asset : assets) {
                const QJsonObject a = asset.toObject();
                const QString hash = a.value(QStringLiteral("blob_sha256"))
                                         .toString()
                                         .trimmed()
                                         .toLower();
                if (hash.isEmpty())
                    continue;
                const QString name = a.value(QStringLiteral("name")).toString();
                if (!name.isEmpty() && !nameByHash.contains(hash))
                    nameByHash.insert(hash, name);
                if (!tag.isEmpty() && !tagByHash.contains(hash))
                    tagByHash.insert(hash, tag);
            }
        }
    }

    qint64 totalBytes = 0;
    for (const MirrorReleaseBlob &blob : blobs) {
        const int row = m_artifactsTable->rowCount();
        m_artifactsTable->insertRow(row);
        totalBytes += blob.size;

        const QString name = nameByHash.value(blob.hash);
        const QString tag = tagByHash.value(blob.hash);
        auto *nameItem = new QTableWidgetItem(
            name.isEmpty() ? QStringLiteral("(unreferenced blob)") : name);
        nameItem->setIcon(themedOcticon("package", QColor("#a371f7"), 14));
        if (name.isEmpty())
            nameItem->setToolTip(QStringLiteral(
                "No release manifest references this blob — safe to delete to "
                "reclaim its space."));
        m_artifactsTable->setItem(row, 0, nameItem);
        m_artifactsTable->setItem(
            row, 1,
            new QTableWidgetItem(tag.isEmpty() ? QStringLiteral("—") : tag));

        auto *sizeItem =
            new QTableWidgetItem(QLocale().formattedDataSize(blob.size));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_artifactsTable->setItem(row, 2, sizeItem);

        auto *hashItem = new QTableWidgetItem(
            QStringLiteral("sha256:%1").arg(blob.hash.left(12)));
        hashItem->setToolTip(QStringLiteral("sha256:%1").arg(blob.hash));
        m_artifactsTable->setItem(row, 3, hashItem);

        auto *del = new QPushButton;
        del->setObjectName("issueIconButton");
        del->setFlat(true);
        del->setCursor(Qt::PointingHandCursor);
        del->setIcon(themedOcticon("trash", QColor("#f85149"), 15));
        del->setIconSize(QSize(15, 15));
        del->setToolTip(QStringLiteral("Delete this artifact from disk"));
        del->setEnabled(writable);
        const QString hash = blob.hash;
        const QString label = name.isEmpty() ? blob.hash.left(12) : name;
        connect(del, &QPushButton::clicked, this,
                [this, hash, label] { deleteArtifact(hash, label); });
        m_artifactsTable->setCellWidget(row, 4, del);
    }

    if (m_artifactsSummary) {
        if (blobs.isEmpty())
            m_artifactsSummary->setText(QStringLiteral("(none)"));
        else
            m_artifactsSummary->setText(
                QStringLiteral("(%1 · %2)")
                    .arg(blobs.size())
                    .arg(QLocale().formattedDataSize(totalBytes)));
    }
}

void MainWindow::deleteArtifact(const QString &hash, const QString &label)
{
    if (hash.isEmpty() || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const QString mirrorPath = m_repositories.at(m_repoDetailIndex).mirrorPath;
    if (QMessageBox::question(
            this, "Delete artifact",
            QStringLiteral("Delete artifact \"%1\" from this node's release store? "
                           "This frees its disk space and cannot be undone, but a "
                           "peer that still holds it can re-seed this node.")
                .arg(label),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    // The blob lives at <mirror>/forkmesh-releases/sha256/<aa>/<hash>/data — drop
    // the whole <hash>/ directory, then the <aa>/ shard once it's empty.
    const QFileInfo info(mirrorReleaseBlobPath(mirrorPath, hash));
    QDir hashDir = info.absoluteDir();
    if (!hashDir.removeRecursively()) {
        setRepoDetailNotice(
            QStringLiteral("Could not delete artifact %1.").arg(label), true);
        return;
    }
    QDir shardDir = hashDir;
    if (shardDir.cdUp() && shardDir.isEmpty())
        shardDir.rmdir(QStringLiteral("."));
    logSystem(
        QStringLiteral("Artifacts: deleted %1 (sha256:%2) from the release store.")
            .arg(label, hash.left(12)));
    setRepoDetailNotice(QStringLiteral("Deleted artifact %1.").arg(label));
    loadArtifactsPanel();
    // The Mirror nodes view advertises this node's artifact tally; keep it honest
    // if it's the visible tab.
    if (m_repoDetailStack && m_mirrorNodesTabIndex >= 0 &&
        m_repoDetailStack->currentIndex() == m_mirrorNodesTabIndex)
        loadMirrorNodesPanel();
}

void MainWindow::pruneReleaseArtifactsForCurrentRepo(const QString &releaseTag)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    const QString mirrorPath = repo.mirrorPath;
    if (mirrorPath.trimmed().isEmpty() || !QDir(mirrorPath).exists()) {
        setRepoDetailNotice(
            QStringLiteral("Release %1 published, but no local artifact store was found to prune.")
                .arg(releaseTag),
            true);
        return;
    }

    const QList<MirrorReleaseBlob> blobs = mirrorReleaseBlobs(mirrorPath);
    if (blobs.isEmpty()) {
        logSystem(QStringLiteral("Artifacts: no previous release artifacts to delete for %1.")
                      .arg(releaseTag));
        return;
    }

    int deleted = 0;
    int failed = 0;
    qint64 bytesDeleted = 0;
    for (const MirrorReleaseBlob &blob : blobs) {
        const QFileInfo info(blob.path);
        QDir hashDir = info.absoluteDir();
        if (hashDir.removeRecursively()) {
            ++deleted;
            bytesDeleted += blob.size;
            QDir shardDir = hashDir;
            if (shardDir.cdUp() && shardDir.isEmpty())
                shardDir.rmdir(QStringLiteral("."));
        } else {
            ++failed;
        }
    }

    if (deleted > 0) {
        logSystem(
            QStringLiteral("Artifacts: deleted %1 previous release artifact%2 (%3) before publishing %4.")
                .arg(deleted)
                .arg(deleted == 1 ? QString() : QStringLiteral("s"))
                .arg(QLocale().formattedDataSize(bytesDeleted), releaseTag));
    }
    if (failed > 0) {
        setRepoDetailNotice(
            QStringLiteral("Published release %1, but %2 previous artifact%3 could not be deleted.")
                .arg(releaseTag)
                .arg(failed)
                .arg(failed == 1 ? QString() : QStringLiteral("s")),
            true);
    } else if (deleted > 0) {
        setRepoDetailNotice(
            QStringLiteral("Published release %1 and deleted %2 previous artifact%3 (%4).")
                .arg(releaseTag)
                .arg(deleted)
                .arg(deleted == 1 ? QString() : QStringLiteral("s"))
                .arg(QLocale().formattedDataSize(bytesDeleted)));
    }

    if (m_repoDetailStack && m_artifactsTabIndex >= 0 &&
        m_repoDetailStack->currentIndex() == m_artifactsTabIndex)
        loadArtifactsPanel();
    if (m_repoDetailStack && m_mirrorNodesTabIndex >= 0 &&
        m_repoDetailStack->currentIndex() == m_mirrorNodesTabIndex)
        loadMirrorNodesPanel();
    m_mirrorAdvertSig.clear();
    refreshRepositoryList();
    if (repo.publishToNetwork) {
        for (int i = 0; i < m_repositories.size(); ++i) {
            const RepositoryRecord &candidate = m_repositories.at(i);
            if (candidate.owner == repo.owner && candidate.name == repo.name &&
                candidate.mirrorPath == mirrorPath) {
                publishRepository(i, false);
                break;
            }
        }
    }
}

void MainWindow::pruneReleaseTagsForCurrentRepo(const QString &keepTag)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;

    QByteArray out;
    if (!runGitCapture(dir,
                       {"for-each-ref", "--format=%(refname:short)", "refs/tags"},
                       &out, nullptr))
        return;
    QStringList staleTags;
    for (const QByteArray &line : out.split('\n')) {
        const QString name = QString::fromUtf8(line).trimmed();
        if (!name.isEmpty() && name != keepTag)
            staleTags << name;
    }
    if (staleTags.isEmpty()) {
        logSystem(QStringLiteral("Releases: no previous release tags to delete for %1.")
                       .arg(keepTag));
        return;
    }

    int deleted = 0;
    int failed = 0;
    for (const QString &name : std::as_const(staleTags)) {
        if (runGitCapture(dir, {"tag", "-d", name}, nullptr, nullptr))
            ++deleted;
        else
            ++failed;
    }

    if (deleted > 0)
        logSystem(
            QStringLiteral("Git: deleted %1 previous release tag%2 before publishing %3.")
                .arg(deleted)
                .arg(deleted == 1 ? QString() : QStringLiteral("s"))
                .arg(keepTag));
    if (failed > 0) {
        setRepoDetailNotice(
            QStringLiteral("Published release %1, but %2 previous tag%3 could not be deleted.")
                .arg(keepTag)
                .arg(failed)
                .arg(failed == 1 ? QString() : QStringLiteral("s")),
            true);
    } else if (deleted > 0) {
        setRepoDetailNotice(
            QStringLiteral("Published release %1 and deleted %2 previous tag%3.")
                .arg(keepTag)
                .arg(deleted)
                .arg(deleted == 1 ? QString() : QStringLiteral("s")));
    }

    loadBranchesAndTags();
    if (m_repoDetailStack && m_releasesTabIndex >= 0 &&
        m_repoDetailStack->currentIndex() == m_releasesTabIndex)
        loadReleasesPanel();

    // Deleting only changes the tag refs in the working copy; propagate that
    // into the served bare mirror now (syncRepository fetches heads+tags with
    // --prune) instead of waiting on the 15-minute auto-sync — the same
    // immediacy propagateRepoUpdate already gives freshly committed issues/PRs.
    propagateRepoUpdate(m_repoDetailIndex);
}

void MainWindow::loadReleasesPanel()
{
    if (!m_releasesTable)
        return;
    TableRepaintGuard repaintGuard(m_releasesTable);
    m_releasesTable->setRowCount(0);
    const QString dir = repoGitDir();
    const bool writable = repoHasWorkingTree();

    // Release artifacts are published under .forkmesh/releases/<channel>/release.json (the
    // channel is usually "latest", NOT the tag name — see .forkmesh/releases/README.md and
    // tools/forkmesh-release-publish.sh). Each manifest records the tag it was cut
    // from in its "tag" field, so scan every channel manifest and key the asset
    // names by that tag. The Artifacts column then looks up each release row by
    // tag, instead of probing a .forkmesh/releases/<tag>/ path that the publisher never
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

    // The relay logs one row per completed download of a release asset (see the
    // worker's /releases/blob/sha256/<hash> route), keyed by this repo's
    // owner/name — the same identity the artifact links above are built from.
    // Fetched below (throttled) and rendered next to each artifact's checksum.
    QString dlOwner;
    QString dlRepo;
    QString dlSource;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &openRepo = m_repositories.at(m_repoDetailIndex);
        dlOwner = repoSegment(openRepo.owner, QStringLiteral("owner"));
        dlRepo = repoSegment(openRepo.name, QStringLiteral("repository"));
        if (!dlOwner.isEmpty() && !dlRepo.isEmpty())
            dlSource = dlOwner + QLatin1Char('/') + dlRepo;
    }
    const bool haveDownloadCounts = !dlSource.isEmpty() &&
                                    m_releaseDownloadsSource == dlSource;

    // Artifacts column holds rich-text links, so key it on the rendered HTML.
    // Size/SHA-256/Downloads get their own plain-text columns (one comma-joined
    // entry per asset, positionally matching the Artifacts links) instead of
    // being crammed into the Artifacts cell.
    QHash<QString, QString> artifactsByTag;
    QHash<QString, QString> sizeByTag;
    QHash<QString, QString> shaByTag;
    QHash<QString, QString> shaTooltipByTag;
    QHash<QString, QString> downloadsByTag;
    // The "latest" channel is what install.sh actually downloads as the current
    // release, regardless of which tag it was cut from. Remember its rendered
    // artifacts (and source tag) so the newest release row can surface them even
    // when that release's own build hasn't published a manifest yet.
    QString latestChannelHtml;
    QString latestChannelTag;
    QString latestChannelSize;
    QString latestChannelSha;
    QString latestChannelShaTooltip;
    QString latestChannelDownloads;
    if (!dir.isEmpty()) {
        const QDir releasesDir(dir + QStringLiteral("/.forkmesh/releases"));
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
            QStringList sizeParts;
            QStringList shaParts;
            QStringList shaTooltipParts;
            QStringList downloadParts;
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
                assetLinks.append(entry);

                // Artifact byte size, as recorded by
                // tools/forkmesh-release-publish.sh when it staged the blob.
                const qint64 size =
                    static_cast<qint64>(a.value(QStringLiteral("size")).toDouble());
                sizeParts.append(size > 0 ? QLocale().formattedDataSize(size)
                                          : QStringLiteral("—"));

                // Abbreviated sha256 for the column, full digest in the tooltip —
                // the full 64-char hex would blow out the column width.
                shaParts.append(hashValid ? hash.left(12) : QStringLiteral("—"));
                shaTooltipParts.append(hashValid ? QStringLiteral("sha256:%1").arg(hash)
                                                 : QString());

                // Download tally the relay has logged for this exact blob.
                if (hashValid && haveDownloadCounts)
                    downloadParts.append(
                        QString::number(m_releaseDownloadsCache.value(hash, 0)));
                else
                    downloadParts.append(QStringLiteral("—"));
            }
            if (!assetLinks.isEmpty()) {
                const QString joined = assetLinks.join(QStringLiteral(", "));
                artifactsByTag.insert(manifestTag, joined);
                sizeByTag.insert(manifestTag, sizeParts.join(QStringLiteral(", ")));
                shaByTag.insert(manifestTag, shaParts.join(QStringLiteral(", ")));
                shaTooltipByTag.insert(
                    manifestTag,
                    shaTooltipParts.join(QStringLiteral("\n")).trimmed());
                downloadsByTag.insert(manifestTag,
                                      downloadParts.join(QStringLiteral(", ")));
                if (channel.compare(QStringLiteral("latest"),
                                    Qt::CaseInsensitive) == 0) {
                    latestChannelHtml = joined;
                    latestChannelTag = manifestTag;
                    latestChannelSize = sizeByTag.value(manifestTag);
                    latestChannelSha = shaByTag.value(manifestTag);
                    latestChannelShaTooltip = shaTooltipByTag.value(manifestTag);
                    latestChannelDownloads = downloadsByTag.value(manifestTag);
                }
            }
        }
    }

    int count = 0;
    QString currentTag;
    QByteArray out;
    // One batched for-each-ref call carries everything the row needs, including
    // the tag's target commit (peeled short sha for an annotated tag, or its own
    // short sha for a lightweight one) and tagger — spawning a git process per row
    // to fetch these froze the UI on repos with many refs before (issue #152), so
    // nothing extra is shelled out here per release.
    if (!dir.isEmpty() &&
        runGitCapture(dir,
                      {"for-each-ref", "--sort=-creatordate", "refs/tags",
                       "--format=%(refname:short)%1f"
                       "%(creatordate:format:%Y-%m-%d %H:%M)%1f"
                       "%(contents:subject)%1f"
                       "%(*objectname:short)%1f"
                       "%(objectname:short)%1f"
                       "%(taggername)%1f"
                       "%(contents:body)%1e"},
                      &out, nullptr)) {
        // Records are RS-separated (%1e) rather than newline-separated, because
        // the trailing contents:body field can itself contain embedded newlines
        // (the "What's Changed" bullet list) — splitting on '\n' would otherwise
        // shred one release's record into several bogus rows.
        QStringList tagLines;
        for (const QString &record :
             QString::fromUtf8(out).split(QLatin1Char('\x1e'))) {
            if (!record.trimmed().isEmpty())
                tagLines << record;
        }
        for (int i = 0; i < tagLines.size(); ++i) {
            const QStringList f = tagLines.at(i).split(QLatin1Char('\x1f'));
            if (f.isEmpty())
                continue;
            const QString tag = f.value(0).trimmed();
            if (tag.isEmpty())
                continue;
            // Tags are sorted newest-first, so the first one is the current
            // release shown at the top of the panel header (issue #226).
            if (currentTag.isEmpty())
                currentTag = tag;

            // The previous release (next-older tag by creation date) is the
            // Compare link's diff base — same ordering showReleaseDetail uses
            // (issue #284), just read off the row already fetched below instead
            // of another git call.
            QString prevTag;
            for (int j = i + 1; j < tagLines.size(); ++j) {
                const QString cand =
                    tagLines.at(j).split(QLatin1Char('\x1f')).value(0).trimmed();
                if (!cand.isEmpty()) {
                    prevTag = cand;
                    break;
                }
            }

            const int row = m_releasesTable->rowCount();
            m_releasesTable->insertRow(row);
            auto *tagItem = new QTableWidgetItem(tag);
            tagItem->setIcon(themedOcticon("tag", QColor("#a371f7"), 14));
            m_releasesTable->setItem(row, 0, tagItem);

            // Commit column: the tag's target commit. Peeled short sha for an
            // annotated tag (%(*objectname:short)); a lightweight tag's own
            // objectname is already the commit.
            QString commitSha = f.value(3).trimmed();
            if (commitSha.isEmpty())
                commitSha = f.value(4).trimmed();
            auto *commitItem = new QTableWidgetItem(commitSha);
            commitItem->setFont(QFont(QStringLiteral("monospace")));
            commitItem->setForeground(QColor("#8b949e"));
            m_releasesTable->setItem(row, 1, commitItem);

            // Parse the datetime string from git (format: "YYYY-MM-DD HH:MM")
            // and convert to relative "x ago" format
            const QString dateTimeStr = f.value(1).trimmed();
            QString relativeTime = dateTimeStr;
            if (!dateTimeStr.isEmpty()) {
                QDateTime dt = QDateTime::fromString(dateTimeStr, "yyyy-MM-dd hh:mm");
                if (dt.isValid()) {
                    relativeTime = formatIssueRelativeTime(dt.toMSecsSinceEpoch());
                }
            }
            auto *whenItem = new QTableWidgetItem(relativeTime);
            const QString tagger = f.value(5).trimmed();
            QString toolTip = dateTimeStr;
            if (!tagger.isEmpty()) {
                if (!toolTip.isEmpty())
                    toolTip += QStringLiteral(" · ");
                toolTip += QStringLiteral("Tagged by %1").arg(tagger);
            }
            if (!toolTip.isEmpty())
                whenItem->setToolTip(toolTip);
            m_releasesTable->setItem(row, 2, whenItem);

            // Release notes column: promptNewRelease defaults the tag message's
            // title to the tag name itself when the user leaves Title blank (the
            // usual case for auto-generated releases), so contents:subject alone
            // is almost always just a copy of the Tag column — not the release's
            // actual notes, which live in the tag body below the title. Prefer a
            // real title when one was typed; otherwise fall back to the first
            // line of the body (skipping the auto-generated "## What's Changed"
            // heading) so this column shows something distinct from Tag.
            const QString subject = f.value(2).trimmed();
            const QString body = f.value(6).trimmed();
            QString notesPreview = subject;
            if (notesPreview.isEmpty() ||
                notesPreview.compare(tag, Qt::CaseInsensitive) == 0) {
                for (const QString &rawLine : body.split(QLatin1Char('\n'))) {
                    QString line = rawLine.trimmed();
                    if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                        continue;
                    if (line.startsWith(QStringLiteral("* ")))
                        line = line.mid(2).trimmed();
                    notesPreview = line;
                    break;
                }
            }
            if (notesPreview.isEmpty())
                notesPreview = QStringLiteral("—");
            auto *notesItem = new QTableWidgetItem(notesPreview);
            const QString notesTooltip =
                body.isEmpty() ? subject : subject + QStringLiteral("\n\n") + body;
            if (!notesTooltip.trimmed().isEmpty())
                notesItem->setToolTip(notesTooltip);
            m_releasesTable->setItem(row, 3, notesItem);

            // Compare column: a GitHub-style link to the diff since the previous
            // release. The actual diff is computed on demand by showReleaseDetail
            // (the same dialog a row click opens) rather than here, so listing
            // releases never spawns a diff per row.
            if (prevTag.isEmpty()) {
                auto *initial = new QTableWidgetItem("Initial release");
                initial->setForeground(QColor("#8b949e"));
                m_releasesTable->setItem(row, 4, initial);
            } else {
                auto *compare = new QLabel(
                    QStringLiteral("<a href=\"#\" style=\"color:#58a6ff;"
                                   "text-decoration:none\">Compare %1...%2</a>")
                        .arg(prevTag.toHtmlEscaped(), tag.toHtmlEscaped()));
                compare->setTextFormat(Qt::RichText);
                compare->setContentsMargins(6, 0, 6, 0);
                compare->setCursor(Qt::PointingHandCursor);
                compare->setStyleSheet(QStringLiteral("background:transparent;"));
                connect(compare, &QLabel::linkActivated, this,
                        [this, tag] { showReleaseDetail(tag); });
                m_releasesTable->setCellWidget(row, 4, compare);
            }

            // Artifacts for this tag come from the channel manifest scanned above
            // (keyed by the manifest's own "tag" field), not a .forkmesh/releases/<tag>/ path.
            // The asset names are rendered as live-download links, so use a
            // rich-text label cell that opens the URL in the browser on click.
            QString artifactsHtml = artifactsByTag.value(tag);
            QString sizeText = sizeByTag.value(tag);
            QString shaText = shaByTag.value(tag);
            QString shaTooltip = shaTooltipByTag.value(tag);
            QString downloadsText = downloadsByTag.value(tag);
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
                sizeText = latestChannelSize;
                shaText = latestChannelSha;
                shaTooltip = latestChannelShaTooltip;
                downloadsText = latestChannelDownloads;
            }
            if (artifactsHtml.isEmpty()) {
                m_releasesTable->setItem(row, 5, new QTableWidgetItem(QString()));
            } else {
                auto *artifacts = new QLabel(artifactsHtml);
                artifacts->setTextFormat(Qt::RichText);
                artifacts->setOpenExternalLinks(true);
                artifacts->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
                artifacts->setContentsMargins(6, 0, 6, 0);
                artifacts->setCursor(Qt::PointingHandCursor);
                artifacts->setStyleSheet(QStringLiteral("background:transparent;"));
                m_releasesTable->setCellWidget(row, 5, artifacts);
            }

            auto *sizeItem = new QTableWidgetItem(
                sizeText.isEmpty() ? QStringLiteral("—") : sizeText);
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            sizeItem->setForeground(QColor("#8b949e"));
            m_releasesTable->setItem(row, 6, sizeItem);

            auto *shaItem = new QTableWidgetItem(
                shaText.isEmpty() ? QStringLiteral("—") : shaText);
            shaItem->setFont(QFont(QStringLiteral("monospace")));
            shaItem->setForeground(QColor("#8b949e"));
            if (!shaTooltip.isEmpty())
                shaItem->setToolTip(shaTooltip);
            m_releasesTable->setItem(row, 7, shaItem);

            // Downloads only reads as "0" once the relay's per-hash tally has
            // actually loaded (haveDownloadCounts); until then every asset shows
            // "—" rather than a misleading zero.
            auto *downloadsItem = new QTableWidgetItem(
                downloadsText.isEmpty() ? QStringLiteral("—") : downloadsText);
            downloadsItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            downloadsItem->setForeground(QColor("#8b949e"));
            m_releasesTable->setItem(row, 8, downloadsItem);

            auto *del = new QPushButton;
            del->setObjectName("issueIconButton");
            del->setFlat(true);
            del->setCursor(Qt::PointingHandCursor);
            del->setIcon(themedOcticon("trash", QColor("#f85149"), 15));
            del->setIconSize(QSize(15, 15));
            del->setToolTip(QStringLiteral("Delete tag %1").arg(tag));
            del->setEnabled(writable);
            connect(del, &QPushButton::clicked, this, [this, tag] { deleteTag(tag); });
            m_releasesTable->setCellWidget(row, 9, del);
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
    // Keep the current-release pill floating above the Releases tab (adhoc #69)
    // in sync after drafting/deleting a tag reloads this panel.
    if (m_releaseStrip) {
        m_releaseStrip->setText(currentTag);
        m_releaseStrip->setToolTip(
            currentTag.isEmpty()
                ? QString()
                : QStringLiteral("Current release: %1").arg(currentTag));
        positionReleaseStrip();
    }
    if (count == 0) {
        m_releasesTable->insertRow(0);
        auto *empty = new QTableWidgetItem(
            "No releases yet. Draft one to tag a commit in the repository.");
        empty->setForeground(QColor("#8b949e"));
        m_releasesTable->setItem(0, 0, empty);
    }
    // Refresh this repo's per-artifact download counts (throttled); the async
    // reply re-renders this panel so a freshly logged download shows up without
    // the user having to hit Refresh.
    if (!dlSource.isEmpty()) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (m_releaseDownloadsFetchSource != dlSource ||
            nowMs - m_releaseDownloadsFetchedMs > 15000) {
            m_releaseDownloadsFetchSource = dlSource;
            m_releaseDownloadsFetchedMs = nowMs;
            fetchReleaseDownloadCounts(dlOwner, dlRepo, dlSource);
        }
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
    m_mirrorNodesOnlineOnlyCheck = new QCheckBox(QStringLiteral("Online only"));
    m_mirrorNodesOnlineOnlyCheck->setChecked(true);
    m_mirrorNodesOnlineOnlyCheck->setToolTip(
        QStringLiteral("Show only mirror nodes that are online right now"));
    connect(m_mirrorNodesOnlineOnlyCheck, &QCheckBox::toggled, this,
            &MainWindow::loadMirrorNodesPanel);
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    refreshButton->setToolTip(QStringLiteral("Reload the local mirror nodes table"));
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this,
            &MainWindow::loadMirrorNodesPanel);
    addRefreshSpin(refreshButton);
    auto *refreshNodesButton = new QPushButton;
    refreshNodesButton->setObjectName("ghostButton");
    refreshNodesButton->setFixedSize(32, 30);
    refreshNodesButton->setAccessibleName(QStringLiteral("Refresh nodes"));
    refreshNodesButton->setCursor(Qt::PointingHandCursor);
    refreshNodesButton->setToolTip(QStringLiteral(
        "Ask online mirror nodes to immediately report their latest commit and "
        "mirror metadata"));
    setOcticon(refreshNodesButton, "broadcast", 16);
    connect(refreshNodesButton, &QPushButton::clicked, this,
            &MainWindow::requestMirrorNodesRefresh);
    headerRow->addWidget(heading);
    headerRow->addWidget(m_mirrorNodesSummary);
    headerRow->addStretch();
    headerRow->addWidget(m_mirrorNodesOnlineOnlyCheck);
    headerRow->addWidget(m_mirrorResetPinButton);
    headerRow->addWidget(refreshNodesButton);
    headerRow->addWidget(refreshButton);
    layout->addLayout(headerRow);

    auto *blurb = new QLabel(
        "Nodes across the network that keep a live mirror of this repository. "
        "Each node serves clones and browsing from its own copy; the commit and "
        "sync time show how fresh that copy is. An underlined value doesn't match "
        "the source of truth \xE2\x80\x94 that node is serving different data.");
    blurb->setObjectName("statusLine");
    blurb->setWordWrap(true);
    layout->addWidget(blurb);

    // The live activity dots no longer sit in this page as a "Live ›" row; they
    // float just above the Mirror nodes tab instead (adhoc #197). The strip is
    // created with the tab row and anchored by positionMirrorActivityStrip.

    m_mirrorNodesTable = new QTableWidget(0, MirrorNodeColumnCount);
    installColumnHeaderMenu(m_mirrorNodesTable); // 3-dots per-column menu (issue #318)
    m_mirrorNodesTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_mirrorNodesTable);
    m_mirrorNodesTable->setHorizontalHeaderLabels(
        {"Node", "Owner", "Latest commit", "Synced", "Size", "Issues", "Commits",
         "Branches", "Pulls", "Discussions", "CPU", "RAM", "Disk",
         "Platform", "Version", "Node id", "Clones", "Website", "Artifacts"});
    m_mirrorNodesTable->verticalHeader()->setVisible(false);
    m_mirrorNodesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mirrorNodesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mirrorNodesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mirrorNodesTable->setShowGrid(false);
    m_mirrorNodesTable->setWordWrap(false);
    m_mirrorNodesTable->setSortingEnabled(true);
    m_mirrorNodesTable->sortByColumn(MirrorNodeColNode,
                                     Qt::AscendingOrder); // source of truth first
    QHeaderView *mh = m_mirrorNodesTable->horizontalHeader();
    mh->setHighlightSections(false);
    mh->setSectionResizeMode(MirrorNodeColNode, QHeaderView::Stretch);
    mh->setSectionResizeMode(MirrorNodeColOwner, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColCommit, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColSynced, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColSize, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColIssues, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColCommits, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColBranches, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColPulls, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColDiscussions, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColCpu, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColRam, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColDisk, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColPlatform, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColVersion, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColId, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColClones, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColWebsite, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColArtifacts, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_mirrorNodesTable);
    // Synced column draws a pac-man countdown for behind nodes; a 1s timer
    // repaints the column so the chart animates while the panel is visible.
    m_mirrorNodesTable->setItemDelegateForColumn(
        MirrorNodeColSynced, new MirrorSyncDelegate(m_mirrorNodesTable));
    // CPU / RAM / disk columns render as little usage bars (details on hover).
    auto *resourceBars = new ResourceBarDelegate(m_mirrorNodesTable);
    for (int col : {MirrorNodeColCpu, MirrorNodeColRam, MirrorNodeColDisk})
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
            if (m_mirrorNodesTable->item(r, MirrorNodeColSynced))
                m_mirrorNodesTable->update(
                    m_mirrorNodesTable->model()->index(r, MirrorNodeColSynced));
        }
    });
    pacmanTick->start();
    // Double-click a node row to open its profile.
    // itemActivated (rather than cellDoubleClicked) so Enter opens the selected
    // node's profile, matching the tab's arrow-key navigation (adhoc #183).
    connect(m_mirrorNodesTable, &QTableWidget::itemActivated, this,
            [this](QTableWidgetItem *item) {
                QTableWidgetItem *it =
                    item ? m_mirrorNodesTable->item(item->row(), MirrorNodeColNode)
                         : nullptr;
                if (it) {
                    const QString nid = it->data(Qt::UserRole).toString();
                    if (!nid.isEmpty())
                        showNodeProfile(nid, it->text());
                }
            });
    layout->addWidget(m_mirrorNodesTable, 1);
    return page;
}

void MainWindow::requestMirrorNodesRefresh()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    if (!m_backend) {
        flashMessage(QStringLiteral("Connect to the mainnode before refreshing nodes."),
                     true);
        return;
    }

    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString name = repoSegment(repo.name, QStringLiteral("repository"));
    const QString source =
        repoSegment(repo.owner, QStringLiteral("owner")) + "/" + name;
    const QString ownerName = catalogOwner(repo) + "/" + name;
    if (source.section('/', 0, 0).isEmpty() || name.isEmpty())
        return;

    // Make our own response fresh too: rebuild the cached mirror adverts from disk
    // and force an immediate hello before asking peers to do the same.
    m_mirrorAdvertSig.clear();
    refreshRepositoryList();
    m_backend->advertiseMirrorsNow();
    m_backend->requestMirrorRefresh(source, ownerName);
    logSystem(QStringLiteral("Mirror nodes: requested live refresh for %1.")
                  .arg(source));
    flashMessage(QStringLiteral("Asked online mirror nodes to refresh."));
    loadMirrorNodesPanel();
}

void MainWindow::onMirrorRefreshRequested(const QString &source,
                                          const QString &requesterName)
{
    if (!m_backend)
        return;
    const QString requested = source.trimmed();
    bool mirrorsRequestedSource = requested.isEmpty();
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly)
            continue;
        const QString name = repoSegment(repo.name, QStringLiteral("repository"));
        const QString repoSource =
            repoSegment(repo.owner, QStringLiteral("owner")) + "/" + name;
        const QString ownerName = catalogOwner(repo) + "/" + name;
        if (requested.compare(repoSource, Qt::CaseInsensitive) == 0 ||
            requested.compare(ownerName, Qt::CaseInsensitive) == 0) {
            mirrorsRequestedSource = true;
            break;
        }
    }
    if (!mirrorsRequestedSource)
        return;

    // The request is explicit, so bypass the advert-signature cache and the
    // backend hello throttle: peers asked for the freshest commit/count/resource
    // snapshot we can report right now.
    m_mirrorAdvertSig.clear();
    refreshRepositoryList();
    m_backend->advertiseMirrorsNow();
    logSystem(QStringLiteral("Mirror nodes: sent fresh mirror metadata%1.")
                  .arg(requesterName.trimmed().isEmpty()
                           ? QString()
                           : QStringLiteral(" to %1").arg(requesterName.trimmed())));
}

void MainWindow::loadMirrorNodesPanel()
{
    if (!m_mirrorNodesTable)
        return;
    TableRepaintGuard repaintGuard(m_mirrorNodesTable);
    m_mirrorNodesTable->setSortingEnabled(false);
    m_mirrorNodesTable->setRowCount(0);
    const bool onlineOnly =
        !m_mirrorNodesOnlineOnlyCheck || m_mirrorNodesOnlineOnlyCheck->isChecked();

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        if (m_mirrorNodesSummary)
            m_mirrorNodesSummary->clear();
        if (m_mirrorResetPinButton)
            m_mirrorResetPinButton->hide();
        if (m_mirrorActivityStrip) {
            static_cast<MirrorActivityStrip *>(m_mirrorActivityStrip)->setNodes({});
            positionMirrorActivityStrip(); // hides the now-empty strip
        }
        if (m_relayRadar)
            static_cast<RelayRadarWidget *>(m_relayRadar)->setBlips({});
        m_mirrorNodesTable->setSortingEnabled(true);
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    // Keep the GUI breathing across this panel's synchronous git reads: ~10 for our
    // own selfAdvert (head/commit/size/counts/worktrees) plus a `git show` per roster
    // row for its commit subject. Rebuilt on every roster update, so without this the
    // main thread froze while a peer's presence flickered (adhoc #83).
    GitKeepAlive keepAlive;
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

    // For our own row, read the primary branch tip locally so it always reflects
    // main/default-branch freshness without waiting for a roster round-trip.
    MirrorAdvert selfAdvert;
    selfAdvert.ownerName = canonical;
    selfAdvert.source = source;
    const MirrorBranchTip selfTip =
        mirrorPrimaryBranchTip(localMirror, repo.localPath);
    selfAdvert.branch = selfTip.branch;
    selfAdvert.commit = selfTip.commit;
    const QString servedBranch = selfAdvert.branch;
    const QString servedCommit = mirrorBranchCommit(localMirror, servedBranch);
    selfAdvert.updatedMs = repo.lastSyncMs;
    selfAdvert.sizeBytes = mirrorRepoSizeBytes(localMirror);
    selfAdvert.issueCount = mirrorIssueCount(localMirror, selfAdvert.branch);
    selfAdvert.commitCount = mirrorCommitCount(localMirror, selfAdvert.branch);
    selfAdvert.branchCount = mirrorBranchCount(localMirror);
    selfAdvert.pullCount = mirrorPullCount(localMirror, selfAdvert.branch);
    selfAdvert.discussionCount = mirrorDiscussionCount(localMirror, selfAdvert.branch);
    selfAdvert.worktreeCount = mirrorWorktreeCount(repo.localPath);
    selfAdvert.artifactCount = mirrorArtifactCount(localMirror);

    // If we are the source of truth, our working copy can be ahead of the bare
    // mirror we serve (e.g. a comment was just committed and the mirror fetch
    // hasn't run/finished). Count those un-mirrored commits so the self row can
    // show a live "↑N to push" badge the moment a change is made.
    int pendingPush = 0;
    if (repoHasWorkingTree() && !repo.localPath.trimmed().isEmpty() &&
        !servedCommit.isEmpty()) {
        QByteArray out;
        const QString pushTarget = servedBranch.isEmpty()
                                       ? QStringLiteral("HEAD")
                                       : servedBranch;
        if (runGitCapture(repo.localPath,
                          {QStringLiteral("rev-list"), QStringLiteral("--count"),
                           servedCommit + QStringLiteral("..") + pushTarget},
                          &out, nullptr))
            pendingPush = QString::fromUtf8(out).trimmed().toInt();
    }

    // Resolve a node's advert for this repo: the shared source identity groups
    // every mirror, with a clone-name fallback for older peers, and our own row
    // always reads the live local primary branch via selfAdvert.
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

    auto advertisedNodeName = [](const MirrorAdvert *advert) {
        if (!advert || advert->ownerName.trimmed().isEmpty())
            return QString();
        return advert->ownerName.section(QLatin1Char('/'), 0, 0).trimmed();
    };
    auto displayNodeName = [&](const MemberInfo &node,
                               const MirrorAdvert *advert) {
        QString name = advertisedNodeName(advert);
        if (name.isEmpty() && !node.nodeName.trimmed().isEmpty())
            name = node.nodeName.trimmed();
        if (name.isEmpty() && node.self)
            name = accountOwner().trimmed();
        if (name.isEmpty())
            name = node.name.trimmed();
        return name;
    };
    auto displayOwnerName = [&](const MemberInfo &node) {
        QString owner = node.ownerUser.trimmed();
        if (owner.isEmpty() && node.self)
            owner = nodeOwnerDisplayName();
        return owner;
    };
    auto makeOwnerCell = [](const QString &owner) {
        auto *item = new QTableWidgetItem(
            owner.trimmed().isEmpty() ? QString::fromUtf8("\xE2\x80\x94")
                                      : owner.trimmed());
        item->setForeground(QColor("#8b949e"));
        if (!owner.trimmed().isEmpty())
            item->setToolTip(QStringLiteral("Owned by user %1").arg(owner.trimmed()));
        return item;
    };

    // One row per node *name*: a node that re-registers (reinstall → new key)
    // can transiently sit in the roster under two identities — the old key's
    // session still heartbeating beside the new one — which listed the same
    // node twice (adhoc #46). Keep the best entry per name: ourselves, else the
    // one online, else the one actually advertising this repo, else the freshest
    // advert, else the newer app version (a stale session lags after an upgrade).
    QList<MemberInfo> rosterNodes;
    QHash<QString, int> rosterIndexByName;
    for (const MemberInfo &node : std::as_const(m_homeRoster)) {
        bool namedOnly = false;
        const MirrorAdvert *advert = matchAdvert(node, namedOnly);
        if (!advert && !namedOnly)
            continue; // not mirroring this repo; the row loop skips these anyway
        const QString nameKey = displayNodeName(node, advert).trimmed().toLower();
        if (nameKey.isEmpty()) {
            rosterNodes.append(node);
            continue;
        }
        const auto it = rosterIndexByName.constFind(nameKey);
        if (it == rosterIndexByName.constEnd()) {
            rosterIndexByName.insert(nameKey, rosterNodes.size());
            rosterNodes.append(node);
            continue;
        }
        const MemberInfo &kept = rosterNodes.at(it.value());
        bool keptNamedOnly = false;
        const MirrorAdvert *keptAdvert = matchAdvert(kept, keptNamedOnly);
        bool replace;
        if (node.self != kept.self)
            replace = node.self;
        else if (node.online != kept.online)
            replace = node.online;
        else if ((advert != nullptr) != (keptAdvert != nullptr))
            replace = advert != nullptr;
        else if (advert && keptAdvert && advert->updatedMs != keptAdvert->updatedMs)
            replace = advert->updatedMs > keptAdvert->updatedMs;
        else
            replace = QVersionNumber::fromString(node.version) >
                      QVersionNumber::fromString(kept.version);
        if (replace)
            rosterNodes[it.value()] = node;
    }

    // The reference HEAD a node must match to count as "in sync": the source of
    // truth's commit if it advertises one, else the freshest-synced commit in the
    // group. Nodes whose commit differs are behind and get a heartbeat countdown.
    QString sourceCommit;
    QString newestCommit;
    qint64 newestMs = -1;
    // The advert those two commits came from, so the per-column mismatch check
    // below can compare a node's advertised counts (issues/commits/branches/…)
    // against the same reference node, not just its commit. Pointers into
    // rosterNodes' mirrorDetails / the local selfAdvert, both live to end-of-scope.
    const MirrorAdvert *sourceAdvert = nullptr;
    const MirrorAdvert *newestAdvert = nullptr;
    for (const MemberInfo &node : std::as_const(rosterNodes)) {
        bool namedOnly = false;
        const MirrorAdvert *advert = matchAdvert(node, namedOnly);
        if (!advert || advert->commit.isEmpty())
            continue;
        if (advert->ownerName == source ||
            displayNodeName(node, advert).compare(sourceOwner, Qt::CaseInsensitive) == 0) {
            sourceCommit = advert->commit;
            sourceAdvert = advert;
        }
        if (advert->updatedMs > newestMs) {
            newestMs = advert->updatedMs;
            newestCommit = advert->commit;
            newestAdvert = advert;
        }
    }
    const QString referenceCommit =
        !sourceCommit.isEmpty() ? sourceCommit : newestCommit;
    // The canonical per-column values every mirror should match. Prefer the
    // source of truth's advert (the owner's), else the freshest node's, mirroring
    // referenceCommit. A cell that differs from these gets underlined below, so a
    // node quietly serving different data than the source is visible at a glance.
    const MirrorAdvert *referenceAdvert =
        sourceAdvert ? sourceAdvert : newestAdvert;
    const int refIssues = referenceAdvert ? referenceAdvert->issueCount : -1;
    const int refCommits = referenceAdvert ? referenceAdvert->commitCount : -1;
    const int refBranches = referenceAdvert ? referenceAdvert->branchCount : -1;
    const int refPulls = referenceAdvert ? referenceAdvert->pullCount : -1;
    const int refDiscussions =
        referenceAdvert ? referenceAdvert->discussionCount : -1;
    const int refArtifacts = referenceAdvert ? referenceAdvert->artifactCount : -1;
    // Underline a cell whose content-derived value doesn't match the reference
    // node's, and note it in the tooltip. Only content columns (commit + the
    // metadata counts) are compared — per-node facts like CPU/version/clones are
    // expected to differ. Skipped when either side is unknown (em-dash) so an
    // older peer that doesn't advertise a field isn't falsely flagged.
    auto markMismatch = [](QTableWidgetItem *item, bool mismatch,
                           const QString &refText) {
        if (!item || !mismatch)
            return;
        QFont f = item->font();
        f.setUnderline(true);
        item->setFont(f);
        const QString note =
            QStringLiteral("Doesn't match the source of truth (%1)").arg(refText);
        item->setToolTip(item->toolTip().isEmpty()
                             ? note
                             : item->toolTip() + QStringLiteral("\n") + note);
    };
    auto countMismatch = [](int value, int ref) {
        return ref >= 0 && value >= 0 && value != ref;
    };

    // Clone / website-serve tallies are per-node local counters, carried across the
    // network only in each node's published catalog record. Index the catalog cache
    // by node name so the live-roster loop can fill the "Clones" / "Website" columns
    // for peers too (our own row reads the fresher local tally). Value = (clones,
    // website serves); -1 == the node hasn't advertised the count yet.
    QHash<QString, QPair<int, int>> serveCounts;
    // Per-node clone-integrity verdict from the same /mirrors payload:
    // "rejected" means the relay's integrity gate refuses every clone this node
    // serves, because the refs fingerprint it published matches no state the
    // source of truth attested (current pin or recent history).
    QHash<QString, QString> integrityByNode;
    if (m_catalogMirrorsSource == source) {
        for (const QJsonValue &value : std::as_const(m_catalogMirrorsCache)) {
            const QJsonObject m = value.toObject();
            const QString n = m.value("node").toString().trimmed().toLower();
            if (!n.isEmpty()) {
                serveCounts.insert(n, {m.value("clonesServed").toInt(-1),
                                       m.value("websiteServed").toInt(-1)});
                integrityByNode.insert(n, m.value("integrity").toString());
            }
        }
    }
    // A right-aligned tally cell: em-dash when the count is unknown (-1), else the
    // (abbreviated) number, sorting on the raw value.
    auto makeServeCountCell = [](int value, const QString &tip) -> SortTableWidgetItem * {
        auto *item = new SortTableWidgetItem(
            value >= 0 ? formatCount(value) : QString::fromUtf8("\xE2\x80\x94"));
        item->setData(kTableSortRole, double(value));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (value >= 0)
            item->setToolTip(tip);
        return item;
    };
    auto clonesTip = [](int n) {
        return QString::fromUtf8("Provided %1 clone%2").arg(n).arg(n == 1 ? "" : "s");
    };
    auto websiteTip = [](int n) {
        return QString::fromUtf8("Served the website %1 time%2")
            .arg(n)
            .arg(n == 1 ? "" : "s");
    };

    int count = 0;
    qint64 totalBytes = 0;     // data mirrored across every node in this group
    qint64 maxRepoBytes = 0;   // best (largest, == most complete) copy seen
    bool weAreSource = false;  // this node holds the source-of-truth copy
    int outOfSyncPeers = 0;    // other nodes whose served state != the source
    // Names already shown from the live chat roster, so the catalog-backed merge
    // below (issue #223) doesn't list a node twice when it's also present in chat.
    QSet<QString> shownNames;
    // Node ids (keys) already shown, so a catalog record published under a node's
    // former name doesn't add a second row for the same identity (adhoc #46).
    QSet<QString> shownIds;
    // One activity dot per active node, fed to the live strip atop the panel.
    QVector<MirrorActivityStrip::Dot> activityDots;
    // Build a right-aligned numeric count cell (Commits/Branches/Pulls/
    // Discussions): the figure, an em-dash when the node doesn't advertise it (-1, an
    // older peer), and a singular/plural tooltip. Shared by the live-roster rows and
    // the catalog-backed rows below so both render these columns identically.
    auto makeCountCell = [](int n, const QString &singular,
                            const QString &plural) -> SortTableWidgetItem * {
        auto *item = new SortTableWidgetItem(
            n >= 0 ? QString::number(n) : QString::fromUtf8("\xE2\x80\x94"));
        item->setData(kTableSortRole, double(n));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (n >= 0)
            item->setToolTip(
                QStringLiteral("%1 %2").arg(n).arg(n == 1 ? singular : plural));
        return item;
    };
    // Mark a node the relay refuses to serve because of the integrity pin: the
    // clone gate rejects every clone from it until the node syncs to a state the
    // source of truth attested (or the owner resets the pin). Applied to the
    // Node cell of both live-roster and catalog-backed rows.
    int pinRejectedNodes = 0;
    auto markPinRejected = [&pinRejectedNodes](QTableWidgetItem *item, bool isSelf) {
        ++pinRejectedNodes;
        item->setText(item->text() +
                      QString::fromUtf8("  \xE2\x9A\xA0 failing integrity pin"));
        item->setForeground(QColor("#f85149"));
        const QString note =
            isSelf
                ? QString::fromUtf8(
                      "Clones of this repo are being rejected: the relay's pinned "
                      "hash no longer matches the refs this node serves. Use "
                      "\xE2\x80\x9CReset integrity pin\xE2\x80\x9D above to re-sign "
                      "the current refs and clear it.")
                : QString::fromUtf8(
                      "Clones from this node are being rejected: the refs it serves "
                      "match no state the source of truth attested (integrity pin). "
                      "This clears once the node syncs \xE2\x80\x94 or, if the node "
                      "is already up to date, when the owner resets the pin.");
        item->setToolTip(item->toolTip().isEmpty()
                             ? note
                             : item->toolTip() + QStringLiteral("\n\n") + note);
    };
    for (const MemberInfo &node : std::as_const(rosterNodes)) {
        bool namedOnly = false;
        const MirrorAdvert *advert = matchAdvert(node, namedOnly);
        if (!advert && !namedOnly)
            continue;

        // Node: green/grey dot + name (+ "you") (+ source-of-truth tag). Our own
        // row also fails here when the relay's pin has drifted past what we
        // serve (m_repoPinMismatch, adhoc #65) — the relay's /mirrors payload
        // can't see that on its own, since it only knows the hash we last
        // published, not our live refs.
        const bool online = node.self ? (m_backend != nullptr) : node.online;
        const bool integrityFailing =
            integrityByNode.value(displayNodeName(node, advert).trimmed().toLower()) ==
                QLatin1String("rejected") ||
            (node.self && m_repoPinMismatch);
        if (onlineOnly && !online)
            continue;
        // An online node serving a commit that isn't the source-of-truth's is out
        // of sync: the source's instant mirror-update signal normally converges it
        // in seconds, with its next heartbeat as the fallback. Computed here (ahead
        // of the Node dot) so the dot can go amber; the Synced cell reuses it below.
        const bool behind = online && advert && !advert->commit.isEmpty() &&
                            !referenceCommit.isEmpty() &&
                            advert->commit != referenceCommit;

        const QString nodeDisplay = displayNodeName(node, advert);
        const QString ownerDisplay = displayOwnerName(node);
        shownNames.insert(nodeDisplay.trimmed().toLower());
        if (!node.id.isEmpty())
            shownIds.insert(node.id);

        // The source of truth: the node whose clone identity equals the shared
        // source (the owner advertises ownerName == source); also match by name.
        const bool isSource =
            (advert && advert->ownerName == source) ||
            nodeDisplay.compare(sourceOwner, Qt::CaseInsensitive) == 0;

        const int row = m_mirrorNodesTable->rowCount();
        m_mirrorNodesTable->insertRow(row);

        // Only online nodes normally get a dot; keep an offline one too when
        // it's failing the integrity pin, so the warning doesn't just vanish
        // from the strip (adhoc #196).
        if (online || integrityFailing)
            activityDots.append(
                {node.id, nodeDisplay, online, node.self, behind, integrityFailing});
        auto *nameItem = new SortTableWidgetItem(
            nodeDisplay + (node.self ? QStringLiteral("  (you)") : QString()) +
            (isSource ? QString::fromUtf8("  \xE2\x98\x85 source of truth")
                      : QString()));
        // Green when online and in sync, amber when online but out of sync
        // (behind the source of truth), grey when offline.
        nameItem->setIcon(themedOcticon(
            "broadcast",
            QColor(!online ? "#8b949e" : behind ? "#d29922" : "#3fb950"), 14));
        nameItem->setData(Qt::UserRole, node.id);
        // Source-of-truth rows sort to the top (★ < letters), then by name.
        nameItem->setData(kTableSortRole,
                          (isSource ? QStringLiteral("0") : QStringLiteral("1")) +
                              nodeDisplay.toLower());
        nameItem->setToolTip(
            isSource ? QString::fromUtf8("Source of truth \xC2\xB7 %1")
                           .arg(online ? "online" : "offline")
                     : (online ? (behind ? QString::fromUtf8(
                                               "Online \xC2\xB7 out of sync")
                                         : QStringLiteral("Online now"))
                               : QStringLiteral("Offline")));
        if (!node.name.trimmed().isEmpty() &&
            node.name.compare(nodeDisplay, Qt::CaseInsensitive) != 0)
            nameItem->setToolTip(nameItem->toolTip() + QStringLiteral("\nChat: ") +
                                 node.name.trimmed());
        if (integrityFailing)
            markPinRejected(nameItem, node.self);
        m_mirrorNodesTable->setItem(row, MirrorNodeColNode, nameItem);
        m_mirrorNodesTable->setItem(row, MirrorNodeColOwner,
                                    makeOwnerCell(ownerDisplay));

        // Latest commit: short hash + branch; tooltip carries the subject/date
        // when we hold the same commit in our own mirror.
        QString commitText = QString::fromUtf8("\xE2\x80\x94");
        QString commitTip;
        if (advert && !advert->commit.isEmpty()) {
            commitText = advert->commit.left(10);
            if (!advert->branch.isEmpty())
                commitText += "  (" + advert->branch + ")";
            commitTip = advert->commit;
            // Commit subjects are immutable per hash — cache them so the panel's
            // roster-driven rebuilds don't re-shell one `git show` per row every
            // time a peer's presence flickers (stall log: loadMirrorNodesPanel
            // <- setRoster).
            if (m_commitSubjectCache.size() > 5000)
                m_commitSubjectCache.clear(); // safety valve, never hit in practice
            auto cached = m_commitSubjectCache.constFind(advert->commit);
            if (cached == m_commitSubjectCache.constEnd()) {
                QByteArray subject;
                QString s;
                if (!localMirror.isEmpty() &&
                    runGitCapture(localMirror,
                                  {"show", "-s", "--format=%s", advert->commit},
                                  &subject, nullptr))
                    s = QString::fromUtf8(subject).trimmed();
                cached = m_commitSubjectCache.insert(advert->commit, s);
            }
            if (!cached.value().isEmpty())
                commitTip = cached.value() + "\n" + advert->commit;
        }
        auto *commitItem = new QTableWidgetItem(commitText);
        commitItem->setToolTip(commitTip);
        markMismatch(commitItem,
                     advert && !advert->commit.isEmpty() &&
                         !referenceCommit.isEmpty() &&
                         advert->commit != referenceCommit,
                     referenceCommit.left(10));
        m_mirrorNodesTable->setItem(row, MirrorNodeColCommit, commitItem);

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
        // offline rows carry no anchor and render as plain text. (`behind` is
        // computed above so the Node dot can also go amber for it.) The source
        // broadcasts an instant mirror-update signal over the websocket when it
        // advances, so a behind node normally converges in seconds; the
        // heartbeat re-sync the countdown shows is the fallback for a node that
        // missed the ephemeral signal.
        if (behind) {
            syncedItem->setData(kPacmanAnchorRole,
                                static_cast<qlonglong>(advert->updatedMs));
            syncedItem->setToolTip(QString::fromUtf8(
                "Behind the source \xC2\xB7 signalled to sync now; its next "
                "heartbeat is the fallback"));
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
        m_mirrorNodesTable->setItem(row, MirrorNodeColSynced, syncedItem);

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
        m_mirrorNodesTable->setItem(row, MirrorNodeColSize, sizeItem);
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
        markMismatch(issuesItem, countMismatch(nodeIssues, refIssues),
                     QString::number(refIssues));
        m_mirrorNodesTable->setItem(row, MirrorNodeColIssues, issuesItem);

        // Commits / Branches / Pulls / Discussions: more per-node tallies
        // advertised alongside the issue count, so the panel shows how much
        // history each node mirrors and how busy it is. Em-dash for older peers.
        const int nodeCommits = advert ? advert->commitCount : -1;
        const int nodeBranches = advert ? advert->branchCount : -1;
        const int nodePulls = advert ? advert->pullCount : -1;
        const int nodeDiscussions = advert ? advert->discussionCount : -1;
        auto *commitsItem = makeCountCell(nodeCommits, "commit", "commits");
        markMismatch(commitsItem, countMismatch(nodeCommits, refCommits),
                     QString::number(refCommits));
        m_mirrorNodesTable->setItem(row, MirrorNodeColCommits, commitsItem);
        auto *branchesItem = makeCountCell(nodeBranches, "branch", "branches");
        markMismatch(branchesItem, countMismatch(nodeBranches, refBranches),
                     QString::number(refBranches));
        m_mirrorNodesTable->setItem(row, MirrorNodeColBranches, branchesItem);
        auto *pullsItem =
            makeCountCell(nodePulls, "pull request", "pull requests");
        markMismatch(pullsItem, countMismatch(nodePulls, refPulls),
                     QString::number(refPulls));
        m_mirrorNodesTable->setItem(row, MirrorNodeColPulls, pullsItem);
        auto *discussionsItem =
            makeCountCell(nodeDiscussions, "discussion", "discussions");
        markMismatch(discussionsItem,
                     countMismatch(nodeDiscussions, refDiscussions),
                     QString::number(refDiscussions));
        m_mirrorNodesTable->setItem(row, MirrorNodeColDiscussions,
                                    discussionsItem);

        // CPU / RAM / disk usage bars (hover for the underlying figures). The
        // telemetry is per-node, advertised in the node's heartbeats; peers that
        // don't advertise it (older builds) leave the bars as an em-dash.
        m_mirrorNodesTable->setItem(row, MirrorNodeColCpu,
                                    makeCpuUsageCell(node.cpuPercent));
        m_mirrorNodesTable->setItem(
            row, MirrorNodeColRam,
            makeByteUsageCell("RAM", node.memUsedBytes, node.memTotalBytes));
        m_mirrorNodesTable->setItem(
            row, MirrorNodeColDisk,
            makeByteUsageCell("Disk", node.diskUsedBytes, node.diskTotalBytes));

        m_mirrorNodesTable->setItem(
            row, MirrorNodeColPlatform,
            new QTableWidgetItem(node.platform.isEmpty()
                                     ? QString::fromUtf8("\xE2\x80\x94")
                                     : node.platform));
        m_mirrorNodesTable->setItem(
            row, MirrorNodeColVersion,
            new QTableWidgetItem(node.version.isEmpty()
                                     ? QString::fromUtf8("\xE2\x80\x94")
                                     : node.version));
        auto *idItem = new QTableWidgetItem(
            node.id.left(12) + (node.id.size() > 12 ? QString::fromUtf8("\xE2\x80\xA6")
                                                    : QString()));
        idItem->setToolTip(node.id);
        m_mirrorNodesTable->setItem(row, MirrorNodeColId, idItem);

        // Clones / website serves: per-node local counters. Our own row reads the
        // freshest count straight from the local tally (keyed as onRequestServed
        // writes it); peers come from their published catalog record (serveCounts).
        int nodeClones = -1, nodeWebsite = -1;
        if (node.self) {
            const QPair<int, int> s =
                m_repoStats.value(catalogOwner(repo) + "/" + repo.name);
            nodeClones = s.second;
            nodeWebsite = qMax(0, s.first - s.second);
        } else {
            const QPair<int, int> s =
                serveCounts.value(nodeDisplay.trimmed().toLower(), {-1, -1});
            nodeClones = s.first;
            nodeWebsite = s.second;
        }
        m_mirrorNodesTable->setItem(row, MirrorNodeColClones,
                                    makeServeCountCell(nodeClones, clonesTip(nodeClones)));
        m_mirrorNodesTable->setItem(
            row, MirrorNodeColWebsite,
            makeServeCountCell(nodeWebsite, websiteTip(nodeWebsite)));
        // Artifacts: how many release binaries this node is hosting for download
        // in its content-addressed store (issue #304). A mirror replicates these
        // separately from git, so the count reflects what it can actually serve.
        const int nodeArtifacts = advert ? advert->artifactCount : -1;
        auto *artifactsItem =
            makeCountCell(nodeArtifacts, "artifact", "artifacts");
        markMismatch(artifactsItem, countMismatch(nodeArtifacts, refArtifacts),
                     QString::number(refArtifacts));
        m_mirrorNodesTable->setItem(row, MirrorNodeColArtifacts, artifactsItem);
        ++count;
    }

    // --- Catalog-backed mirrors (issue #223) --------------------------------
    // The loop above only sees nodes currently live in the chat room, so a
    // mirror with intermittent presence is invisible to the owner. Supplement
    // with the worker's /mirrors list — every node that has published a mirror
    // record for this source — adding any not already shown from the roster.
    // Collect catalog-only mirrors (not in roster) for activity dots so the dots
    // reflect the server's canonical mirror order, making it clear which dot
    // represents which node when they pulse (adhoc #218).
    QVector<MirrorActivityStrip::Dot> catalogOnlyDots;
    if (m_catalogMirrorsSource == source) {
        for (const QJsonValue &value : std::as_const(m_catalogMirrorsCache)) {
            const QJsonObject m = value.toObject();
            const QString nodeName = m.value("node").toString().trimmed();
            if (nodeName.isEmpty() || shownNames.contains(nodeName.toLower()))
                continue;
            // A record published under the node's former name: that identity is
            // already listed from the live roster under its current name, so a
            // second row would double-count the machine (adhoc #46).
            const QString catalogId = m.value("id").toString().trimmed();
            if (!catalogId.isEmpty() && shownIds.contains(catalogId))
                continue;
            shownNames.insert(nodeName.toLower());
            const bool isSource =
                nodeName.compare(sourceOwner, Qt::CaseInsensitive) == 0;
            const bool online =
                m.value("status").toString() == QLatin1String("online");
            const bool integrityFailing =
                m.value("integrity").toString() == QLatin1String("rejected");
            if (onlineOnly && !online)
                continue;
            // Online catalog node serving a commit other than the source of
            // truth's is out of sync — its dot goes amber like the live rows.
            const QString catCommit = m.value("commit").toString();
            const bool behind = online && !catCommit.isEmpty() &&
                                !referenceCommit.isEmpty() &&
                                catCommit != referenceCommit;
            // Only online nodes normally get a dot; keep an offline one too
            // when it's failing the integrity pin (adhoc #196).
            if (online || integrityFailing)
                catalogOnlyDots.append({m.value("id").toString(), nodeName,
                                        online, false, behind, integrityFailing});
            const int row = m_mirrorNodesTable->rowCount();
            m_mirrorNodesTable->insertRow(row);
            const QString ownerUser = m.value(QStringLiteral("ownerUser"))
                                          .toString()
                                          .trimmed();
            auto *nameItem = new SortTableWidgetItem(
                nodeName + (isSource
                                ? QString::fromUtf8("  \xE2\x98\x85 source of truth")
                                : QString()));
            nameItem->setIcon(themedOcticon(
                "broadcast",
                QColor(!online ? "#8b949e" : behind ? "#d29922" : "#3fb950"),
                14));
            nameItem->setData(kTableSortRole,
                              (isSource ? QStringLiteral("0") : QStringLiteral("1")) +
                                  nodeName.toLower());
            nameItem->setToolTip(
                online
                    ? (behind ? QString::fromUtf8("Online \xC2\xB7 out of sync")
                              : QStringLiteral("Online now"))
                    : QStringLiteral("Published mirror \xC2\xB7 not in the live room"));
            if (integrityFailing)
                markPinRejected(nameItem, false);
            m_mirrorNodesTable->setItem(row, MirrorNodeColNode, nameItem);
            m_mirrorNodesTable->setItem(row, MirrorNodeColOwner,
                                        makeOwnerCell(ownerUser));
            // Latest commit: the publishing node mirrors its served HEAD into the
            // catalog record, so even an offline node shows its commit (adhoc #56).
            // (`catCommit` is read above so the dot can go amber for out-of-sync.)
            QString catCommitText = QString::fromUtf8("\xE2\x80\x94");
            if (!catCommit.isEmpty()) {
                catCommitText = catCommit.left(10);
                const QString catBranch = m.value("branch").toString();
                if (!catBranch.isEmpty())
                    catCommitText += "  (" + catBranch + ")";
            }
            auto *catCommitItem = new QTableWidgetItem(catCommitText);
            if (!catCommit.isEmpty())
                catCommitItem->setToolTip(catCommit);
            markMismatch(catCommitItem,
                         !catCommit.isEmpty() && !referenceCommit.isEmpty() &&
                             catCommit != referenceCommit,
                         referenceCommit.left(10));
            m_mirrorNodesTable->setItem(row, MirrorNodeColCommit, catCommitItem);
            const qint64 syncedSecs = qint64(m.value("lastSync").toDouble()) / 1000;
            auto *syncedItem = new SortTableWidgetItem(
                syncedSecs > 0 ? formatShortRelativeTime(syncedSecs) + " ago"
                               : QString::fromUtf8("\xE2\x80\x94"));
            syncedItem->setData(kTableSortRole, double(syncedSecs));
            if (syncedSecs > 0)
                syncedItem->setToolTip(
                    QDateTime::fromSecsSinceEpoch(syncedSecs).toString(Qt::ISODate));
            m_mirrorNodesTable->setItem(row, MirrorNodeColSynced, syncedItem);
            const qint64 nodeBytes = qint64(m.value("sizeBytes").toDouble());
            auto *sizeItem = new SortTableWidgetItem(
                nodeBytes > 0 ? formatByteSize(nodeBytes)
                              : QString::fromUtf8("\xE2\x80\x94"));
            sizeItem->setData(kTableSortRole, double(nodeBytes));
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_mirrorNodesTable->setItem(row, MirrorNodeColSize, sizeItem);
            if (nodeBytes > 0) {
                totalBytes += nodeBytes;
                maxRepoBytes = qMax(maxRepoBytes, nodeBytes);
            }
            // Issues / commit / branch / pull / discussion counts / platform /
            // version / node id: also mirrored into the catalog record by the
            // publishing node, so they show for an offline node too (adhoc #56).
            // Only the live CPU/RAM/disk telemetry (cols 10-12) stays unknown for
            // catalog rows — it's broadcast per heartbeat, never stored.
            const int catIssues = m.value("issueCount").toInt(-1);
            auto *catIssuesItem = new SortTableWidgetItem(
                catIssues >= 0 ? QString::number(catIssues)
                               : QString::fromUtf8("\xE2\x80\x94"));
            catIssuesItem->setData(kTableSortRole, double(catIssues));
            catIssuesItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (catIssues >= 0)
                catIssuesItem->setToolTip(QString::fromUtf8("Mirroring %1 issue%2")
                                              .arg(catIssues)
                                              .arg(catIssues == 1 ? "" : "s"));
            markMismatch(catIssuesItem, countMismatch(catIssues, refIssues),
                         QString::number(refIssues));
            m_mirrorNodesTable->setItem(row, MirrorNodeColIssues, catIssuesItem);
            const int catCommits = m.value("commitCount").toInt(-1);
            const int catBranches = m.value("branchCount").toInt(-1);
            const int catPulls = m.value("pullCount").toInt(-1);
            const int catDiscussions = m.value("discussionCount").toInt(-1);
            auto *catCommitsItem = makeCountCell(catCommits, "commit", "commits");
            markMismatch(catCommitsItem, countMismatch(catCommits, refCommits),
                         QString::number(refCommits));
            m_mirrorNodesTable->setItem(row, MirrorNodeColCommits, catCommitsItem);
            auto *catBranchesItem =
                makeCountCell(catBranches, "branch", "branches");
            markMismatch(catBranchesItem, countMismatch(catBranches, refBranches),
                         QString::number(refBranches));
            m_mirrorNodesTable->setItem(row, MirrorNodeColBranches,
                                        catBranchesItem);
            auto *catPullsItem =
                makeCountCell(catPulls, "pull request", "pull requests");
            markMismatch(catPullsItem, countMismatch(catPulls, refPulls),
                         QString::number(refPulls));
            m_mirrorNodesTable->setItem(row, MirrorNodeColPulls, catPullsItem);
            auto *catDiscussionsItem =
                makeCountCell(catDiscussions, "discussion", "discussions");
            markMismatch(catDiscussionsItem,
                         countMismatch(catDiscussions, refDiscussions),
                         QString::number(refDiscussions));
            m_mirrorNodesTable->setItem(row, MirrorNodeColDiscussions,
                                        catDiscussionsItem);
            for (int col : {MirrorNodeColCpu, MirrorNodeColRam, MirrorNodeColDisk})
                m_mirrorNodesTable->setItem(row, col,
                                            makeResourceBarCell(-1, QString()));
            const QString catPlatform = m.value("platform").toString();
            m_mirrorNodesTable->setItem(
                row, MirrorNodeColPlatform,
                new QTableWidgetItem(catPlatform.isEmpty()
                                         ? QString::fromUtf8("\xE2\x80\x94")
                                         : catPlatform));
            const QString catVersion = m.value("version").toString();
            m_mirrorNodesTable->setItem(
                row, MirrorNodeColVersion,
                new QTableWidgetItem(catVersion.isEmpty()
                                         ? QString::fromUtf8("\xE2\x80\x94")
                                         : catVersion));
            const QString catId = m.value("id").toString();
            auto *catIdItem = new QTableWidgetItem(
                catId.isEmpty()
                    ? QString::fromUtf8("\xE2\x80\x94")
                    : catId.left(12) + (catId.size() > 12
                                            ? QString::fromUtf8("\xE2\x80\xA6")
                                            : QString()));
            if (!catId.isEmpty())
                catIdItem->setToolTip(catId);
            m_mirrorNodesTable->setItem(row, MirrorNodeColId, catIdItem);
            // Clones / website serves the publishing node reported (adhoc #56 kin);
            // an em-dash for records predating the counters.
            const int catClones = m.value("clonesServed").toInt(-1);
            const int catWebsite = m.value("websiteServed").toInt(-1);
            m_mirrorNodesTable->setItem(
                row, MirrorNodeColClones,
                makeServeCountCell(catClones, clonesTip(catClones)));
            m_mirrorNodesTable->setItem(
                row, MirrorNodeColWebsite,
                makeServeCountCell(catWebsite, websiteTip(catWebsite)));
            // Artifacts the publishing node reported hosting for download, so the
            // count shows for an offline node too.
            const int catArtifacts = m.value("artifactCount").toInt(-1);
            auto *catArtifactsItem =
                makeCountCell(catArtifacts, "artifact", "artifacts");
            markMismatch(catArtifactsItem,
                         countMismatch(catArtifacts, refArtifacts),
                         QString::number(refArtifacts));
            m_mirrorNodesTable->setItem(row, MirrorNodeColArtifacts,
                                        catArtifactsItem);
            ++count;
        }
    }
    // Append catalog-only mirrors to the activity dots so they're in the server's
    // canonical order, making it unambiguous which dot represents which node when
    // they pulse (adhoc #218).
    activityDots.append(catalogOnlyDots);

    // Refresh the catalog mirror list; the async reply re-renders this panel so
    // newly-discovered mirrors appear without a restart. Opening a different
    // repo fetches immediately; while the same repo stays open, roster flicker
    // re-runs this panel constantly, so re-hit the relay only every few minutes
    // — the roster itself already carries the live peers' mirror adverts, and
    // /mirrors only adds nodes that are currently offline (those change slowly).
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_catalogMirrorsFetchSource != source ||
        nowMs - m_catalogMirrorsFetchedMs > 5 * 60 * 1000) {
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

    // Echo the same mirror-node dots as blips inside the relay radar, reusing
    // their colours/shapes so the two read as one thing (adhoc #122).
    if (m_relayRadar) {
        QVector<RelayRadarWidget::Blip> blips;
        blips.reserve(activityDots.size());
        for (const MirrorActivityStrip::Dot &d : activityDots)
            blips.append(RelayRadarWidget::Blip{d.id, d.online, d.behind,
                                                d.integrityFailing});
        static_cast<RelayRadarWidget *>(m_relayRadar)->setBlips(blips);
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
        // Nodes the relay's integrity gate is refusing to serve. Shown to every
        // viewer (anyone cloning via one of these nodes is affected), not just
        // the owner — the table rows carry the same per-node flag.
        if (pinRejectedNodes > 0)
            text += QString::fromUtf8(
                        " \xC2\xB7 <span style='color:#f85149'>\xE2\x9A\xA0 %1 "
                        "node%2 failing the integrity pin</span>")
                        .arg(pinRejectedNodes)
                        .arg(pinRejectedNodes == 1 ? "" : "s");
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
            onlineOnly
                ? "No online nodes are advertising a mirror of this repository right now."
                : "No other nodes are advertising a mirror of this repository yet.");
        empty->setForeground(QColor("#8b949e"));
        m_mirrorNodesTable->setItem(0, MirrorNodeColNode, empty);
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
    // Mirror discovery re-fires on every roster flicker; back it off
    // exponentially while the relay is failing (offline / HTTP 429) so a rate-
    // limited relay isn't re-queried on each presence blip.
    const QString backoffKey = url.toString();
    if (!m_pollBackoff.ready(backoffKey, QDateTime::currentMSecsSinceEpoch()))
        return;
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, source, backoffKey]() {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        const QJsonObject resp = QJsonDocument::fromJson(body).object();
        if (!resp.value("ok").toBool()) {
            m_pollBackoff.noteFailure(backoffKey,
                                      QDateTime::currentMSecsSinceEpoch());
            return;
        }
        m_pollBackoff.noteSuccess(backoffKey);
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

void MainWindow::fetchReleaseDownloadCounts(const QString &owner, const QString &repo,
                                            const QString &source)
{
    // The relay logs a row every time it streams a release asset out of a
    // node's content-addressed store (its /releases/blob/sha256/<hash> route)
    // and exposes the per-hash tally here. We cache the result and merge it
    // into loadReleasesPanel(). Public read — no auth token required.
    if (!m_networkAccess || owner.isEmpty() || repo.isEmpty())
        return;
    QUrl url = catalogApiUrl(); // same host/scheme as the catalog
    url.setPath(QStringLiteral("/api/repo/%1/%2/releases/downloads")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(owner)),
                         QString::fromUtf8(QUrl::toPercentEncoding(repo))));
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, source]() {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        const QJsonObject resp = QJsonDocument::fromJson(body).object();
        if (!resp.value("ok").toBool())
            return;
        m_releaseDownloadsSource = source;
        m_releaseDownloadsCache.clear();
        const QJsonObject counts = resp.value("counts").toObject();
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
            m_releaseDownloadsCache.insert(it.key(), it.value().toInt());
        // Re-render only if the user is still viewing this repo's Releases tab,
        // so a freshly logged download shows up without a manual refresh.
        if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
            const RepositoryRecord &r = m_repositories.at(m_repoDetailIndex);
            const QString cur =
                repoSegment(r.owner, QStringLiteral("owner")) + "/" +
                repoSegment(r.name, QStringLiteral("repository"));
            if (cur == source)
                loadReleasesPanel();
        }
    });
}

void MainWindow::replicateReleaseArtifacts(int index)
{
    if (!m_networkAccess || index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(index);
    if (repo.previewOnly)
        return;
    const QString mirrorPath = repo.mirrorPath;
    if (mirrorPath.trimmed().isEmpty() || !QDir(mirrorPath).exists())
        return;
    const QString branch = mirrorHeadBranch(mirrorPath);
    if (branch.isEmpty())
        return;

    // Release manifests are committed metadata (.forkmesh/releases/<channel>/release.json)
    // that git already mirrors; only the binary bytes live out of git in the
    // per-node content-addressed store (issue #304). Read every channel's manifest
    // from the served branch, collect the asset blob hashes we don't already hold,
    // and remember the owner/repo that stages each one so we can pull it.
    QByteArray channelsOut;
    if (!runGitCapture(mirrorPath,
                       {QStringLiteral("ls-tree"), QStringLiteral("-z"),
                        QStringLiteral("--name-only"),
                        branch + QStringLiteral(":.forkmesh/releases")},
                       &channelsOut, nullptr))
        return; // no .forkmesh/releases/ tree on this branch — nothing to mirror
    static const QRegularExpression sha256Re(QStringLiteral("\\A[0-9a-f]{64}\\z"));
    // Every mirror of this repo shares the same source identity; a manifest that
    // doesn't name its own staging repo falls back to it.
    const QString fallbackRepo =
        repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
        repoSegment(repo.name, QStringLiteral("repository"));
    QMap<QString, QString> pending; // blob sha256 -> "owner/name" to download from
    for (const QByteArray &raw : channelsOut.split('\0')) {
        const QString channel = QString::fromUtf8(raw).trimmed();
        if (channel.isEmpty())
            continue;
        QByteArray manifestOut;
        if (!runGitCapture(mirrorPath,
                           {QStringLiteral("show"),
                            branch + QStringLiteral(":.forkmesh/releases/") + channel +
                                QStringLiteral("/release.json")},
                           &manifestOut, nullptr))
            continue;
        const QJsonObject obj = QJsonDocument::fromJson(manifestOut).object();
        const QString manifestRepo =
            obj.value(QStringLiteral("repo")).toString().trimmed();
        const QString downloadRepo =
            manifestRepo.isEmpty() ? fallbackRepo : manifestRepo;
        const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &asset : assets) {
            const QString hash = asset.toObject()
                                     .value(QStringLiteral("blob_sha256"))
                                     .toString()
                                     .trimmed()
                                     .toLower();
            if (!sha256Re.match(hash).hasMatch())
                continue;
            if (QFile::exists(mirrorReleaseBlobPath(mirrorPath, hash)))
                continue; // already hosting this artifact
            // A blob whose only source node is offline (relay answers 503)
            // used to be re-requested on every roster flicker and periodic
            // auto-sync, forever — hammering the relay with predictable
            // failures. Back failing hashes off exponentially instead; a
            // successful download clears the streak.
            if (!m_pollBackoff.ready(QStringLiteral("releaseBlob:") + hash,
                                     QDateTime::currentMSecsSinceEpoch()))
                continue;
            if (!pending.contains(hash))
                pending.insert(hash, downloadRepo);
        }
    }
    if (pending.isEmpty())
        return;
    logSystem(QStringLiteral("Mirror: fetching %1 release artifact%2 for %3/%4 so "
                             "this node can serve them.")
                  .arg(pending.size())
                  .arg(pending.size() == 1 ? "" : "s")
                  .arg(repo.owner, repo.name));
    // Pull them one at a time so a multi-asset release doesn't open a dozen
    // parallel binary streams at once.
    downloadNextReleaseBlob(index, mirrorPath, pending);
}

void MainWindow::downloadNextReleaseBlob(int index, const QString &mirrorPath,
                                         QMap<QString, QString> pending)
{
    if (pending.isEmpty() || !m_networkAccess)
        return;
    auto it = pending.begin();
    const QString hash = it.key();
    const QString downloadRepo = it.value();
    pending.erase(it);

    const int slash = downloadRepo.indexOf('/');
    if (slash <= 0) {
        downloadNextReleaseBlob(index, mirrorPath, pending);
        return;
    }
    const QString owner = downloadRepo.left(slash);
    const QString name = downloadRepo.mid(slash + 1);

    // Stream the bytes straight to a temp file in the target CAS shard, hashing as
    // we go, so even a large binary never sits fully in memory. On a verified match
    // we atomically rename it into place; otherwise the partial is discarded.
    const QString blobPath = mirrorReleaseBlobPath(mirrorPath, hash);
    QDir().mkpath(QFileInfo(blobPath).absolutePath());
    auto tmp = std::make_shared<QFile>(blobPath + QStringLiteral(".part"));
    if (!tmp->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        downloadNextReleaseBlob(index, mirrorPath, pending);
        return;
    }
    auto hasher = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);

    QUrl url = catalogApiUrl(); // relay host/scheme; content-addressed route is public
    url.setPath(QStringLiteral("/api/repo/%1/%2/releases/blob/sha256/%3")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(owner)),
                         QString::fromUtf8(QUrl::toPercentEncoding(name)), hash));
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::readyRead, this, [reply, tmp, hasher]() {
        const QByteArray chunk = reply->readAll();
        tmp->write(chunk);
        hasher->addData(chunk);
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, tmp, hasher, index, mirrorPath, hash, blobPath, pending]() {
                const QByteArray rest = reply->readAll();
                tmp->write(rest);
                hasher->addData(rest);
                const bool ok = reply->error() == QNetworkReply::NoError;
                const QString netError = reply->errorString();
                reply->deleteLater();
                tmp->close();
                const QString partPath = tmp->fileName();
                const QString actual =
                    QString::fromLatin1(hasher->result().toHex());
                if (ok && actual == hash) {
                    m_pollBackoff.noteSuccess(QStringLiteral("releaseBlob:") + hash);
                    QFile::remove(blobPath); // replace any stale/empty leftover
                    if (!QFile::rename(partPath, blobPath)) {
                        QFile::remove(partPath);
                    } else if (index >= 0 && index < m_repositories.size() &&
                               m_repositories.at(index).mirrorPath == mirrorPath) {
                        // Artifact availability is advertised separately from git
                        // refs. Publish/re-announce after the CAS changes so other
                        // nodes and the relay learn this mirror can seed releases.
                        m_mirrorAdvertSig.clear();
                        refreshRepositoryList();
                        if (m_repositories.at(index).publishToNetwork)
                            publishRepository(index, false);
                    }
                } else {
                    QFile::remove(partPath);
                    // Exponential per-hash cooldown (5m doubling to a 6h cap):
                    // the usual failure is the one node holding the bytes being
                    // offline, which won't change on the next roster flicker.
                    m_pollBackoff.noteFailure(
                        QStringLiteral("releaseBlob:") + hash,
                        QDateTime::currentMSecsSinceEpoch(),
                        5LL * 60 * 1000, 6LL * 60 * 60 * 1000);
                    if (!ok)
                        logSystem(QStringLiteral(
                                      "Mirror: release artifact %1 download failed "
                                      "(%2); will retry after a backoff.")
                                      .arg(hash.left(12), netError));
                    else
                        logSystem(QStringLiteral(
                                      "Mirror: release artifact %1 failed checksum, "
                                      "discarded.")
                                      .arg(hash.left(12)));
                }
                // Continue with the rest regardless of this one's outcome; a fresh
                // panel load picks up the newly-hosted artifacts' count.
                downloadNextReleaseBlob(index, mirrorPath, pending);
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
    auto *pruneArtifactsCheck = new QCheckBox(
        QStringLiteral("Delete previous release artifacts from this node"));
    pruneArtifactsCheck->setToolTip(QStringLiteral(
        "After the release tag is created, delete every artifact currently stored "
        "under this repo's local forkmesh-releases/sha256 store. New artifacts "
        "from the release workflow will be written after this."));
    auto *pruneTagsCheck = new QCheckBox(
        QStringLiteral("Delete previous release tags"));
    pruneTagsCheck->setToolTip(QStringLiteral(
        "After the new release tag is created, delete every other release tag "
        "in this repository's history, keeping only the one just published. "
        "This cannot be undone, and links to those older releases will stop "
        "working."));
    auto *genNotesButton = new QPushButton("Generate release notes");
    genNotesButton->setObjectName("ghostButton");
    genNotesButton->setCursor(Qt::PointingHandCursor);
    setOcticon(genNotesButton, "list-unordered", 14);
    // GitHub-style auto-generated notes: every non-merge commit since the
    // previous release (or, for the very first release, a capped recent window),
    // as a "What's Changed" list, plus a compare link at the bottom. Replaces
    // whatever is currently typed in Notes so it can be regenerated after
    // switching Target.
    connect(genNotesButton, &QPushButton::clicked, &dialog,
            [dir, prevTag, tagEdit, targetEdit, notesEdit] {
                const QString targetRef = targetEdit->currentText().trimmed();
                if (targetRef.isEmpty())
                    return;
                QStringList args{"log", "--no-merges", "--date-order",
                                 "--format=%s%x1f%h%x1f%an"};
                // No previous tag to diff from (the first release) — cap the
                // window so a large repo's whole history doesn't get dumped in.
                if (prevTag.isEmpty())
                    args << QStringLiteral("--max-count=250") << targetRef;
                else
                    args << QStringLiteral("%1..%2").arg(prevTag, targetRef);
                QByteArray log;
                runGitCapture(dir, args, &log, nullptr);

                QString text = QStringLiteral("## What's Changed\n");
                int shown = 0;
                for (const QByteArray &line : log.split('\n')) {
                    const QString entry = QString::fromUtf8(line).trimmed();
                    if (entry.isEmpty())
                        continue;
                    const QStringList f = entry.split(QLatin1Char('\x1f'));
                    const QString subject = f.value(0).trimmed();
                    const QString sha = f.value(1).trimmed();
                    const QString author = f.value(2).trimmed();
                    if (subject.isEmpty())
                        continue;
                    text += QStringLiteral("* %1 (`%2`)").arg(subject, sha);
                    if (!author.isEmpty())
                        text += QStringLiteral(" by %1").arg(author);
                    text += QLatin1Char('\n');
                    ++shown;
                }
                if (shown == 0)
                    text += prevTag.isEmpty()
                                ? QStringLiteral("* No commits yet.\n")
                                : QStringLiteral("* No changes since %1.\n")
                                      .arg(prevTag);
                if (!prevTag.isEmpty()) {
                    const QString newTag = tagEdit->text().trimmed();
                    text += QStringLiteral("\n**Full Changelog**: %1...%2")
                                .arg(prevTag, newTag.isEmpty() ? targetRef : newTag);
                }
                notesEdit->setPlainText(text);
            });
    // Second generator: hand the same commit range to an AI agent so it writes
    // grouped, human-readable notes instead of a raw commit list. The picker
    // chooses which provider runs it, seeded to the user's default agent.
    auto *agentNotesButton = new QPushButton("Generate release notes with agent");
    agentNotesButton->setObjectName("ghostButton");
    agentNotesButton->setCursor(Qt::PointingHandCursor);
    setOcticon(agentNotesButton, "rocket", 14);
    auto *agentNotesCombo = new QComboBox;
    agentNotesCombo->addItem(QStringLiteral("Codex"), kCodexProvider);
    agentNotesCombo->addItem(QStringLiteral("OpenAI API"),
                             QStringLiteral("openai"));
    agentNotesCombo->addItem(QStringLiteral("Claude API"),
                             QStringLiteral("claude-api"));
    agentNotesCombo->addItem(QStringLiteral("Claude Code"),
                             QStringLiteral("claude-code"));
    selectDefaultAgentProvider(agentNotesCombo);
    agentNotesCombo->setToolTip("Which agent writes the release notes");
    auto *agentNotesRow = new QWidget;
    auto *agentNotesRowLayout = new QHBoxLayout(agentNotesRow);
    agentNotesRowLayout->setContentsMargins(0, 0, 0, 0);
    agentNotesRowLayout->setSpacing(6);
    agentNotesRowLayout->addWidget(agentNotesButton);
    agentNotesRowLayout->addWidget(agentNotesCombo);
    agentNotesRowLayout->addStretch(1);
    connect(agentNotesButton, &QPushButton::clicked, &dialog,
            [this, dir, prevTag, tagEdit, targetEdit, notesEdit, agentNotesButton,
             agentNotesCombo] {
                const QString targetRef = targetEdit->currentText().trimmed();
                if (targetRef.isEmpty())
                    return;
                generateReleaseNotesWithAgent(
                    dir, prevTag, tagEdit->text().trimmed(), targetRef,
                    agentNotesCombo->currentData().toString(), notesEdit,
                    agentNotesButton);
            });
    form->addRow("Tag", tagEdit);
    form->addRow("Target", targetEdit);
    form->addRow("Title", titleEdit);
    form->addRow(QString(), genNotesButton);
    form->addRow(QString(), agentNotesRow);
    form->addRow("Notes", notesEdit);
    form->addRow(QString(), pruneArtifactsCheck);
    form->addRow(QString(), pruneTagsCheck);
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
    // Keep the bare title for the fediverse announcement before the notes are
    // folded into the tag message below.
    const QString releaseTitle = message;
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
    announceReleaseOnFediverse(tag, releaseTitle, notes);
    loadBranchesAndTags();
    if (pruneArtifactsCheck->isChecked())
        pruneReleaseArtifactsForCurrentRepo(tag);
    if (pruneTagsCheck->isChecked())
        pruneReleaseTagsForCurrentRepo(tag);

    // Trigger any `on: release` workflow (e.g. .forkmesh/release.yml, which
    // builds and publishes the desktop binary for this platform). Resolve the
    // tag's target to a concrete commit the runner can check out, and pass the
    // tag ref so the run reports against refs/tags/<tag> and the workflow sees
    // FORKMESH_TAG.
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        if (repo.actionsEnabled) {
            // The tag (and any version-bump commit) was just created in the
            // working copy, but queueWorkflowsForCommit reads the .forkmesh/
            // workflows AND resolves the commit from the served bare mirror — and
            // ActionRunner later checks that commit out of the mirror too. The
            // periodic mirror sync hasn't caught up yet, so without copying the new
            // tag across first the release tag exists but ls-tree finds no workflow
            // at the (mirror-absent) commit ("nothing to run") and the build never
            // runs. Fetch the new heads+tags straight from the working copy into
            // the mirror (the same refspecs syncRepository uses) so the tagged
            // commit and its workflow files are present before we queue the build.
            if (!repo.mirrorPath.trimmed().isEmpty() && dir != repo.mirrorPath &&
                QDir(repo.mirrorPath).exists())
                runGitCapture(repo.mirrorPath,
                              {QStringLiteral("fetch"), dir,
                               QStringLiteral("+refs/heads/*:refs/heads/*"),
                               QStringLiteral("+refs/tags/*:refs/tags/*")},
                              nullptr, nullptr);
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

void MainWindow::generateReleaseNotesWithAgent(
    const QString &dir, const QString &prevTag, const QString &newTag,
    const QString &targetRef, const QString &provider, QPlainTextEdit *notesEdit,
    QPushButton *button)
{
    if (!m_networkAccess || !notesEdit || targetRef.isEmpty())
        return;

    // Same commit window the deterministic generator uses: everything since the
    // previous release, or a capped recent slice for the very first one.
    QStringList args{"log", "--no-merges", "--date-order",
                     "--format=%s%x1f%h%x1f%an"};
    if (prevTag.isEmpty())
        args << QStringLiteral("--max-count=250") << targetRef;
    else
        args << QStringLiteral("%1..%2").arg(prevTag, targetRef);
    QByteArray log;
    runGitCapture(dir, args, &log, nullptr);

    QStringList commits;
    for (const QByteArray &line : log.split('\n')) {
        const QString entry = QString::fromUtf8(line).trimmed();
        if (entry.isEmpty())
            continue;
        const QStringList f = entry.split(QLatin1Char('\x1f'));
        const QString subject = f.value(0).trimmed();
        const QString sha = f.value(1).trimmed();
        const QString author = f.value(2).trimmed();
        if (subject.isEmpty())
            continue;
        QString c = QStringLiteral("- %1 (%2)").arg(subject, sha);
        if (!author.isEmpty())
            c += QStringLiteral(" by %1").arg(author);
        commits << c;
    }
    if (commits.isEmpty()) {
        setRepoDetailNotice(
            prevTag.isEmpty()
                ? QStringLiteral("No commits to write release notes from.")
                : QStringLiteral("No changes since %1 to write notes from.")
                      .arg(prevTag),
            true);
        return;
    }
    // A big history would blow the request budget; cap what the agent sees.
    if (commits.size() > 250)
        commits = commits.mid(0, 250);

    const bool claude = agentIsClaudeProvider(provider);
    const bool claudeCode = provider == QLatin1String("claude-code");
    const QString oauthToken = claudeCode ? claudeCodeOAuthToken() : QString();
    const QString apiKey =
        claudeCode ? QString()
                   : (claude ? QSettings().value(kClaudeApiKeySetting)
                             : QSettings().value(kCodexApiKeySetting))
                         .toString()
                         .trimmed();
    if (apiKey.isEmpty() && oauthToken.isEmpty()) {
        setRepoDetailNotice(
            claudeCode
                ? QStringLiteral(
                      "Sign in to Claude Code first (run `claude` and log in).")
                : claude ? QStringLiteral("Add a Claude API key in Settings first.")
                         : QStringLiteral(
                               "Add an OpenAI API key in Settings first."),
            true);
        return;
    }

    const QString version = newTag.isEmpty() ? targetRef : newTag;
    const QString task =
        QStringLiteral(
            "Write concise, well-organized GitHub-style release notes in Markdown "
            "for version %1. Start with a `## What's Changed` heading, then group "
            "related commits under short bold category headings (Features, Fixes, "
            "etc.) as bullet points, keeping each commit's `(short-sha)` reference. "
            "Summarize clearly and drop noise like version bumps. Output only the "
            "Markdown notes, no preamble.\n\n----- COMMITS -----\n%2")
            .arg(version, commits.join(QLatin1Char('\n')));
    const QString model =
        claude ? QStringLiteral("claude-haiku-4-5") : kIssueAskAiModel;

    QNetworkReply *reply = nullptr;
    if (claude) {
        QJsonObject payload;
        payload.insert("model", model);
        payload.insert("max_tokens", 2000);
        QJsonArray messages;
        QJsonObject um;
        um.insert("role", "user");
        um.insert("content", task);
        messages.append(um);
        payload.insert("messages", messages);
        QNetworkRequest req(
            QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
        if (!oauthToken.isEmpty()) {
            req.setRawHeader("Authorization", "Bearer " + oauthToken.toUtf8());
            req.setRawHeader("anthropic-beta", "oauth-2025-04-20");
            payload.insert("system", kClaudeCodeOAuthSystem);
        } else {
            req.setRawHeader("x-api-key", apiKey.toUtf8());
        }
        req.setRawHeader("anthropic-version", "2023-06-01");
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    } else {
        QJsonObject payload;
        payload.insert("model", model);
        payload.insert("input", task);
        payload.insert("max_output_tokens", 2000);
        QNetworkRequest req = openAiRequest(
            QUrl(QStringLiteral("https://api.openai.com/v1/responses")), apiKey);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }

    // The draft dialog is modal but its event loop keeps spinning, so the reply
    // fires while it's open. Guard the dialog-scoped widgets with QPointer in
    // case the user dismisses the dialog before the response lands.
    QPointer<QPlainTextEdit> notesGuard(notesEdit);
    QPointer<QPushButton> buttonGuard(button);
    if (button) {
        button->setEnabled(false);
        button->setText(QString::fromUtf8("Generating\xE2\x80\xA6"));
    }
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, claude, notesGuard, buttonGuard] {
                const QByteArray body = reply->readAll();
                reply->deleteLater();
                if (buttonGuard) {
                    buttonGuard->setEnabled(true);
                    buttonGuard->setText("Generate release notes with agent");
                }
                if (reply->error() != QNetworkReply::NoError) {
                    setRepoDetailNotice(
                        "Release-notes request failed: " +
                            apiErrorSummary(reply, body),
                        true);
                    return;
                }
                const QJsonObject obj = QJsonDocument::fromJson(body).object();
                QString text;
                if (claude) {
                    for (const QJsonValue &v : obj.value("content").toArray()) {
                        const QJsonObject o = v.toObject();
                        if (o.value("type").toString() == QLatin1String("text"))
                            text += o.value("text").toString();
                    }
                } else {
                    text = openAiResponseText(obj);
                }
                text = text.trimmed();
                if (text.isEmpty()) {
                    setRepoDetailNotice(
                        "The agent returned no release notes.", true);
                    return;
                }
                if (notesGuard)
                    notesGuard->setPlainText(text);
                else
                    setRepoDetailNotice(
                        "Release notes are ready, but the draft dialog was "
                        "closed.",
                        true);
            });
}

// Announce a freshly published release to the repo's fediverse followers.
// Releases never pass through the relay's signed inboxes (they are canonical
// on this node), so the relay can only federate them when the owner node
// pushes the announcement itself: POST /api/repo/<o>/<r>/ap-publish, gated by
// the same forkmesh-issues-pull-v1 signed token as the inbox drains.
// Best-effort fire-and-forget — a relay hiccup must never affect the release.
void MainWindow::announceReleaseOnFediverse(const QString &tag,
                                            const QString &title,
                                            const QString &notes)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (!repo.publishToNetwork)
        return; // unpublished repos have no public fediverse actor
    const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
    if (!hasOwnerSigningCapability(owner))
        return;
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + owner + "/" +
                repoSegment(repo.name, QStringLiteral("repository")) +
                "/ap-publish");
    url.setQuery(signedInboxQuery(owner));
    QJsonObject body;
    body.insert(QStringLiteral("kind"), QStringLiteral("release"));
    body.insert(QStringLiteral("eventType"), QStringLiteral("publish"));
    body.insert(QStringLiteral("tag"), tag);
    body.insert(QStringLiteral("title"), title);
    body.insert(QStringLiteral("body"), notes.left(4000));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, tag] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            logSystem(
                QStringLiteral("Fediverse: announced release %1 to followers.")
                    .arg(tag));
        else
            logSystem(
                QStringLiteral("Fediverse: could not announce release %1 (%2).")
                    .arg(tag, reply->errorString()));
    });
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
    registerDiffView(diff);
    if (diffHtml.trimmed().isEmpty())
        setDiffHtml(diff,
            prevTag.isEmpty()
                ? QStringLiteral(
                      "<p style='color:#8b949e'>This is the earliest release "
                      "\xE2\x80\x94 no previous release to diff against.</p>")
                : QStringLiteral(
                      "<p style='color:#8b949e'>No file changes between %1 and "
                      "%2.</p>")
                      .arg(prevTag.toHtmlEscaped(), tag.toHtmlEscaped()));
    else
        setDiffHtml(diff, diffHtml);
    layout->addWidget(diff, 1);

    auto *buttons = new QDialogButtonBox;
    auto *browseBtn = buttons->addButton(QStringLiteral("Browse repo at this tag"),
                                         QDialogButtonBox::ActionRole);
    // Retro-announce: releases published before the automatic ap-publish hook
    // existed (or while the relay was unreachable) can be pushed to fediverse
    // followers from here at any time.
    auto *fediBtn = buttons->addButton(QStringLiteral("Announce on fediverse"),
                                       QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(browseBtn, &QPushButton::clicked, &dialog, [this, &dialog, tag] {
        dialog.accept();
        setRepoBranch(tag); // browse the repo's files at this tag
    });
    connect(fediBtn, &QPushButton::clicked, &dialog,
            [this, tag, subject, body, fediBtn] {
        fediBtn->setEnabled(false);
        announceReleaseOnFediverse(tag, subject, body);
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

#ifdef FORKMESH_WINDOW_TESTS
QStringList MainWindow::testMirrorNodeRows() const
{
    QStringList rows;
    if (!m_mirrorNodesTable)
        return rows;
    for (int row = 0; row < m_mirrorNodesTable->rowCount(); ++row) {
        const QTableWidgetItem *item =
            m_mirrorNodesTable->item(row, MirrorNodeColNode);
        if (!item)
            continue;
        rows.append(item->text() + QLatin1Char('|') +
                    item->data(Qt::UserRole).toString());
    }
    return rows;
}

bool MainWindow::testMirrorNodesOnlineOnlyChecked() const
{
    return m_mirrorNodesOnlineOnlyCheck && m_mirrorNodesOnlineOnlyCheck->isChecked();
}

void MainWindow::testSetMirrorNodesOnlineOnly(bool checked)
{
    if (m_mirrorNodesOnlineOnlyCheck)
        m_mirrorNodesOnlineOnlyCheck->setChecked(checked);
}

QString MainWindow::testMirrorNodeCellText(const QString &nodeName, int column) const
{
    if (!m_mirrorNodesTable)
        return QString();
    for (int row = 0; row < m_mirrorNodesTable->rowCount(); ++row) {
        const QTableWidgetItem *name =
            m_mirrorNodesTable->item(row, MirrorNodeColNode);
        if (!name || !name->text().startsWith(nodeName))
            continue;
        const QTableWidgetItem *item = m_mirrorNodesTable->item(row, column);
        return item ? item->text() : QString();
    }
    return QString();
}

QString MainWindow::testMirrorNodeCellToolTip(const QString &nodeName, int column) const
{
    if (!m_mirrorNodesTable)
        return QString();
    for (int row = 0; row < m_mirrorNodesTable->rowCount(); ++row) {
        const QTableWidgetItem *name =
            m_mirrorNodesTable->item(row, MirrorNodeColNode);
        if (!name || !name->text().startsWith(nodeName))
            continue;
        const QTableWidgetItem *item = m_mirrorNodesTable->item(row, column);
        return item ? item->toolTip() : QString();
    }
    return QString();
}
#endif
