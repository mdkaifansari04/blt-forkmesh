// MainWindowBranches: MainWindow feature methods, split out of MainWindow.cpp.
// Branches panel and worktrees.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"
#include "PacmanProgress.h"

#include <QComboBox>
#include <QTimer>

#include <algorithm>

using namespace forkmesh::ui;

// ---- Branches panel --------------------------------------------------------

// Defined further down (next to pullBaseIntoAllBranches); declared here so
// loadBranchesPanel can probe each branch for merge conflicts.
static QString branchMergeTree(const QString &dir, const QString &base,
                               const QString &branch);

// Suffix the Status cell / detail label carry for a branch that can't be merged
// into base cleanly. Shared so the background probe (adhoc #416) appends exactly
// what the inline pass would have written — and can tell it's already there.
static const QString kBranchConflictsSuffix =
    QString::fromUtf8(" \xC2\xB7 conflicts");

// Extra row data painted into the Branch cell. Keeping the actual branch name
// as DisplayRole preserves sorting, selection, navigation and test automation;
// the delegate simply composes the Agent-list-style leading metadata around it.
static constexpr int kBranchUpdatedRole = Qt::UserRole + 70;
static constexpr int kBranchWorktreeRole = Qt::UserRole + 71;
static constexpr int kBranchFilesRole = Qt::UserRole + 72;
static constexpr int kBranchAddedRole = Qt::UserRole + 73;
static constexpr int kBranchRemovedRole = Qt::UserRole + 74;
static constexpr int kBranchConflictRole = Qt::UserRole + 75;

class BranchOverviewDelegate : public SelectionBorderRowDelegate
{
public:
    explicit BranchOverviewDelegate(QAbstractItemView *view)
        : SelectionBorderRowDelegate(view)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize size = SelectionBorderRowDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 29));
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        // Let the shared delegate paint hover/selection and the row outline, but
        // suppress its normal text/icon: the compact row below controls their
        // exact order (status, age, worktree, stats, then full branch name).
        QStyleOptionViewItem background(option);
        background.text.clear();
        background.icon = QIcon();
        SelectionBorderRowDelegate::paint(painter, background, index);

        painter->save();
        painter->setClipRect(option.rect);
        const bool selected = option.state & QStyle::State_Selected;
        const QVariant foreground = index.data(Qt::ForegroundRole);
        const QColor primary = selected
                                   ? option.palette.color(QPalette::HighlightedText)
                                   : foreground.canConvert<QBrush>()
                                         ? foreground.value<QBrush>().color()
                                         : option.palette.color(QPalette::Text);
        const QColor muted = selected ? primary : QColor(QStringLiteral("#8b949e"));
        const int cy = option.rect.center().y();
        int x = option.rect.left() + 8;

        const QIcon statusIcon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!statusIcon.isNull()) {
            statusIcon.paint(painter, QRect(x, cy - 7, 14, 14));
            x += 19;
        } else {
            // Status icons line up even for branches without an agent.
            x += 19;
        }

        painter->setPen(muted);
        const QString updated = index.data(kBranchUpdatedRole).toString();
        const int ageWidth = qMax(28, option.fontMetrics.horizontalAdvance(updated) + 7);
        painter->drawText(QRect(x, option.rect.top(), ageWidth, option.rect.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, updated);
        x += ageWidth;

        if (index.data(kBranchWorktreeRole).toBool()) {
            themedOcticon("file-directory", QColor("#58a6ff"), 13)
                .paint(painter, QRect(x, cy - 7, 14, 14));
            x += 18;
        }

        const QVariant filesValue = index.data(kBranchFilesRole);
        const int files = filesValue.isValid() ? filesValue.toInt() : -1;
        if (files >= 0) {
            painter->setPen(muted);
            const QString fileText = files > 99 ? QStringLiteral("99+")
                                                : QString::number(files);
            const int width = option.fontMetrics.horizontalAdvance(fileText) + 5;
            painter->drawText(QRect(x, option.rect.top(), width, option.rect.height()),
                              Qt::AlignVCenter | Qt::AlignLeft, fileText);
            x += width;
        }

        if (index.data(kBranchConflictRole).toBool()) {
            themedOcticon("alert", QColor("#d29922"), 13)
                .paint(painter, QRect(x, cy - 7, 14, 14));
            x += 18;
        }

        const QVariant addedValue = index.data(kBranchAddedRole);
        const QVariant removedValue = index.data(kBranchRemovedRole);
        const int added = addedValue.isValid() ? addedValue.toInt() : -1;
        const int removed = removedValue.isValid() ? removedValue.toInt() : -1;
        if (added >= 0 && removed >= 0) {
            constexpr int barWidth = 4;
            constexpr int barGap = 2;
            constexpr int maxHeight = 15;
            const int total = qMax(1, added + removed);
            const int addHeight = added == 0 ? 1 : qMax(2, added * maxHeight / total);
            const int removeHeight = removed == 0 ? 1
                                                  : qMax(2, removed * maxHeight / total);
            painter->fillRect(QRect(x, cy + maxHeight / 2 - addHeight,
                                    barWidth, addHeight),
                              QColor("#3fb950"));
            painter->fillRect(QRect(x + barWidth + barGap,
                                    cy + maxHeight / 2 - removeHeight,
                                    barWidth, removeHeight),
                              QColor("#f85149"));
            x += 2 * barWidth + barGap + 8;
        }

        painter->setPen(primary);
        const QRect titleRect(x, option.rect.top(),
                              qMax(0, option.rect.right() - x - 5),
                              option.rect.height());
        // Deliberately no elision: the table clips at its real right edge, so
        // widening the page reveals more of the branch instead of a premature ….
        painter->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
    }
};

// Cache key for an in-memory merge probe: the exact commit pair it merges, so a
// verdict is only ever reused while both tips are unchanged (a new commit on
// either side misses and re-probes). Empty when a tip sha is unknown, which the
// callers treat as "not cacheable".
static QString branchConflictKey(const QString &dir, const QString &baseSha,
                                 const QString &branchSha)
{
    if (dir.isEmpty() || baseSha.isEmpty() || branchSha.isEmpty())
        return QString();
    return dir + QLatin1Char('\n') + baseSha + QLatin1Char('\n') + branchSha;
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
    // Selecting a worktree records it, so the action toolbar below (and the row
    // buttons) act on it. It does NOT render a diff here any more: the branch's
    // changes belong in the Git view's one compare pane (adhoc #16), which the
    // click handler below opens.
    connect(m_worktreesTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                QTableWidgetItem *b = m_worktreesTable->item(row, 0);
                QTableWidgetItem *p = m_worktreesTable->item(row, 1);
                updateWorktreeSelection(
                    b ? b->data(Qt::UserRole).toString() : QString(),
                    p ? p->text() : QString());
            });
    // Clicking a worktree opens its branch in the Git view — its graph plus its
    // diff against the base — exactly as clicking a row in the Branches panel
    // does. There is only one place a branch's changes are shown (adhoc #16).
    // Selection alone (an arrow key, or the re-select after a rebuild) must not
    // navigate, so this rides cellClicked rather than currentCellChanged.
    connect(m_worktreesTable, &QTableWidget::cellClicked, this,
            [this](int row, int column) {
                if (column >= 4)
                    return; // action column: its cell widgets own their clicks
                QTableWidgetItem *b = m_worktreesTable->item(row, 0);
                const QString branch =
                    b ? b->data(Qt::UserRole).toString() : QString();
                if (!branch.isEmpty())
                    switchToBranch(branch);
            });

    // Action bar: a prominent "Merge into main" for the selected worktree, plus
    // the rest of what can be done to it. Mirrors the per-row buttons but always
    // acts on whichever worktree is selected.
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
    // updateWorktreeSelection).
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
    // Action bar for the selected worktree, under the table. The changed-files
    // list and diff viewer that used to sit beside it are gone (adhoc #16):
    // reviewing a branch's changes happens in the Git view's compare pane and
    // nowhere else, so this panel is the worktree list plus what you can do to
    // the selected one.
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

    layout->addWidget(m_worktreesTable, 1);
    layout->addLayout(detailBar);
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
    trackProcessActivity(git, QStringLiteral("git"),
                         QStringLiteral("git ") + args.join(QLatin1Char(' ')));
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
    else if (id == m_projectsTabIndex)
        table = m_projectTable;
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
        if (m_worktreesButton)
            m_worktreesButton->setText(QStringLiteral("Worktrees"));
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

    // PullStore keeps PR metadata in a private linked worktree on this reserved
    // branch. It is implementation storage, not a user or agent workspace, so
    // keep it out of the Worktrees tab and keyboard-navigation order.
    wts.erase(std::remove_if(wts.begin(), wts.end(), [](const WT &wt) {
                  return wt.branch == QLatin1String("forkmesh/pulls");
              }),
              wts.end());

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
        // Review a worktree through the same Git range viewer as its branch and
        // any branch-backed PR. That viewer includes the source graph, commits,
        // changed files and this checkout's uncommitted changes.
        if (!wt.branch.isEmpty()) {
            const QString branch = wt.branch;
            auto *reviewBtn = new QPushButton("Review changes");
            reviewBtn->setObjectName("ghostButton");
            reviewBtn->setCursor(Qt::PointingHandCursor);
            setOcticon(reviewBtn, "git-compare", 14);
            reviewBtn->setToolTip(
                "Open this worktree in the shared Git branch and diff viewer");
            connect(reviewBtn, &QPushButton::clicked, this,
                    [this, branch, isMain] {
                        if (isMain) {
                            showOverviewCommits();
                            setCommitWorkspacePage(kCommitWorkspaceChangesPage);
                            refreshSourceControl();
                            loadCommits();
                        } else {
                            switchToBranch(branch);
                        }
                    });
            h->addWidget(reviewBtn);
        }
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
    if (m_worktreesButton)
        m_worktreesButton->setText(
            QStringLiteral("%1 %2")
                .arg(formatCount(wts.size()))
                .arg(wts.size() == 1 ? QStringLiteral("worktree")
                                     : QStringLiteral("worktrees")));

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

// Open a named worktree in the Git view. A worktree is the checkout of a
// branch, so it lands on the same branch-browsing view switchToBranch uses.
// Detached worktrees have no branch and remain available through the
// Worktrees panel itself.
void MainWindow::switchToWorktree(const QString &branch)
{
    if (!branch.trimmed().isEmpty()) {
        switchToBranch(branch);
        return;
    }
    showOverviewWorktrees();
    loadWorktreesPanel();
}

// Open a branch in the Git view — clicking a branch name in the agent session
// header, the PR header, the Branches panel or the graph's branch dropdown
// lands here. There is no separate branch view any more (adhoc #16): the graph
// browses the branch's history exactly as before, and for any branch other than
// the compare base the right pane opens that branch's range diff against the
// base at the same time, with the "<branch> -> <base>" indicator on the branch
// row naming the comparison. The base starts at the repo's default branch and
// both ends are switchable (the branch button's dropdown, the base button's).
// The Branches panel's refresh below keeps its rows in step and reports a
// branch that doesn't exist (adhoc #185/#420).
void MainWindow::switchToBranch(const QString &branch)
{
    // setRepoBranch below reassigns m_repoBranch, which some callers pass in by
    // reference — copy before the string underneath us can change.
    const QString target = branch.trimmed();
    // Every explicit branch navigation is a fresh opportunity to catch up with
    // a base that may have advanced since this branch was last viewed. Internal
    // rerenders keep the attempted marker, so a failed update still cannot loop.
    m_branchAutoPullAttempted.clear();
    m_branchDiffPullNumber = -1; // plain branch mode
    showOverviewCommits();
    const QString base = branchCompareBase();
    if (!target.isEmpty() && target != m_repoBranch) {
        setRepoBranch(target); // rebuilds the graph + branch button for the ref
    } else {
        // Already browsing this ref: fill the workspace the same deferred way
        // the Commits toggle does.
        QTimer::singleShot(0, this, [this] {
            if (commitsListIsCurrent())
                refreshSourceControl();
            else
                loadCommits();
        });
    }
    if (target.isEmpty() || target == base) {
        // The base has no range against itself: plain working-tree view.
        setCommitWorkspacePage(kCommitWorkspaceChangesPage);
    } else {
        // Compare against the base on the right pane (merge/PR toolbar
        // included), with the left column's CHANGES + graph staying put.
        setCommitWorkspacePage(kCommitWorkspaceRangePage);
        showBranchDiff(target);
        // Ctrl+F and the scroll-driven tools act on the diff from the first key.
        if (m_branchDiffView)
            m_branchDiffView->setFocus();
    }
    // Branch selection is a browser-style destination: Back/Forward must be
    // able to return to the previous branch (or the main Git view).
    scheduleNavRecord();
    if (!m_branchesTable || target.isEmpty()) {
        loadBranchesPanel();
        return;
    }
    // Keep the branches table's selection in step when its rows were built for
    // this repo; otherwise let the refresh land on the branch (and tell the user
    // when it doesn't exist here at all — adhoc #185/#420).
    const bool fresh = !m_branchesPanelDir.isEmpty() &&
                       m_branchesPanelDir == repoGitDir();
    if (fresh && selectBranchRow(target))
        m_branchesPanelPendingSelect.clear();
    else
        m_branchesPanelPendingSelect = target;
    loadBranchesPanel();
}

// Base branch of the Git view's comparison: whatever the compare indicator's
// base dropdown picked, or the repo's default branch until then (adhoc #16).
QString MainWindow::branchCompareBase() const
{
    return m_branchCompareBase.isEmpty() ? repoDefaultBranchFast()
                                         : m_branchCompareBase;
}

// Re-diff the branch on screen against another base (adhoc #16). Picking the
// branch's own name as the base would leave an empty range, so that collapses
// back to the plain working-tree view the base branch always shows.
void MainWindow::setBranchCompareBase(const QString &base)
{
    const QString target = base.trimmed();
    m_branchCompareBase =
        target == repoDefaultBranchFast() ? QString() : target;
    const QString branch = m_repoBranch.isEmpty() ? repoHeadBranch() : m_repoBranch;
    if (branch.isEmpty() || branch == branchCompareBase()) {
        setCommitWorkspacePage(kCommitWorkspaceChangesPage);
        return;
    }
    setCommitWorkspacePage(kCommitWorkspaceRangePage);
    m_branchDiffPullNumber = -1; // a base change leaves PR mode
    showBranchDiff(branch);
}

// Leave the compare view entirely: working-tree diff on the right pane, graph
// back on the default branch, and the base reset so the next branch opens
// against main again (adhoc #16).
void MainWindow::closeBranchCompareView()
{
    m_branchCompareBase.clear();
    setCommitWorkspacePage(kCommitWorkspaceChangesPage);
    const QString base = repoDefaultBranchFast();
    if (!base.isEmpty() && m_repoBranch != base)
        setRepoBranch(base);
}

