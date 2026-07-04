// MainWindowBranches: MainWindow feature methods, split out of MainWindow.cpp.
// Branches panel and worktrees.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"

#include <QComboBox>
#include <QTimer>

using namespace forkmesh::ui;

// ---- Branches panel --------------------------------------------------------

// Defined further down (next to pullBaseIntoAllBranches); declared here so
// loadBranchesPanel can probe each branch for merge conflicts.
static QString branchMergeTree(const QString &dir, const QString &base,
                               const QString &branch);

// Split rendered diff HTML into its self-contained per-file blocks. Each file's
// block begins with its `<a name="file-N"></a>` anchor (see diffFileHeaderHtml)
// and ends before the next one, so these chunks can be streamed into the view a
// few at a time instead of laid out in one blocking pass (adhoc #51). Any
// preamble before the first anchor rides along with the first block.
static QStringList splitDiffFileBlocks(const QString &html)
{
    static const QString marker = QStringLiteral("<a name=\"file-");
    int pos = html.indexOf(marker);
    if (pos < 0)
        return {html}; // no per-file anchors (e.g. an empty/notice body)
    QStringList blocks;
    if (pos > 0)
        blocks.append(html.left(pos)); // preamble before the first file (if any)
    while (pos >= 0) {
        const int next = html.indexOf(marker, pos + marker.size());
        blocks.append(html.mid(pos, next < 0 ? -1 : next - pos));
        pos = next;
    }
    return blocks;
}

// Worktrees tab (next to Branches): lists this repo's git worktrees — the main
// checkout plus each agent's isolated worktree+branch — with open/remove/prune.
QWidget *MainWindow::buildWorktreesTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel("Worktrees");
    heading->setObjectName("channelTitle");
    m_worktreesSummary = new QLabel;
    m_worktreesSummary->setObjectName("statusLine");
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::loadWorktreesPanel);
    auto *pruneButton = new QPushButton("Prune");
    pruneButton->setObjectName("ghostButton");
    pruneButton->setCursor(Qt::PointingHandCursor);
    setOcticon(pruneButton, "trash", 16);
    pruneButton->setToolTip("Drop registrations for worktrees whose folders are gone");
    connect(pruneButton, &QPushButton::clicked, this, [this] {
        QString repoPath;
        if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
            repoPath = m_repositories.at(m_repoDetailIndex).localPath;
        if (!repoPath.isEmpty())
            QProcess::execute(QStringLiteral("git"),
                              {QStringLiteral("-C"), repoPath,
                               QStringLiteral("worktree"), QStringLiteral("prune")});
        loadWorktreesPanel();
    });
    headerRow->addWidget(heading);
    headerRow->addWidget(m_worktreesSummary);
    headerRow->addStretch();
    headerRow->addWidget(pruneButton);
    headerRow->addWidget(refreshButton);
    layout->addLayout(headerRow);

    auto *info = new QLabel("Each agent works in its own worktree + branch, so "
                            "concurrent agents never share a working tree.");
    info->setObjectName("statusLine");
    info->setWordWrap(true);
    layout->addWidget(info);

    m_worktreesTable = new QTableWidget(0, 5);
    installColumnHeaderMenu(m_worktreesTable); // 3-dots per-column menu (issue #318)
    m_worktreesTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_worktreesTable);
    m_worktreesTable->setHorizontalHeaderLabels(
        {"Branch", "Path", "Status", "Ahead/Behind", ""});
    m_worktreesTable->verticalHeader()->setVisible(false);
    m_worktreesTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_worktreesTable->verticalHeader()->setDefaultSectionSize(36);
    m_worktreesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_worktreesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_worktreesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_worktreesTable->setShowGrid(false);
    m_worktreesTable->setWordWrap(false);
    QHeaderView *wh = m_worktreesTable->horizontalHeader();
    wh->setHighlightSections(false);
    wh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    wh->setSectionResizeMode(1, QHeaderView::Stretch);
    wh->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    wh->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    wh->setSectionResizeMode(4, QHeaderView::Fixed);
    makeColumnsResizable(m_worktreesTable);
    connect(m_worktreesTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                if (QTableWidgetItem *it = m_worktreesTable->item(row, 1))
                    QDesktopServices::openUrl(QUrl::fromLocalFile(it->text()));
            });
    // Selecting a worktree previews its changes vs the default branch.
    connect(m_worktreesTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                QTableWidgetItem *b = m_worktreesTable->item(row, 0);
                QTableWidgetItem *p = m_worktreesTable->item(row, 1);
                showWorktreeDiff(b ? b->data(Qt::UserRole).toString() : QString(),
                                 p ? p->text() : QString());
            });

    // File-change list + diff viewer beside the table (same pattern as Branches).
    m_worktreeFilesSummary = new QLabel;
    m_worktreeFilesSummary->setObjectName("sectionLabel");
    m_worktreeFilesSummary->setTextFormat(Qt::RichText);
    m_worktreeFileList = new QListWidget;
    m_worktreeFileList->setObjectName("overviewList");
    enableHoverRowHighlight(m_worktreeFileList); // green outline selection (issue #252)
    m_worktreeFileList->setMinimumWidth(170);
    connect(m_worktreeFileList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item && m_worktreeDiffView)
                    m_worktreeDiffView->scrollToAnchor(item->data(Qt::UserRole).toString());
            });
    auto *filesPane = new QWidget;
    auto *filesLayout = new QVBoxLayout(filesPane);
    filesLayout->setContentsMargins(0, 0, 0, 0);
    filesLayout->setSpacing(6);
    filesLayout->addWidget(m_worktreeFilesSummary);
    filesLayout->addWidget(m_worktreeFileList, 1);

    m_worktreeDiffView = new QTextBrowser;
    m_worktreeDiffView->setObjectName("diffView");
    m_worktreeDiffView->setOpenExternalLinks(false);
    m_worktreeDiffView->setLineWrapMode(QTextEdit::NoWrap);
    registerDiffView(m_worktreeDiffView);

    // Detail pane: a toolbar with a prominent "Merge into main" for the selected
    // worktree, over its diff. Mirrors the per-row button but is reachable while
    // reviewing the changes here (it merges whichever worktree is selected).
    m_worktreeMergeButton = new QPushButton("Merge into main");
    m_worktreeMergeButton->setObjectName("primaryButton");
    m_worktreeMergeButton->setProperty("buttonSize", "sm");
    m_worktreeMergeButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_worktreeMergeButton, "check-circle", 14);
    m_worktreeMergeButton->setToolTip(
        "Merge the selected worktree's branch into the default branch, then delete "
        "the worktree and its branch");
    m_worktreeMergeButton->setEnabled(false);
    connect(m_worktreeMergeButton, &QPushButton::clicked, this, [this] {
        if (!m_worktreeSelectedBranch.isEmpty())
            mergeWorktreeIntoMain(m_worktreeSelectedBranch, m_worktreeSelectedPath);
    });
    // Same merge, but also tear down the agent session that produced the branch.
    m_worktreeMergeDeleteAgentButton = new QPushButton("Merge & delete agent");
    m_worktreeMergeDeleteAgentButton->setObjectName("ghostButton");
    m_worktreeMergeDeleteAgentButton->setProperty("buttonSize", "sm");
    m_worktreeMergeDeleteAgentButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_worktreeMergeDeleteAgentButton, "check-circle", 14);
    m_worktreeMergeDeleteAgentButton->setToolTip(
        "Merge the selected worktree's branch into the default branch, then delete "
        "the worktree, its branch and its agent session");
    m_worktreeMergeDeleteAgentButton->setEnabled(false);
    connect(m_worktreeMergeDeleteAgentButton, &QPushButton::clicked, this, [this] {
        if (!m_worktreeSelectedBranch.isEmpty())
            mergeWorktreeIntoMain(m_worktreeSelectedBranch, m_worktreeSelectedPath,
                                  /*deleteAgent=*/true);
    });
    // The reverse direction: pull the default branch into this worktree so it
    // catches up with main before you keep working (or merge it back).
    m_worktreeUpdateButton = new QPushButton("Update from main");
    m_worktreeUpdateButton->setObjectName("ghostButton");
    m_worktreeUpdateButton->setProperty("buttonSize", "sm");
    m_worktreeUpdateButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_worktreeUpdateButton, "sync", 14);
    m_worktreeUpdateButton->setToolTip(
        "Merge the default branch into the selected worktree's branch");
    m_worktreeUpdateButton->setEnabled(false);
    connect(m_worktreeUpdateButton, &QPushButton::clicked, this, [this] {
        if (!m_worktreeSelectedPath.isEmpty() && !m_worktreeSelectedBranch.isEmpty())
            updateWorktreeFromMain(m_worktreeSelectedPath, m_worktreeSelectedBranch);
    });
    // Opens the merge editor over the selected worktree's conflicted files so the
    // user can resolve and commit a merge that left conflict markers (e.g. an
    // "Update from main" or an agent merge that didn't apply cleanly). Hidden
    // unless the selected worktree actually has unmerged files (set in
    // showWorktreeDiff).
    m_worktreeResolveButton = new QPushButton("Resolve conflicts\xE2\x80\xA6");
    m_worktreeResolveButton->setObjectName("primaryButton");
    m_worktreeResolveButton->setProperty("buttonSize", "sm");
    m_worktreeResolveButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_worktreeResolveButton, "alert", 14);
    m_worktreeResolveButton->setToolTip(
        "Open the merge editor to resolve this worktree's conflicts and commit them");
    m_worktreeResolveButton->hide();
    connect(m_worktreeResolveButton, &QPushButton::clicked, this,
            &MainWindow::resolveWorktreeConflicts);
    // Commit the worktree's uncommitted changes in place, so you can snapshot
    // in-progress work without dropping to a terminal (sits beside "Update from
    // main" since you typically commit before pulling main in).
    m_worktreeCommitButton = new QPushButton("Commit changes");
    m_worktreeCommitButton->setObjectName("ghostButton");
    m_worktreeCommitButton->setProperty("buttonSize", "sm");
    m_worktreeCommitButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_worktreeCommitButton, "git-branch", 14);
    m_worktreeCommitButton->setToolTip(
        "Stage and commit the selected worktree's uncommitted changes");
    m_worktreeCommitButton->setEnabled(false);
    connect(m_worktreeCommitButton, &QPushButton::clicked, this, [this] {
        if (!m_worktreeSelectedPath.isEmpty() && !m_worktreeSelectedBranch.isEmpty())
            commitWorktreeChanges(m_worktreeSelectedPath, m_worktreeSelectedBranch);
    });
    m_worktreeRemoveButton = new QPushButton("Delete");
    m_worktreeRemoveButton->setObjectName("ghostButton");
    m_worktreeRemoveButton->setProperty("buttonSize", "sm");
    m_worktreeRemoveButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_worktreeRemoveButton, "trash", 14);
    m_worktreeRemoveButton->setToolTip(
        "Remove the selected worktree, delete its branch and its agent session");
    m_worktreeRemoveButton->setEnabled(false);
    connect(m_worktreeRemoveButton, &QPushButton::clicked, this, [this] {
        if (!m_worktreeSelectedPath.isEmpty())
            deleteWorktreeBranchAndAgent(m_worktreeSelectedPath,
                                         m_worktreeSelectedBranch);
    });
    // Show which branch the selected worktree is on and where it lives on disk,
    // beside its action buttons. The branch name and the path are links that open
    // the worktree's folder in the system file manager.
    m_worktreeBranchLabel = new QLabel;
    m_worktreeBranchLabel->setObjectName("sectionLabel");
    m_worktreeBranchLabel->setTextFormat(Qt::RichText);
    m_worktreeBranchLabel->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                   Qt::LinksAccessibleByMouse);
    m_worktreeBranchLabel->setOpenExternalLinks(true);
    auto *detailBar = new QHBoxLayout;
    detailBar->setContentsMargins(0, 0, 0, 0);
    detailBar->addWidget(m_worktreeBranchLabel);
    detailBar->addStretch();
    detailBar->addWidget(m_worktreeResolveButton);
    detailBar->addWidget(m_worktreeCommitButton);
    detailBar->addWidget(m_worktreeUpdateButton);
    detailBar->addWidget(m_worktreeMergeButton);
    detailBar->addWidget(m_worktreeMergeDeleteAgentButton);
    detailBar->addWidget(m_worktreeRemoveButton);
    auto *diffPane = new QWidget;
    auto *diffPaneLayout = new QVBoxLayout(diffPane);
    diffPaneLayout->setContentsMargins(0, 0, 0, 0);
    diffPaneLayout->setSpacing(6);
    diffPaneLayout->addLayout(detailBar);
    diffPaneLayout->addWidget(m_worktreeDiffView, 1);

    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    split->addWidget(m_worktreesTable);
    split->addWidget(filesPane);
    split->addWidget(diffPane);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 0);
    split->setStretchFactor(2, 1);
    // Open with the worktrees table taking ~50% of the width and the two detail
    // panes (file list + diff) sharing the other ~50%.
    split->setSizes({500, 150, 350});
    layout->addWidget(split, 1);
    return page;
}

void MainWindow::runGitDetached(const QString &dir, const QStringList &args,
                                std::function<void(bool, const QByteArray &)> onDone)
{
    auto *git = new QProcess(this);
    if (!dir.isEmpty())
        git->setWorkingDirectory(dir);
    // FailedToStart fires errorOccurred but not finished, and a crash fires both,
    // so guard the callback so it runs exactly once whichever way the process ends.
    auto done = std::make_shared<bool>(false);
    auto finish = [git, done, onDone = std::move(onDone)](bool ok) {
        if (*done)
            return;
        *done = true;
        if (onDone)
            onDone(ok, git->readAllStandardOutput());
        git->deleteLater();
    };
    connect(git, &QProcess::finished, this,
            [finish](int code, QProcess::ExitStatus st) {
                finish(st == QProcess::NormalExit && code == 0);
            });
    connect(git, &QProcess::errorOccurred, this,
            [finish](QProcess::ProcessError) { finish(false); });
    git->start(QStringLiteral("git"), args);
}

void MainWindow::focusRepoDetailTable(int id)
{
    // The list table at the heart of each repo-detail tab. Tabs that aren't a
    // single scrollable list (Code, Insights, Security, Settings, …) map to
    // nullptr and are left alone.
    QTableWidget *table = nullptr;
    if (id == 1)
        table = m_commitsTable;
    else if (id == 2)
        table = m_issueTable;
    else if (id == 3)
        table = m_agentTable;
    else if (id == 4)
        table = m_pullTable;
    else if (id == 5)
        table = m_discussionTable;
    else if (id == 6)
        table = m_actionsTable;
    else if (id == m_branchesTabIndex)
        table = m_branchesTable;
    else if (id == m_worktreesTabIndex)
        table = m_worktreesTable;
    else if (id == m_releasesTabIndex)
        table = m_releasesTable;
    else if (id == m_mirrorNodesTabIndex)
        table = m_mirrorNodesTable;
    else if (id == m_artifactsTabIndex)
        table = m_artifactsTable;
    // Only grab focus for a table that's actually on screen (e.g. the Issues tab
    // hides m_issueTable while its Milestones/Labels sub-tab is showing).
    if (table && table->isVisible() && table->isEnabled())
        table->setFocus(Qt::OtherFocusReason);
}

