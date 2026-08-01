






#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"
#include "PacmanProgress.h"

#include <QComboBox>
#include <QTimer>

#include <algorithm>

using namespace forkmesh::ui;





static QString branchMergeTree(const QString &dir, const QString &base,
                               const QString &branch);




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
static constexpr int kBranchAheadRole = Qt::UserRole + 76;
static constexpr int kBranchBehindRole = Qt::UserRole + 77;

// Match the compact agent-branch chip: conflicts win because they need manual
// resolution, otherwise a branch behind its base gets the download/merge-needed
// marker.  Keeping this decision in one helper also makes the icon semantics
// directly testable without comparing rendered pixels.
static QString branchHealthIconName(bool conflicted, int behind)
{
    if (conflicted)
        return QStringLiteral("alert");
    if (behind > 0)
        return QStringLiteral("download");
    return QString();
}

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
        // Paint only the style's cell background before composing the compact
        // contents ourselves. Calling QStyledItemDelegate::paint (including via
        // SelectionBorderRowDelegate) re-runs initStyleOption(index), which puts
        // DisplayRole and DecorationRole back after we clear them and therefore
        // paints the branch name once at the cell edge and again below after the
        // metadata. That double paint is the dense overlap seen on long lists.
        const QStyleOptionViewItem background =
            backgroundStyleOption(option, index);
        QStyle *style = option.widget ? option.widget->style()
                                      : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter,
                           option.widget);
        paintRowSelectionBorder(painter, option, index);

        painter->save();
        painter->setClipRect(option.rect);
        // Selection is a transparent green outline on this table, so keep the
        // ordinary text colours. HighlightedText is white and made the selected
        // branch name disappear against the unchanged white/light background.
        const QColor primary = primaryTextColor(option, index);
        const QColor muted(QStringLiteral("#8b949e"));
        const int cy = option.rect.center().y();
        int x = option.rect.left() + 8;

        constexpr int iconSlot = 20;
        const QIcon statusIcon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!statusIcon.isNull())
            statusIcon.paint(painter, QRect(x, cy - 7, 14, 14));
        // Every optional icon owns the same fixed slot, even when absent, so
        // age, worktree, file, conflict and charts form straight columns.
        x += iconSlot;

        painter->setPen(muted);
        const QString updated = index.data(kBranchUpdatedRole).toString();
        constexpr int ageWidth = 32;
        painter->drawText(QRect(x, option.rect.top(), ageWidth, option.rect.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, updated);
        x += ageWidth;

        if (index.data(kBranchWorktreeRole).toBool())
            themedOcticon("file-directory", QColor("#58a6ff"), 13)
                .paint(painter, QRect(x, cy - 7, 14, 14));
        x += iconSlot;

        const QVariant filesValue = index.data(kBranchFilesRole);
        const int files = filesValue.isValid() ? filesValue.toInt() : -1;
        if (files >= 0) {
            painter->setPen(muted);
            const QString fileText = files > 99 ? QStringLiteral("99+")
                                                : QString::number(files);
            painter->drawText(QRect(x, option.rect.top(), 28, option.rect.height()),
                              Qt::AlignVCenter | Qt::AlignLeft, fileText);
        }
        x += 28;

        const QString healthIcon = branchHealthIconName(
            index.data(kBranchConflictRole).toBool(),
            index.data(kBranchBehindRole).isValid()
                ? index.data(kBranchBehindRole).toInt()
                : -1);
        if (!healthIcon.isEmpty()) {
            const QColor healthColor(currentThemeIsDark() ? "#e3742f"
                                                           : "#bc4c00");
            themedOcticon(healthIcon, healthColor, 13)
                .paint(painter, QRect(x, cy - 7, 14, 14));
        }
        x += iconSlot;

        const QVariant addedValue = index.data(kBranchAddedRole);
        const QVariant removedValue = index.data(kBranchRemovedRole);
        const int added = addedValue.isValid() ? addedValue.toInt() : -1;
        const int removed = removedValue.isValid() ? removedValue.toInt() : -1;
        constexpr int barWidth = 4;
        constexpr int barGap = 2;
        constexpr int maxHeight = 15;
        if (added >= 0 && removed >= 0) {
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
        }
        x += 2 * barWidth + barGap + 8;

        const QVariant aheadValue = index.data(kBranchAheadRole);
        const QVariant behindValue = index.data(kBranchBehindRole);
        const int ahead = aheadValue.isValid() ? aheadValue.toInt() : -1;
        const int behind = behindValue.isValid() ? behindValue.toInt() : -1;
        if (ahead >= 0 && behind >= 0) {
            constexpr int chartHeight = 15;
            constexpr int chartBarWidth = 4;
            constexpr int chartGap = 2;
            const int total = qMax(1, ahead + behind);
            const int behindHeight = behind == 0
                                         ? 1
                                         : qMax(2, behind * chartHeight / total);
            const int aheadHeight = ahead == 0
                                        ? 1
                                        : qMax(2, ahead * chartHeight / total);
            painter->fillRect(QRect(x, cy + chartHeight / 2 - behindHeight,
                                    chartBarWidth, behindHeight),
                              QColor("#d29922"));
            painter->fillRect(QRect(x + chartBarWidth + chartGap,
                                    cy + chartHeight / 2 - aheadHeight,
                                    chartBarWidth, aheadHeight),
                              QColor("#58a6ff"));
        }
        x += 18;

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

#ifdef FORKMESH_WINDOW_TESTS
    bool testStyleLayerHasNoContent(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
    {
        const QStyleOptionViewItem background =
            backgroundStyleOption(option, index);
        return background.text.isEmpty() && background.icon.isNull() &&
               !(background.features & QStyleOptionViewItem::HasDisplay) &&
               !(background.features & QStyleOptionViewItem::HasDecoration);
    }

    bool testSelectionKeepsNormalTextColor(const QStyleOptionViewItem &option,
                                           const QModelIndex &index) const
    {
        QStyleOptionViewItem selected(option);
        selected.state |= QStyle::State_Selected;
        QStyleOptionViewItem normal(option);
        normal.state &= ~QStyle::State_Selected;
        return primaryTextColor(selected, index) == primaryTextColor(normal, index);
    }
#endif

private:
    static QColor primaryTextColor(const QStyleOptionViewItem &option,
                                   const QModelIndex &index)
    {
        const QVariant foreground = index.data(Qt::ForegroundRole);
        return foreground.canConvert<QBrush>()
                   ? foreground.value<QBrush>().color()
                   : option.palette.color(QPalette::Text);
    }

    QStyleOptionViewItem backgroundStyleOption(
        const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        QStyleOptionViewItem background(option);
        initStyleOption(&background, index);
        background.text.clear();
        background.icon = QIcon();
        background.features &= ~QStyleOptionViewItem::HasDisplay;
        background.features &= ~QStyleOptionViewItem::HasDecoration;
        // The branches list intentionally has no hover fill, and selection is
        // represented by the shared green outline painted above—not a style
        // supplied per-cell band.
        background.state &= ~QStyle::State_MouseOver;
        background.state &= ~QStyle::State_Selected;
        return background;
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
    installColumnHeaderMenu(m_worktreesTable);
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
                                   true);
    });


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


    if (table && table->isVisible() && table->isEnabled())
        table->setFocus(Qt::OtherFocusReason);
}