bool MainWindow::selectBranchRow(const QString &branch)
{
    if (!m_branchesTable || branch.isEmpty())
        return false;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        QTableWidgetItem *it = m_branchesTable->item(row, 0);
        if (it && it->text() == branch) {
            m_branchesTable->selectRow(row); // fires currentCellChanged -> diff
            return true;
        }
    }
    return false;
}

// Clicking an agent's branch link when that branch no longer exists here (it may
// have been merged and deleted, or never synced into this checkout) would
// otherwise land on the Branches panel with nothing selected. Tell the user why
// rather than leaving them on a silently empty selection (adhoc #185).
// Deferred a tick because the answer now arrives inside the panel's rebuild: a
// modal dialog there would pump the event loop mid-build, letting a queued
// reload rebuild the table underneath it.
void MainWindow::reportBranchNotFound(const QString &branch)
{
    QTimer::singleShot(0, this, [this, branch] {
        QMessageBox::information(
            this, QStringLiteral("Branch not found"),
            QStringLiteral("Branch '%1' was not found in this repository. "
                           "It may have been merged and deleted.")
                .arg(branch));
    });
}

#ifdef FORKMESH_WINDOW_TESTS
// Rebuild the branches panel and wait for its off-thread git reads to land, so
// tests can read the rows straight after (adhoc #420).
void MainWindow::testReloadBranchesPanel()
{
    loadBranchesPanel();
    QDeadlineTimer deadline(10000);
    while ((m_branchesPanelLoading || m_branchChangeStatsLoading) &&
           !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

QString MainWindow::testBranchVisualBadges(const QString &branch) const
{
    if (!m_branchesTable)
        return QString();
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        const QTableWidgetItem *item = m_branchesTable->item(row, 0);
        if (!item || item->text() != branch)
            continue;
        auto value = [item](int role) {
            const QVariant data = item->data(role);
            return data.isValid() ? data.toString() : QStringLiteral("-");
        };
        return QStringLiteral("%1|%2|%3|%4|%5|%6")
            .arg(value(kBranchFilesRole), value(kBranchAddedRole),
                 value(kBranchRemovedRole),
                 item->data(kBranchWorktreeRole).toBool() ? QStringLiteral("1")
                                                          : QStringLiteral("0"),
                 item->data(kBranchConflictRole).toBool() ? QStringLiteral("1")
                                                          : QStringLiteral("0"),
                 value(kBranchUpdatedRole));
    }
    return QString();
}

bool MainWindow::testBranchesUseCompactColumns() const
{
    return m_branchesTable && m_branchesTable->isColumnHidden(2) &&
           m_branchesTable->isColumnHidden(3);
}

QString MainWindow::testSwitchToBranchImmediateSelection(const QString &branch)
{
    switchToBranch(branch);
    if (!m_branchesTable)
        return QString();
    const QTableWidgetItem *it = m_branchesTable->item(m_branchesTable->currentRow(), 0);
    return it ? it->text() : QString();
}

bool MainWindow::testClickBranchRowInOverview(const QString &branch)
{
    if (!m_branchesTable)
        return false;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        QTableWidgetItem *item = m_branchesTable->item(row, 0);
        if (!item || item->text() != branch)
            continue;
        // Emit the same signal a real non-action cell click produces.
        emit m_branchesTable->cellClicked(row, 0);
        return true;
    }
    return false;
}

bool MainWindow::testBranchesPanelOwnsDiffView() const
{
    return m_branchesTable && m_branchDiffView &&
           m_branchesTable->parentWidget() &&
           m_branchesTable->parentWidget()->isAncestorOf(m_branchDiffView);
}

int MainWindow::testCommitWorkspacePage() const
{
    return m_commitsStack ? m_commitsStack->currentIndex() : -1;
}

int MainWindow::testGitFilesSlotPage() const
{
    return m_gitFilesSlot ? m_gitFilesSlot->currentIndex() : -1;
}

int MainWindow::testGitHistorySlotPage() const
{
    return m_gitHistorySlot ? m_gitHistorySlot->currentIndex() : -1;
}

void MainWindow::testCloseBranchRange()
{
    if (m_branchCloseButton)
        m_branchCloseButton->click();
}

void MainWindow::testClickRailGitButton()
{
    if (m_railGitButton)
        m_railGitButton->click();
}

QString MainWindow::testCompareIndicatorText() const
{
    if (!m_commitsCompareBaseButton || m_commitsCompareBaseButton->isHidden())
        return QString();
    if (auto *elider =
            dynamic_cast<ElidingPushButton *>(m_commitsCompareBaseButton))
        return elider->fullText();
    return m_commitsCompareBaseButton->text();
}

void MainWindow::testSetCompareBase(const QString &base)
{
    setBranchCompareBase(base);
}

bool MainWindow::testClickBranchReviewMerge(bool deleteAll)
{
    QPushButton *button = deleteAll ? m_branchMergeDeleteButton : m_branchMergeButton;
    if (!button)
        return false;
    // updateBranchDetailActions fills the detail bar from a worker thread, and a
    // click on a still-disabled button is a silent no-op — wait for the real
    // enabled state rather than reaching past the button.
    QDeadlineTimer deadline(10000);
    while (!button->isEnabled() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!button->isEnabled())
        return false;
    button->click();
    return true;
}
#endif

bool MainWindow::selectWorktreeRow(const QString &branch)
{
    if (!m_worktreesTable || branch.isEmpty())
        return false;
    for (int row = 0; row < m_worktreesTable->rowCount(); ++row) {
        QTableWidgetItem *b = m_worktreesTable->item(row, 0);
        if (b && b->data(Qt::UserRole).toString() == branch) {
            m_worktreesTable->selectRow(row); // fires currentCellChanged -> selection
            return true;
        }
    }
    return false;
}

// Record which worktree is selected and gate the action bar's buttons on it.
// This used to render the worktree's diff vs the default branch beside the
// table; that view is gone (adhoc #16) — a branch's changes are shown in the
// Git view's compare pane and nowhere else, so clicking a row navigates there
// instead. Dropping the render also takes a synchronous `git diff` off the GUI
// thread on every selection change.
void MainWindow::updateWorktreeSelection(const QString &branch,
                                         const QString &worktreePath)
{
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
}

// A merge started from the branch review ends that review (adhoc #119): the
// branch's work is in the base branch now, so the diff on screen either describes
// history the user is done with or — after "Merge & delete all" — a branch that no
// longer exists. Hand the Git view's columns back to the working tree, exactly
// like the pane's own ✕ does, instead of leaving a finished diff open.
void MainWindow::closeBranchDiffAfterMerge()
{
    closeBranchCompareView();
}

// Merge a worktree's branch into the repo's default branch. Direct + safe: only
// when the primary checkout is ON the default branch and clean (otherwise it
// would clobber concurrent WIP) — else point the user at Create PR.
bool MainWindow::mergeWorktreeIntoMain(const QString &branchArg,
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
        return false;
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice("Read-only mirror — nothing to merge into here.", true);
        return false;
    }
    // Uncommitted work in the checkout blocks the merge outright — check it first,
    // because it is also the only thing that makes the branch switch below unsafe.
    QByteArray st;
    if (runGitCapture(dir, {"status", "--porcelain"}, &st, nullptr)
        && !QString::fromUtf8(st).trimmed().isEmpty()) {
        setRepoDetailNotice(
            "The checkout has uncommitted changes — commit/stash them or use Create PR.",
            true);
        return false;
    }
    // The merge commit has to land on `base`, so `base` has to be what's checked
    // out. Ask git rather than reading the browsed ref: that ref is pinned to the
    // default branch (adhoc #80), so it can no longer stand in for HEAD here.
    //
    // When they differ, just switch the checkout back to base instead of refusing.
    // The tree is clean (checked above), so nothing can be clobbered — and being
    // parked elsewhere is almost never a state the user chose: every merge path
    // transiently checks another branch out in this checkout ("Update from main",
    // the merge editor, the PR update flow), and one that was interrupted leaves
    // HEAD sitting there. Refusing then produced the confusing report this fixes:
    // "Switch the repo to main first (it's on <branch>)" while the status strip
    // below read main, with no visible way to switch. An empty head means a
    // detached HEAD, which needs the switch just as much: merging there would put
    // the merge commit on no branch at all while the teardown below — seeing the
    // branch contained in HEAD — deleted it.
    const QString head = repoHeadBranch();
    if (head != base) {
        QString cerr;
        if (!runGitCapture(dir, {"checkout", base}, nullptr, &cerr)) {
            setRepoDetailNotice(
                QStringLiteral("Couldn't switch the checkout to %1 (it's on %2): %3")
                    .arg(base, head.isEmpty() ? QStringLiteral("a detached HEAD") : head,
                         cerr.left(200)),
                true);
            return false;
        }
        logSystem(QStringLiteral("Git: switched the checkout to %1 to merge %2 into it.")
                      .arg(base, branch));
    }
    // No confirmation dialog on any merge path (adhoc #441, extending #130's "just
    // do the merge in the background, don't jump around"): every entry point here is
    // an explicit click on a button that spells out what it deletes, so a modal over
    // the view only adds a keystroke. The cleanup stays safe without it — a worktree
    // with uncommitted changes bails out below, and a merge that conflicts keeps the
    // worktree and branch instead of deleting them.

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
            return false;
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
            return false;
        }
    }

    // Set when the branch itself is gone by the end of the merge (via removeWorktree
    // or the no-worktree teardown below) — the selection handling at the bottom needs
    // to know it can't re-select it.
    bool branchDeleted = false;
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
            branchDeleted = removed && !localBranchExists(dir, branch);
        } else if (deleteAgent && localBranchExists(dir, branch)) {
            // "Merge & delete all" on a branch with no worktree of its own (adhoc
            // #428): removeWorktree is what normally drops the branch, so delete it
            // here instead. Safe by the branchInBase gate above — the commits are in
            // the base branch, so -D discards nothing.
            branchDeleted = runGitCapture(dir, {"branch", "-D", branch}, nullptr, nullptr);
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
                     : branchDeleted
                           ? QStringLiteral("Merged %1 into %2 and deleted its branch")
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
    // adhoc #139: a clean "Merge to main" from the Branches view (no worktree or
    // agent to tear down) leaves the merged branch in the list, so re-selecting it
    // would just re-render the now-empty diff. Instead advance the selection to the
    // next branch in the list so the user can keep merging down the list without the
    // page snapping back to the branch they just finished. Steer loadBranchesPanel()'s
    // "re-select the previously-viewed branch" logic (which reads m_branchDiffBranch)
    // at the neighbour, exactly like the post-delete flow does (adhoc #256). Compute
    // the neighbour now, while the table still holds the pre-refresh row order. Only
    // for the Branches-view buttons (no worktree path passed) — the Worktrees/Agents
    // merge flows delete the branch and keep their own selection handling. "Merge &
    // delete all" lands here too when the branch had no worktree: the branch is gone,
    // so there's even less to re-select (adhoc #428).
    //
    // A branch that was actually deleted takes neither path (adhoc #15): moving to
    // the neighbour renders a diff the user didn't ask for, and re-selecting a
    // branch that no longer exists falls back to the checked-out one. Leave an
    // animated check in its row instead and select nothing.
    if (branchDeleted)
        flashMergedBranchRow(branch);
    else if (merged && !hasConflicts && branchInBase && worktreePath.isEmpty()) {
        const QString next = neighbourBranchInList(branch);
        if (!next.isEmpty())
            m_branchDiffBranch = next;
    }
    // adhoc #100: the merge just landed a new commit (or, on a failed merge, an
    // aborted one) directly in this checkout, so the top "Sync" button and the
    // Changes panel would otherwise stay stale — showing 0 pending commits — until
    // the user manually refreshes. Force both to recheck now.
    refreshSourceControl(true);
    loadWorktreesPanel();
    // Issue #211: refresh the cheap branch tip/count, but don't eagerly rebuild
    // the Branches panel — it runs a git command per branch (probing each for
    // merge conflicts), which was slow and pointless here since "Merge into main"
    // is driven from the Agents/Worktrees tabs, not the Branches tab.
    // loadBranchesAndTags() repaints the panel only if it's the visible tab.
    loadBranchesAndTags();
    return merged && !hasConflicts && branchInBase;
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

void MainWindow::deleteWorktreeBranchAndAgentInBackground(
    const QString &worktreePath, const QString &branch)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    const QString repoPath = repo.localPath;
    if (repoPath.isEmpty() || branch.trimmed().isEmpty())
        return;
    if (!worktreePath.isEmpty() &&
        QDir(worktreePath).absolutePath() == QDir(repoPath).absolutePath()) {
        setRepoDetailNotice("That's the main checkout — it can't be removed here.",
                            true);
        return;
    }

    QList<int> agentIds;
    QSet<int> issueNumbers;
    for (const AgentSession &session : std::as_const(m_agentSessions)) {
        if (session.owner != repo.owner || session.name != repo.name ||
            session.branchName != branch || isExternalSession(session.id))
            continue;
        agentIds.append(session.id);
        if (session.issueNumber > 0)
            issueNumbers.insert(session.issueNumber);
    }
    if (agentIds.isEmpty())
        return;

    // The visible/session-store deletion is the foreground part. Retaining the
    // worktree here avoids racing deleteStoredAgentSession's detached teardown
    // against the explicit worktree + branch cleanup queued below.
    QElapsedTimer storeTimer;
    storeTimer.start();
    for (const int id : std::as_const(agentIds)) {
        if (!deleteStoredAgentSession(id, /*cleanupWorktree=*/false)) {
            logSystem(QStringLiteral("Agents: delete of session #%1 (%2) stopped "
                                     "before cleanup started.")
                          .arg(id)
                          .arg(branch));
            reloadAgents();
            return;
        }
    }
    logSystem(QStringLiteral("Agents: dropped %1 stored session%2 on %3 in %4 ms; "
                             "worktree and branch cleanup runs in the background.")
                  .arg(agentIds.size())
                  .arg(agentIds.size() == 1 ? QString() : QStringLiteral("s"))
                  .arg(branch)
                  .arg(storeTimer.elapsed()));
    flashMessage(
        agentIds.size() == 1
            ? QStringLiteral("Agent session deleted. Cleanup is running in the background.")
            : QStringLiteral("%1 agent sessions deleted. Cleanup is running in the background.")
                  .arg(agentIds.size()));
    // The reload trio re-shells git per session (merge state) and re-reads the
    // issue store, which is the slowest thing left on this path. Run it on the
    // next tick so the message above and the queued background ticket land first
    // (adhoc #417) instead of after several seconds of git.
    QTimer::singleShot(0, this, [this] {
        reloadAgents();
        reloadIssues();
        refreshIssueList();
        updateIssueActionState();
    });

    const quint64 taskId = beginBackgroundTask(
        QStringLiteral("cleanup"),
        QStringLiteral("Deleting %1 worktree and branch").arg(branch));
    const QString base = repoDefaultBranch(repoBranches());
    const bool deleteBranch = !branch.isEmpty() && branch != base;
    auto completed = std::make_shared<bool>(false);
    auto cleanupTimer = std::make_shared<QElapsedTimer>();
    cleanupTimer->start();
    logSystem(QStringLiteral("Agents: cleaning up worktree %1 and branch %2 in the "
                             "background.")
                  .arg(worktreePath.isEmpty() ? QStringLiteral("(none)") : worktreePath)
                  .arg(branch));

    auto finish = [this, repo, issueNumbers, branch, taskId, completed,
                   cleanupTimer](bool success, const QString &error) {
        if (*completed)
            return;
        *completed = true;
        int closedIssues = 0;
        if (success && !issueNumbers.isEmpty()) {
            const RepositoryRecord &writable = writableRecordFor(repo);
            IssueStore store(writable.localPath, writable.mirrorPath,
                             &m_profileIdentity, m_userName);
            if (store.canWrite()) {
                QHash<int, QString> statusByNumber;
                for (const Issue &issue : store.loadAll())
                    statusByNumber.insert(issue.number, issue.status);
                for (const int number : issueNumbers) {
                    if (!statusByNumber.contains(number) ||
                        statusByNumber.value(number) == QLatin1String("closed"))
                        continue;
                    QString issueError;
                    if (store.setStatus(number, QStringLiteral("closed"),
                                        &issueError)) {
                        ++closedIssues;
                        logSystem(
                            QStringLiteral(
                                "Closed issue #%1 (agent worktree deleted).")
                                .arg(number));
                    } else {
                        logSystem(
                            QStringLiteral(
                                "Issue #%1: could not close on delete: %2")
                                .arg(number)
                                .arg(issueError));
                    }
                }
            }
        }
        loadWorktreesPanel();
        if (m_branchesTable)
            loadBranchesPanel();
        reloadIssues();
        refreshIssueList();
        updateIssueActionState();
        updateRepoIssueCount();
        // On the happy path the elapsed time rides along in the log line; a
        // failure keeps the bare reason so the error toast stays readable.
        const QString detail =
            success
                ? QStringLiteral("Deleted worktree and branch %1%2 (%3 ms).")
                      .arg(branch,
                           closedIssues > 0
                               ? QStringLiteral("; closed %1 linked issue%2")
                                     .arg(closedIssues)
                                     .arg(closedIssues == 1 ? QString()
                                                           : QStringLiteral("s"))
                               : QString())
                      .arg(cleanupTimer->elapsed())
                : (error.trimmed().isEmpty()
                       ? QStringLiteral("Could not finish deleting %1.").arg(branch)
                       : error.trimmed());
        finishBackgroundTask(taskId, success, detail);
    };

    auto deleteBranchNext =
        std::make_shared<std::function<void()>>();
    *deleteBranchNext = [this, repoPath, branch, deleteBranch, finish] {
        if (!deleteBranch || !localBranchExists(repoPath, branch)) {
            finish(true, QString());
            return;
        }
        auto *git = new QProcess(this);
        auto handled = std::make_shared<bool>(false);
        connect(git, &QProcess::errorOccurred, this,
                [git, branch, finish, handled](QProcess::ProcessError error) {
                    if (error != QProcess::FailedToStart || *handled)
                        return;
                    *handled = true;
                    git->deleteLater();
                    finish(false,
                           QStringLiteral("Could not start branch cleanup for %1.")
                               .arg(branch));
                });
        connect(git, &QProcess::finished, this,
                [git, branch, finish, handled](int code,
                                                QProcess::ExitStatus status) {
                    if (*handled)
                        return;
                    *handled = true;
                    const QString error =
                        QString::fromUtf8(git->readAllStandardError()).trimmed();
                    git->deleteLater();
                    if (status == QProcess::NormalExit && code == 0)
                        finish(true, QString());
                    else
                        finish(false,
                               error.isEmpty()
                                   ? QStringLiteral(
                                         "Could not delete branch %1.")
                                         .arg(branch)
                                   : error);
                });
        git->start(QStringLiteral("git"),
                   {QStringLiteral("-C"), repoPath, QStringLiteral("branch"),
                    QStringLiteral("-D"), branch});
    };

    if (worktreePath.isEmpty() || !QDir(worktreePath).exists()) {
        QTimer::singleShot(0, this, [deleteBranchNext] {
            (*deleteBranchNext)();
        });
        return;
    }

    auto *git = new QProcess(this);
    auto handled = std::make_shared<bool>(false);
    connect(git, &QProcess::errorOccurred, this,
            [git, worktreePath, finish,
             handled](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || *handled)
                    return;
                *handled = true;
                git->deleteLater();
                finish(false,
                       QStringLiteral("Could not start worktree cleanup for %1.")
                           .arg(worktreePath));
            });
    connect(git, &QProcess::finished, this,
            [git, worktreePath, deleteBranchNext, finish,
             handled](int code, QProcess::ExitStatus status) {
                if (*handled)
                    return;
                *handled = true;
                const QString error =
                    QString::fromUtf8(git->readAllStandardError()).trimmed();
                git->deleteLater();
                if (status == QProcess::NormalExit && code == 0) {
                    (*deleteBranchNext)();
                    return;
                }
                finish(false,
                       error.isEmpty()
                           ? QStringLiteral("Could not remove worktree %1.")
                                 .arg(worktreePath)
                           : error);
            });
    git->start(
        QStringLiteral("git"),
        {QStringLiteral("-C"), repoPath, QStringLiteral("worktree"),
         QStringLiteral("remove"), QStringLiteral("--force"), worktreePath});
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
    m_branchesTable->verticalHeader()->setDefaultSectionSize(30);
    m_branchesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_branchesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_branchesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_branchesTable->setShowGrid(false);
    m_branchesTable->setWordWrap(false);
    m_branchesTable->setTextElideMode(Qt::ElideNone);
    m_branchesTable->setItemDelegateForColumn(
        0, new BranchOverviewDelegate(m_branchesTable));
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
    // Updated and Worktree now live as compact glyphs/metadata inside Branch,
    // matching the Agents list. Keep their model cells populated (automation and
    // accessibility still read them) but remove the duplicate visual columns.
    m_branchesTable->setColumnHidden(2, true);
    m_branchesTable->setColumnHidden(3, true);
    // Selecting a real branch (click or arrow keys) retires the "merged &
    // deleted" check left where a branch used to be (adhoc #15). The diff
    // preview that used to ride this selection moved into the Git view's range
    // pane (adhoc #107) — see the cellClicked navigation below.
    connect(m_branchesTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                QTableWidgetItem *it = m_branchesTable->item(row, 0);
                if (it && !it->text().isEmpty())
                    clearMergedBranchFlash();
            });
    // Clicking the Issue / Agent cell jumps to the agent run working that branch
    // (adhoc #258). Clicking any other (non-action) cell opens the branch's
    // commits, changed files and diff in the Git view — the branch diff viewer
    // lives there now (adhoc #107).
    connect(m_branchesTable, &QTableWidget::cellClicked, this,
            [this](int row, int column) {
                if (column == 4) {
                    QTableWidgetItem *it = m_branchesTable->item(row, 4);
                    if (!it)
                        return;
                    const QVariant sid = it->data(Qt::UserRole);
                    if (sid.isValid())
                        switchToAgentsTab(sid.toInt());
                    return;
                }
                if (column >= 4)
                    return; // action column: its cell widgets own their clicks
                QTableWidgetItem *it = m_branchesTable->item(row, 0);
                if (it && !it->text().isEmpty())
                    switchToBranch(it->text());
            });

    layout->addWidget(m_branchesTable, 1);
    return page;
}