void MainWindow::loadWorktreesPanel()
{
    if (!m_worktreesTable)
        return;
    // Remember the selected worktree so a rebuild (Refresh, or after an
    // "Update from main"/merge) lands back on it instead of going blank — clearing
    // the table fires currentCellChanged(-1) which wipes the diff + selection (#272).
    const QString keepPath = m_worktreeSelectedPath;
    TableRepaintGuard repaintGuard(m_worktreesTable);
    m_worktreesTable->setRowCount(0);
    QString repoPath, repoOwner, repoName;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        repoPath = m_repositories.at(m_repoDetailIndex).localPath;
        repoOwner = m_repositories.at(m_repoDetailIndex).owner;
        repoName = m_repositories.at(m_repoDetailIndex).name;
    }
    if (repoPath.isEmpty() || !QDir(repoPath).exists(QStringLiteral(".git"))) {
        if (m_worktreesSummary)
            m_worktreesSummary->setText(QStringLiteral("· no local checkout"));
        if (m_repoWorktreesTab)
            m_repoWorktreesTab->setText(QStringLiteral("Worktrees"));
        return;
    }

    struct WT { QString path, branch, head; bool detached = false; };
    QList<WT> wts;
    WT cur;
    bool have = false;
    QByteArray out;
    if (runGitCapture(repoPath, {QStringLiteral("worktree"), QStringLiteral("list"),
                                 QStringLiteral("--porcelain")}, &out, nullptr)) {
        for (const QString &raw : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
            const QString line = raw.trimmed();
            if (line.isEmpty()) {
                if (have) wts.append(cur);
                cur = WT();
                have = false;
                continue;
            }
            have = true;
            if (line.startsWith(QLatin1String("worktree ")))
                cur.path = line.mid(9);
            else if (line.startsWith(QLatin1String("HEAD ")))
                cur.head = line.mid(5);
            else if (line.startsWith(QLatin1String("branch ")))
                cur.branch = line.mid(7).replace(QLatin1String("refs/heads/"), QString());
            else if (line == QLatin1String("detached"))
                cur.detached = true;
        }
        if (have) wts.append(cur);
    }

    const QString mainPath = QDir(repoPath).absolutePath();
    // Base branch each worktree's ahead/behind count is measured against (#272
    // follow-up: show how far each worktree has diverged from main in the list).
    const QString baseBranch = repoDefaultBranch(repoBranches());
    // Each worktree's dirty/clean status needs its own `git status`, which was run
    // synchronously per row and froze the UI for seconds on repos with many
    // worktrees (a 21s stall was reported). Fill the column asynchronously instead
    // (see runGitDetached); this generation tag lets a stale callback bail if the
    // table is rebuilt before it returns.
    const int statusGen = ++m_worktreeStatusGen;
    int actionWidth = 0;
    for (const WT &wt : wts) {
        const int row = m_worktreesTable->rowCount();
        m_worktreesTable->insertRow(row);
        const bool isMain = QDir(wt.path).absolutePath() == mainPath;

        const QString branchLabel =
            wt.detached ? QStringLiteral("(detached %1)").arg(wt.head.left(8))
                        : (wt.branch.isEmpty() ? QStringLiteral("(none)") : wt.branch);
        auto *bItem = new QTableWidgetItem(branchLabel);
        bItem->setIcon(themedOcticon(isMain ? "check-circle" : "git-branch",
                                     QColor(isMain ? "#3fb950" : "#8b949e"), 14));
        bItem->setData(Qt::UserRole, wt.branch); // real branch for diff/merge
        m_worktreesTable->setItem(row, 0, bItem);
        m_worktreesTable->setItem(row, 1, new QTableWidgetItem(wt.path));

        // Show a placeholder now; the real dirty/clean state is filled in by the
        // async `git status` below so the panel appears instantly.
        m_worktreesTable->setItem(
            row, 2,
            new QTableWidgetItem(isMain ? QStringLiteral("main checkout")
                                        : QString::fromUtf8("checking\xE2\x80\xA6")));
        const QString wtPath = wt.path;
        const bool mainRow = isMain;
        runGitDetached(
            wt.path, {QStringLiteral("status"), QStringLiteral("--porcelain")},
            [this, statusGen, wtPath, mainRow](bool ok, const QByteArray &st) {
                if (statusGen != m_worktreeStatusGen || !m_worktreesTable)
                    return; // the table was rebuilt while git ran — drop this result
                const bool dirty =
                    ok && !QString::fromUtf8(st).trimmed().isEmpty();
                QString status = mainRow ? QStringLiteral("main checkout") : QString();
                if (dirty)
                    status = status.isEmpty() ? QStringLiteral("uncommitted changes")
                                              : status + QStringLiteral(" · dirty");
                if (status.isEmpty())
                    status = QStringLiteral("clean");
                const QString want = QDir(wtPath).absolutePath();
                for (int r = 0; r < m_worktreesTable->rowCount(); ++r) {
                    QTableWidgetItem *p = m_worktreesTable->item(r, 1);
                    if (p && QDir(p->text()).absolutePath() == want) {
                        if (QTableWidgetItem *c = m_worktreesTable->item(r, 2))
                            c->setText(status);
                        break;
                    }
                }
            });

        // Ahead/behind vs the default branch, also filled async per row so a repo
        // with many worktrees still paints instantly. `rev-list --left-right` on
        // base...HEAD reports "<behind>\t<ahead>" (left = base, right = worktree).
        auto *abItem = new QTableWidgetItem(
            baseBranch.isEmpty() ? QStringLiteral("—")
                                 : QString::fromUtf8("checking\xE2\x80\xA6"));
        abItem->setTextAlignment(Qt::AlignCenter);
        m_worktreesTable->setItem(row, 3, abItem);
        if (!baseBranch.isEmpty()) {
            runGitDetached(
                wt.path,
                {QStringLiteral("rev-list"), QStringLiteral("--left-right"),
                 QStringLiteral("--count"),
                 baseBranch + QStringLiteral("...HEAD")},
                [this, statusGen, wtPath, baseBranch](bool ok, const QByteArray &c) {
                    if (statusGen != m_worktreeStatusGen || !m_worktreesTable)
                        return; // table rebuilt while git ran — drop this result
                    const QStringList n =
                        QString::fromUtf8(c).trimmed().split(QRegularExpression(
                            QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                    const int behind = ok && n.size() > 0 ? n.at(0).toInt() : 0;
                    const int ahead = ok && n.size() > 1 ? n.at(1).toInt() : 0;
                    QString text, tip;
                    if (ahead > 0 && behind > 0) {
                        text = QString::fromUtf8("\xE2\x86\x91%1 \xE2\x86\x93%2")
                                   .arg(ahead).arg(behind);
                        tip = QStringLiteral("%1 commit(s) ahead of and %2 behind %3")
                                  .arg(ahead).arg(behind).arg(baseBranch);
                    } else if (ahead > 0) {
                        text = QString::fromUtf8("\xE2\x86\x91%1").arg(ahead);
                        tip = QStringLiteral("%1 commit(s) ahead of %2")
                                  .arg(ahead).arg(baseBranch);
                    } else if (behind > 0) {
                        text = QString::fromUtf8("\xE2\x86\x93%1").arg(behind);
                        tip = QStringLiteral("%1 commit(s) behind %2")
                                  .arg(behind).arg(baseBranch);
                    } else {
                        text = QStringLiteral("—");
                        tip = QStringLiteral("Up to date with %1").arg(baseBranch);
                    }
                    const QString want = QDir(wtPath).absolutePath();
                    for (int r = 0; r < m_worktreesTable->rowCount(); ++r) {
                        QTableWidgetItem *p = m_worktreesTable->item(r, 1);
                        if (p && QDir(p->text()).absolutePath() == want) {
                            if (QTableWidgetItem *cc = m_worktreesTable->item(r, 3)) {
                                cc->setText(text);
                                cc->setToolTip(tip);
                            }
                            break;
                        }
                    }
                });
        }

        auto *cell = new QWidget;
        auto *h = new QHBoxLayout(cell);
        h->setContentsMargins(4, 2, 4, 2);
        h->setSpacing(4);
        auto *openBtn = new QPushButton("Open");
        openBtn->setObjectName("ghostButton");
        openBtn->setCursor(Qt::PointingHandCursor);
        const QString p = wt.path;
        connect(openBtn, &QPushButton::clicked, this,
                [p] { QDesktopServices::openUrl(QUrl::fromLocalFile(p)); });
        h->addWidget(openBtn);
        // Issue #295: surface the agent working in this worktree's branch — show
        // its status in the list and let you jump straight to its session.
        const AgentSession *agent = nullptr;
        if (!wt.branch.isEmpty()) {
            for (const AgentSession &s : std::as_const(m_agentSessions)) {
                if (s.owner != repoOwner || s.name != repoName
                    || s.branchName != wt.branch)
                    continue;
                if (!agent) {
                    agent = &s;
                    continue;
                }
                // Prefer a live (non-cleared) session, then the most recent run.
                const bool sLive = s.status != AgentStatus::Cleared;
                const bool curLive = agent->status != AgentStatus::Cleared;
                if ((sLive && !curLive) || (sLive == curLive && s.id > agent->id))
                    agent = &s;
            }
        }
        if (agent) {
            const int agentId = agent->id;
            auto *agentBtn = new QPushButton(
                QString::fromUtf8("Agent \xC2\xB7 %1")
                    .arg(agentStatusText(agent->status)));
            agentBtn->setObjectName("ghostButton");
            agentBtn->setCursor(Qt::PointingHandCursor);
            agentBtn->setIcon(themedOcticon(
                "rocket",
                agent->merged ? QColor("#a371f7") : agentStatusColor(agent->status),
                14));
            agentBtn->setIconSize(QSize(14, 14));
            QString tip = agent->issueNumber > 0
                              ? QString::fromUtf8("Agent #%1 \xC2\xB7 issue #%2 %3")
                                    .arg(agent->id)
                                    .arg(agent->issueNumber)
                                    .arg(agent->issueTitle)
                              : QString::fromUtf8("Agent #%1 \xC2\xB7 %2")
                                    .arg(agent->id)
                                    .arg(agent->issueTitle);
            tip += QString::fromUtf8(" \xC2\xB7 %1").arg(agentStatusText(agent->status));
            if (agent->merged)
                tip += QString::fromUtf8(" \xC2\xB7 merged");
            agentBtn->setToolTip(tip);
            connect(agentBtn, &QPushButton::clicked, this,
                    [this, agentId] { switchToAgentsTab(agentId); });
            h->addWidget(agentBtn);
        }
        if (!isMain && !wt.branch.isEmpty()) {
            const QString branch = wt.branch;
            // Merge this worktree's branch straight into the default branch.
            auto *mergeBtn = new QPushButton("Merge into main");
            mergeBtn->setObjectName("ghostButton");
            mergeBtn->setCursor(Qt::PointingHandCursor);
            setOcticon(mergeBtn, "check-circle", 14);
            connect(mergeBtn, &QPushButton::clicked, this,
                    [this, branch, p] { mergeWorktreeIntoMain(branch, p); });
            h->addWidget(mergeBtn);
            // Or open a pull request from it (the review-first path).
            auto *prBtn = new QPushButton("Create PR");
            prBtn->setObjectName("ghostButton");
            prBtn->setCursor(Qt::PointingHandCursor);
            setOcticon(prBtn, "git-pull-request", 14);
            connect(prBtn, &QPushButton::clicked, this,
                    [this, branch] { createPullFromBranch(branch); });
            h->addWidget(prBtn);
        }
        if (!isMain) {
            // Wipe the worktree, its branch, and the agent that ran on it in one go.
            auto *rmBtn = new QPushButton("Delete");
            rmBtn->setObjectName("ghostButton");
            rmBtn->setCursor(Qt::PointingHandCursor);
            setOcticon(rmBtn, "trash", 14);
            rmBtn->setToolTip(
                agent ? "Remove this worktree, delete its branch and its agent session"
                      : "Remove this worktree and delete its branch");
            const QString branch = wt.branch;
            connect(rmBtn, &QPushButton::clicked, this,
                    [this, p, branch] { deleteWorktreeBranchAndAgent(p, branch); });
            h->addWidget(rmBtn);
        }
        h->addStretch();
        m_worktreesTable->setCellWidget(row, 4, cell);
        actionWidth = qMax(actionWidth, cell->sizeHint().width());
    }
    if (actionWidth > 0)
        m_worktreesTable->setColumnWidth(4, actionWidth + 12);
    if (m_worktreesSummary)
        m_worktreesSummary->setText(
            QString::fromUtf8("\xC2\xB7 %1 worktree(s)").arg(wts.size()));
    if (m_repoWorktreesTab)
        m_repoWorktreesTab->setText(wts.size() > 1
                                        ? QStringLiteral("Worktrees (%1)").arg(wts.size())
                                        : QStringLiteral("Worktrees"));

    // Re-select the worktree that was selected before the rebuild so its diff and
    // the detail buttons stay visible (e.g. right after "Update from main"). If it
    // was removed, no row matches and the pane stays blank, which is correct (#272).
    if (!keepPath.isEmpty()) {
        const QString keep = QDir(keepPath).absolutePath();
        for (int row = 0; row < m_worktreesTable->rowCount(); ++row) {
            QTableWidgetItem *p = m_worktreesTable->item(row, 1);
            if (p && QDir(p->text()).absolutePath() == keep) {
                m_worktreesTable->selectRow(row); // fires currentCellChanged -> diff
                break;
            }
        }
    }
}

// Open the Worktrees tab and select the row whose branch matches, so clicking a
// branch in the agent session header lands on that worktree's changes (#265).
void MainWindow::switchToWorktree(const QString &branch)
{
    if (m_repoDetailTabs && m_repoDetailTabs->button(m_worktreesTabIndex))
        m_repoDetailTabs->button(m_worktreesTabIndex)->setChecked(true);
    if (m_repoDetailStack && m_worktreesTabIndex >= 0)
        m_repoDetailStack->setCurrentIndex(m_worktreesTabIndex);
    loadWorktreesPanel();
    selectWorktreeRow(branch);
}

// Open the Branches tab and select the row whose name matches, so clicking a
// branch name in the agent session header lands on that branch's diff (adhoc
// #123). Selecting the row fires currentCellChanged -> showBranchDiff.
void MainWindow::switchToBranch(const QString &branch)
{
    // The branches panel lives inside the Code overview now (no top-level tab).
    showOverviewBranches();
    loadBranchesPanel();
    if (!m_branchesTable || branch.isEmpty())
        return;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        QTableWidgetItem *it = m_branchesTable->item(row, 0);
        if (it && it->text() == branch) {
            m_branchesTable->selectRow(row); // fires currentCellChanged -> diff
            return;
        }
    }
    // Clicking an agent's branch link when that branch no longer exists here (it
    // may have been merged and deleted, or never synced into this checkout) would
    // otherwise land on the Branches tab with nothing selected. Tell the user why
    // rather than leaving them on a silently empty selection (adhoc #185).
    QMessageBox::information(
        this, QStringLiteral("Branch not found"),
        QStringLiteral("Branch '%1' was not found in this repository. "
                       "It may have been merged and deleted.")
            .arg(branch));
}

bool MainWindow::selectWorktreeRow(const QString &branch)
{
    if (!m_worktreesTable || branch.isEmpty())
        return false;
    for (int row = 0; row < m_worktreesTable->rowCount(); ++row) {
        QTableWidgetItem *b = m_worktreesTable->item(row, 0);
        if (b && b->data(Qt::UserRole).toString() == branch) {
            m_worktreesTable->selectRow(row); // fires currentCellChanged -> diff
            return true;
        }
    }
    return false;
}

// Show a worktree's changes vs the default branch: everything in the worktree
// (committed + uncommitted) when its folder is present, else the branch's commits.
void MainWindow::showWorktreeDiff(const QString &branch, const QString &worktreePath)
{
    if (!m_worktreeDiffView)
        return;
    if (m_worktreeFileList) {
        QSignalBlocker block(m_worktreeFileList);
        m_worktreeFileList->clear();
    }
    if (m_worktreeFilesSummary)
        m_worktreeFilesSummary->clear();

    const QString base = repoDefaultBranch(repoBranches());
    // Remember the selected worktree and (de)activate the detail buttons: only a
    // real feature branch (not the default branch) can be merged either way, and
    // "Update from main" also needs the worktree's folder on disk to merge into.
    m_worktreeSelectedBranch = branch;
    m_worktreeSelectedPath = worktreePath;
    if (m_worktreeBranchLabel) {
        if (branch.isEmpty()) {
            m_worktreeBranchLabel->setText(QString());
        } else {
            // Only link to a folder that actually exists on disk; the merged-away
            // main branch has no separate worktree dir to open.
            const bool onDisk = !worktreePath.isEmpty() && QDir(worktreePath).exists();
            const QString url =
                onDisk ? QUrl::fromLocalFile(worktreePath).toString() : QString();
            const auto link = [&url, onDisk](const QString &html) {
                return onDisk ? QStringLiteral("<a href=\"%1\">%2</a>").arg(url, html)
                              : html;
            };
            QString text = QStringLiteral("On branch %1")
                               .arg(link(QStringLiteral("<b>%1</b>")
                                             .arg(branch.toHtmlEscaped())));
            if (!worktreePath.isEmpty())
                text += QStringLiteral(" · %1").arg(link(worktreePath.toHtmlEscaped()));
            m_worktreeBranchLabel->setText(text);
        }
    }
    const bool feature = !branch.isEmpty() && branch != base;
    QString repoLocal;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
        repoLocal = m_repositories.at(m_repoDetailIndex).localPath;
    const bool isMain = !worktreePath.isEmpty() && !repoLocal.isEmpty() &&
                        QDir(worktreePath).absolutePath() == QDir(repoLocal).absolutePath();
    if (m_worktreeMergeButton)
        m_worktreeMergeButton->setEnabled(feature && repoHasWorkingTree());
    if (m_worktreeMergeDeleteAgentButton)
        m_worktreeMergeDeleteAgentButton->setEnabled(feature && repoHasWorkingTree());
    if (m_worktreeUpdateButton)
        m_worktreeUpdateButton->setEnabled(feature && !worktreePath.isEmpty() &&
                                           QDir(worktreePath).exists());
    // Committing only makes sense when the worktree is on disk; whether it
    // actually has anything to commit is re-checked when the button is clicked.
    if (m_worktreeCommitButton)
        m_worktreeCommitButton->setEnabled(!worktreePath.isEmpty() &&
                                           QDir(worktreePath).exists());
    if (m_worktreeRemoveButton)
        m_worktreeRemoveButton->setEnabled(!isMain && !worktreePath.isEmpty());
    // Surface a "Resolve conflicts" button only when this worktree has a merge in
    // progress that left unmerged (conflicted) files to fix.
    if (m_worktreeResolveButton) {
        bool conflicted = false;
        if (!worktreePath.isEmpty() && QDir(worktreePath).exists()) {
            QByteArray u;
            if (runGitCapture(worktreePath,
                              {"diff", "--name-only", "--diff-filter=U"}, &u, nullptr))
                conflicted = !QString::fromUtf8(u).trimmed().isEmpty();
        }
        m_worktreeResolveButton->setVisible(conflicted);
    }
    QByteArray out;
    bool ok = false;
    if (!worktreePath.isEmpty() && QDir(worktreePath).exists())
        ok = runGitCapture(worktreePath, {"diff", base}, &out, nullptr);
    else if (!branch.isEmpty())
        ok = runGitCapture(repoGitDir(), {"diff", base + ".." + branch}, &out, nullptr);
    if (!ok) {
        m_worktreeDiffView->clear();
        return;
    }

    QList<DiffFileEntry> files;
    const QString html =
        renderDiffHtml(QString::fromUtf8(out), files, repoGitDir(), base, branch,
                       QString(), QHash<QString, QString>(), QSet<QString>());
    setDiffHtml(m_worktreeDiffView,
                html.isEmpty()
                    ? QStringLiteral("<p style='color:#8b949e'>No changes vs %1.</p>")
                          .arg(base.toHtmlEscaped())
                    : html);
    if (m_worktreeFilesSummary)
        m_worktreeFilesSummary->setText(QStringLiteral("%1 file%2 changed")
                                            .arg(files.size())
                                            .arg(files.size() == 1 ? "" : "s"));
    if (m_worktreeFileList) {
        QSignalBlocker block(m_worktreeFileList);
        for (const DiffFileEntry &f : files) {
            const QString name = f.path.section(QLatin1Char('/'), -1);
            auto *item = new QListWidgetItem(
                QString::fromUtf8("%1   +%2 \xE2\x88\x92%3")
                    .arg(name, QString::number(f.adds), QString::number(f.dels)));
            QColor tint("#d29922");
            QString icon = "file-diff";
            if (f.status == QLatin1String("added")) { icon = "diff"; tint = QColor("#3fb950"); }
            else if (f.status == QLatin1String("deleted")) { icon = "trash"; tint = QColor("#f85149"); }
            item->setIcon(themedOcticon(icon, tint, 14));
            item->setData(Qt::UserRole, f.anchor);
            item->setToolTip(QString::fromUtf8("%1 \xC2\xB7 %2").arg(f.status, f.path));
            m_worktreeFileList->addItem(item);
        }
        fitFileListToWidestEntry(m_worktreeFileList);
    }
}

// Merge a worktree's branch into the repo's default branch. Direct + safe: only
// when the primary checkout is ON the default branch and clean (otherwise it
// would clobber concurrent WIP) — else point the user at Create PR.
void MainWindow::mergeWorktreeIntoMain(const QString &branchArg,
                                       const QString &worktreePathArg,
                                       bool deleteAgent)
{
    // Copy by value: the detached merge below runs the event loop before its
    // callback fires, and a refresh could reassign the m_worktreeSelected* members
    // passed here by reference meanwhile — leaving these refs pointing at a new
    // worktree. The callback captures these stable copies instead.
    const QString branch = branchArg;
    const QString worktreePath = worktreePathArg;
    const QString dir = repoGitDir();
    const QString base = repoDefaultBranch(repoBranches());
    if (branch.isEmpty() || branch == base || dir.isEmpty())
        return;
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice("Read-only mirror — nothing to merge into here.", true);
        return;
    }
    const QString current = m_repoBranch.isEmpty() ? base : m_repoBranch;
    if (current != base) {
        setRepoDetailNotice(
            QStringLiteral("Switch the repo to %1 first (it's on %2), or use Create PR.")
                .arg(base, current), true);
        return;
    }
    QByteArray st;
    if (runGitCapture(dir, {"status", "--porcelain"}, &st, nullptr)
        && !QString::fromUtf8(st).trimmed().isEmpty()) {
        setRepoDetailNotice(
            "The checkout has uncommitted changes — commit/stash them or use Create PR.",
            true);
        return;
    }
    if (QMessageBox::question(
            this, QStringLiteral("Merge into %1").arg(base),
            deleteAgent
                ? QStringLiteral("Merge branch %1 into %2, then delete its worktree, "
                                 "branch and agent session?").arg(branch, base)
                : QStringLiteral("Merge branch %1 into %2, then delete its worktree "
                                 "and branch?").arg(branch, base))
        != QMessageBox::Yes)
        return;

    // The merge checks out files, then removeWorktree recursively deletes the
    // worktree folder (slow when it holds build artifacts), then two panels reload
    // — all blocking git on the UI thread. Pump the event loop across the lot so
    // the window stays responsive instead of freezing ("Not Responding").
    GitKeepAlive keepAlive;

    // adhoc #254: bring the branch up to date with base *before* merging it back, so a
    // stale branch (forked before recent base commits) merges cleanly instead of
    // conflicting on base. Do it in the branch's own worktree so any conflicts surface
    // there — where the user/agent can resolve them — and so we never delete a worktree
    // that still holds uncommitted work or a half-finished merge. If it can't update
    // cleanly we keep everything and bail; the into-base merge below then fast-forwards.
    if (!worktreePath.isEmpty() && QDir(worktreePath).exists()
        && QDir(worktreePath).absolutePath() != QDir(dir).absolutePath()) {
        QByteArray wst;
        if (runGitCapture(worktreePath, {"status", "--porcelain"}, &wst, nullptr)
            && !QString::fromUtf8(wst).trimmed().isEmpty()) {
            setRepoDetailNotice(
                QStringLiteral("Worktree %1 has uncommitted changes — commit or stash "
                               "them first (they'd be lost when it's deleted).")
                    .arg(branch),
                true);
            return;
        }
        QString uerr;
        const bool updated = runGitCapture(
            worktreePath,
            {"merge", base, "-m", QStringLiteral("Merge %1 into %2").arg(base, branch)},
            nullptr, &uerr);
        QByteArray uConflicted;
        const bool updateConflicts =
            runGitCapture(worktreePath, {"diff", "--name-only", "--diff-filter=U"},
                          &uConflicted, nullptr)
            && !QString::fromUtf8(uConflicted).trimmed().isEmpty();
        if (!updated || updateConflicts) {
            // Leave the branch as it was and keep the worktree — its work isn't lost.
            runGitCapture(worktreePath, {"merge", "--abort"}, nullptr, nullptr);
            setRepoDetailNotice(
                QStringLiteral("Couldn't update %1 from %2 cleanly (conflicts) — kept "
                               "its worktree and branch. Resolve them with \"Update "
                               "from %2\" or \"Fix with agent\", then merge.")
                    .arg(branch, base),
                true);
            loadWorktreesPanel();
            loadBranchesAndTags();
            return;
        }
    }

    QString err;
    const bool merged =
        runGitCapture(dir,
                      {"merge", "--no-ff", branch,
                       "-m", QStringLiteral("Merge %1 into %2").arg(branch, base)},
                      nullptr, &err);
    // Only treat the merge as clean when git succeeded *and* left no conflicted
    // paths behind. Issue #126: a worktree that couldn't merge cleanly must be kept,
    // not deleted — its commits aren't in main yet, so removing it would discard the
    // only copy of that work. Gate the removal on the repo's actual state rather than
    // git's exit code alone (a killed/slow merge can exit non-zero with the merge
    // already applied, or leave unmerged paths), so we never delete on a dirty merge.
    QByteArray conflicted;
    const bool hasConflicts =
        runGitCapture(dir, {"diff", "--name-only", "--diff-filter=U"}, &conflicted,
                      nullptr) &&
        !QString::fromUtf8(conflicted).trimmed().isEmpty();
    // Authoritative safety gate (adhoc #254): only delete the worktree+branch once
    // the branch's commits are *provably* contained in the base branch — i.e. its tip
    // is now an ancestor of HEAD. The exit-code/conflict checks above can pass while
    // the work isn't actually in main (a stale branch whose merge was aborted, killed,
    // or left half-applied), and deleting then discards the only copy of that work.
    // This is exact: a clean --no-ff merge (or an "Already up to date" no-op) always
    // leaves the branch an ancestor; a merge that didn't land never does.
    const bool branchInBase =
        runGitCapture(dir, {"merge-base", "--is-ancestor", branch, "HEAD"}, nullptr,
                      nullptr);
    if (merged && !hasConflicts && branchInBase) {
        // adhoc #23: an agent's branch just landed in the base branch, so close any
        // issue attached to the session(s) that produced it — mirroring the
        // PR-merge flow's closeIssuesLinkedFromPull. Read the attachments now, while
        // the sessions are still around (the deleteAgent teardown below removes
        // them), and whether or not we delete them so a plain "Merge into main" also
        // resolves the issue.
        if (!branch.isEmpty()
            && m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
            const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
            QList<int> attachedIssues;
            for (const AgentSession &s : std::as_const(m_agentSessions)) {
                if (s.owner == repo.owner && s.name == repo.name
                    && s.branchName == branch && s.issueNumber > 0
                    && !attachedIssues.contains(s.issueNumber))
                    attachedIssues.append(s.issueNumber);
            }
            closeIssuesForMerge(
                attachedIssues,
                QStringLiteral("Closed by merged branch \"%1\".").arg(branch),
                QStringLiteral("merged branch \"%1\"").arg(branch));
        }
        // The branch is now in main, so the worktree has served its purpose — clean
        // it up (silently; the merge was already confirmed). Delete the branch too:
        // its work is preserved in the merge commit, so leaving it behind only
        // clutters the Worktrees/Branches tabs.
        QList<int> deletedAgents;
        if (deleteAgent && !branch.isEmpty()
            && m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
            // Tear down any agent session(s) that produced this branch first, so no
            // runner is left holding the worktree open while we remove it.
            const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
            for (const AgentSession &s : std::as_const(m_agentSessions)) {
                if (s.owner == repo.owner && s.name == repo.name
                    && s.branchName == branch && !isExternalSession(s.id))
                    deletedAgents.append(s.id);
            }
            for (int id : std::as_const(deletedAgents))
                deleteStoredAgentSession(id);
        }
        bool removed = false;
        if (!worktreePath.isEmpty() &&
            QDir(worktreePath).absolutePath() != QDir(dir).absolutePath()) {
            removeWorktree(worktreePath, branch, /*confirm=*/false,
                           /*alsoDeleteBranch=*/true);
            removed = !QDir(worktreePath).exists();
        }
        const QString agentNote =
            deletedAgents.isEmpty()
                ? QString()
                : (deletedAgents.size() == 1
                       ? QStringLiteral(" and deleted its agent session")
                       : QStringLiteral(" and deleted its %1 agent sessions")
                             .arg(deletedAgents.size()));
        setRepoDetailNotice(
            (removed ? QStringLiteral("Merged %1 into %2, removed its worktree and "
                                      "deleted its branch")
                           .arg(branch, base)
                     : QStringLiteral("Merged %1 into %2").arg(branch, base))
                + agentNote + QStringLiteral("."),
            false);
        if (deletedAgents.isEmpty()) {
            // Issue #291: flag any agent session that produced this branch.
            markAgentSessionsMerged(0, branch);
        } else {
            reloadAgents();
            reloadIssues();
            refreshIssueList();
            updateIssueActionState();
        }
        // adhoc #250: with the "Auto after merge" toggle on, bring every other
        // branch up to date with the just-merged base in the same step. It runs
        // without a confirmation prompt and sets its own detail notice
        // summarizing how many branches advanced.
        if (m_branchAutoPullAllCheck && m_branchAutoPullAllCheck->isChecked())
            pullBaseIntoAllBranches();
    } else {
        // The branch did not land cleanly in main — roll back any in-progress merge so
        // the checkout is left clean, and keep the worktree and branch so their work
        // isn't lost (issue #126 / adhoc #254). merge --abort is a harmless no-op when
        // there's nothing to abort.
        runGitCapture(dir, {"merge", "--abort"}, nullptr, nullptr);
        setRepoDetailNotice(
            QStringLiteral("Couldn't merge %1 into %2 cleanly — kept its worktree and "
                           "branch. Update it from %2 to resolve the conflicts (the "
                           "\"Update from %2\" button), or use \"Fix with agent\".")
                .arg(branch, base),
            true);
    }
    loadWorktreesPanel();
    // Issue #211: refresh the cheap branch tip/count, but don't eagerly rebuild
    // the Branches panel — it runs a git command per branch (probing each for
    // merge conflicts), which was slow and pointless here since "Merge into main"
    // is driven from the Agents/Worktrees tabs, not the Branches tab.
    // loadBranchesAndTags() repaints the panel only if it's the visible tab.
    loadBranchesAndTags();
}

