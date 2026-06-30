// MainWindowReleases: MainWindow feature methods, split out of MainWindow.cpp.
// Releases panel and mirror nodes.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "TerminalWidget.h"

#include "ActionFile.h"
#include "ActionRunner.h"
#include "ClaudeAgentScript.h"
#include "ClaudeIdeBridge.h"
#include "ClaudeStreamSession.h"
#include "ClaudeTranscriptView.h"
#include "ScrollJumpButtons.h"
#include "CommitCommentStore.h"
#include "StallWatchdog.h"
#include "IssueBurnup.h"
#include "QrCode.h"
#include "ReferenceLinks.h"

#include "MarkdownEditor.h"
#include "MessageRow.h"
#include "PullReviewModel.h"
#include "RepoHost.h"
#include "RepoSecurity.h"
#include "ServerNode.h"
#include "SystemStats.h"
#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QWidgetAction>
#include <QEnterEvent>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QSslError>
#include <QImage>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRadioButton>
#include <QShortcut>
#include <QTreeWidgetItem>
#include <QtMath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QEventLoop>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QTextCursor>
#include <QTimeZone>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QScreen>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSet>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringListModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleHints>
#include <QSyntaxHighlighter>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QToolButton>
#include <QTreeWidget>
#include <QSystemTrayIcon>
#include <QSvgRenderer>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QWindow>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

#ifndef Q_OS_WIN
#include <csignal>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef FORKMESH_VERSION
#define FORKMESH_VERSION "dev"
#endif
#ifndef FORKMESH_SOURCE_DIR
#define FORKMESH_SOURCE_DIR ""
#endif

using namespace forkmesh::ui;

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
    m_releasesTable->setHorizontalHeaderLabels({"Tag", "Date", "Release notes", "Artifacts", ""});
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
    // keyboard-focused row opens the tag too, matching the arrow-key navigation
    // the tab now supports (adhoc #183).
    connect(m_releasesTable, &QTableWidget::itemActivated, this,
            [this](QTableWidgetItem *item) {
                QTableWidgetItem *it =
                    item ? m_releasesTable->item(item->row(), 0) : nullptr;
                if (it)
                    setRepoBranch(it->text()); // browse the repo at the tag
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
            if (!assetLinks.isEmpty())
                artifactsByTag.insert(manifestTag,
                                      assetLinks.join(QStringLiteral(", ")));
        }
    }

    int count = 0;
    QByteArray out;
    if (!dir.isEmpty() &&
        runGitCapture(dir,
                      {"for-each-ref", "--sort=-creatordate", "refs/tags",
                       "--format=%(refname:short)%1f%(creatordate:short)%1f"
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
            const QString artifactsHtml = artifactsByTag.value(tag);
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
    if (m_releasesSummary)
        m_releasesSummary->setText(
            QString::fromUtf8("\xC2\xB7 %1 release%2")
                .arg(count)
                .arg(count == 1 ? "" : "s"));
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

void MainWindow::loadFileSearchIndex()
{
    if (!m_fileCompleter)
        return;
    QStringList paths;
    const QString dir = repoGitDir();
    QByteArray out;
    if (!dir.isEmpty() &&
        runGitCapture(dir, {"ls-tree", "-r", "--name-only", "-z", currentRef()}, &out,
                      nullptr)) {
        for (const QByteArray &record : out.split('\0'))
            if (!record.isEmpty())
                paths << QString::fromUtf8(record);
    }
    m_fileCompleter->setModel(new QStringListModel(paths, m_fileCompleter));
}

void MainWindow::loadAboutSidebar()
{
    // This panel fires several synchronous git reads back to back — `ls-tree`,
    // `for-each-ref`, a whole-tree `ls-tree -r -l` and a `shortlog -sne --all`
    // that walks every commit. On a large history those add up to multiple
    // seconds, and refreshOpenRepoDetail() calls us on every (debounced) push,
    // so do the reads under a keep-alive scope: waitForGit() then polls in short
    // slices and pumps the event loop, keeping the window responsive (and the
    // stall watchdog's heartbeat alive) instead of freezing the GUI thread.
    GitKeepAlive keepAlive;

    const QString dir = repoGitDir();
    const RepositoryRecord *repo =
        (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
            ? &m_repositories.at(m_repoDetailIndex)
            : nullptr;

    if (m_aboutEditButton) {
        const bool editable = repoHasWorkingTree();
        m_aboutEditButton->setEnabled(editable);
        m_aboutEditButton->setToolTip(
            editable
                ? QStringLiteral("Edit repository details")
                : QStringLiteral("Open a local working copy to edit repository details"));
    }

    // About text + website.
    if (m_aboutText) {
        QString text = m_repoInfo.about.isEmpty()
                           ? (repo ? repo->description : QString())
                           : m_repoInfo.about;
        if (text.isEmpty())
            text = "<span style='color:#8b949e'>No description.</span>";
        else
            text = text.toHtmlEscaped();
        if (!m_repoInfo.website.isEmpty())
            text += QStringLiteral("<br><a href=\"%1\">%1</a>")
                        .arg(m_repoInfo.website.toHtmlEscaped());
        m_aboutText->setText(text);
    }
    // Topics as chips.
    if (m_aboutTopics) {
        QStringList chips;
        for (const QString &t : m_repoInfo.topics)
            chips << "<span style='background:#1f6feb33; color:#58a6ff; "
                     "border-radius:9px; padding:1px 8px;'>" +
                         t.toHtmlEscaped() + "</span>";
        m_aboutTopics->setText(chips.join(" "));
        m_aboutTopics->setVisible(!chips.isEmpty());
    }

    // Community files: surface README / LICENSE / CONTRIBUTING / … as links that
    // open the file in the overview. Match the repo root case-insensitively.
    if (m_aboutFiles) {
        QStringList roots;
        QByteArray out;
        if (!dir.isEmpty() &&
            runGitCapture(dir, {"ls-tree", "--name-only", currentRef()}, &out,
                          nullptr)) {
            for (const QString &line : QString::fromUtf8(out).split('\n')) {
                const QString t = line.trimmed();
                if (!t.isEmpty())
                    roots << t;
            }
        }
        // label, octicon, set of accepted base-name prefixes (lowercase).
        const struct {
            const char *label;
            const char *icon;
            QStringList prefixes;
        } wanted[] = {
            {"README", "repo", {"readme"}},
            {"License", "shield-check", {"license", "licence", "copying"}},
            {"Contributing", "people", {"contributing"}},
            {"Code of Conduct", "comment", {"code_of_conduct"}},
            {"Security", "lock", {"security"}},
        };
        QStringList links;
        for (const auto &w : wanted) {
            QString match;
            for (const QString &f : std::as_const(roots)) {
                const QString base = f.section('.', 0, 0).toLower();
                if (w.prefixes.contains(base)) {
                    match = f;
                    break;
                }
            }
            if (match.isEmpty())
                continue;
            links << QStringLiteral(
                         "<a href=\"%1\" style='color:#58a6ff; text-decoration:none'>"
                         "%2%3</a>")
                         .arg(match.toHtmlEscaped(),
                              octiconMarkup(w.icon, 13, QColor("#58a6ff")),
                              QString::fromUtf8("&nbsp;") + QString(w.label));
        }
        m_aboutFiles->setText(links.join(QString::fromUtf8("&nbsp;&nbsp; ")));
        m_aboutFiles->setVisible(!links.isEmpty());
    }

    // Latest release: newest tag by creation date.
    if (m_releaseHeader && m_releaseRow) {
        QString tag, when;
        QByteArray out;
        if (!dir.isEmpty() &&
            runGitCapture(dir,
                          {"for-each-ref", "--sort=-creatordate", "--count=1",
                           "--format=%(refname:short)%09%(creatordate:relative)",
                           "refs/tags"},
                          &out, nullptr)) {
            const QString line = QString::fromUtf8(out).trimmed();
            const int tab = line.indexOf('\t');
            if (tab > 0) {
                tag = line.left(tab).trimmed();
                when = line.mid(tab + 1).trimmed();
            } else if (!line.isEmpty()) {
                tag = line;
            }
        }
        const bool has = !tag.isEmpty();
        m_releaseHeader->setVisible(has);
        m_releaseRow->setVisible(has);
        if (has) {
            QString row =
                QStringLiteral("<a href=\"#releases\" style='color:#58a6ff; "
                               "text-decoration:none'>%1<span style='background:"
                               "#238636; color:#fff; border-radius:9px; "
                               "padding:1px 8px; font-weight:600'>%2</span></a>")
                    .arg(octiconMarkup("tag", 14, QColor("#3fb950")) +
                             QString::fromUtf8("&nbsp;"),
                         tag.toHtmlEscaped());
            if (!when.isEmpty())
                row += QStringLiteral(
                           "<br><span style='color:#8b949e'>released %1</span>")
                           .arg(when.toHtmlEscaped());
            m_releaseRow->setText(row);
        }
    }

    // Languages: aggregate blob sizes per language.
    if (m_langBar && m_langLegend) {
        QHash<QString, qint64> bytesByLang;
        qint64 total = 0;
        QByteArray out;
        if (!dir.isEmpty() &&
            runGitCapture(dir, {"ls-tree", "-r", "-l", currentRef()}, &out, nullptr)) {
            for (const QByteArray &record : out.split('\n')) {
                const int tab = record.indexOf('\t');
                if (tab < 0)
                    continue;
                const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
                if (meta.size() < 4)
                    continue;
                bool ok = false;
                const qint64 size = QString::fromUtf8(meta.at(3)).toLongLong(&ok);
                if (!ok || size <= 0)
                    continue;
                const QString name = QString::fromUtf8(record.mid(tab + 1));
                const QString lang = languageForFile(name);
                if (lang.isEmpty())
                    continue;
                bytesByLang[lang] += size;
                total += size;
            }
        }
        QList<QPair<QString, qint64>> langs;
        for (auto it = bytesByLang.constBegin(); it != bytesByLang.constEnd(); ++it)
            langs.append({it.key(), it.value()});
        std::sort(langs.begin(), langs.end(),
                  [](const auto &a, const auto &b) { return a.second > b.second; });

        QString legend;
        const int shown = qMin(5, int(langs.size()));
        for (int i = 0; i < shown && total > 0; ++i) {
            const double pct = 100.0 * langs.at(i).second / total;
            const QString color = languageColor(langs.at(i).first);
            // Keep each "● Name 12.3%" entry on one line (all non-breaking
            // spaces); only the trailing normal space between entries may wrap.
            legend += QString::fromUtf8(
                          "<span style='color:%1'>\xE2\x97\x8F</span>&nbsp;"
                          "<span style='color:#c9d1d9'>%2</span>&nbsp;"
                          "<span style='color:#8b949e'>%3%</span>&nbsp;&nbsp; ")
                          .arg(color, langs.at(i).first.toHtmlEscaped(),
                               QString::number(pct, 'f', 1));
        }
        m_langBar->setScaledContents(true);
        m_langBar->setPixmap(languageBarPixmap(langs, total, shown, 600, 12));
        m_langLegend->setText(legend.isEmpty()
                                  ? "<span style='color:#8b949e'>No code yet.</span>"
                                  : legend);
    }

    // Contributors from git shortlog, each shown as a deterministic avatar
    // generated from their email (falling back to name) — gravatar-style.
    if (m_contributorsRow && m_contributorsHeader) {
        struct Contrib {
            QString name;
            QString email;
            int count;
        };
        QList<Contrib> contribs;
        QByteArray out;
        // -e includes the email; lines look like "  12\tName <email>".
        // Merge commits are counted (no --no-merges) so each tooltip's
        // "N commits" is that author's true commit total — matching what
        // `git shortlog -sne` / `git log --author` report — rather than
        // silently dropping every merge they performed.
        if (!dir.isEmpty() &&
            runGitCapture(dir, {"shortlog", "-sne", "--all"}, &out,
                          nullptr)) {
            for (const QString &line : QString::fromUtf8(out).split('\n')) {
                const QString t = line.trimmed();
                if (t.isEmpty())
                    continue;
                const int tab = t.indexOf('\t');
                if (tab < 0)
                    continue;
                QString who = t.mid(tab + 1).trimmed();
                QString email;
                const int lt = who.lastIndexOf('<');
                const int gt = who.lastIndexOf('>');
                if (lt >= 0 && gt > lt) {
                    email = who.mid(lt + 1, gt - lt - 1).trimmed();
                    who = who.left(lt).trimmed();
                }
                contribs.append({who, email, t.left(tab).toInt()});
            }
        }
        m_contributorsHeader->setText(
            QStringLiteral("CONTRIBUTORS %1").arg(formatCount(contribs.size())));

        // Round a source PNG into a rounded-rect avatar (rendered at 2x for
        // crisp hi-dpi edges) so contributors read as soft tiles rather than
        // hard squares. Falls back to the original bytes if decoding fails.
        auto rounded = [](QByteArray src, int px) -> QByteArray {
            QPixmap p;
            if (!p.loadFromData(src, "PNG") || p.isNull())
                return src;
            const int s = px * 2;
            const QPixmap scaled = p.scaled(s, s, Qt::KeepAspectRatioByExpanding,
                                            Qt::SmoothTransformation);
            QPixmap out(s, s);
            out.fill(Qt::transparent);
            QPainter painter(&out);
            painter.setRenderHint(QPainter::Antialiasing, true);
            QPainterPath path;
            path.addRoundedRect(0, 0, s, s, s * 0.28, s * 0.28);
            painter.setClipPath(path);
            painter.drawPixmap(0, 0, scaled);
            painter.end();
            QByteArray result;
            QBuffer buf(&result);
            buf.open(QIODevice::WriteOnly);
            out.save(&buf, "PNG");
            return result;
        };

        // Embed each avatar as an inline base64 PNG so it renders in rich text.
        auto avatarTag = [this, &rounded](const Contrib &c, int px) {
            const QString custom = m_repoInfo.contributorAvatars.value(c.name);
            QByteArray png;
            QPixmap fromFile;
            if (!custom.isEmpty() && fromFile.load(custom)) {
                QBuffer buf(&png);
                buf.open(QIODevice::WriteOnly);
                fromFile.save(&buf, "PNG");
            } else {
                const QString seed =
                    c.email.isEmpty() ? c.name.toLower() : c.email.toLower();
                png = forkMeshAvatarPng(seed);
            }
            png = rounded(png, px);
            const QString tip = (c.name + QString::fromUtf8(" \xC2\xB7 ") +
                                 QString::number(c.count) + " commits")
                                    .toHtmlEscaped();
            return QStringLiteral(
                       "<img src='data:image/png;base64,%1' width='%2' "
                       "height='%2' title='%3'>")
                .arg(QString::fromLatin1(png.toBase64()))
                .arg(px)
                .arg(tip);
        };

        QString html;
        const int shown = qMin(12, int(contribs.size()));
        for (int i = 0; i < shown; ++i)
            html += avatarTag(contribs.at(i), 28) +
                    QString::fromUtf8("&nbsp;&nbsp;");
        if (contribs.size() > shown)
            html += QStringLiteral(
                        "<span style='color:#8b949e'>&nbsp;+%1</span>")
                        .arg(contribs.size() - shown);
        m_contributorsRow->setText(html.isEmpty()
                                       ? "<span style='color:#8b949e'>None yet.</span>"
                                       : html);
    }
}

void MainWindow::spinRefreshButton(QPushButton *button)
{
    if (!button || button->property("fmSpinning").toBool())
        return;
    button->setProperty("fmSpinning", true);
    const int size = button->iconSize().width() > 0 ? button->iconSize().width() : 16;
    const QIcon original = button->icon();
    auto *timer = new QTimer(button);
    auto angle = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, button, [button, angle, size] {
        *angle = (*angle + 30) % 360;
        button->setIcon(
            QIcon(refreshPixmap(QColor(Theme::kTextTertiary), *angle, size)));
    });
    timer->start(60);
    // These refreshes are synchronous (or fire-and-forget), so a brief spin is
    // enough to acknowledge the click; then restore the button's own icon.
    QTimer::singleShot(650, button, [button, timer, original] {
        timer->stop();
        timer->deleteLater();
        button->setIcon(original);
        button->setProperty("fmSpinning", false);
    });
}

void MainWindow::addRefreshSpin(QPushButton *button)
{
    if (!button)
        return;
    connect(button, &QPushButton::clicked, this,
            [this, button] { spinRefreshButton(button); });
}

void MainWindow::startButtonSpin(QPushButton *button)
{
    if (!button || button->property("fmSpinning").toBool())
        return;
    button->setProperty("fmSpinning", true);
    button->setProperty("fmSpinIcon", QVariant::fromValue(button->icon()));
    const int size = button->iconSize().width() > 0 ? button->iconSize().width() : 16;
    auto *timer = new QTimer(button);
    timer->setObjectName(QStringLiteral("fmSpinTimer"));
    auto angle = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, button, [button, angle, size] {
        *angle = (*angle + 30) % 360;
        button->setIcon(
            QIcon(refreshPixmap(QColor(Theme::kTextTertiary), *angle, size)));
    });
    timer->start(60);
}

void MainWindow::stopButtonSpin(QPushButton *button)
{
    if (!button || !button->property("fmSpinning").toBool())
        return;
    if (auto *timer = button->findChild<QTimer *>(QStringLiteral("fmSpinTimer"))) {
        timer->stop();
        timer->deleteLater();
    }
    button->setIcon(button->property("fmSpinIcon").value<QIcon>());
    button->setProperty("fmSpinning", false);
}

void MainWindow::startRestartSpin(QPushButton *button)
{
    if (!button)
        return;
    stopRestartSpin();
    m_restartSpinButton = button;
    startButtonSpin(button);
}

void MainWindow::stopRestartSpin()
{
    if (!m_restartSpinButton)
        return;
    stopButtonSpin(m_restartSpinButton);
    m_restartSpinButton = nullptr;
}

void MainWindow::startRefreshSpin()
{
    if (!m_refreshButton)
        return;
    if (!m_refreshSpinTimer) {
        m_refreshSpinTimer = new QTimer(this);
        connect(m_refreshSpinTimer, &QTimer::timeout, this, [this] {
            m_refreshAngle = (m_refreshAngle + 30) % 360;
            m_refreshButton->setIcon(
                QIcon(refreshPixmap(QColor(Theme::kTextTertiary), m_refreshAngle, 22)));
        });
    }
    m_refreshSpinTimer->start(60);
}

void MainWindow::stopRefreshSpin()
{
    if (m_refreshSpinTimer)
        m_refreshSpinTimer->stop();
    if (m_refreshButton)
        m_refreshButton->setIcon(
            QIcon(refreshPixmap(QColor(Theme::kTextTertiary), 0, 22)));
}

void MainWindow::startCommitsRefreshSpin()
{
    if (!m_commitsRefreshButton)
        return;
    if (!m_commitsRefreshSpinTimer) {
        m_commitsRefreshSpinTimer = new QTimer(this);
        connect(m_commitsRefreshSpinTimer, &QTimer::timeout, this, [this] {
            m_commitsRefreshAngle = (m_commitsRefreshAngle + 30) % 360;
            if (m_commitsRefreshButton)
                m_commitsRefreshButton->setIcon(QIcon(refreshPixmap(
                    QColor(Theme::kTextTertiary), m_commitsRefreshAngle, 16)));
        });
    }
    m_commitsRefreshSpinTimer->start(60);
}

void MainWindow::stopCommitsRefreshSpin()
{
    if (m_commitsRefreshSpinTimer)
        m_commitsRefreshSpinTimer->stop();
    m_commitsRefreshAngle = 0;
    if (m_commitsRefreshButton)
        m_commitsRefreshButton->setIcon(
            QIcon(refreshPixmap(QColor(Theme::kTextTertiary), 0, 16)));
}

void MainWindow::startCommitDiffSpin()
{
    if (m_commitDiffSpinner)
        m_commitDiffSpinner->show();
}

void MainWindow::stopCommitDiffSpin()
{
    if (m_commitDiffSpinner)
        m_commitDiffSpinner->hide();
}

void MainWindow::startNodeSwitchSpin()
{
    if (!m_nodeMenuButton)
        return;
    if (!m_nodeSwitchSpinTimer) {
        m_nodeSwitchSpinTimer = new QTimer(this);
        connect(m_nodeSwitchSpinTimer, &QTimer::timeout, this, [this] {
            m_nodeSwitchAngle = (m_nodeSwitchAngle + 30) % 360;
            m_nodeMenuButton->setIcon(
                QIcon(refreshPixmap(QColor(Theme::kTextTertiary),
                                    m_nodeSwitchAngle, 16)));
        });
    }
    m_nodeSwitchSpinTimer->start(60);

    // Indeterminate loading bar pinned just below the node button for the length
    // of the (potentially multi-second) switch. Parented to the node button's
    // container so it floats over the bar without disturbing the layout.
    if (!m_nodeSwitchProgress) {
        m_nodeSwitchProgress =
            new QProgressBar(m_nodeMenuButton->parentWidget());
        m_nodeSwitchProgress->setObjectName("nodeSwitchProgress");
        m_nodeSwitchProgress->setRange(0, 0); // busy / indeterminate
        m_nodeSwitchProgress->setTextVisible(false);
        m_nodeSwitchProgress->setFixedHeight(3);
        m_nodeSwitchProgress->hide();
    }
    positionNodeSwitchProgress();
    m_nodeSwitchProgress->show();
    m_nodeSwitchProgress->raise();
}

void MainWindow::positionNodeSwitchProgress()
{
    if (!m_nodeSwitchProgress || !m_nodeMenuButton)
        return;
    QWidget *parent = m_nodeSwitchProgress->parentWidget();
    if (!parent)
        return;
    const QPoint topLeft = m_nodeMenuButton->mapTo(
        parent, QPoint(0, m_nodeMenuButton->height() + 1));
    m_nodeSwitchProgress->setGeometry(topLeft.x(), topLeft.y(),
                                      m_nodeMenuButton->width(), 3);
}

void MainWindow::stopNodeSwitchSpin()
{
    if (m_nodeSwitchSpinTimer)
        m_nodeSwitchSpinTimer->stop();
    if (m_nodeSwitchProgress)
        m_nodeSwitchProgress->hide();
    // Restore the node button's normal label + OS/online badge icon.
    updateNodeSwitcher();
}

void MainWindow::startRepoSwitchSpin()
{
    if (!m_repoMenuButton)
        return;
    if (!m_repoSwitchSpinTimer) {
        m_repoSwitchSpinTimer = new QTimer(this);
        connect(m_repoSwitchSpinTimer, &QTimer::timeout, this, [this] {
            m_repoSwitchAngle = (m_repoSwitchAngle + 30) % 360;
            m_repoMenuButton->setIcon(
                QIcon(refreshPixmap(QColor(Theme::kTextTertiary),
                                    m_repoSwitchAngle, 16)));
        });
    }
    m_repoMenuButton->setIcon(
        QIcon(refreshPixmap(QColor(Theme::kTextTertiary), 0, 16)));
    m_repoSwitchSpinTimer->start(60);
}

void MainWindow::stopRepoSwitchSpin()
{
    if (m_repoSwitchSpinTimer)
        m_repoSwitchSpinTimer->stop();
    // Clear the spinner icon; the repo button shows just its label + count.
    if (m_repoMenuButton)
        m_repoMenuButton->setIcon(QIcon());
    updateRepoSwitcher();
}

void MainWindow::openRepoDetailDeferred(int repoIndex)
{
    if (repoIndex < 0 || repoIndex >= m_repositories.size())
        return;
    // Already open: just surface its view, no reload.
    if (repoIndex == m_repoDetailIndex && !m_repoDetailLoading) {
        showSection(0);
        return;
    }
    // Coalesce duplicate requests for the same repo (refreshRepositoryList can
    // fire repeatedly while repos sync in) so we don't stack deferred loads.
    if (m_repoOpenPending == repoIndex)
        return;
    m_repoOpenPending = repoIndex;
    // Paint busy feedback immediately, then run the heavy synchronous load on the
    // next event-loop turn so the dropdown closes and the spinner shows first.
    startRepoSwitchSpin();
    showLoadStatus(QStringLiteral("Opening repository…"));
    QApplication::setOverrideCursor(Qt::BusyCursor);
    QTimer::singleShot(0, this, [this, repoIndex] {
        m_repoOpenPending = -1;
        QElapsedTimer timer;
        timer.start();
        // m_repoLoadActive lets nodeSwitchStep narrate this load too (it otherwise
        // only speaks during node switches); openRepoDetail's steps update the pill.
        m_repoLoadActive = true;
        openRepoDetail(repoIndex);
        m_repoLoadActive = false;
        finishLoadStepTiming(); // log the final step's duration
        stopRepoSwitchSpin();
        QApplication::restoreOverrideCursor();
        // Confirm the result where the user is looking: a brief toast for a slow
        // open, otherwise just retire the progress pill.
        const qint64 ms = timer.elapsed();
        if (ms > 500 && repoIndex >= 0 && repoIndex < m_repositories.size()) {
            const RepositoryRecord &r = m_repositories.at(repoIndex);
            flashMessage(QStringLiteral("Opened %1/%2 in %3 ms.")
                             .arg(r.owner, r.name)
                             .arg(ms));
        } else if (m_loadStatusShowing) {
            dismissTopMessage();
        }
    });
}

void MainWindow::nodeSwitchStep(const QString &what)
{
    // Narrate a repo-load step, but only while a user-driven load is in flight —
    // a node switch (m_nodeSwitching) or opening a repo (m_repoLoadActive).
    // openRepoDetail is also called on startup, which shouldn't spam the user.
    // Show the step in the top bar and log it, then yield to the event loop —
    // user input excluded so a click can't re-enter the load — so the spinner
    // keeps animating and each step appears as the work happens.
    if (!m_nodeSwitching && !m_repoLoadActive)
        return;
    // Close out the previous step in the log with how long it took, so the user
    // gets a real-time, timed breakdown of where a switch spends its time (and
    // the slow step is obvious at a glance) rather than a wall of equal-looking
    // lines. The duration is appended to the just-finished step, not this one.
    if (!m_loadStepName.isEmpty() && m_loadStepTimer.isValid())
        logSystem(QStringLiteral("  - %1 (%2 ms)")
                      .arg(m_loadStepName)
                      .arg(m_loadStepTimer.elapsed()));
    showLoadStatus(what);
    QString plain = what;
    plain.replace(QChar(0x2026), QStringLiteral("..."));
    m_loadStepName = plain;
    m_loadStepTimer.restart();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

// Flush the final (still-running) narration step to the log with its duration.
// Called when a node switch / repo open finishes, since nodeSwitchStep only logs
// a step's timing when the *next* step starts — the last step has no successor.
void MainWindow::finishLoadStepTiming()
{
    if (!m_loadStepName.isEmpty() && m_loadStepTimer.isValid())
        logSystem(QStringLiteral("  - %1 (%2 ms)")
                      .arg(m_loadStepName)
                      .arg(m_loadStepTimer.elapsed()));
    m_loadStepName.clear();
    m_loadStepTimer.invalidate();
}

void MainWindow::showLoadStatus(const QString &what)
{
    if (!m_topMessage || what.isEmpty())
        return;
    m_topMessageRaw = what;
    // Blue, persistent progress pill — distinct from the green success / red
    // error toast — naming the current step. The node/repo button spinner and the
    // node-switch bar convey motion; this conveys *what* is happening.
    m_topMessage->setText(
        QStringLiteral("<span style='color:#58a6ff'>%1 %2</span>")
            .arg(QString::fromUtf8("\xE2\x9F\xB3"), // ⟳
                 what.toHtmlEscaped()));
    m_topMessage->setWordWrap(false);
    m_topMessage->show();
    m_loadStatusShowing = true;
    m_topMessageElided = false;
    m_topMessageExpanded = false;
    if (m_topMessageTimer)
        m_topMessageTimer->stop(); // don't let it fade out mid-load
    if (m_topMessageOverlay)
        m_topMessageOverlay->hide(); // drop any leftover expanded panel
    if (m_topMessageExpand)
        m_topMessageExpand->hide();
    if (m_topMessageCopy)
        m_topMessageCopy->hide();
    if (m_topMessageClose)
        m_topMessageClose->hide();
}

int MainWindow::issuesRepoIndex() const
{
    if (!m_issuesRepoCombo || m_issuesRepoCombo->currentIndex() < 0)
        return -1;
    bool ok = false;
    const int idx = m_issuesRepoCombo->currentData().toInt(&ok);
    if (!ok || idx < 0 || idx >= m_repositories.size())
        return -1;
    return idx;
}

const RepositoryRecord &MainWindow::writableRecordFor(
    const RepositoryRecord &repo) const
{
    // Already backed by a working tree we can commit to.
    if (!repo.localPath.trimmed().isEmpty() &&
        QFileInfo::exists(repo.localPath + QStringLiteral("/.git")))
        return repo;
    // Otherwise, if we own a real working-tree copy of the same repo (e.g. the
    // selected entry is a read-only browse/preview of a repo we host), use it so
    // the source of truth can author locally instead of being told it's read-only.
    for (const RepositoryRecord &r : m_repositories) {
        if (r.previewOnly || &r == &repo)
            continue;
        if (r.owner == repo.owner && r.name == repo.name &&
            !r.localPath.trimmed().isEmpty() &&
            QFileInfo::exists(r.localPath + QStringLiteral("/.git")))
            return r;
    }
    return repo;
}

QString MainWindow::repoAgentGitDir(const RepositoryRecord &repo) const
{
    // Prefer a working-tree checkout we can run plumbing against directly.
    const RepositoryRecord &writable = writableRecordFor(repo);
    if (!writable.localPath.trimmed().isEmpty() &&
        QFileInfo::exists(writable.localPath + QStringLiteral("/.git")))
        return writable.localPath;
    // A preview is a throwaway browse cache, not a repo we mirror to contribute
    // to — don't run agents against it (mirror it first, like repoCanProposePull).
    if (repo.previewOnly)
        return QString();
    // Otherwise fall back to the bare network mirror: `git worktree add` and
    // `git diff` both work straight off it, so a node that only mirrors a repo
    // can still run agents and open pull requests to the owner (adhoc #191).
    const QString mirror = repo.mirrorPath.trimmed();
    if (!mirror.isEmpty() && QDir(mirror).exists())
        return mirror;
    return QString();
}

IssueStore MainWindow::issueStoreForCurrentRepo() const
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return IssueStore(QString(), QString(), &m_profileIdentity, m_userName);
    const RepositoryRecord &repo = writableRecordFor(m_repositories.at(idx));
    return IssueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity, m_userName);
}

void MainWindow::refreshIssuesRepoCombo()
{
    if (!m_issuesRepoCombo)
        return;
    const QVariant previous =
        m_issuesRepoCombo->count() ? m_issuesRepoCombo->currentData() : QVariant();
    QSignalBlocker blocker(m_issuesRepoCombo);
    m_issuesRepoCombo->clear();
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        m_issuesRepoCombo->addItem(repo.owner + "/" + repo.name, i);
    }
    if (previous.isValid()) {
        const int restore = m_issuesRepoCombo->findData(previous);
        if (restore >= 0)
            m_issuesRepoCombo->setCurrentIndex(restore);
    }
    blocker.unblock();
    reloadIssues();
}