// The branch/PR range review pane, hosted as the Git view's third workspace page
// (adhoc #107): the branch detail viewer that used to sit inside the Branches
// panel, moved beside the working-tree and commit pages and upgraded with the PR
// viewer's diff tools (find bar, prev/next change, split toggle, Pac-Man sticky
// header, auto-mark-viewed on scroll). switchToBranch()/openPullDiffInGitView()
// land here. It swaps in for the working-tree diff on the right pane only
// (adhoc #12) — the separate scope / changed-files columns it used to bring
// along are gone, so the rest of the git tab stays put.
QWidget *MainWindow::buildBranchRangePane()
{
    m_branchDiffView = new QTextBrowser;
    m_branchDiffView->setObjectName("diffView");
    m_branchDiffView->setOpenExternalLinks(false);
    m_branchDiffView->setOpenLinks(false); // we handle "viewed:" anchors ourselves
    connect(m_branchDiffView, &QTextBrowser::anchorClicked, this,
            &MainWindow::onBranchDiffAnchorClicked);
    registerDiffView(m_branchDiffView);
    // Sticky header overlay pinned over the diff viewport — same form as the PR
    // viewer's: filename + Pac-Man read-progress + percent + a Viewed toggle.
    m_branchDiffSticky = new QFrame(m_branchDiffView->viewport());
    m_branchDiffSticky->setObjectName("diffStickyHeader");
    {
        const bool dark = qApp->palette().color(QPalette::Base).lightness() < 128;
        m_branchDiffSticky->setStyleSheet(
            QStringLiteral(
                "#diffStickyHeader{background:%1;border-bottom:1px solid %2;}"
                "#diffStickyHeader QLabel{background:transparent;}"
                "#diffStickyHeader QPushButton{background:transparent;border:none;"
                "color:%3;font-size:11px;padding:2px 4px;}"
                "#diffStickyHeader QPushButton:hover{color:#3fb950;}")
                .arg(dark ? "#161b22" : "#f6f8fa", dark ? "#30363d" : "#d0d7de",
                     dark ? "#8b949e" : "#57606a"));
        auto *sl = new QHBoxLayout(m_branchDiffSticky);
        sl->setContentsMargins(10, 4, 8, 4);
        sl->setSpacing(8);
        m_branchStickyPath = new QLabel(m_branchDiffSticky);
        m_branchStickyPath->setTextFormat(Qt::RichText);
        m_branchStickyPath->setTextInteractionFlags(Qt::NoTextInteraction);
        sl->addWidget(m_branchStickyPath, 1);
        m_branchStickyPacman = new PacmanProgress(m_branchDiffSticky);
        m_branchStickyPacman->setToolTip(
            QStringLiteral("How much of this file you've scrolled through"));
        sl->addWidget(m_branchStickyPacman, 0);
        m_branchStickyPercent = new QLabel(QStringLiteral("0% read"),
                                           m_branchDiffSticky);
        m_branchStickyPercent->setObjectName("hintLabel");
        m_branchStickyPercent->setMinimumWidth(52);
        m_branchStickyPercent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sl->addWidget(m_branchStickyPercent, 0);
        m_branchStickyViewed = new QPushButton(m_branchDiffSticky);
        m_branchStickyViewed->setCursor(Qt::PointingHandCursor);
        m_branchStickyViewed->setToolTip(QStringLiteral("Mark this file as viewed"));
        connect(m_branchStickyViewed, &QPushButton::clicked, this, [this] {
            if (m_branchStickyFile.isEmpty())
                return;
            onBranchDiffAnchorClicked(QUrl(
                QStringLiteral("viewed:") +
                QString::fromLatin1(QUrl::toPercentEncoding(m_branchStickyFile))));
        });
        sl->addWidget(m_branchStickyViewed, 0);
        m_branchDiffSticky->hide();
    }
    // Debounce the auto-mark-viewed sweep off scroll ticks, exactly as the PR
    // viewer does: the re-render that collapses newly-viewed files is too heavy
    // to run on every pixel of a fast scroll.
    m_branchAutoViewedDebounce = new QTimer(this);
    m_branchAutoViewedDebounce->setSingleShot(true);
    m_branchAutoViewedDebounce->setInterval(400);
    connect(m_branchAutoViewedDebounce, &QTimer::timeout, this,
            &MainWindow::applyBranchAutoMarkViewedOnScroll);
    connect(m_branchDiffView->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] {
                // Cheap, every-tick: sticky header / Pac-Man / list follow.
                updateBranchDiffSticky();
                // Heavy, debounced: collapse fully-seen files into "Viewed".
                if (m_branchAutoViewedDebounce)
                    m_branchAutoViewedDebounce->start();
            });

    // Diff tools (brought over from the PR viewer): text-size zoom, unified <->
    // side-by-side, and prev/next change. They sit on the pane's top bar beside
    // the branch actions (adhoc #110).
    auto *diffZoomOut = new QPushButton(QString::fromUtf8("\xE2\x88\x92")); // −
    diffZoomOut->setToolTip("Smaller diff text");
    connect(diffZoomOut, &QPushButton::clicked, this, [this] { adjustDiffFont(-1); });
    auto *diffZoomIn = new QPushButton(QStringLiteral("+"));
    diffZoomIn->setToolTip("Larger diff text");
    connect(diffZoomIn, &QPushButton::clicked, this, [this] { adjustDiffFont(1); });
    m_branchSplitButton = new QPushButton;
    m_branchSplitButton->setCheckable(true);
    m_branchSplitButton->setChecked(diffSplitPref());
    setOcticon(m_branchSplitButton, "diff", 14);
    updateDiffSplitButton(m_branchSplitButton);
    connect(m_branchSplitButton, &QPushButton::clicked, this, [this](bool on) {
        setDiffSplitPref(on);
        updateDiffSplitButton(m_branchSplitButton);
        // Keep the commit and PR toggles (which share the preference) in step.
        for (QPushButton *b : {m_commitSplitButton, m_pullSplitButton}) {
            if (b) {
                b->setChecked(on);
                updateDiffSplitButton(b);
            }
        }
        if (m_branchDiffLastValid)
            renderBranchDiffPatch(QString::fromUtf8(m_branchDiffLastPatch),
                                  m_branchDiffLastEmpty, m_branchDiffViewedContext);
        else
            renderBranchScopeDiff();
    });
    auto *prevChange = new QPushButton;
    prevChange->setToolTip("Previous change");
    setOcticon(prevChange, "chevron-up", 14);
    connect(prevChange, &QPushButton::clicked, this,
            [this] { branchScrollToAdjacentHunk(-1); });
    auto *nextChange = new QPushButton;
    nextChange->setToolTip("Next change");
    setOcticon(nextChange, "chevron-down", 14);
    connect(nextChange, &QPushButton::clicked, this,
            [this] { branchScrollToAdjacentHunk(1); });
    for (QPushButton *b :
         {diffZoomOut, diffZoomIn, m_branchSplitButton, prevChange, nextChange}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    // Detail pane: a toolbar with the primary branch actions over its diff. These
    // act on whichever branch is selected (m_branchDiffBranch), so the common
    // actions are reachable at the top while reviewing the changes rather than
    // hunting for the matching row button (issue #116). Their enabled/tooltip
    // state is set in updateBranchDetailActions() as the selection changes.
    m_branchDetailLabel = new QLabel;
    m_branchDetailLabel->setObjectName("sectionLabel");
    m_branchDetailLabel->setTextFormat(Qt::RichText);

    // Leaving the comparison: right pane back to the working-tree diff, graph
    // back on the default branch, compare base reset (adhoc #16).
    m_branchCloseButton = new QPushButton;
    m_branchCloseButton->setObjectName("ghostButton");
    m_branchCloseButton->setProperty("buttonSize", "sm");
    m_branchCloseButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchCloseButton, "x", 14);
    m_branchCloseButton->setToolTip(
        QStringLiteral("Close this comparison and go back to the working tree"));
    connect(m_branchCloseButton, &QPushButton::clicked, this,
            [this] { closeBranchCompareView(); });

    // "PR #N": shown while the pane reviews a pull request (adhoc #107) — jumps
    // to the full PR page (conversation, checks, merge controls).
    m_branchOpenPullButton = new QPushButton;
    m_branchOpenPullButton->setObjectName("ghostButton");
    m_branchOpenPullButton->setProperty("buttonSize", "sm");
    m_branchOpenPullButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchOpenPullButton, "git-pull-request", 14);
    m_branchOpenPullButton->hide();
    connect(m_branchOpenPullButton, &QPushButton::clicked, this, [this] {
        if (m_branchDiffPullNumber >= 0)
            switchToPullTab(m_branchDiffPullNumber);
    });

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
    m_branchFixAgentCombo->addItem(QStringLiteral("CC"),
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
    applyLiveClaudeModelsToCombos();
    connect(m_branchFixAgentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (m_branchFixAgentCombo && m_branchFixModelCombo) {
                    fillAgentFixModelCombo(
                        m_branchFixModelCombo,
                        m_branchFixAgentCombo->currentData().toString());
                    applyLiveClaudeModelsToCombos();
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
        if (!m_branchDiffBranch.isEmpty()
            && mergeWorktreeIntoMain(m_branchDiffBranch, QString()))
            closeBranchDiffAfterMerge();
    });

    // Same merge, but nothing of the branch survives it: its worktree, the branch
    // itself and any agent session that produced it all go once the work is in the
    // base branch (adhoc #428). The one-click end of a finished agent run, without
    // hopping to the Agents/Worktrees tabs to clean up by hand. Runs straight from
    // the click — mergeWorktreeIntoMain no longer confirms (adhoc #441).
    m_branchMergeDeleteButton = new QPushButton("Merge & delete all");
    m_branchMergeDeleteButton->setObjectName("primaryButton");
    m_branchMergeDeleteButton->setProperty("buttonSize", "sm");
    m_branchMergeDeleteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchMergeDeleteButton, "check-circle", 14);
    m_branchMergeDeleteButton->setEnabled(false);
    connect(m_branchMergeDeleteButton, &QPushButton::clicked, this, [this] {
        if (m_branchDiffBranch.isEmpty())
            return;
        const QString repoPath = repoGitDir();
        // An empty path is fine — it just means the branch has no worktree of its
        // own, so there's nothing to prune beyond the branch and its agent.
        if (mergeWorktreeIntoMain(m_branchDiffBranch,
                                  worktreePathForBranch(repoPath, m_branchDiffBranch),
                                  /*deleteAgent=*/true))
            closeBranchDiffAfterMerge();
    });

    auto *detailBar = new QHBoxLayout;
    detailBar->setContentsMargins(0, 0, 0, 0);
    detailBar->addWidget(m_branchCloseButton);
    detailBar->addWidget(m_branchDetailLabel);
    detailBar->addStretch();
    // Diff tools moved up here from the old scope column's header (adhoc #110):
    // one top bar carries everything that acts on what's being reviewed.
    detailBar->addWidget(diffZoomOut);
    detailBar->addWidget(diffZoomIn);
    detailBar->addWidget(m_branchSplitButton);
    detailBar->addWidget(prevChange);
    detailBar->addWidget(nextChange);
    detailBar->addSpacing(12);
    detailBar->addWidget(m_branchOpenPullButton);
    detailBar->addWidget(m_branchOpenCodiumButton);
    detailBar->addWidget(m_branchMergeEditorButton);
    detailBar->addWidget(m_branchPullButton);
    detailBar->addWidget(m_branchFixButton);
    detailBar->addWidget(m_branchFixAgentCombo);
    detailBar->addWidget(m_branchFixModelCombo);
    detailBar->addWidget(m_branchPrButton);
    detailBar->addWidget(m_branchMergeButton);
    detailBar->addWidget(m_branchMergeDeleteButton);

    // ---- Find bar (mirrors the PR viewer's, issue #333): Ctrl+F over the pane
    // highlights every occurrence in the combined diff and steps between matches.
    m_branchDiffSearchInput = new QLineEdit;
    m_branchDiffSearchInput->setObjectName("issueSearch");
    m_branchDiffSearchInput->setPlaceholderText("Find in diff\xE2\x80\xA6");
    m_branchDiffSearchInput->setClearButtonEnabled(true);
    connect(m_branchDiffSearchInput, &QLineEdit::textChanged, this,
            [this] { branchDiffSearchRecompute(); });
    connect(m_branchDiffSearchInput, &QLineEdit::returnPressed, this, [this] {
        branchDiffSearchGoTo(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier
                                 ? -1
                                 : 1);
    });
    m_branchDiffSearchCount = new QLabel;
    m_branchDiffSearchCount->setObjectName("hintLabel");
    auto *searchPrev = new QPushButton;
    searchPrev->setToolTip("Previous match");
    setOcticon(searchPrev, "chevron-up", 14);
    connect(searchPrev, &QPushButton::clicked, this,
            [this] { branchDiffSearchGoTo(-1); });
    auto *searchNext = new QPushButton;
    searchNext->setToolTip("Next match");
    setOcticon(searchNext, "chevron-down", 14);
    connect(searchNext, &QPushButton::clicked, this,
            [this] { branchDiffSearchGoTo(1); });
    auto *searchClose = new QPushButton;
    searchClose->setToolTip("Close find bar");
    setOcticon(searchClose, "x", 14);
    connect(searchClose, &QPushButton::clicked, this,
            [this] { toggleBranchDiffSearch(false); });
    for (QPushButton *b : {searchPrev, searchNext, searchClose}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    m_branchDiffSearchBar = new QWidget;
    auto *searchBarLayout = new QHBoxLayout(m_branchDiffSearchBar);
    searchBarLayout->setContentsMargins(0, 0, 0, 6);
    searchBarLayout->addWidget(m_branchDiffSearchInput, 1);
    searchBarLayout->addWidget(m_branchDiffSearchCount);
    searchBarLayout->addWidget(searchPrev);
    searchBarLayout->addWidget(searchNext);
    searchBarLayout->addWidget(searchClose);
    m_branchDiffSearchBar->setVisible(false);

    // The pane itself is just the toolbar over the diff — the git tab's own
    // CHANGES tree and commit graph stay on screen beside it (adhoc #12), so
    // the diff gets the full width here.
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    // Match the sibling workspace pages (working-tree changes / commit detail).
    layout->setContentsMargins(16, 12, 16, 16);
    layout->setSpacing(8);
    layout->addLayout(detailBar);
    layout->addWidget(m_branchDiffSearchBar);
    layout->addWidget(m_branchDiffView, 1);

    // Ctrl+F / Escape scoped to this pane only (WidgetWithChildren): a
    // window-wide Find here would collide with the PR viewer's find bar.
    auto *findShortcut = new QShortcut(QKeySequence::Find, page);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this,
            [this] { toggleBranchDiffSearch(true); });
    auto *closeSearchShortcut =
        new QShortcut(QKeySequence(Qt::Key_Escape), m_branchDiffSearchInput);
    closeSearchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeSearchShortcut, &QShortcut::activated, this,
            [this] { toggleBranchDiffSearch(false); });
    return page;
}