void MainWindow::removeWorktree(const QString &worktreePath, const QString &branch,
                                bool confirm, bool alsoDeleteBranch, bool async,
                                std::function<void()> onDone)
{
    if (worktreePath.isEmpty())
        return;
    QString repoPath;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
        repoPath = m_repositories.at(m_repoDetailIndex).localPath;
    if (repoPath.isEmpty())
        return;
    if (QDir(worktreePath).absolutePath() == QDir(repoPath).absolutePath()) {
        setRepoDetailNotice("That's the main checkout — it can't be removed here.",
                            true);
        return;
    }
    // Removing a worktree leaves its branch behind; the Worktrees tab wants that
    // branch gone too (issue #271). Never delete the default branch (it lives in the
    // main checkout) or a detached HEAD (no branch to delete).
    const QString base = repoDefaultBranch(repoBranches());
    const bool deleteBranch =
        alsoDeleteBranch && !branch.isEmpty() && branch != base;
    if (confirm &&
        QMessageBox::question(
            this, QStringLiteral("Remove worktree"),
            (deleteBranch
                 ? QStringLiteral("Remove the worktree at\n%1\nand delete its branch "
                                  "%2?\n\nUncommitted changes there will be lost.")
                 : QStringLiteral("Remove the worktree at\n%1\n(branch %2)?\n\n"
                                  "Uncommitted changes there will be lost."))
                .arg(worktreePath, branch.isEmpty() ? QStringLiteral("-") : branch))
            != QMessageBox::Yes)
        return;
    // Once the worktree folder is gone: with it no longer checked out anywhere the
    // branch can be force-deleted. -D matches the "uncommitted changes will be lost"
    // warning the user just accepted: they asked for the whole worktree — branch and
    // all — to go. Then refresh the panels that listed it.
    auto finish = [this, repoPath, worktreePath, branch, deleteBranch,
                   onDone = std::move(onDone)] {
        // The branch may already be gone (e.g. the agent or a merged PR removed it);
        // that's the outcome we wanted, so don't surface a "branch not found" error.
        if (deleteBranch && localBranchExists(repoPath, branch)) {
            QString err;
            if (runGitCapture(repoPath, {"branch", "-D", branch}, nullptr, &err)) {
                logSystem(
                    QStringLiteral("Git: removed worktree %1 and deleted branch %2.")
                        .arg(worktreePath, branch));
                setRepoDetailNotice(
                    QStringLiteral("Removed worktree and deleted branch %1.").arg(branch));
            } else {
                setRepoDetailNotice(
                    QStringLiteral(
                        "Removed the worktree, but could not delete branch %1: %2")
                        .arg(branch,
                             err.isEmpty() ? QStringLiteral("unknown error") : err),
                    true);
            }
        }
        loadWorktreesPanel();
        if (m_branchesTable)
            loadBranchesPanel();
        if (onDone)
            onDone();
    };

    const QStringList removeArgs{QStringLiteral("-C"), repoPath,
                                 QStringLiteral("worktree"), QStringLiteral("remove"),
                                 QStringLiteral("--force"), worktreePath};

    // async: recursively deleting the worktree folder is slow when it holds build
    // artifacts (node_modules, target/…), and blocking git on the UI thread froze
    // the window for that whole stretch — no clicks registered until it returned
    // (issue #95). Run it as a detached QProcess and continue in finished() so the
    // window stays responsive; the branch delete + refresh follow once it's gone.
    if (async) {
        QProcess *git = new QProcess(this);
        auto failed = [this, worktreePath] {
            setRepoDetailNotice(
                QStringLiteral("Could not remove the worktree at %1.").arg(worktreePath),
                true);
            loadWorktreesPanel();
        };
        connect(git, &QProcess::errorOccurred, this,
                [git, failed](QProcess::ProcessError e) {
                    // Only FailedToStart skips finished(); other errors still emit it.
                    if (e != QProcess::FailedToStart)
                        return;
                    git->deleteLater();
                    failed();
                });
        connect(git, &QProcess::finished, this,
                [git, finish, failed](int code, QProcess::ExitStatus status) {
                    git->deleteLater();
                    if (status != QProcess::NormalExit || code != 0) {
                        failed();
                        return;
                    }
                    finish();
                });
        git->start(QStringLiteral("git"), removeArgs);
        return;
    }

    // Synchronous path: the post-merge cleanup inspects the result inline. It runs
    // under GitKeepAlive so the event loop keeps pumping (window stays painted)
    // while git deletes the folder.
    if (!runGitCapture(repoPath, removeArgs.mid(2), nullptr, nullptr)) {
        setRepoDetailNotice(
            QStringLiteral("Could not remove the worktree at %1.").arg(worktreePath),
            true);
        loadWorktreesPanel();
        return;
    }
    finish();
}

QString MainWindow::worktreePathForBranch(const QString &repoPath,
                                          const QString &branch) const
{
    if (repoPath.isEmpty() || branch.trimmed().isEmpty())
        return QString();
    QByteArray out;
    if (!runGitCapture(repoPath, {QStringLiteral("worktree"), QStringLiteral("list"),
                                  QStringLiteral("--porcelain")},
                       &out, nullptr))
        return QString();
    const QString want = QStringLiteral("refs/heads/%1").arg(branch);
    const QString mainPath = QDir(repoPath).absolutePath();
    QString currentPath;
    for (const QString &raw : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("worktree ")))
            currentPath = line.mid(9).trimmed();
        else if (line.startsWith(QLatin1String("branch "))
                 && line.mid(7).trimmed() == want && !currentPath.isEmpty()
                 && QDir(currentPath).absolutePath() != mainPath)
            return currentPath;
    }
    return QString();
}

bool MainWindow::localBranchExists(const QString &repoPath,
                                   const QString &branch) const
{
    if (repoPath.isEmpty() || branch.trimmed().isEmpty())
        return false;
    return runGitCapture(
        repoPath,
        {QStringLiteral("show-ref"), QStringLiteral("--verify"),
         QStringLiteral("--quiet"),
         QStringLiteral("refs/heads/%1").arg(branch)},
        nullptr, nullptr);
}

// One action to wipe everything an agent left behind: its worktree folder, its
// branch, and the stored agent session(s) that ran on it. Resolves the repo from
// the open detail view; agent sessions are matched by branch.
void MainWindow::deleteWorktreeBranchAndAgent(const QString &worktreePath,
                                              const QString &branch, bool confirm,
                                              bool async, bool deferRefresh)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    const QString repoPath = repo.localPath;
    if (repoPath.isEmpty())
        return;
    if (!worktreePath.isEmpty()
        && QDir(worktreePath).absolutePath() == QDir(repoPath).absolutePath()) {
        setRepoDetailNotice("That's the main checkout — it can't be removed here.",
                            true);
        return;
    }

    // Every stored (non-external) agent session that ran on this branch, plus the
    // issues those sessions were started from: deleting the work means that issue is
    // done, so close it along with the worktree/branch (adhoc #138).
    QList<int> agentIds;
    QSet<int> issueNumbers;
    if (!branch.isEmpty()) {
        for (const AgentSession &s : std::as_const(m_agentSessions)) {
            if (s.owner == repo.owner && s.name == repo.name
                && s.branchName == branch && !isExternalSession(s.id)) {
                agentIds.append(s.id);
                if (s.issueNumber > 0)
                    issueNumbers.insert(s.issueNumber);
            }
        }
    }

    const QString base = repoDefaultBranch(repoBranches());
    const bool willDeleteBranch = !branch.isEmpty() && branch != base;

    // One confirmation covering all three pieces.
    QStringList parts;
    if (!worktreePath.isEmpty())
        parts << QStringLiteral("the worktree at\n%1").arg(worktreePath);
    if (willDeleteBranch)
        parts << QStringLiteral("branch %1").arg(branch);
    if (!agentIds.isEmpty())
        parts << (agentIds.size() == 1
                      ? QStringLiteral("its agent session")
                      : QStringLiteral("its %1 agent sessions").arg(agentIds.size()));
    if (parts.isEmpty())
        return;
    const QString what =
        parts.size() == 1
            ? parts.first()
            : QStringLiteral("%1 and %2").arg(
                  QStringList(parts.mid(0, parts.size() - 1)).join(QStringLiteral(", ")),
                  parts.last());
    QString prompt = QStringLiteral("Delete %1?\n\nUncommitted changes there will be "
                                    "lost. This cannot be undone.")
                         .arg(what);
    if (!issueNumbers.isEmpty())
        prompt += issueNumbers.size() == 1
                      ? QStringLiteral("\n\nThe linked issue #%1 will be closed.")
                            .arg(*issueNumbers.cbegin())
                      : QStringLiteral("\n\nThe %1 linked issues will be closed.")
                            .arg(issueNumbers.size());
    if (confirm
        && QMessageBox::question(this,
                                 QStringLiteral("Delete worktree, branch & agent"),
                                 prompt)
               != QMessageBox::Yes)
        return;

    // The recursive worktree folder delete is already off the UI thread (async
    // removeWorktree below). The bookkeeping that follows still runs synchronous
    // git here — closing issues and, via the reloads, the per-session merge
    // checks. Keep that pumping the event loop so the window stays painted instead
    // of freezing for the whole "Delete all".
    GitKeepAlive keepAlive;

    // Delete the agent session(s) first — that stops any runner still holding the
    // worktree open. If one is mid-stop or we lack permission, bail (it flashed
    // why) before touching the worktree so nothing is half-deleted.
    for (int id : std::as_const(agentIds)) {
        if (!deleteStoredAgentSession(id)) {
            if (!deferRefresh)
                reloadAgents();
            return;
        }
    }

    if (!worktreePath.isEmpty()) {
        // removeWorktree handles the folder + branch and refreshes the panels.
        // async=true so the recursive folder delete runs off the UI thread and the
        // window stays clickable while it works (issue #95). The "Delete all merged"
        // batch passes async=false so its sequential removes don't race each other.
        removeWorktree(worktreePath, branch, /*confirm=*/false,
                       /*alsoDeleteBranch=*/willDeleteBranch, async);
    } else if (willDeleteBranch && localBranchExists(repoPath, branch)) {
        // No worktree left (the agent already cleaned it up) — just drop the branch.
        QString err;
        if (runGitCapture(repoPath, {"branch", "-D", branch}, nullptr, &err)) {
            logSystem(QStringLiteral("Git: deleted branch %1.").arg(branch));
            setRepoDetailNotice(QStringLiteral("Deleted branch %1.").arg(branch));
        } else {
            setRepoDetailNotice(
                err.isEmpty() ? QStringLiteral("Could not delete branch %1.").arg(branch)
                              : err,
                true);
        }
        loadWorktreesPanel();
        if (m_branchesTable)
            loadBranchesPanel();
    }

    // Close the issue(s) those agent sessions were started from — the worktree and
    // branch holding that work are gone, so the issue's work is done (adhoc #138).
    int closedIssues = 0;
    if (!issueNumbers.isEmpty()) {
        const RepositoryRecord &writable = writableRecordFor(repo);
        IssueStore store(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                         m_userName);
        if (store.canWrite()) {
            QHash<int, QString> statusByNumber;
            for (const Issue &issue : store.loadAll())
                statusByNumber.insert(issue.number, issue.status);
            for (const int number : std::as_const(issueNumbers)) {
                // Skip issues that are gone or already closed (no spurious event).
                if (!statusByNumber.contains(number)
                    || statusByNumber.value(number) == QLatin1String("closed"))
                    continue;
                QString err;
                if (store.setStatus(number, QStringLiteral("closed"), &err)) {
                    logSystem(
                        QStringLiteral("Closed issue #%1 (agent worktree deleted).")
                            .arg(number));
                    ++closedIssues;
                } else {
                    logSystem(QStringLiteral("Issue #%1: could not close on delete: %2")
                                  .arg(number)
                                  .arg(err));
                }
            }
        }
    }

    // A batch caller (deferRefresh) rebuilds the agents/issues UI once after the whole
    // run, so skip the per-branch reload here — doing it every iteration tore down and
    // rebuilt the agents table repeatedly, flickering the Status column blank.
    if (!deferRefresh) {
        if (!agentIds.isEmpty()) {
            reloadAgents();
            reloadIssues();
            refreshIssueList();
            updateIssueActionState();
        }
        if (closedIssues > 0) {
            updateRepoIssueCount();
            flashMessage(
                closedIssues == 1
                    ? QStringLiteral("Closed the linked issue.")
                    : QStringLiteral("Closed %1 linked issues.").arg(closedIssues));
        }
    }
}

// Batch "Delete all merged": for every merged agent session in the open repo, wipe
// its worktree folder, branch and stored session — the same cleanup the per-session
// "Delete all" does, but for the whole merged backlog at once. No confirmation —
// merged work is already landed, so it just does it (adhoc #5; the dialog from adhoc
// #235 was removed). Sessions are grouped by branch so a branch with several sessions
// is handled once; deleteWorktreeBranchAndAgent removes all of that branch's sessions
// together.
void MainWindow::deleteAllMergedAgentSessions()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    const QString repoPath = repo.localPath;
    if (repoPath.isEmpty())
        return;

    QStringList branches;
    QSet<QString> seen;
    int sessionCount = 0;
    for (const AgentSession &s : std::as_const(m_agentSessions)) {
        if (s.owner != repo.owner || s.name != repo.name)
            continue;
        // Only landed work, with a real branch, that we actually own (external
        // watch-only sessions have no worktree/branch to remove).
        if (!s.merged || s.branchName.isEmpty() || isExternalSession(s.id))
            continue;
        ++sessionCount;
        if (!seen.contains(s.branchName)) {
            seen.insert(s.branchName);
            branches << s.branchName;
        }
    }
    if (branches.isEmpty()) {
        flashMessage(QStringLiteral("No merged agent sessions to delete."));
        return;
    }

    // No confirmation prompt — merged sessions are landed work, so wiping their
    // worktrees/branches is low-risk and the user asked for it to "just do it"
    // (adhoc #5). Resolve every worktree path up front against the live worktree
    // list, then delete: each call below reloads m_agentSessions, but we iterate
    // the branch snapshot captured here, so that churn can't disturb the loop.
    // confirm=false skips the per-item dialog; async=false so the removes run one
    // at a time instead of racing concurrent `git worktree remove`s; deferRefresh=true
    // so the agents/issues UI is rebuilt once below rather than once per branch — the
    // repeated mid-batch rebuilds flickered the Status column blank ("turns blank over
    // here"). The single refresh at the end keeps the table smooth.
    for (const QString &branch : std::as_const(branches)) {
        const QString wt = worktreePathForBranch(repoPath, branch);
        deleteWorktreeBranchAndAgent(wt, branch, /*confirm=*/false, /*async=*/false,
                                     /*deferRefresh=*/true);
    }
    reloadAgents();
    reloadIssues();
    refreshIssueList();
    updateIssueActionState();
    updateRepoIssueCount();
    flashMessage(QStringLiteral("Deleted %1 merged agent session%2.")
                     .arg(sessionCount)
                     .arg(sessionCount == 1 ? QString() : QStringLiteral("s")));
}

void MainWindow::updateWorktreeFromMain(const QString &worktreePath,
                                        const QString &branch,
                                        const QString &baseArg)
{
    // Prefer the base branch the caller named. Only fall back to the open repo
    // detail's default when none was given: repoDefaultBranch()/repoBranches() read
    // m_repoDetailIndex, which from the agent detail page isn't necessarily this
    // session's repo — there it came back empty and the merge ran as `git merge ""`
    // and silently failed, which is why "Update from main" never worked there
    // (adhoc #28). Bail on an empty base rather than attempting that broken merge.
    const QString base = baseArg.isEmpty() ? repoDefaultBranch(repoBranches()) : baseArg;
    if (worktreePath.isEmpty() || branch.isEmpty() || base.isEmpty() || branch == base)
        return;
    if (!QDir(worktreePath).exists()) {
        setRepoDetailNotice("That worktree's folder is gone.", true);
        loadWorktreesPanel();
        return;
    }
    // A merge into a dirty tree is unsafe — make the user commit/stash first.
    QByteArray st;
    if (runGitCapture(worktreePath, {"status", "--porcelain"}, &st, nullptr) &&
        !QString::fromUtf8(st).trimmed().isEmpty()) {
        setRepoDetailNotice(
            QStringLiteral("Worktree %1 has uncommitted changes — commit or stash "
                           "them before updating from %2.")
                .arg(branch, base),
            true);
        return;
    }
    QString err;
    if (runGitCapture(worktreePath,
                      {"merge", base, "-m",
                       QStringLiteral("Merge %1 into %2").arg(base, branch)},
                      nullptr, &err)) {
        setRepoDetailNotice(
            QStringLiteral("Updated %1 from %2.").arg(branch, base), false);
    } else if (editWorktreeConflicts(worktreePath, branch, base)) {
        // The merge left conflict markers; the user resolved them in the editor.
        setRepoDetailNotice(
            QStringLiteral("Updated %1 from %2 (conflicts resolved).").arg(branch, base),
            false);
    } else {
        setRepoDetailNotice(
            QStringLiteral("Couldn't update %1 from %2 cleanly; the merge was left "
                           "unchanged.")
                .arg(branch, base),
            true);
    }
    // loadWorktreesPanel() preserves the current selection across the rebuild, so
    // focus stays on the worktree we just updated instead of going blank (#272).
    loadWorktreesPanel();
}

// Open the shared merge editor over the worktree's unmerged files. On commit,
// stage everything and finish the merge commit; on cancel, abort the merge. The
// branch/base names only feed the intro text. Returns true iff committed.
bool MainWindow::editWorktreeConflicts(const QString &worktreePath,
                                       const QString &branch, const QString &base)
{
    QByteArray unmerged;
    runGitCapture(worktreePath, {"diff", "--name-only", "--diff-filter=U"},
                  &unmerged, nullptr);
    const QStringList conflicted =
        QString::fromUtf8(unmerged).split('\n', Qt::SkipEmptyParts);
    if (conflicted.isEmpty()) {
        // No markers to edit — nothing in progress (or it failed for another
        // reason). Abort any half-started merge so the worktree is left clean.
        runGitCapture(worktreePath, {"merge", "--abort"}, nullptr, nullptr);
        return false;
    }
    const QString intro =
        QString::fromUtf8(
            "Resolve each conflict, then commit the merge into <b>%1</b>. "
            "<b>Ours</b> is %1; <b>theirs</b> is %2. You can also edit the "
            "text directly.")
            .arg(branch.toHtmlEscaped(), base.toHtmlEscaped());
    const bool committed = runMergeConflictEditor(
        QString::fromUtf8("Resolve conflicts \xE2\x80\x94 %1").arg(branch),
        intro, worktreePath, conflicted, QStringLiteral("Commit merge"),
        [this, worktreePath](QString *e) {
            return runGitCapture(worktreePath, {"add", "-A"}, nullptr, e) &&
                   runGitCapture(worktreePath, {"commit", "--no-edit"}, nullptr, e);
        });
    if (!committed)
        runGitCapture(worktreePath, {"merge", "--abort"}, nullptr, nullptr);
    return committed;
}

void MainWindow::resolveWorktreeConflicts()
{
    const QString worktreePath = m_worktreeSelectedPath;
    const QString branch = m_worktreeSelectedBranch;
    if (worktreePath.isEmpty() || !QDir(worktreePath).exists()) {
        setRepoDetailNotice("That worktree's folder is gone.", true);
        loadWorktreesPanel();
        return;
    }
    const QString base = repoDefaultBranch(repoBranches());
    if (editWorktreeConflicts(worktreePath, branch, base))
        setRepoDetailNotice(
            QStringLiteral("Resolved conflicts in %1.").arg(branch), false);
    else
        setRepoDetailNotice(
            QStringLiteral("Cancelled conflict resolution; %1 was left unchanged.")
                .arg(branch));
    loadWorktreesPanel();
}