void MainWindow::reloadIssues()
{
    if (!m_issueTable)
        return;
    if (issuesRepoIndex() < 0) {
        m_currentIssues.clear();
        m_currentLabels.clear();
        m_currentMilestones.clear();
        m_issueTable->setRowCount(0);
        if (m_issueMilestonesTable)
            m_issueMilestonesTable->setRowCount(0);
        if (m_issueLabelsTable)
            m_issueLabelsTable->setRowCount(0);
        m_currentIssueNumber = -1;
        renderIssueThread(Issue());
        updateIssueActionState();
        updateRepoIssueCount();
        return;
    }
    const IssueStore store = issueStoreForCurrentRepo();
    m_currentIssues = store.loadAll();
    m_currentLabels = store.loadLabels();
    m_currentMilestones = store.loadMilestones();

    QSignalBlocker labelBlock(m_issueLabelFilter);
    m_issueLabelFilter->clear();
    m_issueLabelFilter->addItem("All labels", QString());
    for (const IssueLabel &label : m_currentLabels)
        m_issueLabelFilter->addItem(label.name, label.name);
    labelBlock.unblock();

    QSignalBlocker msBlock(m_issueMilestoneFilter);
    m_issueMilestoneFilter->clear();
    m_issueMilestoneFilter->addItem("All milestones", QString());
    for (const IssueMilestone &ms : m_currentMilestones)
        m_issueMilestoneFilter->addItem(ms.title, ms.title);
    msBlock.unblock();

    refreshIssueList();
    refreshIssueMilestones();
    refreshIssueLabels();
    updateIssueActionState();
    updateRepoIssueCount();
}

QWidget *MainWindow::makeIssueRow(const Issue &issue,
                                  const QHash<QString, QString> &labelColors) const
{
    auto *row = new QWidget;
    auto *col = new QVBoxLayout(row);
    col->setContentsMargins(8, 5, 8, 5);
    col->setSpacing(4);

    QString titleText =
        QStringLiteral("#%1  %2").arg(issue.number).arg(issue.title.toHtmlEscaped());
    if (issue.status == "closed")
        titleText += "  (closed)";
    auto *title = new QLabel(titleText);
    title->setObjectName("issueRowTitle");
    title->setWordWrap(true);
    col->addWidget(title);

    if (!issue.labels.isEmpty() || !issue.milestone.isEmpty()) {
        auto *pills = new QHBoxLayout;
        pills->setContentsMargins(0, 0, 0, 0);
        pills->setSpacing(4);
        // Each label shown as a colored, pill-shaped chip. The id selector keeps
        // the dynamic background from being overridden by the sidebar's
        // "#sidebar QLabel { background: transparent }" rule.
        for (const QString &name : issue.labels) {
            const QString bg = labelColors.value(name, QStringLiteral("#94a3b8"));
            auto *pill = new QLabel(name);
            pill->setObjectName("issuePill");
            pill->setStyleSheet(
                QStringLiteral("QLabel#issuePill { background:%1; color:%2; "
                               "border-radius:9px; padding:1px 8px; "
                               "font-size:11px; font-weight:600; }")
                    .arg(bg, pillTextColor(bg)));
            pills->addWidget(pill, 0, Qt::AlignLeft);
        }
        // The milestone (if any) as a subtle outlined pill.
        if (!issue.milestone.isEmpty()) {
            auto *ms = new QLabel(
                QStringLiteral("Milestone %1").arg(issue.milestone));
            ms->setObjectName("issueMilestonePill");
            ms->setStyleSheet(
                "QLabel#issueMilestonePill { border:1px solid #8b949e; "
                "color:#8b949e; border-radius:9px; padding:1px 8px; "
                "font-size:11px; }");
            pills->addWidget(ms, 0, Qt::AlignLeft);
        }
        pills->addStretch();
        col->addLayout(pills);
    }
    return row;
}

void MainWindow::selectIssueListTab(int id)
{
    if (!m_issueListStack)
        return;
    m_issueListStack->setCurrentIndex(id);
    const bool tableMode = id == 0; // the Issues table
    const bool boardMode = id == 3; // the Kanban board
    // Search + label/milestone filters apply to both the table and the board; the
    // Open/Closed status filter is table-only (the board's Done column *is* the
    // closed state). The detail toggle drives the shared right-hand issue panel.
    m_issueSearch->setVisible(tableMode || boardMode);
    m_issueLabelFilter->setVisible(tableMode || boardMode);
    m_issueMilestoneFilter->setVisible(tableMode || boardMode);
    m_issueStatusFilter->setVisible(tableMode);
    m_issueDetailToggle->setVisible(tableMode || boardMode);
    if (boardMode)
        refreshIssueBoard();
}

namespace {
// Default Kanban columns for a repo that hasn't customized them. The final column
// is treated as "done" and maps to the issue's closed status.
const QStringList kDefaultBoardColumns = {QStringLiteral("Backlog"),
                                          QStringLiteral("Todo"),
                                          QStringLiteral("In Progress"),
                                          QStringLiteral("Done")};

// The reserved label that encodes a card's board column (case-insensitive).
QString boardStatusLabel(const QString &column)
{
    return QStringLiteral("status:") + column.trimmed().toLower();
}

bool isBoardStatusLabel(const QString &label)
{
    return label.startsWith(QStringLiteral("status:"), Qt::CaseInsensitive);
}
} // namespace

QStringList MainWindow::boardColumns() const
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return kDefaultBoardColumns;
    const RepositoryRecord &repo = m_repositories.at(idx);
    QSettings settings;
    const QString key =
        QStringLiteral("issueBoard/columns/%1/%2").arg(repo.owner, repo.name);
    const QStringList saved = settings.value(key).toStringList();
    QStringList cleaned;
    for (const QString &c : saved) {
        const QString t = c.trimmed();
        if (!t.isEmpty() && !cleaned.contains(t, Qt::CaseInsensitive))
            cleaned << t;
    }
    return cleaned.isEmpty() ? kDefaultBoardColumns : cleaned;
}

void MainWindow::setBoardColumns(const QStringList &cols)
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    QStringList cleaned;
    for (const QString &c : cols) {
        const QString t = c.trimmed();
        if (!t.isEmpty() && !cleaned.contains(t, Qt::CaseInsensitive))
            cleaned << t;
    }
    if (cleaned.size() < 2) {
        setIssueInlineNotice("A board needs at least two columns.", true);
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(idx);
    QSettings settings;
    const QString key =
        QStringLiteral("issueBoard/columns/%1/%2").arg(repo.owner, repo.name);
    settings.setValue(key, cleaned);
    refreshIssueBoard();
}

void MainWindow::editBoardColumns()
{
    bool ok = false;
    const QString current = boardColumns().join(QStringLiteral(", "));
    const QString text = QInputDialog::getText(
        this, tr("Edit board columns"),
        tr("Column names, left to right (comma separated).\nThe last column is the "
           "\"done\" column and maps to closed issues."),
        QLineEdit::Normal, current, &ok);
    if (!ok)
        return;
    const QStringList cols = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    setBoardColumns(cols);
}

QString MainWindow::issueBoardColumn(const Issue &issue) const
{
    const QStringList cols = boardColumns();
    if (cols.isEmpty())
        return QString();
    // Closed issues live in the final ("done") column regardless of any label.
    if (issue.status == QStringLiteral("closed"))
        return cols.last();
    // Otherwise the column is named by the issue's "status:<name>" label.
    for (const QString &col : cols) {
        const QString want = boardStatusLabel(col);
        for (const QString &lbl : issue.labels)
            if (lbl.compare(want, Qt::CaseInsensitive) == 0)
                return col;
    }
    // No status label yet: an unlabeled open issue starts in the first column.
    return cols.first();
}

void MainWindow::moveIssueToColumn(int number, const QString &column)
{
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        setIssueInlineNotice(
            "This repository is read-only here, so issues can't be moved. Open it "
            "on the owning node to organize the board.",
            true);
        return;
    }
    const Issue *cur = nullptr;
    for (const Issue &i : m_currentIssues)
        if (i.number == number) {
            cur = &i;
            break;
        }
    if (!cur)
        return;
    const QStringList cols = boardColumns();
    if (cols.isEmpty() || issueBoardColumn(*cur).compare(column, Qt::CaseInsensitive) == 0)
        return; // already there (or nothing to move to)
    const bool toDone = column.compare(cols.last(), Qt::CaseInsensitive) == 0;

    // Rewrite the issue's labels: drop any existing status:* label, then tag the
    // target column (the done column relies on the closed status, not a label).
    QStringList labels;
    for (const QString &lbl : cur->labels)
        if (!isBoardStatusLabel(lbl))
            labels << lbl;
    if (!toDone)
        labels << boardStatusLabel(column);

    QString error;
    if (!store.setLabels(number, labels, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not move the issue." : error,
                             true);
        return;
    }
    // Keep open/closed in step with the board: the done column == closed.
    const QString wantStatus =
        toDone ? QStringLiteral("closed") : QStringLiteral("open");
    if (cur->status != wantStatus &&
        !store.setStatus(number, wantStatus, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update issue status."
                                             : error,
                             true);
        return;
    }
    propagateRepoUpdate(issuesRepoIndex());
    reloadIssues();
    setIssueInlineNotice(
        QStringLiteral("Moved #%1 to %2.").arg(number).arg(column));
}

QWidget *MainWindow::buildIssueBoard()
{
    auto *wrap = new QWidget;
    auto *outer = new QVBoxLayout(wrap);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);

    auto *editCols = new QPushButton("Edit columns");
    editCols->setObjectName("ghostButton");
    editCols->setProperty("buttonSize", "sm");
    editCols->setCursor(Qt::PointingHandCursor);
    editCols->setToolTip("Rename, add or remove the board's status columns");
    setOcticon(editCols, "gear", 16);
    connect(editCols, &QPushButton::clicked, this, &MainWindow::editBoardColumns);
    auto *hint = new QLabel("Drag a card to another column to change its status.");
    hint->setObjectName("statusLine");
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->addWidget(hint);
    bar->addStretch();
    bar->addWidget(editCols);
    outer->addLayout(bar);

    auto *scroll = new QScrollArea;
    scroll->setObjectName("issueBoardScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inner = new QWidget;
    m_issueBoardColumns = new QHBoxLayout(inner);
    m_issueBoardColumns->setContentsMargins(2, 2, 2, 2);
    m_issueBoardColumns->setSpacing(10);
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);

    m_issueBoard = wrap;
    return wrap;
}

void MainWindow::refreshIssueBoard()
{
    if (!m_issueBoardColumns)
        return;

    // Tear down the previous columns (widgets and the trailing stretch).
    while (QLayoutItem *child = m_issueBoardColumns->takeAt(0)) {
        if (QWidget *w = child->widget())
            w->deleteLater();
        delete child;
    }

    const QStringList cols = boardColumns();
    QHash<QString, QString> labelColors;
    for (const IssueLabel &label : std::as_const(m_currentLabels))
        labelColors.insert(label.name, label.color);

    // Honor the same label/milestone/search filters as the table (but not the
    // Open/Closed filter — the board shows every issue across its columns).
    const QString labelFilter =
        m_issueLabelFilter ? m_issueLabelFilter->currentData().toString() : QString();
    const QString msFilter =
        m_issueMilestoneFilter ? m_issueMilestoneFilter->currentData().toString()
                               : QString();
    const QString search =
        m_issueSearch ? m_issueSearch->text().trimmed() : QString();

    // Bucket the (filtered) issues by their column, preserving column order.
    QHash<QString, QList<const Issue *>> buckets;
    for (const Issue &issue : std::as_const(m_currentIssues)) {
        if (!labelFilter.isEmpty() && !issue.labels.contains(labelFilter))
            continue;
        if (!msFilter.isEmpty() && issue.milestone != msFilter)
            continue;
        if (!search.isEmpty()) {
            const QString hay = QStringLiteral("#%1 %2 %3 %4 %5")
                                    .arg(issue.number)
                                    .arg(issue.title)
                                    .arg(issue.priority)
                                    .arg(issue.labels.join(" "), issue.milestone);
            if (!hay.contains(search, Qt::CaseInsensitive))
                continue;
        }
        buckets[issueBoardColumn(issue)].append(&issue);
    }

    const bool writable = issueStoreForCurrentRepo().canWrite();
    for (const QString &col : cols) {
        const QList<const Issue *> items = buckets.value(col);

        auto *column = new QWidget;
        column->setObjectName("issueBoardColumn");
        column->setMinimumWidth(230);
        column->setMaximumWidth(320);
        auto *cl = new QVBoxLayout(column);
        cl->setContentsMargins(8, 8, 8, 8);
        cl->setSpacing(6);

        auto *hdr = new QLabel(QStringLiteral("%1  ·  %2").arg(col).arg(items.size()));
        hdr->setObjectName("issueBoardHeader");
        cl->addWidget(hdr);

        auto *list = new BoardColumnList(col);
        enableHoverRowHighlight(list);
        // Read-only repos can't reorganize, but can still click through to issues.
        list->setDragEnabled(writable);
        list->setAcceptDrops(writable);
        if (writable)
            list->onDrop = [this](int number, const QString &target) {
                moveIssueToColumn(number, target);
            };
        for (const Issue *ip : items) {
            const Issue &issue = *ip;
            QString text = QStringLiteral("#%1  %2").arg(issue.number).arg(issue.title);
            QStringList shownLabels;
            for (const QString &lbl : issue.labels)
                if (!isBoardStatusLabel(lbl))
                    shownLabels << lbl;
            if (!shownLabels.isEmpty())
                text += QStringLiteral("\n") + shownLabels.join(QStringLiteral(", "));
            auto *item = new QListWidgetItem(text);
            item->setData(Qt::UserRole, issue.number);

            QStringList tip;
            tip << QStringLiteral("#%1  %2").arg(issue.number).arg(issue.title);
            if (issue.priority > 0)
                tip << QStringLiteral("Priority %1").arg(issue.priority);
            if (issue.progress > 0)
                tip << QStringLiteral("%1%% complete").arg(issue.progress);
            if (!issue.milestone.isEmpty())
                tip << QStringLiteral("Milestone: %1").arg(issue.milestone);
            if (issue.bountyUsd > 0)
                tip << QStringLiteral("Bounty $%1")
                           .arg(QString::number(issue.bountyUsd, 'f', 2));
            if (writable)
                tip << QStringLiteral("Drag to another column to change status");
            item->setToolTip(tip.join(QStringLiteral("\n")));
            if (issue.status == QStringLiteral("closed"))
                item->setForeground(QColor("#8b949e"));
            list->addItem(item);
        }
        connect(list, &QListWidget::itemClicked, this,
                [this](QListWidgetItem *it) {
                    if (it)
                        showIssue(it->data(Qt::UserRole).toInt());
                });
        cl->addWidget(list, 1);
        m_issueBoardColumns->addWidget(column);
    }
    m_issueBoardColumns->addStretch();
}

namespace {
// Braille spinner frames for the issue-list Agent column (same glyphs the Agents
// tab badge animates with).
const char *kAgentSpinFrames[] = {"\xE2\xA0\x8B", "\xE2\xA0\x99", "\xE2\xA0\xB9",
                                  "\xE2\xA0\xB8", "\xE2\xA0\xBC", "\xE2\xA0\xB4",
                                  "\xE2\xA0\xA6", "\xE2\xA0\xA7", "\xE2\xA0\x87",
                                  "\xE2\xA0\x8F"};
} // namespace

void MainWindow::resetIssueFilters()
{
    if (!m_issueStatusFilter)
        return;
    // Block signals so the four resets collapse into a single refreshIssueList()
    // instead of firing one rebuild per control.
    bool changed = false;
    {
        QSignalBlocker statusBlock(m_issueStatusFilter);
        QSignalBlocker labelBlock(m_issueLabelFilter);
        QSignalBlocker msBlock(m_issueMilestoneFilter);
        if (m_issueStatusFilter->currentIndex() != 0) {
            m_issueStatusFilter->setCurrentIndex(0); // "Open"
            changed = true;
        }
        if (m_issueLabelFilter->currentIndex() != 0) {
            m_issueLabelFilter->setCurrentIndex(0); // "All labels"
            changed = true;
        }
        if (m_issueMilestoneFilter->currentIndex() != 0) {
            m_issueMilestoneFilter->setCurrentIndex(0); // "All milestones"
            changed = true;
        }
    }
    if (m_issueSearch && !m_issueSearch->text().isEmpty()) {
        QSignalBlocker searchBlock(m_issueSearch);
        m_issueSearch->clear();
        changed = true;
    }
    if (changed)
        refreshIssueList();
}

void MainWindow::refreshIssueList()
{
    if (!m_issueTable)
        return;
    const QString statusFilter = m_issueStatusFilter->currentText();
    const QString labelFilter = m_issueLabelFilter->currentData().toString();
    const QString msFilter = m_issueMilestoneFilter->currentData().toString();
    const QString search =
        m_issueSearch ? m_issueSearch->text().trimmed() : QString();
    // When a close asked us to advance, target the next issue instead of the one
    // that was being viewed (which a close may have just filtered out) — issue #249.
    const int selectNext = m_selectIssueOnReload;
    m_selectIssueOnReload = -1;
    const int keep = selectNext > 0 ? selectNext : m_currentIssueNumber;
    // Remember whether the viewed issue's detail pane is open so that, if a close
    // drops it out of the filtered list, we can keep the pane open on that same
    // (now-closed) issue instead of collapsing to the full-width list (issue #188).
    const bool keepCurrent = m_keepCurrentOnReload;
    m_keepCurrentOnReload = false;
    const bool detailWasOpen = m_issueDetail && m_issueDetail->isVisible();

    // Disable sorting while inserting so rows aren't reordered mid-build.
    // Block signals during the full rebuild so that setRowCount(0),
    // insertRow, and setSortingEnabled(true) never fire itemSelectionChanged
    // and accidentally navigate to a different issue (issue #188).
    TableRepaintGuard repaintGuard(m_issueTable);
    m_issueTable->blockSignals(true);
    m_issueTable->setSortingEnabled(false);
    m_issueTable->setRowCount(0);
    for (const Issue &issue : m_currentIssues) {
        if (statusFilter == "Open" && issue.status != "open")
            continue;
        if (statusFilter == "Closed" && issue.status != "closed")
            continue;
        if (!labelFilter.isEmpty() && !issue.labels.contains(labelFilter))
            continue;
        if (!msFilter.isEmpty() && issue.milestone != msFilter)
            continue;
        // Free-text search over number, title, priority, labels and milestone.
        if (!search.isEmpty()) {
            const QString hay = QStringLiteral("#%1 %2 %3 %4 %5")
                                    .arg(issue.number)
                                    .arg(issue.title)
                                    .arg(issue.priority)
                                    .arg(issue.labels.join(" "), issue.milestone);
            if (!hay.contains(search, Qt::CaseInsensitive))
                continue;
        }

        const int row = m_issueTable->rowCount();
        m_issueTable->insertRow(row);

        auto *numItem = new QTableWidgetItem;
        // An int in DisplayRole both renders the number and sorts numerically.
        numItem->setData(Qt::DisplayRole, issue.number);
        numItem->setData(Qt::UserRole, issue.number); // lookup key
        m_issueTable->setItem(row, 0, numItem);
        m_issueTable->setItem(row, 1, new QTableWidgetItem(issue.title));

        auto *priority = new SortTableWidgetItem(
            issue.priority > 0 ? QString::number(issue.priority)
                               : QString::fromUtf8("\xE2\x80\x94"));
        // Unset priorities sort after 99 without pretending to be priority 100.
        priority->setData(kTableSortRole,
                          issue.priority > 0 ? issue.priority : 100);
        priority->setTextAlignment(Qt::AlignCenter);
        priority->setToolTip("1 is highest priority; 99 is lowest");
        m_issueTable->setItem(row, 2, priority);

        auto *status = new QTableWidgetItem(issue.status == "closed" ? "Closed"
                                                                     : "Open");
        status->setForeground(QColor(issue.status == "closed" ? "#f85149"
                                                              : "#3fb950"));
        m_issueTable->setItem(row, 3, status);
        auto *votes = new QTableWidgetItem;
        votes->setData(Qt::DisplayRole, issue.votes); // numeric sort
        votes->setTextAlignment(Qt::AlignCenter);
        m_issueTable->setItem(row, 4, votes);
        m_issueTable->setItem(row, 5, new QTableWidgetItem(issue.labels.join(", ")));
        m_issueTable->setItem(row, 6, new QTableWidgetItem(issue.milestone));
        // Created date: ISO yyyy-MM-dd sorts chronologically as plain text; the
        // tooltip carries the friendly "x ago" form.
        auto *created = new QTableWidgetItem(
            issue.createdAt > 0
                ? QDateTime::fromMSecsSinceEpoch(issue.createdAt).toString("yyyy-MM-dd")
                : QString());
        created->setToolTip(formatIssueRelativeTime(issue.createdAt));
        m_issueTable->setItem(row, 7, created);

        // Updated date: the most recent activity on the issue (latest signed
        // event, falling back to the created time). ISO yyyy-MM-dd sorts
        // chronologically as plain text; the tooltip carries the "x ago" form.
        qint64 updatedAt = issue.createdAt;
        for (const IssueEvent &ev : issue.events)
            updatedAt = qMax(updatedAt, ev.ts);
        auto *updated = new QTableWidgetItem(
            updatedAt > 0
                ? QDateTime::fromMSecsSinceEpoch(updatedAt).toString("yyyy-MM-dd")
                : QString());
        updated->setToolTip(formatIssueRelativeTime(updatedAt));
        m_issueTable->setItem(row, 8, updated);

        if (const AgentSession *session = latestAgentSessionForIssue(issue.number)) {
            // Provider name, prefixed with a spinner frame while the agent is
            // still working so the list shows live activity at a glance.
            QString text = agentProviderName(session->provider);
            if (agentSessionActive(session))
                text = QString::fromUtf8(kAgentSpinFrames[m_issueSpinFrame % 10]) +
                       QStringLiteral(" ") + text;
            auto *agentItem = new QTableWidgetItem(text);
            agentItem->setData(Qt::UserRole, session->id);
            m_issueTable->setItem(row, 9, agentItem);
        } else {
            m_issueTable->setItem(row, 9, new QTableWidgetItem(QString()));
        }
        // Author: the node that opened the issue. For mirror-authored issues
        // this is the submitting node, preserved through the inbox merge.
        const QString author =
            issue.authorName.trimmed().isEmpty()
                ? (issue.author.isEmpty() ? QString::fromUtf8("\xE2\x80\x94")
                                          : issue.author.left(8))
                : issue.authorName.trimmed();
        auto *authorItem = new QTableWidgetItem(author);
        authorItem->setToolTip(issue.author);
        m_issueTable->setItem(row, 10, authorItem);

        // Progress: percent complete. Drawn as a mini bar by ProgressBarDelegate
        // (value via kProgressBarRole); display text stays empty. Still sorts
        // numerically via kTableSortRole.
        const int pct = qBound(0, issue.progress, 100);
        auto *progressItem = new SortTableWidgetItem(QString());
        progressItem->setData(kTableSortRole, pct);
        progressItem->setData(kProgressBarRole, pct);
        progressItem->setToolTip(QStringLiteral("%1% complete").arg(pct));
        m_issueTable->setItem(row, 11, progressItem);

        // Estimated OpenAI cost to implement, sorted numerically.
        const double estUsd = openAiEstimateUsd(issue);
        auto *estItem = new SortTableWidgetItem(
            QStringLiteral("$%1").arg(QString::number(estUsd, 'f', 2)));
        estItem->setData(kTableSortRole, estUsd);
        estItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_issueTable->setItem(row, 12, estItem);

        // Bounty pledged on the issue (em dash + sorts first when none).
        auto *bountyItem = new SortTableWidgetItem(
            issue.bountyUsd > 0
                ? QStringLiteral("$%1").arg(QString::number(issue.bountyUsd, 'f', 2))
                : QString::fromUtf8("\xE2\x80\x94"));
        bountyItem->setData(kTableSortRole, issue.bountyUsd);
        bountyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (!issue.bountyStatus.isEmpty())
            bountyItem->setToolTip(
                QStringLiteral("Bounty status: %1").arg(issue.bountyStatus));
        m_issueTable->setItem(row, 13, bountyItem);

        // Comment count: "comment" events minus any that were later deleted,
        // matching what the detail thread renders. Also track the most recent
        // commenter so the column shows "N \xC2\xB7 author" at a glance.
        QSet<QString> deletedComments;
        for (const IssueEvent &ev : issue.events) {
            if (ev.type == "delete" && !ev.target.isEmpty() && ev.target != "self")
                deletedComments.insert(ev.target);
        }
        int commentCount = 0;
        qint64 latestCommentTs = -1;
        QString latestCommenter;
        for (const IssueEvent &ev : issue.events) {
            if (ev.type != "comment" || deletedComments.contains(ev.id))
                continue;
            ++commentCount;
            if (ev.ts >= latestCommentTs) {
                latestCommentTs = ev.ts;
                latestCommenter = ev.authorName.trimmed().isEmpty()
                                      ? ev.author.left(8)
                                      : ev.authorName.trimmed();
            }
        }
        // "N \xC2\xB7 author"; the count still sorts numerically via kTableSortRole.
        auto *commentsItem = new SortTableWidgetItem(
            commentCount > 0 && !latestCommenter.isEmpty()
                ? QString::fromUtf8("%1 \xC2\xB7 %2")
                      .arg(commentCount)
                      .arg(latestCommenter)
                : QString::number(commentCount));
        commentsItem->setData(kTableSortRole, commentCount);
        if (!latestCommenter.isEmpty())
            commentsItem->setToolTip(
                QStringLiteral("Latest comment by %1").arg(latestCommenter));
        commentsItem->setTextAlignment(Qt::AlignCenter);
        m_issueTable->setItem(row, 14, commentsItem);

        // Files: an indicator + changed-file count for issues whose work lives in
        // a linked agent worktree branch or pull request (adhoc #151).
        populateIssueFilesCell(row, issue);

        // Assignee(s): who has claimed the work (em dash when unassigned).
        m_issueTable->setItem(
            row, 16,
            new QTableWidgetItem(issue.assignees.isEmpty()
                                     ? QString::fromUtf8("\xE2\x80\x94")
                                     : issue.assignees.join(QStringLiteral(", "))));
    }
    m_issueTable->setSortingEnabled(true);
    m_issueTable->blockSignals(false);

    // Re-select the kept issue (row order may differ after sorting).
    int selRow = -1;
    for (int r = 0; r < m_issueTable->rowCount(); ++r) {
        if (m_issueTable->item(r, 0)->data(Qt::UserRole).toInt() == keep) {
            selRow = r;
            break;
        }
    }
    if (selRow >= 0) {
        // Re-select the issue the user was already viewing (fires
        // itemSelectionChanged -> showIssue).
        m_issueTable->selectRow(selRow);
    } else if (keepCurrent && detailWasOpen && keep > 0) {
        // The viewed issue just dropped out of the filtered list (e.g. closed
        // while filtering to Open). Stay put: keep the detail panel open on that
        // same issue rather than jumping to another row or collapsing to the
        // full-width list. loadAll() ignores the filter, so the issue is still in
        // m_currentIssues — re-render its thread with no table row selected.
        bool stillThere = false;
        for (const Issue &issue : m_currentIssues) {
            if (issue.number == keep) {
                m_currentIssueNumber = keep;
                renderIssueThread(issue);
                updateIssueActionState();
                stillThere = true;
                break;
            }
        }
        if (!stillThere) {
            // The issue genuinely vanished (e.g. deleted) — fall back to the
            // collapsed list below.
            m_issueTable->clearSelection();
            m_currentIssueNumber = -1;
            if (m_issueDetail && m_issueDetail->isVisible()) {
                m_issueDetail->hide();
                if (m_issueDetailToggle)
                    m_issueDetailToggle->setText("Show detail");
            }
            renderIssueThread(Issue());
            updateIssueActionState();
        }
    } else {
        // First load (or the viewed issue is gone): show the table full width
        // with no row selected; the detail panel stays hidden until a click.
        m_issueTable->clearSelection();
        m_currentIssueNumber = -1;
        if (m_issueDetail && m_issueDetail->isVisible()) {
            m_issueDetail->hide();
            if (m_issueDetailToggle)
                m_issueDetailToggle->setText("Show detail");
        }
        renderIssueThread(Issue());
        updateIssueActionState();
    }

    // Animate the per-row Agent spinner only while something is actually working.
    bool anyActive = false;
    for (const Issue &issue : m_currentIssues) {
        if (agentSessionActive(latestAgentSessionForIssue(issue.number))) {
            anyActive = true;
            break;
        }
    }
    if (anyActive) {
        if (!m_issueSpinTimer) {
            m_issueSpinTimer = new QTimer(this);
            connect(m_issueSpinTimer, &QTimer::timeout, this,
                    &MainWindow::tickIssueListSpinners);
        }
        if (!m_issueSpinTimer->isActive())
            m_issueSpinTimer->start(110);
    } else if (m_issueSpinTimer) {
        m_issueSpinTimer->stop();
    }

    // Keep the Kanban board in sync with the same data + filters whenever it's the
    // visible list view (cheap to skip rebuilding it while hidden).
    if (m_issueListStack && m_issueListStack->currentIndex() == 3)
        refreshIssueBoard();
}

