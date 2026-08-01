






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
    MirrorNodeColMessage,
    MirrorNodeColAuthor,
    MirrorNodeColSynced,
    MirrorNodeColSyncDelay,
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
    MirrorNodeColTunnel,
    MirrorNodeColClones,
    MirrorNodeColWebsite,
    MirrorNodeColArtifacts,
    MirrorNodeColumnCount,
};


















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


    static const QRegularExpression versionLine(
        QStringLiteral("^(project\\(ForkMesh VERSION )([0-9]+\\.[0-9]+\\.[0-9]+)"),
        QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = versionLine.match(text);
    if (!m.hasMatch() || m.captured(2) == version)
        return false;

    text.replace(m.capturedStart(2), m.capturedLength(2), version);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    const bool wrote = file.write(text.toUtf8()) >= 0;
    file.close();
    if (!wrote)
        return false;



    return runGitCapture(
        workTree,
        {"-c", QStringLiteral("user.email=actions@forkmesh.local"), "-c",
         QStringLiteral("user.name=ForkMesh Actions"), "commit", "-m",
         QStringLiteral("release: bump version header to %1").arg(version), "--",
         rel},
        nullptr, nullptr);
}

}



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

    m_releasesTable = new QTableWidget(0, 11);
    installColumnHeaderMenu(m_releasesTable);
    m_releasesTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_releasesTable);
    m_releasesTable->setHorizontalHeaderLabels(
        {"Tag", "Commit", "Released", "Release notes", "Compare", "Artifacts",
         "Size", "SHA-256", "Downloads", "Mirrors", ""});
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
    rh->setSectionResizeMode(10, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_releasesTable);





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
    installColumnHeaderMenu(m_artifactsTable);
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



    const bool writable = !m_repositories.at(m_repoDetailIndex).previewOnly &&
                          !mirrorPath.isEmpty() && QDir(mirrorPath).exists();

    const QList<MirrorReleaseBlob> blobs = mirrorReleaseBlobs(mirrorPath);





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
    bool canPushToMirrors = false;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        canPushToMirrors = writable && !repo.previewOnly && !repo.isPrivate &&
                           !repo.localPath.trimmed().isEmpty();
    }












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





    QHash<QString, QString> artifactsByTag;
    QHash<QString, QString> sizeByTag;
    QHash<QString, QString> shaByTag;
    QHash<QString, QString> shaTooltipByTag;
    QHash<QString, QString> downloadsByTag;




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



                const qint64 size =
                    static_cast<qint64>(a.value(QStringLiteral("size")).toDouble());
                sizeParts.append(size > 0 ? QLocale().formattedDataSize(size)
                                          : QStringLiteral("—"));



                shaParts.append(hashValid ? hash.left(12) : QStringLiteral("—"));
                shaTooltipParts.append(hashValid ? QStringLiteral("sha256:%1").arg(hash)
                                                 : QString());


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


            if (currentTag.isEmpty())
                currentTag = tag;





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




            QString commitSha = f.value(3).trimmed();
            if (commitSha.isEmpty())
                commitSha = f.value(4).trimmed();
            auto *commitItem = new QTableWidgetItem(commitSha);
            commitItem->setFont(QFont(QStringLiteral("monospace")));
            commitItem->setForeground(QColor("#8b949e"));
            m_releasesTable->setItem(row, 1, commitItem);



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





            QString artifactsHtml = artifactsByTag.value(tag);
            QString sizeText = sizeByTag.value(tag);
            QString shaText = shaByTag.value(tag);
            QString shaTooltip = shaTooltipByTag.value(tag);
            QString downloadsText = downloadsByTag.value(tag);





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




            auto *downloadsItem = new QTableWidgetItem(
                downloadsText.isEmpty() ? QStringLiteral("—") : downloadsText);
            downloadsItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            downloadsItem->setForeground(QColor("#8b949e"));
            m_releasesTable->setItem(row, 8, downloadsItem);

            auto *pushMirrors = new QPushButton("Push to mirrors");
            pushMirrors->setObjectName("ghostButton");
            pushMirrors->setCursor(Qt::PointingHandCursor);
            setOcticon(pushMirrors, "broadcast", 14);
            pushMirrors->setToolTip(
                QStringLiteral(
                    "Push %1 to configured SSH mirror gateways and notify "
                    "online peer mirrors. Release artifacts are transferred "
                    "through the existing SHA-256-verified mirror path.")
                    .arg(tag));
            pushMirrors->setEnabled(canPushToMirrors);
            connect(pushMirrors, &QPushButton::clicked, this,
                    [this, tag] { pushReleaseToMirrors(tag); });
            m_releasesTable->setCellWidget(row, 9, pushMirrors);

            auto *del = new QPushButton;
            del->setObjectName("issueIconButton");
            del->setFlat(true);
            del->setCursor(Qt::PointingHandCursor);
            del->setIcon(themedOcticon("trash", QColor("#f85149"), 15));
            del->setIconSize(QSize(15, 15));
            del->setToolTip(QStringLiteral("Delete tag %1").arg(tag));
            del->setEnabled(writable);
            connect(del, &QPushButton::clicked, this, [this, tag] { deleteTag(tag); });
            m_releasesTable->setCellWidget(row, 10, del);
            ++count;
        }
    }


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
    connect(refreshButton, &QPushButton::clicked, this, [this] {


        m_catalogMirrorsFetchedMs = 0;
        loadMirrorNodesPanel();
    });
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






    m_mirrorNodesTable = new QTableWidget(0, MirrorNodeColumnCount);
    installColumnHeaderMenu(m_mirrorNodesTable);
    m_mirrorNodesTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_mirrorNodesTable);
    m_mirrorNodesTable->setHorizontalHeaderLabels(
        {"Node", "Owner", "Latest commit", "Message", "Author", "Synced", "Sync delay", "Size",
         "Issues", "Commits", "Branches", "Pulls", "Discussions", "CPU", "RAM",
         "Disk", "Platform", "Version", "Node id", "Tunnel", "Clones", "Website",
         "Artifacts"});
    m_mirrorNodesTable->verticalHeader()->setVisible(false);
    m_mirrorNodesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mirrorNodesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mirrorNodesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mirrorNodesTable->setShowGrid(false);
    m_mirrorNodesTable->setWordWrap(false);
    m_mirrorNodesTable->setSortingEnabled(true);
    m_mirrorNodesTable->sortByColumn(MirrorNodeColNode,
                                     Qt::AscendingOrder);
    QHeaderView *mh = m_mirrorNodesTable->horizontalHeader();
    mh->setHighlightSections(false);
    mh->setSectionResizeMode(MirrorNodeColNode, QHeaderView::Stretch);
    mh->setSectionResizeMode(MirrorNodeColOwner, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColCommit, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColMessage, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColAuthor, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColSynced, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColSyncDelay, QHeaderView::ResizeToContents);
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
    mh->setSectionResizeMode(MirrorNodeColTunnel, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColClones, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColWebsite, QHeaderView::ResizeToContents);
    mh->setSectionResizeMode(MirrorNodeColArtifacts, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_mirrorNodesTable);


    m_mirrorNodesTable->setItemDelegateForColumn(
        MirrorNodeColSynced, new MirrorSyncDelegate(m_mirrorNodesTable));

    auto *resourceBars = new ResourceBarDelegate(m_mirrorNodesTable);
    for (int col : {MirrorNodeColCpu, MirrorNodeColRam, MirrorNodeColDisk})
        m_mirrorNodesTable->setItemDelegateForColumn(col, resourceBars);
    auto *pacmanTick = new QTimer(m_mirrorNodesTable);
    pacmanTick->setInterval(1000);
    connect(pacmanTick, &QTimer::timeout, m_mirrorNodesTable, [this] {
        if (!m_mirrorNodesTable->isVisible())
            return;





        for (int r = 0; r < m_mirrorNodesTable->rowCount(); ++r) {
            if (m_mirrorNodesTable->item(r, MirrorNodeColSynced))
                m_mirrorNodesTable->update(
                    m_mirrorNodesTable->model()->index(r, MirrorNodeColSynced));
        }
    });
    pacmanTick->start();



    auto *panelRefresh = new QTimer(m_mirrorNodesTable);
    panelRefresh->setInterval(60 * 1000);
    connect(panelRefresh, &QTimer::timeout, m_mirrorNodesTable, [this] {
        if (!m_mirrorNodesTable->isVisible())
            return;


        m_catalogMirrorsFetchedMs = 0;
        loadMirrorNodesPanel();
    });
    panelRefresh->start();



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




    m_mirrorAdvertSig.clear();
    refreshRepositoryList();
    m_backend->advertiseMirrorsNow();
    logSystem(QStringLiteral("Mirror nodes: sent fresh mirror metadata%1.")
                  .arg(requesterName.trimmed().isEmpty()
                           ? QString()
                           : QStringLiteral(" to %1").arg(requesterName.trimmed())));
}