void MainWindow::commitWorktreeChanges(const QString &worktreePath,
                                       const QString &branch)
{
    if (worktreePath.isEmpty() || branch.isEmpty())
        return;
    if (!QDir(worktreePath).exists()) {
        setRepoDetailNotice("That worktree's folder is gone.", true);
        loadWorktreesPanel();
        return;
    }
    // Nothing staged or unstaged means there's nothing to commit — say so rather
    // than popping a dialog that would only produce an empty-commit error.
    QByteArray st;
    if (runGitCapture(worktreePath, {"status", "--porcelain"}, &st, nullptr) &&
        QString::fromUtf8(st).trimmed().isEmpty()) {
        setRepoDetailNotice(
            QStringLiteral("Worktree %1 has no changes to commit.").arg(branch),
            false);
        return;
    }
    bool ok = false;
    const QString message =
        QInputDialog::getMultiLineText(this, "Commit changes", "Commit message:",
                                       QString(), &ok)
            .trimmed();
    if (!ok)
        return;
    if (message.isEmpty()) {
        setRepoDetailNotice("A commit message is required.", true);
        return;
    }
    QString err;
    if (runGitCapture(worktreePath, {"add", "-A"}, nullptr, &err) &&
        runGitCapture(worktreePath, {"commit", "-m", message}, nullptr, &err)) {
        setRepoDetailNotice(
            QStringLiteral("Committed changes in %1.").arg(branch), false);
    } else {
        setRepoDetailNotice(
            QStringLiteral("Couldn't commit changes in %1: %2")
                .arg(branch, err.trimmed()),
            true);
    }
    // Preserves the current selection across the rebuild (#272) and refreshes the
    // diff so the just-committed changes show against the default branch.
    loadWorktreesPanel();
}

QWidget *MainWindow::buildBranchesTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    // Flush horizontally: this panel now sits inside the Code overview body stack,
    // whose column already supplies the page's left/right padding (like the
    // commits panel), so it should line up with the file list above it.
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel("Branches");
    heading->setObjectName("channelTitle");
    m_branchesSummary = new QLabel;
    m_branchesSummary->setObjectName("statusLine");
    auto *newBranchButton = new QPushButton("New branch");
    newBranchButton->setObjectName("primaryButton");
    newBranchButton->setCursor(Qt::PointingHandCursor);
    setOcticon(newBranchButton, "git-branch", 16);
    connect(newBranchButton, &QPushButton::clicked, this, &MainWindow::promptNewBranch);
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::loadBranchesPanel);
    addRefreshSpin(refreshButton);
    // Bring every behind branch up to date with the default branch in one click;
    // its label/enabled state is refreshed in loadBranchesPanel() once the base
    // name and behind-counts are known.
    m_branchPullAllButton = new QPushButton("Pull into all");
    m_branchPullAllButton->setObjectName("ghostButton");
    m_branchPullAllButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchPullAllButton, "download", 16);
    connect(m_branchPullAllButton, &QPushButton::clicked, this,
            &MainWindow::pullBaseIntoAllBranches);
    // Opt-in: when checked, every successful "Merge to main" auto-runs the
    // "Pull into all" above so the remaining branches catch up with the merge
    // without a second click (adhoc #250). Persisted so it survives restart.
    m_branchAutoPullAllCheck = new QCheckBox("Auto after merge");
    m_branchAutoPullAllCheck->setCursor(Qt::PointingHandCursor);
    m_branchAutoPullAllCheck->setToolTip(
        "Automatically pull the default branch into every behind branch after a "
        "merge to main succeeds.");
    m_branchAutoPullAllCheck->setChecked(
        QSettings().value(kBranchAutoPullAllSetting, false).toBool());
    connect(m_branchAutoPullAllCheck, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(kBranchAutoPullAllSetting, on);
    });
    // Tidy up branches that are fully merged into the default branch (0 behind and
    // 0 ahead of it); enabled in loadBranchesPanel() once those counts are known.
    m_branchDeleteMergedButton = new QPushButton("Delete merged");
    m_branchDeleteMergedButton->setObjectName("ghostButton");
    m_branchDeleteMergedButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchDeleteMergedButton, "trash", 16);
    connect(m_branchDeleteMergedButton, &QPushButton::clicked, this,
            &MainWindow::deleteMergedBranches);
    headerRow->addWidget(heading);
    headerRow->addWidget(m_branchesSummary);
    headerRow->addStretch();
    headerRow->addWidget(m_branchPullAllButton);
    headerRow->addWidget(m_branchAutoPullAllCheck);
    headerRow->addWidget(refreshButton);
    // "Delete merged" prunes every branch that's 0 behind / 0 ahead of the
    // default branch; keep it right beside "New branch" so the create/cleanup
    // pair sits together at the end of the toolbar (issue #122).
    headerRow->addWidget(m_branchDeleteMergedButton);
    headerRow->addWidget(newBranchButton);
    layout->addLayout(headerRow);

    m_branchesTable = new QTableWidget(0, 6);
    installColumnHeaderMenu(m_branchesTable); // 3-dots per-column menu (issue #318)
    m_branchesTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_branchesTable);
    m_branchesTable->setHorizontalHeaderLabels(
        {"Branch", "Status", "Updated", "Worktree", "Issue / Agent", ""});
    m_branchesTable->verticalHeader()->setVisible(false);
    // Give each row enough height for the sm action buttons (max 28px tall) plus
    // breathing room, so the buttons don't crowd the row above/below.
    m_branchesTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_branchesTable->verticalHeader()->setDefaultSectionSize(36);
    m_branchesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_branchesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_branchesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_branchesTable->setShowGrid(false);
    m_branchesTable->setWordWrap(false);
    QHeaderView *bh = m_branchesTable->horizontalHeader();
    bh->setHighlightSections(false);
    bh->setSectionResizeMode(0, QHeaderView::Stretch);
    bh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    bh->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    // Worktree column: shows the on-disk path of the worktree (if any) a branch
    // is checked out in, so the list surfaces an agent's isolated working tree
    // without a trip to the Worktrees tab. Sized to its content.
    bh->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    // Issue / Agent column: when an agent session is working this branch, name
    // the issue it's attached to (or "Agent" for an ad-hoc run), so the list
    // shows what each branch is for without opening the Agents tab (adhoc #191).
    bh->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    // The action column holds cell widgets (Pull / Create PR / delete
    // buttons). ResizeToContents only measures item delegates and ignores
    // cell widgets, so it would collapse this column and clip the buttons.
    // Keep it Fixed and size it to the actual buttons in loadBranchesPanel().
    bh->setSectionResizeMode(5, QHeaderView::Fixed);
    makeColumnsResizable(m_branchesTable);
    connect(m_branchesTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                QTableWidgetItem *it = m_branchesTable->item(row, 0);
                if (it)
                    setRepoBranch(it->text());
            });
    // Selecting a branch (single click or arrow keys) previews its changes
    // against the default branch in the panel below — no checkout required.
    connect(m_branchesTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                QTableWidgetItem *it = m_branchesTable->item(row, 0);
                showBranchDiff(it ? it->text() : QString());
            });
    // Clicking the Issue / Agent cell jumps to the agent run working that branch,
    // so the list links straight to its session (adhoc #258). Other columns fall
    // through to the normal row-select preview above.
    connect(m_branchesTable, &QTableWidget::cellClicked, this,
            [this](int row, int column) {
                if (column != 4)
                    return;
                QTableWidgetItem *it = m_branchesTable->item(row, 4);
                if (!it)
                    return;
                const QVariant sid = it->data(Qt::UserRole);
                if (sid.isValid())
                    switchToAgentsTab(sid.toInt());
            });

    m_branchDiffView = new QTextBrowser;
    m_branchDiffView->setObjectName("diffView");
    m_branchDiffView->setOpenExternalLinks(false);
    m_branchDiffView->setOpenLinks(false); // we handle "viewed:" anchors ourselves
    m_branchDiffView->setLineWrapMode(QTextEdit::NoWrap);
    connect(m_branchDiffView, &QTextBrowser::anchorClicked, this,
            &MainWindow::onBranchDiffAnchorClicked);
    registerDiffView(m_branchDiffView);
    // Sticky header naming the file currently scrolled into view.
    m_branchDiffSticky = new QLabel(m_branchDiffView->viewport());
    m_branchDiffSticky->setObjectName("diffStickyHeader");
    m_branchDiffSticky->setStyleSheet(diffStickyStyleSheet(m_diffFontPt));
    m_branchDiffSticky->setTextFormat(Qt::RichText);
    m_branchDiffSticky->setOpenExternalLinks(false);
    connect(m_branchDiffSticky, &QLabel::linkActivated, this,
            [this](const QString &href) { onBranchDiffAnchorClicked(QUrl(href)); });
    m_branchDiffSticky->hide();
    connect(m_branchDiffView->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &MainWindow::updateBranchDiffSticky);

    // Scope selector: pick what the diff pane shows for the selected branch —
    // every change it adds over base, its uncommitted working-tree changes, or a
    // single commit. Selecting a row re-renders the diff for that scope.
    m_branchScopeLabel = new QLabel;
    m_branchScopeLabel->setObjectName("sectionLabel");
    m_branchScopeLabel->setTextFormat(Qt::RichText);
    m_branchScopeList = new QListWidget;
    m_branchScopeList->setObjectName("overviewList");
    enableHoverRowHighlight(m_branchScopeList); // green outline selection (issue #252)
    m_branchScopeList->setMinimumWidth(180);
    connect(m_branchScopeList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) { renderBranchScopeDiff(); });

    // Changed-files list beside the diff (same pattern as the commit/PR viewers):
    // click a file to scroll the diff straight to it.
    m_branchFilesSummary = new QLabel;
    m_branchFilesSummary->setObjectName("sectionLabel");
    m_branchFilesSummary->setTextFormat(Qt::RichText);
    m_branchFileList = new QListWidget;
    m_branchFileList->setObjectName("overviewList");
    enableHoverRowHighlight(m_branchFileList); // green outline selection (issue #252)
    m_branchFileList->setMinimumWidth(180);
    connect(m_branchFileList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item && m_branchDiffView)
                    m_branchDiffView->scrollToAnchor(
                        item->data(Qt::UserRole).toString());
            });
    auto *filesPane = new QWidget;
    auto *filesLayout = new QVBoxLayout(filesPane);
    filesLayout->setContentsMargins(0, 0, 0, 0);
    filesLayout->setSpacing(6);
    filesLayout->addWidget(m_branchScopeLabel);
    filesLayout->addWidget(m_branchScopeList, 1);
    filesLayout->addWidget(m_branchFilesSummary);
    filesLayout->addWidget(m_branchFileList, 1);

    // Detail pane: a toolbar with the primary branch actions over its diff. These
    // act on whichever branch is selected (m_branchDiffBranch), so the common
    // actions are reachable at the top while reviewing the changes rather than
    // hunting for the matching row button (issue #116). Their enabled/tooltip
    // state is set in updateBranchDetailActions() as the selection changes.
    m_branchDetailLabel = new QLabel;
    m_branchDetailLabel->setObjectName("sectionLabel");
    m_branchDetailLabel->setTextFormat(Qt::RichText);

    // Open in Codium: launch VSCodium on the selected branch's working directory
    // (its worktree, or the main checkout) so the branch can be edited in the IDE
    // without dropping to a terminal. Disabled when no local checkout exists.
    m_branchOpenCodiumButton = new QPushButton("Open in Codium");
    m_branchOpenCodiumButton->setObjectName("ghostButton");
    m_branchOpenCodiumButton->setProperty("buttonSize", "sm");
    m_branchOpenCodiumButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchOpenCodiumButton, "code", 14);
    m_branchOpenCodiumButton->setEnabled(false);
    connect(m_branchOpenCodiumButton, &QPushButton::clicked, this, [this] {
        if (!m_branchDiffBranch.isEmpty())
            openBranchInCodium(m_branchDiffBranch);
    });

    // Merge editor: the hands-on path to bring the branch up to date with base,
    // opening the interactive conflict editor so conflicts can be resolved by
    // hand (the manual counterpart to "Fix with agent"). Sits left of "Pull
    // main", which one-clicks the merge and only surfaces the editor on conflict.
    m_branchMergeEditorButton = new QPushButton("Merge editor");
    m_branchMergeEditorButton->setObjectName("ghostButton");
    m_branchMergeEditorButton->setProperty("buttonSize", "sm");
    m_branchMergeEditorButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchMergeEditorButton, "git-merge", 14);
    m_branchMergeEditorButton->setEnabled(false);
    connect(m_branchMergeEditorButton, &QPushButton::clicked, this, [this] {
        if (!m_branchDiffBranch.isEmpty())
            openBranchMergeEditor(m_branchDiffBranch);
    });

    m_branchPullButton = new QPushButton("Pull main");
    m_branchPullButton->setObjectName("ghostButton");
    m_branchPullButton->setProperty("buttonSize", "sm");
    m_branchPullButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchPullButton, "download", 14);
    m_branchPullButton->setEnabled(false);
    connect(m_branchPullButton, &QPushButton::clicked, this, [this] {
        if (!m_branchDiffBranch.isEmpty())
            updateBranchFromBase(m_branchDiffBranch);
    });

    // Fix with agent: only relevant when the selected branch conflicts with base,
    // so updateBranchDetailActions() hides it otherwise. The button now resolves
    // straight away (no menu); the agent and model are chosen in the two dropdowns
    // beside it (adhoc #56).
    m_branchFixButton = new QPushButton("Fix with agent");
    m_branchFixButton->setObjectName("ghostButton");
    m_branchFixButton->setProperty("buttonSize", "sm");
    m_branchFixButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchFixButton, "rocket", 14);
    m_branchFixButton->hide();
    connect(m_branchFixButton, &QPushButton::clicked, this, [this] {
        if (m_branchDiffBranch.isEmpty())
            return;
        const QString provider = m_branchFixAgentCombo
                                     ? m_branchFixAgentCombo->currentData().toString()
                                     : QStringLiteral("claude");
        const QString model = m_branchFixModelCombo
                                  ? m_branchFixModelCombo->currentData().toString()
                                  : QString();
        fixBranchConflictsWithAgent(m_branchDiffBranch, provider, model);
    });

    // Agent dropdown: which provider resolves the conflicts. Data values match the
    // strings fixBranchConflictsWithAgent expects ("claude" is the Claude API).
    m_branchFixAgentCombo = new QComboBox;
    m_branchFixAgentCombo->setObjectName("issueControlSm");
    m_branchFixAgentCombo->setCursor(Qt::PointingHandCursor);
    m_branchFixAgentCombo->setToolTip("Which agent resolves the conflicts");
    m_branchFixAgentCombo->addItem(QStringLiteral("Claude"), QStringLiteral("claude"));
    m_branchFixAgentCombo->addItem(QStringLiteral("OpenAI"), QStringLiteral("openai"));
    m_branchFixAgentCombo->addItem(QStringLiteral("Claude Code"),
                                   QStringLiteral("claude-code"));
    m_branchFixAgentCombo->hide();
    // Start on the user's configured default agent (Settings -> Agents). That
    // setting stores the Claude API as "claude-api"; the combo uses "claude".
    {
        const QString def = defaultAgentProvider();
        const QString want = def == QLatin1String("claude-api")
                                 ? QStringLiteral("claude")
                                 : def;
        const int idx = m_branchFixAgentCombo->findData(want);
        m_branchFixAgentCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    // Model dropdown: refilled to match the selected agent (e.g. Opus / Sonnet /
    // Haiku for Claude).
    m_branchFixModelCombo = new QComboBox;
    m_branchFixModelCombo->setObjectName("issueControlSm");
    m_branchFixModelCombo->setCursor(Qt::PointingHandCursor);
    m_branchFixModelCombo->setToolTip("Which model the agent uses");
    m_branchFixModelCombo->setProperty("claudeModelCombo", true);
    m_branchFixModelCombo->view()->installEventFilter(this);
    m_branchFixModelCombo->hide();
    fillAgentFixModelCombo(m_branchFixModelCombo,
                           m_branchFixAgentCombo->currentData().toString());
    refreshClaudeModelCombo();
    connect(m_branchFixAgentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (m_branchFixAgentCombo && m_branchFixModelCombo) {
                    fillAgentFixModelCombo(
                        m_branchFixModelCombo,
                        m_branchFixAgentCombo->currentData().toString());
                    refreshClaudeModelCombo();
                }
            });

    m_branchPrButton = new QPushButton("Create PR");
    m_branchPrButton->setObjectName("ghostButton");
    m_branchPrButton->setProperty("buttonSize", "sm");
    m_branchPrButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchPrButton, "git-pull-request", 14);
    m_branchPrButton->setEnabled(false);
    connect(m_branchPrButton, &QPushButton::clicked, this, [this] {
        if (!m_branchDiffBranch.isEmpty())
            createPullFromBranch(m_branchDiffBranch);
    });

    // Merge the selected branch straight into the default branch (an empty
    // worktree path tells mergeWorktreeIntoMain not to prune any worktree).
    m_branchMergeButton = new QPushButton("Merge to main");
    m_branchMergeButton->setObjectName("primaryButton");
    m_branchMergeButton->setProperty("buttonSize", "sm");
    m_branchMergeButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchMergeButton, "check-circle", 14);
    m_branchMergeButton->setEnabled(false);
    connect(m_branchMergeButton, &QPushButton::clicked, this, [this] {
        if (!m_branchDiffBranch.isEmpty())
            mergeWorktreeIntoMain(m_branchDiffBranch, QString());
    });

    auto *detailBar = new QHBoxLayout;
    detailBar->setContentsMargins(0, 0, 0, 0);
    detailBar->addWidget(m_branchDetailLabel);
    detailBar->addStretch();
    detailBar->addWidget(m_branchOpenCodiumButton);
    detailBar->addWidget(m_branchMergeEditorButton);
    detailBar->addWidget(m_branchPullButton);
    detailBar->addWidget(m_branchFixButton);
    detailBar->addWidget(m_branchFixAgentCombo);
    detailBar->addWidget(m_branchFixModelCombo);
    detailBar->addWidget(m_branchPrButton);
    detailBar->addWidget(m_branchMergeButton);
    auto *diffPane = new QWidget;
    auto *diffPaneLayout = new QVBoxLayout(diffPane);
    diffPaneLayout->setContentsMargins(0, 0, 0, 0);
    diffPaneLayout->setSpacing(6);
    diffPaneLayout->addLayout(detailBar);
    diffPaneLayout->addWidget(m_branchDiffView, 1);

    auto *split = new QSplitter(Qt::Horizontal);
    m_branchesSplit = split;
    split->setChildrenCollapsible(false);
    split->addWidget(m_branchesTable);
    split->addWidget(filesPane);
    split->addWidget(diffPane);
    // Let the branches table share the window's extra width with the diff pane
    // rather than staying pinned narrow while only the diff grew. The old 0/0/1
    // factors sent every extra pixel to the diff, so on a wide window the table
    // stayed cramped and its Branch/Worktree columns clipped — you had to drag the
    // divider to read the full list. Now the table grows too (factor 1), the file
    // list stays compact (0), and the table keeps showing everything as the window
    // widens (#205).
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setStretchFactor(2, 1);
    // Open the branches list expanded — the table takes the larger share so every
    // column (Branch, Status, Updated, Worktree, Issue/Agent, actions) is visible
    // at a glance without dragging the divider (#205; was ~50% per adhoc #193).
    split->setSizes({1100, 200, 600});
    layout->addWidget(split, 1);
    return page;
}