// Advance the Agent-column spinner one frame for every issue row whose agent is
// still working. Updates cell text in place (no full rebuild) so it stays cheap.
void MainWindow::tickIssueListSpinners()
{
    if (!m_issueTable)
        return;
    m_issueSpinFrame = (m_issueSpinFrame + 1) % 10;
    const QString frame = QString::fromUtf8(kAgentSpinFrames[m_issueSpinFrame]);
    bool anyActive = false;
    for (int r = 0; r < m_issueTable->rowCount(); ++r) {
        QTableWidgetItem *numItem = m_issueTable->item(r, 0);
        QTableWidgetItem *cell = m_issueTable->item(r, 9);
        if (!numItem || !cell)
            continue;
        const AgentSession *session =
            latestAgentSessionForIssue(numItem->data(Qt::UserRole).toInt());
        if (!agentSessionActive(session))
            continue;
        anyActive = true;
        cell->setText(frame + QStringLiteral(" ") +
                      agentProviderName(session->provider));
    }
    if (!anyActive && m_issueSpinTimer)
        m_issueSpinTimer->stop();
}

void MainWindow::refreshIssueMilestones()
{
    if (!m_issueMilestonesTable)
        return;

    struct Counts {
        int open = 0;
        int closed = 0;
    };
    QHash<QString, Counts> counts;
    QHash<QString, IssueMilestone> defs;
    QStringList order;
    for (const IssueMilestone &ms : std::as_const(m_currentMilestones)) {
        if (ms.title.trimmed().isEmpty())
            continue;
        defs.insert(ms.title, ms);
        if (!order.contains(ms.title))
            order << ms.title;
    }
    for (const Issue &issue : std::as_const(m_currentIssues)) {
        if (issue.milestone.trimmed().isEmpty())
            continue;
        if (!order.contains(issue.milestone))
            order << issue.milestone;
        Counts &c = counts[issue.milestone];
        if (issue.status == "closed")
            ++c.closed;
        else
            ++c.open;
    }

    TableRepaintGuard repaintGuard(m_issueMilestonesTable);
    m_issueMilestonesTable->setSortingEnabled(false);
    m_issueMilestonesTable->setRowCount(0);
    for (const QString &title : std::as_const(order)) {
        const Counts c = counts.value(title);
        const int total = c.open + c.closed;
        const int pct = total > 0 ? (c.closed * 100) / total : 0;
        const IssueMilestone ms = defs.value(title);

        const int row = m_issueMilestonesTable->rowCount();
        m_issueMilestonesTable->insertRow(row);
        auto *titleItem = new QTableWidgetItem(title);
        titleItem->setIcon(themedOcticon("graph", QColor("#8b949e"), 14));
        m_issueMilestonesTable->setItem(row, 0, titleItem);
        auto addNumber = [&](int column, int value, const QString &which) {
            auto *item = new QTableWidgetItem;
            item->setData(Qt::DisplayRole, value);
            item->setTextAlignment(Qt::AlignCenter);
            // Open/Closed counts act as links into the filtered issue list.
            item->setForeground(QColor("#388bfd"));
            item->setToolTip(
                QStringLiteral("Show %1 %2 issues").arg(which, title));
            m_issueMilestonesTable->setItem(row, column, item);
        };
        addNumber(1, c.open, QStringLiteral("open"));
        addNumber(2, c.closed, QStringLiteral("closed"));

        auto *progressItem = new QTableWidgetItem(QStringLiteral("%1%").arg(pct));
        progressItem->setData(kTableSortRole, pct);
        m_issueMilestonesTable->setItem(row, 3, progressItem);
        auto *progress = new QProgressBar;
        progress->setRange(0, 100);
        progress->setValue(pct);
        progress->setTextVisible(true);
        progress->setFormat(QStringLiteral("%p%"));
        // Style to match the mini-bar delegate: a muted, clearly-visible track
        // with a rounded green (complete) or blue (in progress) fill, instead of
        // the default groove that reads as a solid black "already done" bar.
        const bool dark = currentThemeIsDark();
        const QString track = dark ? QStringLiteral("#30363d")
                                   : QStringLiteral("#d0d7de");
        const QString chunk = pct >= 100 ? QStringLiteral("#3fb950")
                                         : QStringLiteral("#388bfd");
        const QString txt = dark ? QStringLiteral("#e6edf3")
                                 : QStringLiteral("#1f2328");
        progress->setStyleSheet(
            QStringLiteral("QProgressBar {"
                           "  border: none;"
                           "  border-radius: 6px;"
                           "  background-color: %1;"
                           "  color: %2;"
                           "  text-align: center;"
                           "  font-size: 11px;"
                           "  min-height: 14px;"
                           "  max-height: 16px;"
                           "}"
                           "QProgressBar::chunk {"
                           "  border-radius: 6px;"
                           "  background-color: %3;"
                           "}")
                .arg(track, txt, chunk));
        m_issueMilestonesTable->setCellWidget(row, 3, progress);

        m_issueMilestonesTable->setItem(
            row, 4,
            new QTableWidgetItem(
                ms.due > 0 ? QDateTime::fromMSecsSinceEpoch(ms.due).toString("yyyy-MM-dd")
                           : QString()));
        m_issueMilestonesTable->setItem(
            row, 5,
            new QTableWidgetItem(ms.status.trimmed().isEmpty() ? QStringLiteral("open")
                                                               : ms.status));
    }
    m_issueMilestonesTable->setSortingEnabled(true);
}

void MainWindow::refreshIssueLabels()
{
    if (!m_issueLabelsTable)
        return;

    struct Counts {
        int open = 0;
        int closed = 0;
    };
    QHash<QString, Counts> counts;
    QHash<QString, QString> colors;
    QStringList order;
    for (const IssueLabel &label : std::as_const(m_currentLabels)) {
        if (label.name.trimmed().isEmpty())
            continue;
        colors.insert(label.name, label.color);
        if (!order.contains(label.name))
            order << label.name;
    }
    for (const Issue &issue : std::as_const(m_currentIssues)) {
        for (const QString &name : issue.labels) {
            if (name.trimmed().isEmpty())
                continue;
            if (!order.contains(name))
                order << name;
            Counts &c = counts[name];
            if (issue.status == "closed")
                ++c.closed;
            else
                ++c.open;
        }
    }

    const bool writable = issueStoreForCurrentRepo().canWrite();
    TableRepaintGuard repaintGuard(m_issueLabelsTable);
    m_issueLabelsTable->setSortingEnabled(false);
    m_issueLabelsTable->setRowCount(0);
    for (const QString &name : std::as_const(order)) {
        const Counts c = counts.value(name);
        const QString color = colors.value(name, QStringLiteral("#94a3b8"));
        const int row = m_issueLabelsTable->rowCount();
        m_issueLabelsTable->insertRow(row);

        auto *labelItem = new QTableWidgetItem(name);
        labelItem->setData(Qt::UserRole, name);
        labelItem->setIcon(themedOcticon("tag", QColor(color), 14));
        m_issueLabelsTable->setItem(row, 0, labelItem);
        auto addNumber = [&](int column, int value) {
            auto *item = new QTableWidgetItem;
            item->setData(Qt::DisplayRole, value);
            item->setTextAlignment(Qt::AlignCenter);
            m_issueLabelsTable->setItem(row, column, item);
        };
        addNumber(1, c.open);
        addNumber(2, c.closed);
        addNumber(3, c.open + c.closed);

        auto *edit = new QPushButton("Edit");
        edit->setObjectName("ghostButton");
        edit->setProperty("buttonSize", "sm");
        edit->setEnabled(writable);
        edit->setCursor(Qt::PointingHandCursor);
        setOcticon(edit, "pencil", 14);
        connect(edit, &QPushButton::clicked, this, [this, name] {
            for (int r = 0; r < m_issueLabelsTable->rowCount(); ++r) {
                QTableWidgetItem *current = m_issueLabelsTable->item(r, 0);
                if (current && current->data(Qt::UserRole).toString() == name) {
                    editIssueLabelDefinition(r);
                    return;
                }
            }
        });
        m_issueLabelsTable->setCellWidget(row, 4, edit);
    }
    m_issueLabelsTable->setSortingEnabled(true);
}

void MainWindow::editIssueLabelDefinition(int row)
{
    if (!m_issueLabelsTable || row < 0 || row >= m_issueLabelsTable->rowCount())
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        setIssueInlineNotice("Labels are editable on the repository host.", true);
        return;
    }
    QTableWidgetItem *item = m_issueLabelsTable->item(row, 0);
    if (!item)
        return;
    const QString oldName = item->data(Qt::UserRole).toString();
    QString oldColor = QStringLiteral("#94a3b8");
    for (const IssueLabel &label : std::as_const(m_currentLabels))
        if (label.name == oldName && !label.color.trimmed().isEmpty())
            oldColor = label.color.trimmed();

    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, "Edit label", "Label name:", QLineEdit::Normal,
                              oldName, &ok)
            .trimmed();
    if (!ok)
        return;
    if (newName.isEmpty()) {
        setIssueInlineNotice("A label name is required.", true);
        return;
    }
    const QString newColor =
        QInputDialog::getText(this, "Edit label", "Color (#RRGGBB):",
                              QLineEdit::Normal, oldColor, &ok)
            .trimmed();
    if (!ok)
        return;
    if (!QColor(newColor).isValid()) {
        setIssueInlineNotice("Use a valid label color, for example #3fb950.", true);
        return;
    }

    QList<IssueLabel> labels = m_currentLabels;
    bool updated = false;
    for (IssueLabel &label : labels) {
        if (label.name == oldName) {
            label.name = newName;
            label.color = newColor;
            updated = true;
        } else if (label.name == newName) {
            setIssueInlineNotice("That label already exists.", true);
            return;
        }
    }
    if (!updated)
        labels.append({newName, newColor});

    QString error;
    if (newName != oldName) {
        for (const Issue &issue : std::as_const(m_currentIssues)) {
            if (!issue.labels.contains(oldName))
                continue;
            QStringList issueLabels = issue.labels;
            issueLabels.replaceInStrings(QRegularExpression(
                                             QStringLiteral("^%1$")
                                                 .arg(QRegularExpression::escape(oldName))),
                                         newName);
            issueLabels.removeDuplicates();
            if (!store.setLabels(issue.number, issueLabels, &error)) {
                setIssueInlineNotice(error.isEmpty() ? "Could not rename label." : error,
                                     true);
                return;
            }
        }
    }
    if (!store.saveLabels(labels, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not save labels." : error, true);
        return;
    }
    setIssueInlineNotice("Label updated.");
    reloadIssues();
}

void MainWindow::showIssue(int number)
{
    for (const Issue &issue : m_currentIssues) {
        if (issue.number == number) {
            removeIssueComposePage();
            m_issueDeleteConfirmPending = false;
            m_currentIssueNumber = number;
            if (m_issueDetail && !m_issueDetail->isVisible()) {
                m_issueDetail->show();
                if (m_issueDetailToggle)
                    m_issueDetailToggle->setText("Hide detail");
            }
            renderIssueThread(issue);
            updateIssueActionState();
            return;
        }
    }
}