static MirrorSelfSnapshot gatherMirrorSelfSnapshot(const RepositoryRecord &repo,
                                                   const QString &key,
                                                   bool hasWorkingTree)
{
    MirrorSelfSnapshot snapshot;
    snapshot.key = key;
    snapshot.gatheredMs = QDateTime::currentMSecsSinceEpoch();
    const QString mirror = repo.mirrorPath;
    MirrorAdvert &advert = snapshot.advert;
    const MirrorBranchTip tip = mirrorPrimaryBranchTip(mirror, repo.localPath);
    advert.branch = tip.branch;
    advert.commit = tip.commit;
    advert.commitIdentity = mirrorCommitIdentity(mirror, repo.localPath, tip.commit);
    advert.updatedMs = repo.lastSyncMs;






    const QString countsDir =
        (!mirror.trimmed().isEmpty() && QDir(mirror).exists())
            ? mirror
            : repo.localPath.trimmed();
    advert.sizeBytes = mirrorRepoSizeBytes(countsDir);
    advert.issueCount = mirrorIssueCount(countsDir, advert.branch);
    advert.commitCount = mirrorCommitCount(countsDir, advert.branch);
    advert.branchCount = mirrorBranchCount(countsDir);
    advert.pullCount = mirrorPullCount(countsDir, advert.branch);
    advert.discussionCount = mirrorDiscussionCount(countsDir, advert.branch);
    advert.worktreeCount = mirrorWorktreeCount(repo.localPath);


    advert.artifactCount = mirrorArtifactCount(mirror);
    if (advert.artifactCount < 0)
        advert.artifactCount = checkoutArtifactCount(repo.localPath);
    snapshot.servedCommit = mirrorBranchCommit(countsDir, advert.branch);
    if (hasWorkingTree && !repo.localPath.trimmed().isEmpty() &&
        !snapshot.servedCommit.isEmpty()) {
        QByteArray out;
        const QString pushTarget =
            advert.branch.isEmpty() ? QStringLiteral("HEAD") : advert.branch;
        if (runGitCapture(repo.localPath,
                          {QStringLiteral("rev-list"), QStringLiteral("--count"),
                           snapshot.servedCommit + QStringLiteral("..") + pushTarget},
                          &out, nullptr))
            snapshot.pendingPush = QString::fromUtf8(out).trimmed().toInt();
    }
    return snapshot;
}

QString MainWindow::mirrorSelfSnapshotKey(const RepositoryRecord &repo) const
{


    auto stamp = [](const QString &base, const QString &leaf) {
        return QString::number(QFileInfo(QDir(base).filePath(leaf))
                                   .lastModified()
                                   .toMSecsSinceEpoch());
    };
    return repo.mirrorPath + QLatin1Char('|') + repo.localPath + QLatin1Char('|') +
           QString::number(repo.lastSyncMs) + QLatin1Char('|') +
           stamp(repo.mirrorPath, QStringLiteral("HEAD")) + QLatin1Char('|') +
           stamp(repo.mirrorPath, QStringLiteral("refs")) + QLatin1Char('|') +
           stamp(repo.mirrorPath, QStringLiteral("packed-refs")) + QLatin1Char('|') +
           stamp(repo.localPath, QStringLiteral(".git/HEAD")) + QLatin1Char('|') +
           stamp(repo.localPath, QStringLiteral(".git/refs")) + QLatin1Char('|') +
           stamp(repo.localPath, QStringLiteral(".git/packed-refs")) +
           QLatin1Char('|') +
           stamp(repo.localPath, QStringLiteral(".git/worktrees"));
}