void MainWindow::loadBranchesPanel()
{
    if (!m_branchesTable)
        return;
    // Re-entrancy guard: the GitKeepAlive below pumps the event loop between the
    // per-branch git reads, so a queued reload (a network/roster callback) must
    // not start a second pass that clears the half-built table out from under us.
    // (Mirrors the m_repoDetailLoading guard in openRepoDetail.)
    if (m_branchesPanelLoading)
        return;
    QScopedValueRollback<bool> loadingGuard(m_branchesPanelLoading, true);
    // Each row's ahead/behind count and in-memory merge-conflict probe shells out
    // to git serially below; on a repo with many branches that blocked the GUI
    // thread for ~2s and tripped the stall watchdog (adhoc #222). Keep the event
    // loop pumping across the batch so the window stays responsive (waitForGit
    // polls in short slices while g_gitKeepAliveDepth > 0) instead of freezing.
    GitKeepAlive keepAlive;
    // Remember which branch's diff is on screen so we can re-render it at the end
    // (now reflecting any merge we just performed).
    const QString previouslyViewed = m_branchDiffBranch;
    // Freeze the whole splitter — table, changed-files/scope lists and diff view —
    // while we tear down and rebuild the rows so a refresh after a merge/delete
    // doesn't flash all of them blank before the new contents land; they all
    // repaint once together when the guard lifts (adhoc #256).
    TableRepaintGuard repaintGuard(m_branchesSplit ? m_branchesSplit
                                                   : static_cast<QWidget *>(m_branchesTable));
    // Block the table's selection signals across the rebuild so clearing the rows
    // doesn't fire currentCellChanged -> showBranchDiff(empty), which would blank
    // the diff pane and churn m_branchDiffBranch mid-rebuild. We re-render the
    // viewed branch's diff explicitly at the end instead (adhoc #256).
    QSignalBlocker branchesTableBlock(m_branchesTable);
    m_branchesTable->setRowCount(0);
    const QString dir = repoGitDir();
    QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);
    // Always pin the default branch ("main") to the top of the list, regardless
    // of which feature branch was committed to most recently — repoBranches()
    // sorts by committer date, so without this main sinks below active branches
    // (adhoc #185).
    if (!base.isEmpty() && branches.removeOne(base))
        branches.prepend(base);
    const QString selected = m_repoBranch.isEmpty() ? base : m_repoBranch;
    const bool writable = repoHasWorkingTree();

    // Remote-tracking branches (refs/remotes/*): the branches other nodes / the
    // relay have published, which `git branch` (local heads only, via
    // repoBranches()) leaves out. The list should show every branch in the repo,
    // including these refs, under their full ref-qualified name (adhoc #55).
    // Rendered read-only after the local branches below. Skip each remote's
    // symbolic */HEAD pointer and the bare remote name (e.g. "origin"), which
    // aren't branches; a remote-tracking ref is always "<remote>/<branch>".
    QStringList remoteBranches;
    if (!dir.isEmpty()) {
        QByteArray rout;
        if (runGitCapture(dir,
                          {"for-each-ref", "--sort=-committerdate",
                           "--format=%(refname:short)", "refs/remotes/"},
                          &rout, nullptr)) {
            for (const QString &line :
                 QString::fromUtf8(rout).split('\n', Qt::SkipEmptyParts)) {
                const QString ref = line.trimmed();
                if (ref.isEmpty() || !ref.contains(u'/') ||
                    ref.endsWith(QLatin1String("/HEAD")))
                    continue;
                if (!remoteBranches.contains(ref))
                    remoteBranches.append(ref);
            }
        }
    }

    if (m_branchesSummary) {
        const QString def = base.isEmpty() ? QStringLiteral("none") : base;
        const QString total =
            remoteBranches.isEmpty()
                ? QString::number(branches.size())
                : QString::fromUtf8("%1 local \xC2\xB7 %2 remote")
                      .arg(branches.size())
                      .arg(remoteBranches.size());
        m_branchesSummary->setText(
            QString::fromUtf8("\xC2\xB7 %1 \xC2\xB7 default: %2").arg(total, def));
    }

    // Branch commit timestamps in one batch: spawning a `git log -1` per branch
    // (below) blocked the UI thread for ~2s on repos with many branches because
    // each row started its own git process serially (issue #152). for-each-ref
    // returns every branch tip's committer date in a single call.
    QHash<QString, qint64> branchTimes;
    QHash<QString, QString> branchShortShas;
    QHash<QString, QString> branchSubjects;
    QHash<QString, QString> branchAuthors;
    if (!dir.isEmpty()) {
        QByteArray times;
        if (runGitCapture(
                dir,
                {"for-each-ref",
                 "--format=%(refname:short)%09%(committerdate:unix)%09"
                 "%(objectname:short)%09%(subject)%09%(authorname)",
                 "refs/heads/", "refs/remotes/"},
                &times, nullptr)) {
            for (const QString &line :
                 QString::fromUtf8(times).split('\n', Qt::SkipEmptyParts)) {
                const QStringList parts = line.split(QLatin1Char('\t'));
                if (parts.size() < 2)
                    continue;
                const QString ref = parts.at(0);
                branchTimes.insert(ref, parts.at(1).toLongLong());
                if (parts.size() >= 3)
                    branchShortShas.insert(ref, parts.at(2));
                if (parts.size() >= 4)
                    branchSubjects.insert(ref, parts.at(3));
                if (parts.size() >= 5)
                    branchAuthors.insert(ref, parts.at(4));
            }
        }
    }

    // Ahead/behind of each remote-tracking branch vs the default branch, so those
    // rows can show how far they've diverged from main just like the local ones do
    // (adhoc #61). One batched `for-each-ref` (git 2.41+ '%(ahead-behind:<base>)')
    // rather than a rev-list per branch keeps it cheap even when a repo carries
    // hundreds of remote refs. The atom emits "<ahead> <behind>"; the hash stays
    // empty (rows fall back to a plain "Remote" label) when the field or base is
    // unavailable, e.g. on older git.
    QHash<QString, QPair<int, int>> remoteAheadBehind; // ref -> (ahead, behind)
    if (!dir.isEmpty() && !base.isEmpty() && !remoteBranches.isEmpty()) {
        QByteArray ab;
        if (runGitCapture(
                dir,
                {"for-each-ref",
                 QStringLiteral("--format=%(refname:short) %(ahead-behind:%1)").arg(base),
                 "refs/remotes/"},
                &ab, nullptr)) {
            for (const QString &line :
                 QString::fromUtf8(ab).split('\n', Qt::SkipEmptyParts)) {
                const QStringList parts = line.split(
                    QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                // "<ref> <ahead> <behind>"; ref names never contain whitespace.
                if (parts.size() >= 3)
                    remoteAheadBehind.insert(
                        parts.first(),
                        qMakePair(parts.at(1).toInt(), parts.at(2).toInt()));
            }
        }
    }

    // Map each branch to the agent session working it (if any), scoped to the
    // current repo, so the per-row "Issue / Agent" column can name the issue the
    // branch is attached to (or flag an ad-hoc agent run) (adhoc #191). A branch
    // may carry more than one session over its life; prefer one bound to an issue
    // and otherwise the most recent.
    //
    // Stored by value, not by pointer into m_agentSessions: the per-row git reads
    // further down run under GitKeepAlive, which pumps the event loop, and a
    // queued callback landing mid-pump (e.g. an agent finishing/being deleted)
    // can append/remove entries and reallocate that list. A pointer taken here
    // would dangle and crash (free(): invalid pointer) when later dereferenced —
    // this is what crashed on a branch click after a merge freed its agent
    // session (adhoc #200).
    QHash<QString, AgentSession> branchSessions;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        for (const AgentSession &session : m_agentSessions) {
            if (session.branchName.isEmpty() || session.owner != repo.owner ||
                session.name != repo.name)
                continue;
            const auto existing = branchSessions.constFind(session.branchName);
            if (existing == branchSessions.constEnd() ||
                (session.issueNumber > 0 && existing->issueNumber <= 0) ||
                ((session.issueNumber > 0) == (existing->issueNumber > 0) &&
                 session.id > existing->id))
                branchSessions.insert(session.branchName, session);
        }
    }

    // Map each branch to the worktree (other than the main checkout) it's checked
    // out in, in a single `git worktree list --porcelain` call so the per-row
    // Worktree column below doesn't spawn a git process each (issue #172).
    QHash<QString, QString> branchWorktrees;
    if (!dir.isEmpty()) {
        QByteArray wtOut;
        if (runGitCapture(dir, {"worktree", "list", "--porcelain"}, &wtOut, nullptr)) {
            // Compare canonical paths so a symlinked checkout root (e.g. /tmp on
            // some platforms) doesn't make the main worktree look like a separate
            // branch worktree; fall back to the absolute path if it can't resolve.
            QString mainPath = QFileInfo(dir).canonicalFilePath();
            if (mainPath.isEmpty())
                mainPath = QDir(dir).absolutePath();
            QString currentPath;
            for (const QString &raw :
                 QString::fromUtf8(wtOut).split(QLatin1Char('\n'))) {
                const QString line = raw.trimmed();
                if (line.startsWith(QLatin1String("worktree ")))
                    currentPath = line.mid(9).trimmed();
                else if (line.startsWith(QLatin1String("branch "))) {
                    const QString br =
                        line.mid(7).trimmed().replace(QLatin1String("refs/heads/"),
                                                      QString());
                    QString canonicalPath = QFileInfo(currentPath).canonicalFilePath();
                    if (canonicalPath.isEmpty())
                        canonicalPath = QDir(currentPath).absolutePath();
                    if (!br.isEmpty() && !currentPath.isEmpty()
                        && canonicalPath != mainPath)
                        branchWorktrees.insert(br, currentPath);
                }
            }
        }
    }

    // The action column is Fixed-width because ResizeToContents can't see
    // its cell widgets; size it to the widest action row we build below.
    int actionWidth = 0;
    bool anyBehind = false;
    bool anyMerged = false; // fully-merged branches the "Delete merged" action can remove
    for (const QString &branch : branches) {
        const int row = m_branchesTable->rowCount();
        m_branchesTable->insertRow(row);

        auto *name = new QTableWidgetItem(branch);
        if (branch == selected)
            name->setIcon(themedOcticon("check-circle", QColor("#3fb950"), 14));
        else
            name->setIcon(themedOcticon("git-branch", QColor("#8b949e"), 14));
        {
            const QString sha = branchShortShas.value(branch);
            const QString subj = branchSubjects.value(branch);
            const QString auth = branchAuthors.value(branch);
            QString tip;
            if (!sha.isEmpty())
                tip = sha;
            if (!subj.isEmpty())
                tip += (tip.isEmpty() ? QString() : QStringLiteral(" \xC2\xB7 ")) + subj;
            if (!auth.isEmpty())
                tip += (tip.isEmpty() ? QString() : QStringLiteral(" \xC2\xB7 by ")) + auth;
            if (!tip.isEmpty())
                name->setToolTip(tip);
        }
        m_branchesTable->setItem(row, 0, name);

        // Ahead/behind vs the default branch.
        QString status = branch == base ? QStringLiteral("Default branch") : QString();
        int behind = 0;
        int ahead = 0;
        bool hasConflict = false;
        qint64 ts = 0;
        if (!dir.isEmpty()) {
            if (branch != base) {
                QByteArray counts;
                if (runGitCapture(dir,
                                  {"rev-list", "--left-right", "--count",
                                   base + "..." + branch},
                                  &counts, nullptr)) {
                    const QStringList parts =
                        QString::fromUtf8(counts).trimmed().split(
                            QRegularExpression(QStringLiteral("\\s+")));
                    if (parts.size() >= 2) {
                        behind = parts.at(0).toInt();
                        ahead = parts.at(1).toInt();
                        status = QString::fromUtf8("%1 behind \xC2\xB7 %2 ahead")
                                     .arg(parts.at(0), parts.at(1));
                    }
                }
                // Only a branch with its own commits *and* base commits it lacks
                // can conflict; probe that case with an in-memory merge so the row
                // can flag it and offer "Fix with agent".
                if (behind > 0 && ahead > 0)
                    hasConflict = branchMergeTree(dir, base, branch).isEmpty();
                if (hasConflict)
                    status += QString::fromUtf8(" \xC2\xB7 conflicts");
            }
            ts = branchTimes.value(branch, 0);
        }
        auto *statusItem = new QTableWidgetItem(status);
        if (hasConflict) {
            statusItem->setForeground(QColor("#f85149"));
            statusItem->setIcon(themedOcticon("alert", QColor("#f85149"), 13));
        }
        m_branchesTable->setItem(row, 1, statusItem);
        auto *updated = new QTableWidgetItem(formatShortRelativeTime(ts));
        {
            const QString subj = branchSubjects.value(branch);
            const QString auth = branchAuthors.value(branch);
            QString utip;
            if (!auth.isEmpty())
                utip = QStringLiteral("by %1").arg(auth);
            if (!subj.isEmpty())
                utip += (utip.isEmpty() ? QString() : QStringLiteral(": ")) + subj;
            if (!utip.isEmpty())
                updated->setToolTip(utip);
        }
        m_branchesTable->setItem(row, 2, updated);

        // Worktree this branch is checked out in (an agent's isolated tree), if
        // any. Shown so the list reveals where the branch lives on disk; the full
        // path is also the cell tooltip for the long /tmp agent-worktree paths.
        const QString worktreePath = branchWorktrees.value(branch);
        auto *worktree = new QTableWidgetItem(worktreePath);
        if (!worktreePath.isEmpty()) {
            worktree->setIcon(themedOcticon("file-directory", QColor("#8b949e"), 13));
            worktree->setToolTip(worktreePath);
            worktree->setForeground(QColor("#8b949e"));
        }
        m_branchesTable->setItem(row, 3, worktree);

        // Issue / Agent this branch is attached to. When an agent session is
        // working the branch, name its issue ("#N") or flag an ad-hoc run
        // ("Agent") and stamp the session's status icon — a green spinner while
        // running, a check on success, an x on failure, etc. — so the list shows
        // both what each branch is for and how its agent is doing at a glance
        // (adhoc #191, #251). The text says which it is; the tooltip leads with
        // the status word and spells out the issue title / prompt.
        auto *attach = new QTableWidgetItem;
        const auto sessionIt = branchSessions.constFind(branch);
        if (sessionIt != branchSessions.constEnd()) {
            const AgentSession *session = &sessionIt.value();
            const QString statusWord =
                session->merged ? QStringLiteral("merged")
                                : agentStatusText(session->status);
            QString detail;
            if (session->issueNumber > 0) {
                // Show issue number + status word so you can tell at a glance
                // whether the agent is still running or has finished.
                attach->setText(
                    QString::fromUtf8("#%1 \xC2\xB7 %2")
                        .arg(session->issueNumber)
                        .arg(statusWord));
                detail = session->issueTitle.isEmpty()
                             ? QStringLiteral("Issue #%1").arg(session->issueNumber)
                             : QStringLiteral("Issue #%1: %2")
                                   .arg(session->issueNumber)
                                   .arg(session->issueTitle);
            } else {
                attach->setText(
                    QString::fromUtf8("Agent \xC2\xB7 %1").arg(statusWord));
                detail = session->prompt;
            }
            attach->setIcon(agentStatusOcticon(*session));
            // Stash the session id so a click on this cell can jump straight to
            // the agent run working the branch (adhoc #258).
            attach->setData(Qt::UserRole, session->id);
            // Underline the text so the cell reads as the clickable link it now
            // is (the tooltip below spells out the action).
            QFont linkFont = attach->font();
            linkFont.setUnderline(true);
            attach->setFont(linkFont);
            // Tooltip: status · issue/prompt · model · provider · cost · PR · turns.
            QString tip =
                detail.isEmpty()
                    ? statusWord
                    : QStringLiteral("%1 \xC2\xB7 %2").arg(statusWord, detail);
            if (!session->model.isEmpty())
                tip += QStringLiteral(" \xC2\xB7 model: %1").arg(session->model);
            if (!session->provider.isEmpty())
                tip += QStringLiteral(" \xC2\xB7 %1").arg(session->provider);
            if (session->costUsd > 0.0)
                tip += QStringLiteral(" \xC2\xB7 $%1")
                           .arg(session->costUsd, 0, 'f', 4);
            if (session->prNumber > 0)
                tip += QStringLiteral(" \xC2\xB7 PR #%1").arg(session->prNumber);
            if (session->numTurns > 0)
                tip += QStringLiteral(" \xC2\xB7 %1 turns").arg(session->numTurns);
            attach->setToolTip(
                QString::fromUtf8("%1 \xE2\x80\x94 click to open agent").arg(tip));
            attach->setForeground(session->merged ? QColor("#a371f7")
                                                  : agentStatusColor(session->status));
        }
        m_branchesTable->setItem(row, 4, attach);

        // Row actions: just delete here — the Pull / Fix with agent / Create PR /
        // Merge to main actions live in the detail-pane toolbar and act on the
        // selected branch (issue #116). The ahead/behind/conflict counts above
        // still drive the header's "Pull into all" / "Delete merged" enablement.
        if (writable && branch != base && behind > 0)
            anyBehind = true;
        auto *actions = new QWidget;
        // Keep the container transparent so the row's hover/selection highlight
        // shows through it; the global "QWidget { background }" rule would
        // otherwise paint an opaque box over the highlighted row.
        actions->setObjectName("branchActions");
        actions->setStyleSheet("#branchActions { background: transparent; }");
        auto *actionRow = new QHBoxLayout(actions);
        actionRow->setContentsMargins(0, 0, 8, 0);
        actionRow->setSpacing(4);

        // Delete button (disabled for the default/checked-out branch).
        auto *del = new QPushButton;
        del->setObjectName("issueIconButton");
        del->setFlat(true);
        del->setCursor(Qt::PointingHandCursor);
        del->setIcon(themedOcticon("trash", QColor("#f85149"), 15));
        del->setIconSize(QSize(15, 15));
        del->setToolTip(QStringLiteral("Delete branch %1").arg(branch));
        const bool canDelete = writable && branch != base && branch != selected;
        del->setEnabled(canDelete);
        // A deletable branch sitting at the base tip (0 behind, 0 ahead) is fully
        // merged and a candidate for the header's one-click "Delete merged".
        if (canDelete && behind == 0 && ahead == 0)
            anyMerged = true;
        if (!canDelete)
            del->setToolTip(writable
                                ? "Can't delete the default or current branch"
                                : "Read-only mirror — no working tree to delete from");
        connect(del, &QPushButton::clicked, this,
                [this, branch] { deleteBranch(branch); });
        actionRow->addWidget(del);

        m_branchesTable->setCellWidget(row, 5, actions);
        // Measure the true width the delete button needs:
        //  - ensurePolished() applies the sm-button stylesheet (font-size/padding),
        //    which sizeHint() ignores until the style is in effect;
        //  - pin each button to its natural width so a tight column can't
        //    compress and clip it;
        //  - invalidate the row layout so it recomputes its hint from the now
        //    polished buttons instead of the stale (too-small) cached value.
        actions->ensurePolished();
        for (QPushButton *b : actions->findChildren<QPushButton *>()) {
            b->ensurePolished();
            b->setMinimumWidth(b->sizeHint().width());
        }
        actionRow->invalidate();
        actionWidth = qMax(actionWidth, actions->sizeHint().width());
    }
    if (actionWidth > 0)
        // A little slack so the rightmost button never sits flush against the
        // column edge (the action row already carries an 8px right margin).
        m_branchesTable->horizontalHeader()->resizeSection(5, actionWidth + 8);

    // Remote-tracking branches, listed read-only under their full ref-qualified
    // name (e.g. "origin/feature", "nnn/issue-9") so the panel shows every branch
    // in the repo, not just the local heads (adhoc #55). No row actions here (these
    // aren't checked out locally), but the Status column shows their ahead/behind
    // vs the default branch from the batched probe above so the divergence info
    // matches the local rows (adhoc #61). Clicking one still renders its diff vs
    // the default branch.
    for (const QString &branch : remoteBranches) {
        const int row = m_branchesTable->rowCount();
        m_branchesTable->insertRow(row);

        auto *name = new QTableWidgetItem(branch);
        name->setIcon(themedOcticon("repo-forked", QColor("#8b949e"), 14));
        name->setForeground(QColor("#8b949e"));
        {
            const QString rsha = branchShortShas.value(branch);
            const QString rsubj = branchSubjects.value(branch);
            const QString rauth = branchAuthors.value(branch);
            QString rtname = QStringLiteral("Remote-tracking branch %1").arg(branch);
            if (!rsha.isEmpty())
                rtname += QStringLiteral("\n%1").arg(rsha);
            if (!rsubj.isEmpty())
                rtname += QStringLiteral(" \xC2\xB7 %1").arg(rsubj);
            if (!rauth.isEmpty())
                rtname += QStringLiteral(" \xC2\xB7 by %1").arg(rauth);
            name->setToolTip(rtname);
        }
        m_branchesTable->setItem(row, 0, name);

        // Ahead/behind vs the default branch when known, else a plain "Remote".
        QString rstatus = QStringLiteral("Remote");
        QString rtip = QStringLiteral("Remote-tracking branch");
        const auto abIt = remoteAheadBehind.constFind(branch);
        if (abIt != remoteAheadBehind.constEnd()) {
            const int rahead = abIt->first;
            const int rbehind = abIt->second;
            if (rahead == 0 && rbehind == 0) {
                rstatus = QStringLiteral("Up to date");
                rtip = QStringLiteral("Up to date with %1").arg(base);
            } else {
                rstatus = QString::fromUtf8("%1 behind \xC2\xB7 %2 ahead")
                              .arg(rbehind)
                              .arg(rahead);
                rtip = QStringLiteral("%1 commit(s) behind and %2 ahead of %3")
                           .arg(rbehind)
                           .arg(rahead)
                           .arg(base);
            }
        }
        auto *statusItem = new QTableWidgetItem(rstatus);
        statusItem->setForeground(QColor("#8b949e"));
        statusItem->setToolTip(rtip);
        m_branchesTable->setItem(row, 1, statusItem);

        {
            auto *rupdated = new QTableWidgetItem(
                formatShortRelativeTime(branchTimes.value(branch, 0)));
            const QString rsubj = branchSubjects.value(branch);
            const QString rauth = branchAuthors.value(branch);
            QString rutip;
            if (!rauth.isEmpty())
                rutip = QStringLiteral("by %1").arg(rauth);
            if (!rsubj.isEmpty())
                rutip += (rutip.isEmpty() ? QString() : QStringLiteral(": ")) + rsubj;
            if (!rutip.isEmpty())
                rupdated->setToolTip(rutip);
            m_branchesTable->setItem(row, 2, rupdated);
        }
        m_branchesTable->setItem(row, 3, new QTableWidgetItem);
        m_branchesTable->setItem(row, 4, new QTableWidgetItem);
    }

    // Header "Pull <base> into all" reflects the current base and is enabled only
    // when there's at least one behind branch to update.
    if (m_branchPullAllButton) {
        m_branchPullAllButton->setText(
            base.isEmpty() ? QStringLiteral("Pull into all")
                           : QStringLiteral("Pull %1 into all").arg(base));
        m_branchPullAllButton->setEnabled(writable && anyBehind);
        m_branchPullAllButton->setToolTip(
            !writable
                ? QStringLiteral("Read-only mirror \xE2\x80\x94 nothing to update")
                : anyBehind
                      ? QStringLiteral("Merge %1 into every branch that's behind it")
                            .arg(base)
                      : QStringLiteral("All branches are up to date with %1")
                            .arg(base));
    }

    // Header "Delete merged" is enabled only when at least one branch is fully
    // merged into the base (0 behind, 0 ahead) and therefore safe to prune.
    if (m_branchDeleteMergedButton) {
        m_branchDeleteMergedButton->setEnabled(writable && anyMerged);
        m_branchDeleteMergedButton->setToolTip(
            !writable
                ? QStringLiteral("Read-only mirror \xE2\x80\x94 nothing to delete")
                : anyMerged
                      ? QStringLiteral("Delete every branch fully merged into %1 "
                                       "(0 behind, 0 ahead)")
                            .arg(base)
                      : QStringLiteral("No branches are fully merged into %1")
                            .arg(base));
    }

    if (m_branchesTable->rowCount() == 0) {
        m_branchesTable->insertRow(0);
        auto *empty = new QTableWidgetItem("No branches in this repository.");
        empty->setForeground(QColor("#8b949e"));
        m_branchesTable->setItem(0, 0, empty);
        // Table signals are blocked, so blank the diff pane ourselves.
        showBranchDiff(QString());
        return;
    }

    // Re-select the row the user was viewing (falling back to the checked-out
    // branch) so rebuilding the table doesn't leave the diff pane blank.
    QString target = previouslyViewed;
    if (target.isEmpty() ||
        (!branches.contains(target) && !remoteBranches.contains(target)))
        target = selected;
    for (int r = 0; r < m_branchesTable->rowCount(); ++r) {
        QTableWidgetItem *it = m_branchesTable->item(r, 0);
        if (it && it->text() == target) {
            m_branchesTable->setCurrentCell(r, 0);
            break;
        }
    }
    // The table's selection signals were blocked across the rebuild, so the
    // setCurrentCell above won't have re-rendered the diff. Do it explicitly now —
    // a single, guarded transition rather than the blank-then-refill flash the
    // old signal-driven path produced (adhoc #256).
    showBranchDiff(target);
}