void MainWindow::loadWorktreesPanel()
{
    if (!m_worktreesTable)
        return;



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
        if (auto *b = dynamic_cast<VerticalIconButton *>(m_worktreesButton))
            b->setBadgeCount(0);
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




    wts.erase(std::remove_if(wts.begin(), wts.end(), [](const WT &wt) {
                  return wt.branch == QLatin1String("forkmesh/pulls");
              }),
              wts.end());

    const QString mainPath = QDir(repoPath).absolutePath();


    const QString baseBranch = repoDefaultBranch(repoBranches());





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
        bItem->setData(Qt::UserRole, wt.branch);
        m_worktreesTable->setItem(row, 0, bItem);
        m_worktreesTable->setItem(row, 1, new QTableWidgetItem(wt.path));



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
                    return;
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
                        return;
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

            auto *mergeBtn = new QPushButton("Merge into main");
            mergeBtn->setObjectName("ghostButton");
            mergeBtn->setCursor(Qt::PointingHandCursor);
            setOcticon(mergeBtn, "check-circle", 14);
            connect(mergeBtn, &QPushButton::clicked, this,
                    [this, branch, p] { mergeWorktreeIntoMain(branch, p); });
            h->addWidget(mergeBtn);

            auto *prBtn = new QPushButton("Create PR");
            prBtn->setObjectName("ghostButton");
            prBtn->setCursor(Qt::PointingHandCursor);
            setOcticon(prBtn, "git-pull-request", 14);
            connect(prBtn, &QPushButton::clicked, this,
                    [this, branch] { createPullFromBranch(branch); });
            h->addWidget(prBtn);
        }
        if (!isMain) {

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
    if (auto *b = dynamic_cast<VerticalIconButton *>(m_worktreesButton))
        b->setBadgeCount(wts.size());




    if (!keepPath.isEmpty()) {
        const QString keep = QDir(keepPath).absolutePath();
        for (int row = 0; row < m_worktreesTable->rowCount(); ++row) {
            QTableWidgetItem *p = m_worktreesTable->item(row, 1);
            if (p && QDir(p->text()).absolutePath() == keep) {
                m_worktreesTable->selectRow(row);
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
        QTableWidgetItem *it =
            m_branchesTable->item(row, kBranchesNameColumn);
        if (it && it->text() == branch) {
            m_branchesTable->selectRow(row);
            return true;
        }
    }
    return false;
}








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
        const QTableWidgetItem *item =
            m_branchesTable->item(row, kBranchesNameColumn);
        if (!item || item->text() != branch)
            continue;
        auto value = [item](int role) {
            const QVariant data = item->data(role);
            return data.isValid() ? data.toString() : QStringLiteral("-");
        };
        return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
            .arg(value(kBranchFilesRole), value(kBranchAddedRole),
                 value(kBranchRemovedRole),
                 item->data(kBranchWorktreeRole).toBool() ? QStringLiteral("1")
                                                          : QStringLiteral("0"),
                 item->data(kBranchConflictRole).toBool() ? QStringLiteral("1")
                                                          : QStringLiteral("0"),
                 value(kBranchUpdatedRole), value(kBranchBehindRole),
                 value(kBranchAheadRole));
    }
    return QString();
}

QString MainWindow::testBranchHealthIcon(const QString &branch) const
{
    if (!m_branchesTable)
        return QString();
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        const QTableWidgetItem *item =
            m_branchesTable->item(row, kBranchesNameColumn);
        if (!item || item->text() != branch)
            continue;
        const QVariant behind = item->data(kBranchBehindRole);
        return branchHealthIconName(
            item->data(kBranchConflictRole).toBool(),
            behind.isValid() ? behind.toInt() : -1);
    }
    return QString();
}

bool MainWindow::testBranchesUseCompactColumns() const
{
    return m_branchesTable && m_branchesTable->columnCount() == 5 &&
           m_branchesTable->isColumnHidden(kBranchesUpdatedColumn) &&
           m_branchesTable->isColumnHidden(kBranchesWorktreeColumn);
}

bool MainWindow::testBranchesKeepFlexibleNameColumn() const
{
    return m_branchesTable &&
           m_branchesTable->horizontalHeader()->sectionResizeMode(
               kBranchesNameColumn) ==
               QHeaderView::Stretch;
}

bool MainWindow::testBranchDelegatePaintsSingleTextLayer(
    const QString &branch) const
{
    if (!m_branchesTable)
        return false;
    auto *delegate = dynamic_cast<BranchOverviewDelegate *>(
        m_branchesTable->itemDelegateForColumn(kBranchesNameColumn));
    if (!delegate)
        return false;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        const QModelIndex index =
            m_branchesTable->model()->index(row, kBranchesNameColumn);
        if (index.data(Qt::DisplayRole).toString() != branch)
            continue;
        QStyleOptionViewItem option;
        option.initFrom(m_branchesTable->viewport());
        option.rect = m_branchesTable->visualRect(index);
        return delegate->testStyleLayerHasNoContent(option, index);
    }
    return false;
}

bool MainWindow::testBranchSelectedTextColorIsReadable(const QString &branch) const
{
    if (!m_branchesTable)
        return false;
    auto *delegate = dynamic_cast<BranchOverviewDelegate *>(
        m_branchesTable->itemDelegateForColumn(kBranchesNameColumn));
    if (!delegate)
        return false;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        const QModelIndex index =
            m_branchesTable->model()->index(row, kBranchesNameColumn);
        if (index.data(Qt::DisplayRole).toString() != branch)
            continue;
        QStyleOptionViewItem option;
        option.initFrom(m_branchesTable->viewport());
        option.rect = m_branchesTable->visualRect(index);
        return delegate->testSelectionKeepsNormalTextColor(option, index);
    }
    return false;
}

QString MainWindow::testSwitchToBranchImmediateSelection(const QString &branch)
{
    switchToBranch(branch);
    if (!m_branchesTable)
        return QString();
    const QTableWidgetItem *it = m_branchesTable->item(
        m_branchesTable->currentRow(), kBranchesNameColumn);
    return it ? it->text() : QString();
}