void MainWindow::renderIssueThread(const Issue &issue)
{
    // The transient "AI is answering…" card lives in this layout, so it's about
    // to be deleted below — drop our reference and stop its animation first.
    if (m_issueAiTypingTimer)
        m_issueAiTypingTimer->stop();
    m_issueAiTypingTimer = nullptr;
    m_issueAiTypingRow = nullptr;

    // Clear all cards (keep the trailing stretch rebuilt at the end).
    while (QLayoutItem *item = m_issueThreadLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    if (issue.number == 0) {
        m_issueTitle->setText("Select an issue");
        cancelIssueTitleEdit();
        m_issueMeta->clear();
        m_issueMeta->hide();
        if (m_issueAssigneesValue)
            m_issueAssigneesValue->setText("No one - <a href='#'>Assign yourself</a>");
        if (m_issueLabelsValue)
            m_issueLabelsValue->setText("No labels");
        if (m_issueMilestoneValue)
            m_issueMilestoneValue->setText("No milestone");
        if (m_issuePriorityValue)
            m_issuePriorityValue->setText("No priority");
        updateIssueAgentUi(Issue());
        refreshIssueFilesPanel(Issue());
        cancelIssueSidebarEditors();
        m_issueThreadLayout->addStretch();
        return;
    }

    m_issueTitle->setText(
        QStringLiteral("%1 <span style='color:#656d76;font-weight:400'>#%2</span>")
            .arg(issue.title.toHtmlEscaped())
            .arg(issue.number));
    if (m_issueTitleEditor)
        m_issueTitleEditor->setText(issue.title);

    // Status pill: rounded corners come from QSS (QLabel rich text can't render
    // border-radius), switched by the dynamic "status" property.
    const bool issueClosed = issue.status == "closed";
    m_issueMeta->show();
    m_issueMeta->setText(issueClosed ? "Closed" : "Open");
    m_issueMeta->setProperty("status", issueClosed ? "closed" : "open");
    m_issueMeta->style()->unpolish(m_issueMeta);
    m_issueMeta->style()->polish(m_issueMeta);

    auto colorFor = [this](const QString &name) -> QString {
        for (const IssueLabel &l : m_currentLabels)
            if (l.name == name && !l.color.isEmpty())
                return l.color;
        return QStringLiteral("#94a3b8");
    };
    if (issue.assignees.isEmpty()) {
        m_issueAssigneesValue->setText("No one - <a href='#'>Assign yourself</a>");
    } else {
        QStringList shown;
        for (const QString &a : issue.assignees)
            shown << a.left(16).toHtmlEscaped();
        m_issueAssigneesValue->setText(shown.join("<br>"));
    }
    if (issue.labels.isEmpty()) {
        m_issueLabelsValue->setText("No labels");
    } else {
        QStringList chips;
        for (const QString &name : issue.labels)
            chips << QString::fromUtf8("<span style='color:%1'>\xE2\x97\x8F %2</span>")
                         .arg(colorFor(name), name.toHtmlEscaped());
        m_issueLabelsValue->setText(chips.join("<br>"));
    }
    m_issueMilestoneValue->setText(
        issue.milestone.isEmpty()
            ? QStringLiteral("No milestone")
            : QStringLiteral("<b>%1</b>").arg(issue.milestone.toHtmlEscaped()));
    m_issuePriorityValue->setText(
        issue.priority > 0
            ? QStringLiteral("<b>%1</b> <span style='color:#8b949e'>(1 highest, 99 lowest)</span>")
                  .arg(issue.priority)
            : QStringLiteral("No priority"));
    if (m_issueProgressSlider)
        static_cast<ProgressSlider *>(m_issueProgressSlider)
            ->setValue(qBound(0, issue.progress, 100));
    if (m_issueEstimateValue) {
        m_issueEstimateValue->setText(
            QStringLiteral("~$%1 <span style='color:#8b949e'>(OpenAI to implement)</span>")
                .arg(QString::number(openAiEstimateUsd(issue), 'f', 2)));
    }
    if (m_issueBountyValue) {
        if (issue.bountyUsd > 0) {
            const QString status = issue.bountyStatus.isEmpty()
                                       ? QStringLiteral("open")
                                       : issue.bountyStatus;
            m_issueBountyValue->setText(
                QStringLiteral("<b>$%1</b> <span style='color:#8b949e'>(%2)</span>")
                    .arg(QString::number(issue.bountyUsd, 'f', 2), status.toHtmlEscaped()));
        } else {
            m_issueBountyValue->setText(QStringLiteral("No bounty"));
        }
    }
    updateIssueAgentUi(issue);
    refreshIssueFilesPanel(issue);
    if (m_issueDevelopmentValue) {
        const QList<int> pulls = pullsLinkedToIssue(issue.number);
        if (pulls.isEmpty()) {
            m_issueDevelopmentValue->setText("No linked pull requests.");
        } else {
            QStringList links;
            for (const int n : pulls) {
                // Append the linked PR's state (Open/Merged/Closed) when the PR is
                // loaded, colour-matched to the pull request detail header.
                QString badge;
                for (const PullRequest &pr : m_currentPulls) {
                    if (pr.number != n)
                        continue;
                    const QString label =
                        pr.status == QLatin1String("merged")  ? QStringLiteral("Merged")
                        : pr.status == QLatin1String("closed") ? QStringLiteral("Closed")
                                                               : QStringLiteral("Open");
                    const QString color =
                        pr.status == QLatin1String("merged")  ? QStringLiteral("#a371f7")
                        : pr.status == QLatin1String("closed") ? QStringLiteral("#f85149")
                                                               : QStringLiteral("#3fb950");
                    badge = QStringLiteral(
                                " <span style='color:%1'>%2</span>").arg(color, label);
                    break;
                }
                links << QStringLiteral(
                             "<a href='pull:%1' style='color:#58a6ff;"
                             "text-decoration:none'>pull request #%1</a>%2")
                             .arg(QString::number(n), badge);
            }
            m_issueDevelopmentValue->setText(links.join("<br>"));
        }
    }
    if (m_issueAssigneesEdit)
        m_issueAssigneesEdit->setText(issue.assignees.join(", "));
    if (m_issueLabelsEdit)
        m_issueLabelsEdit->setText(issue.labels.join(", "));
    if (m_issueMilestoneEdit) {
        QSignalBlocker blocker(m_issueMilestoneEdit);
        m_issueMilestoneEdit->clear();
        m_issueMilestoneEdit->addItem("No milestone", QString());
        for (const IssueMilestone &ms : m_currentMilestones)
            m_issueMilestoneEdit->addItem(ms.title, ms.title);
        const int selected = m_issueMilestoneEdit->findData(issue.milestone);
        if (selected >= 0)
            m_issueMilestoneEdit->setCurrentIndex(selected);
    }
    if (m_issuePriorityEdit) {
        const int selected = m_issuePriorityEdit->findData(issue.priority);
        if (selected >= 0)
            m_issuePriorityEdit->setCurrentIndex(selected);
    }
    cancelIssueSidebarEditors();

    // Pre-compute edits (target -> latest edit) and deletions.
    QHash<QString, IssueEvent> edits;
    QSet<QString> deleted;
    for (const IssueEvent &ev : issue.events) {
        if (ev.type == "edit" && !ev.target.isEmpty())
            edits.insert(ev.target, ev); // later edits overwrite
        else if (ev.type == "delete" && !ev.target.isEmpty() && ev.target != "self")
            deleted.insert(ev.target);
    }

    const int idx = issuesRepoIndex();
    const QString imageBase =
        idx >= 0 ? m_repositories.at(idx).localPath + "/issues/" +
                       QString::number(issue.number) + "/"
                 : QString();
    const bool haveLocalFiles = !imageBase.isEmpty() &&
                                QFileInfo::exists(imageBase + "issue.md");
    const bool writable = issueStoreForCurrentRepo().canWrite();

    auto addCard = [&](const IssueEvent &ev, bool isOpen) {
        IssueEvent shown = ev;
        if (edits.contains(ev.id)) {
            shown.body = edits.value(ev.id).body;
            shown.attachments = edits.value(ev.id).attachments;
        }
        const int num = issue.number;
        const QString eid = ev.id;
        const QString eventBody = shown.body;
        const QStringList eventAttachments = shown.attachments;
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        const QString when = formatIssueRelativeTime(ev.ts);

        auto *row = new QWidget;
        row->setObjectName("issueTimelineRow");
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(14);
        auto *avatar = new QLabel(who.left(2).toUpper());
        avatar->setObjectName("issueAvatar");
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setFixedSize(36, 36);
        // Show the author's real avatar instead of the initials tile when we
        // have a picture. Peer avatars are broadcast over chat and cached in
        // m_avatars keyed by node id, which is the same Ed25519 pubkey that
        // signs issue events (ev.author), so the keys line up. Our own avatar
        // may not be in that cache yet (it only lands there once the backend
        // has broadcast it this run), so fall back to effectiveAvatar() for our
        // own events — that keeps an author's description card consistent with
        // the composer below, which always shows effectiveAvatar(). Peers we've
        // never seen a picture from keep the initials tile (set above).
        QPixmap authorAvatar;
        const QPixmap cached = m_avatars.value(ev.author);
        if (!cached.isNull())
            authorAvatar = roundedRectPixmap(cached, 36, 36 * 0.28);
        else if (ev.author == m_profileIdentity.publicKey())
            authorAvatar = roundedAvatar(effectiveAvatar(), 36);
        if (!authorAvatar.isNull()) {
            avatar->setText(QString());
            avatar->setPixmap(authorAvatar);
        }
        rowLayout->addWidget(avatar, 0, Qt::AlignTop);

        auto *card = new QWidget;
        card->setObjectName("issueTimelineCard");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(0);
        auto *headerBox = new QWidget(card);
        headerBox->setObjectName("issueTimelineHeader");
        auto *headerRow = new QHBoxLayout(headerBox);
        headerRow->setContentsMargins(16, 8, 10, 8);
        headerRow->setSpacing(8);
        auto *header = new QLabel(
            QStringLiteral("<b>%1</b> <span>%2 %3</span>")
                .arg(who.toHtmlEscaped(), isOpen ? "opened" : "commented", when));
        header->setTextFormat(Qt::RichText);
        headerRow->addWidget(header);
        headerRow->addStretch();

        auto *bodyContainer = new ClickableIssueBody(card);
        auto *bodyLayout = new QVBoxLayout(bodyContainer);
        bodyLayout->setContentsMargins(16, 16, 16, 16);
        bodyLayout->setSpacing(10);

        auto clearBody = [bodyLayout]() {
            while (QLayoutItem *item = bodyLayout->takeAt(0)) {
                if (QWidget *w = item->widget()) {
                    w->hide();
                    w->deleteLater();
                }
                delete item;
            }
        };
        auto renderBody = [=]() {
            clearBody();
            auto *body = new QLabel;
            body->setTextFormat(Qt::MarkdownText);
            body->setText(autolinkReferences(eventBody));
            body->setWordWrap(true);
            body->setTextInteractionFlags(Qt::TextBrowserInteraction);
            // Reference links (#N, commit SHAs, forkmesh:// permalinks) resolve in
            // app; real external links fall through to the system browser.
            body->setOpenExternalLinks(false);
            connect(body, &QLabel::linkActivated, this,
                    [this](const QString &href) { openBodyReference(href); });
            const bool emptyEditableDescription =
                writable && isOpen && eventBody.trimmed().isEmpty();
            if (emptyEditableDescription) {
                body->setText("Click to add a description.");
                body->setObjectName("statusLine");
                body->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                bodyContainer->setCursor(Qt::PointingHandCursor);
                bodyContainer->setMinimumHeight(52);
            } else {
                bodyContainer->unsetCursor();
                bodyContainer->setMinimumHeight(0);
            }
            bodyLayout->addWidget(body);
            for (const QString &rel : eventAttachments) {
                if (haveLocalFiles) {
                    QPixmap pix(imageBase + rel);
                    if (!pix.isNull()) {
                        auto *img = new QLabel;
                        img->setObjectName("issueAttachmentPreview");
                        img->setPixmap(pix.width() > 640
                                           ? pix.scaledToWidth(640, Qt::SmoothTransformation)
                                           : pix);
                        bodyLayout->addWidget(img);
                        continue;
                    }
                }
                auto *placeholder =
                    new QLabel(QStringLiteral("Image: %1").arg(rel));
                placeholder->setObjectName("statusLine");
                bodyLayout->addWidget(placeholder);
            }
        };
        auto showBodyEditor = [=]() {
            clearBody();
            bodyContainer->onClicked = nullptr;
            bodyContainer->unsetCursor();
            bodyContainer->setMinimumHeight(0);
            auto *editor = new MarkdownEditor(bodyContainer);
            editor->setMarkdown(eventBody);
            editor->setMentionCandidates(mentionCandidateNames());
            editor->setMinimumHeight(250);
            editor->setPlaceholderText(isOpen ? "Type your description here..."
                                              : "Type your comment here...");
            const int repoIdx = issuesRepoIndex();
            if (repoIdx >= 0)
                editor->setPreviewBasePath(m_repositories.at(repoIdx).localPath +
                                           "/issues/" + QString::number(num));
            bodyLayout->addWidget(editor);
            auto *attach = new QPushButton("Paste, drop, or click to add files");
            attach->setObjectName("ghostButton");
            attach->setCursor(Qt::PointingHandCursor);
            setOcticon(attach, "paperclip", 16);
            connect(attach, &QPushButton::clicked, this, [editor]() {
                const QStringList files = QFileDialog::getOpenFileNames(
                    editor, "Attach images", QString(),
                    "Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg);;All files (*)");
                for (const QString &file : files)
                    editor->addImageFile(file);
            });
            bodyLayout->addWidget(attach, 0, Qt::AlignLeft);
            auto *buttonRow = new QHBoxLayout;
            buttonRow->setContentsMargins(0, 0, 0, 0);
            auto *cancel = new QPushButton("Cancel");
            cancel->setObjectName("ghostButton");
            cancel->setCursor(Qt::PointingHandCursor);
            auto *save = new QPushButton("Save");
            save->setObjectName("primaryButton");
            save->setCursor(Qt::PointingHandCursor);
            buttonRow->addStretch();
            buttonRow->addWidget(cancel);
            buttonRow->addWidget(save);
            bodyLayout->addLayout(buttonRow);
            connect(cancel, &QPushButton::clicked, this, [this, num]() { showIssue(num); });
            connect(save, &QPushButton::clicked, this,
                    [this, num, eid, eventAttachments, editor]() {
                        IssueStore store = issueStoreForCurrentRepo();
                        QString error;
                        if (!store.editEvent(num, eid, editor->markdown(),
                                             eventAttachments,
                                             editor->pendingAttachments(),
                                             editor->pendingAttachmentPlaceholders(),
                                             &error)) {
                            setIssueInlineNotice(
                                error.isEmpty() ? "Could not update the issue body."
                                                : error,
                                true);
                            return;
                        }
                        setIssueInlineNotice("Issue body updated.");
                        reloadIssues();
                    });
            editor->focusEditor();
        };
        // Only intercept clicks when the body is an empty, editable description
        // (click-to-add-a-description). For real comment text, leave onClicked
        // unset so clicks reach the label and the text stays selectable —
        // including word (double-click) and paragraph (triple-click) selection.
        if (writable && isOpen && eventBody.trimmed().isEmpty())
            bodyContainer->onClicked = [=]() { showBodyEditor(); };

        QMenu *menu = new QMenu(card);
        menu->setAttribute(Qt::WA_TranslucentBackground, false);
        menu->setAutoFillBackground(true);
        menu->setWindowOpacity(1.0);
        const bool darkMenu = currentThemeIsDark();
        menu->setStyleSheet(
            QStringLiteral(
                "QMenu { background-color:%1; color:%2; border:1px solid %3; "
                "border-radius:8px; padding:6px; }"
                "QMenu::item { background-color:%1; padding:7px 26px 7px 22px; "
                "border-radius:6px; }"
                "QMenu::item:selected { background-color:%4; }"
                "QMenu::separator { height:1px; background:%3; margin:6px 0; }")
                .arg(darkMenu ? "#161b22" : "#ffffff",
                     darkMenu ? "#e6edf3" : "#1f2328",
                     darkMenu ? "#30363d" : "#d0d7de",
                     darkMenu ? "#21262d" : "#f6f8fa"));
        QAction *copyLink = menu->addAction("Copy link");
        QAction *copyMarkdown = menu->addAction("Copy Markdown");
        QAction *quoteReply = menu->addAction("Quote reply");
        connect(copyLink, &QAction::triggered, this, [this, num, eid]() {
            QString owner = "repo";
            QString repo = "issue";
            const int repoIdx = issuesRepoIndex();
            if (repoIdx >= 0) {
                owner = m_repositories.at(repoIdx).owner;
                repo = m_repositories.at(repoIdx).name;
            }
            QApplication::clipboard()->setText(
                QStringLiteral("forkmesh://issue/%1/%2/%3#%4")
                    .arg(owner, repo)
                    .arg(num)
                    .arg(eid));
            setIssueInlineNotice("Issue link copied.");
        });
        connect(copyMarkdown, &QAction::triggered, this, [this, eventBody]() {
            QApplication::clipboard()->setText(eventBody);
            setIssueInlineNotice("Markdown copied.");
        });
        connect(quoteReply, &QAction::triggered, this, [this, eventBody]() {
            if (!m_issueComposer)
                return;
            QStringList quoted;
            for (const QString &line : eventBody.split('\n'))
                quoted << QStringLiteral("> %1").arg(line);
            QString text = m_issueComposer->markdown();
            if (!text.isEmpty() && !text.endsWith('\n'))
                text += '\n';
            text += quoted.join('\n') + "\n\n";
            m_issueComposer->setMarkdown(text);
            m_issueComposer->focusEditor();
            setIssueInlineNotice("Quoted into the comment box.");
        });
        if (writable) {
            auto *editButton = new QPushButton(headerBox);
            editButton->setObjectName("issueActionButton");
            editButton->setFixedSize(30, 30);
            editButton->setCursor(Qt::PointingHandCursor);
            editButton->setToolTip(isOpen ? "Edit description" : "Edit comment");
            setOcticon(editButton, "pencil", 15);
            connect(editButton, &QPushButton::clicked, this, showBodyEditor);
            headerRow->addWidget(editButton);
        }
        auto *actionsButton = new QToolButton(headerBox);
        actionsButton->setObjectName("issueActionButton");
        actionsButton->setText("...");
        actionsButton->setCursor(Qt::PointingHandCursor);
        actionsButton->setPopupMode(QToolButton::InstantPopup);
        actionsButton->setMenu(menu);
        headerRow->addWidget(actionsButton);

        cardLayout->addWidget(headerBox);
        renderBody();
        cardLayout->addWidget(bodyContainer);
        rowLayout->addWidget(card, 1);
        m_issueThreadLayout->addWidget(row);
    };

    auto addActivity = [&](const QString &text, qint64 ts, const QString &who) {
        const QString when = QDateTime::fromMSecsSinceEpoch(ts).toString("HH:mm");
        auto *line = new QLabel(QString::fromUtf8("\xC2\xB7 %1 %2 (%3)")
                                    .arg(who.toHtmlEscaped(), text, when));
        line->setObjectName("statusLine");
        line->setWordWrap(true);
        m_issueThreadLayout->addWidget(line);
    };

    for (const IssueEvent &ev : issue.events) {
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        if (ev.type == "open")
            addCard(ev, true);
        else if (ev.type == "comment") {
            if (!deleted.contains(ev.id))
                addCard(ev, false);
        } else if (ev.type == "status")
            addActivity(ev.status == "closed" ? "closed this" : "reopened this", ev.ts, who);
        else if (ev.type == "labels")
            addActivity("set labels: " + ev.labels.join(", "), ev.ts, who);
        else if (ev.type == "milestone")
            addActivity(ev.milestone.isEmpty() ? "cleared the milestone"
                                               : "set milestone: " + ev.milestone,
                        ev.ts, who);
        else if (ev.type == "priority")
            addActivity(ev.priority > 0
                            ? QStringLiteral("set priority: %1").arg(ev.priority)
                            : QStringLiteral("cleared the priority"),
                        ev.ts, who);
        else if (ev.type == "assignees")
            addActivity("set assignees: " + ev.assignees.join(", "), ev.ts, who);
        else if (ev.type == "agent") {
            QString text;
            if (ev.agentSessionId <= 0 || ev.agentStatus == AgentStatus::Cleared) {
                text = QStringLiteral("cleared the agent assignment");
            } else {
                text = QStringLiteral("assigned %1 session #%2")
                           .arg(agentProviderName(ev.agentProvider))
                           .arg(ev.agentSessionId);
                if (ev.agentCreatePr)
                    text += QStringLiteral(" with PR creation requested");
                if (!ev.agentStatus.isEmpty())
                    text += QStringLiteral(" (%1)").arg(agentStatusText(ev.agentStatus));
            }
            addActivity(text, ev.ts, who);
        }
    }
    m_issueThreadLayout->addStretch();
}

void MainWindow::showIssueBurnupChart()
{
    if (!m_issueDetailStack)
        return;

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);

    auto *title = new QLabel("Issue burn-up");
    title->setObjectName("issuePageTitle");
    auto *back = new QPushButton("Back to issue");
    back->setObjectName("ghostButton");
    back->setCursor(Qt::PointingHandCursor);
    setOcticon(back, "arrow-left", 16);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(back);
    layout->addLayout(titleRow);

    auto *description = new QLabel(
        "Open and closed totals reconstructed from issue creation and status "
        "events. List filters do not change the chart.");
    description->setObjectName("statusLine");
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *rangeGroup = new QButtonGroup(page);
    rangeGroup->setExclusive(true);
    auto *rangeRow = new QHBoxLayout;
    rangeRow->setContentsMargins(0, 0, 0, 0);
    rangeRow->setSpacing(4);
    const QStringList rangeLabels{
        QStringLiteral("Day"), QStringLiteral("Week"),
        QStringLiteral("2 weeks"), QStringLiteral("Month"),
        QStringLiteral("All time")};
    for (int i = 0; i < rangeLabels.size(); ++i) {
        auto *button = new QPushButton(rangeLabels.at(i));
        button->setObjectName("repoTab");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        rangeGroup->addButton(button, i);
        rangeRow->addWidget(button);
    }
    rangeRow->addStretch();
    layout->addLayout(rangeRow);

    auto *summary = new QLabel;
    summary->setTextFormat(Qt::RichText);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto *legend = new QLabel(
        "<span style='color:#58a6ff;font-weight:700'>\xE2\x97\x8F Open</span>"
        "&nbsp;&nbsp;&nbsp;"
        "<span style='color:#3fb950;font-weight:700'>\xE2\x97\x8F Closed</span>");
    legend->setTextFormat(Qt::RichText);
    layout->addWidget(legend);

    auto *chart = new IssueBurnupChart(page);
    layout->addWidget(chart, 1);

    auto refreshChart = [this, chart, summary](int range) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 start = now - 24LL * 60 * 60 * 1000;
        int intervals = 24;
        QString rangeName = QStringLiteral("past day");
        if (range == 1) {
            start = now - 7LL * 24 * 60 * 60 * 1000;
            intervals = 28;
            rangeName = QStringLiteral("past week");
        } else if (range == 2) {
            start = now - 14LL * 24 * 60 * 60 * 1000;
            intervals = 28;
            rangeName = QStringLiteral("past 2 weeks");
        } else if (range == 3) {
            start = now - 30LL * 24 * 60 * 60 * 1000;
            intervals = 30;
            rangeName = QStringLiteral("past month");
        } else if (range == 4) {
            start = firstIssueHistoryTimestamp(
                m_currentIssues, now - 24LL * 60 * 60 * 1000);
            if (start >= now)
                start = now - 24LL * 60 * 60 * 1000;
            intervals = 60;
            rangeName = QStringLiteral("all time");
        }

        if (m_currentIssues.isEmpty()) {
            chart->setSeries({});
            summary->setText(
                QStringLiteral("No issues are available for the %1 range.")
                    .arg(rangeName));
            return;
        }

        const QList<IssueBurnupPoint> series =
            buildIssueBurnupSeries(m_currentIssues, start, now, intervals);
        chart->setSeries(series);
        const IssueBurnupPoint &first = series.first();
        const IssueBurnupPoint &last = series.last();
        const int firstTotal = first.openCount + first.closedCount;
        const int lastTotal = last.openCount + last.closedCount;
        auto signedNumber = [](int value) {
            return value > 0 ? QStringLiteral("+%1").arg(value)
                             : QString::number(value);
        };
        summary->setText(
            QStringLiteral(
                "<span style='font-size:22px;font-weight:800'>%1</span> open"
                "&nbsp;&nbsp;&nbsp;"
                "<span style='font-size:22px;font-weight:800'>%2</span> closed"
                "&nbsp;&nbsp;&nbsp;"
                "<span style='color:#8b949e'>%3 total &middot; %4 total and %5 "
                "net closed over the %6</span>")
                .arg(last.openCount)
                .arg(last.closedCount)
                .arg(lastTotal)
                .arg(signedNumber(lastTotal - firstTotal))
                .arg(signedNumber(last.closedCount - first.closedCount),
                     rangeName));
    };

    connect(rangeGroup, &QButtonGroup::idClicked, this, refreshChart);
    connect(back, &QPushButton::clicked, this,
            &MainWindow::removeIssueComposePage);
    rangeGroup->button(1)->setChecked(true);
    refreshChart(1);
    showIssueComposePage(page);
}

void MainWindow::showIssueComposePage(QWidget *page)
{
    if (!m_issueDetailStack || !page)
        return;
    removeIssueComposePage();
    // Host the compose form in a scroll area so a short window scrolls instead of
    // clipping the title/body/buttons — important on small screens. The page
    // keeps its preferred size; scrollbars only appear when the viewport is
    // smaller than that.
    auto *scroll = new QScrollArea;
    scroll->setObjectName("issueComposeScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    m_issueComposePage = scroll; // removeIssueComposePage deletes this (and page)
    m_issueDetailStack->addWidget(scroll);
    m_issueDetailStack->setCurrentWidget(scroll);
    if (m_issueDetail)
        m_issueDetail->setVisible(true);
    if (m_issueDetailToggle)
        m_issueDetailToggle->setText("Hide detail");
}

void MainWindow::removeIssueComposePage()
{
    if (!m_issueDetailStack)
        return;
    if (m_issueComposePage) {
        QWidget *old = m_issueComposePage;
        m_issueComposePage = nullptr;
        m_issueDetailStack->setCurrentIndex(0);
        m_issueDetailStack->removeWidget(old);
        old->deleteLater();
    } else {
        m_issueDetailStack->setCurrentIndex(0);
    }
}

void MainWindow::setIssueInlineNotice(const QString &message, bool error)
{
    // #96: issue-created and related notices now surface in the top notification
    // toast instead of an in-page banner. The inline label stays hidden.
    if (m_issueInlineNotice)
        m_issueInlineNotice->hide();
    if (!message.trimmed().isEmpty())
        flashMessage(message, error);
}

void MainWindow::promptEditIssueTitle()
{
    if (m_currentIssueNumber < 0)
        return;
    QString currentTitle;
    for (const Issue &issue : std::as_const(m_currentIssues)) {
        if (issue.number == m_currentIssueNumber) {
            currentTitle = issue.title;
            break;
        }
    }
    if (currentTitle.isEmpty() || !m_issueTitleEditor)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());
    m_issueTitleEditor->setText(currentTitle);
    m_issueTitle->hide();
    m_issueTitleEditButton->hide();
    m_issueTitleEditor->show();
    m_issueTitleSaveButton->show();
    m_issueTitleCancelButton->show();
    m_issueTitleEditor->setFocus();
    m_issueTitleEditor->selectAll();
}

void MainWindow::saveIssueTitleEdit()
{
    if (m_currentIssueNumber < 0 || !m_issueTitleEditor)
        return;
    const QString trimmed = m_issueTitleEditor->text().trimmed();
    if (trimmed.isEmpty()) {
        setIssueInlineNotice("A title is required.", true);
        return;
    }
    QString currentTitle;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.number == m_currentIssueNumber)
            currentTitle = issue.title;
    if (trimmed == currentTitle) {
        cancelIssueTitleEdit();
        return;
    }

    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setTitle(m_currentIssueNumber, trimmed, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update the title." : error,
                             true);
        return;
    }
    cancelIssueTitleEdit();
    setIssueInlineNotice("Title updated.");
    reloadIssues();
}

void MainWindow::cancelIssueTitleEdit()
{
    if (m_issueTitle)
        m_issueTitle->show();
    if (m_issueTitleEditButton)
        m_issueTitleEditButton->show();
    if (m_issueTitleEditor)
        m_issueTitleEditor->hide();
    if (m_issueTitleSaveButton)
        m_issueTitleSaveButton->hide();
    if (m_issueTitleCancelButton)
        m_issueTitleCancelButton->hide();
}

void MainWindow::updateIssueActionState()
{
    const IssueStore store = issueStoreForCurrentRepo();
    const bool writable = store.canWrite();
    const bool haveIssue = m_currentIssueNumber >= 0;

    // New issues can be filed on a mirror too: they go to the owner's inbox and
    // sync back. Only the owner drains the inbox, so Sync stays writable-only.
    if (m_issueNewButton)
        m_issueNewButton->setEnabled(writable || issuesRepoIndex() >= 0);
    if (m_issueSyncButton)
        m_issueSyncButton->setEnabled(writable);
    if (m_issueTitleEditButton)
        m_issueTitleEditButton->setEnabled(writable && haveIssue);
    if (m_issueTitleEditor)
        m_issueTitleEditor->setEnabled(writable && haveIssue);
    if (m_issueTitleSaveButton)
        m_issueTitleSaveButton->setEnabled(writable && haveIssue);
    if (m_issueTitleCancelButton)
        m_issueTitleCancelButton->setEnabled(haveIssue);
    if (!haveIssue || !writable)
        m_issueDeleteConfirmPending = false;
    // Owner-only structural edits.
    for (QPushButton *b : {m_issueCloseButton, m_issueCloseCommentButton,
                           m_issueLabelsButton,
                           m_issueMilestoneButton, m_issuePriorityButton,
                           m_issuePriorityRaiseButton, m_issuePriorityLowerButton,
                           m_issueAssigneesButton,
                           m_issueDeleteButton, m_issueAttachButton,
                           m_issueAssignAgentButton}) {
        if (b)
            b->setEnabled(writable && haveIssue);
    }
    if (m_issueAgentProvider)
        m_issueAgentProvider->setEnabled(writable && haveIssue);
    if (m_issueAgentCreatePrCheck)
        m_issueAgentCreatePrCheck->setEnabled(writable && haveIssue);
    if (m_issueAgentViewButton)
        m_issueAgentViewButton->setEnabled(haveIssue &&
                                           latestAgentSessionForIssue(m_currentIssueNumber));
    // Comments work for everyone with an issue selected: owners write locally,
    // others submit a signed comment to the relay inbox.
    if (m_issueCommentButton)
        m_issueCommentButton->setEnabled(haveIssue);
    if (m_issueAskAiButton)
        m_issueAskAiButton->setEnabled(haveIssue);
    if (m_issueCopyButton)
        m_issueCopyButton->setEnabled(haveIssue);
    if (m_issueCopyAllButton)
        m_issueCopyAllButton->setEnabled(haveIssue);
    if (m_issueComposer) {
        m_issueComposer->setEnabled(haveIssue);
        m_issueComposer->setMentionCandidates(mentionCandidateNames());
    }
    updateVoteUi();

    // Reflect current status on the close/reopen button, and only offer "Close
    // with comment" while the issue is open (it has no meaning once closed).
    if (haveIssue) {
        for (const Issue &issue : m_currentIssues) {
            if (issue.number == m_currentIssueNumber) {
                const bool open = issue.status != QLatin1String("closed");
                if (m_issueCloseButton)
                    m_issueCloseButton->setText(open ? "Close issue" : "Reopen");
                if (m_issueCloseCommentButton) {
                    m_issueCloseCommentButton->setEnabled(writable && open);
                    m_issueCloseCommentButton->setVisible(open);
                }
                break;
            }
        }
    }
    if (m_issueReadonlyNote) {
        m_issueReadonlyNote->setVisible(!writable && issuesRepoIndex() >= 0);
        m_issueReadonlyNote->setText(
            "You don't host this repository \xE2\x80\x94 new issues and comments are "
            "sent to the maintainer's inbox (text only) and sync back once they "
            "merge them. Edits stay owner-only.");
    }
    if (m_issueCommentButton)
        m_issueCommentButton->setText(writable ? "Comment" : "Send to maintainer");
}

void MainWindow::promptNewIssue()
{
    // Owners write straight to issues/; mirror nodes compose the same page but
    // submit to the source of truth's inbox (handled in the create button). Only
    // block when there is no repo selected at all.
    if (issuesRepoIndex() < 0)
        return;

    auto *page = new QWidget;

    auto *titleLabel = new QLabel("Add a title <span style='color:#cf222e'>*</span>",
                                  page);
    titleLabel->setTextFormat(Qt::RichText);
    titleLabel->setObjectName("sectionLabel");
    auto *titleEdit = new QLineEdit(page);
    titleEdit->setPlaceholderText("Title");
    auto *bodyEdit = new MarkdownEditor(page);
    bodyEdit->setMentionCandidates(mentionCandidateNames());
    // A modest minimum keeps the window shrinkable on small screens; the editor
    // still expands to fill the available space (it has stretch in the layout),
    // and the compose page scrolls when the window is shorter than this.
    bodyEdit->setMinimumHeight(200);
    bodyEdit->setPlaceholderText("Type your description here...");

    auto *left = new QWidget(page);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(titleLabel);
    leftLayout->addWidget(titleEdit);
    auto *descriptionLabel = new QLabel("Add a description", page);
    descriptionLabel->setObjectName("sectionLabel");
    leftLayout->addSpacing(8);
    leftLayout->addWidget(descriptionLabel);
    leftLayout->addWidget(bodyEdit, 1);
    auto *attachHint = new QLabel("Paste, drop, or click Image to add files", page);
    attachHint->setObjectName("statusLine");
    leftLayout->addWidget(attachHint);

    auto *sidebar = new QWidget(page);
    sidebar->setObjectName("issueComposeSidebar");
    sidebar->setFixedWidth(285);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(18, 2, 0, 0);
    sideLayout->setSpacing(8);

    auto addDivider = [&]() {
        auto *line = new QWidget(sidebar);
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background:%1;")
                                .arg(currentThemeIsDark() ? "#30363d" : "#d0d7de"));
        sideLayout->addWidget(line);
    };
    auto addSection = [&](const QString &label, QWidget *field) {
        auto *header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        auto *title = new QLabel(label, sidebar);
        title->setObjectName("sectionLabel");
        auto *gear = new QPushButton(sidebar);
        gear->setObjectName("ghostButton");
        gear->setProperty("buttonSize", "sm");
        gear->setFixedSize(28, 28);
        gear->setEnabled(false);
        setOcticon(gear, "gear", 14);
        header->addWidget(title);
        header->addStretch();
        header->addWidget(gear);
        sideLayout->addLayout(header);
        sideLayout->addWidget(field);
        sideLayout->addSpacing(6);
        addDivider();
        sideLayout->addSpacing(6);
    };

    auto *assigneeBox = new QWidget(sidebar);
    auto *assigneeLayout = new QVBoxLayout(assigneeBox);
    assigneeLayout->setContentsMargins(0, 0, 0, 0);
    assigneeLayout->setSpacing(6);
    auto *assigneesEdit = new QLineEdit(sidebar);
    assigneesEdit->setPlaceholderText("No one");
    auto *assignSelf = new QPushButton("Assign yourself", sidebar);
    assignSelf->setObjectName("ghostButton");
    assignSelf->setProperty("buttonSize", "sm");
    assignSelf->setCursor(Qt::PointingHandCursor);
    connect(assignSelf, &QPushButton::clicked, this, [this, assigneesEdit] {
        const QString who = m_userName.trimmed();
        if (who.isEmpty())
            return;
        QStringList assignees = splitIssueFieldList(assigneesEdit->text());
        if (!assignees.contains(who))
            assignees << who;
        assigneesEdit->setText(assignees.join(", "));
    });
    assigneeLayout->addWidget(assigneesEdit);
    assigneeLayout->addWidget(assignSelf, 0, Qt::AlignLeft);
    addSection("Assignees", assigneeBox);

    auto *labelsEdit = new QLineEdit(sidebar);
    labelsEdit->setPlaceholderText("No labels");
    addSection("Labels", labelsEdit);

    auto *typeValue = new QLabel("No type", sidebar);
    typeValue->setObjectName("statusLine");
    addSection("Type", typeValue);

    auto *priorityCombo = new QComboBox(sidebar);
    priorityCombo->addItem("No priority", 0);
    for (int priority = 1; priority <= 99; ++priority)
        priorityCombo->addItem(QString::number(priority), priority);
    priorityCombo->setToolTip("1 is highest priority; 99 is lowest");
    addSection("Priority", priorityCombo);

    auto *projectsValue = new QLabel("No projects", sidebar);
    projectsValue->setObjectName("statusLine");
    addSection("Projects", projectsValue);

    auto *milestoneCombo = new QComboBox(sidebar);
    milestoneCombo->addItem("No milestone", QString());
    for (const IssueMilestone &ms : m_currentMilestones)
        milestoneCombo->addItem(ms.title, ms.title);
    addSection("Milestone", milestoneCombo);
    sideLayout->addStretch();

    auto *content = new QHBoxLayout;
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(22);
    content->addWidget(left, 1);
    content->addWidget(sidebar);

    auto *createMore = new QCheckBox("Create more", page);
    auto *cancelButton = new QPushButton("Cancel", page);
    cancelButton->setObjectName("ghostButton");
    cancelButton->setCursor(Qt::PointingHandCursor);
    auto *createButton = new QPushButton("Create", page);
    createButton->setObjectName("primaryButton");
    createButton->setCursor(Qt::PointingHandCursor);
    setOcticon(createButton, "issue-opened", 16);
    auto *pageNotice = new QLabel(page);
    pageNotice->setObjectName("issueInlineNotice");
    pageNotice->setWordWrap(true);
    pageNotice->hide();
    auto setPageNotice = [pageNotice](const QString &message, bool error = false) {
        if (message.trimmed().isEmpty()) {
            pageNotice->clear();
            pageNotice->hide();
            return;
        }
        const bool dark = currentThemeIsDark();
        const QString bg = error ? (dark ? "#3d1f21" : "#ffebe9")
                                 : (dark ? "#11251a" : "#dafbe1");
        const QString border = error ? (dark ? "#f85149" : "#cf222e")
                                     : (dark ? "#2ea043" : "#1f883d");
        const QString fg = dark ? "#e6edf3" : "#1f2328";
        pageNotice->setStyleSheet(
            QStringLiteral("QLabel#issueInlineNotice { background-color:%1; color:%2; "
                           "border:1px solid %3; border-radius:6px; padding:8px 10px; }")
                .arg(bg, fg, border));
        pageNotice->setText(message.toHtmlEscaped());
        pageNotice->show();
    };
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addStretch();
    buttonRow->addWidget(createMore);
    buttonRow->addSpacing(18);
    buttonRow->addWidget(cancelButton);
    buttonRow->addWidget(createButton);

    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(22, 18, 16, 14);
    pageLayout->setSpacing(14);
    pageLayout->addLayout(content, 1);
    pageLayout->addWidget(pageNotice);
    pageLayout->addLayout(buttonRow);
    connect(cancelButton, &QPushButton::clicked, this,
            &MainWindow::removeIssueComposePage);
    connect(createButton, &QPushButton::clicked, this, [=] {
        if (titleEdit->text().trimmed().isEmpty()) {
            setPageNotice("A title is required.", true);
            return;
        }

        const QString title = titleEdit->text().trimmed();
        const QStringList labels = splitIssueFieldList(labelsEdit->text());
        const QStringList assignees = splitIssueFieldList(assigneesEdit->text());
        const QString milestone = milestoneCombo->currentData().toString();
        const int priority = priorityCombo->currentData().toInt();

        IssueStore store = issueStoreForCurrentRepo();
        // On a mirror (no work tree) we can't write the issue locally, so send a
        // signed "open" event to the source of truth's inbox; the owner merges it
        // into issues/ preserving us as the author, and it syncs back to mirrors.
        if (!store.canWrite()) {
            if (!submitNewIssueToInbox(title, bodyEdit->markdown(), labels,
                                       milestone, priority, assignees)) {
                setPageNotice("Open a repository you can reach to file an issue.",
                              true);
                return;
            }
            removeIssueComposePage();
            setIssueInlineNotice("Your signed issue was sent to the maintainer's "
                                 "inbox. It appears once they sync it.");
            return;
        }

        QString error;
        const int number = store.createIssue(title, bodyEdit->markdown(), labels,
                                             milestone, priority, assignees,
                                             bodyEdit->pendingAttachments(),
                                             bodyEdit->pendingAttachmentPlaceholders(),
                                             &error);
        if (number < 0) {
            setPageNotice(error.isEmpty() ? "Could not create the issue." : error,
                          true);
            return;
        }
        m_currentIssueNumber = number;
        const bool more = createMore->isChecked();
        removeIssueComposePage();
        reloadIssues();
        propagateRepoUpdate(issuesRepoIndex());
        setIssueInlineNotice("Issue created.");
        if (more)
            promptNewIssue();
    });

    showIssueComposePage(page);
    titleEdit->setFocus();
}