void MainWindow::refreshMirrorSelfSnapshot(const RepositoryRecord &repo,
                                           const QString &key)
{
    if (m_mirrorSelfSnapshotsInFlight.contains(repo.mirrorPath))
        return;
    m_mirrorSelfSnapshotsInFlight.insert(repo.mirrorPath);
    const RepositoryRecord snapshotRepo = repo;
    const bool hasWorkingTree = repoHasWorkingTree();
    auto result = std::make_shared<MirrorSelfSnapshot>();
    QThread *worker = QThread::create([snapshotRepo, key, hasWorkingTree, result] {
        const forkmesh::BackgroundScope activity(
            QStringLiteral("mirrors"),
            QStringLiteral("read mirror row for %1").arg(snapshotRepo.name),
            forkmesh::ActionTelemetry::Execution::Worker);
        *result = gatherMirrorSelfSnapshot(snapshotRepo, key, hasWorkingTree);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this,
            [this, mirror = repo.mirrorPath, result] {
                m_mirrorSelfSnapshotsInFlight.remove(mirror);
                m_mirrorSelfSnapshots.insert(mirror, *result);
                loadMirrorNodesPanel();
            });
    worker->start();
}

void MainWindow::loadMirrorNodesPanel()
{
    if (!m_mirrorNodesTable)
        return;










    if (m_mirrorNodesPanelLoading)
        return;
    QScopedValueRollback<bool> loadingGuard(m_mirrorNodesPanelLoading, true);
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


        setNodeDotRepoStates({});
        m_mirrorNodesTable->setSortingEnabled(true);
        updateMirrorNodeLightTimer();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);




    GitKeepAlive keepAlive;

    const QString canonical =
        catalogOwner(repo) + "/" +
        repoSegment(repo.name, QStringLiteral("repository"));



    const QString source = repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
                           repoSegment(repo.name, QStringLiteral("repository"));
    const QString sourceOwner = source.section('/', 0, 0);
    const QString legacy = repo.owner + "/" + repo.name;

    const QString localMirror = repo.mirrorPath;











    const QString selfKey = mirrorSelfSnapshotKey(repo);
    const auto cached = m_mirrorSelfSnapshots.constFind(repo.mirrorPath);
    MirrorSelfSnapshot snapshot;
    if (cached == m_mirrorSelfSnapshots.constEnd()) {
        snapshot = gatherMirrorSelfSnapshot(repo, selfKey, repoHasWorkingTree());
        m_mirrorSelfSnapshots.insert(repo.mirrorPath, snapshot);
    } else {
        snapshot = *cached;



        constexpr qint64 kSelfSnapshotFloorMs = 2000;
        if (snapshot.key != selfKey &&
            QDateTime::currentMSecsSinceEpoch() - snapshot.gatheredMs >
                kSelfSnapshotFloorMs)
            refreshMirrorSelfSnapshot(repo, selfKey);
    }
    MirrorAdvert selfAdvert = snapshot.advert;
    selfAdvert.ownerName = canonical;
    selfAdvert.source = source;
    selfAdvert.updatedMs = repo.lastSyncMs;
    const QString servedBranch = selfAdvert.branch;
    const QString servedCommit = snapshot.servedCommit;




    const int pendingPush = snapshot.pendingPush;




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
            advert = &selfAdvert;
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






    auto displayNodeLabel = [&](const MemberInfo &node,
                                const MirrorAdvert *advert) {
        QString label = node.nodeName.trimmed();
        if (label.isEmpty() && node.self)
            label = machineNodeName().trimmed();
        if (label.isEmpty())
            label = displayNodeName(node, advert);
        return label;
    };



    QHash<QString, QString> catalogOwnerUserByNode;
    if (m_catalogMirrorsSource == source) {
        for (const QJsonValue &value : std::as_const(m_catalogMirrorsCache)) {
            const QJsonObject m = value.toObject();
            const QString n = m.value("node").toString().trimmed().toLower();
            const QString ownerUser =
                m.value(QStringLiteral("ownerUser")).toString().trimmed();
            if (!n.isEmpty() && !ownerUser.isEmpty())
                catalogOwnerUserByNode.insert(n, ownerUser);
        }
    }
    auto displayOwnerName = [&](const MemberInfo &node,
                                const MirrorAdvert *advert) {
        QString owner = node.ownerUser.trimmed();
        if (owner.isEmpty())
            owner = catalogOwnerUserByNode.value(
                displayNodeName(node, advert).trimmed().toLower());
        if (owner.isEmpty())
            owner = catalogOwnerUserByNode.value(node.nodeName.trimmed().toLower());




        if (owner.isEmpty() &&
            (node.self ||
             (!node.nodeName.trimmed().isEmpty() &&
              m_profileLinkedNodes.contains(node.nodeName.trimmed(),
                                            Qt::CaseInsensitive))))
            owner = topBarUserName().trimmed();
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







    QList<MemberInfo> rosterNodes;
    QHash<QString, int> rosterIndexByName;
    for (const MemberInfo &node : std::as_const(m_homeRoster)) {
        bool namedOnly = false;
        const MirrorAdvert *advert = matchAdvert(node, namedOnly);
        if (!advert && !namedOnly)
            continue;
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




    QString sourceCommit;
    QString newestCommit;
    qint64 newestMs = -1;




    const MirrorAdvert *sourceAdvert = nullptr;
    const MirrorAdvert *newestAdvert = nullptr;
    for (const MemberInfo &node : std::as_const(rosterNodes)) {
        bool namedOnly = false;
        const MirrorAdvert *advert = matchAdvert(node, namedOnly);
        if (!advert || advert->commit.isEmpty())
            continue;
        if (advert->ownerName == source ||
            displayNodeName(node, advert).compare(sourceOwner, Qt::CaseInsensitive) == 0 ||
            (node.self && repoHasWorkingTree())) {
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




    const MirrorAdvert *referenceAdvert =
        sourceAdvert ? sourceAdvert : newestAdvert;
    const int refIssues = referenceAdvert ? referenceAdvert->issueCount : -1;
    const int refCommits = referenceAdvert ? referenceAdvert->commitCount : -1;
    const int refBranches = referenceAdvert ? referenceAdvert->branchCount : -1;
    const int refPulls = referenceAdvert ? referenceAdvert->pullCount : -1;
    const int refDiscussions =
        referenceAdvert ? referenceAdvert->discussionCount : -1;
    const int refArtifacts = referenceAdvert ? referenceAdvert->artifactCount : -1;





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




    auto makeSyncDelayCell = [&referenceCommit](qint64 syncedMs,
                                                 qint64 committedAtMs,
                                                 const QString &commit,
                                                 bool isSource) {
        QString text = QString::fromUtf8("\xE2\x80\x94");
        QString tip;
        qint64 sortValue = -1;
        if (isSource) {
            text = QStringLiteral("Source");
            tip = QStringLiteral("This node is the source of truth");
        } else if (!referenceCommit.isEmpty() && !commit.isEmpty() &&
                   commit != referenceCommit) {
            text = QStringLiteral("Pending");
            tip = QStringLiteral("Waiting to sync the latest source commit");
        } else if (syncedMs > 0 && committedAtMs > 0 && syncedMs >= committedAtMs) {
            const qint64 delayMs = syncedMs - committedAtMs;
            text = formatDuration(delayMs);
            sortValue = delayMs;
            tip = QStringLiteral("%1 from commit to this node's sync")
                      .arg(formatDuration(delayMs));
        }
        auto *item = new SortTableWidgetItem(text);
        item->setData(kTableSortRole, double(sortValue));
        item->setToolTip(tip);
        return item;
    };






    QHash<QString, QPair<int, int>> serveCounts;




    QHash<QString, QString> integrityByNode;





    struct TunnelInfo {
        bool registered = false;
        bool healthy = false;
        bool fresh = false;
        bool abuseBlocked = false;
        int latencyMs = 0;
        QString endpoint;
        QString integrity;
    };
    QHash<QString, TunnelInfo> tunnelByNode;
    if (m_catalogMirrorsSource == source) {
        for (const QJsonValue &value : std::as_const(m_catalogMirrorsCache)) {
            const QJsonObject m = value.toObject();
            const QString n = m.value("node").toString().trimmed().toLower();
            if (!n.isEmpty()) {
                serveCounts.insert(n, {m.value("clonesServed").toInt(-1),
                                       m.value("websiteServed").toInt(-1)});
                integrityByNode.insert(n, m.value("integrity").toString());
                TunnelInfo tunnel;
                tunnel.endpoint = m.value("endpoint").toString().trimmed();
                tunnel.registered = !tunnel.endpoint.isEmpty();
                tunnel.healthy = m.value("endpointHealthy").toBool();
                tunnel.fresh = m.value("endpointFresh").toBool();
                tunnel.abuseBlocked = m.value("abuseBlocked").toBool();
                tunnel.latencyMs = m.value("latencyMs").toInt(0);
                tunnel.integrity = m.value("endpointIntegrity").toString();
                tunnelByNode.insert(n, tunnel);
            }
        }
    }



    auto makeTunnelCell = [](const TunnelInfo &tunnel) -> SortTableWidgetItem * {
        QString text = QString::fromUtf8("\xE2\x80\x94");
        double sortValue = 0;
        QString tip = QStringLiteral("No direct-HTTPS tunnel endpoint registered; "
                                     "this node serves through relay sync only.");
        if (tunnel.abuseBlocked) {
            text = QStringLiteral("blocked");
            sortValue = 1;
            tip = QStringLiteral("Endpoint blocked for abuse.");
        } else if (tunnel.registered) {
            if (tunnel.healthy && tunnel.fresh) {
                text = tunnel.latencyMs > 0
                           ? QStringLiteral("up · %1 ms").arg(tunnel.latencyMs)
                           : QStringLiteral("up");
                sortValue = 4;
                tip = QStringLiteral("Tunnel healthy (%1)").arg(tunnel.endpoint);
            } else if (tunnel.healthy) {
                text = QStringLiteral("up · stale");
                sortValue = 3;
                tip = QStringLiteral(
                          "Tunnel answered its last signed health probe, but the "
                          "node has not been seen recently (%1)")
                          .arg(tunnel.endpoint);
            } else {
                text = QStringLiteral("down");
                sortValue = 2;
                tip = QStringLiteral("Tunnel registered but failing its signed "
                                     "health probe (%1)")
                          .arg(tunnel.endpoint);
            }
            if (!tunnel.integrity.isEmpty() &&
                tunnel.integrity != QLatin1String("ok"))
                tip += QStringLiteral("\nIntegrity: %1").arg(tunnel.integrity);
        }
        auto *item = new SortTableWidgetItem(text);
        item->setData(kTableSortRole, sortValue);
        item->setToolTip(tip);
        return item;
    };


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
    qint64 totalBytes = 0;
    qint64 maxRepoBytes = 0;
    bool weAreSource = false;
    int outOfSyncPeers = 0;


    QSet<QString> shownNames;


    QSet<QString> shownIds;

    QVector<MirrorNodeDot> activityDots;




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






    auto resolveCommitIdentity = [this, &localMirror](
                                     const QString &commit,
                                     const CommitIdentity &advertised) {
        if (!advertised.subject.isEmpty() || !advertised.author.isEmpty())
            return advertised;
        if (commit.isEmpty())
            return CommitIdentity{};
        if (m_commitIdentityCache.size() > 5000)
            m_commitIdentityCache.clear();
        auto cached = m_commitIdentityCache.constFind(commit);
        if (cached == m_commitIdentityCache.constEnd())
            cached = m_commitIdentityCache.insert(
                commit, gitCommitIdentity(localMirror, commit));
        return cached.value();
    };



    auto setCommitIdentityCells = [this](int row,
                                         const CommitIdentity &identity) {
        const QString dash = QString::fromUtf8("\xE2\x80\x94");
        const QString subject = identity.subject.trimmed();
        auto *messageItem = new QTableWidgetItem(
            subject.isEmpty()
                ? dash
                : (subject.size() > 72
                       ? subject.left(71) + QString::fromUtf8("\xE2\x80\xA6")
                       : subject));
        if (!subject.isEmpty()) {
            messageItem->setToolTip(
                identity.committedAtMs > 0
                    ? subject + "\n" +
                          QDateTime::fromMSecsSinceEpoch(identity.committedAtMs)
                              .toString(Qt::ISODate)
                    : subject);
        }
        m_mirrorNodesTable->setItem(row, MirrorNodeColMessage, messageItem);
        const QString author = identity.author.trimmed();
        auto *authorItem = new QTableWidgetItem(author.isEmpty() ? dash : author);
        if (!author.isEmpty())
            authorItem->setToolTip(
                QStringLiteral("Authored the node's latest commit"));
        m_mirrorNodesTable->setItem(row, MirrorNodeColAuthor, authorItem);
    };




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
                      "hash no longer matches the refs this node serves. As the "
                      "source of truth this node re-signs its current refs "
                      "automatically within seconds; \xE2\x80\x9CReset integrity "
                      "pin\xE2\x80\x9D above forces it now.")
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






        const bool online = node.self ? (m_backend != nullptr) : node.online;
        const bool integrityFailing =
            integrityByNode.value(displayNodeName(node, advert).trimmed().toLower()) ==
                QLatin1String("rejected") ||
            (node.self && m_repoPinMismatch);
        if (onlineOnly && !online)
            continue;




        const bool behind = online && advert && !advert->commit.isEmpty() &&
                            !referenceCommit.isEmpty() &&
                            advert->commit != referenceCommit;

        const QString nodeDisplay = displayNodeName(node, advert);
        const QString nodeLabel = displayNodeLabel(node, advert);
        const QString ownerDisplay = displayOwnerName(node, advert);
        shownNames.insert(nodeDisplay.trimmed().toLower());
        shownNames.insert(nodeLabel.trimmed().toLower());
        if (!node.id.isEmpty())
            shownIds.insert(node.id);







        const bool isSource =
            (advert && advert->ownerName == source) ||
            nodeDisplay.compare(sourceOwner, Qt::CaseInsensitive) == 0 ||
            (node.self && repoHasWorkingTree());

        const int row = m_mirrorNodesTable->rowCount();
        m_mirrorNodesTable->insertRow(row);




        if (online || integrityFailing)
            activityDots.append(
                {node.id, nodeLabel, online, node.self, behind, integrityFailing});
        auto *nameItem = new SortTableWidgetItem(
            nodeLabel + (node.self ? QStringLiteral("  (you)") : QString()) +
            (isSource ? QString::fromUtf8("  \xE2\x98\x85 source of truth")
                      : QString()));





        const int light = integrityFailing ? 2 : behind ? 1 : 0;
        const QColor lightColor = integrityFailing ? QColor("#f85149")
                                  : !online          ? QColor("#8b949e")
                                  : behind           ? QColor("#d29922")
                                                     : QColor("#3fb950");
        nameItem->setIcon(
            QIcon(nodeStatusLightPixmap(lightColor, 14, 0.0, light != 0)));
        nameItem->setData(kNodeLightRole, light);
        nameItem->setData(Qt::UserRole, node.id);

        nameItem->setData(kTableSortRole,
                          (isSource ? QStringLiteral("0") : QStringLiteral("1")) +
                              nodeLabel.toLower());
        nameItem->setToolTip(
            isSource ? QString::fromUtf8("Source of truth \xC2\xB7 %1")
                           .arg(online ? "online" : "offline")
                     : (online ? (behind ? QString::fromUtf8(
                                               "Online \xC2\xB7 out of sync")
                                         : QStringLiteral("Online now"))
                               : QStringLiteral("Offline")));
        if (!node.name.trimmed().isEmpty() &&
            node.name.compare(nodeLabel, Qt::CaseInsensitive) != 0)
            nameItem->setToolTip(nameItem->toolTip() + QStringLiteral("\nChat: ") +
                                 node.name.trimmed());
        if (integrityFailing)
            markPinRejected(nameItem, node.self);
        m_mirrorNodesTable->setItem(row, MirrorNodeColNode, nameItem);
        m_mirrorNodesTable->setItem(row, MirrorNodeColOwner,
                                    makeOwnerCell(ownerDisplay));



        QString commitText = QString::fromUtf8("\xE2\x80\x94");
        QString commitTip;
        CommitIdentity commitIdentity;
        if (advert && !advert->commit.isEmpty()) {
            commitText = advert->commit.left(10);
            if (!advert->branch.isEmpty())
                commitText += "  (" + advert->branch + ")";
            commitTip = advert->commit;
            commitIdentity =
                resolveCommitIdentity(advert->commit, advert->commitIdentity);
            if (commitIdentity.committedAtMs > 0)
                commitText += QString::fromUtf8(" \xC2\xB7 ") +
                              formatShortRelativeTime(
                                  commitIdentity.committedAtMs / 1000) +
                              QStringLiteral(" ago");
            if (!commitIdentity.subject.isEmpty())
                commitTip = commitIdentity.subject + "\n" + advert->commit;
        }
        setCommitIdentityCells(row, commitIdentity);
        auto *commitItem = new QTableWidgetItem(commitText);
        commitItem->setToolTip(commitTip);
        markMismatch(commitItem,
                     advert && !advert->commit.isEmpty() &&
                         !referenceCommit.isEmpty() &&
                         advert->commit != referenceCommit,
                     referenceCommit.left(10));
        m_mirrorNodesTable->setItem(row, MirrorNodeColCommit, commitItem);


        const qint64 syncedSecs = advert ? advert->updatedMs / 1000 : 0;
        auto *syncedItem = new SortTableWidgetItem(
            syncedSecs > 0 ? formatShortRelativeTime(syncedSecs) + " ago"
                           : QString::fromUtf8("\xE2\x80\x94"));
        syncedItem->setData(kTableSortRole, double(syncedSecs));
        if (syncedSecs > 0)
            syncedItem->setToolTip(
                QDateTime::fromSecsSinceEpoch(syncedSecs).toString(Qt::ISODate));








        if (behind) {
            syncedItem->setData(kPacmanAnchorRole,
                                static_cast<qlonglong>(advert->updatedMs));
            syncedItem->setToolTip(QString::fromUtf8(
                "Behind the source \xC2\xB7 signalled to sync now; its next "
                "heartbeat is the fallback"));
        }


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
        m_mirrorNodesTable->setItem(
            row, MirrorNodeColSyncDelay,
            makeSyncDelayCell(advert ? advert->updatedMs : 0,
                              commitIdentity.committedAtMs,
                              advert ? advert->commit : QString(), isSource));



        if (node.self && isSource)
            weAreSource = true;
        if (!node.self && behind)
            ++outOfSyncPeers;




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

        m_mirrorNodesTable->setItem(
            row, MirrorNodeColTunnel,
            makeTunnelCell(tunnelByNode.value(nodeDisplay.trimmed().toLower())));







        int nodeClones = -1, nodeWebsite = -1;
        {
            const QPair<int, int> s =
                serveCounts.value(nodeDisplay.trimmed().toLower(), {-1, -1});
            nodeClones = s.first;
            nodeWebsite = s.second;
        }
        if (node.self) {
            const QPair<int, int> s =
                m_repoStats.value(catalogOwner(repo) + "/" + repo.name);
            nodeClones = qMax(nodeClones, s.second);
            nodeWebsite = qMax(nodeWebsite, qMax(0, s.first - s.second));
        }
        m_mirrorNodesTable->setItem(row, MirrorNodeColClones,
                                    makeServeCountCell(nodeClones, clonesTip(nodeClones)));
        m_mirrorNodesTable->setItem(
            row, MirrorNodeColWebsite,
            makeServeCountCell(nodeWebsite, websiteTip(nodeWebsite)));



        const int nodeArtifacts = advert ? advert->artifactCount : -1;
        auto *artifactsItem =
            makeCountCell(nodeArtifacts, "artifact", "artifacts");
        markMismatch(artifactsItem, countMismatch(nodeArtifacts, refArtifacts),
                     QString::number(refArtifacts));
        m_mirrorNodesTable->setItem(row, MirrorNodeColArtifacts, artifactsItem);
        ++count;
    }









    QVector<MirrorNodeDot> catalogOnlyDots;
    if (m_catalogMirrorsSource == source) {
        for (const QJsonValue &value : std::as_const(m_catalogMirrorsCache)) {
            const QJsonObject m = value.toObject();
            const QString nodeName = m.value("node").toString().trimmed();
            if (nodeName.isEmpty() || shownNames.contains(nodeName.toLower()))
                continue;



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


            const QString catCommit = m.value("commit").toString();
            const bool behind = online && !catCommit.isEmpty() &&
                                !referenceCommit.isEmpty() &&
                                catCommit != referenceCommit;


            if (online || integrityFailing)
                catalogOnlyDots.append({m.value("id").toString(), nodeName,
                                        online, false, behind, integrityFailing});
            const int row = m_mirrorNodesTable->rowCount();
            m_mirrorNodesTable->insertRow(row);
            QString ownerUser = m.value(QStringLiteral("ownerUser"))
                                    .toString()
                                    .trimmed();



            if (ownerUser.isEmpty() &&
                m_profileLinkedNodes.contains(nodeName, Qt::CaseInsensitive))
                ownerUser = topBarUserName().trimmed();
            auto *nameItem = new SortTableWidgetItem(
                nodeName + (isSource
                                ? QString::fromUtf8("  \xE2\x98\x85 source of truth")
                                : QString()));


            const int light = integrityFailing ? 2 : behind ? 1 : 0;
            const QColor lightColor = integrityFailing ? QColor("#f85149")
                                      : !online          ? QColor("#8b949e")
                                      : behind           ? QColor("#d29922")
                                                         : QColor("#3fb950");
            nameItem->setIcon(
                QIcon(nodeStatusLightPixmap(lightColor, 14, 0.0, light != 0)));
            nameItem->setData(kNodeLightRole, light);
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



            QString catCommitText = QString::fromUtf8("\xE2\x80\x94");
            if (!catCommit.isEmpty()) {
                catCommitText = catCommit.left(10);
                const QString catBranch = m.value("branch").toString();
                if (!catBranch.isEmpty())
                    catCommitText += "  (" + catBranch + ")";
            }


            CommitIdentity catIdentity;
            catIdentity.subject =
                m.value(QStringLiteral("lastCommitMessage")).toString().trimmed();
            catIdentity.author = m.value(QStringLiteral("lastCommitAuthorName"))
                                     .toString()
                                     .trimmed();
            catIdentity.committedAtMs =
                qMax(qint64(0), qint64(m.value(QStringLiteral("lastCommitAt"))
                                           .toDouble()));
            catIdentity = resolveCommitIdentity(catCommit, catIdentity);
            if (!catCommit.isEmpty() && catIdentity.committedAtMs > 0)
                catCommitText += QString::fromUtf8(" \xC2\xB7 ") +
                                 formatShortRelativeTime(
                                     catIdentity.committedAtMs / 1000) +
                                 QStringLiteral(" ago");
            setCommitIdentityCells(row, catIdentity);
            auto *catCommitItem = new QTableWidgetItem(catCommitText);
            if (!catCommit.isEmpty())
                catCommitItem->setToolTip(
                    catIdentity.subject.isEmpty()
                        ? catCommit
                        : catIdentity.subject + "\n" + catCommit);
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
            m_mirrorNodesTable->setItem(
                row, MirrorNodeColSyncDelay,
                makeSyncDelayCell(qint64(m.value("lastSync").toDouble()),
                                  catIdentity.committedAtMs, catCommit, isSource));
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





            m_mirrorNodesTable->setItem(
                row, MirrorNodeColCpu,
                makeCpuUsageCell(m.value(QStringLiteral("cpuPercent"))
                                     .toDouble(-1.0)));
            m_mirrorNodesTable->setItem(
                row, MirrorNodeColRam,
                makeByteUsageCell(
                    QStringLiteral("RAM"),
                    qint64(m.value(QStringLiteral("memUsedBytes")).toDouble()),
                    qint64(m.value(QStringLiteral("memTotalBytes")).toDouble())));
            m_mirrorNodesTable->setItem(
                row, MirrorNodeColDisk,
                makeByteUsageCell(
                    QStringLiteral("Disk"),
                    qint64(m.value(QStringLiteral("diskUsedBytes")).toDouble()),
                    qint64(m.value(QStringLiteral("diskTotalBytes")).toDouble())));
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
            m_mirrorNodesTable->setItem(
                row, MirrorNodeColTunnel,
                makeTunnelCell(tunnelByNode.value(
                    nodeName.trimmed().toLower())));


            const int catClones = m.value("clonesServed").toInt(-1);
            const int catWebsite = m.value("websiteServed").toInt(-1);
            m_mirrorNodesTable->setItem(
                row, MirrorNodeColClones,
                makeServeCountCell(catClones, clonesTip(catClones)));
            m_mirrorNodesTable->setItem(
                row, MirrorNodeColWebsite,
                makeServeCountCell(catWebsite, websiteTip(catWebsite)));


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



    activityDots.append(catalogOnlyDots);







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





    QHash<QString, NodeDotRepoState> repoStates;
    repoStates.reserve(activityDots.size());
    for (const MirrorNodeDot &d : activityDots) {
        const QString key = d.name.trimmed().toLower();
        if (!key.isEmpty())
            repoStates.insert(key, NodeDotRepoState{d.behind, d.integrityFailing});
    }
    setNodeDotRepoStates(repoStates);

    if (m_mirrorNodesSummary) {

        QString text = QString::fromUtf8("\xC2\xB7 %1 node%2 mirroring %3")
                           .arg(count)
                           .arg(count == 1 ? "" : "s")
                           .arg(source);
        if (maxRepoBytes > 0)
            text += QString::fromUtf8(" \xC2\xB7 %1 each \xC2\xB7 %2 total")
                        .arg(formatByteSize(maxRepoBytes),
                             formatByteSize(totalBytes));




        if (weAreSource && outOfSyncPeers > 0)
            text += QString::fromUtf8(
                        " \xC2\xB7 <span style='color:#f85149'>\xE2\x9A\xA0 %1 "
                        "mirror node%2 out of sync</span>")
                        .arg(outOfSyncPeers)
                        .arg(outOfSyncPeers == 1 ? "" : "s");



        if (pinRejectedNodes > 0)
            text += QString::fromUtf8(
                        " \xC2\xB7 <span style='color:#f85149'>\xE2\x9A\xA0 %1 "
                        "node%2 failing the integrity pin</span>")
                        .arg(pinRejectedNodes)
                        .arg(pinRejectedNodes == 1 ? "" : "s");


        if (pendingPush > 0)
            text += QString::fromUtf8(
                        " \xC2\xB7 <span style='color:#d29922'>\xE2\x86\x91 %1 "
                        "to push</span>")
                        .arg(pendingPush);
        m_mirrorNodesSummary->setTextFormat(Qt::RichText);
        m_mirrorNodesSummary->setText(text);
    }




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
    updateMirrorNodeLightTimer();
}





void MainWindow::animateMirrorNodeLights()
{
    if (!m_mirrorNodesTable)
        return;
    m_nodeLightFrame = (m_nodeLightFrame + 1) % 10;
    const qreal angle = m_nodeLightFrame * 36.0;
    const QIcon caution(nodeStatusLightPixmap(QColor("#d29922"), 14, angle, true));
    const QIcon error(nodeStatusLightPixmap(QColor("#f85149"), 14, angle, true));
    QSignalBlocker block(m_mirrorNodesTable);
    for (int r = 0; r < m_mirrorNodesTable->rowCount(); ++r) {
        QTableWidgetItem *item = m_mirrorNodesTable->item(r, MirrorNodeColNode);
        if (!item)
            continue;
        const int light = item->data(kNodeLightRole).toInt();
        if (light == 1)
            item->setIcon(caution);
        else if (light == 2)
            item->setIcon(error);
    }
}




void MainWindow::updateMirrorNodeLightTimer()
{
    bool spinning = false;
    if (m_mirrorNodesTable) {
        for (int r = 0; r < m_mirrorNodesTable->rowCount() && !spinning; ++r) {
            QTableWidgetItem *item = m_mirrorNodesTable->item(r, MirrorNodeColNode);
            spinning = item && item->data(kNodeLightRole).toInt() != 0;
        }
    }
    if (spinning) {
        if (!m_nodeLightTimer) {
            m_nodeLightTimer = new QTimer(this);
            connect(m_nodeLightTimer, &QTimer::timeout, this,
                    &MainWindow::animateMirrorNodeLights);
        }
        if (!m_nodeLightTimer->isActive())
            m_nodeLightTimer->start(120);
    } else if (m_nodeLightTimer) {
        m_nodeLightTimer->stop();
    }
}

void MainWindow::fetchCatalogMirrors(const QString &owner, const QString &repo,
                                     const QString &source)
{





    if (!m_networkAccess || owner.isEmpty() || repo.isEmpty())
        return;
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/repo/%1/%2/mirrors")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(owner)),
                         QString::fromUtf8(QUrl::toPercentEncoding(repo))));



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




    if (!m_networkAccess || owner.isEmpty() || repo.isEmpty())
        return;
    QUrl url = catalogApiUrl();
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






    QByteArray channelsOut;
    if (!runGitCapture(mirrorPath,
                       {QStringLiteral("ls-tree"), QStringLiteral("-z"),
                        QStringLiteral("--name-only"),
                        branch + QStringLiteral(":.forkmesh/releases")},
                       &channelsOut, nullptr))
        return;
    static const QRegularExpression sha256Re(QStringLiteral("\\A[0-9a-f]{64}\\z"));


    const QString fallbackRepo =
        repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
        repoSegment(repo.name, QStringLiteral("repository"));
    QMap<QString, QString> pending;
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
                continue;





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




    const QString blobPath = mirrorReleaseBlobPath(mirrorPath, hash);
    QDir().mkpath(QFileInfo(blobPath).absolutePath());
    auto tmp = std::make_shared<QFile>(blobPath + QStringLiteral(".part"));
    if (!tmp->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        downloadNextReleaseBlob(index, mirrorPath, pending);
        return;
    }
    auto hasher = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);

    QUrl url = catalogApiUrl();
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
                    QFile::remove(blobPath);
                    if (!QFile::rename(partPath, blobPath)) {
                        QFile::remove(partPath);
                    } else if (index >= 0 && index < m_repositories.size() &&
                               m_repositories.at(index).mirrorPath == mirrorPath) {



                        m_mirrorAdvertSig.clear();
                        refreshRepositoryList();
                        if (m_repositories.at(index).publishToNetwork)
                            publishRepository(index, false);
                    }
                } else {
                    QFile::remove(partPath);



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


                downloadNextReleaseBlob(index, mirrorPath, pending);
            });
}