bool MainWindow::testClickBranchRowInOverview(const QString &branch)
{
    if (!m_branchesTable)
        return false;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        QTableWidgetItem *item =
            m_branchesTable->item(row, kBranchesNameColumn);
        if (!item || item->text() != branch)
            continue;
        // Emit the same signal a real non-action cell click produces.
        emit m_branchesTable->cellClicked(row, kBranchesNameColumn);
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

bool MainWindow::testGitWorkspaceIsExclusive() const
{
    if (!m_railGitButton || !m_railCodeButton || !m_repoDetailChrome ||
        !m_repoFilesModeBar || !m_repoOverviewChrome || !m_footerDock ||
        !m_commitsStack)
        return false;
    if (!m_railGitButton->isChecked() || m_railCodeButton->isChecked() ||
        !m_repoDetailChrome->isHidden() || !m_repoFilesModeBar->isHidden() ||
        !m_repoOverviewChrome->isHidden() || !m_footerDock->isHidden())
        return false;
    for (QTextEdit *view : m_diffViews) {
        if (view && view->isVisibleTo(this) &&
            !m_commitsStack->isAncestorOf(view))
            return false;
    }
    return true;
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

int MainWindow::testGitPendingSyncCount() const
{
    return m_railGitButton ? m_railGitButton->pendingSyncCount() : 0;
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



    m_worktreeSelectedBranch = branch;
    m_worktreeSelectedPath = worktreePath;
    if (m_worktreeBranchLabel) {
        if (branch.isEmpty()) {
            m_worktreeBranchLabel->setText(QString());
        } else {


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


    if (m_worktreeCommitButton)
        m_worktreeCommitButton->setEnabled(!worktreePath.isEmpty() &&
                                           QDir(worktreePath).exists());
    if (m_worktreeRemoveButton)
        m_worktreeRemoveButton->setEnabled(!isMain && !worktreePath.isEmpty());


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






void MainWindow::closeBranchDiffAfterMerge()
{
    closeBranchCompareView();
}




bool MainWindow::mergeWorktreeIntoMain(const QString &branchArg,
                                       const QString &worktreePathArg,
                                       bool deleteAgent)
{




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


    QByteArray st;
    if (runGitCapture(dir, {"status", "--porcelain"}, &st, nullptr)
        && !QString::fromUtf8(st).trimmed().isEmpty()) {
        setRepoDetailNotice(
            "The checkout has uncommitted changes — commit/stash them or use Create PR.",
            true);
        return false;
    }















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











    GitKeepAlive keepAlive;







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




    bool branchDeleted = false;
    QString err;
    const bool merged =
        runGitCapture(dir,
                      {"merge", "--no-ff", branch,
                       "-m", QStringLiteral("Merge %1 into %2").arg(branch, base)},
                      nullptr, &err);






    QByteArray conflicted;
    const bool hasConflicts =
        runGitCapture(dir, {"diff", "--name-only", "--diff-filter=U"}, &conflicted,
                      nullptr) &&
        !QString::fromUtf8(conflicted).trimmed().isEmpty();







    const bool branchInBase =
        runGitCapture(dir, {"merge-base", "--is-ancestor", branch, "HEAD"}, nullptr,
                      nullptr);
    if (merged && !hasConflicts && branchInBase) {






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




        QList<int> deletedAgents;
        if (deleteAgent && !branch.isEmpty()
            && m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {


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
            removeWorktree(worktreePath, branch,  false,
                            true);
            removed = !QDir(worktreePath).exists();
            branchDeleted = removed && !localBranchExists(dir, branch);
        } else if (deleteAgent && localBranchExists(dir, branch)) {




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

            markAgentSessionsMerged(0, branch);
        } else {
            reloadAgents();
            reloadIssues();
            refreshIssueList();
            updateIssueActionState();
        }




        if (m_branchAutoPullAllCheck && m_branchAutoPullAllCheck->isChecked())
            pullBaseIntoAllBranches();
    } else {




        runGitCapture(dir, {"merge", "--abort"}, nullptr, nullptr);
        setRepoDetailNotice(
            QStringLiteral("Couldn't merge %1 into %2 cleanly — kept its worktree and "
                           "branch. Update it from %2 to resolve the conflicts (the "
                           "\"Update from %2\" button), or use \"Fix with agent\".")
                .arg(branch, base),
            true);
    }

















    if (branchDeleted)
        flashMergedBranchRow(branch);
    else if (merged && !hasConflicts && branchInBase && worktreePath.isEmpty()) {
        const QString next = neighbourBranchInList(branch);
        if (!next.isEmpty())
            m_branchDiffBranch = next;
    }




    refreshSourceControl(true);
    loadWorktreesPanel();





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




    auto finish = [this, repoPath, worktreePath, branch, deleteBranch,
                   onDone = std::move(onDone)] {


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






    GitKeepAlive keepAlive;




    for (int id : std::as_const(agentIds)) {
        if (!deleteStoredAgentSession(id)) {
            if (!deferRefresh)
                reloadAgents();
            return;
        }
    }

    if (!worktreePath.isEmpty()) {




        removeWorktree(worktreePath, branch,  false,
                        willDeleteBranch, async);
    } else if (willDeleteBranch && localBranchExists(repoPath, branch)) {

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




    QElapsedTimer storeTimer;
    storeTimer.start();
    for (const int id : std::as_const(agentIds)) {
        if (!deleteStoredAgentSession(id,  false)) {
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











    for (const QString &branch : std::as_const(branches)) {
        const QString wt = worktreePathForBranch(repoPath, branch);
        deleteWorktreeBranchAndAgent(wt, branch,  false,  false,
                                      true);
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






    const QString base = baseArg.isEmpty() ? repoDefaultBranch(repoBranches()) : baseArg;
    if (worktreePath.isEmpty() || branch.isEmpty() || base.isEmpty() || branch == base)
        return;
    if (!QDir(worktreePath).exists()) {
        setRepoDetailNotice("That worktree's folder is gone.", true);
        loadWorktreesPanel();
        return;
    }

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


    loadWorktreesPanel();
}




bool MainWindow::editWorktreeConflicts(const QString &worktreePath,
                                       const QString &branch, const QString &base)
{
    QByteArray unmerged;
    runGitCapture(worktreePath, {"diff", "--name-only", "--diff-filter=U"},
                  &unmerged, nullptr);
    const QStringList conflicted =
        QString::fromUtf8(unmerged).split('\n', Qt::SkipEmptyParts);
    if (conflicted.isEmpty()) {


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


    loadWorktreesPanel();
}

QWidget *MainWindow::buildBranchesTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);



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



    m_branchPullAllButton = new QPushButton("Pull into all");
    m_branchPullAllButton->setObjectName("ghostButton");
    m_branchPullAllButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_branchPullAllButton, "download", 16);
    connect(m_branchPullAllButton, &QPushButton::clicked, this,
            &MainWindow::pullBaseIntoAllBranches);



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



    headerRow->addWidget(m_branchDeleteMergedButton);
    headerRow->addWidget(newBranchButton);
    layout->addLayout(headerRow);

    m_branchesTable = new QTableWidget(0, 5);
    installColumnHeaderMenu(m_branchesTable); // 3-dots per-column menu (issue #318)
    m_branchesTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_branchesTable);
    m_branchesTable->setHorizontalHeaderLabels(
        {"", "Branch", "Status", "Updated", "Worktree"});
    m_branchesTable->verticalHeader()->setVisible(false);


    m_branchesTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_branchesTable->verticalHeader()->setDefaultSectionSize(30);
    m_branchesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_branchesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_branchesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_branchesTable->setShowGrid(false);
    m_branchesTable->setWordWrap(false);
    m_branchesTable->setTextElideMode(Qt::ElideNone);
    m_branchesTable->setItemDelegateForColumn(
        kBranchesNameColumn, new BranchOverviewDelegate(m_branchesTable));
    QHeaderView *bh = m_branchesTable->horizontalHeader();
    bh->setHighlightSections(false);
    // The compact trash action sits immediately before the flexible branch name.
    bh->setSectionResizeMode(kBranchesDeleteColumn, QHeaderView::Fixed);
    bh->resizeSection(kBranchesDeleteColumn, 36);
    bh->setSectionResizeMode(kBranchesNameColumn, QHeaderView::Stretch);
    bh->setSectionResizeMode(kBranchesStatusColumn, QHeaderView::ResizeToContents);
    bh->setSectionResizeMode(kBranchesUpdatedColumn, QHeaderView::ResizeToContents);
    // Worktree column: shows the on-disk path of the worktree (if any) a branch
    // is checked out in, so the list surfaces an agent's isolated working tree
    // without a trip to the Worktrees tab. Sized to its content.
    bh->setSectionResizeMode(kBranchesWorktreeColumn,
                             QHeaderView::ResizeToContents);
    // Keep the Branch cell flexible. Converting it to Interactive freezes its
    // narrow initial size and leaves a large unused area to the right, forcing
    // the inline agent/worktree/churn metadata to overlap the branch name.
    makeColumnsResizable(m_branchesTable, kBranchesNameColumn);
    // Updated and Worktree now live as compact glyphs/metadata inside Branch,
    // matching the Agents list. Keep their model cells populated (automation and
    // accessibility still read them) but remove the duplicate visual columns.
    m_branchesTable->setColumnHidden(kBranchesUpdatedColumn, true);
    m_branchesTable->setColumnHidden(kBranchesWorktreeColumn, true);
    // Selecting a real branch (click or arrow keys) retires the "merged &
    // deleted" check left where a branch used to be (adhoc #15). The diff
    // preview that used to ride this selection moved into the Git view's range
    // pane (adhoc #107) — see the cellClicked navigation below.
    connect(m_branchesTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                QTableWidgetItem *it =
                    m_branchesTable->item(row, kBranchesNameColumn);
                if (it && !it->text().isEmpty())
                    clearMergedBranchFlash();
            });
    // The leading trash cell owns its button click. Every other visible cell
    // opens the branch's commits, changed files and diff in the Git view.
    connect(m_branchesTable, &QTableWidget::cellClicked, this,
            [this](int row, int column) {
                if (column == kBranchesDeleteColumn)
                    return;
                QTableWidgetItem *it =
                    m_branchesTable->item(row, kBranchesNameColumn);
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
    m_branchDiffView->setOpenLinks(false);
    connect(m_branchDiffView, &QTextBrowser::anchorClicked, this,
            &MainWindow::onBranchDiffAnchorClicked);
    registerDiffView(m_branchDiffView);


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



    m_branchAutoViewedDebounce = new QTimer(this);
    m_branchAutoViewedDebounce->setSingleShot(true);
    m_branchAutoViewedDebounce->setInterval(400);
    connect(m_branchAutoViewedDebounce, &QTimer::timeout, this,
            &MainWindow::applyBranchAutoMarkViewedOnScroll);
    connect(m_branchDiffView->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] {

                updateBranchDiffSticky();

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



    m_branchFixAgentCombo = new QComboBox;
    m_branchFixAgentCombo->setObjectName("issueControlSm");
    m_branchFixAgentCombo->setCursor(Qt::PointingHandCursor);
    m_branchFixAgentCombo->setToolTip("Which agent resolves the conflicts");
    m_branchFixAgentCombo->addItem(QStringLiteral("Claude"), QStringLiteral("claude"));
    m_branchFixAgentCombo->addItem(QStringLiteral("OpenAI"), QStringLiteral("openai"));
    m_branchFixAgentCombo->addItem(QStringLiteral("CC"),
                                   QStringLiteral("claude-code"));
    m_branchFixAgentCombo->hide();


    {
        const QString def = defaultAgentProvider();
        const QString want = def == QLatin1String("claude-api")
                                 ? QStringLiteral("claude")
                                 : def;
        const int idx = m_branchFixAgentCombo->findData(want);
        m_branchFixAgentCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }



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


        if (mergeWorktreeIntoMain(m_branchDiffBranch,
                                  worktreePathForBranch(repoPath, m_branchDiffBranch),
                                   true))
            closeBranchDiffAfterMerge();
    });

    auto *detailBar = new QHBoxLayout;
    detailBar->setContentsMargins(0, 0, 0, 0);
    detailBar->addWidget(m_branchCloseButton);
    detailBar->addWidget(m_branchDetailLabel);
    detailBar->addStretch();


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

    layout->setContentsMargins(16, 12, 16, 16);
    layout->setSpacing(8);
    layout->addLayout(detailBar);
    layout->addWidget(m_branchDiffSearchBar);
    layout->addWidget(m_branchDiffView, 1);



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






MainWindow::BranchesPanelData
MainWindow::readBranchesPanelGit(BranchesPanelData data)
{
    const QString &dir = data.dir;
    if (dir.isEmpty())
        return data;





    data.branches = listRepoBranches(dir);
    data.base = chooseDefaultBranch(data.branches, data.configuredDefault, dir,
                                    data.checkedOut);
    const QString &base = data.base;




    if (!base.isEmpty() && data.branches.removeOne(base))
        data.branches.prepend(base);
    data.selected = data.checkedOut.isEmpty() ? base : data.checkedOut;








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

                if (parts.size() >= 3)
                    data.remoteAheadBehind.insert(
                        parts.first(),
                        qMakePair(parts.at(1).toInt(), parts.at(2).toInt()));
            }
        }
    }







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

            if (parts.size() >= 2)
                data.localAheadBehind.insert(
                    branch, qMakePair(parts.at(1).toInt(), parts.at(0).toInt()));
        }
    }




    QByteArray wtOut;
    if (runGitCapture(dir, {"worktree", "list", "--porcelain"}, &wtOut, nullptr)) {



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


    data.previouslyViewed = m_branchDiffBranch;

    m_branchesPanelLoading = true;
    const int gen = ++m_branchesPanelGen;
    runOffThread<BranchesPanelData>(
        [data] { return readBranchesPanelGit(data); },
        [this, gen](BranchesPanelData loaded) {
            if (gen != m_branchesPanelGen)
                return;
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
                QTableWidgetItem *name =
                    m_branchesTable->item(row, kBranchesNameColumn);
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




    TableRepaintGuard repaintGuard(m_branchesTable);



    QSignalBlocker branchesTableBlock(m_branchesTable);




    m_branchesTable->setRowCount(data.branches.size() +
                                 data.remoteBranches.size());
    // setRowCount() only destroys the cell widgets of rows it trims, so the
    // "Merged & deleted" widget the previous render put in the Branch column survives into
    // a surviving row and paints on top of that row's branch name — and every
    // rebuild inside the flash window stacked another one (adhoc #56). The Branch
    // column is the only one whose widget is conditional; the delete column gets
    // a fresh widget on every row below.
    for (int r = 0, rows = m_branchesTable->rowCount(); r < rows; ++r) {
        if (m_branchesTable->cellWidget(r, kBranchesNameColumn))
            m_branchesTable->removeCellWidget(r, kBranchesNameColumn);
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
    // current repo, so its status icon stays in the compact Branch cell after the
    // redundant Issue / Agent column was removed. A branch may carry more than
    // one session over its life; prefer one bound to an issue and otherwise the
    // most recent.
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



    int actionWidth = 0;
    bool anyBehind = false;
    bool anyMerged = false;


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
        m_branchesTable->setItem(row, kBranchesNameColumn, name);


        QString status = branch == base ? QStringLiteral("Default branch") : QString();
        int behind = 0;
        int ahead = 0;
        bool divergenceKnown = branch == base;
        bool hasConflict = false;
        qint64 ts = 0;
        if (branch == base) {
            name->setData(kBranchAheadRole, 0);
            name->setData(kBranchBehindRole, 0);
        }
        if (!dir.isEmpty()) {
            if (branch != base) {



                const auto abIt = localAheadBehind.constFind(branch);
                const bool counted = abIt != localAheadBehind.constEnd();
                if (counted) {
                    divergenceKnown = true;
                    ahead = abIt->first;
                    behind = abIt->second;
                    name->setData(kBranchAheadRole, ahead);
                    name->setData(kBranchBehindRole, behind);
                }
                if (counted)
                    status = QString::fromUtf8("%1 behind \xC2\xB7 %2 ahead")
                                 .arg(behind)
                                 .arg(ahead);







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
        QString healthTip;
        if (branch != base && divergenceKnown) {
            healthTip = QStringLiteral("%1 commit(s) behind · %2 ahead of %3")
                            .arg(behind)
                            .arg(ahead)
                            .arg(base);
            if (behind > 0)
                healthTip += QStringLiteral("\nMerge %1 into this branch to update it")
                                 .arg(base);
        }
        if (hasConflict)
            healthTip += (healthTip.isEmpty() ? QString() : QStringLiteral("\n")) +
                         QStringLiteral("Conflicts with %1").arg(base);
        const QString existingTip = name->toolTip();
        if (!healthTip.isEmpty())
            name->setToolTip(existingTip.isEmpty()
                                 ? healthTip
                                 : existingTip + QLatin1Char('\n') + healthTip);
        m_branchesTable->setItem(row, kBranchesStatusColumn, statusItem);
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
        m_branchesTable->setItem(row, kBranchesUpdatedColumn, updated);




        const QString worktreePath = branchWorktrees.value(branch);
        name->setData(kBranchWorktreeRole, !worktreePath.isEmpty());
        auto *worktree = new QTableWidgetItem(worktreePath);
        if (!worktreePath.isEmpty()) {
            worktree->setIcon(themedOcticon("file-directory", QColor("#8b949e"), 13));
            worktree->setToolTip(worktreePath);
            worktree->setForeground(QColor("#8b949e"));
        }
        m_branchesTable->setItem(row, kBranchesWorktreeColumn, worktree);





        if (writable && branch != base && behind > 0)
            anyBehind = true;
        auto *actions = new QWidget;



        actions->setObjectName("branchActions");
        actions->setStyleSheet("#branchActions { background: transparent; }");
        auto *actionRow = new QHBoxLayout(actions);
        actionRow->setContentsMargins(2, 0, 2, 0);
        actionRow->setSpacing(0);


        auto *del = new QPushButton;
        del->setObjectName("issueIconButton");
        del->setFlat(true);
        del->setCursor(Qt::PointingHandCursor);
        del->setIcon(themedOcticon("trash", QColor("#f85149"), 15));
        del->setIconSize(QSize(15, 15));
        del->setToolTip(QStringLiteral("Delete branch %1").arg(branch));
        const bool canDelete = writable && branch != base && branch != selected;
        del->setEnabled(canDelete);


        if (canDelete && behind == 0 && ahead == 0)
            anyMerged = true;
        if (!canDelete)
            del->setToolTip(writable
                                ? "Can't delete the default or current branch"
                                : "Read-only mirror — no working tree to delete from");
        connect(del, &QPushButton::clicked, this,
                [this, branch] { deleteBranch(branch); });
        actionRow->addWidget(del);

        m_branchesTable->setCellWidget(row, kBranchesDeleteColumn, actions);
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
        m_branchesTable->horizontalHeader()->resizeSection(
            kBranchesDeleteColumn, actionWidth + 4);









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
        m_branchesTable->setItem(row, kBranchesNameColumn, name);


        QString rstatus = QStringLiteral("Remote");
        QString rtip = QStringLiteral("Remote-tracking branch");
        const auto abIt = remoteAheadBehind.constFind(branch);
        if (abIt != remoteAheadBehind.constEnd()) {
            const int rahead = abIt->first;
            const int rbehind = abIt->second;
            name->setData(kBranchAheadRole, rahead);
            name->setData(kBranchBehindRole, rbehind);
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
        m_branchesTable->setItem(row, kBranchesStatusColumn, statusItem);

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
            m_branchesTable->setItem(row, kBranchesUpdatedColumn, rupdated);
        }
        m_branchesTable->setItem(row, kBranchesWorktreeColumn,
                                 new QTableWidgetItem);





        const QString remoteName = branch.section('/', 0, 0);
        const QString remoteRef = branch.section('/', 1);
        const bool canDeleteRemote =
            !remoteName.isEmpty() && !remoteRef.isEmpty() && remoteRef != base;
        auto *ractions = new QWidget;
        ractions->setObjectName("branchActions");
        ractions->setStyleSheet("#branchActions { background: transparent; }");
        auto *ractionRow = new QHBoxLayout(ractions);
        ractionRow->setContentsMargins(2, 0, 2, 0);
        ractionRow->setSpacing(0);
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
        m_branchesTable->setCellWidget(row, kBranchesDeleteColumn, ractions);
        ractions->ensurePolished();
        for (QPushButton *b : ractions->findChildren<QPushButton *>()) {
            b->ensurePolished();
            b->setMinimumWidth(b->sizeHint().width());
        }
        ractionRow->invalidate();
        actionWidth = qMax(actionWidth, ractions->sizeHint().width());
    }
    if (actionWidth > 0)
        m_branchesTable->horizontalHeader()->resizeSection(
            kBranchesDeleteColumn, actionWidth + 4);



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






    startBranchConflictProbes(dir, base, conflictProbes);




    const QString pendingSelect = m_branchesPanelPendingSelect;
    m_branchesPanelPendingSelect.clear();

    if (m_branchesTable->rowCount() == 0) {
        m_branchesTable->insertRow(0);
        auto *empty = new QTableWidgetItem("No branches in this repository.");
        empty->setForeground(QColor("#8b949e"));
        m_branchesTable->setItem(0, kBranchesNameColumn, empty);
        // Table signals are blocked, so blank the diff pane ourselves.
        showBranchDiff(QString());
        if (!pendingSelect.isEmpty())
            reportBranchNotFound(pendingSelect);
        return;
    }






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
        m_branchesTable->setItem(row, kBranchesNameColumn, new QTableWidgetItem);
        m_branchesTable->setCellWidget(row, kBranchesNameColumn, cell);
        for (int c = 0; c < m_branchesTable->columnCount(); ++c) {
            if (c == kBranchesNameColumn)
                continue;
            m_branchesTable->setItem(row, c, new QTableWidgetItem);
        }
        m_branchesTable->setCurrentCell(row, kBranchesNameColumn);
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
        QTableWidgetItem *it =
            m_branchesTable->item(r, kBranchesNameColumn);
        if (it && it->text() == target) {
            m_branchesTable->setCurrentCell(r, kBranchesNameColumn);
            break;
        }
    }




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






    QString nextSelection = neighbourBranchInList(branch);

    QString err;

    if (!runGitCapture(dir, {"branch", "-D", branch}, nullptr, &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not delete the branch." : err, true);
        return;
    }
    logSystem(QStringLiteral("Git: deleted branch %1.").arg(branch));
    setRepoDetailNotice(QStringLiteral("Deleted branch %1.").arg(branch));
    m_branchesCache.clear();



    m_branchDiffBranch = nextSelection;
    loadBranchesAndTags();
}






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






QString MainWindow::neighbourBranchInList(const QString &branch) const
{
    const int row = branchRowInList(branch);
    if (row < 0)
        return QString();
    for (int r = row + 1; r < m_branchesTable->rowCount(); ++r) {
        QTableWidgetItem *it =
            m_branchesTable->item(r, kBranchesNameColumn);
        if (it && !it->text().isEmpty())
            return it->text();
    }
    for (int r = row - 1; r >= 0; --r) {
        QTableWidgetItem *it =
            m_branchesTable->item(r, kBranchesNameColumn);
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
        QTableWidgetItem *it =
            m_branchesTable->item(r, kBranchesNameColumn);
        if (it && it->text() == branch)
            return r;
    }
    return -1;
}







void MainWindow::flashMergedBranchRow(const QString &branch)
{
    const int row = branchRowInList(branch);
    if (branch.isEmpty() || row < 0)
        return;
    m_branchMergedFlashBranch = branch;
    m_branchMergedFlashDir = m_branchesPanelDir;
    m_branchMergedFlashRow = row;


    m_branchDiffBranch.clear();
    QTimer::singleShot(kBranchMergedFlashMs, this, [this, branch] {

        if (m_branchMergedFlashBranch == branch)
            clearMergedBranchFlash();
    });
}



void MainWindow::clearMergedBranchFlash()
{
    m_branchMergedFlashBranch.clear();
    m_branchMergedFlashDir.clear();
    m_branchMergedFlashRow = -1;
}




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


    QByteArray headOut;
    QString currentBranch;
    if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &headOut, nullptr))
        currentBranch = QString::fromUtf8(headOut).trimmed();



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





void MainWindow::updateBranchDetailActions(const QString &branch)
{
    if (!m_branchMergeButton)
        return;
    const QString dir = repoGitDir();



    const QString base = repoDefaultBranchFast();
    const bool isBase = branch.isEmpty() || branch == base;
    if (dir.isEmpty() || isBase) {
        applyBranchDetailActions(branch, base, 0, 0, false);
        return;
    }







    const int gen = ++m_branchDetailActionsGen;


    applyBranchDetailActions(branch, base, -1, -1, false);
    const QString branchWorktree = worktreePathForBranch(dir, branch);
    struct BranchDetailStats {
        int behind = 0;
        int ahead = 0;
        QString conflictKey;
        bool worktreeConflict = false;
    };
    runOffThread<BranchDetailStats>(
        [dir, base, branch, branchWorktree] {
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
            // `merge --autostash` can update the branch successfully but need
            // help while restoring the agent's uncommitted edits. That state
            // lives in the linked worktree, not in the branch refs, and must stay
            // visible even after the branch reaches 0 behind.
            if (!branchWorktree.isEmpty()) {
                QByteArray unmerged;
                s.worktreeConflict =
                    runGitCapture(branchWorktree,
                                  {"diff", "--name-only", "--diff-filter=U"},
                                  &unmerged, nullptr) &&
                    !unmerged.trimmed().isEmpty();
            }
            return s;
        },
        [this, gen, branch, base, dir](BranchDetailStats s) {

            if (gen != m_branchDetailActionsGen || m_branchDiffBranch != branch)
                return;
            bool hasConflict = false;
            if (!s.conflictKey.isEmpty()) {
                const auto cached = m_branchConflictCache.constFind(s.conflictKey);
                if (cached != m_branchConflictCache.constEnd())
                    hasConflict = *cached;
                else

                    startBranchConflictProbes(dir, base, {{branch, s.conflictKey}});
            }
            applyBranchDetailActions(branch, base, s.behind, s.ahead, hasConflict,
                                     s.worktreeConflict);
        });
}


void MainWindow::applyBranchDetailActions(const QString &branch, const QString &base,
                                          int behind, int ahead, bool hasConflict,
                                          bool worktreeConflict)
{
    if (!m_branchMergeButton)
        return;
    const QString dir = repoGitDir();
    const bool writable = repoHasWorkingTree();
    const bool isBase = branch.isEmpty() || branch == base;



    const bool counted = behind >= 0 && ahead >= 0;

    if (m_branchDetailLabel) {



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


            text = QString::fromUtf8("%1 \xC2\xB7 checking\xE2\x80\xA6").arg(pair);
        } else {
            text = QString::fromUtf8("%1 \xC2\xB7 %2 behind \xC2\xB7 %3 ahead")
                       .arg(pair)
                       .arg(behind)
                       .arg(ahead);
            if (hasConflict)
                text += QString::fromUtf8(
                    " \xC2\xB7 <span style='color:#f85149'>conflicts</span>");
            if (worktreeConflict)
                text += QString::fromUtf8(
                    " \xC2\xB7 <span style='color:#d29922'>local edits need "
                    "resolution</span>");
        }
        m_branchDetailLabel->setToolTip(
            branch.isEmpty() || base.isEmpty() || branch == base
                ? QString()
                : QStringLiteral("Showing %1's complete checkout compared with %2")
                      .arg(branch, base));


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



    if (m_branchOpenCodiumButton) {
        const bool canOpen = !branch.isEmpty() && !dir.isEmpty();
        m_branchOpenCodiumButton->setEnabled(canOpen);
        m_branchOpenCodiumButton->setToolTip(
            canOpen ? QStringLiteral("Open %1 in VSCodium").arg(branch)
                    : QStringLiteral("No local checkout to open"));
    }


    m_branchPullButton->setText(base.isEmpty() ? QStringLiteral("Pull main")
                                               : QStringLiteral("Pull %1").arg(base));
    const bool canPull = writable && !isBase && behind > 0 && !worktreeConflict;
    m_branchPullButton->setEnabled(canPull);
    if (worktreeConflict)
        m_branchPullButton->setToolTip(
            "The branch is updated; resolve its restored local edits next");
    else if (isBase)
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




    if (m_branchMergeEditorButton) {
        const bool canMergeEditor =
            writable && !isBase && (behind > 0 || worktreeConflict);
        m_branchMergeEditorButton->setText(worktreeConflict
                                               ? QStringLiteral("Resolve local edits")
                                               : QStringLiteral("Merge editor"));
        m_branchMergeEditorButton->setEnabled(canMergeEditor);
        if (worktreeConflict)
            m_branchMergeEditorButton->setToolTip(
                QStringLiteral("Resolve the agent's local edits after updating %1 "
                               "from %2; Git kept an autostash backup")
                    .arg(branch, base));
        else if (isBase)
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


    const bool canPr = writable && !isBase;
    m_branchPrButton->setEnabled(canPr);
    m_branchPrButton->setToolTip(
        canPr ? QStringLiteral("Open a pull request from %1 into %2").arg(branch, base)
              : (isBase ? "Select a branch other than the default to open a pull request"
                        : "Read-only mirror \xE2\x80\x94 no working tree to open a pull "
                          "request from"));


    const bool canMerge = writable && !isBase;
    m_branchMergeButton->setEnabled(canMerge);
    m_branchMergeButton->setToolTip(
        canMerge ? QStringLiteral("Merge %1 into %2").arg(branch, base)
                 : (isBase ? QStringLiteral("Select a branch other than %1").arg(base)
                           : "Read-only mirror \xE2\x80\x94 nothing to merge into here"));



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



    maybeAutoPullBranch(branch);
}














void MainWindow::maybeAutoPullBranch(const QString &branch)
{
    if (branch.isEmpty() || m_branchDiffBranch != branch)
        return;


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


    QString dir = worktreePathForBranch(repoPath, branch);
    if (dir.isEmpty())
        dir = repoPath;



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

    const QString wt = worktreePathForBranch(localPath, branch);
    if (!wt.isEmpty())
        return wt;


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
        m_branchDiffSticky->hide();
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
            // Review only what the branch adds from its merge base. Comparing
            // complete snapshots with `base..branch` makes every newer file on
            // a behind base appear as a reverse, unrelated branch change — the
            // Agents page might correctly report 3 files while Git reports 22.
            // A checked-out branch still includes committed, staged, unstaged,
            // deleted and untracked files through the temporary-index helper.
            if (!work.isEmpty()) {
                QByteArray mergeBaseOut;
                const bool foundMergeBase =
                    runGitCapture(dir, {QStringLiteral("merge-base"), base, branch},
                                  &mergeBaseOut, nullptr);
                const QString contentBase =
                    foundMergeBase && !mergeBaseOut.trimmed().isEmpty()
                        ? QString::fromUtf8(mergeBaseOut).trimmed()
                        : base;
                if (!buildWorkingTreeDiff(work, contentBase, &r.out, &r.err))
                    r.ok = false;
            } else if (!runGitCapture(dir, {"diff", base + "..." + branch},
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

    if (m_branchDiffSearchBar && m_branchDiffSearchBar->isVisible())
        branchDiffSearchRecompute();
}





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


    QByteArray mbox;
    runGitCapture(dir, {"format-patch", "--stdout", base + ".." + branch}, &mbox,
                  nullptr);
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    const int number = store.createPull(title, description, base, branch,
                                        QString::fromUtf8(diff),
                                        QString::fromUtf8(mbox),
                                         true, &error);
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
    // Agent worktrees are commonly dirty while the agent is still running. Use
    // Git's autostash merge so those edits are protected, base is merged, and the
    // edits are restored on top. The old plain merge refused as soon as main and
    // the agent had both touched any of the same files, which is why many
    // long-running agents suddenly accumulated the same "would be overwritten"
    // error while main kept moving.
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

        QByteArray beforeStatus;
        runGitCapture(linkedWorktree, {"status", "--porcelain"}, &beforeStatus,
                      nullptr);
        const bool protectedLocalEdits = !beforeStatus.trimmed().isEmpty();
        QString worktreeError;
        if (!runGitCapture(linkedWorktree,
                           {"merge", "--autostash", "--no-edit", base}, nullptr,
                           &worktreeError)) {
            // Abort only a merge this call actually started. A refusal caused by
            // a genuine branch conflict has MERGE_HEAD; with --autostash, aborting
            // also puts the protected local edits back where they started.
            QByteArray startedMerge;
            if (runGitCapture(linkedWorktree,
                              {"rev-parse", "-q", "--verify", "MERGE_HEAD"},
                              &startedMerge, nullptr) &&
                !startedMerge.trimmed().isEmpty())
                runGitCapture(linkedWorktree, {"merge", "--abort"}, nullptr,
                              nullptr);
            setRepoDetailNotice(
                QStringLiteral("Couldn't update %1 from %2 in its worktree: %3. "
                               "Its local changes are safe. Use Merge editor or "
                               "Fix with agent to resolve the branch conflict.")
                    .arg(branch, base,
                         worktreeError.trimmed().isEmpty()
                             ? QStringLiteral("the merge was refused")
                             : worktreeError.trimmed().left(240)),
                true);
            return;
        }

        // A merge can return success after advancing the branch but still leave
        // conflicts while reapplying the autostash. Nothing was lost: Git keeps
        // the autostash, and the toolbar exposes a one-click conflict editor that
        // resolves the worktree while leaving the agent's edits uncommitted.
        QByteArray unmerged;
        const bool restoreConflict =
            runGitCapture(linkedWorktree,
                          {"diff", "--name-only", "--diff-filter=U"}, &unmerged,
                          nullptr) &&
            !unmerged.trimmed().isEmpty();
        logSystem(QStringLiteral("Git: merged %1 into %2 in its linked worktree.")
                      .arg(base, branch));
        if (restoreConflict) {
            const int count = QString::fromUtf8(unmerged)
                                  .split('\n', Qt::SkipEmptyParts)
                                  .size();
            logSystem(
                QStringLiteral("Git: %1 local file(s) need resolution after "
                               "restoring %2's protected edits; autostash retained.")
                    .arg(count)
                    .arg(branch));
            setRepoDetailNotice(
                QStringLiteral("Updated %1 from %2, but %3 restored local file(s) "
                               "need resolution. Nothing was lost — click Resolve "
                               "local edits; Git kept an autostash backup.")
                    .arg(branch, base)
                    .arg(count),
                true);
        } else {
            setRepoDetailNotice(
                protectedLocalEdits
                    ? QStringLiteral("Updated %1 with %2 and safely restored its "
                                     "local changes.")
                          .arg(branch, base)
                    : QStringLiteral("Updated %1 with %2 in its worktree.")
                          .arg(branch, base));
        }
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



    if (ahead == 0 && !isCurrent) {







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
        m_branchesCache.clear();


        if (m_branchDiffBranch == branch)
            showBranchDiff(branch);
        return;
    }







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

        QStringList files;
        for (const QString &l :
             QString::fromUtf8(status).split('\n', Qt::SkipEmptyParts))
            files << l.mid(3);
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




    auto restoreStash = [&]() -> QString {
        if (!stashed)
            return QString();
        stashed = false;
        if (runGitCapture(dir, {"stash", "pop"}, nullptr, nullptr))
            return QStringLiteral(" Your stashed changes were restored.");


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



        QByteArray unmerged;
        runGitCapture(dir, {"diff", "--name-only", "--diff-filter=U"}, &unmerged,
                      nullptr);
        const QStringList conflicted =
            QString::fromUtf8(unmerged).split('\n', Qt::SkipEmptyParts);
        if (conflicted.isEmpty()) {

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
        m_branchesCache.clear();


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
    m_branchesCache.clear();


    if (m_branchDiffBranch == branch)
        showBranchDiff(branch);
}







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

    // If pulling base already succeeded but reapplying the agent's protected
    // local edits conflicted, finish that restoration first. This is not a merge
    // commit: keep the resolved files as the agent's uncommitted work, and only
    // clear their unmerged index entries. The automatically-created stash remains
    // a safety copy until the resolution succeeds.
    const QString linkedWorktree = worktreePathForBranch(dir, branch);
    if (!linkedWorktree.isEmpty()) {
        QByteArray unmerged;
        runGitCapture(linkedWorktree,
                      {"diff", "--name-only", "--diff-filter=U"}, &unmerged,
                      nullptr);
        const QStringList conflicted =
            QString::fromUtf8(unmerged).split('\n', Qt::SkipEmptyParts);
        if (!conflicted.isEmpty()) {
            const QString intro =
                QString::fromUtf8(
                    "Git updated <b>%1</b> from <b>%2</b>, then found overlaps "
                    "while restoring the agent's uncommitted edits. Choose the "
                    "right result for each conflict. The resolved files stay "
                    "uncommitted so the agent can continue normally.")
                    .arg(branch.toHtmlEscaped(), base.toHtmlEscaped());
            const bool resolved = runMergeConflictEditor(
                QString::fromUtf8("Resolve local edits \xE2\x80\x94 %1").arg(branch),
                intro, linkedWorktree, conflicted,
                QStringLiteral("Keep resolved edits"),
                [this, linkedWorktree, conflicted](QString *e) {
                    QStringList addArgs{"add", "--"};
                    addArgs.append(conflicted);
                    QStringList resetArgs{"reset", "--"};
                    resetArgs.append(conflicted);
                    return runGitCapture(linkedWorktree, addArgs, nullptr, e) &&
                           runGitCapture(linkedWorktree, resetArgs, nullptr, e);
                });
            if (!resolved) {
                setRepoDetailNotice(
                    QStringLiteral("Left %1's local-edit conflicts open; its "
                                   "autostash backup is still safe.")
                        .arg(branch),
                    true);
                return;
            }

            // `merge --autostash` retains its stash when application conflicts.
            // Once every marker has been resolved and the worktree contains the
            // result, remove only that known top autostash (never a user's stash).
            QByteArray latestStash;
            if (runGitCapture(linkedWorktree,
                              {"stash", "list", "-1", "--format=%gd%x09%gs"},
                              &latestStash, nullptr)) {
                const QString stash = QString::fromUtf8(latestStash).trimmed();
                const int tab = stash.indexOf(QLatin1Char('\t'));
                if (tab > 0 && stash.mid(tab + 1) == QLatin1String("autostash"))
                    runGitCapture(linkedWorktree,
                                  {"stash", "drop", stash.left(tab)}, nullptr,
                                  nullptr);
            }
            logSystem(QStringLiteral("Git: resolved and restored %1 local file(s) "
                                     "in %2 without committing them.")
                          .arg(conflicted.size())
                          .arg(branch));
            setRepoDetailNotice(
                QStringLiteral("Resolved %1 local file(s) in %2. The edits remain "
                               "uncommitted for the agent.")
                    .arg(conflicted.size())
                    .arg(branch));
            m_branchesCache.clear();
            if (m_branchDiffBranch == branch)
                showBranchDiff(branch);
            loadWorktreesPanel();
            return;
        }
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

    const bool inLinkedWorktree =
        !linkedWorktree.isEmpty() &&
        QDir(linkedWorktree).absolutePath() != QDir(dir).absolutePath();
    const QString mergeDir = inLinkedWorktree ? linkedWorktree : dir;

    // A dedicated agent worktree may be dirty by design, so protect it with
    // --autostash below. The shared checkout still uses the conservative clean
    // requirement because switching its branch would otherwise mix unrelated
    // repository work into this operation.
    QByteArray status;
    QString err;
    if (!runGitCapture(mergeDir, {"status", "--porcelain"}, &status, &err) ||
        (!inLinkedWorktree && !status.trimmed().isEmpty())) {
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
    const bool isCurrent = inLinkedWorktree ||
                           (!currentBranch.isEmpty() && branch == currentBranch);
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
    QStringList mergeArgs{"merge"};
    if (inLinkedWorktree)
        mergeArgs << "--autostash";
    mergeArgs << "--no-commit" << "--no-ff" << base;
    runGitCapture(mergeDir, mergeArgs, nullptr, &err);

    QByteArray unmerged;
    runGitCapture(mergeDir, {"diff", "--name-only", "--diff-filter=U"}, &unmerged,
                  nullptr);
    const QStringList conflicted =
        QString::fromUtf8(unmerged).split('\n', Qt::SkipEmptyParts);

    if (conflicted.isEmpty()) {
        // Clean merge — nothing to resolve; record it and report.
        if (!runGitCapture(mergeDir, {"commit", "--no-edit"}, nullptr, &err)) {
            runGitCapture(mergeDir, {"merge", "--abort"}, nullptr, nullptr);
            restoreBranch();
            setRepoDetailNotice(
                QStringLiteral("Could not merge %1 into %2: %3")
                    .arg(base, branch, err.left(200)),
                true);
            return;
        }
        restoreBranch();
        QByteArray restoredConflicts;
        runGitCapture(mergeDir, {"diff", "--name-only", "--diff-filter=U"},
                      &restoredConflicts, nullptr);
        if (!restoredConflicts.trimmed().isEmpty()) {
            setRepoDetailNotice(
                QStringLiteral("Updated %1 from %2. Its protected local edits now "
                               "need resolution — click Resolve local edits.")
                    .arg(branch, base),
                true);
            if (m_branchDiffBranch == branch)
                showBranchDiff(branch);
            return;
        }
        logSystem(QStringLiteral("Git: merged %1 into %2.").arg(base, branch));
        setRepoDetailNotice(
            QString::fromUtf8("Updated %1 with %2 \xE2\x80\x94 no conflicts.")
                .arg(branch, base));


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
        QString::fromUtf8("Merge editor \xE2\x80\x94 %1").arg(branch), intro,
        mergeDir, conflicted, QStringLiteral("Commit merge"),
        [this, mergeDir](QString *e) {
            return runGitCapture(mergeDir, {"add", "-A"}, nullptr, e) &&
                   runGitCapture(mergeDir, {"commit", "--no-edit"}, nullptr, e);
        });
    if (!committed) {
        runGitCapture(mergeDir, {"merge", "--abort"}, nullptr, nullptr);
        restoreBranch();
        setRepoDetailNotice(
            QStringLiteral("Cancelled the merge of %1 into %2; %2 was left unchanged.")
                .arg(base, branch));
        return;
    }
    restoreBranch();
    QByteArray restoredConflicts;
    runGitCapture(mergeDir, {"diff", "--name-only", "--diff-filter=U"},
                  &restoredConflicts, nullptr);
    if (!restoredConflicts.trimmed().isEmpty()) {
        setRepoDetailNotice(
            QStringLiteral("Updated %1 from %2. Its protected local edits now "
                           "need resolution — click Resolve local edits.")
                .arg(branch, base),
            true);
        if (m_branchDiffBranch == branch)
            showBranchDiff(branch);
        return;
    }
    logSystem(QStringLiteral("Git: merged %1 into %2 (conflicts resolved).")
                  .arg(base, branch));
    setRepoDetailNotice(QStringLiteral("Updated %1 with %2.").arg(branch, base));
    loadBranchesAndTags();
}





static QString branchMergeTree(const QString &dir, const QString &base,
                               const QString &branch)
{
    QByteArray out;
    if (!runGitCapture(dir, {"merge-tree", "--write-tree", branch, base}, &out,
                       nullptr))
        return QString();
    return QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts).value(0);
}









void MainWindow::startBranchConflictProbes(
    const QString &dir, const QString &base,
    const QList<QPair<QString, QString>> &probes)
{
    if (dir.isEmpty() || base.isEmpty() || probes.isEmpty())
        return;


    QList<QPair<QString, QString>> pending;
    for (const QPair<QString, QString> &probe : probes) {
        if (probe.second.isEmpty() || m_branchConflictProbes.contains(probe.first))
            continue;
        m_branchConflictProbes.insert(probe.first);
        pending.append(probe);
    }
    if (pending.isEmpty())
        return;

    auto verdicts = std::make_shared<QList<bool>>();
    QThread *worker = QThread::create([dir, base, pending, verdicts] {
        for (const QPair<QString, QString> &probe : pending)
            verdicts->append(branchMergeTree(dir, base, probe.first).isEmpty());
    });
    connect(worker, &QThread::finished, this,
            [this, worker, pending, verdicts, dir] {
                worker->deleteLater();
                for (const QPair<QString, QString> &probe : pending)
                    m_branchConflictProbes.remove(probe.first);



                if (m_branchConflictCache.size() > 512)
                    m_branchConflictCache.clear();
                const int answered = qMin(pending.size(), verdicts->size());
                for (int i = 0; i < answered; ++i)
                    m_branchConflictCache.insert(pending.at(i).second,
                                                 verdicts->at(i));




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
                        QTableWidgetItem *name = m_branchesTable->item(
                            row, kBranchesNameColumn);
                        QTableWidgetItem *status = m_branchesTable->item(
                            row, kBranchesStatusColumn);
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




    QByteArray headOut;
    QString currentBranch;
    if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &headOut, nullptr))
        currentBranch = QString::fromUtf8(headOut).trimmed();



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

        if (ahead == 0) {
            if (runGitCapture(dir, {"fetch", ".", base + ":" + branch}, nullptr,
                              nullptr))
                ++updated;
            else
                conflicts << branch;
            continue;
        }


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



    const bool claudeCode = provider == QLatin1String("claude-code");
    const bool claude = !claudeCode && agentIsClaudeProvider(provider);




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








    const QString branchWorktree = worktreePathForBranch(dir, branch);
    const bool inBranchWorktree = !branchWorktree.isEmpty();
    const QString mergeDir = inBranchWorktree ? branchWorktree : dir;


    QByteArray status;
    if (!runGitCapture(mergeDir, {"status", "--porcelain"}, &status, nullptr) ||
        !status.trimmed().isEmpty()) {
        setRepoDetailNotice(
            "Commit or stash local changes before fixing this branch.", true);
        return;
    }



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

        runGitCapture(mergeDir, {"merge", "--abort"}, nullptr, nullptr);
        if (!restoreBranch.isEmpty() && restoreBranch != branch)
            runGitCapture(mergeDir, {"checkout", restoreBranch}, nullptr, nullptr);
        setRepoDetailNotice(
            QStringLiteral("Could not merge %1 into %2: %3")
                .arg(base, branch, err.left(160)),
            true);
        return;
    }


    AgentSession session;
    session.owner = repo.owner;
    session.name = repo.name;
    session.issueNumber = 0;
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



    const QString href = url.toString(QUrl::FullyDecoded);
    if (m_branchDiffPullNumber >= 0 &&
        (href.startsWith(QLatin1String("thread:")) ||
         url.scheme() == QLatin1String("cmt") ||
         url.scheme() == QLatin1String("filecomment"))) {


        if (m_currentPullNumber != m_branchDiffPullNumber)
            showPull(m_branchDiffPullNumber);
        const int scroll =
            m_branchDiffView ? m_branchDiffView->verticalScrollBar()->value() : 0;
        onPullDiffAnchorClicked(url);
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


    const QString context = m_branchDiffViewedContext.isEmpty()
                                ? QStringLiteral("branch/") + m_branchDiffBranch
                                : m_branchDiffViewedContext;
    const QSet<QString> cur = loadDiffViewed(context);
    setDiffViewed(context, path, !cur.contains(path));
    const int scroll =
        m_branchDiffView ? m_branchDiffView->verticalScrollBar()->value() : 0;





    if (m_branchDiffLastValid)
        renderBranchDiffPatch(QString::fromUtf8(m_branchDiffLastPatch),
                              m_branchDiffLastEmpty, context);
    else
        renderBranchScopeDiff();
    if (m_branchDiffView)
        m_branchDiffView->verticalScrollBar()->setValue(scroll);
}





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

    if (viewTop <= fileTop + m_branchDiffSticky->sizeHint().height()) {
        m_branchDiffSticky->hide();
        return;
    }
    m_branchDiffSticky->show();
    m_branchDiffSticky->raise();
}





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
    QString currentFile;
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



bool MainWindow::branchScrollToAdjacentHunk(int delta)
{
    if (!m_branchDiffView)
        return false;
    QScrollBar *vbar = m_branchDiffView->verticalScrollBar();
    if (!vbar)
        return false;

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
        return false;
    vbar->setValue(std::clamp(target - 4, vbar->minimum(), vbar->maximum()));
    return true;
}



void MainWindow::toggleBranchDiffSearch(bool show)
{
    if (!m_branchDiffSearchBar || !m_branchDiffSearchInput)
        return;
    m_branchDiffSearchBar->setVisible(show);
    if (show) {
        m_branchDiffSearchInput->setFocus();
        m_branchDiffSearchInput->selectAll();
    } else {
        m_branchDiffSearchInput->clear();
        if (m_branchDiffView)
            m_branchDiffView->setFocus();
    }
}



void MainWindow::branchDiffSearchRecompute()
{
    if (!m_branchDiffView)
        return;
    m_branchDiffSearchMatches.clear();
    m_branchDiffSearchIndex = -1;

    const QString term =
        m_branchDiffSearchInput ? m_branchDiffSearchInput->text() : QString();
    if (!term.isEmpty()) {

        flushDiffStream(m_branchDiffView);
        QTextCursor cur = m_branchDiffView->document()->find(term);
        while (!cur.isNull()) {
            m_branchDiffSearchMatches.append(cur);
            if (m_branchDiffSearchMatches.size() >= 5000)
                break;
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