void MainWindow::quickAddIssue()
{
    if (!m_issueQuickAdd)
        return;
    const QString title = m_issueQuickAdd->toPlainText().trimmed();
    if (title.isEmpty())
        return;
    // Remember this prompt so Up can recall it later (adhoc #200). Recording here,
    // before the field is cleared, covers every send path below.
    recordQuickAddHistory(title);

    // "No issue" mode (issue #299): don't create an issue at all — hand the typed
    // text straight to a coding agent as its prompt, like the Agents-tab composer.
    if (m_quickAddNoIssue && m_quickAddNoIssue->isChecked()) {
        const QString provider =
            m_quickAddAgentProvider
                ? m_quickAddAgentProvider->currentData().toString()
                : QStringLiteral("claude-code");
        const bool createPr = m_quickAddCreatePr && m_quickAddCreatePr->isChecked();
        // Hand any attached images to the agent the same way the new-agent
        // composer does: an "Attached image: <path>" line per file (issue #79).
        QString prompt = title;
        for (const QString &img : m_quickAddImages) {
            if (!prompt.endsWith(QLatin1Char('\n')))
                prompt += QLatin1Char('\n');
            prompt += QStringLiteral("Attached image: %1").arg(img);
        }
        if (startAdHocAgentForRepo(issuesRepoIndex(), prompt, provider, createPr) > 0) {
            m_issueQuickAdd->clear();
            clearQuickAddImages();
            setIssueInlineNotice(
                QStringLiteral("Started a %1 agent on your prompt \xE2\x80\x94 no "
                               "issue created.")
                    .arg(agentProviderName(provider)));
        }
        return;
    }

    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        // Mirror node: send the new issue to the source of truth's inbox. The
        // agent hand-off below needs a local issue, so it stays owner-only.
        if (issuesRepoIndex() < 0) {
            setIssueInlineNotice("Pick a repository to add issues.", true);
            return;
        }
        if (submitNewIssueToInbox(title, QString(), {}, QString(), 0, {})) {
            m_issueQuickAdd->clear();
            setIssueInlineNotice("Your signed issue was sent to the maintainer's "
                                 "inbox. It appears once they sync it.");
        }
        return;
    }
    QString error;
    // Any queued images ride along as the new issue's attachments (issue #79).
    const int number = store.createIssue(title, QString(), {}, QString(), 0, {},
                                         m_quickAddImages, &error);
    if (number < 0) {
        setIssueInlineNotice(error.isEmpty() ? "Could not create the issue." : error,
                             true);
        return;
    }
    m_issueQuickAdd->clear();
    clearQuickAddImages();
    m_currentIssueNumber = number;
    reloadIssues();
    propagateRepoUpdate(issuesRepoIndex());
    // A more descriptive confirmation than the old bare "Issue created." — names
    // the number and title so the toast says exactly what landed (issue #299).
    setIssueInlineNotice(QStringLiteral("Issue #%1 created: %2").arg(number).arg(title));
    // If requested, hand the freshly-created issue straight to a coding agent.
    if (m_quickAddAssignAgent && m_quickAddAssignAgent->isChecked()) {
        const QString provider =
            m_quickAddAgentProvider
                ? m_quickAddAgentProvider->currentData().toString()
                : QStringLiteral("codex");
        const bool oldCreatePr =
            m_issueAgentCreatePrCheck && m_issueAgentCreatePrCheck->isChecked();
        if (m_issueAgentCreatePrCheck) {
            const QSignalBlocker block(m_issueAgentCreatePrCheck);
            m_issueAgentCreatePrCheck->setChecked(m_quickAddCreatePr &&
                                                  m_quickAddCreatePr->isChecked());
            assignIssueToAgent(provider);
            m_issueAgentCreatePrCheck->setChecked(oldCreatePr);
        } else {
            assignIssueToAgent(provider);
        }
    }
}

// Append a just-sent quick-add prompt to the recall history (adhoc #200). Skips
// consecutive duplicates so Up doesn't step through repeats, caps the list, and
// resets navigation so the next Up starts from this freshest entry.
void MainWindow::recordQuickAddHistory(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return;
    if (m_quickAddHistory.isEmpty() || m_quickAddHistory.last() != t)
        m_quickAddHistory.append(t);
    constexpr int kMaxQuickAddHistory = 50;
    while (m_quickAddHistory.size() > kMaxQuickAddHistory)
        m_quickAddHistory.removeFirst();
    m_quickAddHistoryIndex = -1;
    m_quickAddDraft.clear();
    // Persist so Up still recalls these prompts after a restart (adhoc #200).
    QSettings().setValue(kQuickAddHistorySetting, m_quickAddHistory);
}

// Walk the quick-add prompt history from the footer bar (adhoc #200). direction
// < 0 is Up (older prompts), > 0 is Down (back toward the live draft). Returns
// true when the key was consumed so the event filter swallows it.
bool MainWindow::navigateQuickAddHistory(int direction)
{
    if (!m_issueQuickAdd || m_quickAddHistory.isEmpty())
        return false;
    // Replace the field's text without the textChanged handler treating the
    // recall as a manual edit (which would reset the history position).
    auto showText = [this](const QString &text) {
        m_quickAddHistoryNavigating = true;
        m_issueQuickAdd->setPlainText(text);
        m_issueQuickAdd->moveCursor(QTextCursor::End);
        m_quickAddHistoryNavigating = false;
    };
    const int count = m_quickAddHistory.size();
    if (direction < 0) { // Up: step toward older prompts
        if (m_quickAddHistoryIndex < 0) {
            // Entering history: stash whatever was being typed, show the newest.
            m_quickAddDraft = m_issueQuickAdd->toPlainText();
            m_quickAddHistoryIndex = count - 1;
        } else if (m_quickAddHistoryIndex > 0) {
            --m_quickAddHistoryIndex;
        } else {
            return true; // already at the oldest entry; swallow the key
        }
        showText(m_quickAddHistory.at(m_quickAddHistoryIndex));
        return true;
    }
    // Down: step toward newer prompts, then back out to the stashed draft.
    if (m_quickAddHistoryIndex < 0)
        return false; // not navigating; let the field handle the key
    if (m_quickAddHistoryIndex < count - 1) {
        ++m_quickAddHistoryIndex;
        showText(m_quickAddHistory.at(m_quickAddHistoryIndex));
    } else {
        m_quickAddHistoryIndex = -1;
        showText(m_quickAddDraft);
    }
    return true;
}

// Footer quick-add "paperclip": pick one or more images to attach to the next
// send (issue #79). They're queued, not sent now — the actual hand-off happens
// in quickAddIssue() when the user hits Enter / Send.
void MainWindow::attachQuickAddImage()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Attach image"), QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp)"));
    for (const QString &f : files)
        queueQuickAddImage(f);
    if (m_issueQuickAdd)
        m_issueQuickAdd->setFocus();
}

// Ctrl+V into the quick-add bar: if the clipboard holds an image, save it to a
// temp PNG and queue it. Returns true only when an image was queued, so a normal
// text paste still falls through to the line edit.
bool MainWindow::tryPasteImageIntoQuickAdd()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasImage())
        return false;
    // Reuse the new-agent-prompt image temp store: it saves to a stable file that
    // outlives this call (the agent / issue write reads it later).
    const QString path =
        saveNewAgentPromptImage(qvariant_cast<QImage>(mime->imageData()));
    if (path.isEmpty())
        return false;
    queueQuickAddImage(path);
    return true;
}

// Queue an image path for the next quick-add send, ignoring blanks and dupes.
void MainWindow::queueQuickAddImage(const QString &path)
{
    if (path.isEmpty() || m_quickAddImages.contains(path))
        return;
    m_quickAddImages.append(path);
    updateQuickAddImageButton();
    setIssueInlineNotice(
        QStringLiteral("Image attached (%1) \xE2\x80\x94 it'll go with your next "
                       "send.")
            .arg(m_quickAddImages.size()));
}

void MainWindow::clearQuickAddImages()
{
    if (m_quickAddImages.isEmpty())
        return;
    m_quickAddImages.clear();
    updateQuickAddImageButton();
}

// Reflect how many images are queued: a count badge on the button text and a
// tooltip naming the files, so the paperclip stands out once something's attached.
void MainWindow::updateQuickAddImageButton()
{
    if (!m_quickAddImageButton)
        return;
    const int n = m_quickAddImages.size();
    m_quickAddImageButton->setText(n > 0 ? QString::number(n) : QString());
    if (n == 0) {
        m_quickAddImageButton->setToolTip(
            QStringLiteral("Attach an image \xE2\x80\x94 pick a file or paste with "
                           "Ctrl+V. Sent to the agent in \"No issue\" mode, or "
                           "attached to the created issue."));
        return;
    }
    QStringList names;
    for (const QString &p : m_quickAddImages)
        names << QFileInfo(p).fileName();
    m_quickAddImageButton->setToolTip(
        QStringLiteral("%1 image%2 attached:\n%3\n\nClick to add more.")
            .arg(n)
            .arg(n == 1 ? QString() : QStringLiteral("s"), names.join(QLatin1Char('\n'))));
}

void MainWindow::copyIssueToClipboard()
{
    if (m_currentIssueNumber < 0)
        return;
    const Issue *issue = nullptr;
    for (const Issue &candidate : m_currentIssues)
        if (candidate.number == m_currentIssueNumber)
            issue = &candidate;
    if (!issue)
        return;

    // Copy just the issue title — no number, status, labels, or per-comment
    // author/date headers — so it pastes cleanly as plain text.
    QApplication::clipboard()->setText(issue->title);
    setIssueInlineNotice("Issue title copied.");
}

void MainWindow::copyIssueThreadToClipboard()
{
    if (m_currentIssueNumber < 0)
        return;
    const Issue *issue = nullptr;
    for (const Issue &candidate : m_currentIssues)
        if (candidate.number == m_currentIssueNumber)
            issue = &candidate;
    if (!issue)
        return;

    // Pre-compute edits (target -> latest edit) and deletions, mirroring
    // renderIssueThread so the copied text matches what's shown on screen.
    QHash<QString, IssueEvent> edits;
    QSet<QString> deleted;
    for (const IssueEvent &ev : issue->events) {
        if (ev.type == QLatin1String("edit") && !ev.target.isEmpty())
            edits.insert(ev.target, ev);
        else if (ev.type == QLatin1String("delete") && !ev.target.isEmpty() &&
                 ev.target != QLatin1String("self"))
            deleted.insert(ev.target);
    }

    // Title + status, then the opening post and every comment with an
    // author/date header. Activity events (labels, status changes, …) are left
    // out so the result reads as the issue conversation in plain text.
    QStringList parts;
    parts << QStringLiteral("#%1 %2").arg(issue->number).arg(issue->title);
    parts << QStringLiteral("Status: %1").arg(issue->status);

    int commentCount = 0;
    for (const IssueEvent &ev : issue->events) {
        const bool isOpen = ev.type == QLatin1String("open");
        const bool isComment = ev.type == QLatin1String("comment");
        if (!isOpen && !isComment)
            continue;
        if (isComment && deleted.contains(ev.id))
            continue;
        QString body = edits.contains(ev.id) ? edits.value(ev.id).body : ev.body;
        const QString who =
            ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        const QString when = QDateTime::fromMSecsSinceEpoch(ev.ts).toString(
            QStringLiteral("yyyy-MM-dd HH:mm"));
        parts << QString()
              << QStringLiteral("--- %1 %2 (%3) ---")
                     .arg(who,
                          isOpen ? QStringLiteral("opened")
                                 : QStringLiteral("commented"),
                          when)
              << body.trimmed();
        if (isComment)
            ++commentCount;
    }

    QApplication::clipboard()->setText(parts.join(QChar('\n')));
    setIssueInlineNotice(QStringLiteral("Issue and %1 comment%2 copied.")
                             .arg(commentCount)
                             .arg(commentCount == 1 ? QString() : QStringLiteral("s")));
}

void MainWindow::showIssueAiTyping()
{
    hideIssueAiTyping();
    if (!m_issueThreadLayout)
        return;

    // A thread card styled like a comment, with an animated "answering" line.
    auto *row = new QWidget;
    row->setObjectName("issueTimelineRow");
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(14);
    auto *avatar = new QLabel(QStringLiteral("AI"));
    avatar->setObjectName("issueAvatar");
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(36, 36);
    rowLayout->addWidget(avatar, 0, Qt::AlignTop);

    auto *card = new QWidget;
    card->setObjectName("issueTimelineCard");
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 12, 16, 12);
    auto *label = new QLabel(QString::fromUtf8("\xF0\x9F\xA4\x96  AI is answering"));
    label->setObjectName("statusLine");
    cardLayout->addWidget(label);
    rowLayout->addWidget(card, 1);

    // Drop it in just above the trailing stretch so it sits at the bottom.
    const int insertAt = qMax(0, m_issueThreadLayout->count() - 1);
    m_issueThreadLayout->insertWidget(insertAt, row);
    m_issueAiTypingRow = row;

    // Animate a little walking bot with cycling dots.
    m_issueAiTypingTimer = new QTimer(row);
    connect(m_issueAiTypingTimer, &QTimer::timeout, label, [label, n = 0]() mutable {
        n = (n + 1) % 4;
        label->setText(QString::fromUtf8("%1\xF0\x9F\xA4\x96  AI is answering%2")
                           .arg(QString(n, QChar(' ')), QString(n, QChar('.'))));
    });
    m_issueAiTypingTimer->start(400);

    // Scroll the new card into view.
    if (m_issueThreadScroll) {
        QTimer::singleShot(0, this, [this] {
            if (m_issueThreadScroll && m_issueThreadScroll->verticalScrollBar())
                m_issueThreadScroll->verticalScrollBar()->setValue(
                    m_issueThreadScroll->verticalScrollBar()->maximum());
        });
    }
}

void MainWindow::hideIssueAiTyping()
{
    if (m_issueAiTypingTimer) {
        m_issueAiTypingTimer->stop();
        m_issueAiTypingTimer = nullptr; // parented to the row; freed with it
    }
    if (m_issueAiTypingRow) {
        if (m_issueThreadLayout)
            m_issueThreadLayout->removeWidget(m_issueAiTypingRow);
        m_issueAiTypingRow->deleteLater();
        m_issueAiTypingRow = nullptr;
    }
}

void MainWindow::askAiForCurrentIssue()
{
    if (m_currentIssueNumber < 0 || !m_networkAccess)
        return;
    const QString question =
        m_issueComposer ? m_issueComposer->markdown().trimmed() : QString();
    if (question.isEmpty()) {
        setIssueInlineNotice("Type a question in the comment box first.", true);
        return;
    }
    const QString apiKey =
        QSettings().value(kCodexApiKeySetting).toString().trimmed();
    if (apiKey.isEmpty()) {
        setIssueInlineNotice("Add an OpenAI API key in Settings first.", true);
        return;
    }

    const int issueNumber = m_currentIssueNumber;
    const Issue *selected = nullptr;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.number == issueNumber)
            selected = &issue;
    if (!selected)
        return;
    const int repoIdx = issuesRepoIndex();
    if (repoIdx < 0)
        return;
    const RepositoryRecord selectedRepo = m_repositories.at(repoIdx);
    const RepositoryRecord writableRepo = writableRecordFor(selectedRepo);

    QHash<QString, QString> edits;
    QSet<QString> deleted;
    for (const IssueEvent &ev : selected->events) {
        if (ev.type == "edit" && !ev.target.isEmpty())
            edits.insert(ev.target, ev.body);
        else if (ev.type == "delete" && !ev.target.isEmpty() && ev.target != "self")
            deleted.insert(ev.target);
    }

    QStringList context;
    context << QStringLiteral("Issue #%1: %2")
                   .arg(selected->number)
                   .arg(selected->title)
            << QStringLiteral("Status: %1").arg(selected->status);
    if (!selected->labels.isEmpty())
        context << QStringLiteral("Labels: %1").arg(selected->labels.join(", "));
    if (!selected->milestone.isEmpty())
        context << QStringLiteral("Milestone: %1").arg(selected->milestone);
    for (const IssueEvent &ev : selected->events) {
        if (ev.type != "open" && ev.type != "comment")
            continue;
        if (deleted.contains(ev.id))
            continue;
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        const QString body = edits.contains(ev.id) ? edits.value(ev.id) : ev.body;
        context << QStringLiteral("\n%1:\n%2").arg(who, body);
    }

    QString issueContext = context.join('\n');
    if (issueContext.size() > 12000)
        issueContext = issueContext.left(12000) +
                       QStringLiteral("\n\n[Issue context truncated]");

    const QString prompt =
        QStringLiteral("Use this issue thread as context.\n\n%1\n\nQuestion:\n%2")
            .arg(issueContext, question);

    // Post the user's question to the thread as their own comment first (so the
    // conversation reads naturally), then show the animated "AI is answering…"
    // card while the request is in flight. The prompt is already captured above,
    // so reloading the thread here is safe.
    {
        IssueStore questionStore = issueStoreForCurrentRepo();
        if (questionStore.canWrite()) {
            QString qError;
            questionStore.addComment(issueNumber, question, {}, &qError);
        } else {
            submitIssueCommentToInbox(question);
        }
        if (m_issueComposer)
            m_issueComposer->setMarkdown(QString());
        reloadIssues();
        showIssueAiTyping();
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), kIssueAskAiModel);
    payload.insert(QStringLiteral("instructions"),
                   QStringLiteral("Answer the user's issue question concisely. "
                                  "If the repository context is insufficient, "
                                  "say what is missing. Do not claim to have "
                                  "changed code or inspected files beyond the "
                                  "provided issue thread."));
    payload.insert(QStringLiteral("input"), prompt);
    payload.insert(QStringLiteral("max_output_tokens"), 900);

    QNetworkRequest request(
        QUrl(QStringLiteral("https://api.openai.com/v1/responses")));
    request = openAiRequest(request.url(), apiKey);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    if (m_issueAskAiButton) {
        m_issueAskAiButton->setEnabled(false);
        m_issueAskAiButton->setText("Asking...");
    }
    setIssueInlineNotice("Asking OpenAI...");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, issueNumber, question, selectedRepo, writableRepo] {
                const QByteArray body = reply->readAll();
                reply->deleteLater();
                hideIssueAiTyping();
                if (m_issueAskAiButton) {
                    m_issueAskAiButton->setText("Ask AI");
                    m_issueAskAiButton->setEnabled(m_currentIssueNumber >= 0);
                }
                if (reply->error() != QNetworkReply::NoError) {
                    setIssueInlineNotice("OpenAI request failed: " +
                                             apiErrorSummary(reply, body),
                                         true);
                    return;
                }

                const QJsonObject obj = QJsonDocument::fromJson(body).object();
                const QString answer = openAiResponseText(obj);
                if (answer.isEmpty()) {
                    setIssueInlineNotice("OpenAI returned an empty answer.", true);
                    return;
                }

                // What the answer cost, from the response's token usage.
                qint64 inTok = 0, outTok = 0;
                const double costUsd = openAiAskCostUsd(obj, &inTok, &outTok);
                const QString costLine =
                    QString::fromUtf8("\n\n*\xF0\x9F\xA4\x96 %1 \xC2\xB7 cost "
                                      "$%2 (%3 in / %4 out tokens)*")
                        .arg(kIssueAskAiModel,
                             QString::number(costUsd, 'f', 4))
                        .arg(inTok)
                        .arg(outTok);
                // The question is already its own comment in the thread, so the
                // bot comment is just the answer plus the cost footer.
                const QString comment = answer.trimmed() + costLine;

                IssueStore store(writableRepo.localPath, writableRepo.mirrorPath,
                                 &m_profileIdentity, m_userName);
                if (store.canWrite()) {
                    QString error;
                    if (!store.addComment(issueNumber, comment, {}, &error)) {
                        setIssueInlineNotice(error.isEmpty()
                                                 ? "Could not add the AI answer."
                                                 : error,
                                             true);
                        return;
                    }
                    if (m_issueComposer)
                        m_issueComposer->setMarkdown(QString());
                    reloadIssues();
                    setIssueInlineNotice("AI answer added.");
                    return;
                }

                IssueStore signingStore(selectedRepo.localPath,
                                        selectedRepo.mirrorPath,
                                        &m_profileIdentity, m_userName);
                IssueEvent ev;
                ev.type = "comment";
                ev.body = comment;
                ev = signingStore.makeSignedEvent(issueNumber, ev);
                ev.bodyFile = "comments/" + ev.id + ".md";
                QJsonObject eventJson = ev.toJson();
                eventJson.insert("body", ev.body);
                const QJsonObject payload{{"owner", selectedRepo.owner},
                                          {"repo", selectedRepo.name},
                                          {"number", issueNumber},
                                          {"event", eventJson}};

                QNetworkRequest request(issuesApiUrl(selectedRepo));
                request.setHeader(QNetworkRequest::ContentTypeHeader,
                                  "application/json");
                QNetworkReply *postReply = m_networkAccess->post(
                    request,
                    QJsonDocument(payload).toJson(QJsonDocument::Compact));
                connect(postReply, &QNetworkReply::finished, this,
                        [this, postReply] {
                            postReply->deleteLater();
                            if (postReply->error() == QNetworkReply::NoError) {
                                if (m_issueComposer)
                                    m_issueComposer->setMarkdown(QString());
                                setIssueInlineNotice(
                                    "AI answer sent to the maintainer's inbox.");
                            } else {
                                setIssueInlineNotice(
                                    "Could not send the AI answer: " +
                                        postReply->errorString(),
                                    true);
                            }
                        });
            });
}

void MainWindow::addIssueComment()
{
    if (m_currentIssueNumber < 0)
        return;
    const QString body = m_issueComposer ? m_issueComposer->markdown() : QString();
    const QStringList attachments =
        m_issueComposer ? m_issueComposer->pendingAttachments() : m_pendingIssueAttachments;
    const QStringList placeholders =
        m_issueComposer ? m_issueComposer->pendingAttachmentPlaceholders() : QStringList();
    if (body.trimmed().isEmpty() && attachments.isEmpty())
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        // Not the host: send a signed comment to the maintainer's relay inbox.
        submitIssueCommentToInbox(body);
        return;
    }

    const int number = m_currentIssueNumber;

    // Optimistic UI: drop the comment into the open thread right away so it
    // appears instantly, instead of waiting on the git write+commit and the full
    // issue reload below. The deferred persist (next event-loop tick) reconciles
    // it with the canonical on-disk events. Attachments are copied during the
    // real write, so comments carrying files fall back to the post-write render.
    if (attachments.isEmpty()) {
        for (Issue &issue : m_currentIssues) {
            if (issue.number != number)
                continue;
            IssueEvent ev;
            ev.type = "comment";
            ev.body = body;
            ev = store.makeSignedEvent(number, ev);
            issue.events.append(ev);
            renderIssueThread(issue);
            break;
        }
    }
    if (m_issueComposer) {
        m_issueComposer->setMarkdown(QString());
        m_issueComposer->clearPendingAttachments();
    }
    m_pendingIssueAttachments.clear();
    if (m_issueAttachButton)
        m_issueAttachButton->setText("Paste, drop, or click to add files");

    // Persist on the next tick so the optimistic card paints before the
    // (comparatively slow) git commit and reload block the UI thread.
    QTimer::singleShot(0, this, [this, number, body, attachments, placeholders]() {
        IssueStore store = issueStoreForCurrentRepo();
        QString error;
        if (!store.addComment(number, body, attachments, placeholders, &error)) {
            setIssueInlineNotice(error.isEmpty() ? "Could not add the comment." : error,
                                 true);
            reloadIssues(); // discard the optimistic card on failure
            return;
        }
        reloadIssues();
        propagateRepoUpdate(issuesRepoIndex());
        setIssueInlineNotice("Comment added.");
    });
}

void MainWindow::closeIssueWithComment()
{
    if (m_currentIssueNumber < 0)
        return;
    const int number = m_currentIssueNumber;
    const QString body = m_issueComposer ? m_issueComposer->markdown() : QString();
    const QStringList attachments =
        m_issueComposer ? m_issueComposer->pendingAttachments()
                        : m_pendingIssueAttachments;
    const QStringList placeholders =
        m_issueComposer ? m_issueComposer->pendingAttachmentPlaceholders() : QStringList();
    // The whole point of this button is closing *with* a comment; an empty box
    // should use plain "Close issue" instead.
    if (body.trimmed().isEmpty() && attachments.isEmpty()) {
        setIssueInlineNotice(
            "Write a comment to close with, or use \xE2\x80\x9C" "Close issue\xE2\x80\x9D.",
            true);
        if (m_issueComposer)
            m_issueComposer->setFocus();
        return;
    }
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        // Mirror nodes can't close (the button is disabled for them anyway).
        setIssueInlineNotice("This repo is read-only here; can't close the issue.",
                             true);
        return;
    }
    // Persist the comment, then flip the status — both as one synchronous action
    // so the comment is guaranteed to land before the close event.
    QString error;
    if (!store.addComment(number, body, attachments, placeholders, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not add the comment." : error,
                             true);
        return;
    }
    if (!store.setStatus(number, QStringLiteral("closed"), &error)) {
        setIssueInlineNotice(error.isEmpty()
                                 ? "Comment added, but could not close the issue."
                                 : error,
                             true);
        reloadIssues();
        return;
    }
    if (m_issueComposer) {
        m_issueComposer->setMarkdown(QString());
        m_issueComposer->clearPendingAttachments();
    }
    m_pendingIssueAttachments.clear();
    if (m_issueAttachButton)
        m_issueAttachButton->setText("Paste, drop, or click to add files");
    // Closing advances to the next issue in the list so the user can keep working
    // through them (adhoc #249); if there's none, stay on the just-closed issue
    // rather than collapsing to the list (mirrors toggleIssueStatus).
    const int nextIssue = nextVisibleIssueAfter(number);
    if (nextIssue > 0)
        m_selectIssueOnReload = nextIssue;
    else
        m_keepCurrentOnReload = true;
    reloadIssues();
    propagateRepoUpdate(issuesRepoIndex());
    setIssueInlineNotice("Comment added and issue closed.");
}