// Gather everything the branches table needs from git. Runs on a worker thread
// (see loadBranchesPanel), so it must touch nothing but its by-value argument:
// every read below used to run on the GUI thread, which made clicking into the
// panel — or following an agent's branch link — wait on six git processes
// (adhoc #420).
MainWindow::BranchesPanelData
MainWindow::readBranchesPanelGit(BranchesPanelData data)
{
    const QString &dir = data.dir;
    if (dir.isEmpty())
        return data;

    // The local heads and the base they're measured against. `git branch
    // --sort=-committerdate` reads every branch tip and can take hundreds of ms
    // on a repo with many agent branches, which is exactly the wait this whole
    // panel used to impose on a click.
    data.branches = listRepoBranches(dir);
    data.base = chooseDefaultBranch(data.branches, data.configuredDefault, dir,
                                    data.checkedOut);
    const QString &base = data.base;
    // Always pin the default branch ("main") to the top of the list, regardless
    // of which feature branch was committed to most recently — the listing sorts
    // by committer date, so without this main sinks below active branches
    // (adhoc #185).
    if (!base.isEmpty() && data.branches.removeOne(base))
        data.branches.prepend(base);
    data.selected = data.checkedOut.isEmpty() ? base : data.checkedOut;

    // Remote-tracking branches (refs/remotes/*): the branches other nodes / the
    // relay have published, which `git branch` (local heads only, via
    // repoBranches()) leaves out. The list should show every branch in the repo,
    // including these refs, under their full ref-qualified name (adhoc #55).
    // Rendered read-only after the local branches. Skip each remote's symbolic
    // */HEAD pointer and the bare remote name (e.g. "origin"), which aren't
    // branches; a remote-tracking ref is always "<remote>/<branch>".
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
            if (!data.remoteBranches.contains(ref))
                data.remoteBranches.append(ref);
        }
    }

    // Branch commit timestamps in one batch: spawning a `git log -1` per branch
    // blocked the UI thread for ~2s on repos with many branches because each row
    // started its own git process serially (issue #152). for-each-ref returns
    // every branch tip's committer date in a single call.
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
            data.times.insert(ref, parts.at(1).toLongLong());
            if (parts.size() >= 3)
                data.shortShas.insert(ref, parts.at(2));
            if (parts.size() >= 4)
                data.subjects.insert(ref, parts.at(3));
            if (parts.size() >= 5)
                data.authors.insert(ref, parts.at(4));
        }
    }

    // Ahead/behind of each remote-tracking branch vs the default branch, so those
    // rows can show how far they've diverged from main just like the local ones do
    // (adhoc #61). One batched `for-each-ref` (git 2.41+ '%(ahead-behind:<base>)')
    // rather than a rev-list per branch keeps it cheap even when a repo carries
    // hundreds of remote refs. The atom emits "<ahead> <behind>"; the hash stays
    // empty (rows fall back to a plain "Remote" label) when the field or base is
    // unavailable, e.g. on older git.
    if (!base.isEmpty() && !data.remoteBranches.isEmpty()) {
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
                    data.remoteAheadBehind.insert(
                        parts.first(),
                        qMakePair(parts.at(1).toInt(), parts.at(2).toInt()));
            }
        }
    }

    // The same batched read for the local heads. Each row used to shell its own
    // `git rev-list --left-right --count`, so a repo with dozens of branches ran
    // dozens of git processes serially every time the panel rebuilt (adhoc #416).
    // One for-each-ref answers them all. The atom needs git 2.41+; where it's
    // missing the per-branch rev-list below fills the gaps, so older git still
    // reports the right counts.
    if (!base.isEmpty()) {
        QByteArray ab;
        if (runGitCapture(
                dir,
                {"for-each-ref",
                 QStringLiteral("--format=%(refname:short) %(ahead-behind:%1)").arg(base),
                 "refs/heads/"},
                &ab, nullptr)) {
            for (const QString &line :
                 QString::fromUtf8(ab).split('\n', Qt::SkipEmptyParts)) {
                const QStringList parts = line.split(
                    QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                if (parts.size() >= 3)
                    data.localAheadBehind.insert(
                        parts.first(),
                        qMakePair(parts.at(1).toInt(), parts.at(2).toInt()));
            }
        }
        for (const QString &branch : std::as_const(data.branches)) {
            if (branch == base || data.localAheadBehind.contains(branch))
                continue;
            QByteArray counts;
            if (!runGitCapture(dir,
                               {"rev-list", "--left-right", "--count",
                                base + "..." + branch},
                               &counts, nullptr))
                continue;
            const QStringList parts = QString::fromUtf8(counts).trimmed().split(
                QRegularExpression(QStringLiteral("\\s+")));
            // "<behind> <ahead>" — the left side is base's own commits.
            if (parts.size() >= 2)
                data.localAheadBehind.insert(
                    branch, qMakePair(parts.at(1).toInt(), parts.at(0).toInt()));
        }
    }

    // Map each branch to the worktree (other than the main checkout) it's checked
    // out in, in a single `git worktree list --porcelain` call so the per-row
    // Worktree column doesn't spawn a git process each (issue #172).
    QByteArray wtOut;
    if (runGitCapture(dir, {"worktree", "list", "--porcelain"}, &wtOut, nullptr)) {
        // Compare canonical paths so a symlinked checkout root (e.g. /tmp on
        // some platforms) doesn't make the main worktree look like a separate
        // branch worktree; fall back to the absolute path if it can't resolve.
        QString mainPath = QFileInfo(dir).canonicalFilePath();
        if (mainPath.isEmpty())
            mainPath = QDir(dir).absolutePath();
        QString currentPath;
        for (const QString &raw : QString::fromUtf8(wtOut).split(QLatin1Char('\n'))) {
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
                if (!br.isEmpty() && !currentPath.isEmpty() && canonicalPath != mainPath)
                    data.worktrees.insert(br, currentPath);
            }
        }
    }
    return data;
}

// Refresh the branches panel. The git reads run on a worker thread and the rows
// are built when they land, so this returns immediately and the window stays
// responsive (adhoc #420 — it used to shell six-plus git processes on the GUI
// thread under a GitKeepAlive, which is what made clicking a branch link feel
// slow). Reloads that arrive while a read is in flight are coalesced into one.
void MainWindow::loadBranchesPanel()
{
    if (!m_branchesTable)
        return;
    if (m_branchesPanelLoading) {
        m_branchesPanelReloadQueued = true;
        return;
    }
    BranchesPanelData data;
    data.dir = repoGitDir();
    data.configuredDefault = m_repoInfo.defaultBranch.trimmed();
    data.checkedOut = m_repoBranch;
    data.writable = repoHasWorkingTree();
    // Remember which branch's diff is on screen so we can re-render it at the end
    // (now reflecting any merge we just performed).
    data.previouslyViewed = m_branchDiffBranch;

    m_branchesPanelLoading = true;
    const int gen = ++m_branchesPanelGen;
    runOffThread<BranchesPanelData>(
        [data] { return readBranchesPanelGit(data); },
        [this, gen](BranchesPanelData loaded) {
            if (gen != m_branchesPanelGen)
                return; // a newer load already owns the table
            m_branchesPanelLoading = false;
            if (m_branchesTable)
                renderBranchesPanel(loaded);
            if (m_branchesPanelReloadQueued) {
                m_branchesPanelReloadQueued = false;
                loadBranchesPanel();
            } else if (m_branchesTable) {
                startBranchChangeStats(loaded);
            }
        });
}