void MainWindow::promptNewBranch()
{
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice("This is a read-only mirror; branches can't be created here.",
                            true);
        return;
    }
    const QString dir = repoGitDir();
    const QStringList branches = repoBranches();
    const QString base = m_repoBranch.isEmpty() ? repoDefaultBranch(branches)
                                                : m_repoBranch;
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, "New branch",
                              QStringLiteral("Create a branch from %1:").arg(base),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return;
    QString err;
    if (!runGitCapture(dir, {"branch", name, base}, nullptr, &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not create the branch." : err, true);
        return;
    }
    logSystem(QStringLiteral("Git: created branch %1 from %2.").arg(name, base));
    setRepoDetailNotice(QStringLiteral("Created branch %1.").arg(name));
    loadBranchesAndTags();
    setRepoBranch(name);
}

void MainWindow::deleteBranch(const QString &branch)
{
    if (branch.isEmpty() || !repoHasWorkingTree())
        return;
    const QString dir = repoGitDir();
    if (QMessageBox::question(
            this, "Delete branch",
            QStringLiteral("Delete branch \"%1\"? This cannot be undone.").arg(branch),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    // Pick the branch to land on after the delete: the neighbour in the list (the
    // row that slides up into the deleted one, or the row above when we deleted the
    // last one) rather than snapping back to the checked-out branch, so deleting
    // several in a row walks down the list. Falls through to the default branch when
    // this was the last non-default branch (adhoc #256). Computed from the table now,
    // while its rows still reflect the pre-delete order.
    QString nextSelection = neighbourBranchInList(branch);

    QString err;
    // -D force-deletes even if not merged; the user explicitly confirmed.
    if (!runGitCapture(dir, {"branch", "-D", branch}, nullptr, &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not delete the branch." : err, true);
        return;
    }
    logSystem(QStringLiteral("Git: deleted branch %1.").arg(branch));
    setRepoDetailNotice(QStringLiteral("Deleted branch %1.").arg(branch));
    m_branchesCache.clear(); // branch removed — bust the cache
    // Steer loadBranchesPanel()'s "re-select the previously-viewed branch" logic at
    // the neighbour: it reads m_branchDiffBranch as the branch to restore, and an
    // empty value falls back to the default branch ("go to main") (adhoc #256).
    m_branchDiffBranch = nextSelection;
    loadBranchesAndTags();
}

// The branch sitting next to `branch` in the Branches table — the row just below
// it (which slides up when it's deleted) or, if it was the last row, the row just
// above. Empty when there's no other branch listed, which makes loadBranchesPanel
// fall back to the default branch. Used to pick the post-delete selection so
// removing a branch doesn't jump the list back to the checked-out branch (#256).
QString MainWindow::neighbourBranchInList(const QString &branch) const
{
    if (!m_branchesTable || branch.isEmpty())
        return QString();
    int row = -1;
    for (int r = 0; r < m_branchesTable->rowCount(); ++r) {
        QTableWidgetItem *it = m_branchesTable->item(r, 0);
        if (it && it->text() == branch) {
            row = r;
            break;
        }
    }
    if (row < 0)
        return QString();
    for (int r = row + 1; r < m_branchesTable->rowCount(); ++r) {
        QTableWidgetItem *it = m_branchesTable->item(r, 0);
        if (it && !it->text().isEmpty())
            return it->text();
    }
    for (int r = row - 1; r >= 0; --r) {
        QTableWidgetItem *it = m_branchesTable->item(r, 0);
        if (it && !it->text().isEmpty())
            return it->text();
    }
    return QString();
}

// Prune every branch that's fully merged into the default branch (0 behind and
// 0 ahead of it), in one confirmed pass. The default and checked-out branches
// are always skipped — they're never redundant and can't be force-deleted.
void MainWindow::deleteMergedBranches()
{
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice(
            "This is a read-only mirror; branches can't be deleted here.", true);
        return;
    }
    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);
    if (base.isEmpty())
        return;

    // The checked-out branch can't be force-deleted; skip it (it's usually base).
    QByteArray headOut;
    QString currentBranch;
    if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &headOut, nullptr))
        currentBranch = QString::fromUtf8(headOut).trimmed();

    // A branch with 0 behind and 0 ahead of base points at the same tip — fully
    // merged and redundant.
    QStringList merged;
    for (const QString &branch : branches) {
        if (branch == base || branch == currentBranch)
            continue;
        QByteArray counts;
        if (!runGitCapture(dir,
                           {"rev-list", "--left-right", "--count",
                            base + "..." + branch},
                           &counts, nullptr))
            continue;
        const QStringList parts = QString::fromUtf8(counts).trimmed().split(
            QRegularExpression(QStringLiteral("\\s+")));
        if (parts.size() >= 2 && parts.at(0).toInt() == 0 && parts.at(1).toInt() == 0)
            merged << branch;
    }

    if (merged.isEmpty()) {
        setRepoDetailNotice(
            QStringLiteral("No branches are fully merged into %1.").arg(base));
        return;
    }
    if (QMessageBox::question(
            this, "Delete merged branches",
            QStringLiteral("Delete the %1 branch(es) fully merged into %2 "
                           "(0 behind, 0 ahead)? This cannot be undone.\n\n%3")
                .arg(merged.size())
                .arg(base, merged.join(QStringLiteral("\n"))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QStringList deleted, failed;
    for (const QString &branch : merged) {
        // -D rather than -d: identical-tip branches are merged, but -D keeps the
        // pass from stalling on any edge case git counts differently.
        if (runGitCapture(dir, {"branch", "-D", branch}, nullptr, nullptr))
            deleted << branch;
        else
            failed << branch;
    }
    if (!deleted.isEmpty())
        logSystem(QStringLiteral("Git: deleted %1 merged branch(es): %2.")
                      .arg(deleted.size())
                      .arg(deleted.join(QStringLiteral(", "))));
    if (failed.isEmpty())
        setRepoDetailNotice(
            QStringLiteral("Deleted %1 merged branch(es).").arg(deleted.size()));
    else
        setRepoDetailNotice(
            QStringLiteral("Deleted %1 merged branch(es); %2 could not be deleted (%3).")
                .arg(deleted.size())
                .arg(failed.size())
                .arg(failed.join(QStringLiteral(", "))),
            true);
    loadBranchesAndTags();
}

// Refresh the detail-pane action bar for the selected branch: label its
// ahead/behind status and enable Pull / Fix with agent / Create PR / Merge to
// main exactly as the per-row buttons used to (issue #116). Fix with agent only
// appears when the branch genuinely conflicts with the base.
void MainWindow::updateBranchDetailActions(const QString &branch)
{
    if (!m_branchMergeButton)
        return;
    const QString dir = repoGitDir();
    const QString base = repoDefaultBranch(repoBranches());
    const bool writable = repoHasWorkingTree();
    const bool isBase = branch.isEmpty() || branch == base;

    int behind = 0, ahead = 0;
    bool hasConflict = false;
    if (!dir.isEmpty() && !isBase) {
        QByteArray counts;
        if (runGitCapture(dir,
                          {"rev-list", "--left-right", "--count", base + "..." + branch},
                          &counts, nullptr)) {
            const QStringList parts = QString::fromUtf8(counts).trimmed().split(
                QRegularExpression(QStringLiteral("\\s+")));
            if (parts.size() >= 2) {
                behind = parts.at(0).toInt();
                ahead = parts.at(1).toInt();
            }
        }
        // Only a branch with its own commits and base commits it lacks can conflict.
        if (behind > 0 && ahead > 0)
            hasConflict = branchMergeTree(dir, base, branch).isEmpty();
    }

    if (m_branchDetailLabel) {
        QString text;
        if (branch.isEmpty())
            text.clear();
        else if (branch == base)
            text = QString::fromUtf8("<b>%1</b> \xC2\xB7 default branch")
                       .arg(branch.toHtmlEscaped());
        else {
            text = QString::fromUtf8("<b>%1</b> \xC2\xB7 %2 behind \xC2\xB7 %3 ahead")
                       .arg(branch.toHtmlEscaped())
                       .arg(behind)
                       .arg(ahead);
            if (hasConflict)
                text += QString::fromUtf8(
                    " \xC2\xB7 <span style='color:#f85149'>conflicts</span>");
        }
        m_branchDetailLabel->setText(text);
    }

    // Open in Codium: available whenever there's a local checkout to open. Works
    // for the base branch too (opens the main checkout), unlike the merge actions.
    if (m_branchOpenCodiumButton) {
        const bool canOpen = !branch.isEmpty() && !dir.isEmpty();
        m_branchOpenCodiumButton->setEnabled(canOpen);
        m_branchOpenCodiumButton->setToolTip(
            canOpen ? QStringLiteral("Open %1 in VSCodium").arg(branch)
                    : QStringLiteral("No local checkout to open"));
    }

    // Pull <base> into this branch (only when it's actually behind).
    m_branchPullButton->setText(base.isEmpty() ? QStringLiteral("Pull main")
                                               : QStringLiteral("Pull %1").arg(base));
    const bool canPull = writable && !isBase && behind > 0;
    m_branchPullButton->setEnabled(canPull);
    if (isBase)
        m_branchPullButton->setToolTip(
            QStringLiteral("Select a branch other than %1").arg(base));
    else if (!writable)
        m_branchPullButton->setToolTip(
            "Read-only mirror \xE2\x80\x94 no working tree to update");
    else if (behind == 0)
        m_branchPullButton->setToolTip(
            QStringLiteral("%1 is already up to date with %2").arg(branch, base));
    else
        m_branchPullButton->setToolTip(
            QStringLiteral("Merge %1 into %2 (%3 commit(s) behind)")
                .arg(base, branch)
                .arg(behind));

    // Merge editor: bring the branch up to date with base, resolving conflicts
    // by hand. Available whenever the branch is behind (same as Pull main); the
    // editor only opens when git reports conflicts.
    if (m_branchMergeEditorButton) {
        const bool canMergeEditor = writable && !isBase && behind > 0;
        m_branchMergeEditorButton->setEnabled(canMergeEditor);
        if (isBase)
            m_branchMergeEditorButton->setToolTip(
                QStringLiteral("Select a branch other than %1").arg(base));
        else if (!writable)
            m_branchMergeEditorButton->setToolTip(
                "Read-only mirror \xE2\x80\x94 no working tree to update");
        else if (behind == 0)
            m_branchMergeEditorButton->setToolTip(
                QStringLiteral("%1 is already up to date with %2").arg(branch, base));
        else
            m_branchMergeEditorButton->setToolTip(
                QStringLiteral("Merge %1 into %2 and resolve any conflicts in an "
                               "editor")
                    .arg(base, branch));
    }

    // Fix with agent: shown only when the selected branch conflicts with base. The
    // agent/model dropdowns travel with the button.
    m_branchFixButton->setVisible(hasConflict);
    m_branchFixButton->setEnabled(hasConflict && writable);
    m_branchFixButton->setToolTip(
        QStringLiteral("Let the chosen agent merge %1 into %2 and resolve the "
                       "conflicts \xE2\x80\x94 watch it on the Agents tab")
            .arg(base, branch));
    if (m_branchFixAgentCombo) {
        m_branchFixAgentCombo->setVisible(hasConflict);
        m_branchFixAgentCombo->setEnabled(hasConflict && writable);
    }
    if (m_branchFixModelCombo) {
        m_branchFixModelCombo->setVisible(hasConflict);
        m_branchFixModelCombo->setEnabled(hasConflict && writable);
    }

    // Create PR from this branch into base.
    const bool canPr = writable && !isBase;
    m_branchPrButton->setEnabled(canPr);
    m_branchPrButton->setToolTip(
        canPr ? QStringLiteral("Open a pull request from %1 into %2").arg(branch, base)
              : (isBase ? "Select a branch other than the default to open a pull request"
                        : "Read-only mirror \xE2\x80\x94 no working tree to open a pull "
                          "request from"));

    // Merge this branch directly into the default branch.
    const bool canMerge = writable && !isBase;
    m_branchMergeButton->setEnabled(canMerge);
    m_branchMergeButton->setToolTip(
        canMerge ? QStringLiteral("Merge %1 into %2").arg(branch, base)
                 : (isBase ? QStringLiteral("Select a branch other than %1").arg(base)
                           : "Read-only mirror \xE2\x80\x94 nothing to merge into here"));
}

void MainWindow::openBranchInCodium(const QString &branch)
{
    const QString repoPath = repoGitDir();
    if (branch.isEmpty() || repoPath.isEmpty()) {
        setRepoDetailNotice("No local checkout to open.", true);
        return;
    }
    // Prefer the branch's own worktree; fall back to the main checkout for the
    // default branch (or any branch without a dedicated worktree).
    QString dir = worktreePathForBranch(repoPath, branch);
    if (dir.isEmpty())
        dir = repoPath;

    // Resolve the VSCodium launcher. "codium" is the Linux/Homebrew CLI name;
    // "vscodium" is the alternative shim some distros ship.
    QString codium = QStandardPaths::findExecutable(QStringLiteral("codium"));
    if (codium.isEmpty())
        codium = QStandardPaths::findExecutable(QStringLiteral("vscodium"));
#ifdef Q_OS_MACOS
    if (codium.isEmpty()) {
        const QString cli =
            QStringLiteral("/Applications/VSCodium.app/Contents/Resources/app/bin/codium");
        if (QFileInfo::exists(cli))
            codium = cli;
    }
#endif
    if (codium.isEmpty()) {
        setRepoDetailNotice(
            "VSCodium not found \xE2\x80\x94 install it and ensure \"codium\" is on PATH.",
            true);
        return;
    }

    if (QProcess::startDetached(codium, {dir}))
        setRepoDetailNotice(QStringLiteral("Opening %1 in VSCodium\xE2\x80\xA6").arg(branch),
                            false);
    else
        setRepoDetailNotice("Could not launch VSCodium.", true);
}

QString MainWindow::branchWorkDir(const QString &branch) const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return QString();
    const QString localPath = m_repositories.at(m_repoDetailIndex).localPath;
    if (localPath.isEmpty() || branch.isEmpty())
        return QString();
    // A dedicated worktree for the branch owns its own uncommitted changes...
    const QString wt = worktreePathForBranch(localPath, branch);
    if (!wt.isEmpty())
        return wt;
    // ...otherwise the changes live in the main checkout, but only if the branch
    // is the one checked out there (any other branch can't be dirty locally).
    if (!repoHasWorkingTree())
        return QString();
    QByteArray headOut;
    if (runGitCapture(localPath, {"rev-parse", "--abbrev-ref", "HEAD"}, &headOut,
                      nullptr)
        && QString::fromUtf8(headOut).trimmed() == branch)
        return localPath;
    return QString();
}

void MainWindow::showBranchDiff(const QString &branch)
{
    if (!m_branchDiffView)
        return;
    m_branchDiffBranch = branch;
    updateBranchDetailActions(branch);
    m_branchDiffFileSpans.clear();
    m_branchDiffViewedContext.clear();
    // A new branch's diff hasn't been fetched yet; drop the cached patch so the
    // "Viewed" toggle can't re-render a stale one before the async read lands.
    m_branchDiffLastValid = false;
    // Reset the scope + changed-files lists; the success path below repopulates.
    if (m_branchScopeList) {
        QSignalBlocker block(m_branchScopeList);
        m_branchScopeList->clear();
    }
    if (m_branchScopeLabel)
        m_branchScopeLabel->clear();
    if (m_branchFileList) {
        QSignalBlocker block(m_branchFileList);
        m_branchFileList->clear();
    }
    if (m_branchFilesSummary)
        m_branchFilesSummary->clear();
    const QString dir = repoGitDir();
    if (branch.isEmpty() || dir.isEmpty()) {
        m_branchDiffView->clear();
        return;
    }
    const QString base = repoDefaultBranch(repoBranches());
    if (branch == base) {
        setDiffHtml(m_branchDiffView,
            QStringLiteral("<p style='color:#8b949e'>%1 is the default branch.</p>")
                .arg(branch.toHtmlEscaped()));
        return;
    }

    // Build the scope selector: the whole branch, its uncommitted working-tree
    // changes (when its checkout is dirty), then one row per commit it adds over
    // base. UserRole carries the scope key consumed by renderBranchScopeDiff().
    // The "All changes" row is added synchronously so the list is never empty;
    // the dirty-count and per-commit rows come from git reads (a `status` and a
    // `log`) that used to freeze the GUI thread here — gather them off-thread and
    // append the rows when they arrive (issue #353).
    if (!m_branchScopeList) {
        renderBranchScopeDiff();
        return;
    }
    {
        QSignalBlocker block(m_branchScopeList);
        auto *all = new QListWidgetItem(QStringLiteral("All changes"));
        all->setIcon(themedOcticon("git-compare", QColor("#58a6ff"), 14));
        all->setData(Qt::UserRole, QStringLiteral("all"));
        all->setToolTip(QStringLiteral("Every change %1 adds over %2").arg(branch, base));
        m_branchScopeList->addItem(all);
        m_branchScopeList->setCurrentRow(0);
    }
    if (m_branchScopeLabel)
        m_branchScopeLabel->setText(QStringLiteral("Scope"));
    // Render the whole-branch diff right away (also async — see below); the extra
    // scope rows only add selectable entries, they don't change the default view.
    renderBranchScopeDiff();

    const QString work = branchWorkDir(branch);
    const int gen = ++m_branchScopeLoadGen;
    struct BranchScope {
        int dirty = 0;
        QList<QStringList> commits; // {fullHash, shortHash, subject, relTime}
    };
    runOffThread<BranchScope>(
        [dir, base, branch, work]() {
            BranchScope s;
            if (!work.isEmpty()) {
                QByteArray status;
                runGitCapture(work, {"status", "--porcelain"}, &status, nullptr);
                s.dirty = QString::fromUtf8(status)
                              .split('\n', Qt::SkipEmptyParts)
                              .size();
            }
            QByteArray log;
            runGitCapture(dir,
                          {"log", "--format=%H%x1f%h%x1f%s%x1f%cr", base + ".." + branch},
                          &log, nullptr);
            for (const QString &line :
                 QString::fromUtf8(log).split('\n', Qt::SkipEmptyParts)) {
                const QStringList f = line.split(QLatin1Char('\x1f'));
                if (f.size() >= 4)
                    s.commits.append(f);
            }
            return s;
        },
        [this, gen, branch, work](BranchScope s) {
            // Dropped if the user switched branch (or reselected, bumping the gen)
            // while the reads were in flight.
            if (gen != m_branchScopeLoadGen || !m_branchScopeList ||
                m_branchDiffBranch != branch)
                return;
            QSignalBlocker block(m_branchScopeList);
            if (s.dirty > 0) {
                auto *wt = new QListWidgetItem(
                    QStringLiteral("Uncommitted changes  (%1)").arg(s.dirty));
                wt->setIcon(themedOcticon("pencil", QColor("#d29922"), 14));
                wt->setData(Qt::UserRole, QStringLiteral("wt"));
                wt->setToolTip(
                    QStringLiteral("%1 uncommitted file(s) in %2").arg(s.dirty).arg(work));
                m_branchScopeList->addItem(wt);
            }
            for (const QStringList &f : s.commits) {
                auto *item = new QListWidgetItem(
                    QString::fromUtf8("%1   \xC2\xB7 %2").arg(f.at(2), f.at(3)));
                item->setIcon(themedOcticon("git-commit", QColor("#8b949e"), 14));
                item->setData(Qt::UserRole, QStringLiteral("commit:") + f.at(0));
                item->setToolTip(QStringLiteral("%1  %2").arg(f.at(1), f.at(2)));
                m_branchScopeList->addItem(item);
            }
        });
}

void MainWindow::renderBranchScopeDiff()
{
    if (!m_branchDiffView)
        return;
    const QString branch = m_branchDiffBranch;
    const QString dir = repoGitDir();
    if (branch.isEmpty() || dir.isEmpty())
        return;
    const QString base = repoDefaultBranch(repoBranches());

    QString scope = QStringLiteral("all");
    if (m_branchScopeList && m_branchScopeList->currentItem())
        scope = m_branchScopeList->currentItem()->data(Qt::UserRole).toString();

    // The diff/show reads below can take seconds on a large branch — they were a
    // recurring StallWatchdog offender freezing the GUI thread (issue #353). Run
    // them off-thread and render the patch on the GUI thread once ready, dropping
    // the result if the user has since switched branch or scope. `work` (which
    // reads GUI state via branchWorkDir) is resolved here on the GUI thread.
    const QString work =
        scope == QLatin1String("wt") ? branchWorkDir(branch) : QString();
    const int gen = ++m_branchScopeDiffGen;
    struct ScopeDiff {
        bool ok = true;
        QByteArray out;
        QString err;
        QString emptyMessage;
        QString viewedContext;
    };
    runOffThread<ScopeDiff>(
        [dir, base, branch, scope, work]() {
            ScopeDiff r;
            if (scope == QLatin1String("wt")) {
                // The branch's uncommitted changes (working tree vs HEAD), with
                // untracked files appended as /dev/null diffs so new files show.
                r.viewedContext =
                    QStringLiteral("branch/") + branch + QStringLiteral("/wt");
                r.emptyMessage = QStringLiteral("No uncommitted changes.");
                if (work.isEmpty() ||
                    !runGitCapture(work, {"diff", "HEAD"}, &r.out, &r.err)) {
                    r.ok = false;
                    return r;
                }
                QByteArray others;
                runGitCapture(work, {"ls-files", "--others", "--exclude-standard", "-z"},
                              &others, nullptr);
                for (const QByteArray &p : others.split('\0')) {
                    if (p.isEmpty())
                        continue;
                    r.out += gitCaptureStdout(
                        work,
                        {"diff", "--no-index", "--", "/dev/null", QString::fromUtf8(p)});
                }
            } else if (scope.startsWith(QLatin1String("commit:"))) {
                // A single commit's diff (against its parent); --format= drops the
                // commit message so the patch starts at the first file header.
                const QString hash = scope.mid(7);
                r.viewedContext = QStringLiteral("branch/") + branch +
                                  QStringLiteral("/commit/") + hash;
                r.emptyMessage = QStringLiteral("This commit has no file changes.");
                if (!runGitCapture(dir, {"show", "--format=", hash}, &r.out, &r.err))
                    r.ok = false;
            } else {
                r.viewedContext = QStringLiteral("branch/") + branch;
                r.emptyMessage =
                    QStringLiteral("No changes between %1 and %2.").arg(branch, base);
                if (!runGitCapture(dir, {"diff", base + ".." + branch}, &r.out, &r.err))
                    r.ok = false;
            }
            return r;
        },
        [this, gen, branch, scope](ScopeDiff r) {
            // Dropped if the branch or scope changed while the read was in flight.
            if (gen != m_branchScopeDiffGen || !m_branchDiffView ||
                m_branchDiffBranch != branch)
                return;
            if (!r.ok) {
                if (scope == QLatin1String("wt"))
                    setDiffHtml(m_branchDiffView,
                        QStringLiteral(
                            "<p style='color:#8b949e'>No uncommitted changes.</p>"));
                else if (scope.startsWith(QLatin1String("commit:")))
                    setDiffHtml(m_branchDiffView,
                        QStringLiteral(
                            "<p style='color:#f85149'>Could not show %1: %2</p>")
                            .arg(scope.mid(7).left(8).toHtmlEscaped(),
                                 r.err.toHtmlEscaped()));
                else
                    setDiffHtml(m_branchDiffView,
                        QStringLiteral(
                            "<p style='color:#f85149'>Could not diff %1: %2</p>")
                            .arg(branch.toHtmlEscaped(), r.err.toHtmlEscaped()));
                return;
            }
            m_branchDiffLastPatch = r.out;
            m_branchDiffLastEmpty = r.emptyMessage;
            m_branchDiffLastValid = true;
            renderBranchDiffPatch(QString::fromUtf8(r.out), r.emptyMessage,
                                  r.viewedContext);
        });
}

void MainWindow::renderBranchDiffPatch(const QString &patch,
                                       const QString &emptyMessage,
                                       const QString &viewedContext)
{
    if (!m_branchDiffView)
        return;
    m_branchDiffViewedContext = viewedContext;
    m_branchDiffFileSpans.clear();
    // Supersede any progressive render still streaming in from a prior scope.
    ++m_branchDiffRenderGen;
    m_branchDiffPendingBlocks.clear();
    if (m_branchFileList) {
        QSignalBlocker block(m_branchFileList);
        m_branchFileList->clear();
    }
    const QString dir = repoGitDir();
    const QString base = repoDefaultBranch(repoBranches());
    QList<DiffFileEntry> files;
    const QSet<QString> viewed = loadDiffViewed(viewedContext);
    const QString html = renderDiffHtml(patch, files, dir, base, m_branchDiffBranch,
                                        QString(), QHash<QString, QString>(), viewed);
    m_branchDiffFilePaths.clear();
    for (const DiffFileEntry &f : files)
        m_branchDiffFilePaths.append(f.path);

    // Handing an enormous diff to QTextEdit::setHtml() in one go parses, styles
    // and lays it all out on the GUI thread at once, freezing the window for
    // seconds (issue #187). Rather than refuse to render a large commit, split it
    // into per-file blocks and stream them in: paint enough to fill the viewport
    // now (so the commit shows immediately), then append the rest a batch at a
    // time off the event loop, keeping the window responsive while it fills in
    // (adhoc #51). A truly pathological diff (a huge generated/vendored file) is
    // still refused past a hard ceiling, to stay mindful of memory.
    constexpr int kStreamDiffHtmlChars = 1'000'000;  // stream, don't block, above this
    constexpr int kMaxDiffHtmlChars = 8'000'000;     // refuse entirely above this
    constexpr int kFirstPaintChars = 250'000;        // fill the viewport synchronously
    if (html.isEmpty()) {
        setDiffHtml(m_branchDiffView,
            QStringLiteral("<p style='color:#8b949e'>%1</p>")
                .arg(emptyMessage.toHtmlEscaped()));
    } else if (html.size() > kMaxDiffHtmlChars) {
        setDiffHtml(m_branchDiffView,
            QStringLiteral(
                "<p style='color:#d29922'>This diff is too large to render here "
                "(%1 file%2). Use the changed-files list, or view the branch in "
                "your editor.</p>")
                .arg(files.size())
                .arg(files.size() == 1 ? "" : "s"));
    } else if (html.size() > kStreamDiffHtmlChars) {
        QStringList blocks = splitDiffFileBlocks(html);
        QString firstChunk;
        while (!blocks.isEmpty() &&
               (firstChunk.isEmpty() || firstChunk.size() < kFirstPaintChars))
            firstChunk += blocks.takeFirst();
        // A streamed diff is assembled incrementally; don't let a
        // Ctrl+wheel zoom re-render a partial copy (issue #254).
        m_branchDiffView->setProperty("fm_diffSource", QString());
        m_branchDiffView->document()->setDefaultStyleSheet(
            diffStyleSheet(m_diffFontPt));
        m_branchDiffView->setHtml(firstChunk);
        m_branchDiffPendingBlocks = blocks;
        appendBranchDiffBlocks(m_branchDiffRenderGen);
    } else {
        setDiffHtml(m_branchDiffView, html);
    }

    // Changed-files list: a status-coloured row per file; click to scroll the
    // diff to it (mirrors the commit/PR diff viewers).
    if (m_branchFilesSummary)
        m_branchFilesSummary->setText(QStringLiteral("%1 file%2 changed")
                                          .arg(files.size())
                                          .arg(files.size() == 1 ? "" : "s"));
    if (m_branchFileList) {
        QSignalBlocker block(m_branchFileList);
        for (const DiffFileEntry &f : files) {
            const QString name = f.path.section(QLatin1Char('/'), -1);
            auto *item = new QListWidgetItem(
                QString::fromUtf8("%1   +%2 \xE2\x88\x92%3")
                    .arg(name, QString::number(f.adds), QString::number(f.dels)));
            QString icon = "file-diff";
            QColor tint("#d29922"); // modified
            if (f.status == QLatin1String("added")) {
                icon = "diff";
                tint = QColor("#3fb950");
            } else if (f.status == QLatin1String("deleted")) {
                icon = "trash";
                tint = QColor("#f85149");
            } else if (f.status == QLatin1String("renamed")) {
                icon = "file-diff";
                tint = QColor("#58a6ff");
            }
            item->setIcon(themedOcticon(icon, tint, 14));
            item->setData(Qt::UserRole, f.anchor);
            item->setToolTip(QString::fromUtf8("%1 \xC2\xB7 %2").arg(f.status, f.path));
            m_branchFileList->addItem(item);
        }
        fitFileListToWidestEntry(m_branchFileList);
    }

    // Map each file header to its document position for the sticky bar. When the
    // diff is being streamed in (above), only the first blocks are in the document
    // now; appendBranchDiffBlocks() rebuilds the full map once the last batch
    // lands. Either way this covers whatever is currently shown.
    rebuildBranchDiffSpans();
}

// Stream the next queued batch of per-file diff blocks into the branch diff view,
// then reschedule until the queue drains (see renderBranchDiffPatch). A batch
// from a superseded scope selection (m_branchDiffRenderGen bumped) bails.
void MainWindow::appendBranchDiffBlocks(int gen)
{
    if (gen != m_branchDiffRenderGen || !m_branchDiffView)
        return;
    if (m_branchDiffPendingBlocks.isEmpty()) {
        rebuildBranchDiffSpans(); // every file is in the document now
        return;
    }
    QTimer::singleShot(0, this, [this, gen] {
        if (gen != m_branchDiffRenderGen || !m_branchDiffView)
            return;
        constexpr int kAppendBatchChars = 400'000;
        QString batch;
        while (!m_branchDiffPendingBlocks.isEmpty() &&
               (batch.isEmpty() || batch.size() < kAppendBatchChars))
            batch += m_branchDiffPendingBlocks.takeFirst();
        // Append at the document's end via a private cursor so the user's current
        // scroll position is left untouched as the rest fills in below.
        QTextCursor cur(m_branchDiffView->document());
        cur.movePosition(QTextCursor::End);
        cur.insertHtml(batch);
        appendBranchDiffBlocks(gen);
    });
}

// Rebuild the sticky-bar file-span map from whatever is currently in the branch
// diff document. Walk the document's blocks (cheap and layout-free) rather than
// QTextDocument::find(), whose cursor positioning forces a full synchronous
// layout of the entire diff — that alone froze the UI for seconds (issue #187).
void MainWindow::rebuildBranchDiffSpans()
{
    if (!m_branchDiffView)
        return;
    m_branchDiffFileSpans.clear();
    QTextDocument *spanDoc = m_branchDiffView->document();
    int fileIdx = 0;
    for (QTextBlock block = spanDoc->begin();
         block.isValid() && fileIdx < m_branchDiffFilePaths.size();
         block = block.next()) {
        const int at = block.text().indexOf(m_branchDiffFilePaths.at(fileIdx));
        if (at >= 0) {
            m_branchDiffFileSpans.append(
                qMakePair(block.position() + at, m_branchDiffFilePaths.at(fileIdx)));
            ++fileIdx;
        }
    }
    updateBranchDiffSticky();
}

void MainWindow::createPullFromBranch(const QString &branch)
{
    const QString dir = repoGitDir();
    const QString base = repoDefaultBranch(repoBranches());
    if (branch.isEmpty() || branch == base)
        return;
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice(
            "This is a read-only mirror; pull requests can't be opened here.", true);
        return;
    }
    QByteArray diff;
    QString err;
    if (!runGitCapture(dir, {"diff", "--binary", base + ".." + branch}, &diff, &err) ||
        QString::fromUtf8(diff).trimmed().isEmpty()) {
        setRepoDetailNotice(
            QStringLiteral("%1 has no changes to open as a pull request.").arg(branch),
            true);
        return;
    }
    // Default the PR title to the branch's first commit subject.
    QByteArray subjectOut;
    runGitCapture(dir, {"log", "--format=%s", "--reverse", base + ".." + branch},
                  &subjectOut, nullptr);
    const QStringList subjects =
        QString::fromUtf8(subjectOut).split('\n', Qt::SkipEmptyParts);
    bool ok = false;
    const QString title =
        QInputDialog::getText(
            this, "Create pull request",
            QStringLiteral("Title for the pull request from %1 into %2:")
                .arg(branch, base),
            QLineEdit::Normal, subjects.isEmpty() ? branch : subjects.first(), &ok)
            .trimmed();
    if (!ok || title.isEmpty())
        return;
    QString description;
    for (const QString &s : subjects)
        description += "- " + s + "\n";

    // Capture the authored commit series too, so a merge replays it with `git am`.
    QByteArray mbox;
    runGitCapture(dir, {"format-patch", "--stdout", base + ".." + branch}, &mbox,
                  nullptr);
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    const int number = store.createPull(title, description, base, branch,
                                        QString::fromUtf8(diff),
                                        QString::fromUtf8(mbox),
                                        /*branchBacked=*/true, &error);
    if (number < 0) {
        setRepoDetailNotice(
            error.isEmpty() ? "Could not create the pull request." : error, true);
        return;
    }
    logSystem(QStringLiteral("Opened pull #%1 from %2 into %3.")
                  .arg(number)
                  .arg(branch, base));
    setRepoDetailNotice(
        QStringLiteral("Opened pull request #%1 from %2.").arg(number).arg(branch));
    m_currentPullNumber = number;
    switchToPullTab(number);
}