void MainWindow::attachIssueImage()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Attach images", QString(),
        "Images (*.png *.jpg *.jpeg *.gif *.webp);;All files (*)");
    if (files.isEmpty())
        return;
    for (const QString &f : files)
        queueIssueAttachment(f);
}

void MainWindow::queueIssueAttachment(const QString &path)
{
    if (path.isEmpty() || m_pendingIssueAttachments.contains(path))
        return;
    m_pendingIssueAttachments += path;
    if (m_issueComposer)
        m_issueComposer->addImageFile(path);
    if (m_issueAttachButton)
        m_issueAttachButton->setText(
            QStringLiteral("Attached: %1").arg(m_pendingIssueAttachments.size()));
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // First expose of the top-level window: its first frame is now on screen, so
    // it's safe to run the deferred git-backed startup without a black frame.
    if (event->type() == QEvent::Expose && obj == windowHandle()) {
        if (QWindow *handle = windowHandle(); handle && handle->isExposed())
            QTimer::singleShot(0, this, &MainWindow::runDeferredStartup);
        return QMainWindow::eventFilter(obj, event); // never consume expose
    }
    // Click the top-bar balance to cycle its display currency (SOL/USD/INR).
    if (obj == m_navSolanaBalance && event->type() == QEvent::MouseButtonRelease) {
        cycleNavSolanaCurrency();
        return true;
    }
    // Click a growing action-strip box (or its timer) to jump straight to that
    // run's live output. The labels carry the run id as a dynamic property.
    if (event->type() == QEvent::MouseButtonRelease) {
        if (auto *w = qobject_cast<QWidget *>(obj)) {
            const QVariant runId = w->property("actionRunId");
            if (runId.isValid()) {
                openActionRunFromNotification(runId.toInt());
                return true;
            }
        }
    }
    // Pasting an image into the chat composer shares it as an attachment. Only
    // consume the event when we actually sent an image; otherwise let the line
    // edit handle a normal text paste.
    if (obj == m_messageInput && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->matches(QKeySequence::Paste) && trySendClipboardImage())
            return true;
    }
    // Ctrl+V into the footer quick-add bar: if the clipboard holds an image,
    // queue it as an attachment instead of pasting its (usually empty) text
    // (issue #79). A normal text paste falls through.
    if (obj == m_issueQuickAdd && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->matches(QKeySequence::Paste) && tryPasteImageIntoQuickAdd())
            return true;
        // Enter sends the prompt; Shift+Enter inserts a newline (the box is now a
        // two-line QPlainTextEdit, which would otherwise just add a newline).
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            !(ke->modifiers() & Qt::ShiftModifier)) {
            quickAddIssue();
            return true;
        }
        // Up/Down walk the quick-add prompt history (adhoc #200): Up recalls the
        // last prompt sent so it can be fired again, Down returns toward the draft.
        if (ke->key() == Qt::Key_Up && navigateQuickAddHistory(-1))
            return true;
        if (ke->key() == Qt::Key_Down && navigateQuickAddHistory(1))
            return true;
    }
    // Agents composer: Enter sends the queued message; Shift+Enter inserts a
    // newline (issue #41). Mirrors the Claude Code conversation input.
    if (obj == m_agentPromptEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && !(ke->modifiers() & Qt::ShiftModifier)) {
            if (m_agentSendPromptButton)
                m_agentSendPromptButton->click();
            return true;
        }
    }
    // Drag along the issue list's Progress column to set a row's percent.
    if (m_issueTable && obj == m_issueTable->viewport() &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonRelease)) {
        if (handleIssueProgressDrag(static_cast<QMouseEvent *>(event)))
            return true;
    }
    // Global search box: drive the floating results dropdown from the keyboard
    // (the dropdown is NoFocus, so it never takes the keyboard itself).
    if (obj == m_globalSearch) {
        if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            const bool open = m_globalSearchPopup && m_globalSearchPopup->isVisible();
            switch (ke->key()) {
            case Qt::Key_Down:
                if (open) { moveGlobalSearchSelection(1); return true; }
                break;
            case Qt::Key_Up:
                if (open) { moveGlobalSearchSelection(-1); return true; }
                break;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (open) {
                    activateGlobalSearchItem(m_globalSearchPopup->currentItem());
                    return true;
                }
                break;
            case Qt::Key_Escape:
                if (open) { hideGlobalSearchPopup(); return true; }
                break;
            default:
                break;
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Clicking elsewhere dismisses the dropdown; clicking a result keeps
            // focus in the box (the list is NoFocus), so this won't pre-empt it.
            hideGlobalSearchPopup();
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::toggleIssueStatus()
{
    if (m_currentIssueNumber < 0)
        return;
    QString status = "open";
    for (const Issue &issue : m_currentIssues)
        if (issue.number == m_currentIssueNumber)
            status = issue.status;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    const bool closing = status == "open";
    // Closing advances to the next issue in the list so the user can keep working
    // through them; capture that target before the status flip reorders things.
    const int nextIssue = closing ? nextVisibleIssueAfter(m_currentIssueNumber) : -1;
    if (!store.setStatus(m_currentIssueNumber, closing ? "closed" : "open",
                         &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update issue status." : error,
                             true);
        return;
    }
    if (closing && nextIssue > 0) {
        // Select the next issue once the table is rebuilt (adhoc #249).
        m_selectIssueOnReload = nextIssue;
    } else {
        // No next issue (or reopening): if this toggle drops the issue out of the
        // current filter (e.g. closing it while filtering to Open), stay on it —
        // keep the detail panel open on the same issue rather than jumping away or
        // collapsing back to the list.
        m_keepCurrentOnReload = true;
    }
    reloadIssues();
    setIssueInlineNotice(closing ? "Issue closed." : "Issue reopened.");
}

int MainWindow::nextVisibleIssueAfter(int number) const
{
    if (!m_issueTable)
        return -1;
    const int rows = m_issueTable->rowCount();
    int idx = -1;
    for (int r = 0; r < rows; ++r) {
        const QTableWidgetItem *item = m_issueTable->item(r, 0);
        if (item && item->data(Qt::UserRole).toInt() == number) {
            idx = r;
            break;
        }
    }
    if (idx < 0)
        return -1;
    // Prefer the row below; fall back to the row above when closing the last one.
    if (idx + 1 < rows) {
        if (const QTableWidgetItem *item = m_issueTable->item(idx + 1, 0))
            return item->data(Qt::UserRole).toInt();
    }
    if (idx - 1 >= 0) {
        if (const QTableWidgetItem *item = m_issueTable->item(idx - 1, 0))
            return item->data(Qt::UserRole).toInt();
    }
    return -1;
}

void MainWindow::deleteCurrentIssue()
{
    if (m_currentIssueNumber < 0)
        return;
    const int number = m_currentIssueNumber;
    // One click, then a single confirm dialog offering two flavours of delete.
    // "Delete" tombstones the issue: it vanishes from every list and the deletion
    // syncs to peers, but it's a cheap commit that never freezes the app. "Delete
    // with history" additionally purges the issue from all of git history — the
    // old slow path, now run off the UI thread.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Delete issue"));
    box.setText(QStringLiteral("Delete issue #%1?").arg(number));
    box.setInformativeText(QStringLiteral(
        "Delete hides it everywhere and syncs the deletion to peers; the issue "
        "stays in git history.\n\n"
        "Delete with history also purges it from all of git history — thorough but "
        "slower and unrecoverable."));
    QPushButton *regularBtn =
        box.addButton(QStringLiteral("Delete"), QMessageBox::AcceptRole);
    QPushButton *historyBtn = box.addButton(QStringLiteral("Delete with history"),
                                            QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(regularBtn);
    box.exec();
    QAbstractButton *clicked = box.clickedButton();
    if (clicked == historyBtn) {
        deleteCurrentIssueWithHistory(number);
        return;
    }
    if (clicked != regularBtn)
        return;

    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.tombstoneIssue(number, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not delete the issue." : error,
                             true);
        return;
    }
    m_currentIssueNumber = -1;
    reloadIssues();
    setIssueInlineNotice("Issue deleted.");
}

void MainWindow::deleteCurrentIssueWithHistory(int number)
{
    if (m_issueHistoryDeleteInProgress) {
        setIssueInlineNotice(
            QStringLiteral("Issue history deletion is already running."));
        return;
    }
    m_issueHistoryDeleteInProgress = true;
    setIssueInlineNotice(
        QStringLiteral("Deleting issue #%1 and rewriting history… this can take a "
                       "while.")
            .arg(number));
    if (m_issueDeleteButton)
        m_issueDeleteButton->setEnabled(false);
    QApplication::setOverrideCursor(Qt::BusyCursor);

    // deleteIssue only shells out to git (no event signing), so it's safe to run
    // on a worker thread with a copy of the store. Results travel back via shared
    // state read in the finished handler on the main thread.
    IssueStore store = issueStoreForCurrentRepo();
#ifdef FORKMESH_WINDOW_TESTS
    auto testRunner = m_testIssueHistoryDeleteRunner;
#endif
    auto ok = std::make_shared<bool>(false);
    auto error = std::make_shared<QString>();
    QThread *worker = QThread::create([store, number, ok, error
#ifdef FORKMESH_WINDOW_TESTS
                                       , testRunner
#endif
    ]() mutable {
        QString err;
#ifdef FORKMESH_WINDOW_TESTS
        if (testRunner) {
            *ok = testRunner(number, &err);
        } else
#endif
        {
            *ok = store.deleteIssue(number, &err);
        }
        *error = err;
    });
    connect(worker, &QThread::finished, this,
            [this, worker, ok, error]() {
                m_issueHistoryDeleteInProgress = false;
                QApplication::restoreOverrideCursor();
                if (m_issueDeleteButton)
                    m_issueDeleteButton->setEnabled(true);
                if (*ok) {
                    m_currentIssueNumber = -1;
                    reloadIssues();
                    setIssueInlineNotice("Issue deleted with history.");
                } else {
                    setIssueInlineNotice(error->isEmpty()
                                             ? QStringLiteral("Could not delete the issue.")
                                             : *error,
                                         true);
                }
                worker->deleteLater();
            });
    worker->start();
}

void MainWindow::editIssueLabels()
{
    if (m_currentIssueNumber < 0)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());
    if (m_issueLabelsStack)
        m_issueLabelsStack->setCurrentIndex(1);
    if (m_issueLabelsEdit) {
        m_issueLabelsEdit->setFocus();
        m_issueLabelsEdit->selectAll();
    }
}

void MainWindow::saveIssueLabelsInline()
{
    if (m_currentIssueNumber < 0 || !m_issueLabelsEdit)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    const QStringList labels = splitIssueFieldList(m_issueLabelsEdit->text());
    if (!store.setLabels(m_currentIssueNumber, labels, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update labels." : error,
                             true);
        return;
    }
    setIssueInlineNotice("Labels updated.");
    reloadIssues();
}

void MainWindow::editIssueMilestone()
{
    if (m_currentIssueNumber < 0)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());
    if (m_issueMilestoneStack)
        m_issueMilestoneStack->setCurrentIndex(1);
    if (m_issueMilestoneEdit)
        m_issueMilestoneEdit->setFocus();
}

void MainWindow::saveIssueMilestoneInline()
{
    if (m_currentIssueNumber < 0 || !m_issueMilestoneEdit)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    const QString milestone = m_issueMilestoneEdit->currentData().toString();
    if (!store.setMilestone(m_currentIssueNumber, milestone, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update milestone." : error,
                             true);
        return;
    }
    setIssueInlineNotice("Milestone updated.");
    reloadIssues();
}

void MainWindow::editIssuePriority()
{
    if (m_currentIssueNumber < 0)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());
    if (m_issuePriorityStack)
        m_issuePriorityStack->setCurrentIndex(1);
    if (m_issuePriorityEdit)
        m_issuePriorityEdit->setFocus();
}

void MainWindow::saveIssuePriorityInline()
{
    if (m_currentIssueNumber < 0 || !m_issuePriorityEdit)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    const int priority = m_issuePriorityEdit->currentData().toInt();
    if (!store.setPriority(m_currentIssueNumber, priority, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update priority." : error,
                             true);
        return;
    }
    setIssueInlineNotice(priority > 0 ? "Priority updated." : "Priority cleared.");
    reloadIssues();
}

void MainWindow::nudgeIssuePriority(int direction)
{
    if (m_currentIssueNumber < 0 || direction == 0)
        return;
    // Priority runs 1 (highest) to 99 (lowest). A quick nudge moves by a quarter
    // of that span (~25); direction < 0 raises priority (toward 1), direction > 0
    // lowers it (toward 99).
    const int kHighest = 1;
    const int kLowest = 99;
    const int step = qRound((kLowest - kHighest) * 0.25);
    int original = 0;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.number == m_currentIssueNumber) {
            original = issue.priority;
            break;
        }
    // An unset priority starts from the middle of the range so the first nudge
    // lands somewhere sensible rather than jumping to an extreme.
    const int base = original > 0 ? original
                                  : qRound((kHighest + kLowest) / 2.0);
    const int next = qBound(kHighest, base + direction * step, kLowest);
    if (next == original) {
        setIssueInlineNotice(direction < 0 ? "Already at the highest priority."
                                           : "Already at the lowest priority.");
        return;
    }
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setPriority(m_currentIssueNumber, next, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update priority." : error,
                             true);
        return;
    }
    setIssueInlineNotice(QStringLiteral("Priority set to %1.").arg(next));
    reloadIssues();
}

bool MainWindow::handleIssueProgressDrag(QMouseEvent *ev)
{
    constexpr int kProgressCol = 11;
    if (ev->type() == QEvent::MouseButtonPress) {
        if (ev->button() != Qt::LeftButton)
            return false;
        const QModelIndex idx = m_issueTable->indexAt(ev->pos());
        if (!idx.isValid() || idx.column() != kProgressCol ||
            !m_issueTable->item(idx.row(), kProgressCol))
            return false;
        m_issueProgressDragRow = idx.row();
        applyIssueProgressDragAt(ev->pos());
        return true; // consume so the click doesn't select/open the row
    }
    if (m_issueProgressDragRow < 0)
        return false; // not a progress drag we started
    if (ev->type() == QEvent::MouseMove) {
        if (!(ev->buttons() & Qt::LeftButton))
            return false;
        applyIssueProgressDragAt(ev->pos());
        return true;
    }
    // MouseButtonRelease: commit the value to the store and refresh.
    commitIssueProgressDrag();
    m_issueProgressDragRow = -1;
    return true;
}

// Live-update the dragged cell's painted percentage (kProgressBarRole) from the
// pointer x, leaving the store write to commitIssueProgressDrag() on release.
void MainWindow::applyIssueProgressDragAt(const QPoint &pos)
{
    QTableWidgetItem *item = m_issueTable->item(m_issueProgressDragRow, 11);
    if (!item)
        return;
    const int pct = progressPctForX(m_issueTable->visualItemRect(item), pos.x(),
                                    m_issueTable->fontMetrics());
    item->setData(kProgressBarRole, pct);
    item->setData(kTableSortRole, pct);
    item->setToolTip(QStringLiteral("%1% complete").arg(pct));
}

void MainWindow::commitIssueProgressDrag()
{
    QTableWidgetItem *progressItem = m_issueTable->item(m_issueProgressDragRow, 11);
    QTableWidgetItem *numberItem = m_issueTable->item(m_issueProgressDragRow, 0);
    if (!progressItem || !numberItem)
        return;
    const int number = numberItem->data(Qt::UserRole).toInt();
    const int pct = qBound(0, progressItem->data(kProgressBarRole).toInt(), 100);
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setProgress(number, pct, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update progress." : error,
                             true);
        return;
    }
    setIssueInlineNotice(
        QStringLiteral("Issue #%1 progress set to %2%.").arg(number).arg(pct));
    reloadIssues();
}

void MainWindow::nudgeIssueProgress(int deltaPercent)
{
    if (m_currentIssueNumber < 0 || deltaPercent == 0)
        return;
    int current = 0;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.number == m_currentIssueNumber) {
            current = qBound(0, issue.progress, 100);
            break;
        }
    const int next = qBound(0, current + deltaPercent, 100);
    if (next == current) {
        setIssueInlineNotice(deltaPercent > 0 ? "Already at 100% complete."
                                              : "Already at 0%.");
        return;
    }
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setProgress(m_currentIssueNumber, next, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update progress." : error,
                             true);
        return;
    }
    setIssueInlineNotice(QStringLiteral("Progress set to %1%.").arg(next));
    reloadIssues();
}

double MainWindow::openAiEstimateUsd(const Issue &issue)
{
    // Rough size of the task in characters: the title plus the description body
    // of the open event (later comments are discussion, not spec).
    int chars = issue.title.size();
    for (const IssueEvent &ev : issue.events) {
        if (ev.type == QLatin1String("open")) {
            chars += ev.body.size();
            break;
        }
    }
    // ~4 chars/token. A coding agent reads the repo for context and writes a
    // patch, so model fixed context overhead plus output that scales with the
    // spec size. OpenAI/Codex price: ~$1.25 input, ~$10 output per 1M tokens.
    const double specTokens = chars / 4.0;
    const double inputTokens = 12000.0 + specTokens * 3.0; // context + re-reads
    const double outputTokens = 3000.0 + specTokens * 2.0; // the patch + messages
    const double usd =
        (inputTokens * 1.25 + outputTokens * 10.0) / 1000000.0;
    return qMax(0.05, usd);
}

void MainWindow::editIssueProgress()
{
    if (m_currentIssueNumber < 0)
        return;
    int current = 0;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.number == m_currentIssueNumber) {
            current = qBound(0, issue.progress, 100);
            break;
        }
    bool ok = false;
    const int progress = QInputDialog::getInt(
        this, QStringLiteral("Set progress"),
        QString::fromUtf8("Percent complete (0\xE2\x80\x93""100):"), current, 0, 100, 5,
        &ok);
    if (!ok)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setProgress(m_currentIssueNumber, progress, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update progress." : error,
                             true);
        return;
    }
    setIssueInlineNotice(QStringLiteral("Progress set to %1%.").arg(progress));
    reloadIssues();
}

int MainWindow::estimateIssueProgress(const Issue &issue,
                                      const QSet<int> &mergedIssues) const
{
    // A closed issue, or one covered by a merged PR, is done.
    if (issue.status == QLatin1String("closed") ||
        mergedIssues.contains(issue.number))
        return 100;
    // Otherwise read the agent's state on the issue: a finished session produced
    // a patch; one still running is partway; nothing yet is 0.
    if (const AgentSession *session = latestAgentSessionForIssue(issue.number)) {
        const QString s = session->status;
        if (s == AgentStatus::Success)
            return session->prNumber > 0 ? 90 : 75;
        if (s == AgentStatus::Running || s == AgentStatus::Waiting)
            return 40;
        if (s == AgentStatus::Queued)
            return 15;
        if (s == AgentStatus::Failed || s == AgentStatus::Stopped)
            return 10;
    }
    // A claimed-but-not-started issue counts as just begun.
    if (!issue.assignees.isEmpty())
        return 10;
    return 0;
}

void MainWindow::reprioritizeBacklog()
{
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        setIssueInlineNotice("This repo is read-only here; can't reprioritize.", true);
        return;
    }

    // Done-detection input: which issue numbers a merged PR closes/references.
    QSet<int> mergedIssues;
    for (const PullRequest &pr : pullStoreForCurrentRepo().loadAll())
        if (pr.status == QLatin1String("merged"))
            for (const int number : issuesLinkedFromPull(pr))
                mergedIssues.insert(number);

    // Only assign priority to OPEN issues with none set, so a manual triage is
    // never clobbered.
    QList<Issue> todo;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.status != QLatin1String("closed") && issue.priority == 0)
            todo.append(issue);

    // Progress is estimated for every issue that has no value set yet.
    int progressPending = 0;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.progress == 0 && estimateIssueProgress(issue, mergedIssues) > 0)
            ++progressPending;

    if (todo.isEmpty() && progressPending == 0) {
        setIssueInlineNotice("Nothing to triage: priorities and progress are set.");
        return;
    }
    const int answer = QMessageBox::question(
        this, QStringLiteral("Reprioritize backlog"),
        QStringLiteral("Assign a priority and an MVP/Phase 2 label to %1 open, "
                       "unprioritized issue(s), and estimate progress for %2 "
                       "issue(s) from whether the work landed (closed / merged "
                       "PR / agent activity)?\n\nIssues with votes are treated as "
                       "MVP; the rest become Phase 2.")
            .arg(todo.size())
            .arg(progressPending),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;

    // Most-wanted first: more votes, then older (lower number).
    std::sort(todo.begin(), todo.end(), [](const Issue &a, const Issue &b) {
        if (a.votes != b.votes)
            return a.votes > b.votes;
        return a.number < b.number;
    });

    int done = 0, failed = 0, prio = 1;
    for (const Issue &issue : std::as_const(todo)) {
        const bool mvp = issue.votes > 0;
        const QString phaseLabel =
            mvp ? QStringLiteral("MVP") : QStringLiteral("Phase 2");
        QStringList labels = issue.labels;
        labels.removeAll(QStringLiteral("MVP"));
        labels.removeAll(QStringLiteral("Phase 2"));
        labels << phaseLabel;
        QString error;
        const bool okLabels = store.setLabels(issue.number, labels, &error);
        const bool okPrio =
            store.setPriority(issue.number, qMin(99, prio), &error);
        if (okLabels && okPrio)
            ++done;
        else
            ++failed;
        ++prio;
    }

    // Apply estimated progress to issues that don't already carry a value.
    int progressed = 0;
    for (const Issue &issue : std::as_const(m_currentIssues)) {
        if (issue.progress != 0)
            continue;
        const int pct = estimateIssueProgress(issue, mergedIssues);
        if (pct <= 0)
            continue;
        QString error;
        if (store.setProgress(issue.number, pct, &error))
            ++progressed;
    }

    setIssueInlineNotice(
        failed == 0
            ? QStringLiteral("Prioritized %1 issue(s); estimated progress on %2.")
                  .arg(done)
                  .arg(progressed)
            : QStringLiteral("Prioritized %1 issue(s) (%2 failed); estimated "
                             "progress on %3.")
                  .arg(done)
                  .arg(failed)
                  .arg(progressed),
        failed != 0);
    reloadIssues();
}

QString MainWindow::currentRepoReadme() const
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return QString();
    const RepositoryRecord repo = writableRecordFor(m_repositories.at(idx));

    // Prefer the working tree (the same copy issue edits land in): find a
    // README* file, preferring README.md, matching openRepoReadme()'s rule.
    const QString workTree = repo.localPath.trimmed();
    if (!workTree.isEmpty()) {
        QDir dir(workTree);
        QString chosen;
        for (const QString &name : dir.entryList(QDir::Files)) {
            if (!name.startsWith(QStringLiteral("README"), Qt::CaseInsensitive))
                continue;
            if (name.compare(QStringLiteral("README.md"), Qt::CaseInsensitive) ==
                0) {
                chosen = name;
                break;
            }
            if (chosen.isEmpty())
                chosen = name;
        }
        if (!chosen.isEmpty()) {
            QFile file(dir.filePath(chosen));
            if (file.open(QIODevice::ReadOnly))
                return QString::fromUtf8(file.readAll());
        }
    }

    // Fall back to the bare mirror's HEAD tree for repos with no work tree.
    const QString mirror = repo.mirrorPath.trimmed();
    if (!mirror.isEmpty() && QDir(mirror).exists()) {
        for (const QString &name :
             {QStringLiteral("README.md"), QStringLiteral("README"),
              QStringLiteral("readme.md")}) {
            QByteArray out;
            if (runGitCapture(mirror, {"show", "HEAD:" + name}, &out, nullptr) &&
                !out.isEmpty() && !out.contains('\0'))
                return QString::fromUtf8(out);
        }
    }
    return QString();
}