void MainWindow::startBranchChangeStats(const BranchesPanelData &data)
{
    if (data.dir.isEmpty() || data.base.isEmpty() || data.branches.isEmpty()) {
        m_branchChangeStatsLoading = false;
        return;
    }

    const int gen = ++m_branchChangeStatsGen;
    m_branchChangeStatsLoading = true;
    const QString dir = data.dir;
    const QString base = data.base;
    const QStringList branches = data.branches;
    const QHash<QString, QString> shas = data.shortShas;
    const QHash<QString, QPair<int, int>> aheadBehind = data.localAheadBehind;
    const QHash<QString, BranchChangeStat> cache = m_branchChangeStatsCache;

    runOffThread<QHash<QString, BranchChangeStat>>(
        [dir, base, branches, shas, aheadBehind, cache] {
            QHash<QString, BranchChangeStat> result;
            for (const QString &branch : branches) {
                const QString key = branchConflictKey(
                    dir, shas.value(base), shas.value(branch));
                const auto cached = cache.constFind(key);
                if (!key.isEmpty() && cached != cache.constEnd()) {
                    result.insert(branch, *cached);
                    continue;
                }

                BranchChangeStat stat;
                const auto counts = aheadBehind.constFind(branch);
                // A branch with no commits of its own has an empty merge-base
                // range by definition; avoid spawning a diff process for it.
                if (branch == base ||
                    (counts != aheadBehind.constEnd() && counts->first == 0)) {
                    stat.files = stat.added = stat.removed = 0;
                    result.insert(branch, stat);
                    continue;
                }

                QByteArray numstat;
                if (runGitCapture(dir,
                                  {QStringLiteral("diff"), QStringLiteral("--numstat"),
                                   base + QStringLiteral("...") + branch},
                                  &numstat, nullptr)) {
                    stat.files = stat.added = stat.removed = 0;
                    for (const QByteArray &line : numstat.split('\n')) {
                        if (line.trimmed().isEmpty())
                            continue;
                        const QList<QByteArray> fields = line.split('\t');
                        if (fields.size() < 3)
                            continue;
                        ++stat.files;
                        bool addOk = false;
                        bool removeOk = false;
                        const int add = fields.at(0).toInt(&addOk);
                        const int remove = fields.at(1).toInt(&removeOk);
                        if (addOk)
                            stat.added += add;
                        if (removeOk)
                            stat.removed += remove;
                    }
                }
                result.insert(branch, stat);
            }
            return result;
        },
        [this, gen, data](QHash<QString, BranchChangeStat> stats) {
            if (gen != m_branchChangeStatsGen)
                return;
            m_branchChangeStatsLoading = false;
            if (m_branchChangeStatsCache.size() > 4096)
                m_branchChangeStatsCache.clear();
            for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
                const QString key = branchConflictKey(
                    data.dir, data.shortShas.value(data.base),
                    data.shortShas.value(it.key()));
                if (!key.isEmpty())
                    m_branchChangeStatsCache.insert(key, it.value());
            }

            if (!m_branchesTable || repoGitDir() != data.dir)
                return;
            for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
                QTableWidgetItem *name = m_branchesTable->item(row, 0);
                if (!name)
                    continue;
                const auto stat = stats.constFind(name->text());
                if (stat == stats.constEnd())
                    continue;
                name->setData(kBranchFilesRole, stat->files);
                name->setData(kBranchAddedRole, stat->added);
                name->setData(kBranchRemovedRole, stat->removed);
            }
            m_branchesTable->viewport()->update();
        });
}