void MainWindow::updateBranchFromBase(const QString &branch)
{
    const QString dir = repoGitDir();
    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);
    if (branch.isEmpty() || branch == base || dir.isEmpty())
        return;
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice(
            "This is a read-only mirror; branches can't be updated here.", true);
        return;
    }

    // How far behind base is the branch? Nothing to do if it's already current.
    int behind = 0, ahead = 0;
    QByteArray counts;
    if (runGitCapture(dir,
                      {"rev-list", "--left-right", "--count", base + "..." + branch},
                      &counts, nullptr)) {
        const QStringList parts = QString::fromUtf8(counts).trimmed().split(
            QRegularExpression(QStringLiteral("\\s+")));
        if (parts.size() >= 2) {
            behind = parts.at(0).toInt();
            ahead = parts.at(1).toInt();
        }
    }
    if (behind == 0) {
        setRepoDetailNotice(
            QStringLiteral("%1 is already up to date with %2.").arg(branch, base));
        return;
    }

    // The user invoked this deliberately (the Pull button is only enabled when
    // the branch is behind), so skip the confirmation and update straight away.
    QByteArray headOut;
    QString currentBranch;
    if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &headOut, nullptr))
        currentBranch = QString::fromUtf8(headOut).trimmed();
    const bool isCurrent = !currentBranch.isEmpty() && branch == currentBranch;

    // When the branch is strictly behind (no commits of its own that base lacks)
    // and isn't checked out, advance the ref without touching the working tree.
    if (ahead == 0 && !isCurrent) {
        // `isCurrent` only reflects *this* checkout's HEAD. The branch can still be
        // checked out in a separate agent worktree (e.g. an issue session under
        // /tmp/forkmesh-worktrees/...), and git flatly refuses to fetch into a ref
        // that's live in another worktree — surfacing a cryptic
        // "fatal: refusing to fetch into branch '...' checked out at '...'".
        // Explain what's actually happening instead of dumping the raw error, so
        // it's obvious the branch is busy in an active session (adhoc #205).
        const QString otherWorktree = worktreePathForBranch(dir, branch);
        if (!otherWorktree.isEmpty()) {
            setRepoDetailNotice(
                QStringLiteral(
                    "%1 is behind %2 but is checked out by an active agent session at "
                    "%3, so it can't be updated from here — git won't fetch into a "
                    "branch that's live in another worktree. Stop or finish that agent "
                    "first, or let it update from %2 itself.")
                    .arg(branch, base, otherWorktree),
                true);
            return;
        }
        QString err;
        if (!runGitCapture(dir, {"fetch", ".", base + ":" + branch}, nullptr, &err)) {
            // Fallback: if git still refused (e.g. a worktree we couldn't enumerate),
            // rewrite its terse "refusing to fetch into branch" into plain language
            // rather than leaking raw git output.
            QString msg = err.trimmed();
            if (msg.contains(QLatin1String("refusing to fetch into branch"))) {
                msg = QStringLiteral(
                          "%1 can't be updated from here because it's currently checked "
                          "out in another worktree (an active agent session). Stop or "
                          "finish that agent first, or let it update from %2 itself.")
                          .arg(branch, base);
            } else if (msg.isEmpty()) {
                msg = QStringLiteral("Could not fast-forward the branch.");
            } else {
                msg = msg.left(240);
            }
            setRepoDetailNotice(msg, true);
            return;
        }
        logSystem(QStringLiteral("Git: fast-forwarded %1 to %2.").arg(branch, base));
        setRepoDetailNotice(QStringLiteral("Updated %1 with %2.").arg(branch, base));
        m_branchesCache.clear(); // branch was updated — bust cache
        // Re-render the detail pane's scope/files-changed lists, not just the
        // ahead/behind label — they still reflected the pre-pull commit range.
        if (m_branchDiffBranch == branch)
            showBranchDiff(branch);
        return;
    }

    // A merge commit is needed: it has to happen on a checkout, so the working
    // tree must be clean before we switch branches and merge. When it isn't, we
    // used to just refuse with "commit or stash local changes" — opaque, and it
    // left the user to do it by hand. Instead, show exactly which files are in the
    // way and offer to stash them, run the update, then restore them on top, so the
    // pull "just goes through" without losing any work (adhoc #183).
    QByteArray status;
    QString err;
    if (!runGitCapture(dir, {"status", "--porcelain"}, &status, &err)) {
        setRepoDetailNotice(
            err.isEmpty() ? "Could not read the repository status." : err.left(240),
            true);
        return;
    }
    bool stashed = false;
    if (!status.trimmed().isEmpty()) {
        // Surface the actual dirty paths (transparency) instead of a bare warning.
        QStringList files;
        for (const QString &l :
             QString::fromUtf8(status).split('\n', Qt::SkipEmptyParts))
            files << l.mid(3); // strip the two-char XY status + space
        const QString n = QString::number(files.size());
        const QString plural = files.size() == 1 ? QString() : QStringLiteral("s");
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Update %1 from %2").arg(branch, base));
        box.setText(QStringLiteral("The working tree has uncommitted changes in %1 "
                                   "file%2, so %3 can't be merged in directly.")
                        .arg(n, plural, base));
        box.setInformativeText(
            QStringLiteral("Stash those changes, update %1 from %2, then restore them "
                           "on top? Nothing is discarded.")
                .arg(branch, base));
        box.setDetailedText(files.join('\n'));
        QPushButton *stashBtn = box.addButton(
            QStringLiteral("Stash, update & restore"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(stashBtn);
        box.exec();
        if (box.clickedButton() != stashBtn) {
            setRepoDetailNotice(
                QStringLiteral("Left %1 unchanged; commit or stash its %2 uncommitted "
                               "file%3 to update from %4.")
                    .arg(branch, n, plural, base));
            return;
        }
        if (!runGitCapture(
                dir,
                {"stash", "push", "-u", "-m",
                 QStringLiteral("forkmesh: auto-stash before pulling %1 into %2")
                     .arg(base, branch)},
                nullptr, &err)) {
            setRepoDetailNotice(
                err.isEmpty() ? "Could not stash the local changes." : err.left(240),
                true);
            return;
        }
        stashed = true;
        logSystem(QStringLiteral("Git: auto-stashed local changes before pulling %1 "
                                 "into %2.")
                      .arg(base, branch));
    }

    // Restores an auto-stash after the update, whatever the outcome. Returns a note
    // to append to the detail message: describes the restore so the user can see it
    // happened, or how to recover the stash if the pop didn't apply cleanly.
    auto restoreStash = [&]() -> QString {
        if (!stashed)
            return QString();
        stashed = false;
        if (runGitCapture(dir, {"stash", "pop"}, nullptr, nullptr))
            return QStringLiteral(" Your stashed changes were restored.");
        // Pop failed/conflicted — git keeps the stash on the stack, so nothing is
        // lost; tell the user how to reapply it.
        return QStringLiteral(" Your local changes couldn't be cleanly restored and "
                              "remain stashed — run \"git stash pop\" to reapply them.");
    };

    if (!isCurrent && !checkoutReleasingWorktree(dir, branch, &err)) {
        const QString note = restoreStash();
        setRepoDetailNotice(
            QStringLiteral("Could not check out %1: %2%3")
                .arg(branch, err.left(200), note),
            true);
        return;
    }

    if (!runGitCapture(dir, {"merge", "--no-edit", base}, nullptr, &err)) {
        // The merge left conflict markers in the tree. Collect the unmerged
        // files and open the merge editor so the user can resolve and commit
        // them onto this branch (the same editor the PR flow uses).
        QByteArray unmerged;
        runGitCapture(dir, {"diff", "--name-only", "--diff-filter=U"}, &unmerged,
                      nullptr);
        const QStringList conflicted =
            QString::fromUtf8(unmerged).split('\n', Qt::SkipEmptyParts);
        if (conflicted.isEmpty()) {
            // Failed for some other reason — abort and restore as before.
            runGitCapture(dir, {"merge", "--abort"}, nullptr, nullptr);
            if (!isCurrent && !currentBranch.isEmpty())
                runGitCapture(dir, {"checkout", currentBranch}, nullptr, nullptr);
            const QString note = restoreStash();
            setRepoDetailNotice(
                QStringLiteral("Could not merge %1 into %2: %3%4")
                    .arg(base, branch, err.left(200), note),
                true);
            return;
        }
        const QString intro =
            QString::fromUtf8(
                "Resolve each conflict, then commit the merge into <b>%1</b>. "
                "<b>Ours</b> is %1; <b>theirs</b> is %2. You can also edit the "
                "text directly.")
                .arg(branch.toHtmlEscaped(), base.toHtmlEscaped());
        const bool committed = runMergeConflictEditor(
            QString::fromUtf8("Resolve conflicts \xE2\x80\x94 %1").arg(branch),
            intro, dir, conflicted, QStringLiteral("Commit merge"),
            [this, dir](QString *e) {
                return runGitCapture(dir, {"add", "-A"}, nullptr, e) &&
                       runGitCapture(dir, {"commit", "--no-edit"}, nullptr, e);
            });
        if (!committed) {
            runGitCapture(dir, {"merge", "--abort"}, nullptr, nullptr);
            if (!isCurrent && !currentBranch.isEmpty())
                runGitCapture(dir, {"checkout", currentBranch}, nullptr, nullptr);
            const QString note = restoreStash();
            setRepoDetailNotice(
                QStringLiteral("Cancelled the merge of %1 into %2; %2 was left "
                               "unchanged.%3")
                    .arg(base, branch, note));
            return;
        }
        if (!isCurrent && !currentBranch.isEmpty())
            runGitCapture(dir, {"checkout", currentBranch}, nullptr, nullptr);
        const QString note = restoreStash();
        logSystem(QStringLiteral("Git: merged %1 into %2 (conflicts resolved).")
                      .arg(base, branch));
        setRepoDetailNotice(
            QStringLiteral("Updated %1 with %2 (conflicts resolved).%3")
                .arg(branch, base, note));
        m_branchesCache.clear(); // branch was updated — bust cache
        // Re-render the detail pane's scope/files-changed lists, not just the
        // ahead/behind label — they still reflected the pre-pull commit range.
        if (m_branchDiffBranch == branch)
            showBranchDiff(branch);
        return;
    }

    if (!isCurrent && !currentBranch.isEmpty())
        runGitCapture(dir, {"checkout", currentBranch}, nullptr, nullptr);

    const QString note = restoreStash();
    logSystem(QStringLiteral("Git: merged %1 into %2.").arg(base, branch));
    setRepoDetailNotice(
        QStringLiteral("Updated %1 with %2.%3").arg(branch, base, note));
    m_branchesCache.clear(); // branch was updated — bust cache
    // Re-render the detail pane's scope/files-changed lists, not just the
    // ahead/behind label — they still reflected the pre-pull commit range.
    if (m_branchDiffBranch == branch)
        showBranchDiff(branch);
}

// Bring `branch` up to date with base via the interactive merge editor — the
// hands-on counterpart to updateBranchFromBase ("Pull main"). It stages the
// merge without committing (--no-commit --no-ff, so the editor owns the final
// commit and there's always a reviewable merge commit) and, when git reports
// conflicts, opens runMergeConflictEditor so the user resolves them by hand. A
// clean merge is committed straight away. Reached from the "Merge editor" button.
void MainWindow::openBranchMergeEditor(const QString &branch)
{
    const QString dir = repoGitDir();
    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);
    if (branch.isEmpty() || branch == base || dir.isEmpty())
        return;
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice(
            "This is a read-only mirror; branches can't be updated here.", true);
        return;
    }

    // Nothing to merge if the branch is already current with base.
    int behind = 0, ahead = 0;
    QByteArray counts;
    if (runGitCapture(dir,
                      {"rev-list", "--left-right", "--count", base + "..." + branch},
                      &counts, nullptr)) {
        const QStringList parts = QString::fromUtf8(counts).trimmed().split(
            QRegularExpression(QStringLiteral("\\s+")));
        if (parts.size() >= 2) {
            behind = parts.at(0).toInt();
            ahead = parts.at(1).toInt();
        }
    }
    Q_UNUSED(ahead);
    if (behind == 0) {
        setRepoDetailNotice(
            QStringLiteral("%1 is already up to date with %2.").arg(branch, base));
        return;
    }

    // The merge runs on a checkout, so the working tree must be clean first.
    QByteArray status;
    QString err;
    if (!runGitCapture(dir, {"status", "--porcelain"}, &status, &err) ||
        !status.trimmed().isEmpty()) {
        setRepoDetailNotice(
            err.isEmpty()
                ? "Commit or stash local changes before merging into this branch."
                : err.left(240),
            true);
        return;
    }

    QByteArray headOut;
    QString currentBranch;
    if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &headOut, nullptr))
        currentBranch = QString::fromUtf8(headOut).trimmed();
    const bool isCurrent = !currentBranch.isEmpty() && branch == currentBranch;
    const auto restoreBranch = [&] {
        if (!isCurrent && !currentBranch.isEmpty())
            runGitCapture(dir, {"checkout", currentBranch}, nullptr, nullptr);
    };

    if (!isCurrent && !checkoutReleasingWorktree(dir, branch, &err)) {
        setRepoDetailNotice(
            QStringLiteral("Could not check out %1: %2").arg(branch, err.left(200)),
            true);
        return;
    }

    // Stage the merge but leave the commit to us/the editor. --no-ff guarantees
    // the merge stops even when base could fast-forward, so the clean path below
    // can record a single merge commit consistently.
    runGitCapture(dir, {"merge", "--no-commit", "--no-ff", base}, nullptr, &err);

    QByteArray unmerged;
    runGitCapture(dir, {"diff", "--name-only", "--diff-filter=U"}, &unmerged, nullptr);
    const QStringList conflicted =
        QString::fromUtf8(unmerged).split('\n', Qt::SkipEmptyParts);

    if (conflicted.isEmpty()) {
        // Clean merge — nothing to resolve; record it and report.
        if (!runGitCapture(dir, {"commit", "--no-edit"}, nullptr, &err)) {
            runGitCapture(dir, {"merge", "--abort"}, nullptr, nullptr);
            restoreBranch();
            setRepoDetailNotice(
                QStringLiteral("Could not merge %1 into %2: %3")
                    .arg(base, branch, err.left(200)),
                true);
            return;
        }
        restoreBranch();
        logSystem(QStringLiteral("Git: merged %1 into %2.").arg(base, branch));
        setRepoDetailNotice(
            QString::fromUtf8("Updated %1 with %2 \xE2\x80\x94 no conflicts.")
                .arg(branch, base));
        const QString browsed = m_repoBranch;
        loadBranchesAndTags();
        if (!browsed.isEmpty() && repoBranches().contains(browsed))
            setRepoBranch(browsed);
        return;
    }

    const QString intro =
        QString::fromUtf8(
            "Resolve each conflict, then commit the merge into <b>%1</b>. "
            "<b>Ours</b> is %1; <b>theirs</b> is %2. You can also edit the "
            "text directly.")
            .arg(branch.toHtmlEscaped(), base.toHtmlEscaped());
    const bool committed = runMergeConflictEditor(
        QString::fromUtf8("Merge editor \xE2\x80\x94 %1").arg(branch), intro, dir,
        conflicted, QStringLiteral("Commit merge"), [this, dir](QString *e) {
            return runGitCapture(dir, {"add", "-A"}, nullptr, e) &&
                   runGitCapture(dir, {"commit", "--no-edit"}, nullptr, e);
        });
    if (!committed) {
        runGitCapture(dir, {"merge", "--abort"}, nullptr, nullptr);
        restoreBranch();
        setRepoDetailNotice(
            QStringLiteral("Cancelled the merge of %1 into %2; %2 was left unchanged.")
                .arg(base, branch));
        return;
    }
    restoreBranch();
    logSystem(QStringLiteral("Git: merged %1 into %2 (conflicts resolved).")
                  .arg(base, branch));
    setRepoDetailNotice(QStringLiteral("Updated %1 with %2.").arg(branch, base));
    const QString browsed = m_repoBranch;
    loadBranchesAndTags();
    if (!browsed.isEmpty() && repoBranches().contains(browsed))
        setRepoBranch(browsed);
}