void MainWindow::pushReleaseToMirrors(const QString &tag)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const int index = m_repoDetailIndex;
    const RepositoryRecord repo = m_repositories.at(index);
    if (repo.previewOnly || repo.localPath.trimmed().isEmpty()) {
        setRepoDetailNotice(
            "Only the repository's source working copy can push releases.", true);
        return;
    }
    if (repo.isPrivate) {
        setRepoDetailNotice(
            "Private repositories use sealed replica synchronization, not SSH "
            "mirror pushes.",
            true);
        return;
    }

    QByteArray commitOut;
    const QString tagRef = QStringLiteral("refs/tags/") + tag;
    if (!runGitCapture(repo.localPath,
                       {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                        tagRef + QStringLiteral("^{commit}")},
                       &commitOut, nullptr)) {
        setRepoDetailNotice(
            QStringLiteral("Release %1 no longer resolves to a commit.").arg(tag),
            true);
        return;
    }
    const QString commit = QString::fromUtf8(commitOut).trimmed();
    if (commit.isEmpty()) {
        setRepoDetailNotice(
            QStringLiteral("Release %1 has no commit to push.").arg(tag), true);
        return;
    }

    if (m_backend)
        m_backend->notifyMirrorUpdated(
            catalogOwner(repo) + QLatin1Char('/') +
                repoSegment(repo.name, QStringLiteral("repository")),
            commit);

    propagateRepoUpdate(index);
    const int sshRemotes =
        pushToSshMirrorRemotes(index,  true, tag);
    if (sshRemotes == 0) {
        setRepoDetailNotice(
            QStringLiteral(
                "Notified online mirrors about %1, but this working copy has no "
                "SSH push remotes configured.")
                .arg(tag),
            true);
        return;
    }
    logSystem(QStringLiteral("Release: fanning %1 out to %2 SSH mirror%3.")
                  .arg(tag)
                  .arg(sshRemotes)
                  .arg(sshRemotes == 1 ? QString() : QStringLiteral("s")));
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


            if (prevTitle.isEmpty() || prevTitle == prevTag)
                suggestedTitle = suggestedTag;
            else if (prevTitle.contains(prevTag))
                suggestedTitle = QString(prevTitle).replace(prevTag, suggestedTag);
            else
                suggestedTitle = prevTitle;
        }
    }


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





    connect(genNotesButton, &QPushButton::clicked, &dialog,
            [dir, prevTag, tagEdit, targetEdit, notesEdit] {
                const QString targetRef = targetEdit->currentText().trimmed();
                if (targetRef.isEmpty())
                    return;
                QStringList args{"log", "--no-merges", "--date-order",
                                 "--format=%s%x1f%h%x1f%an"};


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



    auto *agentNotesModelCombo = new QComboBox;
    agentNotesModelCombo->setToolTip("Which model the agent uses");


    auto populateModels = [agentNotesModelCombo](const QString &provider) {
        const QSignalBlocker block(agentNotesModelCombo);
        agentNotesModelCombo->clear();
        const bool claude = agentIsClaudeProvider(provider);
        if (claude) {
            agentNotesModelCombo->addItem(QStringLiteral("Haiku"),
                                          QStringLiteral("claude-haiku-4-5"));
            agentNotesModelCombo->addItem(QStringLiteral("Sonnet"),
                                          QStringLiteral("claude-sonnet-5"));
            agentNotesModelCombo->addItem(QStringLiteral("Opus"),
                                          QStringLiteral("claude-opus-4-8"));
        } else {
            agentNotesModelCombo->addItem(QStringLiteral("GPT nano"),
                                          QStringLiteral("gpt-4.1-nano"));
            agentNotesModelCombo->addItem(QStringLiteral("GPT mini"),
                                          QStringLiteral("gpt-4.1-mini"));
            agentNotesModelCombo->addItem(QStringLiteral("GPT"),
                                          QStringLiteral("gpt-4.1"));
        }


        selectModelComboValue(
            agentNotesModelCombo,
            QSettings()
                .value(claude ? kReleaseNotesClaudeModelSetting
                              : kReleaseNotesGptModelSetting)
                .toString());
    };
    populateModels(agentNotesCombo->currentData().toString());
    connect(agentNotesCombo, &QComboBox::currentTextChanged, &dialog,
            [agentNotesCombo, populateModels](const QString &) {
                populateModels(agentNotesCombo->currentData().toString());
            });


    auto persistReleaseNotesModel = [agentNotesCombo, agentNotesModelCombo] {
        const bool claude =
            agentIsClaudeProvider(agentNotesCombo->currentData().toString());
        QSettings().setValue(claude ? kReleaseNotesClaudeModelSetting
                                     : kReleaseNotesGptModelSetting,
                             selectedModelComboValue(agentNotesModelCombo));
    };
    connect(agentNotesModelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, [persistReleaseNotesModel](int) { persistReleaseNotesModel(); });
    auto *agentNotesRow = new QWidget;
    auto *agentNotesRowLayout = new QHBoxLayout(agentNotesRow);
    agentNotesRowLayout->setContentsMargins(0, 0, 0, 0);
    agentNotesRowLayout->setSpacing(6);
    agentNotesRowLayout->addWidget(agentNotesButton);
    agentNotesRowLayout->addWidget(agentNotesCombo);
    agentNotesRowLayout->addWidget(agentNotesModelCombo);
    agentNotesRowLayout->addStretch(1);
    connect(agentNotesButton, &QPushButton::clicked, &dialog,
            [this, dir, prevTag, tagEdit, targetEdit, notesEdit, agentNotesButton,
             agentNotesCombo, agentNotesModelCombo] {
                const QString targetRef = targetEdit->currentText().trimmed();
                if (targetRef.isEmpty())
                    return;
                generateReleaseNotesWithAgent(
                    dir, prevTag, tagEdit->text().trimmed(), targetRef,
                    agentNotesCombo->currentData().toString(),
                    agentNotesModelCombo->currentData().toString(), notesEdit,
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


    const QString releaseTitle = message;
    if (!notes.isEmpty())
        message += "\n\n" + notes;








    QByteArray headBranch;
    if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &headBranch,
                      nullptr) &&
        QString::fromUtf8(headBranch).trimmed() == targetRef &&
        bumpQtVersionForRelease(dir, tag))
        logSystem(
            QStringLiteral("Bumped ForkMesh version header to match %1.").arg(tag));


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






    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        if (repo.actionsEnabled) {










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
    const QString &targetRef, const QString &provider, const QString &modelChoice,
    QPlainTextEdit *notesEdit, QPushButton *button)
{
    if (!m_networkAccess || !notesEdit || targetRef.isEmpty())
        return;



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
            "Write the release notes for version %1 as a single friendly, "
            "conversational paragraph — like a nicely written changelog blurb a "
            "human would post. Highlight the most important changes in flowing "
            "prose, skip noise like version bumps, and don't use headings, bullet "
            "points, or Markdown lists. Keep it under 500 characters. Output only "
            "the paragraph, no preamble.\n\n----- COMMITS -----\n%2")
            .arg(version, commits.join(QLatin1Char('\n')));

    const QString model =
        !modelChoice.isEmpty()
            ? modelChoice
            : (claude ? QStringLiteral("claude-haiku-4-5") : kIssueAskAiModel);

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







void MainWindow::announceReleaseOnFediverse(const QString &tag,
                                            const QString &title,
                                            const QString &notes)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (!repo.publishToNetwork)
        return;
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


    if (!runGitCapture(dir, {"rev-parse", "--verify", "--quiet",
                             QStringLiteral("refs/tags/") + tag},
                       nullptr, nullptr))
        return;




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
    diff->setOpenLinks(false);
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



    auto *fediBtn = buttons->addButton(QStringLiteral("Announce on fediverse"),
                                       QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(browseBtn, &QPushButton::clicked, &dialog, [this, &dialog, tag] {
        dialog.accept();
        setRepoBranch(tag);
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