void MainWindow::prioritizeIssuesFromReadme()
{
    if (m_prioritizeInFlight || !m_networkAccess)
        return;

    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        setIssueInlineNotice("This repo is read-only here; can't reprioritize.",
                             true);
        return;
    }

    // Rank only the open issues; closed ones don't need a priority.
    QList<Issue> open;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.status != QLatin1String("closed"))
            open.append(issue);
    if (open.size() < 2) {
        setIssueInlineNotice("Need at least two open issues to prioritize.");
        return;
    }

    QString readme = currentRepoReadme().trimmed();
    if (readme.isEmpty()) {
        setIssueInlineNotice(
            "No README found for this repo to prioritize against.", true);
        return;
    }
    // Keep a large README from blowing the request budget.
    if (readme.size() > 8000)
        readme = readme.left(8000) + QStringLiteral("\n\n[README truncated]");

    // Use the agent picked in the dropdown next to the button; fall back to the
    // saved default agent when the picker isn't built yet.
    const QString provider = m_issuePrioritizeAgentCombo
                                 ? m_issuePrioritizeAgentCombo->currentData()
                                       .toString()
                                 : defaultAgentProvider();
    const bool claude = agentIsClaudeProvider(provider);
    const bool claudeCode = provider == QLatin1String("claude-code");
    // "Claude Code" (issue #294) authenticates with the claude.ai subscription
    // OAuth token the CLI keeps in ~/.claude/.credentials.json, never a metered
    // API key. Read that token first and ignore any stored Claude API key, so an
    // explicit Claude Code selection can't silently fall through to the
    // credit-billed API and fail with "credit balance too low". "Claude API" and
    // "OpenAI API" keep using their respective keys.
    const QString oauthToken = claudeCode ? claudeCodeOAuthToken() : QString();
    const QString apiKey =
        claudeCode ? QString()
                   : (claude ? QSettings().value(kClaudeApiKeySetting)
                             : QSettings().value(kCodexApiKeySetting))
                         .toString()
                         .trimmed();
    if (apiKey.isEmpty() && oauthToken.isEmpty()) {
        setIssueInlineNotice(
            claudeCode
                ? "Sign in to Claude Code first (run `claude` and log in)."
                : claude ? "Add a Claude API key in Settings first."
                         : "Add an OpenAI API key in Settings first.",
            true);
        return;
    }

    // One line per open issue: "#N: title - opening snippet".
    QStringList lines;
    for (const Issue &issue : std::as_const(open)) {
        QString line =
            QStringLiteral("#%1: %2").arg(issue.number).arg(issue.title.trimmed());
        QString body;
        for (const IssueEvent &ev : issue.events)
            if (ev.type == QLatin1String("open")) {
                body = ev.body.trimmed();
                break;
            }
        if (!body.isEmpty()) {
            body = body.simplified();
            if (body.size() > 200)
                body = body.left(200) + QString::fromUtf8("\xE2\x80\xA6");
            line += QString::fromUtf8(" \xE2\x80\x94 ") + body;
        }
        lines << line;
    }

    const QString task =
        QStringLiteral(
            "%1\n\n----- README -----\n%2\n\n----- OPEN ISSUES -----\n%3")
            .arg(prioritizePromptSetting(), readme, lines.join('\n'));
    const QString model =
        claude ? QStringLiteral("claude-haiku-4-5") : kIssueAskAiModel;
    // Budget enough output to list every issue number, with headroom.
    const int outTok = qBound(256, open.size() * 8 + 256, 4000);

    QNetworkReply *reply = nullptr;
    if (claude) {
        QJsonObject payload;
        payload.insert("model", model);
        payload.insert("max_tokens", outTok);
        QJsonArray messages;
        QJsonObject um;
        um.insert("role", "user");
        um.insert("content", task);
        messages.append(um);
        payload.insert("messages", messages);
        QNetworkRequest req(
            QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
        if (!oauthToken.isEmpty()) {
            // Claude Code's subscription OAuth token authenticates with a Bearer
            // header and requires the Claude Code system identity.
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
        payload.insert("max_output_tokens", outTok);
        QNetworkRequest req = openAiRequest(
            QUrl(QStringLiteral("https://api.openai.com/v1/responses")), apiKey);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }

    m_prioritizeInFlight = true;
    if (m_issuePrioritizeButton) {
        m_issuePrioritizeButton->setEnabled(false);
        m_issuePrioritizeButton->setText(
            QString::fromUtf8("Prioritizing\xE2\x80\xA6"));
    }
    setIssueInlineNotice(
        QString::fromUtf8(claude ? "Asking Claude to prioritize\xE2\x80\xA6"
                                 : "Asking OpenAI to prioritize\xE2\x80\xA6"));

    connect(reply, &QNetworkReply::finished, this, [this, reply, claude] {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        m_prioritizeInFlight = false;
        if (m_issuePrioritizeButton) {
            m_issuePrioritizeButton->setEnabled(true);
            m_issuePrioritizeButton->setText("Prioritize from README");
        }
        if (reply->error() != QNetworkReply::NoError) {
            setIssueInlineNotice(
                "Prioritize request failed: " + apiErrorSummary(reply, body),
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

        // The reply should be a JSON array of issue numbers, highest priority
        // first. Slice out the first [...] so stray prose or code fences don't
        // break parsing.
        const int lb = text.indexOf('[');
        const int rb = text.lastIndexOf(']');
        const QJsonArray order =
            (lb >= 0 && rb > lb)
                ? QJsonDocument::fromJson(text.mid(lb, rb - lb + 1).toUtf8())
                      .array()
                : QJsonArray();
        if (order.isEmpty()) {
            setIssueInlineNotice(
                "Could not read a priority list from the agent's response.",
                true);
            return;
        }

        IssueStore writeStore = issueStoreForCurrentRepo();
        if (!writeStore.canWrite()) {
            setIssueInlineNotice(
                "This repo is read-only here; can't reprioritize.", true);
            return;
        }
        // Only touch numbers that are still open, and dedupe a repeated number.
        QSet<int> openNow;
        for (const Issue &issue : std::as_const(m_currentIssues))
            if (issue.status != QLatin1String("closed"))
                openNow.insert(issue.number);

        int prio = 1, applied = 0, failed = 0;
        QSet<int> seen;
        for (const QJsonValue &v : order) {
            const int number = v.toInt(-1);
            if (number < 0 || seen.contains(number) || !openNow.contains(number))
                continue;
            seen.insert(number);
            QString error;
            if (writeStore.setPriority(number, qMin(99, prio), &error))
                ++applied;
            else
                ++failed;
            ++prio;
        }

        if (applied == 0) {
            setIssueInlineNotice("No open issues matched the agent's ranking.",
                                 true);
            return;
        }
        setIssueInlineNotice(
            failed == 0
                ? QStringLiteral("Prioritized %1 issue(s) from the README.")
                      .arg(applied)
                : QStringLiteral(
                      "Prioritized %1 issue(s) from the README (%2 failed).")
                      .arg(applied)
                      .arg(failed),
            failed != 0);
        reloadIssues();
    });
}

void MainWindow::analyzeIssueCompleteness()
{
    if (m_completenessInFlight || !m_networkAccess)
        return;

    // The verdict is written back onto each issue (a completeness label plus a
    // progress estimate), so the repo must be writable here.
    if (!issueStoreForCurrentRepo().canWrite()) {
        setIssueInlineNotice("This repo is read-only here; can't update issues.",
                             true);
        return;
    }

    // Judge the open issues; closed ones don't need completeness rated.
    QList<Issue> open;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.status != QLatin1String("closed"))
            open.append(issue);
    if (open.isEmpty()) {
        setIssueInlineNotice("No open issues to analyze.");
        return;
    }

    // README is context only here, so it's optional: include it when present,
    // otherwise judge the issues on their own.
    QString readme = currentRepoReadme().trimmed();
    if (readme.size() > 8000)
        readme = readme.left(8000) + QStringLiteral("\n\n[README truncated]");

    // Repository file listing so the agent can judge whether each issue's feature
    // is actually implemented rather than guessing from the issue text alone
    // (adhoc #200). Tracked files from the work tree's HEAD; capped for the prompt.
    QString fileTree;
    {
        const int repoIdx = issuesRepoIndex();
        if (repoIdx >= 0) {
            const RepositoryRecord repo =
                writableRecordFor(m_repositories.at(repoIdx));
            QByteArray out;
            if (!repo.localPath.trimmed().isEmpty() &&
                runGitCapture(repo.localPath, {"ls-files"}, &out, nullptr))
                fileTree = QString::fromUtf8(out).trimmed();
        }
    }
    if (fileTree.size() > 12000)
        fileTree = fileTree.left(12000) + QStringLiteral("\n[file list truncated]");

    // Use the agent picked in the dropdown shared with "Prioritize from README";
    // fall back to the saved default agent when the picker isn't built yet.
    const QString provider = m_issuePrioritizeAgentCombo
                                 ? m_issuePrioritizeAgentCombo->currentData()
                                       .toString()
                                 : defaultAgentProvider();
    const bool claude = agentIsClaudeProvider(provider);
    const bool claudeCode = provider == QLatin1String("claude-code");
    // "Claude Code" authenticates with the claude.ai subscription OAuth token,
    // never a metered API key (see prioritizeIssuesFromReadme for the rationale).
    const QString oauthToken = claudeCode ? claudeCodeOAuthToken() : QString();
    const QString apiKey =
        claudeCode ? QString()
                   : (claude ? QSettings().value(kClaudeApiKeySetting)
                             : QSettings().value(kCodexApiKeySetting))
                         .toString()
                         .trimmed();
    if (apiKey.isEmpty() && oauthToken.isEmpty()) {
        setIssueInlineNotice(
            claudeCode
                ? "Sign in to Claude Code first (run `claude` and log in)."
                : claude ? "Add a Claude API key in Settings first."
                         : "Add an OpenAI API key in Settings first.",
            true);
        return;
    }

    // One line per open issue: "#N: title - opening snippet".
    QStringList lines;
    for (const Issue &issue : std::as_const(open)) {
        QString line =
            QStringLiteral("#%1: %2").arg(issue.number).arg(issue.title.trimmed());
        QString body;
        for (const IssueEvent &ev : issue.events)
            if (ev.type == QLatin1String("open")) {
                body = ev.body.trimmed();
                break;
            }
        if (!body.isEmpty()) {
            body = body.simplified();
            if (body.size() > 400)
                body = body.left(400) + QString::fromUtf8("\xE2\x80\xA6");
            line += QString::fromUtf8(" \xE2\x80\x94 ") + body;
        }
        lines << line;
    }

    const QString readmeSection =
        readme.isEmpty()
            ? QStringLiteral("(no README found)")
            : readme;
    const QString filesSection =
        fileTree.isEmpty() ? QStringLiteral("(file listing unavailable)") : fileTree;
    const QString task =
        QStringLiteral("%1\n\n----- README -----\n%2\n\n----- REPOSITORY FILES "
                       "-----\n%3\n\n----- OPEN ISSUES -----\n%4")
            .arg(completenessPrompt(), readmeSection, filesSection,
                 lines.join('\n'));
    // Opus does the implementation-vs-issue reasoning; the user asked for it by
    // name so the percentages and comments are grounded in the actual code.
    const QString model =
        claude ? QStringLiteral("claude-opus-4-8") : kIssueAskAiModel;
    // Budget enough output for a per-issue comment plus the verdict, with headroom.
    const int outTok = qBound(1024, open.size() * 220 + 512, 8000);

    QNetworkReply *reply = nullptr;
    if (claude) {
        QJsonObject payload;
        payload.insert("model", model);
        payload.insert("max_tokens", outTok);
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
        payload.insert("max_output_tokens", outTok);
        QNetworkRequest req = openAiRequest(
            QUrl(QStringLiteral("https://api.openai.com/v1/responses")), apiKey);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }

    m_completenessInFlight = true;
    if (m_issueCompletenessButton) {
        m_issueCompletenessButton->setEnabled(false);
        m_issueCompletenessButton->setText(
            QString::fromUtf8("Analyzing\xE2\x80\xA6"));
    }
    setIssueInlineNotice(QString::fromUtf8(
        claude ? "Asking Claude to analyze completeness\xE2\x80\xA6"
               : "Asking OpenAI to analyze completeness\xE2\x80\xA6"));

    connect(reply, &QNetworkReply::finished, this, [this, reply, claude] {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        m_completenessInFlight = false;
        if (m_issueCompletenessButton) {
            m_issueCompletenessButton->setEnabled(true);
            m_issueCompletenessButton->setText("Analyze completeness");
        }
        if (reply->error() != QNetworkReply::NoError) {
            setIssueInlineNotice(
                "Completeness request failed: " + apiErrorSummary(reply, body),
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
            setIssueInlineNotice(
                "The agent returned an empty completeness report.", true);
            return;
        }

        // The reply should be a JSON array of verdicts. Slice out the first
        // [...] so stray prose or code fences don't break parsing.
        const int lb = text.indexOf('[');
        const int rb = text.lastIndexOf(']');
        const QJsonArray verdicts =
            (lb >= 0 && rb > lb)
                ? QJsonDocument::fromJson(text.mid(lb, rb - lb + 1).toUtf8())
                      .array()
                : QJsonArray();
        if (verdicts.isEmpty()) {
            setIssueInlineNotice(
                "Could not read completeness verdicts from the agent's response.",
                true);
            return;
        }

        IssueStore writeStore = issueStoreForCurrentRepo();
        if (!writeStore.canWrite()) {
            setIssueInlineNotice(
                "This repo is read-only here; can't update issues.", true);
            return;
        }

        // Index the still-open issues so we apply only to current numbers and
        // can fold each verdict's label into the issue's existing labels.
        QHash<int, Issue> openByNumber;
        for (const Issue &issue : std::as_const(m_currentIssues))
            if (issue.status != QLatin1String("closed"))
                openByNumber.insert(issue.number, issue);

        // The mutually-exclusive completeness labels we manage; the chosen one
        // replaces any previously applied so re-running re-labels cleanly.
        static const QStringList kCompletenessLabels = {
            QStringLiteral("Complete"), QStringLiteral("Partial"),
            QStringLiteral("Incomplete")};

        const QString dash = QString::fromUtf8(" \xE2\x80\x94 ");
        int applied = 0, failed = 0;
        QStringList reportLines;
        for (const QJsonValue &v : verdicts) {
            const QJsonObject o = v.toObject();
            const int number = o.value("number").toInt(-1);
            if (!openByNumber.contains(number))
                continue;
            // Normalise the rating; "incomplete" contains "complete", so test it
            // first.
            const QString rating = o.value("rating").toString().toLower();
            QString label;
            if (rating.contains(QLatin1String("incomplete")))
                label = QStringLiteral("Incomplete");
            else if (rating.contains(QLatin1String("complete")))
                label = QStringLiteral("Complete");
            else if (rating.contains(QLatin1String("partial")))
                label = QStringLiteral("Partial");
            const int pct = qBound(0, o.value("completeness").toInt(), 100);
            // "comment" is the new field; fall back to the legacy "reason" key so
            // an older-style response still yields a note.
            QString comment = o.value("comment").toString().trimmed();
            if (comment.isEmpty())
                comment = o.value("reason").toString().trimmed();

            QStringList labels = openByNumber.value(number).labels;
            for (const QString &cl : kCompletenessLabels)
                labels.removeAll(cl);
            if (!label.isEmpty())
                labels << label;

            QString error;
            const bool okLabels =
                label.isEmpty() ? true
                                : writeStore.setLabels(number, labels, &error);
            const bool okProgress = writeStore.setProgress(number, pct, &error);
            // Post the short verdict as a comment on the issue so it's visible in
            // the thread, not just this run's dialog (adhoc #200).
            bool okComment = true;
            if (!comment.isEmpty()) {
                const QString commentBody =
                    QStringLiteral("**Completeness analysis:** %1 (%2%)\n\n%3")
                        .arg(label.isEmpty() ? QStringLiteral("?") : label)
                        .arg(pct)
                        .arg(comment);
                okComment = writeStore.addComment(number, commentBody, {}, &error);
            }
            if (okLabels && okProgress && okComment)
                ++applied;
            else
                ++failed;

            reportLines
                << QStringLiteral("- #%1 **%2**")
                           .arg(number)
                           .arg(label.isEmpty() ? QStringLiteral("?") : label) +
                       dash + QStringLiteral("%1%").arg(pct) + dash +
                       (comment.isEmpty() ? QStringLiteral("(no detail)")
                                          : comment);
        }

        if (applied == 0) {
            setIssueInlineNotice("No open issues matched the agent's verdicts.",
                                 true);
            return;
        }

        reloadIssues();
        setIssueInlineNotice(
            failed == 0
                ? QStringLiteral(
                      "Updated %1 issue(s) from the completeness analysis.")
                      .arg(applied)
                : QStringLiteral("Updated %1 issue(s) (%2 failed) from the "
                                 "completeness analysis.")
                      .arg(applied)
                      .arg(failed),
            failed != 0);

        // Show what was applied, per issue, in a read-only report dialog.
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Issue completeness"));
        dialog.resize(560, 480);
        auto *layout = new QVBoxLayout(&dialog);
        auto *view = new QTextBrowser(&dialog);
        view->setOpenExternalLinks(true);
        view->setMarkdown(reportLines.join('\n'));
        layout->addWidget(view);
        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog,
                &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog,
                &QDialog::accept);
        layout->addWidget(buttons);
        dialog.exec();
    });
}

void MainWindow::pickIssueAssignees()
{
    if (m_currentIssueNumber < 0 || !m_issueAssigneesButton)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());

    // The line edit mirrors the issue's current assignees; treat it as the
    // source of truth so manual edits and the "Assign yourself" link round-trip.
    const QStringList current = m_issueAssigneesEdit
                                    ? splitIssueFieldList(m_issueAssigneesEdit->text())
                                    : QStringList();
    QSet<QString> selected(current.begin(), current.end());

    // Candidates: every known mesh node, then any current assignee that isn't a
    // known node (a name typed by hand) so opening the picker never drops it.
    struct Candidate {
        QString name;
        QString platform;
        bool online = false;
        bool self = false;
    };
    QList<Candidate> candidates;
    QSet<QString> seen;
    for (const NodeMenuEntry &e : std::as_const(m_nodeMenuEntries)) {
        if (e.name.isEmpty() || seen.contains(e.name))
            continue;
        seen.insert(e.name);
        candidates.append({e.name, e.platform, e.online, e.self});
    }
    for (const QString &a : current) {
        if (!a.isEmpty() && !seen.contains(a)) {
            seen.insert(a);
            candidates.append({a, QString(), false, false});
        }
    }

    QMenu menu(this);
    QAction *header = menu.addAction(QStringLiteral("Assign nodes"));
    header->setEnabled(false);

    auto *searchEdit = new QLineEdit(&menu);
    searchEdit->setPlaceholderText(QStringLiteral("Search nodes") +
                                   QString::fromUtf8("\xE2\x80\xA6"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(240);
    auto *searchAction = new QWidgetAction(&menu);
    searchAction->setDefaultWidget(searchEdit);
    menu.addAction(searchAction);
    menu.addSeparator();

    if (candidates.isEmpty()) {
        QAction *empty = menu.addAction(QStringLiteral("No nodes yet"));
        empty->setEnabled(false);
    }

    // A checkable list (not menu actions) so ticking several nodes in a row
    // doesn't dismiss the popup the way a normal checkable QAction would.
    auto *listWidget = new QListWidget(&menu);
    listWidget->setObjectName("assigneePickerList");
    listWidget->setMinimumWidth(240);
    listWidget->setMaximumHeight(320);
    listWidget->setFrameShape(QFrame::NoFrame);
    for (const Candidate &c : std::as_const(candidates)) {
        QString text = c.name;
        if (c.self)
            text += QStringLiteral(" (you)");
        auto *item = new QListWidgetItem(osBadgeIcon(c.platform, c.online, 16),
                                         text, listWidget);
        item->setData(Qt::UserRole, c.name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selected.contains(c.name) ? Qt::Checked : Qt::Unchecked);
    }
    // Connect after populating so the setCheckState calls above don't churn the
    // working set (it already starts equal to the issue's assignees).
    connect(listWidget, &QListWidget::itemChanged, &menu,
            [&selected](QListWidgetItem *item) {
                const QString name = item->data(Qt::UserRole).toString();
                if (item->checkState() == Qt::Checked)
                    selected.insert(name);
                else
                    selected.remove(name);
            });
    auto *listAction = new QWidgetAction(&menu);
    listAction->setDefaultWidget(listWidget);
    menu.addAction(listAction);

    connect(searchEdit, &QLineEdit::textChanged, listWidget,
            [listWidget](const QString &text) {
                const QString needle = text.trimmed().toLower();
                for (int i = 0; i < listWidget->count(); ++i) {
                    QListWidgetItem *it = listWidget->item(i);
                    const QString name = it->data(Qt::UserRole).toString().toLower();
                    it->setHidden(!needle.isEmpty() && !name.contains(needle));
                }
            });
    QTimer::singleShot(0, searchEdit, [searchEdit] { searchEdit->setFocus(); });

    menu.exec(m_issueAssigneesButton->mapToGlobal(
        QPoint(0, m_issueAssigneesButton->height())));

    // Persist once on close, and only if something actually changed, so an
    // open-and-cancel doesn't log a no-op "set assignees" activity entry.
    const QSet<QString> before(current.begin(), current.end());
    if (selected == before || !m_issueAssigneesEdit)
        return;
    // Keep existing assignees in their current order; append newly-ticked nodes
    // in node-list order.
    QStringList result;
    for (const QString &a : current)
        if (selected.contains(a))
            result << a;
    for (const Candidate &c : std::as_const(candidates))
        if (selected.contains(c.name) && !result.contains(c.name))
            result << c.name;
    m_issueAssigneesEdit->setText(result.join(", "));
    saveIssueAssigneesInline();
}

void MainWindow::editIssueAssignees()
{
    if (m_currentIssueNumber < 0)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());
    if (m_issueAssigneesStack)
        m_issueAssigneesStack->setCurrentIndex(1);
    if (m_issueAssigneesEdit) {
        m_issueAssigneesEdit->setFocus();
        m_issueAssigneesEdit->selectAll();
    }
}

void MainWindow::saveIssueAssigneesInline()
{
    if (m_currentIssueNumber < 0 || !m_issueAssigneesEdit)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    const QStringList assignees = splitIssueFieldList(m_issueAssigneesEdit->text());
    if (!store.setAssignees(m_currentIssueNumber, assignees, &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update assignees." : error,
                             true);
        return;
    }
    setIssueInlineNotice("Assignees updated.");
    reloadIssues();
}

void MainWindow::cancelIssueSidebarEditors()
{
    if (m_issueAssigneesStack)
        m_issueAssigneesStack->setCurrentIndex(0);
    if (m_issueLabelsStack)
        m_issueLabelsStack->setCurrentIndex(0);
    if (m_issueMilestoneStack)
        m_issueMilestoneStack->setCurrentIndex(0);
    if (m_issuePriorityStack)
        m_issuePriorityStack->setCurrentIndex(0);
}

QUrl MainWindow::issuesApiUrl(const RepositoryRecord &repo) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/issues");
    return url;
}

QUrl MainWindow::bountyApiUrl(const RepositoryRecord &repo) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/bounty");
    return url;
}

QUrl MainWindow::sharesApiUrl(const RepositoryRecord &repo) const
{
    // Private-repo collaborator ACL endpoint (issue #9).
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/shares");
    return url;
}

void MainWindow::shareRepoRequest(const RepositoryRecord &repo,
                                  const QString &grantee, const QString &action)
{
    // Grant ("add") or revoke ("remove") a collaborator on a private repo
    // (issue #9). Owner-signed: only the repo owner may change the ACL. The
    // action is bound into the signature so an add token can't be replayed as a
    // remove and vice versa (matches the relay's forkmesh-share-v1 canonical).
    if (!m_networkAccess || !m_profileIdentity.isValid())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-share-v1\n" + repo.owner + "\n" + repo.name + "\n" + grantee +
         "\n" + action + "\n" + ts).toUtf8();
    const QJsonObject payload{{"action", action},
                              {"grantee", grantee},
                              {"ts", ts},
                              {"sig", m_profileIdentity.signData(canonical)}};
    QNetworkRequest request(sharesApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, grantee, action] {
                const QByteArray body = reply->readAll();
                const auto err = reply->error();
                const QString errStr = reply->errorString();
                reply->deleteLater();
                const QJsonObject obj = QJsonDocument::fromJson(body).object();
                if (err != QNetworkReply::NoError || !obj.value("ok").toBool()) {
                    flashMessage(
                        QStringLiteral("Could not update collaborators: %1")
                            .arg(obj.value("error").toString(errStr)),
                        true);
                    return;
                }
                logSystem(QStringLiteral("%1 collaborator %2.")
                              .arg(action == QLatin1String("add") ? "Added"
                                                                  : "Removed",
                                   grantee));
                refreshRepoCollaborators();
            });
}

void MainWindow::addRepoCollaborator(const QString &nameRaw)
{
    const QString grantee = nameRaw.trimmed().toLower();
    if (grantee.isEmpty())
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    if (grantee == repo.owner) {
        flashMessage(QStringLiteral("A repository is already readable by its "
                                    "owner."),
                     true);
        return;
    }
    shareRepoRequest(repo, grantee, QStringLiteral("add"));
    if (m_collabEdit)
        m_collabEdit->clear();
}

void MainWindow::removeRepoCollaborator(const QString &nameRaw)
{
    const QString grantee = nameRaw.trimmed().toLower();
    if (grantee.isEmpty())
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    shareRepoRequest(m_repositories.at(m_repoDetailIndex), grantee,
                     QStringLiteral("remove"));
}

void MainWindow::refreshRepoCollaborators()
{
    if (!m_collabSection)
        return;
    const bool haveRepo =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size();
    // Sharing only applies to a private repo this node owns and has published —
    // the ACL lives on the relay, so an unpublished or mirrored repo has none.
    const RepositoryRecord blank;
    const RepositoryRecord &repo =
        haveRepo ? m_repositories.at(m_repoDetailIndex) : blank;
    const bool show = haveRepo && !accountOwner().isEmpty() &&
                      repo.owner == accountOwner() && repo.isPrivate &&
                      repo.publishToNetwork;
    m_collabSection->setVisible(show);
    if (m_collabList)
        m_collabList->clear();
    if (!show || !m_networkAccess || !m_profileIdentity.isValid())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-shares-list-v1\n" + repo.owner + "\n" + repo.name + "\n" + ts)
            .toUtf8();
    QUrl url = sharesApiUrl(repo);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ts"), ts);
    query.addQueryItem(QStringLiteral("sig"),
                       m_profileIdentity.signData(canonical));
    url.setQuery(query);
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!m_collabList)
            return;
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        m_collabList->clear();
        const QJsonArray grantees = obj.value("grantees").toArray();
        for (const QJsonValue &v : grantees)
            m_collabList->addItem(v.toString());
        if (m_collabEmptyHint)
            m_collabEmptyHint->setVisible(grantees.isEmpty());
    });
}

void MainWindow::showBountyQrDialog(const RepositoryRecord &repo, int number,
                                    const QString &uri, const QString &address,
                                    double amountUsd, const QString &amountSol)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Fund bounty"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(
        QString::fromUtf8("The pull request is merged. Send <b>%1 SOL</b> (\xE2\x89\x88 "
                       "$%2) to this escrow address to fund the bounty. On "
                       "confirmation, 90%% is paid to the pull request author and "
                       "10%% to the ForkMesh treasury.")
            .arg(amountSol.isEmpty() ? QStringLiteral("…") : amountSol,
                 QString::number(amountUsd, 'f', 2)));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);
    // The Solana Pay URI bakes in the amount, so it's long and yields a
    // high-version (many-module) QR. At a fixed scale that overflows the dialog
    // and gets clipped, so size each module to the largest integer that keeps
    // the whole code within the dialog width (and crisp).
    const auto modules = QrCode::encode(uri.toUtf8());
    if (!modules.empty()) {
        constexpr int kMargin = 3;
        constexpr int kMaxQrPx = 300;
        const int span = static_cast<int>(modules.size()) + 2 * kMargin;
        const int scale = qMax(2, kMaxQrPx / span);
        const QImage qr = QrCode::encodeToImage(uri, scale, kMargin);
        auto *qrLabel = new QLabel;
        qrLabel->setPixmap(QPixmap::fromImage(qr));
        qrLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(qrLabel);
    }
    auto *addr = new QLabel(address);
    addr->setObjectName("statusLine");
    addr->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addr->setAlignment(Qt::AlignCenter);
    addr->setWordWrap(true);
    layout->addWidget(addr);

    auto *status = new QLabel(QStringLiteral("Waiting for the deposit…"));
    status->setObjectName("modeHint");
    status->setWordWrap(true);
    status->setAlignment(Qt::AlignCenter);
    layout->addWidget(status);

    auto *copyBtn = new QPushButton(QStringLiteral("Copy address"));
    connect(copyBtn, &QPushButton::clicked, this, [address] {
        QGuiApplication::clipboard()->setText(address);
    });
    auto *closeBtn = new QPushButton(QStringLiteral("Done"));
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto *row = new QHBoxLayout;
    row->addWidget(copyBtn);
    row->addStretch();
    row->addWidget(closeBtn);
    layout->addLayout(row);

    // Poll the escrow like the signup donation flow: show the received balance,
    // and when the worker confirms + splits it (status "paid"), record the paid
    // state on the issue and report the payout tx.
    bool paid = false;
    auto *poll = new QTimer(&dialog);
    poll->setInterval(4000);
    connect(poll, &QTimer::timeout, &dialog, [&, this]() {
        if (!m_networkAccess)
            return;
        const QJsonObject payload{{"action", "status"},
                                  {"owner", repo.owner},
                                  {"repo", repo.name},
                                  {"number", number}};
        QNetworkRequest request(bountyApiUrl(repo));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *reply = m_networkAccess->post(
            request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        if (obj.value("status").toString() == QLatin1String("paid")) {
            paid = true;
            poll->stop();
            IssueStore writeStore = issueStoreForCurrentRepo();
            QString err;
            writeStore.setBounty(number, amountUsd, obj.value("payee").toString(),
                                 QStringLiteral("paid"), &err);
            if (m_repoDetailIndex == issuesRepoIndex())
                reloadIssues();
            status->setText(
                QStringLiteral("Paid out to the author + treasury (tx %1).")
                    .arg(obj.value("payoutSig").toString().left(12)));
            status->setStyleSheet("color:#3fb950; background:transparent;");
            closeBtn->setText(QStringLiteral("Close"));
            return;
        }
        const qint64 got =
            obj.value("receivedLamports").toVariant().toLongLong();
        if (got > 0)
            status->setText(QStringLiteral("Received %1 SOL — confirming…")
                                .arg(got / 1000000000.0, 0, 'f', 9));
    });
    poll->start();
    dialog.exec();
    poll->stop();
    // Closed before the deposit confirmed: keep watching in the background so the
    // issue is still marked paid once the funds land (the worker cron is the
    // final backstop regardless).
    if (!paid)
        pollBountyPayout(repo, number, amountUsd);
}