// Would merging `base` into `branch` conflict? Answered with an in-memory merge
// (`merge-tree --write-tree`) that never touches the working tree or index, so it
// is safe to call while building the panel. Returns the merged tree's oid (clean)
// or an empty string (conflicts, or an unmergeable/error case).
static QString branchMergeTree(const QString &dir, const QString &base,
                               const QString &branch)
{
    QByteArray out;
    if (!runGitCapture(dir, {"merge-tree", "--write-tree", branch, base}, &out,
                       nullptr))
        return QString(); // non-zero exit == conflicts (or error)
    return QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts).value(0);
}

void MainWindow::pullBaseIntoAllBranches()
{
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice(
            "This is a read-only mirror; branches can't be updated here.", true);
        return;
    }
    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);
    if (base.isEmpty())
        return;

    // The checked-out branch can't be advanced by a bare ref update without
    // desyncing its working tree, so the batch skips it (it's usually the base);
    // the user can still update it individually with its row's "Pull" button.
    QByteArray headOut;
    QString currentBranch;
    if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &headOut, nullptr))
        currentBranch = QString::fromUtf8(headOut).trimmed();

    // First pass: how many branches are actually behind, so we can bail early
    // when there's nothing to do.
    const auto behindOf = [&](const QString &branch, int *ahead) -> int {
        QByteArray counts;
        if (!runGitCapture(dir,
                           {"rev-list", "--left-right", "--count",
                            base + "..." + branch},
                           &counts, nullptr))
            return 0;
        const QStringList parts = QString::fromUtf8(counts).trimmed().split(
            QRegularExpression(QStringLiteral("\\s+")));
        if (parts.size() < 2)
            return 0;
        if (ahead)
            *ahead = parts.at(1).toInt();
        return parts.at(0).toInt();
    };

    int behindCount = 0;
    for (const QString &branch : branches) {
        if (branch == base || branch == currentBranch)
            continue;
        if (behindOf(branch, nullptr) > 0)
            ++behindCount;
    }
    if (behindCount == 0) {
        setRepoDetailNotice(
            QStringLiteral("Every branch is already up to date with %1.").arg(base));
        return;
    }

    // Acknowledge the click with a spinner on the button. The per-branch
    // git work runs synchronously, so a GitKeepAlive scope pumps the event loop
    // across it to keep the spinner turning; the scope guard restores the button on
    // every exit path below.
    startButtonSpin(m_branchPullAllButton);
    GitKeepAlive keepAlive;
    QPushButton *const spinButton = m_branchPullAllButton;
    const auto spinGuard = qScopeGuard([this, spinButton] {
        stopButtonSpin(spinButton);
    });

    int updated = 0;
    QStringList conflicts, skipped;
    for (const QString &branch : branches) {
        if (branch == base)
            continue;
        int ahead = 0;
        const int behind = behindOf(branch, &ahead);
        if (behind == 0)
            continue;
        if (branch == currentBranch) {
            skipped << branch;
            continue;
        }
        // No commits of its own: a plain fast-forward of the ref, no merge needed.
        if (ahead == 0) {
            if (runGitCapture(dir, {"fetch", ".", base + ":" + branch}, nullptr,
                              nullptr))
                ++updated;
            else
                conflicts << branch;
            continue;
        }
        // Real merge: resolve it in memory; on a clean result, write the merge
        // commit straight onto the branch ref without disturbing the work tree.
        const QString tree = branchMergeTree(dir, base, branch);
        if (tree.isEmpty()) {
            conflicts << branch;
            continue;
        }
        QByteArray commitOut;
        const QString msg = QStringLiteral("Merge %1 into %2").arg(base, branch);
        if (!runGitCapture(dir,
                           {"commit-tree", tree, "-p", branch, "-p", base, "-m", msg},
                           &commitOut, nullptr)) {
            conflicts << branch;
            continue;
        }
        const QString commit = QString::fromUtf8(commitOut).trimmed();
        if (commit.isEmpty() ||
            !runGitCapture(dir, {"update-ref", "refs/heads/" + branch, commit},
                           nullptr, nullptr)) {
            conflicts << branch;
            continue;
        }
        ++updated;
    }

    logSystem(QStringLiteral("Git: pulled %1 into %2 branch(es); %3 conflict(s).")
                  .arg(base)
                  .arg(updated)
                  .arg(conflicts.size()));
    const QString browsed = m_repoBranch;
    loadBranchesAndTags();
    loadBranchesPanel();
    if (!browsed.isEmpty() && repoBranches().contains(browsed))
        setRepoBranch(browsed);

    QString summary =
        QStringLiteral("Pulled %1 into %2 branch(es).").arg(base).arg(updated);
    if (!conflicts.isEmpty())
        summary += QStringLiteral(" %1 have conflicts (%2) — use \"Fix with "
                                  "agent\" in the list.")
                       .arg(conflicts.size())
                       .arg(conflicts.join(QStringLiteral(", ")));
    if (!skipped.isEmpty())
        summary += QStringLiteral(" Skipped the checked-out branch %1.")
                       .arg(skipped.join(QStringLiteral(", ")));
    // Conflicts are an expected outcome here — branches that diverged from the
    // base need an agent to reconcile them — so report the summary as an ordinary
    // notice rather than a persistent red error toast.
    setRepoDetailNotice(summary);
}

void MainWindow::fixBranchConflictsWithAgent(const QString &branch,
                                             const QString &provider,
                                             const QString &modelArg)
{
    if (m_aiFix) {
        flashMessage("An AI conflict fix is already running; wait for it to finish.",
                     true);
        return;
    }
    if (!m_agentStore || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString dir = writableRecordFor(repo).localPath;
    if (dir.isEmpty()) {
        QMessageBox::warning(this, "Fix with agent",
                             "This repository is read-only on this node.");
        return;
    }
    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);
    if (branch.isEmpty() || branch == base || base.isEmpty())
        return;

    // "claude-code" drives the real `claude` CLI (no API key, authenticates via the
    // local login); the two API providers POST each conflicted file to their endpoint.
    const bool claudeCode = provider == QLatin1String("claude-code");
    const bool claude = !claudeCode && agentIsClaudeProvider(provider);
    // The dropdown lets the user pick a model per provider (adhoc #60). For the
    // API providers an empty choice falls back to the provider's low-cost
    // default; Claude Code passes the alias straight through to the CLI as
    // --model (empty = the CLI's own default).
    QString model = modelArg.trimmed();
    if (model.isEmpty() && !claudeCode)
        model = claude ? QStringLiteral("claude-haiku-4-5")
                       : QStringLiteral("gpt-4.1-nano");
    QString apiKey;
    if (!claudeCode) {
        apiKey = (claude ? QSettings().value(kClaudeApiKeySetting)
                         : QSettings().value(kCodexApiKeySetting))
                     .toString()
                     .trimmed();
        if (apiKey.isEmpty()) {
            flashMessage(claude ? "Add a Claude API key in Settings first."
                                : "Add an OpenAI API key in Settings first.",
                         true);
            return;
        }
    }

    // Nothing to do if it's already current.
    QByteArray counts;
    int behind = 0;
    if (runGitCapture(dir,
                      {"rev-list", "--left-right", "--count", base + "..." + branch},
                      &counts, nullptr)) {
        const QStringList parts = QString::fromUtf8(counts).trimmed().split(
            QRegularExpression(QStringLiteral("\\s+")));
        if (!parts.isEmpty())
            behind = parts.at(0).toInt();
    }
    if (behind == 0) {
        setRepoDetailNotice(
            QStringLiteral("%1 is already up to date with %2.").arg(branch, base));
        return;
    }

    // Branches that live in their own dedicated worktree (every agent branch) are
    // already checked out there, and git refuses to check a branch out a second
    // time in the main checkout — so the old "checkout in main, then merge" path
    // failed before the agent ever started. Run the merge directly in that worktree
    // instead: it's already on the branch, so there's nothing to check out or
    // restore. Branches without a worktree fall back to merging in the main
    // checkout, briefly switched onto the branch and restored afterwards.
    const QString branchWorktree = worktreePathForBranch(dir, branch);
    const bool inBranchWorktree = !branchWorktree.isEmpty();
    const QString mergeDir = inBranchWorktree ? branchWorktree : dir;

    // The merge happens in mergeDir, so its working tree must be clean first.
    QByteArray status;
    if (!runGitCapture(mergeDir, {"status", "--porcelain"}, &status, nullptr) ||
        !status.trimmed().isEmpty()) {
        setRepoDetailNotice(
            "Commit or stash local changes before fixing this branch.", true);
        return;
    }

    // Only the main-checkout path checks out the branch (and restores afterwards);
    // a dedicated worktree is already on it, so restoreBranch stays empty.
    QString restoreBranch;
    QString err;
    if (!inBranchWorktree) {
        QByteArray headOut;
        if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &headOut,
                          nullptr))
            restoreBranch = QString::fromUtf8(headOut).trimmed();
        if (restoreBranch != branch &&
            !checkoutReleasingWorktree(dir, branch, &err)) {
            setRepoDetailNotice(
                QStringLiteral("Could not check out %1: %2").arg(branch, err.left(240)),
                true);
            return;
        }
    }

    // A clean merge needs no agent — commit it and we're done.
    if (runGitCapture(mergeDir, {"merge", "--no-edit", base}, nullptr, &err)) {
        if (!restoreBranch.isEmpty() && restoreBranch != branch)
            runGitCapture(mergeDir, {"checkout", restoreBranch}, nullptr, nullptr);
        logSystem(QStringLiteral("Git: merged %1 into %2 (no conflicts).")
                      .arg(base, branch));
        setRepoDetailNotice(
            QStringLiteral("Updated %1 with %2 (no conflicts).").arg(branch, base));
        loadBranchesAndTags();
        loadBranchesPanel();
        return;
    }

    QByteArray unmerged;
    runGitCapture(mergeDir, {"diff", "--name-only", "--diff-filter=U"}, &unmerged,
                  nullptr);
    const QStringList conflicted =
        QString::fromUtf8(unmerged).split('\n', Qt::SkipEmptyParts);
    if (conflicted.isEmpty()) {
        // Failed for some other reason — restore as before.
        runGitCapture(mergeDir, {"merge", "--abort"}, nullptr, nullptr);
        if (!restoreBranch.isEmpty() && restoreBranch != branch)
            runGitCapture(mergeDir, {"checkout", restoreBranch}, nullptr, nullptr);
        setRepoDetailNotice(
            QStringLiteral("Could not merge %1 into %2: %3")
                .arg(base, branch, err.left(160)),
            true);
        return;
    }

    // Spin up a visible agent session so the run shows on the Agents tab.
    AgentSession session;
    session.owner = repo.owner;
    session.name = repo.name;
    session.issueNumber = 0; // branch-scoped, not issue- or PR-scoped
    session.issueTitle =
        QStringLiteral("Resolve conflicts merging %1 into %2").arg(base, branch);
    session.provider = provider;
    session.branchName = branch;
    session.status = AgentStatus::Running;
    session = m_agentStore->createSession(session);
    session.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_agentStore->saveSession(session);
    m_agentStore->appendLog(
        session,
        model.isEmpty()
            ? QStringLiteral("==> %1 resolving merge conflicts: %2 into %3.\n")
                  .arg(agentProviderName(provider), base, branch)
            : QStringLiteral("==> %1 (%2) resolving merge conflicts: %3 into %4.\n")
                  .arg(agentProviderName(provider), model, base, branch));

    m_aiFix = new AiConflictFix;
    m_aiFix->branchMerge = true;
    m_aiFix->repoIndex = m_repoDetailIndex;
    m_aiFix->sessionId = session.id;
    m_aiFix->provider = provider;
    m_aiFix->model = model;
    m_aiFix->apiKey = apiKey;
    m_aiFix->workTree = mergeDir;
    m_aiFix->files = conflicted;
    m_aiFix->branch = branch;
    m_aiFix->baseBranch = base;
    m_aiFix->restoreBranch = restoreBranch;
    m_aiFix->claudeCode = claudeCode;

    switchToAgentsTab(session.id);
    aiFixLog(QStringLiteral("==> %1 file(s) to resolve: %2\n")
                 .arg(conflicted.size())
                 .arg(conflicted.join(QStringLiteral(", "))));
    if (m_aiFix->claudeCode)
        aiFixRunClaudeCode();
    else
        aiFixResolveNextFile();
}

void MainWindow::onBranchDiffAnchorClicked(const QUrl &url)
{
    if (url.scheme() != QLatin1String("viewed"))
        return;
    const QString path = url.path();
    // Persist against the current scope's context, not always the whole-branch
    // one, so Viewed sticks per commit / per uncommitted-changes view.
    const QString context = m_branchDiffViewedContext.isEmpty()
                                ? QStringLiteral("branch/") + m_branchDiffBranch
                                : m_branchDiffViewedContext;
    const QSet<QString> cur = loadDiffViewed(context);
    setDiffViewed(context, path, !cur.contains(path));
    const int scroll =
        m_branchDiffView ? m_branchDiffView->verticalScrollBar()->value() : 0;
    // Re-render only the current scope so the scope selection isn't reset. The
    // toggle changes only the viewed set, not the patch, so re-render from the
    // cached patch synchronously — this both avoids a redundant git read and keeps
    // the scroll-position restore below correct (renderBranchScopeDiff is async
    // now, so its render would land after the restore — issue #353).
    if (m_branchDiffLastValid)
        renderBranchDiffPatch(QString::fromUtf8(m_branchDiffLastPatch),
                              m_branchDiffLastEmpty, context);
    else
        renderBranchScopeDiff();
    if (m_branchDiffView)
        m_branchDiffView->verticalScrollBar()->setValue(scroll);
}

void MainWindow::updateBranchDiffSticky()
{
    if (!m_branchDiffView || !m_branchDiffSticky)
        return;
    const int sv = m_branchDiffView->verticalScrollBar()->value();
    QString cur;
    if (!m_branchDiffFileSpans.isEmpty()) {
        const QTextCursor top = m_branchDiffView->cursorForPosition(QPoint(2, 2));
        const int pos = top.position();
        for (const auto &span : m_branchDiffFileSpans) {
            if (span.first <= pos)
                cur = span.second;
            else
                break;
        }
    }
    if (cur.isEmpty() || sv <= 0) {
        m_branchDiffSticky->hide();
        return;
    }
    const QString viewedContext = m_branchDiffViewedContext.isEmpty()
                                      ? QStringLiteral("branch/") + m_branchDiffBranch
                                      : m_branchDiffViewedContext;
    const bool isViewed = loadDiffViewed(viewedContext).contains(cur);
    const QString encPath = QString::fromLatin1(QUrl::toPercentEncoding(cur));
    const QString pathHtml = diffStickyPathHtml(cur);
    m_branchDiffSticky->setText(
        QStringLiteral("<table width='100%' cellspacing='0' cellpadding='0'><tr><td>%1"
                       "</td><td align='right'>"
                       "<a style='color:%2; text-decoration:none' href='viewed:%3'>"
                       "<span style='font-size:19px'>%4</span> Viewed</a>"
                       "</td></tr></table>")
            .arg(pathHtml,
                 isViewed ? QStringLiteral("#3fb950") : QStringLiteral("#8b949e"),
                 encPath,
                 isViewed ? QString::fromUtf8("\xE2\x98\x91")
                          : QString::fromUtf8("\xE2\x98\x90")));
    m_branchDiffSticky->setGeometry(0, 0, m_branchDiffView->viewport()->width(),
                                    m_branchDiffSticky->sizeHint().height());
    m_branchDiffSticky->show();
    m_branchDiffSticky->raise();
}

QString MainWindow::diffViewedScope(const QString &context) const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return QString();
    const RepositoryRecord &r = m_repositories.at(m_repoDetailIndex);
    return r.owner + QStringLiteral("/") + r.name + QStringLiteral("/") + context;
}

QSet<QString> MainWindow::loadDiffViewed(const QString &context) const
{
    const QString scope = diffViewedScope(context);
    if (scope.isEmpty())
        return {};
    const QStringList list =
        QSettings().value(QStringLiteral("diffViewed/") + scope).toStringList();
    return QSet<QString>(list.begin(), list.end());
}

void MainWindow::setDiffViewed(const QString &context, const QString &path, bool viewed)
{
    const QString scope = diffViewedScope(context);
    if (scope.isEmpty())
        return;
    QSet<QString> set = loadDiffViewed(context);
    if (viewed)
        set.insert(path);
    else
        set.remove(path);
    QSettings().setValue(QStringLiteral("diffViewed/") + scope,
                         QStringList(set.begin(), set.end()));
}