void MainWindow::renderBranchesPanel(const BranchesPanelData &data)
{
    // Freeze the table while we tear down and rebuild the rows so a refresh
    // after a merge/delete doesn't flash it blank before the new contents land;
    // it repaints once when the guard lifts (adhoc #256). (The changed-files /
    // scope / diff panes live in the Git view's range pane now — adhoc #107.)
    TableRepaintGuard repaintGuard(m_branchesTable);
    // Block the table's selection signals across the rebuild so clearing the
    // rows doesn't churn the selection handlers mid-rebuild. The Git view's
    // range pane is refreshed explicitly at the end instead (adhoc #256/#107).
    QSignalBlocker branchesTableBlock(m_branchesTable);
    // Allocate the model in one change. insertRow() emitted rowsInserted for
    // every branch, and QTableView responded to each signal with a full editor/
    // geometry pass even while painting was disabled (a recurring 1.3s stall in
    // the watchdog log).
    m_branchesTable->setRowCount(data.branches.size() +
                                 data.remoteBranches.size());
    // setRowCount() only destroys the cell widgets of rows it trims, so the
    // "Merged & deleted" widget the previous render put in column 0 survives into
    // a surviving row and paints on top of that row's branch name — and every
    // rebuild inside the flash window stacked another one (adhoc #56). Column 0 is
    // the only column whose widget is conditional; the action column gets a fresh
    // widget on every row below.
    for (int r = 0, rows = m_branchesTable->rowCount(); r < rows; ++r) {
        if (m_branchesTable->cellWidget(r, 0))
            m_branchesTable->removeCellWidget(r, 0);
    }
    int nextRow = 0;
    const QString &dir = data.dir;
    const QStringList &branches = data.branches;
    const QString &base = data.base;
    const QString &selected = data.selected;
    const QString &previouslyViewed = data.previouslyViewed;
    const bool writable = data.writable;
    const QStringList &remoteBranches = data.remoteBranches;
    const QHash<QString, qint64> &branchTimes = data.times;
    const QHash<QString, QString> &branchShortShas = data.shortShas;
    const QHash<QString, QString> &branchSubjects = data.subjects;
    const QHash<QString, QString> &branchAuthors = data.authors;
    const QHash<QString, QPair<int, int>> &localAheadBehind = data.localAheadBehind;
    const QHash<QString, QPair<int, int>> &remoteAheadBehind = data.remoteAheadBehind;
    const QHash<QString, QString> &branchWorktrees = data.worktrees;
    // Rows on screen now describe this repo, so switchToBranch can trust them.
    m_branchesPanelDir = dir;

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

    // Map each branch to the agent session working it (if any), scoped to the
    // current repo, so the per-row "Issue / Agent" column can name the issue the
    // branch is attached to (or flag an ad-hoc agent run) (adhoc #191). A branch
    // may carry more than one session over its life; prefer one bound to an issue
    // and otherwise the most recent.
    //
    // Stored by value, not by pointer into m_agentSessions: a queued callback
    // (e.g. an agent finishing/being deleted) can append/remove entries and
    // reallocate that list between rebuilds. A pointer taken here would dangle
    // and crash (free(): invalid pointer) when later dereferenced — this is what
    // crashed on a branch click after a merge freed its agent session (adhoc
    // #200).
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

    // The action column is Fixed-width because ResizeToContents can't see
    // its cell widgets; size it to the widest action row we build below.
    int actionWidth = 0;
    bool anyBehind = false;
    bool anyMerged = false; // fully-merged branches the "Delete merged" action can remove
    // Branches whose merge verdict isn't memoised yet, as (branch, cache key).
    // Probed on a worker thread once the table is built (adhoc #416).
    QList<QPair<QString, QString>> conflictProbes;
    for (const QString &branch : branches) {
        const int row = nextRow++;

        auto *name = new QTableWidgetItem(branch);
        const auto sessionIt = branchSessions.constFind(branch);
        if (sessionIt != branchSessions.constEnd())
            name->setIcon(agentStatusOcticon(sessionIt.value()));
        // No generic branch/check icon: this page already establishes that every
        // row is a branch. The leading icon is reserved for useful agent state.
        name->setData(kBranchUpdatedRole,
                      formatShortRelativeTime(branchTimes.value(branch, 0)));
        {
            BranchChangeStat stat;
            bool known = false;
            if (branch == base) {
                stat.files = stat.added = stat.removed = 0;
                known = true;
            } else {
                const QString key = branchConflictKey(
                    dir, branchShortShas.value(base), branchShortShas.value(branch));
                const auto cached = m_branchChangeStatsCache.constFind(key);
                if (!key.isEmpty() && cached != m_branchChangeStatsCache.constEnd()) {
                    stat = *cached;
                    known = true;
                }
            }
            if (known) {
                name->setData(kBranchFilesRole, stat.files);
                name->setData(kBranchAddedRole, stat.added);
                name->setData(kBranchRemovedRole, stat.removed);
            }
        }
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
                // Counts come from the worker's batched read (with its per-branch
                // rev-list fallback for pre-2.41 git); a missing entry means the
                // divergence is unknown, so the row shows no status at all.
                const auto abIt = localAheadBehind.constFind(branch);
                const bool counted = abIt != localAheadBehind.constEnd();
                if (counted) {
                    ahead = abIt->first;
                    behind = abIt->second;
                }
                if (counted)
                    status = QString::fromUtf8("%1 behind \xC2\xB7 %2 ahead")
                                 .arg(behind)
                                 .arg(ahead);
                // Only a branch with its own commits *and* base commits it lacks
                // can conflict; probe that case with an in-memory merge so the row
                // can flag it and offer "Fix with agent". The probe is the slow
                // part of this loop, so it only runs inline when a previous sweep
                // already answered it for these exact two tips; otherwise the row
                // renders now and a worker thread paints the flag in when it lands
                // (adhoc #416).
                if (behind > 0 && ahead > 0) {
                    const QString key = branchConflictKey(
                        dir, branchShortShas.value(base), branchShortShas.value(branch));
                    const auto cached = m_branchConflictCache.constFind(key);
                    if (!key.isEmpty() && cached != m_branchConflictCache.constEnd())
                        hasConflict = *cached;
                    else if (!key.isEmpty())
                        conflictProbes.append(qMakePair(branch, key));
                }
                if (hasConflict)
                    status += kBranchConflictsSuffix;
            }
            ts = branchTimes.value(branch, 0);
        }
        auto *statusItem = new QTableWidgetItem(status);
        if (hasConflict) {
            statusItem->setForeground(QColor("#f85149"));
        }
        name->setData(kBranchConflictRole, hasConflict);
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
        name->setData(kBranchWorktreeRole, !worktreePath.isEmpty());
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
        if (sessionIt != branchSessions.constEnd()) {
            const AgentSession *session = &sessionIt.value();
            const QString statusWord =
                session->merged ? QStringLiteral("merged")
                                : agentStatusText(session->status);
            QString detail;
            if (session->issueNumber > 0) {
                // Show issue number + status word so you can tell at a glance
                // whether the agent is still running or has finished.
                attach->setText(QStringLiteral("#%1").arg(session->issueNumber));
                detail = session->issueTitle.isEmpty()
                             ? QStringLiteral("Issue #%1").arg(session->issueNumber)
                             : QStringLiteral("Issue #%1: %2")
                                   .arg(session->issueNumber)
                                   .arg(session->issueTitle);
            } else {
                attach->setText(QStringLiteral("Agent"));
                detail = session->prompt;
            }
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
    // in the repo, not just the local heads (adhoc #55). The only row action is a
    // delete that pushes the removal to the remote (adhoc #196); the Status column
    // shows their ahead/behind vs the default branch from the batched probe above
    // so the divergence info
    // matches the local rows (adhoc #61). Clicking one still renders its diff vs
    // the default branch.
    for (const QString &branch : remoteBranches) {
        const int row = nextRow++;

        auto *name = new QTableWidgetItem(branch);
        name->setForeground(QColor("#8b949e"));
        name->setData(kBranchUpdatedRole,
                      formatShortRelativeTime(branchTimes.value(branch, 0)));
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

        // Row action: delete the branch on its remote (git push <remote> --delete).
        // A remote-tracking ref is always "<remote>/<branch>"; the remote's own
        // default branch is skipped since it can't be deleted. Unlike the local
        // rows this doesn't need a working tree — it's a network push to the origin.
        const QString remoteName = branch.section('/', 0, 0);
        const QString remoteRef = branch.section('/', 1);
        const bool canDeleteRemote =
            !remoteName.isEmpty() && !remoteRef.isEmpty() && remoteRef != base;
        auto *ractions = new QWidget;
        ractions->setObjectName("branchActions");
        ractions->setStyleSheet("#branchActions { background: transparent; }");
        auto *ractionRow = new QHBoxLayout(ractions);
        ractionRow->setContentsMargins(0, 0, 8, 0);
        ractionRow->setSpacing(4);
        auto *rdel = new QPushButton;
        rdel->setObjectName("issueIconButton");
        rdel->setFlat(true);
        rdel->setCursor(Qt::PointingHandCursor);
        rdel->setIcon(themedOcticon("trash", QColor("#f85149"), 15));
        rdel->setIconSize(QSize(15, 15));
        rdel->setEnabled(canDeleteRemote);
        rdel->setToolTip(canDeleteRemote
                             ? QStringLiteral("Delete branch %1 on %2")
                                   .arg(remoteRef, remoteName)
                             : QStringLiteral("Can't delete the remote's default "
                                              "branch"));
        connect(rdel, &QPushButton::clicked, this,
                [this, branch] { deleteRemoteBranch(branch); });
        ractionRow->addWidget(rdel);
        m_branchesTable->setCellWidget(row, 5, ractions);
        ractions->ensurePolished();
        for (QPushButton *b : ractions->findChildren<QPushButton *>()) {
            b->ensurePolished();
            b->setMinimumWidth(b->sizeHint().width());
        }
        ractionRow->invalidate();
        actionWidth = qMax(actionWidth, ractions->sizeHint().width());
    }
    if (actionWidth > 0)
        m_branchesTable->horizontalHeader()->resizeSection(5, actionWidth + 8);

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

    // Kick off the merge-conflict probes the rows couldn't answer from the memo.
    // Started before the diff pane is re-rendered below so updateBranchDetailActions
    // sees the selected branch's probe as in flight and skips its own inline
    // merge-tree — that one call is what stalled the GUI for ~1s after a delete
    // (adhoc #416).
    startBranchConflictProbes(dir, base, conflictProbes);

    // A branch switchToBranch asked for before the rows existed: land on it now
    // that they do, and tell the user if it isn't in this repository after all
    // (adhoc #185/#420).
    const QString pendingSelect = m_branchesPanelPendingSelect;
    m_branchesPanelPendingSelect.clear();

    if (m_branchesTable->rowCount() == 0) {
        m_branchesTable->insertRow(0);
        auto *empty = new QTableWidgetItem("No branches in this repository.");
        empty->setForeground(QColor("#8b949e"));
        m_branchesTable->setItem(0, 0, empty);
        // Table signals are blocked, so blank the diff pane ourselves.
        showBranchDiff(QString());
        if (!pendingSelect.isEmpty())
            reportBranchNotFound(pendingSelect);
        return;
    }

    // adhoc #15: a branch the user just merged & deleted keeps its place in the
    // list as an animated check, so the row it held reads as "done" instead of the
    // next branch sliding under the cursor with its diff already loading. Skipped
    // once the rows belong to another repo, and a pending switchToBranch always
    // wins — that's a branch the user explicitly clicked.
    if (!m_branchMergedFlashBranch.isEmpty() && m_branchMergedFlashRow >= 0 &&
        pendingSelect.isEmpty() && m_branchMergedFlashDir == dir &&
        !branches.contains(m_branchMergedFlashBranch)) {
        const int row = qMin(m_branchMergedFlashRow, m_branchesTable->rowCount());
        m_branchesTable->insertRow(row);
        auto *cell = new QWidget;
        cell->setObjectName("branchMergedFlash");
        cell->setStyleSheet("#branchMergedFlash { background: transparent; }");
        auto *cellRow = new QHBoxLayout(cell);
        cellRow->setContentsMargins(4, 0, 8, 0);
        cellRow->setSpacing(8);
        cellRow->addWidget(new DoneCheckMark(cell));
        auto *label = new QLabel(
            QStringLiteral("Merged & deleted %1").arg(m_branchMergedFlashBranch));
        label->setStyleSheet("color: #8b949e; background: transparent;");
        cellRow->addWidget(label);
        cellRow->addStretch();
        // An empty name item keeps the row out of neighbourBranchInList and out of
        // the "select this branch" scan below — it names no branch.
        m_branchesTable->setItem(row, 0, new QTableWidgetItem);
        m_branchesTable->setCellWidget(row, 0, cell);
        for (int c = 1; c < m_branchesTable->columnCount(); ++c)
            m_branchesTable->setItem(row, c, new QTableWidgetItem);
        m_branchesTable->setCurrentCell(row, 0);
        // Table signals are blocked across the rebuild, so blank the diff pane
        // ourselves rather than leaving the deleted branch's diff on screen.
        showBranchDiff(QString());
        if (m_branchDiffView)
            setDiffHtml(m_branchDiffView,
                        QStringLiteral("<p style='color:#8b949e'>Merged %1 into %2 "
                                       "and deleted it. Pick a branch to see its "
                                       "changes.</p>")
                            .arg(m_branchMergedFlashBranch.toHtmlEscaped(),
                                 base.toHtmlEscaped()));
        return;
    }

    // Re-select the row the user was viewing (falling back to the checked-out
    // branch) so rebuilding the table doesn't leave the diff pane blank. A
    // pending switchToBranch wins: it's the branch the user just clicked.
    QString target = previouslyViewed;
    if (!pendingSelect.isEmpty() &&
        (branches.contains(pendingSelect) || remoteBranches.contains(pendingSelect)))
        target = pendingSelect;
    else if (!pendingSelect.isEmpty())
        reportBranchNotFound(pendingSelect);
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
    // Refresh the Git view's range pane only when it is showing this branch
    // (adhoc #107): a background rebuild of the list must not hijack the pane
    // onto whatever branch the table happens to select (e.g. the checked-out
    // fallback after a not-found report).
    if (!target.isEmpty() && target == m_branchDiffBranch)
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

// Delete a remote-tracking branch on its origin: "git push <remote> --delete
// <ref>". The list shows remote refs as "<remote>/<branch>"; split off the first
// path component as the remote and push a deletion of the rest. This is a network
// push, so it runs detached (runGitDetached) rather than blocking the UI, and on
// success we prune the now-stale remote-tracking ref before reloading the panel.
void MainWindow::deleteRemoteBranch(const QString &branch)
{
    const QString remote = branch.section('/', 0, 0);
    const QString ref = branch.section('/', 1);
    if (remote.isEmpty() || ref.isEmpty())
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    if (QMessageBox::question(
            this, "Delete remote branch",
            QStringLiteral("Delete branch \"%1\" on the remote \"%2\"? This "
                           "removes it for everyone and cannot be undone.")
                .arg(ref, remote),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const QString nextSelection = neighbourBranchInList(branch);
    setRepoDetailNotice(
        QStringLiteral("Deleting %1 on %2\xE2\x80\xA6").arg(ref, remote));
    runGitDetached(
        dir, {"push", remote, "--delete", ref},
        [this, branch, remote, ref, dir, nextSelection](bool ok,
                                                         const QByteArray &out) {
            if (!ok) {
                const QString detail = QString::fromUtf8(out).trimmed();
                setRepoDetailNotice(
                    detail.isEmpty()
                        ? QStringLiteral("Could not delete %1 on %2.")
                              .arg(ref, remote)
                        : QStringLiteral("Could not delete %1 on %2: %3")
                              .arg(ref, remote, detail.left(200)),
                    true);
                return;
            }
            // Drop the now-stale remote-tracking ref so the row disappears without
            // waiting for the next fetch/prune.
            runGitCapture(dir, {"branch", "-dr", branch}, nullptr, nullptr);
            logSystem(
                QStringLiteral("Git: deleted branch %1 on remote %2.")
                    .arg(ref, remote));
            setRepoDetailNotice(
                QStringLiteral("Deleted %1 on %2.").arg(ref, remote));
            m_branchesCache.clear();
            m_branchDiffBranch = nextSelection;
            loadBranchesAndTags();
        });
}

// The branch sitting next to `branch` in the Branches table — the row just below
// it (which slides up when it's deleted) or, if it was the last row, the row just
// above. Empty when there's no other branch listed, which makes loadBranchesPanel
// fall back to the default branch. Used to pick the post-delete selection so
// removing a branch doesn't jump the list back to the checked-out branch (#256).
QString MainWindow::neighbourBranchInList(const QString &branch) const
{
    const int row = branchRowInList(branch);
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

int MainWindow::branchRowInList(const QString &branch) const
{
    if (!m_branchesTable || branch.isEmpty())
        return -1;
    for (int r = 0; r < m_branchesTable->rowCount(); ++r) {
        QTableWidgetItem *it = m_branchesTable->item(r, 0);
        if (it && it->text() == branch)
            return r;
    }
    return -1;
}

// "Merge & delete all" ends with the branch gone, so there's nothing left to
// re-select. Sliding the selection onto the next branch (adhoc #139) drops the
// user into an unrelated diff they never asked for, so instead we hold the spot:
// the row the branch occupied comes back as an animated check and the diff pane
// says what happened (adhoc #15). Nothing here touches git — it only steers the
// rebuild loadBranchesAndTags() is about to run.
void MainWindow::flashMergedBranchRow(const QString &branch)
{
    const int row = branchRowInList(branch);
    if (branch.isEmpty() || row < 0)
        return;
    m_branchMergedFlashBranch = branch;
    m_branchMergedFlashDir = m_branchesPanelDir;
    m_branchMergedFlashRow = row;
    // Nothing is selected any more; an empty value would otherwise make the
    // rebuild fall back to the checked-out branch.
    m_branchDiffBranch.clear();
    QTimer::singleShot(kBranchMergedFlashMs, this, [this, branch] {
        // Only retire our own check: a later merge may have replaced it.
        if (m_branchMergedFlashBranch == branch)
            clearMergedBranchFlash();
    });
}

// Drop the check. Deliberately doesn't repaint the panel: the row disappears at
// the next natural rebuild, so retiring it never moves anything under the cursor.
void MainWindow::clearMergedBranchFlash()
{
    m_branchMergedFlashBranch.clear();
    m_branchMergedFlashDir.clear();
    m_branchMergedFlashRow = -1;
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
    // repoDefaultBranchFast() rather than repoDefaultBranch(repoBranches()): this
    // runs on every branch click, and the sorted `git branch` listing behind
    // repoBranches() is exactly the kind of wait a click shouldn't pay.
    const QString base = repoDefaultBranchFast();
    const bool isBase = branch.isEmpty() || branch == base;
    if (dir.isEmpty() || isBase) {
        applyBranchDetailActions(branch, base, 0, 0, false);
        return;
    }

    // The ahead/behind count and the branch/base tips behind the conflict memo's
    // key are git reads, and a cold selection used to add an inline merge-tree
    // (~0.5-1s) on top — all of it on the GUI thread, so clicking a branch froze
    // the window before its detail bar appeared (adhoc #420). Read them on a
    // worker and paint the bar when they land; the merge-tree itself stays in
    // startBranchConflictProbes, which calls back here once it has a verdict.
    const int gen = ++m_branchDetailActionsGen;
    // Paint what's already known (the branch name, the actions that don't depend
    // on the divergence) while the counts are read.
    applyBranchDetailActions(branch, base, -1, -1, false);
    struct BranchDetailStats {
        int behind = 0;
        int ahead = 0;
        QString conflictKey;
    };
    runOffThread<BranchDetailStats>(
        [dir, base, branch] {
            BranchDetailStats s;
            QByteArray counts;
            if (runGitCapture(dir,
                              {"rev-list", "--left-right", "--count",
                               base + "..." + branch},
                              &counts, nullptr)) {
                const QStringList parts = QString::fromUtf8(counts).trimmed().split(
                    QRegularExpression(QStringLiteral("\\s+")));
                if (parts.size() >= 2) {
                    s.behind = parts.at(0).toInt();
                    s.ahead = parts.at(1).toInt();
                }
            }
            // Only a branch with its own commits and base commits it lacks can
            // conflict; the memo key names that exact commit pair.
            if (s.behind > 0 && s.ahead > 0) {
                QByteArray shas;
                if (runGitCapture(dir, {"rev-parse", "--short", base, branch}, &shas,
                                  nullptr)) {
                    const QStringList tips =
                        QString::fromUtf8(shas).split('\n', Qt::SkipEmptyParts);
                    if (tips.size() >= 2)
                        s.conflictKey = branchConflictKey(dir, tips.at(0).trimmed(),
                                                          tips.at(1).trimmed());
                }
            }
            return s;
        },
        [this, gen, branch, base, dir](BranchDetailStats s) {
            // Dropped once a newer selection owns the detail bar.
            if (gen != m_branchDetailActionsGen || m_branchDiffBranch != branch)
                return;
            bool hasConflict = false;
            if (!s.conflictKey.isEmpty()) {
                const auto cached = m_branchConflictCache.constFind(s.conflictKey);
                if (cached != m_branchConflictCache.constEnd())
                    hasConflict = *cached;
                else
                    // Leaves the flag off until the verdict lands (adhoc #416).
                    startBranchConflictProbes(dir, base, {{branch, s.conflictKey}});
            }
            applyBranchDetailActions(branch, base, s.behind, s.ahead, hasConflict);
        });
}

// Paint the branch detail bar from counts the caller has already gathered.
void MainWindow::applyBranchDetailActions(const QString &branch, const QString &base,
                                          int behind, int ahead, bool hasConflict)
{
    if (!m_branchMergeButton)
        return;
    const QString dir = repoGitDir();
    const bool writable = repoHasWorkingTree();
    const bool isBase = branch.isEmpty() || branch == base;
    // behind/ahead of -1 mean "not read yet": the actions that depend on the
    // divergence stay disabled until the worker's counts land, so a click in that
    // window can't act on the previous branch's state.
    const bool counted = behind >= 0 && ahead >= 0;

    if (m_branchDetailLabel) {
        // Name both ends of the comparison, not just the branch (adhoc #110):
        // the diff below is everything <branch> adds over <base>, and the merge
        // actions to the right act in that same direction.
        const QString pair =
            base.isEmpty() || branch == base
                ? QStringLiteral("<b>%1</b>").arg(branch.toHtmlEscaped())
                : QString::fromUtf8("<b>%1</b> \xE2\x86\x92 <b>%2</b>")
                      .arg(branch.toHtmlEscaped(), base.toHtmlEscaped());
        QString text;
        if (branch.isEmpty())
            text.clear();
        else if (branch == base)
            text = QString::fromUtf8("%1 \xC2\xB7 default branch").arg(pair);
        else if (!counted) {
            // Counts still being read off-thread (adhoc #420): name the branches
            // rather than claiming a divergence we don't know yet.
            text = QString::fromUtf8("%1 \xC2\xB7 checking\xE2\x80\xA6").arg(pair);
        } else {
            text = QString::fromUtf8("%1 \xC2\xB7 %2 behind \xC2\xB7 %3 ahead")
                       .arg(pair)
                       .arg(behind)
                       .arg(ahead);
            if (hasConflict)
                text += QString::fromUtf8(
                    " \xC2\xB7 <span style='color:#f85149'>conflicts</span>");
        }
        m_branchDetailLabel->setToolTip(
            branch.isEmpty() || base.isEmpty() || branch == base
                ? QString()
                : QStringLiteral("Showing %1's complete checkout compared with %2")
                      .arg(branch, base));
        // Reviewing a pull request (adhoc #107): lead with its number and state
        // so the pane reads as that PR's changes, not just a branch.
        if (m_branchDiffPullNumber >= 0 && !text.isEmpty()) {
            QString state;
            for (const PullRequest &p : std::as_const(m_currentPulls)) {
                if (p.number == m_branchDiffPullNumber) {
                    state = p.status;
                    break;
                }
            }
            const QString color = state == QLatin1String("merged")
                                      ? QStringLiteral("#a371f7")
                                      : state == QLatin1String("closed")
                                            ? QStringLiteral("#f85149")
                                            : QStringLiteral("#3fb950");
            text = QString::fromUtf8("<b>PR #%1</b>%2 \xC2\xB7 %3")
                       .arg(m_branchDiffPullNumber)
                       .arg(state.isEmpty()
                                ? QString()
                                : QStringLiteral(
                                      " <span style='color:%1'>%2</span>")
                                      .arg(color, state.toHtmlEscaped()))
                       .arg(text);
        }
        m_branchDetailLabel->setText(text);
    }

    // "PR #N" jump to the full pull request page: only while reviewing a PR.
    if (m_branchOpenPullButton) {
        const bool prMode = m_branchDiffPullNumber >= 0;
        m_branchOpenPullButton->setVisible(prMode);
        if (prMode) {
            m_branchOpenPullButton->setText(
                QStringLiteral("PR #%1").arg(m_branchDiffPullNumber));
            m_branchOpenPullButton->setToolTip(
                QStringLiteral("Open pull request #%1 \xE2\x80\x94 conversation, "
                               "checks and merge controls")
                    .arg(m_branchDiffPullNumber));
        }
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
    else if (!counted)
        m_branchPullButton->setToolTip(
            QStringLiteral("Checking how far %1 is behind %2\xE2\x80\xA6")
                .arg(branch, base));
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
        else if (!counted)
            m_branchMergeEditorButton->setToolTip(
                QStringLiteral("Checking how far %1 is behind %2\xE2\x80\xA6")
                    .arg(branch, base));
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

    // Merge, then delete everything the branch owned. Same availability as the plain
    // merge — the teardown only runs once the work is provably in base.
    if (m_branchMergeDeleteButton) {
        m_branchMergeDeleteButton->setEnabled(canMerge);
        m_branchMergeDeleteButton->setToolTip(
            canMerge
                ? QStringLiteral("Merge %1 into %2, then delete its agent session, "
                                 "branch and worktree")
                      .arg(branch, base)
                : (isBase ? QStringLiteral("Select a branch other than %1").arg(base)
                          : "Read-only mirror \xE2\x80\x94 nothing to merge into here"));
    }

    // The buttons now say whether the branch is behind and whether it conflicts,
    // which is exactly what auto-pull needs to decide.
    maybeAutoPullBranch(branch);
}

// Auto-pull: as soon as the branch's detail view is behind base with no conflict,
// try the same update "Pull main" would do by hand, spinning that button while it
// runs, so landing on a branch is enough to bring it current without an extra
// click. Skipped when there's a conflict (the "Fix with agent" / "Merge editor"
// buttons own that case) and attempted at most once per branch so a declined
// stash prompt can't nag on every incidental rebuild of this panel while the
// branch stays selected. Deferred a tick so it runs after the caller's own render
// rather than recursing into it (the pull re-renders itself via showBranchDiff()
// once it succeeds). Also held off while a background sweep is still deciding
// whether the branch conflicts: the Fix button is hidden until that verdict
// lands, and pulling a conflicting branch on the strength of a not-yet-known
// answer would surface a "couldn't update cleanly" notice the user never asked
// for (adhoc #416).
void MainWindow::maybeAutoPullBranch(const QString &branch)
{
    if (branch.isEmpty() || m_branchDiffBranch != branch)
        return;
    // Reviewing a PR must not rewrite its head branch as a side effect of
    // opening the diff; the PR page's own Update button owns that (adhoc #107).
    if (m_branchDiffPullNumber >= 0)
        return;
    if (!m_branchPullButton || !m_branchPullButton->isEnabled() ||
        (m_branchFixButton && m_branchFixButton->isVisible()) ||
        m_branchConflictProbes.contains(branch) ||
        m_branchAutoPullAttempted == branch)
        return;
    m_branchAutoPullAttempted = branch;
    startButtonSpin(m_branchPullButton);
    QTimer::singleShot(0, this, [this, branch] {
        QPushButton *const spinButton = m_branchPullButton;
        const auto spinGuard =
            qScopeGuard([this, spinButton] { stopButtonSpin(spinButton); });
        if (m_branchDiffBranch != branch)
            return;
        GitKeepAlive keepAlive;
        updateBranchFromBase(branch);
    });
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
    updateCommitsCompareIndicator(); // "<branch> -> <base>" on the branch row
    // Fills in the detail bar (and, once it knows the branch is behind and
    // conflict-free, kicks off the auto-pull) from a worker thread — see
    // updateBranchDetailActions / maybeAutoPullBranch.
    updateBranchDetailActions(branch);

    m_branchDiffFileSpans.clear();
    m_branchFileTops.clear();
    m_branchStickyFile.clear();
    if (m_branchDiffSticky)
        m_branchDiffSticky->hide(); // no spans yet; reappears on scroll
    m_branchDiffViewedContext.clear();
    m_branchDiffWorkDir = branchWorkDir(branch);
    // A new branch's diff hasn't been fetched yet; drop the cached patch so the
    // "Viewed" toggle can't re-render a stale one before the async read lands.
    m_branchDiffLastValid = false;
    const QString dir = repoGitDir();
    if (branch.isEmpty() || dir.isEmpty()) {
        m_branchDiffWorkDir.clear();
        m_branchDiffView->clear();
        return;
    }
    // The range is against the compare base, which the base dropdown can move
    // off the default branch (adhoc #16).
    const QString base = branchCompareBase();
    if (branch == base) {
        setDiffHtml(m_branchDiffView,
            QStringLiteral("<p style='color:#8b949e'>%1 is the branch being "
                           "compared against.</p>")
                .arg(branch.toHtmlEscaped()));
        return;
    }

    // Render the whole-branch diff (async — renderBranchScopeDiff reads git on
    // a worker thread and drops the result if the branch changes meanwhile).
    renderBranchScopeDiff();
}

void MainWindow::renderBranchScopeDiff()
{
    if (!m_branchDiffView)
        return;
    const QString branch = m_branchDiffBranch;
    const QString dir = repoGitDir();
    if (branch.isEmpty() || dir.isEmpty())
        return;
    const QString base = branchCompareBase();
    const QString work = m_branchDiffWorkDir;

    // The diff read below can take seconds on a large branch — it was a
    // recurring StallWatchdog offender freezing the GUI thread (issue #353). Run
    // it off-thread and render the patch on the GUI thread once ready, dropping
    // the result if the user has since switched branch.
    // In PR mode the range shares the PR viewer's per-file Viewed state
    // ("pull/<N>"), so a file checked off in either place stays checked in both
    // (adhoc #107).
    const int pullNumber = m_branchDiffPullNumber;
    const int gen = ++m_branchScopeDiffGen;
    struct ScopeDiff {
        bool ok = true;
        QByteArray out;
        QString err;
        QString emptyMessage;
        QString viewedContext;
    };
    runOffThread<ScopeDiff>(
        [dir, base, branch, work, pullNumber]() {
            ScopeDiff r;
            r.viewedContext = pullNumber >= 0
                                  ? QStringLiteral("pull/") +
                                        QString::number(pullNumber)
                                  : QStringLiteral("branch/") + branch;
            r.emptyMessage =
                QStringLiteral("No changes between %1 and %2.").arg(branch, base);
            // A checked-out branch is represented by its complete worktree
            // snapshot, not just its committed tip. The temporary-index helper
            // folds committed, staged, unstaged, deleted, and untracked files
            // into one patch against the selected base without modifying the
            // real index. A branch with no checkout falls back to its ref range.
            if (!work.isEmpty()) {
                if (!buildWorkingTreeDiff(work, base, &r.out, &r.err))
                    r.ok = false;
            } else if (!runGitCapture(dir, {"diff", base + ".." + branch},
                                      &r.out, &r.err)) {
                r.ok = false;
            }
            return r;
        },
        [this, gen, branch](ScopeDiff r) {
            // Dropped if the branch changed while the read was in flight.
            if (gen != m_branchScopeDiffGen || !m_branchDiffView ||
                m_branchDiffBranch != branch)
                return;
            if (!r.ok) {
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
    const QString dir = m_branchDiffWorkDir.isEmpty() ? repoGitDir()
                                                       : m_branchDiffWorkDir;
    const QString base = branchCompareBase();
    QList<DiffFileEntry> files;
    const QSet<QString> viewed = loadDiffViewed(viewedContext);
    // PR mode (adhoc #107): drop the PR's review threads beneath the lines they
    // annotate and turn on the clickable comment gutters, exactly as the PR
    // viewer's Files-changed page does (onBranchDiffAnchorClicked forwards the
    // comment/thread anchors to the PR handlers).
    QHash<QString, QString> notes;
    QString anchorFile;
    if (m_branchDiffPullNumber >= 0) {
        for (const PullRequest &p : std::as_const(m_currentPulls)) {
            if (p.number == m_branchDiffPullNumber) {
                notes = buildPullLineNotes(p);
                anchorFile = QStringLiteral("*");
                break;
            }
        }
    }
    const QString html = renderDiffHtml(patch, files, dir, base, m_branchDiffBranch,
                                        anchorFile, notes, viewed);
    m_branchDiffFilePaths.clear();
    m_branchDiffFileAnchors.clear();
    QStringList rangeStatuses;
    for (const DiffFileEntry &f : files) {
        m_branchDiffFilePaths.append(f.path);
        m_branchDiffFileAnchors.append(f.anchor);
        rangeStatuses.append(f.status);
    }
    showRangeFilesInSourceControl(m_branchDiffFilePaths, rangeStatuses);

    // Handing an enormous diff to QTextEdit::setHtml() in one go parses, styles
    // and lays it all out on the GUI thread at once, freezing the window for
    // seconds (issue #187). setDiffHtml renders progressively for exactly that
    // reason: the visible window now, the rest a batch at a time off the event
    // loop (adhoc #51/#421).
    setDiffHtml(m_branchDiffView,
                html.isEmpty()
                    ? QStringLiteral("<p style='color:#8b949e'>%1</p>")
                          .arg(emptyMessage.toHtmlEscaped())
                    : html);

    // Map each file header to its document position for the sticky bar. While the
    // diff is still streaming in only the first blocks are in the document now;
    // the stream-finished hook (onDiffStreamFinished) rebuilds the full map once
    // the last batch lands. Either way this covers whatever is currently shown.
    m_branchFileTops.clear(); // positions change on re-render; force a recompute
    m_branchStickyFile.clear();
    rebuildBranchDiffSpans();
    // The document was just replaced; an open find bar's cursors died with it.
    if (m_branchDiffSearchBar && m_branchDiffSearchBar->isVisible())
        branchDiffSearchRecompute();
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

    // Agent branches are normally checked out in their own linked worktree.
    // Updating their ref from the main checkout is rejected by Git (and the old
    // path either displayed that refusal or tried to check out an already-live
    // branch). Merge the base in the checkout that actually owns the branch.
    // Local edits remain in place when Git can merge safely; if they overlap the
    // base update, Git refuses and we leave them untouched.
    const QString linkedWorktree = worktreePathForBranch(dir, branch);
    if (!linkedWorktree.isEmpty() &&
        QDir(linkedWorktree).absolutePath() != QDir(dir).absolutePath()) {
        QByteArray existingMerge;
        if (runGitCapture(linkedWorktree,
                          {"rev-parse", "-q", "--verify", "MERGE_HEAD"},
                          &existingMerge, nullptr) && !existingMerge.trimmed().isEmpty()) {
            setRepoDetailNotice(
                QStringLiteral("%1 already has a merge in progress in its worktree; "
                               "finish or abort it before updating from %2.")
                    .arg(branch, base),
                true);
            return;
        }

        QString worktreeError;
        if (!runGitCapture(linkedWorktree, {"merge", "--no-edit", base}, nullptr,
                           &worktreeError)) {
            // Abort only a merge this call actually started. A refusal caused by
            // overlapping uncommitted files has no MERGE_HEAD and needs no cleanup.
            QByteArray startedMerge;
            if (runGitCapture(linkedWorktree,
                              {"rev-parse", "-q", "--verify", "MERGE_HEAD"},
                              &startedMerge, nullptr) &&
                !startedMerge.trimmed().isEmpty())
                runGitCapture(linkedWorktree, {"merge", "--abort"}, nullptr,
                              nullptr);
            setRepoDetailNotice(
                QStringLiteral("Couldn't update %1 from %2 in its worktree: %3. "
                               "Its local changes were left untouched.")
                    .arg(branch, base,
                         worktreeError.trimmed().isEmpty()
                             ? QStringLiteral("the merge was refused")
                             : worktreeError.trimmed().left(240)),
                true);
            return;
        }

        logSystem(QStringLiteral("Git: merged %1 into %2 in its linked worktree.")
                      .arg(base, branch));
        setRepoDetailNotice(
            QStringLiteral("Updated %1 with %2 in its worktree.").arg(branch, base));
        m_branchesCache.clear();
        if (m_branchDiffBranch == branch)
            showBranchDiff(branch); // refreshed range + universal CHANGES list
        loadWorktreesPanel();
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
        // Third way out: the user may just want to look at (or commit) those
        // files first. Send them to the commits view, whose working-changes
        // panel lists exactly these paths, instead of making them find it.
        QPushButton *reviewBtn = box.addButton(QStringLiteral("Review changes"),
                                               QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(stashBtn);
        box.exec();
        if (box.clickedButton() == reviewBtn) {
            setRepoDetailNotice(
                QStringLiteral("Left %1 unchanged — review its %2 uncommitted file%3 "
                               "below, then update from %4.")
                    .arg(branch, n, plural, base));
            showOverviewCommits(); // the universal Git workspace
            loadCommits();         // refresh history + the working-changes panel
            return;
        }
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
        // Just refresh the counts: the browsed ref stays on the default branch,
        // so there is nothing to re-apply after a merge (adhoc #80).
        loadBranchesAndTags();
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
    loadBranchesAndTags();
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

// Answer the branches panel's outstanding merge-conflict probes on a worker
// thread (adhoc #416). `merge-tree` costs ~0.5-1s per branch on a busy repo, so
// running one per row while building the table froze the GUI for seconds every
// time the panel rebuilt — which it does after every delete, merge and pull. The
// rows go up immediately without the flag instead and this paints it in as each
// verdict lands. `branchMergeTree` is pure git reads over value-copied strings,
// so it is safe off the GUI thread (runGitCapture only pumps the event loop when
// it is on it).
void MainWindow::startBranchConflictProbes(
    const QString &dir, const QString &base,
    const QList<QPair<QString, QString>> &probes)
{
    if (dir.isEmpty() || base.isEmpty() || probes.isEmpty())
        return;
    // Skip branches an earlier sweep is still working on: a rebuild landing
    // mid-flight would otherwise re-run the same merge for the same commit pair.
    QList<QPair<QString, QString>> pending;
    for (const QPair<QString, QString> &probe : probes) {
        if (probe.second.isEmpty() || m_branchConflictProbes.contains(probe.first))
            continue;
        m_branchConflictProbes.insert(probe.first);
        pending.append(probe);
    }
    if (pending.isEmpty())
        return;

    auto verdicts = std::make_shared<QList<bool>>(); // parallel to `pending`
    QThread *worker = QThread::create([dir, base, pending, verdicts] {
        for (const QPair<QString, QString> &probe : pending)
            verdicts->append(branchMergeTree(dir, base, probe.first).isEmpty());
    });
    connect(worker, &QThread::finished, this,
            [this, worker, pending, verdicts, dir] {
                worker->deleteLater();
                for (const QPair<QString, QString> &probe : pending)
                    m_branchConflictProbes.remove(probe.first);
                // The memo grows a fresh key every time a branch or the base gains
                // a commit; it is a pure speed-up, so dropping the lot once it gets
                // large is always safe.
                if (m_branchConflictCache.size() > 512)
                    m_branchConflictCache.clear();
                const int answered = qMin(pending.size(), verdicts->size());
                for (int i = 0; i < answered; ++i)
                    m_branchConflictCache.insert(pending.at(i).second,
                                                 verdicts->at(i));
                // Paint the conflicting rows, as long as the panel still shows the
                // repo we probed. Rows are matched by branch name rather than by
                // the index they had when the sweep started, so a rebuild in
                // between can't flag the wrong one.
                if (!m_branchesTable || repoGitDir() != dir)
                    return;
                bool refreshDetail = false;
                for (int i = 0; i < answered; ++i) {
                    const QString branch = pending.at(i).first;
                    if (branch == m_branchDiffBranch)
                        refreshDetail = true;
                    if (!verdicts->at(i))
                        continue;
                    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
                        QTableWidgetItem *name = m_branchesTable->item(row, 0);
                        QTableWidgetItem *status = m_branchesTable->item(row, 1);
                        if (!name || !status || name->text() != branch)
                            continue;
                        if (!status->text().endsWith(kBranchConflictsSuffix)) {
                            status->setText(status->text() + kBranchConflictsSuffix);
                            status->setForeground(QColor("#f85149"));
                            name->setData(kBranchConflictRole, true);
                        }
                        break;
                    }
                }
                // The detail pane rendered before its verdict was known, so its
                // label and "Fix with agent" button need the answer too. It reads
                // the memo we just filled, so this doesn't re-shell merge-tree.
                if (refreshDetail && !m_branchDiffBranch.isEmpty())
                    updateBranchDetailActions(m_branchDiffBranch);
            });
    worker->start();
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
    loadBranchesAndTags();
    loadBranchesPanel();

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
    // In PR mode the comment gutters and review-thread actions are live: hand
    // them to the PR handlers (they act on m_currentPullNumber), then re-render
    // this pane so the new/changed thread shows here too (adhoc #107).
    const QString href = url.toString(QUrl::FullyDecoded);
    if (m_branchDiffPullNumber >= 0 &&
        (href.startsWith(QLatin1String("thread:")) ||
         url.scheme() == QLatin1String("cmt") ||
         url.scheme() == QLatin1String("filecomment"))) {
        // The PR tab may have moved on to another PR since this pane opened;
        // re-anchor it so the comment lands on the PR being reviewed here.
        if (m_currentPullNumber != m_branchDiffPullNumber)
            showPull(m_branchDiffPullNumber);
        const int scroll =
            m_branchDiffView ? m_branchDiffView->verticalScrollBar()->value() : 0;
        onPullDiffAnchorClicked(url); // dialogs pump the event loop; members only
        if (m_branchDiffLastValid)
            renderBranchDiffPatch(QString::fromUtf8(m_branchDiffLastPatch),
                                  m_branchDiffLastEmpty, m_branchDiffViewedContext);
        else
            renderBranchScopeDiff();
        if (m_branchDiffView)
            m_branchDiffView->verticalScrollBar()->setValue(scroll);
        return;
    }
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

// Absolute document y-position of each file header (aligned to
// m_branchDiffFileSpans), collected in one pass and cached until the next
// re-render — the per-scroll-tick sticky/progress update just reads the cache
// (mirrors computePullFileTops, adhoc #107).
void MainWindow::computeBranchFileTops()
{
    m_branchFileTops.assign(m_branchDiffFileSpans.size(), -1);
    if (!m_branchDiffView || m_branchDiffFileSpans.isEmpty())
        return;
    QScrollBar *vbar = m_branchDiffView->verticalScrollBar();
    const int viewTop = vbar ? vbar->value() : 0;
    QTextDocument *doc = m_branchDiffView->document();
    for (int i = 0; i < m_branchDiffFileSpans.size(); ++i) {
        QTextCursor cur(doc);
        cur.setPosition(
            qMin(m_branchDiffFileSpans.at(i).first, doc->characterCount() - 1));
        m_branchFileTops[i] = m_branchDiffView->cursorRect(cur).top() + viewTop;
    }
}

// Runs on every scroll tick of the branch/PR diff (cheap; no re-render): mirrors
// the current file's header into the sticky bar, advances the Pac-Man chart by
// how much of the file has scrolled past, and selects the file in the list so it
// follows the scroll — the same behaviour as the PR viewer's sticky (adhoc #107).
void MainWindow::updateBranchDiffSticky()
{
    if (!m_branchDiffView || !m_branchDiffSticky)
        return;
    QScrollBar *vbar = m_branchDiffView->verticalScrollBar();
    if (!vbar)
        return;
    if (m_branchDiffFileSpans.isEmpty()) {
        m_branchDiffSticky->hide();
        return;
    }
    const int viewTop = vbar->value();
    const int viewBottom = viewTop + m_branchDiffView->viewport()->height();
    const int docHeight =
        m_branchDiffView->document()->documentLayout()->documentSize().height();
    if (m_branchFileTops.size() != m_branchDiffFileSpans.size())
        computeBranchFileTops();

    // The file at the top of the viewport is the first whose section still
    // reaches below the top edge.
    int idx = -1, fileTop = 0, fileBottom = 0;
    for (int i = 0; i < m_branchDiffFileSpans.size(); ++i) {
        if (m_branchFileTops.at(i) < 0)
            continue;
        const int bottom =
            (i + 1 < m_branchFileTops.size() && m_branchFileTops.at(i + 1) >= 0)
                ? m_branchFileTops.at(i + 1)
                : docHeight;
        if (bottom > viewTop) {
            idx = i;
            fileTop = m_branchFileTops.at(i);
            fileBottom = bottom;
            break;
        }
    }
    if (idx < 0) {
        m_branchDiffSticky->hide();
        return;
    }
    const QString cur = m_branchDiffFileSpans.at(idx).second;

    // Fraction of the file's extent that has passed the viewport's bottom edge.
    double progress = 1.0;
    if (fileBottom > fileTop)
        progress = double(viewBottom - fileTop) / double(fileBottom - fileTop);
    progress = qBound(0.0, progress, 1.0);

    const QString viewedContext = m_branchDiffViewedContext.isEmpty()
                                      ? QStringLiteral("branch/") + m_branchDiffBranch
                                      : m_branchDiffViewedContext;
    const bool isViewed = loadDiffViewed(viewedContext).contains(cur);
    if (cur != m_branchStickyFile) {
        m_branchStickyFile = cur;
        if (m_branchStickyPath)
            m_branchStickyPath->setText(diffStickyPathHtml(cur));
    }
    if (m_branchStickyViewed)
        m_branchStickyViewed->setText(isViewed
                                          ? QString::fromUtf8("\xE2\x98\x91 Viewed")
                                          : QString::fromUtf8("\xE2\x98\x90 Viewed"));
    if (m_branchStickyPacman) {
        m_branchStickyPacman->setColor(progress >= 0.999 || isViewed
                                           ? QColor(0x3f, 0xb9, 0x50)
                                           : QColor(0x58, 0xa6, 0xff));
        m_branchStickyPacman->setProgress(isViewed ? 1.0 : progress);
    }
    if (m_branchStickyPercent) {
        const int percent = isViewed ? 100 : qBound(0, qRound(progress * 100.0), 100);
        m_branchStickyPercent->setText(QStringLiteral("%1% read").arg(percent));
    }

    m_branchDiffSticky->setGeometry(0, 0, m_branchDiffView->viewport()->width(),
                                    m_branchDiffSticky->sizeHint().height());
    // Do not cover the real per-file header while it is still visible.
    if (viewTop <= fileTop + m_branchDiffSticky->sizeHint().height()) {
        m_branchDiffSticky->hide();
        return;
    }
    m_branchDiffSticky->show();
    m_branchDiffSticky->raise();
}

// Debounced off the branch diff's scrollbar: mark every file scrolled fully
// through (its end reached the viewport bottom) as Viewed, re-render once for
// the batch and keep the current file on screen — the PR viewer's
// read-as-you-scroll behaviour, brought over with the pane (adhoc #107).
void MainWindow::applyBranchAutoMarkViewedOnScroll()
{
    if (!m_branchDiffView || m_branchDiffFileSpans.isEmpty() ||
        !m_branchDiffLastValid || !autoMarkViewedOnScrollPref())
        return;
    QScrollBar *vbar = m_branchDiffView->verticalScrollBar();
    if (!vbar)
        return;
    const int viewBottom = vbar->value() + m_branchDiffView->viewport()->height();
    const int docHeight =
        m_branchDiffView->document()->documentLayout()->documentSize().height();
    if (m_branchFileTops.size() != m_branchDiffFileSpans.size())
        computeBranchFileTops();

    const QString context = m_branchDiffViewedContext.isEmpty()
                                ? QStringLiteral("branch/") + m_branchDiffBranch
                                : m_branchDiffViewedContext;
    const QSet<QString> viewed = loadDiffViewed(context);
    QString currentFile; // first file the reviewer hasn't fully scrolled through
    QStringList newlyViewed;
    for (int i = 0; i < m_branchDiffFileSpans.size(); ++i) {
        if (m_branchFileTops.at(i) < 0)
            continue;
        const int bottom =
            (i + 1 < m_branchFileTops.size() && m_branchFileTops.at(i + 1) >= 0)
                ? m_branchFileTops.at(i + 1)
                : docHeight;
        const QString &path = m_branchDiffFileSpans.at(i).second;
        if (bottom <= viewBottom) {
            if (!viewed.contains(path))
                newlyViewed << path;
        } else if (currentFile.isEmpty()) {
            currentFile = path;
        }
    }
    if (newlyViewed.isEmpty())
        return;
    for (const QString &path : std::as_const(newlyViewed))
        setDiffViewed(context, path, true);
    renderBranchDiffPatch(QString::fromUtf8(m_branchDiffLastPatch),
                          m_branchDiffLastEmpty, context);
    if (!currentFile.isEmpty()) {
        // Collapsing the files above shifted the document; land back on the
        // file still being read (anchors are rebuilt by the render above).
        const int idx = m_branchDiffFilePaths.indexOf(currentFile);
        if (idx >= 0 && idx < m_branchDiffFileAnchors.size()) {
            flushDiffStream(m_branchDiffView);
            m_branchDiffView->scrollToAnchor(m_branchDiffFileAnchors.at(idx));
        }
    }
}

// Scroll the branch/PR diff to the next/previous hunk header ("@@ -"), the same
// navigation the PR viewer's Prev/Next change buttons provide (adhoc #107).
bool MainWindow::branchScrollToAdjacentHunk(int delta)
{
    if (!m_branchDiffView)
        return false;
    QScrollBar *vbar = m_branchDiffView->verticalScrollBar();
    if (!vbar)
        return false;
    // Next/Prev reaches past the visible window, so land the whole diff first.
    flushDiffStream(m_branchDiffView);
    const int curTop = vbar->value();
    int target = delta > 0 ? std::numeric_limits<int>::max()
                           : std::numeric_limits<int>::min();
    QTextCursor cur(m_branchDiffView->document());
    while (true) {
        cur = m_branchDiffView->document()->find(QStringLiteral("@@ -"), cur);
        if (cur.isNull())
            break;
        QTextCursor lineCur(cur);
        lineCur.setPosition(cur.selectionStart());
        lineCur.movePosition(QTextCursor::StartOfLine);
        const int y = m_branchDiffView->cursorRect(lineCur).top() + curTop;
        if (delta > 0) {
            if (y > curTop + 4)
                target = std::min(target, y);
        } else if (y < curTop - 4) {
            target = std::max(target, y);
        }
    }
    if (delta > 0 ? target == std::numeric_limits<int>::max()
                  : target == std::numeric_limits<int>::min())
        return false; // no further hunk in that direction
    vbar->setValue(std::clamp(target - 4, vbar->minimum(), vbar->maximum()));
    return true;
}

// Show or hide the range pane's find bar; hiding clears the search text (and so
// the highlights), matching the PR viewer's bar (issue #333, adhoc #107).
void MainWindow::toggleBranchDiffSearch(bool show)
{
    if (!m_branchDiffSearchBar || !m_branchDiffSearchInput)
        return;
    m_branchDiffSearchBar->setVisible(show);
    if (show) {
        m_branchDiffSearchInput->setFocus();
        m_branchDiffSearchInput->selectAll();
    } else {
        m_branchDiffSearchInput->clear(); // triggers recompute; clears highlights
        if (m_branchDiffView)
            m_branchDiffView->setFocus();
    }
}

// Re-scan the branch/PR diff for the current search text and highlight every
// match; called on every keystroke and after each re-render.
void MainWindow::branchDiffSearchRecompute()
{
    if (!m_branchDiffView)
        return;
    m_branchDiffSearchMatches.clear();
    m_branchDiffSearchIndex = -1;

    const QString term =
        m_branchDiffSearchInput ? m_branchDiffSearchInput->text() : QString();
    if (!term.isEmpty()) {
        // Search covers the whole diff, not just the rendered window.
        flushDiffStream(m_branchDiffView);
        QTextCursor cur = m_branchDiffView->document()->find(term);
        while (!cur.isNull()) {
            m_branchDiffSearchMatches.append(cur);
            if (m_branchDiffSearchMatches.size() >= 5000)
                break; // safety cap on pathological match counts
            cur = m_branchDiffView->document()->find(term, cur);
        }
        if (!m_branchDiffSearchMatches.isEmpty())
            m_branchDiffSearchIndex = 0;
    }

    applyDiffSearchHighlights(m_branchDiffView, m_branchDiffSearchMatches,
                              m_branchDiffSearchIndex, m_branchDiffSearchCount,
                              term.isEmpty());
    if (m_branchDiffSearchIndex >= 0)
        branchDiffSearchGoTo(0);
}

// Step the active match by delta (wrapping), re-highlight, and scroll it into
// view; delta of 0 just scrolls to the current match.
void MainWindow::branchDiffSearchGoTo(int delta)
{
    if (!m_branchDiffView || m_branchDiffSearchMatches.isEmpty())
        return;
    QScrollBar *vbar = m_branchDiffView->verticalScrollBar();
    if (!vbar)
        return;
    const int count = m_branchDiffSearchMatches.size();
    m_branchDiffSearchIndex =
        ((m_branchDiffSearchIndex + delta) % count + count) % count;
    applyDiffSearchHighlights(m_branchDiffView, m_branchDiffSearchMatches,
                              m_branchDiffSearchIndex, m_branchDiffSearchCount,
                              false);
    const QTextCursor &target =
        m_branchDiffSearchMatches.at(m_branchDiffSearchIndex);
    QTextCursor lineCur(target);
    lineCur.setPosition(target.selectionStart());
    const int y = m_branchDiffView->cursorRect(lineCur).top() + vbar->value();
    const int centered = y - m_branchDiffView->viewport()->height() / 3;
    vbar->setValue(std::clamp(centered, vbar->minimum(), vbar->maximum()));
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