void MainWindow::editIssueBounty()
{
    if (m_currentIssueNumber < 0)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        setIssueInlineNotice("This repo is read-only here; can't add a bounty.", true);
        return;
    }
    double existing = 0.0;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.number == m_currentIssueNumber) {
            existing = issue.bountyUsd;
            break;
        }
    bool ok = false;
    const double amount = QInputDialog::getDouble(
        this, QStringLiteral("Add bounty"),
        QStringLiteral("Bounty amount (USD):"), existing > 0 ? existing : 10.0,
        1.0, 100000.0, 2, &ok);
    if (!ok)
        return;

    // Pledge only — no money changes hands now. The escrow address is minted and
    // its funding QR is shown when a pull request that closes the issue is
    // merged (see fundBountiesForMergedPull), so no worker call is needed here.
    QString error;
    if (!store.setBounty(m_currentIssueNumber, amount, QString(),
                         QStringLiteral("open"), &error)) {
        setIssueInlineNotice(
            error.isEmpty() ? "Could not record the bounty." : error, true);
        return;
    }
    setIssueInlineNotice(
        QStringLiteral("Bounty of $%1 pledged. You'll fund it with a QR when the "
                       "issue's pull request is merged.")
            .arg(QString::number(amount, 'f', 2)));
    reloadIssues();
}

void MainWindow::bountyAllOpenIssues(double amountUsd)
{
    if (amountUsd < 1.0)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        setIssueInlineNotice("This repo is read-only here; can't add bounties.", true);
        return;
    }
    int openCount = 0;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.status != QLatin1String("closed"))
            ++openCount;
    if (openCount == 0) {
        setIssueInlineNotice("No open issues to add a bounty to.", true);
        return;
    }
    if (QMessageBox::question(
            this, QStringLiteral("Bounty all issues"),
            QStringLiteral("Pledge a $%1 bounty on all %2 open issue(s)?\n\nBounties "
                           "are funded when each issue's pull request is merged.")
                .arg(QString::number(amountUsd, 'f', 2))
                .arg(openCount)) != QMessageBox::Yes)
        return;

    // Pledge only on every open issue (same model as single-issue bounties —
    // funded on merge, no money moves now).
    int applied = 0;
    int failed = 0;
    for (const Issue &issue : std::as_const(m_currentIssues)) {
        if (issue.status == QLatin1String("closed"))
            continue;
        QString error;
        if (store.setBounty(issue.number, amountUsd, QString(),
                            QStringLiteral("open"), &error))
            ++applied;
        else
            ++failed;
    }
    setIssueInlineNotice(
        failed == 0
            ? QStringLiteral("Pledged a $%1 bounty on %2 open issue(s).")
                  .arg(QString::number(amountUsd, 'f', 2))
                  .arg(applied)
            : QStringLiteral("Pledged a $%1 bounty on %2 issue(s) (%3 failed).")
                  .arg(QString::number(amountUsd, 'f', 2))
                  .arg(applied)
                  .arg(failed),
        failed != 0);
    reloadIssues();
}

void MainWindow::submitIssueCommentToInbox(const QString &body)
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);

    QString text = body;
    while (text.endsWith('\n') || text.endsWith('\r'))
        text.chop(1);

    IssueStore store = issueStoreForCurrentRepo();
    IssueEvent ev;
    ev.type = "comment";
    ev.body = text;
    ev = store.makeSignedEvent(m_currentIssueNumber, ev);
    // bodyFile isn't part of the signature; name it after the (now-assigned) id
    // so the maintainer's node stores it predictably.
    ev.bodyFile = "comments/" + ev.id + ".md";

    QJsonObject eventJson = ev.toJson();
    eventJson.insert("body", ev.body); // worker needs the text to verify the sig
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", m_currentIssueNumber},
                              {"event", eventJson}};

    QNetworkRequest request(issuesApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            if (m_issueComposer) {
                m_issueComposer->setMarkdown(QString());
                m_issueComposer->clearPendingAttachments();
            }
            setIssueInlineNotice(
                "Your signed comment was delivered to the maintainer's inbox.");
        } else {
            setIssueInlineNotice("Could not send the comment: " + reply->errorString(),
                                 true);
        }
    });
}

void MainWindow::submitIssueAssigneesToInbox(int number,
                                             const QStringList &assignees)
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);

    IssueStore store = issueStoreForCurrentRepo();
    IssueEvent ev;
    ev.type = "assignees";
    ev.assignees = assignees;
    ev = store.makeSignedEvent(number, ev);

    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", number},
                              {"event", ev.toJson()}};

    QNetworkRequest request(issuesApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply] { reply->deleteLater(); });
}

bool MainWindow::submitNewIssueToInbox(const QString &title, const QString &body,
                                       const QStringList &labels,
                                       const QString &milestone, int priority,
                                       const QStringList &assignees)
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return false;
    const RepositoryRecord &repo = m_repositories.at(idx);

    // Propose the next number from the mirror's view; the owner reassigns it if
    // it collides with an issue we haven't synced yet (the signature is advisory
    // once the owner commits and vouches for the merge).
    int proposed = 1;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.number >= proposed)
            proposed = issue.number + 1;

    IssueStore store = issueStoreForCurrentRepo();
    IssueEvent ev;
    ev.type = "open";
    ev.id = QStringLiteral("open-%1").arg(proposed);
    ev.title = title;
    ev.body = body;
    while (ev.body.endsWith('\n') || ev.body.endsWith('\r'))
        ev.body.chop(1);
    ev = store.makeSignedEvent(proposed, ev);

    QJsonObject eventJson = ev.toJson();
    eventJson.insert("body", ev.body); // worker needs the text to verify the sig
    // Issue-level metadata isn't part of the open-event signature; the owner
    // applies it on merge (vouched, like the rest of an accepted submission).
    QJsonObject meta{{"labels", QJsonArray::fromStringList(labels)},
                     {"milestone", milestone},
                     {"priority", priority},
                     {"assignees", QJsonArray::fromStringList(assignees)}};
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", proposed},
                              {"titleIfNew", title},
                              {"event", eventJson},
                              {"meta", meta}};

    QNetworkRequest request(issuesApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            setIssueInlineNotice("Could not send the issue: " + reply->errorString(),
                                 true);
    });
    return true;
}

int MainWindow::availableCredits() const
{
    const qint64 live =
        m_connectedAtMs > 0 ? QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs : 0;
    const int earned = int((m_totalConnectionMs + live) / 3600000); // 1 per hour
    const int spent = QSettings().value(kVotesSpentSetting).toInt();
    return std::max(0, earned - spent);
}

void MainWindow::submitIssueVoteToInbox()
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);
    IssueStore store = issueStoreForCurrentRepo();
    IssueEvent ev;
    ev.type = "vote";
    ev = store.makeSignedEvent(m_currentIssueNumber, ev);
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", m_currentIssueNumber},
                              {"event", ev.toJson()}};
    QNetworkRequest request(issuesApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            setIssueInlineNotice("Could not send your vote: " + reply->errorString(),
                                 true);
    });
}

void MainWindow::voteOnCurrentIssue()
{
    const int idx = issuesRepoIndex();
    if (idx < 0 || m_currentIssueNumber < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);
    const QString key = repo.owner + "/" + repo.name + "#" +
                        QString::number(m_currentIssueNumber);
    QStringList voted = QSettings().value(kVotedSetting).toStringList();
    // Repeat voting is allowed now; you may keep voting as long as you have
    // credits (each vote spends one).
    if (availableCredits() <= 0) {
        setIssueInlineNotice(
            "No voting credits yet. You earn 1 credit for every hour online.",
            true);
        return;
    }

    IssueStore store = issueStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        if (!store.addVote(m_currentIssueNumber, &error)) {
            setIssueInlineNotice(error.isEmpty() ? "Could not record your vote." : error,
                                 true);
            return;
        }
    } else {
        // Not the host: submit a signed vote to the maintainer's inbox.
        submitIssueVoteToInbox();
        setIssueInlineNotice("Your signed vote was sent to the maintainer's inbox.");
    }

    // Spend a credit; credits are the only limit on voting now. We still note
    // which issues you've voted on (deduped) for reference, but it no longer
    // blocks further votes.
    QSettings s;
    s.setValue(kVotesSpentSetting, s.value(kVotesSpentSetting).toInt() + 1);
    if (!voted.contains(key)) {
        voted << key;
        s.setValue(kVotedSetting, voted);
    }
    reloadIssues();
    setIssueInlineNotice("Vote recorded.");
    updateVoteUi();
}

void MainWindow::updateVoteUi()
{
    if (m_issueCreditsLabel)
        m_issueCreditsLabel->setText(
            QStringLiteral("Credits: %1").arg(availableCredits()));
    if (!m_issueVoteButton)
        return;
    const bool haveIssue = m_currentIssueNumber >= 0;
    int votes = 0;
    if (haveIssue) {
        for (const Issue &issue : m_currentIssues)
            if (issue.number == m_currentIssueNumber)
                votes = issue.votes;
    }
    const int credits = availableCredits();
    m_issueVoteButton->setText(
        QStringLiteral("Vote (%1)").arg(formatCount(votes)));
    // You can vote repeatedly as long as you have credits; each vote spends one.
    m_issueVoteButton->setEnabled(haveIssue && credits > 0);
    m_issueVoteButton->setToolTip(
        credits > 0
            ? QStringLiteral("Upvote this issue (spends 1 of %1 voting credits)")
                  .arg(credits)
            : QString::fromUtf8("No voting credits yet \xE2\x80\x94 you earn 1 per "
                             "hour online"));
}

void MainWindow::syncIssuesInbox()
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    drainIssuesInboxFor(m_repositories.at(idx), /*interactive=*/true);
}

void MainWindow::drainIssuesInboxFor(RepositoryRecord repo, bool interactive)
{
    // Only the owner (a writable working-tree copy) can read and merge the inbox.
    const RepositoryRecord writable = writableRecordFor(repo);
    {
        IssueStore probe(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                         m_userName);
        if (!probe.canWrite())
            return;
    }

    const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-issues-pull-v1\n" + owner + "\n" + ts).toUtf8();
    const QString sig = m_profileIdentity.signData(canonical);

    QUrl url = issuesApiUrl(repo);
    QUrlQuery query;
    query.addQueryItem("owner", owner);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", sig);
    url.setQuery(query);

    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, url, repo, writable, interactive] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (interactive)
                setIssueInlineNotice("Could not reach the inbox: " +
                                         reply->errorString(),
                                     true);
            return;
        }
        const QJsonArray pending = QJsonDocument::fromJson(reply->readAll())
                                       .object()
                                       .value("pending")
                                       .toArray();
        if (pending.isEmpty()) {
            if (interactive)
                setIssueInlineNotice("No pending submissions.");
            return;
        }
        IssueStore store(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                         m_userName);
        int merged = 0;
        int comments = 0;
        int newIssues = 0;
        QString lastCommentAuthor;
        int lastCommentNumber = 0;
        QString lastCommentBody;
        QString lastIssueAuthor;
        QString lastIssueTitle;
        int lastIssueNumber = 0;
        for (const QJsonValue &value : pending) {
            const QJsonObject item = value.toObject();
            const int number = item.value("number").toInt();
            const QJsonObject eventObj = item.value("event").toObject();
            IssueEvent ev = IssueEvent::fromJson(eventObj);
            ev.body = eventObj.value("body").toString();
            const QString titleIfNew = item.value("titleIfNew").toString();
            const QJsonObject metaObj = item.value("meta").toObject();
            RemoteIssueMeta meta;
            for (const QJsonValue &l : metaObj.value("labels").toArray())
                meta.labels << l.toString();
            meta.milestone = metaObj.value("milestone").toString();
            meta.priority = metaObj.value("priority").toInt();
            for (const QJsonValue &a : metaObj.value("assignees").toArray())
                meta.assignees << a.toString();
            if (store.applyRemoteEvent(number, ev, titleIfNew, nullptr, meta)) {
                ++merged;
                const QString who =
                    ev.authorName.isEmpty() ? ev.author.left(8) : ev.authorName;
                if (ev.type == QLatin1String("comment")) {
                    ++comments;
                    lastCommentAuthor = who;
                    lastCommentNumber = number;
                    lastCommentBody = ev.body.simplified();
                } else if (ev.type == QLatin1String("open")) {
                    ++newIssues;
                    lastIssueAuthor = who;
                    lastIssueTitle = ev.title.isEmpty() ? titleIfNew : ev.title;
                    lastIssueNumber = number;
                }
            }
        }
        // Acknowledge so the inbox clears the merged submissions.
        m_networkAccess->deleteResource(QNetworkRequest(url));
        // Refresh the issue list if this is the repo currently on screen.
        const int curIdx = issuesRepoIndex();
        if (curIdx >= 0 &&
            m_repositories.at(curIdx).owner == repo.owner &&
            m_repositories.at(curIdx).name == repo.name)
            reloadIssues();
        // Incoming issues just landed in the working copy: push them to the
        // mirror and notify peers now so every node's count converges promptly.
        if (merged > 0) {
            propagateRepoUpdate(repoIndexFor(repo.owner, repo.name));
            // An inbound issue/comment may @mention the owner running this node.
            scanRepoMentionsFor(writable);
        }
        if (interactive)
            setIssueInlineNotice(
                QStringLiteral("Merged %1 submission(s) into issues/.").arg(merged));

        // Notify on new issues filed by other nodes (the source of truth should
        // see incoming issues) and on inbound comments — interactive or not.
        if (newIssues > 0) {
            const QString body =
                newIssues == 1
                    ? QStringLiteral("%1 filed a new issue on %2/%3: %4")
                          .arg(lastIssueAuthor, repo.owner, repo.name, lastIssueTitle)
                    : QStringLiteral("%1 new issues filed on %2/%3")
                          .arg(newIssues)
                          .arg(repo.owner, repo.name);
            flashMessage(body);
            if (notifyEnabled(kIssueAlertSetting))
                notifyIfInactive(QString::fromUtf8("ForkMesh \xE2\x80\x94 new issue"),
                                 body);
            // Log it on the Notifications page so it persists past the toast.
            // A single new issue links straight to it; a batch lands on the
            // repo's Issues tab (issue #292).
            NotificationLink link;
            link.kind = QStringLiteral("issue");
            link.owner = repo.owner;
            link.name = repo.name;
            link.number = newIssues == 1 ? lastIssueNumber : -1;
            addNotification(QStringLiteral("New issue"), body, false, link);
            if (notifyEnabled(kIssueAlertSetting) && m_trayIcon &&
                QSystemTrayIcon::supportsMessages())
                m_trayIcon->showMessage("ForkMesh — new issue", body,
                                        QSystemTrayIcon::Information, 6000);
        }
        if (comments > 0) {
            QString body;
            if (comments == 1) {
                body = QStringLiteral("%1 commented on %2/%3 issue #%4")
                           .arg(lastCommentAuthor, repo.owner, repo.name)
                           .arg(lastCommentNumber);
                if (!lastCommentBody.isEmpty()) {
                    const QString snippet = lastCommentBody.left(140) +
                        (lastCommentBody.size() > 140 ? QString::fromUtf8("\xE2\x80\xA6")
                                                      : QString());
                    body += QString::fromUtf8(": \xE2\x80\x9C%1\xE2\x80\x9D").arg(snippet);
                }
            } else {
                body = QStringLiteral("%1 new comments on %2/%3 issues")
                           .arg(comments)
                           .arg(repo.owner, repo.name);
            }
            if (notifyEnabled(kCommentAlertSetting))
                notifyIfInactive(QString::fromUtf8("ForkMesh \xE2\x80\x94 new comment"),
                                 body);
            // Log it on the Notifications page so it persists past the toast.
            // A single comment links to its issue; a batch lands on the Issues
            // tab (issue #292).
            NotificationLink link;
            link.kind = QStringLiteral("issue");
            link.owner = repo.owner;
            link.name = repo.name;
            link.number = comments == 1 ? lastCommentNumber : -1;
            addNotification(QStringLiteral("New comment"), body, false, link);
            if (notifyEnabled(kCommentAlertSetting) && m_trayIcon &&
                QSystemTrayIcon::supportsMessages())
                m_trayIcon->showMessage("ForkMesh — new comment", body,
                                        QSystemTrayIcon::Information, 6000);
        }
    });
}

QWidget *MainWindow::buildChatSection()
{
    auto *page = new QWidget;

    // Sidebar
    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(280);

    // Kept alive (status updates still write to it) but no longer shown in the
    // sidebar — the connection summary lives in the top bar instead.
    m_statusLine = new QLabel(sidebar);
    m_statusLine->setObjectName("statusLine");
    m_statusLine->setWordWrap(true);
    m_statusLine->hide();

    m_channelList = new QListWidget;
    auto *addChannelButton = new QPushButton("+ Add chat");
    addChannelButton->setObjectName("ghostButton");
    addChannelButton->setCursor(Qt::PointingHandCursor);

    auto *dmsLabel = new QLabel("DIRECT MESSAGES");
    dmsLabel->setObjectName("sectionLabel");
    m_dmList = new QListWidget;
    // Members list removed: nodes are the members. Use the Node dropdown and the
    // node profile's "Message" button to start a direct chat.

    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(14, 16, 14, 12);
    sidebarLayout->setSpacing(6);
    sidebarLayout->addWidget(m_channelList, 2);
    sidebarLayout->addWidget(addChannelButton);
    sidebarLayout->addWidget(dmsLabel);
    sidebarLayout->addWidget(m_dmList, 1);
    sidebarLayout->addStretch();

    // Main column
    auto *header = new QWidget;
    header->setObjectName("chatHeader");
    m_channelTitle = new QLabel("#general");
    m_channelTitle->setObjectName("channelTitle");
    // Just a padlock — hovering explains it's fully end-to-end encrypted.
    m_encryptionLabel = new QLabel;
    m_encryptionLabel->setObjectName("encryptionLabel");
    m_encryptionLabel->setPixmap(
        tintedOcticonPixmap("lock", QColor("#8b949e"), 16));
    m_encryptionLabel->setToolTip("Fully end-to-end encrypted");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 12, 18, 12);
    headerLayout->addWidget(m_channelTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(m_encryptionLabel);

    // Firewall banner: hidden until the backend reports the host firewall is
    // blocking ForkMesh, then offers a one-click "Allow through firewall".
    m_firewallBanner = new QWidget;
    m_firewallBanner->setObjectName("firewallBanner");
    m_firewallBannerLabel = new QLabel;
    m_firewallBannerLabel->setObjectName("firewallBannerLabel");
    m_firewallBannerLabel->setWordWrap(true);
    m_firewallAllowButton = new QPushButton("Allow through firewall");
    m_firewallAllowButton->setObjectName("primaryButton");
    m_firewallAllowButton->setCursor(Qt::PointingHandCursor);
    auto *firewallDismiss = new QPushButton(QString());
    firewallDismiss->setObjectName("ghostButton");
    firewallDismiss->setCursor(Qt::PointingHandCursor);
    firewallDismiss->setToolTip("Dismiss");
    setOcticon(firewallDismiss, "x", 16);
    auto *firewallLayout = new QHBoxLayout(m_firewallBanner);
    firewallLayout->setContentsMargins(16, 10, 12, 10);
    firewallLayout->setSpacing(10);
    firewallLayout->addWidget(m_firewallBannerLabel, 1);
    firewallLayout->addWidget(m_firewallAllowButton);
    firewallLayout->addWidget(firewallDismiss);
    m_firewallBanner->hide();
    connect(m_firewallAllowButton, &QPushButton::clicked, this,
            &MainWindow::allowFirewall);
    connect(firewallDismiss, &QPushButton::clicked, m_firewallBanner,
            &QWidget::hide);

    // Scrollable column of message-row widgets (supports avatars, inline
    // images, animated GIFs, file chips, and reaction bars).
    m_messageScroll = new QScrollArea;
    m_messageScroll->setObjectName("messageView");
    m_messageScroll->setWidgetResizable(true);
    m_messageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageContainer = new QWidget;
    m_messageContainer->setObjectName("messageContainer");
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(4, 8, 4, 8);
    m_messageLayout->setSpacing(0);
    m_messageLayout->addStretch();
    m_messageScroll->setWidget(m_messageContainer);

    // Keep the newest message visible. A row added to the layout grows the
    // scroll range asynchronously, so we can't reliably scroll the instant we
    // insert it; instead, whenever the range grows while we're pinned to the
    // bottom, jump to the new maximum. The user scrolling up clears the pin so
    // we don't drag them back down while they read history.
    QScrollBar *vbar = m_messageScroll->verticalScrollBar();
    connect(vbar, &QScrollBar::rangeChanged, this, [this](int, int max) {
        if (m_stickToBottom)
            m_messageScroll->verticalScrollBar()->setValue(max);
    });
    connect(vbar, &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar *bar = m_messageScroll->verticalScrollBar();
        m_stickToBottom = value >= bar->maximum() - 4;
    });

    auto *composer = new QWidget;
    composer->setObjectName("composerBar");
    auto *attachButton = new QPushButton(QString());
    attachButton->setObjectName("iconButton");
    attachButton->setCursor(Qt::PointingHandCursor);
    attachButton->setToolTip("Share a file (any type, including GIFs)");
    setOcticon(attachButton, "paperclip", 18);
    connect(attachButton, &QPushButton::clicked, this, &MainWindow::attachFile);
    m_messageInput = new QLineEdit;
    m_messageInput->setObjectName("messageInput");
    m_messageInput->setPlaceholderText("Message #general");
    m_messageInput->setMaxLength(16000);
    // Intercept Ctrl+V so a clipboard image (e.g. a screenshot) is shared as a
    // file attachment instead of being dropped by the text-only line edit.
    m_messageInput->installEventFilter(this);

    // @-mention autocomplete: a completer driven manually off the cursor (hence
    // setWidget, not setCompleter, which would try to complete the whole line).
    // updateMentionPopup() feeds it the "@token" being typed and pops the list;
    // picking a name replaces that token with "@name ".
    m_mentionModel = new QStringListModel(this);
    m_mentionCompleter = new QCompleter(m_mentionModel, this);
    m_mentionCompleter->setWidget(m_messageInput);
    m_mentionCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_mentionCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_mentionCompleter->setFilterMode(Qt::MatchContains);
    connect(m_mentionCompleter,
            QOverload<const QString &>::of(&QCompleter::activated), this,
            &MainWindow::insertMention);
    refreshMentionCandidates();
    auto *sendButton = new QPushButton("Send");
    sendButton->setObjectName("primaryButton");
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(14, 10, 14, 12);
    composerLayout->setSpacing(8);
    composerLayout->addWidget(attachButton);
    composerLayout->addWidget(m_messageInput);
    composerLayout->addWidget(sendButton);

    m_typingLabel = new QLabel;
    m_typingLabel->setObjectName("typingLabel");
    m_typingLabel->setFixedHeight(20);
    m_typingLabel->setText(QString());

    auto *mainColumn = new QVBoxLayout;
    mainColumn->setContentsMargins(0, 0, 0, 0);
    mainColumn->setSpacing(0);
    mainColumn->addWidget(header);
    mainColumn->addWidget(m_firewallBanner);
    mainColumn->addWidget(m_messageScroll, 1);
    mainColumn->addWidget(m_typingLabel);
    mainColumn->addWidget(composer);

    // Right column: online members, each with avatar + green/grey status dot.
    // Mirrors the node-card look used elsewhere; refreshed from setRoster().
    auto *membersPanel = new QWidget;
    membersPanel->setObjectName("sidebar");
    membersPanel->setFixedWidth(220);
    m_chatMembersHeading = new QLabel("ONLINE \xE2\x80\x94 0");
    m_chatMembersHeading->setObjectName("sectionLabel");
    auto *membersScroll = new QScrollArea;
    membersScroll->setObjectName("messageView");
    membersScroll->setWidgetResizable(true);
    membersScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    membersScroll->setFrameShape(QFrame::NoFrame);
    auto *membersContainer = new QWidget;
    m_chatMembersLayout = new QVBoxLayout(membersContainer);
    m_chatMembersLayout->setContentsMargins(0, 0, 0, 0);
    m_chatMembersLayout->setSpacing(4);
    m_chatMembersLayout->addStretch();
    membersScroll->setWidget(membersContainer);
    auto *membersLayout = new QVBoxLayout(membersPanel);
    membersLayout->setContentsMargins(14, 16, 14, 12);
    membersLayout->setSpacing(8);
    membersLayout->addWidget(m_chatMembersHeading);
    membersLayout->addWidget(membersScroll, 1);

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sidebar);
    layout->addLayout(mainColumn, 1);
    layout->addWidget(membersPanel);

    refreshChatMembers();

    connect(m_channelList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    switchConversation(item->data(Qt::UserRole).toString());
            });
    connect(m_dmList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    switchConversation(item->data(Qt::UserRole).toString());
            });
    connect(addChannelButton, &QPushButton::clicked, this, &MainWindow::promptAddChannel);
    connect(m_messageInput, &QLineEdit::textEdited, this, &MainWindow::onComposerEdited);
    // Re-evaluate the @-mention popup when the caret moves (arrow keys, a click)
    // so it follows the token or dismisses when the caret leaves it.
    connect(m_messageInput, &QLineEdit::cursorPositionChanged, this,
            [this] { updateMentionPopup(); });
    connect(m_messageInput, &QLineEdit::returnPressed, this, &MainWindow::sendCurrentMessage);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendCurrentMessage);

    return page;
}

void MainWindow::updateHomeStats()
{
    // The quest board is gone; this now just persists accumulated uptime. The
    // per-node stats live inline in the repositories panel (see selfNodeStats).
    if (m_connectedAtMs > 0) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 totalMs = m_totalConnectionMs + (now - m_connectedAtMs);
        QSettings().setValue(kConnectionTotalSetting, totalMs);
    }
}

QString MainWindow::selfNodeStats() const
{
    int mirrored = 0;
    int online = 0;
    for (const RepositoryRecord &repo : m_repositories) {
        if (repo.previewOnly)
            continue;
        if (repo.lastSyncMs > 0 ||
            (!repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists()))
            ++mirrored;
        if (repo.publishedAtMs > 0 || repo.publishToNetwork)
            ++online;
    }
    const qint64 sessionMs =
        m_connectedAtMs > 0 ? QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs : 0;
    const qint64 totalMs = m_totalConnectionMs + sessionMs;
    const QString key = m_profileIdentity.shortPublicKey();
    const int permanentRepoCount =
        int(std::count_if(m_repositories.cbegin(), m_repositories.cend(),
                          [](const RepositoryRecord &repo) {
                              return !repo.previewOnly;
                          }));
    return QString::fromUtf8(
               "%1 repos \xC2\xB7 %2 mirrored \xC2\xB7 %3 online \xC2\xB7 %4 chats")
               .arg(permanentRepoCount)
               .arg(mirrored)
               .arg(online)
               .arg(m_channels.size()) +
           "\nuptime " + formatDuration(sessionMs) + " \xC2\xB7 total " +
           formatDuration(totalMs) +
           (key.isEmpty() ? QString() : " \xC2\xB7 key " + key);
}

