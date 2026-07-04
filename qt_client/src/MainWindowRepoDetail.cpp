// MainWindowRepoDetail: MainWindow feature methods, split out of MainWindow.cpp.
// The repository detail view: file browser, overview/about, code search,
// navigation history, the commits list + commit detail/diff, and the Claude
// Code IDE-extension integration that opens repo content in the editor.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"

using namespace forkmesh::ui;

// ---- IDE extension integration --------------------------------------------
// The contract with the VS Code / Codeium extension lives under
// ~/.forkmesh/ide/ (see ide_extension/PROTOCOL.md). The extension heartbeats a
// registration file we poll, and watches a requests/ dir we drop tasks into.

static QString ideRegistryDir()
{
    return QDir::homePath() + QStringLiteral("/.forkmesh/ide");
}

// True when the extension is running (heartbeat within the last 90s). Fills
// ideName with the IDE's display name when known.
bool MainWindow::ideExtensionActive(QString *ideName) const
{
    QFile f(ideRegistryDir() + QStringLiteral("/registration.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    const qint64 ts = static_cast<qint64>(obj.value("ts").toDouble());
    if (QDateTime::currentMSecsSinceEpoch() - ts > 90'000)
        return false; // stale: the IDE is closed or the extension stopped
    if (ideName)
        *ideName = obj.value("ide").toString(QStringLiteral("your IDE"));
    return true;
}

// The toggle is on AND a live extension is detected.
bool MainWindow::ideIntegrationReady(QString *ideName) const
{
    if (!QSettings().value(kIdeIntegrationSetting, false).toBool())
        return false;
    return ideExtensionActive(ideName);
}

// Drop a task request the extension will pick up and run in a terminal.
void MainWindow::startIssueInIde(int issueNumber, const QString &title,
                                 const QString &provider)
{
    const int idx = issuesRepoIndex();
    if (idx < 0 || issueNumber < 0)
        return;
    const QString repoPath = writableRecordFor(m_repositories.at(idx)).localPath;
    if (repoPath.isEmpty()) {
        setIssueInlineNotice(
            QStringLiteral("This repo has no local checkout to run in."), true);
        return;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject req;
    req["id"] = id;
    req["ts"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    req["repoPath"] = repoPath;
    req["issueNumber"] = issueNumber;
    req["issueTitle"] = title;
    req["provider"] = provider;

    const QString dir = ideRegistryDir() + QStringLiteral("/requests");
    QDir().mkpath(dir);
    QFile out(dir + QStringLiteral("/") + id + QStringLiteral(".json"));
    if (!out.open(QIODevice::WriteOnly)) {
        setIssueInlineNotice(
            QStringLiteral("Could not write the IDE task request."), true);
        return;
    }
    out.write(QJsonDocument(req).toJson(QJsonDocument::Compact));
    out.close();
    QString ideName = QStringLiteral("the IDE");
    ideExtensionActive(&ideName);
    setIssueInlineNotice(QStringLiteral("Sent issue #%1 to %2 (%3)")
                             .arg(issueNumber)
                             .arg(ideName, provider == QLatin1String("claude")
                                               ? QStringLiteral("Claude Code")
                                               : QStringLiteral("Codex")),
                         false);
}

// Show/hide the issue-detail "run in IDE" buttons based on the toggle + whether
// a live extension is detected. Called whenever an issue is rendered.
void MainWindow::updateIssueIdeButtons()
{
    const bool ready = ideIntegrationReady();
    const bool haveIssue = m_currentIssueNumber > 0;
    if (m_issueIdeLabel)
        m_issueIdeLabel->setVisible(ready && haveIssue);
    if (m_issueIdeClaudeButton)
        m_issueIdeClaudeButton->setVisible(ready && haveIssue);
    if (m_issueIdeCodexButton)
        m_issueIdeCodexButton->setVisible(ready && haveIssue);
}

void MainWindow::updateIssueAgentUi(const Issue &issue)
{
    m_currentIssueTitle = issue.title;
    updateIssueIdeButtons();
    const AgentSession *session =
        issue.number > 0 ? latestAgentSessionForIssue(issue.number) : nullptr;
    if (m_issueAgentValue) {
        if (session) {
            m_issueAgentValue->setText(
                QStringLiteral("%1 session #%2<br>%3 · ~%4 credits · cost ~%5")
                    .arg(agentProviderName(session->provider))
                    .arg(session->id)
                    .arg(agentStatusText(session->status))
                    .arg(session->estimatedCredits)
                    .arg(agentCostText(session->costUsd)));
        } else {
            m_issueAgentValue->setText("No agent assigned");
        }
    }
    if (m_issueAgentViewButton)
        m_issueAgentViewButton->setVisible(session);
}

// Issue #145: drive the issue detail's "Files changed" tab. The tab appears only
// when the issue has a diff to show — preferring a linked agent session's live
// worktree branch diff (exactly the agent detail's view), falling back to a
// linked pull request's stored patch. Otherwise the tab stays hidden.
void MainWindow::refreshIssueFilesPanel(const Issue &issue)
{
    if (!m_issueDetailTabs || m_issueFilesTabIndex < 0)
        return;

    auto hideTab = [this] {
        if (m_issueFilesList)
            m_issueFilesList->clear();
        if (m_issueDiffView)
            m_issueDiffView->clear();
        if (m_issueDetailTabs->currentIndex() == m_issueFilesTabIndex)
            m_issueDetailTabs->setCurrentIndex(0);
        m_issueDetailTabs->setTabVisible(m_issueFilesTabIndex, false);
        m_issueDetailTabs->setTabText(m_issueFilesTabIndex,
                                      QStringLiteral("Files changed"));
    };

    if (issue.number <= 0) {
        hideTab();
        return;
    }

    // A linked agent session's branch gives a live worktree diff (committed +
    // uncommitted), measured against the same base its PR is built from.
    if (const AgentSession *s = latestAgentSessionForIssue(issue.number)) {
        const QString dir = sessionWorkdir(s->id);
        if (!dir.isEmpty()) {
            const QString base = sessionBaseRef(s->id);
            QStringList args{QStringLiteral("diff")};
            if (!base.isEmpty())
                args << base;
            m_issueDetailTabs->setTabVisible(m_issueFilesTabIndex, true);
            const int issueNo = issue.number;
            runGitDetached(dir, args,
                           [this, issueNo, dir, base](bool ok, const QByteArray &out) {
                               if (ok && issueNo == m_currentIssueNumber)
                                   renderIssueDiff(issueNo, out, dir, base);
                           });
            return;
        }
    }

    // Otherwise fall back to a linked pull request's patch (newest first). The PR
    // patch carries no git object context, so render it as a standalone diff.
    const QList<int> pulls = pullsLinkedToIssue(issue.number);
    for (auto it = pulls.crbegin(); it != pulls.crend(); ++it) {
        for (const PullRequest &pr : m_currentPulls) {
            if (pr.number == *it && !pr.patch.isEmpty()) {
                m_issueDetailTabs->setTabVisible(m_issueFilesTabIndex, true);
                renderIssueDiff(issue.number, pr.patch.toUtf8(), QString(), QString());
                return;
            }
        }
    }

    hideTab();
}

// Render a patch into the issue's Files-changed tab: lay out the diff, rebuild the
// per-file list with +/- counts and scroll anchors, and stamp the file count onto
// the tab header. Mirrors renderAgentDiff; a no-op once the selection has moved on
// so a late async (git diff) callback can't clobber another issue's panel.
void MainWindow::renderIssueDiff(int issueNumber, const QByteArray &patch,
                                 const QString &dir, const QString &base)
{
    if (!m_issueDiffView || issueNumber != m_currentIssueNumber)
        return;
    QList<DiffFileEntry> files;
    const QString html =
        renderDiffHtml(QString::fromUtf8(patch), files, dir, base, QString(),
                       QString(), QHash<QString, QString>(), QSet<QString>());
    setDiffHtml(m_issueDiffView,
        html.isEmpty()
            ? QStringLiteral("<p style='color:#8b949e'>No changes yet.</p>")
            : html);

    if (m_issueFilesList) {
        QSignalBlocker block(m_issueFilesList);
        m_issueFilesList->clear();
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
            const QString abs = dir.isEmpty() ? f.path : QDir(dir).filePath(f.path);
            item->setData(Qt::UserRole, abs);          // open on activate
            item->setData(Qt::UserRole + 1, f.anchor); // scroll diff on select
            item->setToolTip(QString::fromUtf8("%1 \xC2\xB7 %2").arg(f.status, f.path));
            m_issueFilesList->addItem(item);
        }
        fitFileListToWidestEntry(m_issueFilesList);
    }
    if (!m_issueDiffNav && m_issueFilesList)
        m_issueDiffNav = new DiffFileNavigator(m_issueDiffView, m_issueFilesList,
                                               Qt::UserRole + 1, this);
    if (m_issueDiffNav)
        m_issueDiffNav->rebuild(files, m_diffFontPt);

    const int n = files.size();
    if (m_issueDetailTabs && m_issueFilesTabIndex >= 0)
        m_issueDetailTabs->setTabText(
            m_issueFilesTabIndex,
            n > 0 ? QStringLiteral("Files changed (%1)").arg(n)
                  : QStringLiteral("Files changed"));
    if (m_issueFilesChangedSummary)
        m_issueFilesChangedSummary->setText(
            QStringLiteral("%1 file%2 changed").arg(n).arg(n == 1 ? "" : "s"));
}

// Defined further down (used by the status/agent code); declared here so the
// "Files" cell can gate its background refresh on whether the session is live.
// agentSessionActive() is a shared helper declared in MainWindowInternal.h.

// adhoc #151: fill the issue list's "Files" cell. Mirrors refreshIssueFilesPanel's
// source preference — a linked agent session's live worktree diff (counted async
// against the session base, cached per issue so a re-sort/rebuild shows it at
// once), else the newest linked PR's stored file count. No source -> a blank cell.
void MainWindow::populateIssueFilesCell(int row, const Issue &issue)
{
    auto *item = new SortTableWidgetItem(QString());
    item->setTextAlignment(Qt::AlignCenter);
    item->setData(kTableSortRole, -1); // no changes sorts before any real count
    m_issueTable->setItem(row, 15, item);

    // A linked agent session's worktree gives a live count (committed +
    // uncommitted) against the same base its PR is built from.
    if (const AgentSession *s = latestAgentSessionForIssue(issue.number)) {
        const QString dir = sessionWorkdir(s->id);
        if (!dir.isEmpty()) {
            // Show any cached count immediately so a rebuild/re-sort doesn't blank
            // the cell; refresh it in the background.
            const bool cached = m_issueFilesChangedCounts.contains(issue.number);
            if (cached)
                setIssueFilesCell(item, m_issueFilesChangedCounts.value(issue.number),
                                  QStringLiteral("worktree branch"));
            // Re-run only while the agent is active (the tree is changing) or when
            // the count isn't cached yet, to avoid spawning git on every rebuild.
            if (agentSessionActive(s) || !cached) {
                const QString base = sessionBaseRef(s->id);
                QStringList args{QStringLiteral("diff"), QStringLiteral("--name-only")};
                if (!base.isEmpty())
                    args << base;
                const int issueNo = issue.number;
                runGitDetached(dir, args,
                               [this, issueNo](bool ok, const QByteArray &out) {
                                   if (!ok)
                                       return;
                                   const QByteArray trimmed = out.trimmed();
                                   const int n =
                                       trimmed.isEmpty() ? 0 : trimmed.count('\n') + 1;
                                   m_issueFilesChangedCounts.insert(issueNo, n);
                                   applyIssueFilesCount(
                                       issueNo, n, QStringLiteral("worktree branch"));
                               });
            }
            return;
        }
    }

    // Otherwise fall back to the newest linked PR's stored file count.
    const QList<int> pulls = pullsLinkedToIssue(issue.number);
    for (auto it = pulls.crbegin(); it != pulls.crend(); ++it) {
        for (const PullRequest &pr : m_currentPulls) {
            if (pr.number == *it && pr.filesChanged > 0) {
                setIssueFilesCell(
                    item, pr.filesChanged,
                    QStringLiteral("pull request #%1").arg(pr.number));
                return;
            }
        }
    }
}

// Stamp a files-changed count onto an existing "Files" cell: an octicon + count
// when there are changes, a blank cell otherwise. Numeric sort via kTableSortRole.
void MainWindow::setIssueFilesCell(QTableWidgetItem *item, int count,
                                   const QString &source)
{
    if (!item)
        return;
    if (count > 0) {
        item->setText(QString::number(count));
        item->setIcon(themedOcticon("file-diff", QColor("#d29922"), 14));
        item->setToolTip(QStringLiteral("%1 file%2 changed via %3")
                             .arg(count)
                             .arg(count == 1 ? "" : "s", source));
    } else {
        item->setText(QString());
        item->setIcon(QIcon());
        item->setToolTip(QString());
    }
    item->setData(kTableSortRole, count);
}

// Apply an async files-changed count to whatever row currently holds the issue:
// the table may have re-sorted/rebuilt since the git diff was kicked off, so look
// the row up by issue number rather than trusting a stale row index.
void MainWindow::applyIssueFilesCount(int issueNumber, int count,
                                      const QString &source)
{
    if (!m_issueTable)
        return;
    for (int r = 0; r < m_issueTable->rowCount(); ++r) {
        QTableWidgetItem *numItem = m_issueTable->item(r, 0);
        if (numItem && numItem->data(Qt::UserRole).toInt() == issueNumber) {
            setIssueFilesCell(m_issueTable->item(r, 15), count, source);
            return;
        }
    }
}

QWidget *MainWindow::buildRepoFilesPanel()
{
    // Two modes: a GitHub-style overview, and an explorer+editor view. A
    // persistent segmented toggle sits above both so switching between
    // "Code overview" and "Explorer" is always one click away, no matter
    // which view is currently showing.
    m_filesStack = new QStackedWidget;
    m_filesStack->addWidget(buildRepoOverviewPage()); // 0 overview
    m_filesStack->addWidget(buildRepoEditorPage());   // 1 editor (explorer + tabs)

    m_filesModeOverviewButton = new QPushButton("Code overview");
    m_filesModeOverviewButton->setObjectName("repoTab");
    m_filesModeOverviewButton->setCheckable(true);
    m_filesModeOverviewButton->setChecked(true);
    m_filesModeOverviewButton->setCursor(Qt::PointingHandCursor);
    m_filesModeOverviewButton->setToolTip(
        "Show the repository overview (latest commit, file list and README)");
    setOcticon(m_filesModeOverviewButton, "code", 16);
    connect(m_filesModeOverviewButton, &QPushButton::clicked, this,
            [this] { showRepoOverview(); });

    m_filesModeExplorerButton = new QPushButton("Explorer");
    m_filesModeExplorerButton->setObjectName("repoTab");
    m_filesModeExplorerButton->setCheckable(true);
    m_filesModeExplorerButton->setCursor(Qt::PointingHandCursor);
    m_filesModeExplorerButton->setToolTip(
        "Open the file explorer and code editor");
    setOcticon(m_filesModeExplorerButton, "file-directory", 16);
    connect(m_filesModeExplorerButton, &QPushButton::clicked, this,
            [this] { showRepoEditor(); });

    // Git identity (name <email>) configured for the repo we're viewing, pinned
    // to the far right of this same row. Filled in by updateFooterGitIdentity()
    // each time a repo opens.
    m_footerGitIdentity = new QLabel;
    m_footerGitIdentity->setObjectName("footerGitIdentity");
    m_footerGitIdentity->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_footerGitIdentity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_footerGitIdentity->setToolTip(
        "Git author identity configured for the repository you're viewing");

    auto *modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(16, 6, 16, 0);
    modeRow->setSpacing(2);
    modeRow->addWidget(m_filesModeOverviewButton);
    modeRow->addWidget(m_filesModeExplorerButton);
    modeRow->addStretch();
    modeRow->addWidget(m_footerGitIdentity);

    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(modeRow);
    layout->addWidget(m_filesStack, 1);
    return panel;
}

QWidget *MainWindow::buildRepoOverviewPage()
{
    auto *page = new QWidget;

    // Latest-commit bar with a history button, like GitHub's commit strip.
    auto *commitCard = new QWidget;
    commitCard->setObjectName("commitBar");
    m_commitBar = new QLabel;
    m_commitBar->setObjectName("commitBarText");
    m_commitBar->setTextFormat(Qt::RichText);
    m_commitBar->setWordWrap(true);
    m_historyButton = new QPushButton("Commits");
    // Ghost-button look plus a scoped :checked highlight (see Theme.h) — the
    // shared #ghostButton style can't take a :checked rule without also
    // restyling the split-diff and Issues sub-tab toggles.
    m_historyButton->setObjectName("commitsToggle");
    m_historyButton->setCheckable(true); // lit while the commits panel shows
    m_historyButton->setCursor(Qt::PointingHandCursor);
    m_historyButton->setToolTip(
        "Show the full commit history below; click again for the file list");
    setOcticon(m_historyButton, "git-branch", 16);
    connect(m_historyButton, &QPushButton::clicked, this, [this](bool checked) {
        if (!checked) {
            showOverviewFiles();
            return;
        }
        showOverviewCommits();
        // Same deferred build the old Commits tab click ran: the panel paints
        // first, then the table work runs. Rebuilding an identical 300-row
        // table is the expensive part, so skip it when nothing changed.
        QTimer::singleShot(0, this, [this] {
            if (commitsListIsCurrent()) {
                // The list may be current, but the working tree can still have
                // moved (an agent staged/edited files) — always rescan the
                // changes panel so it's fresh on open.
                refreshSourceControl();
            } else {
                loadCommits();
            }
            // Land on the newest commit's change view, not an empty list.
            openMostRecentCommit();
        });
    });
    auto *commitRow = new QHBoxLayout(commitCard);
    commitRow->setContentsMargins(12, 8, 8, 8);
    commitRow->addWidget(m_commitBar, 1);
    commitRow->addWidget(m_historyButton);

    m_overviewCrumb = new QLabel;
    m_overviewCrumb->setObjectName("statusLine");
    m_overviewCrumb->setTextFormat(Qt::RichText);
    m_overviewCrumb->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(m_overviewCrumb, &QLabel::linkActivated, this,
            [this](const QString &href) {
                loadRepoOverview(href == "/" ? QString() : href);
            });

    m_overviewList = new QTreeWidget;
    m_overviewList->setObjectName("overviewList");
    m_overviewList->setColumnCount(6);
    m_overviewList->setHeaderLabels(
        {"Name", "Size", "LoC", "Files", "Last commit", "Updated"});
    m_overviewList->setRootIsDecorated(false);
    m_overviewList->setUniformRowHeights(true);
    m_overviewList->setSortingEnabled(false); // we sort the cached rows ourselves
    m_overviewList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_overviewList->setAllColumnsShowFocus(true);
    m_overviewList->header()->setStretchLastSection(false);
    m_overviewList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_overviewList->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_overviewList->setColumnWidth(1, 130);
    m_overviewList->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_overviewList->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_overviewList->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_overviewList->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    // Sorting is done by clicking the column headers. We keep Qt's own row
    // sorting off and reorder the cached rows ourselves (populateOverviewTree),
    // so folders stay grouped first by name and the ".." up-row stays pinned.
    m_overviewList->header()->setSectionsClickable(true);
    m_overviewList->header()->setSortIndicatorShown(true);
    m_overviewList->header()->setSortIndicator(0, Qt::AscendingOrder);
    connect(m_overviewList->header(), &QHeaderView::sortIndicatorChanged, this,
            [this](int col, Qt::SortOrder order) {
                // Map each column to the metric populateOverviewTree sorts on.
                static const char *const keys[] = {"name",  "size",    "loc",
                                                   "files", "subject", "updated"};
                m_overviewSortKey = QString::fromLatin1(
                    col >= 0 && col < 6 ? keys[col] : "name");
                m_overviewSortDesc = (order == Qt::DescendingOrder);
                populateOverviewTree();
            });
    enableHoverRowHighlight(m_overviewList);
    connect(m_overviewList, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) {
                const QString path = item->data(0, Qt::UserRole).toString();
                const int kind = item->data(0, Qt::UserRole + 1).toInt();
                if (kind == 1)
                    loadRepoOverview(path); // navigate into the directory (or up)
                else if (!path.isEmpty())
                    openRepoFile(path); // open the file (switches to editor view)
            });

    m_readmeView = new QTextBrowser;
    m_readmeView->setObjectName("readmeView");
    m_readmeView->setOpenExternalLinks(true);

    // Toolbar: branch switcher + tags + "go to file" search.
    m_branchButton = new QPushButton("main");
    m_branchButton->setObjectName("ghostButton");
    m_branchButton->setCursor(Qt::PointingHandCursor);
    m_branchButton->setToolTip("Switch branch");
    setOcticon(m_branchButton, "git-branch", 16);
    m_branchesButton = new QPushButton("0 branches");
    m_branchesButton->setObjectName("ghostButton");
    m_branchesButton->setCursor(Qt::PointingHandCursor);
    m_branchesButton->setToolTip(
        "Open the Branches panel to view, compare, switch and delete branches");
    setOcticon(m_branchesButton, "git-branch", 16);
    connect(m_branchesButton, &QPushButton::clicked, this, [this] {
        // Branches has no top-level tab anymore: its panel lives inside the Code
        // overview, under the toolbar, toggled by this button (same pattern as
        // the commit strip's "N Commits" toggle for the Commits panel).
        showOverviewBranches();
        loadBranchesPanel();
    });
    m_toolbarCommitsButton = new QPushButton("Commits");
    m_toolbarCommitsButton->setObjectName("ghostButton");
    m_toolbarCommitsButton->setCursor(Qt::PointingHandCursor);
    m_toolbarCommitsButton->setToolTip("Show the full commit history");
    setOcticon(m_toolbarCommitsButton, "history", 16);
    connect(m_toolbarCommitsButton, &QPushButton::clicked, this, [this] {
        // Reuse the commit strip's toggle: it already handles the deferred
        // list build (loadCommits/refreshSourceControl + openMostRecentCommit).
        if (m_historyButton && !m_historyButton->isChecked())
            m_historyButton->click();
    });
    m_tagsButton = new QPushButton("Tags");
    m_tagsButton->setObjectName("ghostButton");
    m_tagsButton->setCursor(Qt::PointingHandCursor);
    m_tagsButton->setToolTip("Open the Releases panel to create and manage tagged releases");
    setOcticon(m_tagsButton, "tag", 16);
    connect(m_tagsButton, &QPushButton::clicked, this, [this] {
        if (m_releasesTabIndex >= 0 && m_repoDetailTabs &&
            m_repoDetailTabs->button(m_releasesTabIndex)) {
            m_repoDetailTabs->button(m_releasesTabIndex)->setChecked(true);
            m_repoDetailStack->setCurrentIndex(m_releasesTabIndex);
            loadReleasesPanel();
        }
    });
    m_fileSearch = new QLineEdit;
    m_fileSearch->setPlaceholderText("Go to file\xE2\x80\xA6");
    m_fileSearch->setClearButtonEnabled(true);
    m_fileCompleter = new QCompleter(this);
    m_fileCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_fileCompleter->setFilterMode(Qt::MatchContains);
    m_fileCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_fileSearch->setCompleter(m_fileCompleter);
    connect(m_fileCompleter, QOverload<const QString &>::of(&QCompleter::activated),
            this, [this](const QString &path) {
                if (!path.isEmpty())
                    openRepoFile(path);
                m_fileSearch->clear();
            });
    // Switching into the explorer + editor view is handled by the persistent
    // "Explorer" toggle above the stack (see buildRepoFilesPanel).
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);
    toolbar->addWidget(m_branchButton);
    toolbar->addWidget(m_branchesButton);
    toolbar->addWidget(m_toolbarCommitsButton);
    toolbar->addWidget(m_tagsButton);
    toolbar->addWidget(m_fileSearch, 1);

    // Everything below the latest-commit bar swaps between the file browser
    // (crumb + file list + README) and the commits panel: the commit strip's
    // "N Commits" button toggles between them now that Commits no longer has
    // its own top-level tab.
    auto *filesBody = new QWidget;
    auto *filesBodyLayout = new QVBoxLayout(filesBody);
    filesBodyLayout->setContentsMargins(0, 0, 0, 0);
    filesBodyLayout->setSpacing(8);
    filesBodyLayout->addWidget(m_overviewCrumb);
    filesBodyLayout->addWidget(m_overviewList, 2);
    filesBodyLayout->addWidget(m_readmeView, 3);

    m_overviewBodyStack = new QStackedWidget;
    m_overviewBodyStack->addWidget(filesBody);             // 0 files + README
    m_overviewBodyStack->addWidget(buildRepoCommitsTab()); // 1 commit history
    m_overviewBodyStack->addWidget(buildBranchesTab());    // 2 branches panel

    // Left column: toolbar, latest commit, then the swappable body.
    auto *leftColumn = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addLayout(toolbar);
    leftLayout->addWidget(commitCard);
    leftLayout->addWidget(m_overviewBodyStack, 1);

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(16);
    body->addWidget(leftColumn, 1);
    body->addWidget(buildAboutSidebar());

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 10, 16, 16);
    layout->setSpacing(8);
    layout->addLayout(body);
    return page;
}

QWidget *MainWindow::buildAboutSidebar()
{
    auto *side = new QWidget;
    side->setObjectName("aboutSidebar");
    side->setFixedWidth(300);

    auto *aboutLabel = new QLabel("About");
    aboutLabel->setObjectName("aboutHeading");
    m_aboutEditButton = new QPushButton;
    m_aboutEditButton->setObjectName("aboutEditButton");
    m_aboutEditButton->setCursor(Qt::PointingHandCursor);
    m_aboutEditButton->setToolTip("Edit repository details");
    m_aboutEditButton->setFixedSize(28, 28);
    setOcticon(m_aboutEditButton, "gear", 15);
    connect(m_aboutEditButton, &QPushButton::clicked, this,
            &MainWindow::editRepoAbout);

    auto *aboutHeader = new QHBoxLayout;
    aboutHeader->setContentsMargins(0, 0, 0, 0);
    aboutHeader->setSpacing(6);
    aboutHeader->addWidget(aboutLabel);
    aboutHeader->addStretch();
    aboutHeader->addWidget(m_aboutEditButton);

    m_aboutText = new QLabel;
    m_aboutText->setObjectName("aboutText");
    m_aboutText->setWordWrap(true);
    m_aboutText->setTextFormat(Qt::RichText);
    m_aboutText->setOpenExternalLinks(true);
    m_aboutTopics = new QLabel;
    m_aboutTopics->setObjectName("statusLine");
    m_aboutTopics->setWordWrap(true);
    m_aboutTopics->setTextFormat(Qt::RichText);

    // On-disk repository size now lives on the "Code (N MB)" tab label again.

    // Community files (README / LICENSE / CONTRIBUTING …) rendered as links that
    // open the file in the overview, rather than escaping to a browser.
    m_aboutFiles = new QLabel;
    m_aboutFiles->setObjectName("statusLine");
    m_aboutFiles->setWordWrap(true);
    m_aboutFiles->setTextFormat(Qt::RichText);
    connect(m_aboutFiles, &QLabel::linkActivated, this,
            [this](const QString &href) { openRepoFile(href); });

    m_releaseHeader = new QLabel("LATEST RELEASE");
    m_releaseHeader->setObjectName("sectionLabel");
    m_releaseRow = new QLabel;
    m_releaseRow->setObjectName("statusLine");
    m_releaseRow->setWordWrap(true);
    m_releaseRow->setTextFormat(Qt::RichText);
    connect(m_releaseRow, &QLabel::linkActivated, this, [this](const QString &) {
        if (m_releasesTabIndex >= 0 && m_repoDetailTabs &&
            m_repoDetailTabs->button(m_releasesTabIndex)) {
            m_repoDetailTabs->button(m_releasesTabIndex)->setChecked(true);
            m_repoDetailStack->setCurrentIndex(m_releasesTabIndex);
            loadReleasesPanel();
        }
    });

    auto *langLabel = new QLabel("LANGUAGES");
    langLabel->setObjectName("sectionLabel");
    m_langBar = new QLabel;
    m_langBar->setObjectName("langBar");
    m_langBar->setFixedHeight(10);
    m_langBar->setTextFormat(Qt::RichText);
    m_langLegend = new QLabel;
    m_langLegend->setObjectName("statusLine");
    m_langLegend->setWordWrap(true);
    m_langLegend->setTextFormat(Qt::RichText);

    m_filesCountHeader = new QLabel("FILES");
    m_filesCountHeader->setObjectName("sectionLabel");
    m_filesCountRow = new QLabel;
    m_filesCountRow->setObjectName("statusLine");
    m_filesCountRow->setWordWrap(true);
    m_filesCountRow->setTextFormat(Qt::RichText);

    m_contributorsHeader = new QLabel("CONTRIBUTORS");
    m_contributorsHeader->setObjectName("sectionLabel");
    m_contributorsRow = new QLabel;
    m_contributorsRow->setObjectName("statusLine");
    m_contributorsRow->setWordWrap(true);
    m_contributorsRow->setTextFormat(Qt::RichText);

    // Thin hairline separators between sections for a cleaner, carded look.
    auto rule = [&side]() {
        auto *line = new QFrame(side);
        line->setObjectName("aboutRule");
        line->setFrameShape(QFrame::HLine);
        line->setFixedHeight(1);
        return line;
    };

    // Generous, even spacing so each section can breathe. One gap value is used
    // on both sides of every hairline rule for a consistent vertical rhythm.
    const int kSectionGap = 14;
    auto *layout = new QVBoxLayout(side);
    layout->setContentsMargins(22, 6, 20, 20);
    layout->setSpacing(8);
    layout->addLayout(aboutHeader);
    layout->addWidget(m_aboutText);
    layout->addWidget(m_aboutTopics);
    layout->addWidget(m_aboutFiles);
    layout->addSpacing(kSectionGap);
    layout->addWidget(rule());
    layout->addSpacing(kSectionGap);
    layout->addWidget(m_releaseHeader);
    layout->addWidget(m_releaseRow);
    layout->addSpacing(kSectionGap);
    layout->addWidget(rule());
    layout->addSpacing(kSectionGap);
    layout->addWidget(langLabel);
    layout->addWidget(m_langBar);
    layout->addWidget(m_langLegend);
    layout->addSpacing(kSectionGap);
    layout->addWidget(rule());
    layout->addSpacing(kSectionGap);
    layout->addWidget(m_filesCountHeader);
    layout->addWidget(m_filesCountRow);
    layout->addSpacing(kSectionGap);
    layout->addWidget(rule());
    layout->addSpacing(kSectionGap);
    layout->addWidget(m_contributorsHeader);
    layout->addWidget(m_contributorsRow);
    layout->addStretch();
    return side;
}

QWidget *MainWindow::buildRepoEditorPage()
{
    auto *page = new QWidget;

    auto *backRow = new QHBoxLayout;
    backRow->setContentsMargins(8, 4, 8, 0);
    // Returning to the GitHub-style overview is handled by the persistent
    // "Code overview" toggle above the stack (see buildRepoFilesPanel).
    backRow->addStretch();
    m_repoFileHistoryButton = new QPushButton("Show history");
    m_repoFileHistoryButton->setObjectName("ghostButton");
    m_repoFileHistoryButton->setCursor(Qt::PointingHandCursor);
    m_repoFileHistoryButton->setToolTip(
        "Show the commit history and changes for this file");
    setOcticon(m_repoFileHistoryButton, "history", 16);
    connect(m_repoFileHistoryButton, &QPushButton::clicked, this, [this] {
        QWidget *w = m_repoFileTabs ? m_repoFileTabs->currentWidget() : nullptr;
        const QString path = w ? w->property("previewPath").toString() : QString();
        if (!path.isEmpty())
            showRepoFileHistory(path);
    });
    // Markdown files get a tiny toggle that flips between the raw source and a
    // rendered preview (README.md, docs, any *.md / *.markdown).
    m_repoFilePreviewButton = new QPushButton("Preview");
    m_repoFilePreviewButton->setObjectName("ghostButton");
    m_repoFilePreviewButton->setCursor(Qt::PointingHandCursor);
    m_repoFilePreviewButton->setCheckable(true);
    m_repoFilePreviewButton->setToolTip("Preview rendered Markdown");
    setOcticon(m_repoFilePreviewButton, "eye", 16);
    connect(m_repoFilePreviewButton, &QPushButton::clicked, this,
            &MainWindow::toggleRepoFileMarkdownPreview);
    m_repoFileCommitButton = new QPushButton("Commit direct");
    m_repoFileCommitButton->setObjectName("ghostButton");
    m_repoFileCommitButton->setCursor(Qt::PointingHandCursor);
    m_repoFileCommitButton->setToolTip("Save this file and commit it directly to the default branch");
    setOcticon(m_repoFileCommitButton, "upload", 16);
    connect(m_repoFileCommitButton, &QPushButton::clicked, this,
            [this] { saveCurrentRepoFile(false); });
    m_repoFilePullButton = new QPushButton("Save as PR");
    m_repoFilePullButton->setObjectName("primaryButton");
    m_repoFilePullButton->setCursor(Qt::PointingHandCursor);
    m_repoFilePullButton->setToolTip("Save this file on a new branch and open a pull request");
    setOcticon(m_repoFilePullButton, "git-pull-request", 16);
    connect(m_repoFilePullButton, &QPushButton::clicked, this,
            [this] { saveCurrentRepoFile(true); });
    backRow->addWidget(m_repoFilePreviewButton);
    backRow->addWidget(m_repoFileHistoryButton);
    backRow->addWidget(m_repoFileCommitButton);
    backRow->addWidget(m_repoFilePullButton);

    m_repoFileTree = new QTreeWidget;
    m_repoFileTree->setObjectName("fileTree");
    enableHoverRowHighlight(m_repoFileTree);
    // Two columns: name (stretch) + a thin, right-aligned size column.
    m_repoFileTree->setColumnCount(2);
    m_repoFileTree->setHeaderHidden(true);
    m_repoFileTree->header()->setStretchLastSection(false);
    m_repoFileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_repoFileTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_repoFileTree->setMinimumWidth(200);
    m_repoFileTree->setIndentation(14);
    // Right-click a file or folder for IDE-style operations (new/rename/delete,
    // copy path, reveal) — see showRepoFileTreeMenu.
    m_repoFileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_repoFileTree, &QWidget::customContextMenuRequested, this,
            &MainWindow::showRepoFileTreeMenu);
    // Single-click a folder to expand/collapse it; single-click a file to
    // open it in an editable tab on the right.
    connect(m_repoFileTree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                if (item->data(0, Qt::UserRole + 1).toBool())
                    item->setExpanded(!item->isExpanded());
                else
                    openRepoFile(item->data(0, Qt::UserRole).toString());
            });
    // Double-click a file to open it in an editable tab on the right.
    connect(m_repoFileTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (item && !item->data(0, Qt::UserRole + 1).toBool())
                    openRepoFile(item->data(0, Qt::UserRole).toString());
            });
    connect(m_repoFileTree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) {
                if (item && item->data(0, Qt::UserRole + 1).toBool())
                    item->setIcon(0, iconForDir(true));
            });
    connect(m_repoFileTree, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) {
                if (item && item->data(0, Qt::UserRole + 1).toBool())
                    item->setIcon(0, iconForDir(false));
            });

    m_repoFileTabs = new QTabWidget;
    m_repoFileTabs->setObjectName("fileTabs");
    m_repoFileTabs->setDocumentMode(true);
    m_repoFileTabs->setMovable(true);
    m_repoFileTabs->setTabsClosable(true);
    connect(m_repoFileTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *w = m_repoFileTabs->widget(index);
        m_openFileTabs.remove(m_openFileTabs.key(w));
        m_repoFileTabs->removeTab(index);
        w->deleteLater();
        // With no files left open, return to the overview.
        if (m_repoFileTabs->count() == 0)
            showRepoOverview();
        updateRepoFileSaveActions();
    });
    connect(m_repoFileTabs, &QTabWidget::currentChanged, this,
            [this] { updateRepoFileSaveActions(); });

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName("filesSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_repoFileTree);
    splitter->addWidget(m_repoFileTabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 700});

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(backRow);
    layout->addWidget(splitter, 1);
    updateRepoFileSaveActions();
    return page;
}

QString MainWindow::iconsDir() const
{
    static bool resolved = false;
    static QString cached;
    if (resolved)
        return cached;
    resolved = true;
    const QString src = QStringLiteral(FORKMESH_SOURCE_DIR);
    if (!src.isEmpty()) {
        const QString candidate = QDir(src).absoluteFilePath("../icons");
        if (QDir(candidate).exists()) {
            cached = QDir(candidate).absolutePath();
            return cached;
        }
    }
    const QString beside = QCoreApplication::applicationDirPath() + "/icons";
    if (QDir(beside).exists())
        cached = beside;
    return cached;
}

QIcon MainWindow::iconForFile(const QString &fileName) const
{
    const QString dir = iconsDir();
    if (dir.isEmpty())
        return {};
    QString path = dir + "/" + fileTypeIconName(fileName.toLower()) + ".svg";
    if (!QFileInfo::exists(path))
        path = dir + "/default_file.svg";
    return QIcon(path);
}

QIcon MainWindow::iconForDir(bool opened) const
{
    const QString dir = iconsDir();
    if (dir.isEmpty())
        return {};
    return QIcon(dir + (opened ? "/default_folder_opened.svg" : "/default_folder.svg"));
}

QString MainWindow::repoGitDir() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return {};
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (!repo.localPath.isEmpty() && QDir(repo.localPath).exists())
        return repo.localPath;
    if (!repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists())
        return repo.mirrorPath;
    return {};
}

void MainWindow::setRepoDetailNotice(const QString &message, bool error)
{
    // Success/failure notices now surface as a compact, centered banner in the
    // top bar (between the breadcrumb and the bell) instead of an inline strip,
    // so the message is consistent everywhere in the app.
    if (m_repoDetailNotice) {
        m_repoDetailNotice->clear();
        m_repoDetailNotice->hide();
    }
    flashMessage(message, error);
}

void MainWindow::forkCurrentRepo()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord src = m_repositories.at(m_repoDetailIndex);

    // The fork keeps the same repository name and is owned by your own node, so
    // there's nothing to ask about that — we just need to know where on disk to
    // put it. Clone the fork from the existing local mirror when available (fast,
    // offline); otherwise from the repo's configured source.
    const QString owner = accountOwner();
    const QString name = repoSegment(src.name, QStringLiteral("repository"));
    if (owner.isEmpty() || name.isEmpty())
        return;
    for (const RepositoryRecord &r : std::as_const(m_repositories))
        if (!r.previewOnly && r.owner == owner && r.name == name) {
            QMessageBox::information(
                this, "Fork repository",
                QStringLiteral("You already have %1/%2.").arg(owner, name));
            return;
        }

    const QString source =
        (!src.mirrorPath.isEmpty() && QDir(src.mirrorPath).exists())
            ? src.mirrorPath
            : repositorySource(src);
    if (source.isEmpty()) {
        QMessageBox::warning(
            this, "Fork repository",
            "There is no local mirror or source to fork from yet. Sync the "
            "repository first, then fork.");
        return;
    }

    // --- Ask where to fork it to: pick a parent folder; the working copy lands
    // in a "<name>" subfolder inside it.
    const QString parentDir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose a folder to fork %1 into").arg(name),
        QDir::homePath());
    if (parentDir.isEmpty())
        return; // cancelled
    const QString targetDir = QDir(parentDir).absoluteFilePath(name);
    if (QDir(targetDir).exists() && !QDir(targetDir).isEmpty()) {
        QMessageBox::warning(
            this, "Fork repository",
            QStringLiteral("%1 already exists and isn't empty. Choose another "
                           "location for the fork.")
                .arg(QDir::toNativeSeparators(targetDir)));
        return;
    }

    RepositoryRecord fork;
    fork.owner = owner;
    fork.name = name;
    fork.description = src.description;
    fork.solanaAddress = savedSolanaAddress();
    fork.publishToNetwork = true;
    fork.actionsEnabled = src.actionsEnabled;
    fork.disabledWorkflows = src.disabledWorkflows;
    fork.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    fork.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(owner, QStringLiteral("owner")) + "-" +
                      repoSegment(name, QStringLiteral("repository")) + ".git";
    // The chosen folder is the fork's working directory; its origin points at our
    // own mirror (set up below), so it never auto-syncs from upstream — pushes to
    // it keep the branches you publish.
    fork.localPath = targetDir;

    m_repositories.append(fork);
    saveRepositories();
    refreshRepositoryList();
    logSystem(QStringLiteral("Forking %1/%2 to %3/%4 in %5 from %6")
                  .arg(src.owner, src.name, owner, name,
                       QDir::toNativeSeparators(targetDir), source));

    QDir().mkpath(QFileInfo(fork.mirrorPath).absolutePath());
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, owner, name, targetDir](int code, QProcess::ExitStatus) {
                const QString err =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                const int idx = repoIndexFor(owner, name);
                if (idx < 0)
                    return;
                if (code != 0) {
                    logSystem("Fork failed: " + err.right(300));
                    QMessageBox::warning(this, "Fork repository",
                                         "Could not clone the fork" +
                                             (err.isEmpty() ? QString()
                                                            : ":\n" + err.right(400)));
                    m_repositories.removeAt(idx);
                    saveRepositories();
                    refreshRepositoryList();
                    return;
                }
                RepositoryRecord &f = m_repositories[idx];
                // Detach from upstream so the fork is fully independent.
                QProcess::execute(QStringLiteral("git"),
                                  {QStringLiteral("-C"), f.mirrorPath,
                                   QStringLiteral("remote"), QStringLiteral("remove"),
                                   QStringLiteral("origin")});
                f.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                saveRepositories();
                ensurePushHook(f);
                if (m_backend)
                    m_backend->addChannel(repositoryChannel(f));
                publishRepository(idx, false);
                startRepoHosts();
                refreshRepositoryList();
                logSystem(QStringLiteral("Forked into %1/%2.").arg(owner, name));

                // Check the fork out into the folder the user picked. Its origin
                // is the local mirror, so commits pushed here update the fork.
                const QString mirrorPath = f.mirrorPath;
                QDir().mkpath(QFileInfo(targetDir).absolutePath());
                auto *checkout = new QProcess(this);
                connect(
                    checkout, &QProcess::finished, this,
                    [this, checkout, owner, name, targetDir](int wcode,
                                                             QProcess::ExitStatus) {
                        const QString werr =
                            QString::fromUtf8(checkout->readAllStandardError())
                                .trimmed();
                        checkout->deleteLater();
                        const int i = repoIndexFor(owner, name);
                        if (i < 0)
                            return;
                        if (wcode != 0) {
                            logSystem("Fork checkout failed: " + werr.right(300));
                            QMessageBox::warning(
                                this, "Fork repository",
                                "The fork was created, but checking it out into "
                                "the chosen folder failed" +
                                    (werr.isEmpty() ? QString()
                                                    : ":\n" + werr.right(400)));
                            m_repositories[i].localPath.clear();
                            saveRepositories();
                        } else {
                            logSystem(QStringLiteral("Checked out %1/%2 into %3.")
                                          .arg(owner, name,
                                               QDir::toNativeSeparators(targetDir)));
                        }
                        refreshRepositoryList();
                        openRepoDetail(i);
                    });
                checkout->start(QStringLiteral("git"),
                                {QStringLiteral("clone"), mirrorPath, targetDir});
            });
    process->start(QStringLiteral("git"),
                   {QStringLiteral("clone"), QStringLiteral("--mirror"), source,
                    fork.mirrorPath});
}

void MainWindow::downloadCurrentRepoZip()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        setRepoDetailNotice(
            "Sync this repository first; there is no local mirror to archive yet.",
            true);
        return;
    }

    const QString downloads =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString outDir = downloads.isEmpty() ? QDir::homePath() : downloads;
    QDir().mkpath(outDir);

    const QString safeOwner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString safeName = repoSegment(repo.name, QStringLiteral("repository"));
    const QString safeRef = repoSegment(currentRef(), QStringLiteral("head"));
    const QString base = safeOwner + "-" + safeName + "-" + safeRef;
    QString target = QDir(outDir).filePath(base + ".zip");
    for (int i = 2; QFileInfo::exists(target); ++i)
        target = QDir(outDir).filePath(base + "-" + QString::number(i) + ".zip");

    if (m_sourceButton) {
        m_sourceButton->setEnabled(false);
        m_sourceButton->setText("Zipping...");
    }
    setRepoDetailNotice("Creating ZIP archive in Downloads...");

    auto *process = new QProcess(this);
    auto handled = std::make_shared<bool>(false);
    auto finishButton = [this] {
        if (m_sourceButton) {
            m_sourceButton->setEnabled(true);
            m_sourceButton->setText("Source");
            setOcticon(m_sourceButton, "code", 16);
        }
    };
    connect(process, &QProcess::errorOccurred, this,
            [this, process, handled, finishButton](QProcess::ProcessError) {
                if (*handled)
                    return;
                *handled = true;
                finishButton();
                setRepoDetailNotice("Could not start git archive.", true);
                process->deleteLater();
            });
    connect(process, &QProcess::finished, this,
            [this, process, handled, finishButton, target](int code,
                                                           QProcess::ExitStatus status) {
                if (*handled)
                    return;
                *handled = true;
                const QString err =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                finishButton();
                if (status != QProcess::NormalExit || code != 0) {
                    setRepoDetailNotice(
                        "Could not create ZIP archive" +
                            (err.isEmpty() ? QString() : ": " + err.left(240)),
                        true);
                    return;
                }
                setRepoDetailNotice("Saved ZIP archive to " +
                                    QFileInfo(target).absoluteFilePath());
                logSystem("Saved repository ZIP to " + target);
            });
    process->start(
        QStringLiteral("git"),
        {QStringLiteral("-C"), dir, QStringLiteral("archive"),
         QStringLiteral("--format=zip"), QStringLiteral("-o"), target,
         QStringLiteral("--prefix=") + safeOwner + "-" + safeName + "/",
         currentRef()});
}

void MainWindow::openRepoDetail(int repoIndex)
{
    if (repoIndex < 0 || repoIndex >= m_repositories.size())
        return;
    // Guard against re-entrancy: a node switch yields the event loop between load
    // steps (see nodeSwitchStep), so a queued call must not start a second load
    // on top of this one.
    if (m_repoDetailLoading)
        return;
    m_repoDetailLoading = true;
    // The load below fires a dozen blocking git reads, any of which can take
    // seconds on a large repo. Keep the event loop alive across them so the
    // window doesn't freeze (and the WM doesn't flag it "Not Responding").
    GitKeepAlive keepAlive;
    m_repoDetailIndex = repoIndex;
    // Copy by value: the keep-alive pump services queued slots between git reads,
    // and a roster/network callback could mutate (and reallocate) m_repositories
    // mid-load — a reference into it would dangle.
    const RepositoryRecord repo = m_repositories.at(repoIndex);
    nodeSwitchStep(QStringLiteral("Reading %1/%2 metadata…")
                       .arg(repo.owner, repo.name));
    if (!repo.previewOnly)
        QSettings().setValue(kLastRepositorySetting, repo.owner + "/" + repo.name);
    if (m_repoHeaderTitle)
        m_repoHeaderTitle->setText(
            QStringLiteral("%1 / <b>%2</b>")
                .arg(repo.owner.toHtmlEscaped(), repo.name.toHtmlEscaped()));
    setRepoDetailNotice(QString());

    // Per-repo metadata (.forkmesh/info.json) + the branch we view; both feed
    // the loaders.
    m_repoInfo = RepoInfo();
    m_repoBranch.clear();
    loadRepoInfo();
    loadBranchesAndTags();
    if (m_forkButton)
        m_forkButton->setText(QStringLiteral("Fork %1").arg(formatCount(m_repoInfo.forks)));
    if (m_mirrorButton) {
        if (repo.previewOnly) {
            m_mirrorButton->setText(QStringLiteral("Mirror it"));
            m_mirrorButton->setToolTip(
                "Clone this preview into your local mirrors and host it");
        } else {
            m_mirrorButton->setText(
                QStringLiteral("Mirror %1").arg(qMax(1, m_repoInfo.mirrors)));
            m_mirrorButton->setToolTip("Sync this repository's mirror now");
        }
    }
    updateRepoDetailStatus();
    updateRepoActionMenus();
    refreshRepoSettings();
    updateRepoCodeSize();
    updateFooterGitIdentity();

    // Point the embedded issues UI at this repo (its combo is hidden).
    refreshIssuesRepoCombo();
    if (m_issuesRepoCombo) {
        const int combo = m_issuesRepoCombo->findData(repoIndex);
        if (combo >= 0)
            m_issuesRepoCombo->setCurrentIndex(combo);
    }
    // Default the Agents-tab compose row's repo picker (adhoc #234) to the repo
    // being opened, so a prompt started there runs in the repo on screen.
    if (m_agentComposeRepo) {
        const int combo = m_agentComposeRepo->findData(repoIndex);
        if (combo >= 0)
            m_agentComposeRepo->setCurrentIndex(combo);
    }
    logStartup(QStringLiteral("  openRepo: info+branches+codeSize done"));
    // Pulls load before agents on purpose: the Agents table annotates each
    // session with its PR status (open/merged/closed) read from m_currentPulls,
    // so loading pulls first lets a single reloadAgents() render the right state.
    // (Previously pulls came last and the Agents tab paid for a second full
    // reloadAgents() — the heaviest per-open step, a git probe per session.)
    nodeSwitchStep(QStringLiteral("Loading pull requests…"));
    m_currentPulls = pullStoreForCurrentRepo().loadAll();
    updateRepoPullCount();
    logStartup(QStringLiteral("  openRepo: pulls loaded"));
    nodeSwitchStep(QStringLiteral("Loading issues & agents…"));
    reloadIssues();
    reloadAgents();
    updateRepoIssueCount();
    logStartup(QStringLiteral("  openRepo: issues+agents loaded"));
    m_currentDiscussions.clear();
    reloadDiscussions();
    updateRepoDiscussionCount();

    // Land on the user's preferred default tab (Settings → General; Agents by
    // default). Each candidate tab's data was eagerly loaded above, so we only
    // need to select it. Reset the editor tabs/tree for the new repo.
    const int defaultTab = defaultRepoTabIndex();
    // "Commits" (1) lives inside the Code overview now, under the latest-commit
    // bar — land on Code and swap the overview body to the commits panel below.
    const int landingTab = defaultTab == 1 ? 0 : defaultTab;
    if (m_repoDetailTabs && m_repoDetailTabs->button(landingTab))
        m_repoDetailTabs->button(landingTab)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(landingTab);
    if (m_repoFileTabs) {
        m_repoFileTabs->clear();
        m_openFileTabs.clear();
    }
    if (m_repoFileTree)
        m_repoFileTree->clear();
    m_treeLoadedForIndex = -1;

    // The file-search completer is only consulted once the user starts typing a
    // path, and ls-tree -r is the single heaviest git read on a large repo —
    // build it just after the repo paints so it never delays first view. Re-check
    // the index so a fast follow-up switch doesn't index the wrong repo.
    // loadFileSearchIndex() itself is fully async (runGitDetached) now, so no
    // GitKeepAlive scope is needed here — StallWatchdog still caught this ls-tree
    // stalling the GUI thread even under a keep-alive poll, because the pumped
    // event loop can land on an unrelated slow paint (see loadFileSearchIndex).
    const int searchIndexFor = m_repoDetailIndex;
    QTimer::singleShot(0, this, [this, searchIndexFor] {
        if (m_repoDetailIndex != searchIndexFor)
            return;
        loadFileSearchIndex();
    });
    nodeSwitchStep(QStringLiteral("Loading README & about…"));
    loadAboutSidebar();
    logStartup(QStringLiteral("  openRepo: about sidebar loaded"));
    nodeSwitchStep(QStringLiteral("Loading commit history…"));
    // Building the commit table is the single heaviest piece of per-open UI work
    // (up to 300 rows, each with cell widgets, plus several git reads). Most opens
    // land on Agents/Code and never show it, so only build it when Commits is the
    // landing tab; otherwise just refresh the cheap "Commits (N)" badge and let
    // the tab-click handler build the table on demand. The previous repo's rows
    // and cached tip are cleared so commitsListIsCurrent() forces a rebuild for
    // this repo when its Commits tab is first opened.
    if (defaultTab == 1) {
        loadCommits();
        showOverviewCommits();
    } else {
        // A previously open repo may have left the commits panel showing.
        showOverviewFiles();
        updateRepoCommitCount();
        if (m_commitsTable)
            m_commitsTable->setRowCount(0);
        m_commitsLoadedTip.clear();
        // Invalidate any still-pending stat fill from the repo we just left, so it
        // doesn't run a --numstat read against this repo and write into rows we
        // just cleared.
        ++m_commitsLoadGen;
    }
    logStartup(QStringLiteral("  openRepo: commits loaded"));
    // Insights (contributor stats, git shortlog) are computed lazily when the
    // Insights tab is opened — see the tab-switch handler — so opening a repo
    // doesn't pay for them up front.
    // Land on the GitHub-style overview at the repo root by default; the
    // explorer + editor is one click away via the persistent "Explorer" toggle.
    nodeSwitchStep(QStringLiteral("Rendering overview…"));
    loadRepoOverview(QString());
    logStartup(QStringLiteral("  openRepo: overview loaded"));
    showRepoOverview();
    // The detail panel lives inside Home next to the columns now, so just make
    // sure Home is the active section and refresh the breadcrumb.
    showSection(0);
    updateBreadcrumb();
    // Populate the workflow list now so the "Actions (N)" badge is correct from
    // the start, rather than reading 0 until the Actions tab is first opened.
    refreshRepoActions();
    updateActionsTabIndicator(); // reflect any in-flight runs for this repo
    refreshRepoPinBanner();      // warn if the relay's integrity pin is stale
    m_repoDetailLoading = false;
}

void MainWindow::updateRepoCodeSize()
{
    if (!m_repoCodeTab)
        return;
    auto showSize = [this](const QString &size) {
        m_repoCodeTab->setText(QStringLiteral("Code (%1)").arg(size)); // Code (N MB)
    };
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        showSize(QStringLiteral("0 B"));
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (repo.mirrorPath.isEmpty() || !QDir(repo.mirrorPath).exists()) {
        showSize(QStringLiteral("0 B"));
        return;
    }

    // `git count-objects -v` walks the whole object store; on a large mirror
    // that single call was the blocking git read StallWatchdog caught freezing
    // the GUI thread from inside openRepoDetail's eager (non-deferred) load —
    // even under GitKeepAlive, whose polling pump can itself get stuck behind a
    // slow paint. Fetch it fully off the GUI thread instead (see
    // runGitDetached) and only apply the result if still showing this repo.
    showSize(QStringLiteral("…"));
    const int forIndex = m_repoDetailIndex;
    runGitDetached(repo.mirrorPath, {"count-objects", "-v"},
                   [this, forIndex, showSize](bool ok, const QByteArray &out) {
                       if (forIndex != m_repoDetailIndex || !m_repoCodeTab)
                           return;
                       showSize(ok ? formatByteSize(parseCountObjectsSizeBytes(out))
                                   : QStringLiteral("0 B"));
                   });
}

void MainWindow::updateRepoCommitCount()
{
    // The count badge lives on the commit strip's "N Commits" toggle now that
    // the top-bar Commits tab is gone (same text loadRepoOverview renders).
    if (!m_historyButton)
        return;
    const QString dir = repoGitDir();
    QByteArray out;
    int count = 0;
    if (!dir.isEmpty() &&
        runGitCapture(dir, {"rev-list", "--count", currentRef()}, &out, nullptr))
        count = QString::fromUtf8(out).trimmed().toInt();
    m_historyButton->setText(
        count > 0 ? QStringLiteral("%1 Commits").arg(formatCount(count))
                  : QStringLiteral("Commits"));
}

void MainWindow::updateRepoIssueCount()
{
    if (m_repoIssuesTab) {
        int openCount = 0;
        for (const Issue &issue : std::as_const(m_currentIssues)) {
            if (issue.status != "closed")
                ++openCount;
        }
        m_repoIssuesTab->setText(
            QStringLiteral("Issues (%1)").arg(formatCount(openCount)));
    }
    // Opening a repo (or reloading its issues) runs here, so it's the reliable
    // funnel for revealing the looper toggle above the Issues tab even when the
    // loop is off — updateIssueLooperButton only fires on a state change (#130).
    positionLooperToggle();
}

void MainWindow::updateRepoDiscussionCount()
{
    if (m_repoDiscussionsTab)
        m_repoDiscussionsTab->setText(
            QStringLiteral("Discussions (%1)")
                .arg(formatCount(m_currentDiscussions.size())));
}

void MainWindow::updateRepoPullCount()
{
    if (m_repoPullsTab)
        m_repoPullsTab->setText(
            QStringLiteral("Pull requests (%1)").arg(formatCount(m_currentPulls.size())));
}

void MainWindow::loadRepoFileTree()
{
    if (!m_repoFileTree)
        return;
    m_repoFileTree->clear();

    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        new QTreeWidgetItem(m_repoFileTree,
                            {"No local copy of this repository to browse."});
        return;
    }
    QByteArray out;
    QString err;
    // One recursive listing of every tracked path, with blob sizes (-l); we build
    // the hierarchy and a per-folder size total below. Records look like
    // "<mode> <type> <object> <size>\t<path>" (size is "-" for submodules).
    if (!runGitCapture(dir, {"ls-tree", "-r", "-l", "-z", currentRef()}, &out,
                       &err)) {
        new QTreeWidgetItem(m_repoFileTree,
                            {err.isEmpty() ? "This repository has no commits yet."
                                           : "Could not read files: " + err.left(120)});
        return;
    }

    QStringList paths;
    QHash<QString, qint64> fileSize; // path -> blob size in bytes
    QHash<QString, qint64> dirSize;  // accumulated folder path -> total bytes
    for (const QByteArray &record : out.split('\0')) {
        if (record.isEmpty())
            continue;
        const int tab = record.indexOf('\t');
        if (tab < 0)
            continue;
        const QString path = QString::fromUtf8(record.mid(tab + 1));
        // Coves are encrypted vaults whose slug-based filename leaks their name to
        // anyone browsing the tree (including mirror nodes that can't unlock them).
        // Keep them out of the file browser entirely — and out of folder-size totals
        // — so they reveal nothing here; they live in Settings → Coves. (#231)
        if (path.endsWith(QStringLiteral(".cove"), Qt::CaseInsensitive) &&
            path.startsWith(CoveStore::covesDirRel() + "/"))
            continue;
        const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
        const qint64 size =
            meta.size() >= 4 ? QString::fromUtf8(meta.at(3)).toLongLong() : 0;
        paths << path;
        fileSize.insert(path, size);
        // Add this file's size to every ancestor folder for a folder total.
        const QStringList parts = path.split('/', Qt::SkipEmptyParts);
        QString acc;
        for (int i = 0; i + 1 < parts.size(); ++i) {
            acc = acc.isEmpty() ? parts.at(i) : acc + "/" + parts.at(i);
            dirSize[acc] += size;
        }
    }
    paths.sort(Qt::CaseInsensitive);

    // Right-aligned, muted size text in column 1 for both files and folders.
    auto setSize = [](QTreeWidgetItem *item, qint64 bytes) {
        item->setText(1, formatByteSize(bytes));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setForeground(1, QBrush(QColor("#8b949e")));
    };

    QHash<QString, QTreeWidgetItem *> dirs; // accumulated path -> directory node
    for (const QString &path : std::as_const(paths)) {
        const QStringList parts = path.split('/', Qt::SkipEmptyParts);
        QString acc;
        QTreeWidgetItem *parent = nullptr;
        for (int i = 0; i < parts.size(); ++i) {
            acc = acc.isEmpty() ? parts.at(i) : acc + "/" + parts.at(i);
            const bool isLast = (i == parts.size() - 1);
            if (isLast) {
                auto *item = parent ? new QTreeWidgetItem(parent)
                                    : new QTreeWidgetItem(m_repoFileTree);
                item->setText(0, parts.at(i));
                item->setIcon(0, iconForFile(parts.at(i)));
                item->setData(0, Qt::UserRole, acc);
                item->setData(0, Qt::UserRole + 1, false);
                setSize(item, fileSize.value(acc));
            } else {
                QTreeWidgetItem *node = dirs.value(acc);
                if (!node) {
                    node = parent ? new QTreeWidgetItem(parent)
                                  : new QTreeWidgetItem(m_repoFileTree);
                    node->setText(0, parts.at(i));
                    node->setIcon(0, iconForDir(false));
                    node->setData(0, Qt::UserRole, acc);
                    node->setData(0, Qt::UserRole + 1, true);
                    setSize(node, dirSize.value(acc));
                    dirs.insert(acc, node);
                }
                parent = node;
            }
        }
    }

    // Folders first, then files, alphabetically — at every level.
    std::function<void(QTreeWidgetItem *)> sortChildren = [&](QTreeWidgetItem *parent) {
        const auto kids = parent->takeChildren();
        QList<QTreeWidgetItem *> sorted = kids;
        std::sort(sorted.begin(), sorted.end(),
                  [](QTreeWidgetItem *a, QTreeWidgetItem *b) {
                      const bool ad = a->data(0, Qt::UserRole + 1).toBool();
                      const bool bd = b->data(0, Qt::UserRole + 1).toBool();
                      if (ad != bd)
                          return ad;
                      return a->text(0).toLower() < b->text(0).toLower();
                  });
        for (QTreeWidgetItem *child : std::as_const(sorted)) {
            parent->addChild(child);
            sortChildren(child);
        }
    };
    sortChildren(m_repoFileTree->invisibleRootItem());

    if (paths.isEmpty())
        new QTreeWidgetItem(m_repoFileTree, {"(empty repository)"});
}

// Reject a repo-relative path that would escape the repository or touch .git.
static bool repoRelPathIsSafe(const QString &cleanPath)
{
    return !cleanPath.isEmpty() && cleanPath != QLatin1String(".") &&
           !cleanPath.startsWith("../") && !cleanPath.contains("/../") &&
           !QDir::isAbsolutePath(cleanPath) && cleanPath != QLatin1String(".git") &&
           !cleanPath.startsWith(".git/");
}

// Right-click context menu for the file-explorer tree. Files/folders get
// IDE-style operations; an empty-area or placeholder click targets the repo
// root (so the first file can be added to an empty working tree). New/rename/
// delete commit directly to the default branch and only appear when this node
// owns a working tree; copy-path and reveal work on any locally available repo.
void MainWindow::showRepoFileTreeMenu(const QPoint &pos)
{
    if (!m_repoFileTree)
        return;
    QTreeWidgetItem *item = m_repoFileTree->itemAt(pos);
    const QString path = item ? item->data(0, Qt::UserRole).toString() : QString();
    const bool isDir = item && item->data(0, Qt::UserRole + 1).toBool();
    // Placeholder rows ("(empty repository)", error notes) carry no path.
    const bool isEntry = item && !path.isEmpty();
    const bool canWrite = repoHasWorkingTree();
    // The folder a new entry is created in: the clicked folder, the clicked
    // file's parent folder, or the repo root.
    const QString parentDir =
        !isEntry ? QString() : (isDir ? path : path.section('/', 0, -2));

    QMenu menu(this);
    QAction *open = (isEntry && !isDir) ? menu.addAction(QStringLiteral("Open"))
                                        : nullptr;

    QAction *newFile = nullptr;
    QAction *newFolder = nullptr;
    QAction *rename = nullptr;
    QAction *del = nullptr;
    if (canWrite) {
        if (open)
            menu.addSeparator();
        newFile = menu.addAction(QString::fromUtf8("New file\xE2\x80\xA6"));
        newFolder = menu.addAction(QString::fromUtf8("New folder\xE2\x80\xA6"));
        if (isEntry) {
            menu.addSeparator();
            rename = menu.addAction(QString::fromUtf8("Rename\xE2\x80\xA6"));
            del = menu.addAction(QStringLiteral("Delete"));
        }
    }

    QAction *copyRel = nullptr;
    QAction *copyAbs = nullptr;
    QAction *reveal = nullptr;
    if (isEntry) {
        menu.addSeparator();
        copyRel = menu.addAction(QStringLiteral("Copy relative path"));
        // The on-disk path and reveal only make sense for a real working tree;
        // a bare mirror has no checked-out files.
        if (canWrite) {
            copyAbs = menu.addAction(QStringLiteral("Copy full path"));
            reveal = menu.addAction(isDir ? QStringLiteral("Reveal folder")
                                          : QStringLiteral("Reveal in file manager"));
        }
    }

    if (menu.isEmpty())
        return;
    QAction *chosen = menu.exec(m_repoFileTree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == open)
        openRepoFile(path);
    else if (chosen == newFile)
        newRepoFileEntry(parentDir, false);
    else if (chosen == newFolder)
        newRepoFileEntry(parentDir, true);
    else if (chosen == rename)
        renameRepoFileEntry(path, isDir);
    else if (chosen == del)
        deleteRepoFileEntry(path, isDir);
    else if (chosen == copyRel)
        QApplication::clipboard()->setText(path);
    else if (chosen == copyAbs)
        QApplication::clipboard()->setText(QDir(repoGitDir()).filePath(path));
    else if (chosen == reveal) {
        const QString full = QDir(repoGitDir()).filePath(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            isDir ? full : QFileInfo(full).absolutePath()));
    }
}

QString MainWindow::prepareRepoFileOp(QString *base)
{
    if (!repoHasWorkingTree()) {
        setRepoDetailNotice(
            "File operations need a local working copy of this repository.", true);
        return {};
    }
    const QString dir = repoGitDir();
    QByteArray status;
    QString err;
    if (!runGitCapture(dir, {"status", "--porcelain"}, &status, &err) ||
        !status.trimmed().isEmpty()) {
        setRepoDetailNotice(
            err.isEmpty() ? "Commit or stash local changes before changing files."
                          : err.left(240),
            true);
        return {};
    }
    const QString branch = repoDefaultBranch(repoBranches());
    if (branch.isEmpty()) {
        setRepoDetailNotice("This repository has no branch to commit onto.", true);
        return {};
    }
    if (!runGitCapture(dir, {"checkout", branch}, nullptr, &err)) {
        setRepoDetailNotice(
            QStringLiteral("Could not check out %1: %2").arg(branch, err.left(200)),
            true);
        return {};
    }
    if (base)
        *base = branch;
    return dir;
}

void MainWindow::finishRepoFileOp(const QString &base)
{
    setRepoBranch(base);
    // Stay on whichever files-panel page the user was on (the explorer, normally)
    // across the heavyweight refresh.
    const int filesPage = m_filesStack ? m_filesStack->currentIndex() : 0;
    refreshOpenRepoDetail();
    // The explorer is the active view; rebuild it now rather than lazily so the
    // change is visible immediately.
    loadRepoFileTree();
    m_treeLoadedForIndex = m_repoDetailIndex;
    if (m_filesStack)
        m_filesStack->setCurrentIndex(filesPage);
}

void MainWindow::newRepoFileEntry(const QString &parentDir, bool folder)
{
    bool ok = false;
    const QString label = folder ? QStringLiteral("New folder")
                                  : QStringLiteral("New file");
    const QString name =
        QInputDialog::getText(this, label,
                              folder ? QStringLiteral("Folder name:")
                                     : QStringLiteral("File name:"),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return;
    const QString rel =
        QDir::cleanPath(parentDir.isEmpty() ? name : parentDir + "/" + name);
    if (!repoRelPathIsSafe(rel)) {
        setRepoDetailNotice("Refusing to create that path.", true);
        return;
    }
    // git can't track an empty folder, so seed a new folder with a .gitkeep; the
    // commit then has content and the folder appears in the tree.
    const QString trackRel = folder ? rel + "/.gitkeep" : rel;

    QString base;
    const QString dir = prepareRepoFileOp(&base);
    if (dir.isEmpty())
        return;
    const QString full = QDir(dir).filePath(trackRel);
    if (QFileInfo::exists(full)) {
        setRepoDetailNotice(QStringLiteral("%1 already exists.").arg(rel), true);
        return;
    }
    QDir().mkpath(QFileInfo(full).absolutePath());
    QFile file(full);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setRepoDetailNotice(QStringLiteral("Could not create %1.").arg(rel), true);
        return;
    }
    file.close();

    QString err;
    if (!runGitCapture(dir, {"add", "--", trackRel}, nullptr, &err) ||
        !runGitCapture(dir, {"commit", "-m", QStringLiteral("Add %1").arg(rel)},
                       nullptr, &err)) {
        file.remove();
        runGitCapture(dir, {"reset", "--hard"}, nullptr, nullptr);
        setRepoDetailNotice(err.isEmpty() ? "Could not commit the new file."
                                          : err.left(240),
                            true);
        return;
    }
    logSystem(QStringLiteral("Created %1 in %2.").arg(rel, base));
    setRepoDetailNotice(QStringLiteral("Created %1.").arg(rel));
    finishRepoFileOp(base);
    if (!folder)
        openRepoFile(rel);
}

void MainWindow::renameRepoFileEntry(const QString &path, bool isDir)
{
    bool ok = false;
    const QString oldName = path.section('/', -1);
    const QString newName =
        QInputDialog::getText(this, QStringLiteral("Rename"),
                              QStringLiteral("New name for \"%1\":").arg(oldName),
                              QLineEdit::Normal, oldName, &ok)
            .trimmed();
    if (!ok || newName.isEmpty() || newName == oldName)
        return;
    if (newName.contains('/')) {
        setRepoDetailNotice("Enter a name, not a path.", true);
        return;
    }
    const QString parent = path.section('/', 0, -2);
    const QString dest =
        QDir::cleanPath(parent.isEmpty() ? newName : parent + "/" + newName);
    if (!repoRelPathIsSafe(dest)) {
        setRepoDetailNotice("Refusing to rename to that path.", true);
        return;
    }

    QString base;
    const QString dir = prepareRepoFileOp(&base);
    if (dir.isEmpty())
        return;
    if (QFileInfo::exists(QDir(dir).filePath(dest))) {
        setRepoDetailNotice(QStringLiteral("%1 already exists.").arg(dest), true);
        return;
    }
    QString err;
    if (!runGitCapture(dir, {"mv", "--", path, dest}, nullptr, &err) ||
        !runGitCapture(dir,
                       {"commit", "-m",
                        QStringLiteral("Rename %1 to %2").arg(path, dest)},
                       nullptr, &err)) {
        runGitCapture(dir, {"reset", "--hard"}, nullptr, nullptr);
        setRepoDetailNotice(err.isEmpty() ? "Could not rename." : err.left(240),
                            true);
        return;
    }
    closeRepoFileTabsUnder(path, isDir);
    logSystem(QStringLiteral("Renamed %1 to %2.").arg(path, dest));
    setRepoDetailNotice(QStringLiteral("Renamed to %1.").arg(dest));
    finishRepoFileOp(base);
}

void MainWindow::deleteRepoFileEntry(const QString &path, bool isDir)
{
    if (QMessageBox::question(
            this, QStringLiteral("Delete"),
            QStringLiteral("Delete %1 \"%2\" from the repository? The removal is "
                           "committed to the default branch.")
                .arg(isDir ? QStringLiteral("folder") : QStringLiteral("file"),
                     path),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString base;
    const QString dir = prepareRepoFileOp(&base);
    if (dir.isEmpty())
        return;
    QStringList args = {"rm", "-q"};
    if (isDir)
        args << "-r";
    args << "--" << path;
    QString err;
    if (!runGitCapture(dir, args, nullptr, &err) ||
        !runGitCapture(dir, {"commit", "-m", QStringLiteral("Delete %1").arg(path)},
                       nullptr, &err)) {
        runGitCapture(dir, {"reset", "--hard"}, nullptr, nullptr);
        setRepoDetailNotice(err.isEmpty() ? "Could not delete." : err.left(240),
                            true);
        return;
    }
    closeRepoFileTabsUnder(path, isDir);
    logSystem(QStringLiteral("Deleted %1 from %2.").arg(path, base));
    setRepoDetailNotice(QStringLiteral("Deleted %1.").arg(path));
    finishRepoFileOp(base);
}

// Close open editor tabs pointing at a path that was renamed or deleted (and,
// for a folder, everything beneath it) so no stale tab keeps a gone file open.
void MainWindow::closeRepoFileTabsUnder(const QString &path, bool isDir)
{
    if (!m_repoFileTabs)
        return;
    const QString prefix = path + "/";
    const QList<QString> openPaths = m_openFileTabs.keys();
    for (const QString &p : openPaths) {
        if (p != path && !(isDir && p.startsWith(prefix)))
            continue;
        QWidget *w = m_openFileTabs.value(p);
        const int idx = w ? m_repoFileTabs->indexOf(w) : -1;
        if (idx >= 0)
            m_repoFileTabs->removeTab(idx);
        m_openFileTabs.remove(p);
        if (w)
            w->deleteLater();
    }
    if (m_repoFileTabs->count() == 0)
        showRepoOverview();
}

void MainWindow::openRepoFile(const QString &path)
{
    if (path.isEmpty() || !m_repoFileTabs)
        return;
    // A cove is encrypted; open it in the cove viewer (unlocking as needed) rather
    // than dumping ciphertext into the code editor.
    if (path.endsWith(QStringLiteral(".cove"), Qt::CaseInsensitive) &&
        path.startsWith(CoveStore::covesDirRel() + "/")) {
        openCove(path);
        return;
    }
    // Opening a file reveals the explorer + editor view; build the tree lazily.
    if (m_treeLoadedForIndex != m_repoDetailIndex) {
        loadRepoFileTree();
        m_treeLoadedForIndex = m_repoDetailIndex;
    }
    if (m_filesStack)
        m_filesStack->setCurrentIndex(1);

    // Focus an already-open tab for this file.
    if (m_openFileTabs.contains(path)) {
        m_repoFileTabs->setCurrentWidget(m_openFileTabs.value(path));
        return;
    }
    const QString dir = repoGitDir();
    QByteArray out;
    QString err;
    QString content;
    bool editable = false;
    if (dir.isEmpty() || !runGitCapture(dir, {"show", currentRef() + ":" + path}, &out, &err))
        content = "Could not read file: " + err.left(200);
    else if (out.size() > 1024 * 1024)
        content = QStringLiteral("File is too large to preview (%1 KB).")
                      .arg(out.size() / 1024);
    else if (out.contains('\0'))
        content = QString::fromUtf8("Binary file (%1 bytes) \xE2\x80\x94 not shown.")
                      .arg(out.size());
    else {
        content = QString::fromUtf8(out);
        // A real working tree allows direct commits; a node mirroring someone
        // else's repo can still edit and propose the change as a pull request.
        editable = repoHasWorkingTree() || repoCanProposePull();
        if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive) ||
            path.endsWith(QStringLiteral(".jsonc"), Qt::CaseInsensitive)) {
            const QJsonDocument doc = QJsonDocument::fromJson(out);
            if (!doc.isNull())
                content = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
        }
    }

    auto *editor = new CodePreviewEditor(path);
    editor->setReadOnly(!editable);
    editor->setPlainText(content);
    editor->document()->setModified(false);
    new CodePreviewHighlighter(editor->document(), path);

    const QString name = path.section('/', -1);
    const int index = m_repoFileTabs->addTab(editor, iconForFile(name), name);
    m_repoFileTabs->setTabToolTip(index, path);
    m_repoFileTabs->setCurrentIndex(index);
    m_openFileTabs.insert(path, editor);
    updateRepoFileSaveActions();
}

void MainWindow::updateRepoFileSaveActions()
{
    const QWidget *w = m_repoFileTabs ? m_repoFileTabs->currentWidget() : nullptr;
    const auto *editor = qobject_cast<const QPlainTextEdit *>(w);
    const QString path = w ? w->property("previewPath").toString() : QString();
    const bool haveFile = editor && !path.isEmpty();
    const bool editable = haveFile && !editor->isReadOnly();
    // The rendered-markdown toggle only makes sense for Markdown files; it reflects
    // whichever side (source / preview) the current tab is showing.
    if (m_repoFilePreviewButton) {
        const bool isMarkdown =
            haveFile && previewSyntaxForPath(path) == PreviewSyntax::Markdown;
        const auto *preview = dynamic_cast<const CodePreviewEditor *>(w);
        m_repoFilePreviewButton->setEnabled(isMarkdown);
        m_repoFilePreviewButton->setVisible(isMarkdown);
        m_repoFilePreviewButton->setChecked(preview && preview->markdownPreviewVisible());
    }
    // Viewing history only reads git, so it works for any open file — including
    // read-only previews on a mirror.
    if (m_repoFileHistoryButton)
        m_repoFileHistoryButton->setEnabled(haveFile);
    // Direct commits need a working tree we own; a mirrored repo can still open a
    // pull request, which is sent to the owner's inbox.
    if (m_repoFileCommitButton)
        m_repoFileCommitButton->setEnabled(editable && repoHasWorkingTree());
    if (m_repoFilePullButton)
        m_repoFilePullButton->setEnabled(
            editable && (repoHasWorkingTree() || repoCanProposePull()));
}

void MainWindow::saveCurrentRepoFile(bool createPull)
{
    QWidget *w = m_repoFileTabs ? m_repoFileTabs->currentWidget() : nullptr;
    auto *editor = qobject_cast<QPlainTextEdit *>(w);
    if (!editor || editor->isReadOnly())
        return;
    const QString path = w->property("previewPath").toString();
    if (path.isEmpty())
        return;
    if (saveRepoFileEdit(path, editor->toPlainText(), createPull))
        editor->document()->setModified(false);
    updateRepoFileSaveActions();
}

void MainWindow::toggleRepoFileMarkdownPreview()
{
    QWidget *w = m_repoFileTabs ? m_repoFileTabs->currentWidget() : nullptr;
    auto *editor = dynamic_cast<CodePreviewEditor *>(w);
    if (!editor)
        return;
    // Re-renders from the editor's current text each time, so edits made in the
    // source view show up the moment you flip to the preview.
    editor->setMarkdownPreviewVisible(!editor->markdownPreviewVisible());
    updateRepoFileSaveActions();
}

void MainWindow::showRepoFileHistory(const QString &path)
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || path.isEmpty())
        return;

    // The commits that touched this file, newest first. --follow keeps the
    // history walking across renames so an early commit under an old name still
    // shows up.
    QByteArray out;
    QString err;
    if (!runGitCapture(dir,
                       {"log", "--follow", "--date=format:%b %e, %Y",
                        "--format=%H%x1f%an%x1f%ad%x1f%s", currentRef(), "--", path},
                       &out, &err)) {
        setRepoDetailNotice("Could not read history: " + err.left(200), true);
        return;
    }

    struct HistEntry {
        QString hash, author, date, subject;
    };
    QList<HistEntry> entries;
    for (const QByteArray &lineRaw : out.split('\n')) {
        const QString line = QString::fromUtf8(lineRaw);
        if (line.trimmed().isEmpty())
            continue;
        const QStringList f = line.split(QLatin1Char('\x1f'));
        if (f.size() < 4)
            continue;
        entries.append(
            {f[0].trimmed(), f[1].trimmed(), f[2].trimmed(), f[3].trimmed()});
    }
    if (entries.isEmpty()) {
        setRepoDetailNotice("No commit history for " + path, false);
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("fileHistoryDialog");
    dialog->setWindowTitle(QString::fromUtf8("History \xE2\x80\x94 ") + path);
    dialog->resize(940, 620);

    // Left: one row per commit (subject + short hash / author / date).
    auto *list = new QListWidget;
    list->setObjectName("commitFileList");
    list->setMinimumWidth(260);
    list->setMaximumWidth(380);
    for (const HistEntry &e : entries) {
        auto *item = new QListWidgetItem(
            QString::fromUtf8("%1\n%2 \xC2\xB7 %3 \xC2\xB7 %4")
                .arg(e.subject, e.hash.left(7), e.author, e.date));
        item->setData(Qt::UserRole, e.hash);
        list->addItem(item);
    }

    // Right: the selected commit's diff for just this file.
    auto *diffView = new QTextBrowser;
    diffView->setObjectName("commitDiffView");
    diffView->setOpenExternalLinks(false);
    registerDiffView(diffView);

    connect(list, &QListWidget::currentItemChanged, this,
            [this, diffView, dir, path](QListWidgetItem *item, QListWidgetItem *) {
                if (!item)
                    return;
                const QString hash = item->data(Qt::UserRole).toString();
                QByteArray patch;
                runGitCapture(dir, {"show", "-M", "--format=", hash, "--", path},
                              &patch, nullptr);
                QList<DiffFileEntry> files;
                QString html = renderDiffHtml(QString::fromUtf8(patch), files, dir,
                                              hash + "^", hash);
                if (html.trimmed().isEmpty())
                    html = QStringLiteral(
                        "<p style='color:#8b949e'>No textual changes to this file "
                        "in the selected commit.</p>");
                setDiffHtml(diffView, html);
            });

    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(list);
    split->addWidget(diffView);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({300, 640});

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(split, 1);
    layout->addWidget(buttons);

    // Land on the most recent commit's diff straight away.
    list->setCurrentRow(0);
    dialog->show();
}

bool MainWindow::saveRepoFileEdit(const QString &path, const QString &content,
                                  bool createPull)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return false;
    const QString cleanPath = QDir::cleanPath(path);
    if (cleanPath.isEmpty() || cleanPath.startsWith("../") ||
        cleanPath.contains("/../") || QDir::isAbsolutePath(cleanPath)) {
        setRepoDetailNotice("Refusing to write outside the repository.", true);
        return false;
    }
    if (!repoHasWorkingTree()) {
        // A node mirroring someone else's repo has no working tree to commit to,
        // but it can still propose the edit as a pull request to the owner.
        if (!createPull) {
            setRepoDetailNotice(
                "This is a mirror; commit direct isn't available. Use \"Save as PR\".",
                true);
            return false;
        }
        if (!repoCanProposePull()) {
            setRepoDetailNotice(
                "This is a read-only mirror; files can't be edited here.", true);
            return false;
        }
        return proposePullFromMirrorEdit(cleanPath, content);
    }

    const QString dir = repoGitDir();
    QByteArray status;
    QString err;
    if (!runGitCapture(dir, {"status", "--porcelain"}, &status, &err) ||
        !status.trimmed().isEmpty()) {
        setRepoDetailNotice(
            err.isEmpty() ? "Commit or stash local changes before saving a file."
                          : err.left(240),
            true);
        return false;
    }

    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);
    if (base.isEmpty()) {
        setRepoDetailNotice("This repository has no branch to commit onto.", true);
        return false;
    }

    QString title = QStringLiteral("Edit %1").arg(cleanPath);
    QString description;
    QString branch;
    if (createPull) {
        QDialog dialog(this);
        dialog.setWindowTitle("Save as pull request");
        auto *branchEdit = new QLineEdit(&dialog);
        branch = QStringLiteral("edit/%1-%2")
                     .arg(repoSegment(cleanPath, QStringLiteral("file")),
                          QString::number(QDateTime::currentSecsSinceEpoch()));
        branchEdit->setText(branch.left(80));
        auto *titleEdit = new QLineEdit(title, &dialog);
        auto *bodyEdit = new QPlainTextEdit(&dialog);
        bodyEdit->setPlaceholderText("Describe the change...");
        auto *form = new QFormLayout;
        form->addRow("Branch", branchEdit);
        form->addRow("Title", titleEdit);
        form->addRow("Description", bodyEdit);
        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                 &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText("Create pull request");
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        auto *layout = new QVBoxLayout(&dialog);
        layout->addLayout(form);
        layout->addWidget(buttons);
        dialog.resize(520, 320);
        if (dialog.exec() != QDialog::Accepted)
            return false;
        branch = branchEdit->text().trimmed();
        title = titleEdit->text().trimmed();
        description = bodyEdit->toPlainText();
        if (branch.isEmpty() || title.isEmpty()) {
            setRepoDetailNotice("A pull request needs a branch and title.", true);
            return false;
        }
    } else {
        bool ok = false;
        title = QInputDialog::getText(this, "Commit file edit", "Commit message:",
                                      QLineEdit::Normal, title, &ok)
                    .trimmed();
        if (!ok)
            return false;
        if (title.isEmpty()) {
            setRepoDetailNotice("A commit message is required.", true);
            return false;
        }
    }

    auto writeEditedFile = [&]() -> bool {
        const QString fullPath = QDir(dir).filePath(cleanPath);
        QDir().mkpath(QFileInfo(fullPath).absolutePath());
        QFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            err = QStringLiteral("Could not write %1").arg(cleanPath);
            return false;
        }
        if (file.write(content.toUtf8()) < 0) {
            err = QStringLiteral("Could not write %1").arg(cleanPath);
            return false;
        }
        return true;
    };

    if (!runGitCapture(dir, {"checkout", base}, nullptr, &err)) {
        setRepoDetailNotice(QStringLiteral("Could not check out %1: %2")
                                .arg(base, err.left(200)),
                            true);
        return false;
    }

    QString commitRef = base;
    if (createPull) {
        if (!runGitCapture(dir, {"check-ref-format", "--branch", branch}, nullptr,
                           &err) ||
            !runGitCapture(dir, {"checkout", "-b", branch, base}, nullptr, &err)) {
            setRepoDetailNotice(QStringLiteral("Could not create branch %1: %2")
                                    .arg(branch, err.left(200)),
                                true);
            runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
            return false;
        }
        commitRef = branch;
    }

    if (!writeEditedFile() ||
        !runGitCapture(dir, {"add", "--", cleanPath}, nullptr, &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not stage the edited file."
                                          : err.left(240),
                            true);
        runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
        return false;
    }
    if (runGitCapture(dir, {"diff", "--cached", "--quiet"}, nullptr, nullptr)) {
        setRepoDetailNotice("No changes to save.");
        runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
        return false;
    }
    if (!runGitCapture(dir, {"commit", "-m", title, "--", cleanPath}, nullptr, &err)) {
        setRepoDetailNotice(QStringLiteral("git commit failed: %1").arg(err.left(240)),
                            true);
        runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
        return false;
    }

    if (createPull) {
        QByteArray diff;
        if (!runGitCapture(dir, {"diff", "--binary", base + ".." + branch}, &diff,
                           &err)) {
            setRepoDetailNotice(QStringLiteral("Could not create pull request diff: %1")
                                    .arg(err.left(240)),
                                true);
            runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
            return false;
        }
        // Capture the authored commit(s) so the merge preserves authorship.
        QByteArray mbox;
        runGitCapture(dir, {"format-patch", "--stdout", base + ".." + branch}, &mbox,
                      nullptr);
        runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
        PullStore store = pullStoreForCurrentRepo();
        QString error;
        const int number = store.createPull(title, description, base, branch,
                                            QString::fromUtf8(diff),
                                            QString::fromUtf8(mbox),
                                            /*branchBacked=*/true, &error);
        if (number < 0) {
            setRepoDetailNotice(error.isEmpty() ? "Could not create the pull request."
                                                : error,
                                true);
            return false;
        }
        logSystem(QStringLiteral("Saved %1 on %2 and opened pull #%3.")
                      .arg(cleanPath, branch)
                      .arg(number));
        setRepoDetailNotice(QStringLiteral("Opened pull request #%1 from %2.")
                                .arg(number)
                                .arg(branch));
        m_currentPullNumber = number;
        switchToPullTab(number);
    } else {
        logSystem(QStringLiteral("Committed %1 to %2.").arg(cleanPath, commitRef));
        setRepoDetailNotice(QStringLiteral("Committed %1 to %2.")
                                .arg(cleanPath, commitRef));
    }

    setRepoBranch(base);
    // A direct commit shouldn't yank the user out of the file editor back to the
    // overview. Remember which files-panel page they were on and restore it after
    // the refresh (the PR path intentionally navigates to the new pull instead).
    const int filesPage = m_filesStack ? m_filesStack->currentIndex() : 0;
    refreshOpenRepoDetail();
    if (!createPull && m_filesStack)
        m_filesStack->setCurrentIndex(filesPage);
    return true;
}

bool MainWindow::repoCanProposePull() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return false;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (repo.previewOnly)
        return false;
    if (repoHasWorkingTree())
        return true;
    // No working tree: a bare mirror of the repo is enough — we build the commit
    // in a throwaway worktree off it and send the patch to the owner's inbox.
    return !repo.mirrorPath.trimmed().isEmpty() && QDir(repo.mirrorPath).exists();
}

bool MainWindow::proposePullFromMirrorEdit(const QString &cleanPath,
                                           const QString &content)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return false;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    const QString mirror = repo.mirrorPath.trimmed();
    if (mirror.isEmpty() || !QDir(mirror).exists()) {
        setRepoDetailNotice("No local mirror to base a pull request on.", true);
        return false;
    }

    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);
    if (base.isEmpty()) {
        setRepoDetailNotice("This repository has no branch to base a pull request on.",
                            true);
        return false;
    }

    // Branch / title / description, mirroring the working-tree "Save as PR" dialog
    // but making clear the change is sent to the upstream owner.
    QString branch = QStringLiteral("edit/%1-%2")
                         .arg(repoSegment(cleanPath, QStringLiteral("file")),
                              QString::number(QDateTime::currentSecsSinceEpoch()));
    QString title = QStringLiteral("Edit %1").arg(cleanPath);
    QString description;
    {
        QDialog dialog(this);
        dialog.setWindowTitle("Save as pull request");
        auto *info = new QLabel(
            QStringLiteral("This repository is mirrored from <b>%1/%2</b>. Your "
                           "change will be sent to its owner as a pull request.")
                .arg(repo.owner.toHtmlEscaped(), repo.name.toHtmlEscaped()),
            &dialog);
        info->setWordWrap(true);
        info->setTextFormat(Qt::RichText);
        auto *branchEdit = new QLineEdit(branch.left(80), &dialog);
        auto *titleEdit = new QLineEdit(title, &dialog);
        auto *bodyEdit = new QPlainTextEdit(&dialog);
        bodyEdit->setPlaceholderText("Describe the change...");
        auto *form = new QFormLayout;
        form->addRow("Branch", branchEdit);
        form->addRow("Title", titleEdit);
        form->addRow("Description", bodyEdit);
        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                 &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText("Create pull request");
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        auto *layout = new QVBoxLayout(&dialog);
        layout->addWidget(info);
        layout->addLayout(form);
        layout->addWidget(buttons);
        dialog.resize(520, 340);
        if (dialog.exec() != QDialog::Accepted)
            return false;
        branch = branchEdit->text().trimmed();
        title = titleEdit->text().trimmed();
        description = bodyEdit->toPlainText();
        if (branch.isEmpty() || title.isEmpty()) {
            setRepoDetailNotice("A pull request needs a branch and title.", true);
            return false;
        }
    }

    // Build the commit in a throwaway, detached worktree off the bare mirror so we
    // never touch the mirror's refs or working state.
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        setRepoDetailNotice("Could not create a temporary work area.", true);
        return false;
    }
    const QString work = tmp.filePath(QStringLiteral("wt"));
    QString err;
    auto cleanupWorktree = [&]() {
        runGitCapture(mirror, {"worktree", "remove", "--force", work}, nullptr,
                      nullptr);
        runGitCapture(mirror, {"worktree", "prune"}, nullptr, nullptr);
    };
    if (!runGitCapture(mirror, {"worktree", "add", "--detach", work, base}, nullptr,
                       &err)) {
        setRepoDetailNotice(
            QStringLiteral("Could not prepare a pull request: %1").arg(err.left(200)),
            true);
        cleanupWorktree();
        return false;
    }

    const QString fullPath = QDir(work).filePath(cleanPath);
    bool wrote = QDir().mkpath(QFileInfo(fullPath).absolutePath());
    if (wrote) {
        QFile file(fullPath);
        wrote = file.open(QIODevice::WriteOnly | QIODevice::Truncate |
                          QIODevice::Text) &&
                file.write(content.toUtf8()) >= 0;
    }
    if (!wrote) {
        setRepoDetailNotice(QStringLiteral("Could not write %1.").arg(cleanPath), true);
        cleanupWorktree();
        return false;
    }

    if (!runGitCapture(work, {"add", "--", cleanPath}, nullptr, &err)) {
        setRepoDetailNotice(err.isEmpty() ? "Could not stage the edited file."
                                          : err.left(240),
                            true);
        cleanupWorktree();
        return false;
    }
    if (runGitCapture(work, {"diff", "--cached", "--quiet"}, nullptr, nullptr)) {
        setRepoDetailNotice("No changes to save.");
        cleanupWorktree();
        return false;
    }
    // Credit the commit to this node so format-patch carries a sensible author even
    // when the bare mirror has no user identity configured. The PR itself is signed
    // separately by makeSignedPull().
    const QString authorName =
        m_userName.trimmed().isEmpty() ? QStringLiteral("ForkMesh contributor")
                                       : m_userName.trimmed();
    const QString authorEmail =
        (m_profileIdentity.publicKey().isEmpty() ? QStringLiteral("contributor")
                                                 : m_profileIdentity.publicKey()) +
        QStringLiteral("@forkmesh");
    if (!runGitCapture(work,
                       {"-c", "user.name=" + authorName,
                        "-c", "user.email=" + authorEmail, "commit", "-m", title,
                        "--", cleanPath},
                       nullptr, &err)) {
        setRepoDetailNotice(QStringLiteral("git commit failed: %1").arg(err.left(240)),
                            true);
        cleanupWorktree();
        return false;
    }

    QByteArray diff;
    if (!runGitCapture(work, {"diff", "--binary", base + "..HEAD"}, &diff, &err) ||
        diff.trimmed().isEmpty()) {
        setRepoDetailNotice(
            QStringLiteral("Could not create pull request diff: %1").arg(err.left(240)),
            true);
        cleanupWorktree();
        return false;
    }
    QByteArray mbox;
    runGitCapture(work, {"format-patch", "--stdout", base + "..HEAD"}, &mbox, nullptr);
    cleanupWorktree();

    PullRequest pr;
    pr.title = title;
    pr.description = description;
    pr.base = base;
    pr.head = branch;
    pr.patch = QString::fromUtf8(diff);
    pr.commits = QString::fromUtf8(mbox);

    PullStore store = pullStoreForCurrentRepo();
    submitPullToInbox(store.makeSignedPull(pr), repo);
    logSystem(QStringLiteral("Sent %1 to %2/%3 as a pull request.")
                  .arg(cleanPath, repo.owner, repo.name));
    return true;
}

void MainWindow::showRepoOverview()
{
    // Default Code view: the GitHub-style overview (branch/tags toolbar, latest
    // commit, file list and README). The explorer + editor lives one toggle
    // away — see showRepoEditor().
    if (!m_filesStack)
        return;
    loadRepoOverview(m_overviewPath);
    m_filesStack->setCurrentIndex(0);
    if (m_filesModeOverviewButton)
        m_filesModeOverviewButton->setChecked(true);
    if (m_filesModeExplorerButton)
        m_filesModeExplorerButton->setChecked(false);
    // "Code overview" always means the file list + README: if the commits
    // panel was left showing (via the commit strip's toggle), swap it back.
    showOverviewFiles();
}

void MainWindow::showRepoEditor()
{
    // Explorer + editor view. Make sure the file tree is built and a file is
    // open so the pane is never blank, then show it.
    if (!m_filesStack)
        return;
    if (m_treeLoadedForIndex != m_repoDetailIndex) {
        loadRepoFileTree();
        m_treeLoadedForIndex = m_repoDetailIndex;
    }
    if (m_repoFileTabs && m_repoFileTabs->count() == 0)
        openRepoReadme();
    m_filesStack->setCurrentIndex(1);
    if (m_filesModeOverviewButton)
        m_filesModeOverviewButton->setChecked(false);
    if (m_filesModeExplorerButton)
        m_filesModeExplorerButton->setChecked(true);
}

// Show the commits panel in the Code overview, under the latest-commit bar,
// and light up the commit strip's "N Commits" toggle. Pure navigation — no
// loading — so callers that jump straight to one commit (showCommit) aren't
// clobbered by an openMostRecentCommit; the history-button click handler and
// the repo-open landing path layer the list build on top.
void MainWindow::showOverviewCommits()
{
    // Callers can be anywhere (another tab, the explorer, a search result):
    // land on the Code tab's overview page first.
    if (m_repoDetailTabs && m_repoDetailTabs->button(0))
        m_repoDetailTabs->button(0)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(0);
    if (m_filesStack)
        m_filesStack->setCurrentIndex(0);
    if (m_filesModeOverviewButton)
        m_filesModeOverviewButton->setChecked(true);
    if (m_filesModeExplorerButton)
        m_filesModeExplorerButton->setChecked(false);
    if (m_overviewBodyStack)
        m_overviewBodyStack->setCurrentIndex(1);
    if (m_historyButton)
        m_historyButton->setChecked(true);
}

// Swap the overview body back to the file list + README and dim the toggle.
void MainWindow::showOverviewFiles()
{
    if (m_overviewBodyStack)
        m_overviewBodyStack->setCurrentIndex(0);
    if (m_historyButton)
        m_historyButton->setChecked(false);
}

// Show the branches panel in the Code overview, under the toolbar — the same
// in-page navigation showOverviewCommits does for commits. Pure navigation; the
// caller layers loadBranchesPanel() on top to (re)build the rows.
void MainWindow::showOverviewBranches()
{
    // Callers can be anywhere (an agent header link, another tab): land on the
    // Code tab's overview page first.
    if (m_repoDetailTabs && m_repoDetailTabs->button(0))
        m_repoDetailTabs->button(0)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(0);
    if (m_filesStack)
        m_filesStack->setCurrentIndex(0);
    if (m_filesModeOverviewButton)
        m_filesModeOverviewButton->setChecked(true);
    if (m_filesModeExplorerButton)
        m_filesModeExplorerButton->setChecked(false);
    if (m_overviewBodyStack)
        m_overviewBodyStack->setCurrentIndex(2);
    // The commits toggle isn't lit when branches show.
    if (m_historyButton)
        m_historyButton->setChecked(false);
}

void MainWindow::openRepoReadme()
{
    if (!m_repoFileTabs)
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    QByteArray out;
    if (!runGitCapture(dir, {"ls-tree", "--name-only", currentRef()}, &out, nullptr))
        return;
    QString readme;
    for (const QString &name :
         QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts)) {
        if (!name.startsWith(QStringLiteral("README"), Qt::CaseInsensitive))
            continue;
        // Prefer README.md, otherwise take the first README* we see.
        if (name.compare(QStringLiteral("README.md"), Qt::CaseInsensitive) == 0) {
            readme = name;
            break;
        }
        if (readme.isEmpty())
            readme = name;
    }
    if (!readme.isEmpty())
        openRepoFile(readme);
}

void MainWindow::loadRepoOverview(const QString &path)
{
    if (!m_overviewList)
        return;

    // The rebuild below pumps the event loop between its git reads
    // (GitKeepAlive), so a queued slot serviced mid-load could call back in
    // here and interleave a second rebuild with the first — visible as extra
    // redraw churn. Drop the re-entrant call: the in-flight load finishes a
    // consistent view, and anything it missed re-keys the next load.
    if (m_overviewLoading)
        return;

    const QString dir = repoGitDir();

    // Re-entering the repo screen (e.g. back from Chat) re-runs this whole
    // function, which is expensive: several git passes plus a `git log -1` per
    // entry. Nothing in the overview can have changed unless the repo, the
    // browsed directory, the branch, or its HEAD commit moved, so skip the
    // rebuild when the signature matches what's already on screen.
    QString head;
    if (!dir.isEmpty()) {
        QByteArray headOut;
        if (runGitCapture(dir, {"rev-parse", currentRef()}, &headOut, nullptr))
            head = QString::fromUtf8(headOut).trimmed();
    }
    const QString key = QStringLiteral("%1|%2|%3|%4")
                            .arg(m_repoDetailIndex)
                            .arg(path, currentRef(), head);
    if (key == m_overviewLoadedKey && m_overviewList->topLevelItemCount() > 0) {
        m_overviewPath = path;
        return;
    }
    m_overviewLoadedKey.clear(); // cleared until this load completes successfully

    // Past the skip check we do the full rebuild: a whole-tree --numstat diff plus a
    // `git log -1` per entry (and the README read). Serviced on a button click, so
    // pump the event loop across those reads to keep the window responsive (adhoc #83).
    ScopedFlag loading(m_overviewLoading);
    GitKeepAlive keepAlive;

    m_overviewPath = path;

    // Gather phase: every git read below lands in locals only. The keep-alive
    // pump services paint events between reads, so any widget cleared or
    // written here would repaint mid-load — the old "flash of blank page, then
    // piecewise redraws" jank. The previous overview stays on screen untouched
    // until the new one is applied in one shot at the end.

    // Latest commit strip: "<subject> · <author> committed <relative time>".
    QString commitBarText;
    if (m_commitBar) {
        QByteArray logOut;
        QStringList logArgs{"log", "-1", "--format=%H%x1f%an%x1f%ar%x1f%s",
                            currentRef()};
        if (!path.isEmpty())
            logArgs << "--" << path;
        if (!dir.isEmpty() && runGitCapture(dir, logArgs, &logOut, nullptr) &&
            !logOut.trimmed().isEmpty()) {
            const QStringList f = QString::fromUtf8(logOut).trimmed().split('\x1f');
            const QString fullHash = f.value(0);
            const QString author = f.value(1);
            const QString when = f.value(2);
            const QString subject = f.value(3);
            // Latest commit: subject, author and "x ago", plus the action/check
            // status glyph for this (the first/most-recent) commit.
            commitBarText =
                QStringLiteral("%1<b>%2</b> &nbsp; <span style='color:#8b949e'>%3 "
                               "committed %4</span>")
                    .arg(commitStatusGlyph(fullHash),
                         subject.toHtmlEscaped(), author.toHtmlEscaped(),
                         when.toHtmlEscaped());
        } else {
            commitBarText = "<span style='color:#8b949e'>No commits yet</span>";
        }
    }
    QString historyText = QStringLiteral("Commits");
    if (m_historyButton) {
        QByteArray countOut;
        QString count;
        if (!dir.isEmpty() &&
            runGitCapture(dir, {"rev-list", "--count", currentRef()}, &countOut, nullptr))
            count = QString::fromUtf8(countOut).trimmed();
        if (!count.isEmpty())
            historyText = QStringLiteral("%1 Commits").arg(formatCount(count.toLongLong()));
    }

    // Breadcrumb for directory navigation.
    QString crumbText = QStringLiteral("<a href=\"/\">root</a>");
    {
        QString acc;
        for (const QString &part : path.split('/', Qt::SkipEmptyParts)) {
            acc = acc.isEmpty() ? part : acc + "/" + part;
            crumbText += " / <a href=\"" + acc.toHtmlEscaped() + "\">" +
                         part.toHtmlEscaped() + "</a>";
        }
    }

    QList<OverviewRow> rows;
    qint64 repoBytes = 0;
    QString messageRow; // non-empty: the file list shows this single row instead
    if (dir.isEmpty())
        messageRow = QStringLiteral("No local copy of this repository to browse.");

    const QString treeish = path.isEmpty() ? currentRef() : currentRef() + ":" + path;
    QByteArray out;
    QString err;
    if (messageRow.isEmpty() &&
        !runGitCapture(dir, {"ls-tree", "-z", treeish}, &out, &err))
        messageRow = err.isEmpty()
                         ? QStringLiteral("This repository has no commits yet.")
                         : "Could not read files: " + err.left(120);

    // Per-entry blob sizes (directories = recursive sum) and the whole-repo total,
    // from a single recursive ls-tree. The total is the size-bar denominator so a
    // bar shows each entry's share of the entire repository.
    QHash<QString, qint64> childBytes;
    // Recursive file count per top-level entry (directories = number of blobs
    // beneath them), shown in the overview's Files column. ls-tree -r lists every
    // blob once, so counting per top-level path segment is a single pass.
    QHash<QString, qint64> childFiles;
    QByteArray sizeOut;
    if (messageRow.isEmpty() &&
        runGitCapture(dir, {"ls-tree", "-r", "-l", "-z", currentRef()}, &sizeOut,
                      nullptr)) {
        const QString prefix = path.isEmpty() ? QString() : path + "/";
        for (const QByteArray &record : sizeOut.split('\0')) {
            if (record.isEmpty())
                continue;
            const int tab = record.indexOf('\t');
            if (tab < 0)
                continue;
            const QStringList meta =
                QString::fromUtf8(record.left(tab)).split(' ', Qt::SkipEmptyParts);
            if (meta.size() < 4)
                continue;
            const qint64 sz = meta.at(3).toLongLong(); // '-' (submodule) -> 0
            repoBytes += sz;
            const QString blob = QString::fromUtf8(record.mid(tab + 1));
            if (!prefix.isEmpty() && !blob.startsWith(prefix))
                continue;
            const QString rel = prefix.isEmpty() ? blob : blob.mid(prefix.size());
            const QString top = rel.section('/', 0, 0);
            childBytes[top] += sz;
            childFiles[top] += 1;
        }
    }

    // Per-entry line counts (directories = recursive sum). Diffing the empty
    // tree against the ref makes every blob a pure addition, so numstat's
    // added-line column equals the blob's total line count — one fast git pass,
    // no blob contents read into the client. Binary files report "-" and are
    // skipped (they contribute 0 lines).
    QHash<QString, qint64> childLoc;
    QByteArray locOut;
    static const QByteArray kEmptyTree = "4b825dc642cb6eb9a060e54bf8d69288fbee4904";
    if (messageRow.isEmpty() &&
        runGitCapture(dir,
                      {"diff", "--numstat", "--no-renames", "-z",
                       QString::fromLatin1(kEmptyTree), currentRef()},
                      &locOut, nullptr)) {
        const QString prefix = path.isEmpty() ? QString() : path + "/";
        for (const QByteArray &record : locOut.split('\0')) {
            if (record.isEmpty())
                continue;
            const int firstTab = record.indexOf('\t');
            const int secondTab =
                firstTab < 0 ? -1 : record.indexOf('\t', firstTab + 1);
            if (secondTab < 0)
                continue;
            const QByteArray added = record.left(firstTab);
            if (added == "-") // binary blob
                continue;
            const qint64 lines = added.toLongLong();
            const QString blob =
                QString::fromUtf8(record.mid(secondTab + 1));
            if (!prefix.isEmpty() && !blob.startsWith(prefix))
                continue;
            const QString rel = prefix.isEmpty() ? blob : blob.mid(prefix.size());
            childLoc[rel.section('/', 0, 0)] += lines;
        }
    }

    QString readmePath;
    if (messageRow.isEmpty()) {
        for (const QByteArray &record : out.split('\0')) {
            if (record.isEmpty())
                continue;
            const int tab = record.indexOf('\t');
            if (tab < 0)
                continue;
            const QStringList meta =
                QString::fromUtf8(record.left(tab)).split(' ', Qt::SkipEmptyParts);
            if (meta.size() < 2)
                continue;
            OverviewRow e;
            e.name = QString::fromUtf8(record.mid(tab + 1));
            e.isDir = meta.at(1) == "tree";
            e.path = path.isEmpty() ? e.name : path + "/" + e.name;
            e.size = childBytes.value(e.name, 0);
            e.loc = childLoc.value(e.name, 0);
            e.fileCount = childFiles.value(e.name, 0);
            // Last commit that touched this entry: timestamp (for sorting), relative
            // "x ago" and subject (shown in the row).
            QByteArray logOut;
            if (runGitCapture(dir,
                              {"log", "-1", "--format=%ct%x1f%cr%x1f%s", currentRef(),
                               "--", e.path},
                              &logOut, nullptr) &&
                !logOut.trimmed().isEmpty()) {
                const QStringList f = QString::fromUtf8(logOut).trimmed().split('\x1f');
                e.commitTs = f.value(0).toLongLong();
                e.whenText = f.value(1);
                e.subject = f.value(2);
            }
            rows.append(e);
            if (!e.isDir && e.name.compare("README.md", Qt::CaseInsensitive) == 0)
                readmePath = e.path;
        }
    }

    // The directory's README, rendered beneath the file list (GitHub-style).
    QString readmeMarkdown;
    if (!readmePath.isEmpty()) {
        QByteArray readme;
        if (runGitCapture(dir, {"show", currentRef() + ":" + readmePath}, &readme,
                          nullptr) &&
            !readme.contains('\0'))
            readmeMarkdown = QString::fromUtf8(readme);
    }

    // Apply phase: all the git reads are done, so swap the widgets from the old
    // overview to the new one together. Updates stay disabled across the swap so
    // the page repaints exactly once — no cleared-then-refilled flicker.
    QWidget *page = m_filesStack ? m_filesStack->widget(0) : nullptr;
    if (page)
        page->setUpdatesEnabled(false);
    if (m_commitBar)
        m_commitBar->setText(commitBarText);
    if (m_historyButton)
        m_historyButton->setText(historyText);
    if (m_overviewCrumb)
        m_overviewCrumb->setText(crumbText);
    m_overviewRows = rows;
    m_overviewRepoBytes = repoBytes;
    if (!messageRow.isEmpty()) {
        m_overviewList->clear();
        new QTreeWidgetItem(m_overviewList, {messageRow});
    } else {
        populateOverviewTree();
        m_overviewLoadedKey = key; // overview is now in sync with this commit
    }
    if (m_readmeView) {
        if (!readmeMarkdown.isEmpty())
            m_readmeView->setMarkdown(readmeMarkdown);
        else
            m_readmeView->clear();
    }
    if (page)
        page->setUpdatesEnabled(true);
}

// Build the small per-row size bar widget shown in the overview's Size column:
// a muted track with an accent fill proportional to the entry's share of the
// repo, plus the human-readable size. Transparent to mouse so row clicks still
// reach the tree (navigate / open file).
static QWidget *makeOverviewSizeBar(double fraction, const QString &sizeText)
{
    auto *w = new QWidget;
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(4, 0, 2, 0);
    lay->setSpacing(6);
    auto *track = new QFrame;
    track->setObjectName("sizeBarTrack");
    track->setFixedSize(60, 6);
    auto *fill = new QFrame(track);
    fill->setObjectName("sizeBarFill");
    const double f = std::clamp(fraction, 0.0, 1.0);
    const int fw = f <= 0.0 ? 0 : std::max(2, int(f * 60.0));
    fill->setGeometry(0, 0, fw, 6);
    auto *label = new QLabel(sizeText);
    label->setObjectName("statusLine");
    lay->addWidget(track);
    lay->addWidget(label, 1);
    for (QWidget *child : {w, static_cast<QWidget *>(track),
                           static_cast<QWidget *>(fill), static_cast<QWidget *>(label)})
        child->setAttribute(Qt::WA_TransparentForMouseEvents);
    return w;
}

void MainWindow::populateOverviewTree()
{
    if (!m_overviewList)
        return;
    // The rebuild is a clear plus an item insert and a size-bar widget per row;
    // hold repaints until the tree is complete so it redraws once instead of
    // flashing empty and repainting as rows land (also hit on sort clicks).
    m_overviewList->setUpdatesEnabled(false);
    m_overviewList->clear();

    // A ".." row to step up a directory (kept pinned at the top, above the sort).
    if (!m_overviewPath.isEmpty()) {
        auto *up = new QTreeWidgetItem(m_overviewList);
        up->setIcon(0, iconForDir(false));
        up->setText(0, "..");
        const int cut = m_overviewPath.lastIndexOf('/');
        up->setData(0, Qt::UserRole, cut < 0 ? QString() : m_overviewPath.left(cut));
        up->setData(0, Qt::UserRole + 1, 1); // kind 1 = directory / up
    }

    const QString key = m_overviewSortKey;
    const bool desc = m_overviewSortDesc;
    QList<OverviewRow> rows = m_overviewRows;
    std::sort(rows.begin(), rows.end(),
              [&key, desc](const OverviewRow &a, const OverviewRow &b) {
                  if (key == QLatin1String("size")) {
                      if (a.size != b.size)
                          return desc ? a.size > b.size : a.size < b.size;
                  } else if (key == QLatin1String("loc")) {
                      if (a.loc != b.loc)
                          return desc ? a.loc > b.loc : a.loc < b.loc;
                  } else if (key == QLatin1String("files")) {
                      if (a.fileCount != b.fileCount)
                          return desc ? a.fileCount > b.fileCount
                                      : a.fileCount < b.fileCount;
                  } else if (key == QLatin1String("updated")) {
                      if (a.commitTs != b.commitTs)
                          return desc ? a.commitTs > b.commitTs
                                      : a.commitTs < b.commitTs;
                  } else if (key == QLatin1String("subject")) {
                      const int c = a.subject.compare(b.subject, Qt::CaseInsensitive);
                      if (c != 0)
                          return desc ? c > 0 : c < 0;
                  } else { // name: folders first, then by name
                      if (a.isDir != b.isDir)
                          return a.isDir;
                      const int c = a.name.compare(b.name, Qt::CaseInsensitive);
                      return desc ? c > 0 : c < 0;
                  }
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });

    for (const OverviewRow &e : std::as_const(rows)) {
        auto *item = new QTreeWidgetItem(m_overviewList);
        item->setIcon(0, e.isDir ? iconForDir(false) : iconForFile(e.name));
        item->setText(0, e.name);
        item->setData(0, Qt::UserRole, e.path);
        item->setData(0, Qt::UserRole + 1, e.isDir ? 1 : 0);
        item->setText(2, e.loc > 0 ? QLocale().toString(e.loc)
                                   : QString::fromUtf8("\xE2\x80\x94"));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setForeground(2, QColor("#8b949e"));
        if (e.loc > 0)
            item->setToolTip(2, QString::fromUtf8("%1 lines of code")
                                    .arg(QLocale().toString(e.loc)));
        // Files column: recursive file count for directories (files themselves
        // are a single file, so leave their cell as an em dash).
        item->setText(3, e.isDir && e.fileCount > 0
                             ? QLocale().toString(e.fileCount)
                             : QString::fromUtf8("\xE2\x80\x94"));
        item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        item->setForeground(3, QColor("#8b949e"));
        if (e.isDir && e.fileCount > 0)
            item->setToolTip(3, QString::fromUtf8("%1 file%2")
                                    .arg(QLocale().toString(e.fileCount),
                                         e.fileCount == 1 ? QString()
                                                          : QStringLiteral("s")));
        item->setText(4, e.subject);
        item->setToolTip(4, e.subject);
        item->setText(5, e.whenText);
        const double frac = m_overviewRepoBytes > 0
                                ? double(e.size) / double(m_overviewRepoBytes)
                                : 0.0;
        m_overviewList->setItemWidget(item, 1,
                                      makeOverviewSizeBar(frac, formatByteSize(e.size)));
        const double pct = frac * 100.0;
        item->setToolTip(1, QString::fromUtf8("%1 \xC2\xB7 %2% of the repository")
                                .arg(formatByteSize(e.size),
                                     QString::number(pct, 'f', pct < 10 ? 1 : 0)));
    }
    m_overviewList->setUpdatesEnabled(true);
}

QString MainWindow::currentRef() const
{
    return m_repoBranch.isEmpty() ? QStringLiteral("HEAD") : m_repoBranch;
}

QString MainWindow::currentMirrorTip() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return QString();
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (repo.mirrorPath.isEmpty() || !QDir(repo.mirrorPath).exists())
        return QString();
    const QString branch =
        m_repoBranch.isEmpty() ? mirrorHeadBranch(repo.mirrorPath) : m_repoBranch;
    return mirrorBranchCommit(repo.mirrorPath, branch);
}

QSet<QString> MainWindow::unpushedCommitHashes() const
{
    QSet<QString> result;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return result;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    // Only a node holding the working copy can be ahead of its mirror; a
    // browse-only mirror just pulls, so nothing is ever unsynced there.
    if (repo.localPath.isEmpty() || !QDir(repo.localPath).exists() ||
        repo.mirrorPath.isEmpty() || !QDir(repo.mirrorPath).exists())
        return result;
    const QString mirrorTip = currentMirrorTip();
    if (mirrorTip.isEmpty())
        return result;
    // Commits reachable from the local branch tip but not from the mirror's tip
    // are exactly the ones the mirror hasn't received. If the mirror tip isn't a
    // local ancestor (diverged histories), git errors and we flag nothing.
    QByteArray out;
    if (!runGitCapture(repo.localPath,
                       {"rev-list", mirrorTip + ".." + currentRef()}, &out,
                       nullptr))
        return result;
    for (const QByteArray &line : out.split('\n')) {
        const QString hash = QString::fromUtf8(line).trimmed();
        if (!hash.isEmpty())
            result.insert(hash);
    }
    return result;
}

bool MainWindow::commitsListIsCurrent()
{
    // Never claim "current" before anything has loaded, or if a prior error path
    // left the list empty.
    if (!m_commitsTable || m_commitsTable->rowCount() == 0 ||
        m_commitsLoadedTip.isEmpty())
        return false;
    if (m_commitsLoadedRef != currentRef())
        return false;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return false;
    // One quick rev-parse: if the branch tip still matches what's on screen the
    // 300 rows are identical and there's nothing to rebuild.
    QByteArray out;
    if (!runGitCapture(dir, {"rev-parse", currentRef()}, &out, nullptr))
        return false;
    if (QString::fromUtf8(out).trimmed() != m_commitsLoadedTip)
        return false;
    // The rows can be identical yet their "waiting to sync" markers stale: a
    // publish/sync advances the mirror without touching the local tip. Reload
    // when the mirror moved so the markers reflect what's actually synced.
    return currentMirrorTip() == m_commitsLoadedMirrorTip;
}

void MainWindow::refreshCommitMarkersIfStale()
{
    // Only while the commit list is actually on screen — isVisible() is false
    // unless the commits panel is showing *and* the repo-detail view is the active
    // section, so this stays a cheap no-op everywhere else. When it isn't visible
    // the next visit reloads it anyway (commitsListIsCurrent now tracks the
    // mirror tip), so there's nothing to keep in sync in the background.
    if (!m_commitsListPage || !m_commitsListPage->isVisible())
        return;
    if (!commitsListIsCurrent())
        loadCommits();
}

// How deep a commit search widens the table window (see filterCommits). Bounded:
// row-building is the cost, not git — 5000 rows build in a blink, a whole large
// history froze the UI for 10-20s.
constexpr int kCommitSearchDepth = 5000;

void MainWindow::loadCommits()
{
    // The reload fires a few blocking git reads (status, the commit log, the
    // unpushed-set walk). Keep the event loop breathing across them so the window
    // stays painted (and the Refresh spinner keeps turning) instead of freezing.
    // The slow per-commit diff (--numstat) is deferred to fillCommitStats() so it
    // no longer holds up the list appearing. Nestable/RAII.
    GitKeepAlive keepAlive;
    refreshSourceControl(); // keep the working-changes panel in sync with the tab
    if (!m_commitsTable)
        return;
    // Bump the load generation up front so any background stat fill still pending
    // from a previous load (see fillCommitStats) sees its tag go stale and aborts,
    // even if this load takes an early return below.
    const int loadGen = ++m_commitsLoadGen;
    QSignalBlocker block(m_commitsTable);
    // Suspend the table's repaints for the *whole* reload, not just the row-build
    // loop below. The git reads (the commit log, the unpushed-set walk) run
    // under the GitKeepAlive above, which pumps the event loop — so without the
    // guard the table, already cleared to empty by setRowCount(0), would repaint
    // blank mid-load and flash before the rows arrive. With it the previous rows
    // stay frozen on screen until the new ones snap in (one repaint when the guard
    // unwinds, including on the early returns below). Matches every other loader.
    TableRepaintGuard repaintGuard(m_commitsTable);
    m_commitsTable->setSortingEnabled(false);
    m_commitsTable->setRowCount(0);
    showCommitList(); // always land on the list when (re)loading
    updateRepoCommitCount();
    // The Insights "Contributors & activity" counts are derived from the same
    // history; keep them in step when it moves underneath an open Insights tab
    // (a background sync, agent commit, revert or commit can advance it). A tab
    // click reloads them anyway, so only refresh when that page is on screen.
    if (m_repoDetailStack && m_insightsTabIndex >= 0 &&
        m_repoDetailStack->currentIndex() == m_insightsTabIndex)
        loadRepoInsights();
    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        m_commitsTable->setSortingEnabled(true);
        return;
    }
    QByteArray out;
    // Reset to the base depth whenever the branch being viewed changes; an
    // in-place reload (Refresh, or scroll-to-load-more) keeps the deepened window.
    if (currentRef() != m_commitsLoadedRef)
        m_commitsLimit = 300;
    m_commitsHasMore = false;
    // Fetch one extra record so a full page tells us older history remains. When a
    // search is active (m_commitsShowingAll) deepen the window to the search depth
    // — bounded, not the whole ref: building *every* commit as table rows (each
    // with a cell-widget button) froze the UI for 10-20s on a big history (stall
    // log: loadCommits <- filterCommits), and the matching uncapped --numstat in
    // fillCommitStats() froze it again. Anything deeper stays reachable by
    // scrolling the window onward first.
    //
    // Deliberately NO --numstat here: that flag makes git diff every commit in the
    // window (~1s on a large history) and was the bulk of this load's cost, yet it
    // only feeds the Files/+/− columns. This plain log returns in milliseconds so
    // the list paints immediately; fillCommitStats() backfills those three columns
    // from a deferred --numstat read once the rows are on screen.
    const int rowLimit = m_commitsShowingAll ? kCommitSearchDepth : m_commitsLimit;
    QStringList logArgs{
        "log",
        "--format=%x1e%H%x1f%h%x1f%an%x1f%ar%x1f%ct%x1f%s%x1f%P"};
    logArgs << "-n" << QString::number(rowLimit + 1);
    logArgs << currentRef();
    if (!runGitCapture(dir, logArgs, &out, nullptr)) {
        m_commitsTable->setSortingEnabled(true);
        return;
    }
    // Local commits the network mirror doesn't have yet, so the list can flag
    // (and the banner can count) what hasn't synced.
    const QSet<QString> unpushed = unpushedCommitHashes();
    int unpushedShown = 0;
    // Rewriting history is only meaningful on the source of truth (the node that
    // holds the working copy). On a browse-only mirror the delete button is shown
    // disabled, matching the Branches panel.
    const bool writable = repoHasWorkingTree();
    // The newest commit (git log's first record, before the table is sorted) is
    // the branch tip; remember it so a later tab click can skip an identical rebuild.
    QString loadedTip;
    // Commit-graph lane state, walked newest-first alongside the rows. Each entry
    // is the hash the lane is currently waiting to reach; an empty entry is a free
    // slot a new branch can reuse. maxGraphLane sizes the gutter column afterwards.
    QList<QString> activeLanes;
    int maxGraphLane = 0;
    // Repaints stay suspended by the TableRepaintGuard above while up to 300 rows
    // (each with a cell-widget button) are built: otherwise the table repaints on
    // every insertRow/setItem, which is what made a refresh feel sluggish.
    for (const QByteArray &record : out.split('\x1e')) {
        if (record.trimmed().isEmpty())
            continue;
        // Stop at the current window; the extra fetched record means more remain,
        // which the scroll handler uses to load the next page. A search load
        // (m_commitsShowingAll) uses the deeper — but still bounded — window.
        if (m_commitsTable->rowCount() >= rowLimit) {
            m_commitsHasMore = true;
            break;
        }
        const QStringList lines =
            QString::fromUtf8(record).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.isEmpty())
            continue;
        const QStringList f = lines.first().split(QLatin1Char('\x1f'));
        if (f.size() < 6)
            continue;
        if (loadedTip.isEmpty())
            loadedTip = f.at(0);
        const bool isUnpushed = unpushed.contains(f.at(0));

        // --- Commit-graph lanes for this row (newest-first walk). The lane the
        // commit sits in inherits its first parent; extra parents (a merge) open
        // new lanes, and lanes waiting for the same hash collapse back together.
        const QString hash = f.at(0);
        const QStringList parents =
            f.value(6).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        int nodeLane = activeLanes.indexOf(hash);
        if (nodeLane < 0) {
            nodeLane = activeLanes.indexOf(QString()); // reuse a freed slot
            if (nodeLane < 0) {
                nodeLane = activeLanes.size();
                activeLanes.append(hash);
            } else {
                activeLanes[nodeLane] = hash;
            }
        }
        // Snapshot the lanes drawn through this row before advancing them.
        QVariantList laneCols;
        for (int i = 0; i < activeLanes.size(); ++i) {
            if (!activeLanes.at(i).isEmpty()) {
                laneCols.append(i);
                maxGraphLane = std::max(maxGraphLane, i);
            }
        }
        // Close any other lane also waiting for this commit (it merges in here).
        for (int i = 0; i < activeLanes.size(); ++i)
            if (i != nodeLane && activeLanes.at(i) == hash)
                activeLanes[i].clear();
        if (parents.isEmpty()) {
            activeLanes[nodeLane].clear(); // root commit: the lane ends
        } else {
            activeLanes[nodeLane] = parents.at(0);
            for (int k = 1; k < parents.size(); ++k) {
                if (activeLanes.indexOf(parents.at(k)) >= 0)
                    continue; // that parent already has a lane
                int slot = activeLanes.indexOf(QString());
                if (slot < 0) {
                    slot = activeLanes.size();
                    activeLanes.append(parents.at(k));
                } else {
                    activeLanes[slot] = parents.at(k);
                }
            }
        }
        while (!activeLanes.isEmpty() && activeLanes.last().isEmpty())
            activeLanes.removeLast(); // keep the gutter as narrow as the history
        // Snapshot the lanes leaving the row (its bottom edge). The delegate
        // compares this with the top edge to tell pass-through lanes from the
        // merge/branch curves into and out of the node.
        QVariantList botLaneCols;
        for (int i = 0; i < activeLanes.size(); ++i) {
            if (!activeLanes.at(i).isEmpty()) {
                botLaneCols.append(i);
                maxGraphLane = std::max(maxGraphLane, i);
            }
        }

        const int row = m_commitsTable->rowCount();
        m_commitsTable->insertRow(row);
        // Graph gutter cell: carries this row's lane layout for CommitGraphDelegate.
        auto *graphItem = new QTableWidgetItem;
        graphItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        graphItem->setData(kGraphLanesRole, laneCols);
        graphItem->setData(kGraphNodeLaneRole, nodeLane);
        graphItem->setData(kGraphBottomLanesRole, botLaneCols);
        m_commitsTable->setItem(row, kCommitGraphCol, graphItem);
        auto *summary = new SortTableWidgetItem(f.at(5));
        summary->setData(Qt::UserRole, f.at(0));
        summary->setData(kTableSortRole, f.at(5).toLower());
        // Action/check status badge for this commit (green check / red x /
        // spinning-blue dot), shown as a leading icon when a workflow ran for it.
        switch (commitStatusCode(f.at(0))) {
        case 1:
            summary->setIcon(themedOcticon("check-circle", QColor("#3fb950"), 14));
            summary->setToolTip(QString::fromUtf8("Checks passed \xC2\xB7 %1").arg(f.at(1)));
            break;
        case 2:
            summary->setIcon(themedOcticon("x", QColor("#f85149"), 14));
            summary->setToolTip(QString::fromUtf8("Checks failed \xC2\xB7 %1").arg(f.at(1)));
            break;
        case 3:
            summary->setIcon(themedOcticon("sync", QColor("#58a6ff"), 14));
            summary->setToolTip(QString::fromUtf8("Checks running \xC2\xB7 %1").arg(f.at(1)));
            break;
        default:
            summary->setToolTip(
                QStringLiteral("Click to view the diff for %1").arg(f.at(1)));
            break;
        }
        // Summary (with the commit hash on UserRole) sits last; the metadata
        // columns are to its left.
        m_commitsTable->setItem(row, kCommitSummaryCol, summary);

        auto *author = new SortTableWidgetItem(f.at(2));
        author->setData(kTableSortRole, f.at(2).toLower());
        m_commitsTable->setItem(row, 0, author);
        const qint64 commitTs = f.at(4).toLongLong();
        auto *date = new SortTableWidgetItem(formatShortRelativeTime(commitTs));
        date->setData(kTableSortRole, commitTs);
        date->setToolTip(f.at(3)); // full "x ago" form on hover
        m_commitsTable->setItem(row, 1, date);
        // Short commit hash; unsynced commits get a leading marker + amber tint.
        auto *hashItem = new SortTableWidgetItem(
            isUnpushed ? QString::fromUtf8("\xE2\x96\xB2 ") + f.at(1) : f.at(1));
        hashItem->setData(kTableSortRole, f.at(1));
        if (isUnpushed) {
            ++unpushedShown;
            hashItem->setForeground(QColor("#d29922"));
            hashItem->setToolTip(
                QStringLiteral("%1 — not yet synced to the network mirror").arg(f.at(1)));
        } else {
            hashItem->setToolTip(f.at(1));
        }
        m_commitsTable->setItem(row, kCommitHashCol, hashItem);
        // Files/+/− start as a pending dot and are filled by fillCommitStats()
        // once the deferred --numstat read returns (see the log above). Sort key 0
        // until then so sorting by these columns mid-fill stays well-defined.
        const QString pending = QString::fromUtf8("\xC2\xB7"); // "·"
        auto *fileItem = new SortTableWidgetItem(pending);
        fileItem->setData(kTableSortRole, 0);
        m_commitsTable->setItem(row, 3, fileItem);
        auto *addsItem = new SortTableWidgetItem(pending);
        addsItem->setForeground(QColor("#2ea043"));
        addsItem->setData(kTableSortRole, 0);
        m_commitsTable->setItem(row, 4, addsItem);
        auto *delsItem = new SortTableWidgetItem(pending);
        delsItem->setForeground(QColor("#f85149"));
        delsItem->setData(kTableSortRole, 0);
        m_commitsTable->setItem(row, 5, delsItem);

        // Per-row "delete from history" button. Enabled only on the source of
        // truth; on a browse-only mirror it stays visible but disabled so the
        // reason is discoverable.
        const QString fullHash = f.at(0);
        auto *del = new QPushButton;
        del->setObjectName("issueIconButton");
        del->setFlat(true);
        del->setCursor(Qt::PointingHandCursor);
        // Don't let the per-row button take keyboard focus: as a focusable cell
        // widget it makes the table scroll itself to keep it visible, which read
        // as the list "jumping" on click. The row click still selects/opens the
        // commit; the trash button is mouse-only.
        del->setFocusPolicy(Qt::NoFocus);
        del->setIcon(themedOcticon("trash", QColor("#f85149"), 15));
        del->setIconSize(QSize(15, 15));
        del->setEnabled(writable);
        del->setToolTip(writable
                            ? QStringLiteral("Remove %1 from history").arg(f.at(1))
                            : QStringLiteral("Read-only mirror — no working tree to "
                                             "rewrite history in"));
        connect(del, &QPushButton::clicked, this,
                [this, fullHash] { deleteCommit(fullHash); });
        m_commitsTable->setCellWidget(row, kCommitActionCol, del);
    }
    // Size the graph gutter to the widest the lanes ever got, then re-assert the
    // Date-descending sort so rows stay in git-log order (the order the lanes were
    // computed in) after sorting is re-enabled.
    {
        const int laneSpan = 2 * kGraphMargin + maxGraphLane * kGraphLaneWidth;
        m_commitsTable->horizontalHeader()->resizeSection(
            kCommitGraphCol, std::clamp(laneSpan, 22, 140));
    }
    // Repaints stay suspended (TableRepaintGuard) through the sort, banner update
    // and filter re-apply below, so the whole reload lands in a single repaint when
    // the guard unwinds at function scope. Only sorting needs re-enabling by hand.
    m_commitsTable->setSortingEnabled(true);
    m_commitsTable->sortByColumn(1, Qt::DescendingOrder);

    if (m_commitsUnsyncedBanner) {
        if (unpushedShown > 0) {
            showCommitsBanner(
                QString::fromUtf8(
                    "<span style='color:#d29922'>\xE2\x96\xB2 %1 commit%2 not yet "
                    "synced to the network mirror.</span>")
                    .arg(unpushedShown)
                    .arg(unpushedShown == 1 ? QString() : QStringLiteral("s")));
        } else {
            hideCommitsBanner();
        }
    }

    // Re-apply any active search filter so a reload doesn't drop the user's
    // current query (rows are all visible again after the rebuild above).
    if (m_commitSearch && !m_commitSearch->text().trimmed().isEmpty())
        filterCommits(m_commitSearch->text());

    // Record what's now on screen so a repeat tab click can skip this whole
    // rebuild while the branch and tip are unchanged. The mirror tip is part of
    // that signature: the markers go stale if it advances under an unchanged
    // local tip (see commitsListIsCurrent).
    m_commitsLoadedRef = currentRef();
    m_commitsLoadedTip = loadedTip;
    m_commitsLoadedMirrorTip = currentMirrorTip();
    refreshCommitsBranchButton(); // keep the branch indicator + switcher in sync

    // Backfill the Files/+/− columns now that the rows are painted. This is the one
    // slow read (a --numstat diff of every commit in the window); running it after
    // the list is on screen is what makes opening Commits feel instant. loadGen
    // discards it if another load supersedes this one before it lands.
    QTimer::singleShot(0, this, [this, loadGen] { fillCommitStats(loadGen); });

    // Honour "closes #N" / "fixes #N" / "resolves #N" in commit messages by
    // closing and annotating the referenced issues (idempotent). This does its
    // own `git log` and a full issue-store scan, so defer it until after the
    // table has painted — it's a side effect, not part of rendering the list.
    QTimer::singleShot(0, this, [this] { applyCommitIssueClosures(); });
}

void MainWindow::fillCommitStats(int loadGen)
{
    // A newer load already replaced these rows (or the table is gone) — its own
    // fill will run, so this stale one would only write mismatched counts. Bail
    // before paying for the git read.
    if (loadGen != m_commitsLoadGen || !m_commitsTable)
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    // The expensive read loadCommits() left out: --numstat diffs every commit in
    // the window. Keep the window breathing across it (it can take ~1s on a large
    // history) — but the list is already on screen, so this only fills three
    // columns rather than blocking the whole tab. Mirror loadCommits()'s window:
    // the same cap, or no cap while a search is showing every commit.
    GitKeepAlive keepAlive;
    QStringList args{"log", "--numstat", "--format=%x1e%H"};
    args << "-n"
         << QString::number(m_commitsShowingAll ? kCommitSearchDepth
                                                : m_commitsLimit);
    args << currentRef();
    QByteArray out;
    if (!runGitCapture(dir, args, &out, nullptr))
        return;
    // The event-loop pump above can run another load while git worked; if so its
    // rows are different and these stats no longer line up. Drop them.
    if (loadGen != m_commitsLoadGen || !m_commitsTable)
        return;

    struct CommitStat { int files = 0; int adds = 0; int dels = 0; };
    QHash<QString, CommitStat> stats;
    for (const QByteArray &record : out.split('\x1e')) {
        const QStringList lines =
            QString::fromUtf8(record).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.isEmpty())
            continue;
        CommitStat s;
        for (int i = 1; i < lines.size(); ++i) {
            const QStringList cols = lines.at(i).split(QLatin1Char('\t'));
            if (cols.size() < 3)
                continue;
            ++s.files;
            bool ok = false;
            const int addCount = cols.at(0).toInt(&ok); // "-" for binary → skipped
            if (ok)
                s.adds += addCount;
            const int delCount = cols.at(1).toInt(&ok);
            if (ok)
                s.dels += delCount;
        }
        stats.insert(lines.first(), s); // full hash (%H) is the record's first line
    }

    // Drop the counts into each row by full hash — the table may be sorted into a
    // different order than git log returned them. Suspend sorting/signals so the
    // bulk update is a single repaint, not one re-sort per cell.
    QSignalBlocker block(m_commitsTable);
    const bool wasSorting = m_commitsTable->isSortingEnabled();
    m_commitsTable->setSortingEnabled(false);
    for (int row = 0; row < m_commitsTable->rowCount(); ++row) {
        const QTableWidgetItem *sum = m_commitsTable->item(row, kCommitSummaryCol);
        if (!sum)
            continue;
        const auto it = stats.constFind(sum->data(Qt::UserRole).toString());
        if (it == stats.constEnd())
            continue;
        const CommitStat &s = it.value();
        if (QTableWidgetItem *f = m_commitsTable->item(row, 3)) {
            f->setText(QString::number(s.files));
            f->setData(kTableSortRole, s.files);
        }
        if (QTableWidgetItem *a = m_commitsTable->item(row, 4)) {
            a->setText(QStringLiteral("+%1").arg(s.adds));
            a->setData(kTableSortRole, s.adds);
        }
        if (QTableWidgetItem *d = m_commitsTable->item(row, 5)) {
            d->setText(QString::fromUtf8("\xE2\x88\x92%1").arg(s.dels));
            d->setData(kTableSortRole, s.dels);
        }
    }
    m_commitsTable->setSortingEnabled(wasSorting);
}

void MainWindow::loadMoreCommits()
{
    if (m_commitsLoadingMore || !m_commitsHasMore || !m_commitsTable)
        return;
    m_commitsLoadingMore = true;
    // Keep the viewport where it is: the rows above are identical after the
    // deeper reload, so restoring the same scrollbar value lands in place.
    const int scrollVal = m_commitsTable->verticalScrollBar()->value();
    m_commitsLimit += 300;
    loadCommits(); // same ref → keeps the deepened window (no reset)
    m_commitsTable->verticalScrollBar()->setValue(scrollVal);
    m_commitsLoadingMore = false;
}

void MainWindow::generatePostFromSelectedCommits()
{
    if (!m_commitsTable)
        return;
    // Collect the selected rows (de-duped across selection ranges), in the table's
    // display order (newest first).
    QSet<int> rowSet;
    const auto ranges = m_commitsTable->selectedRanges();
    for (const QTableWidgetSelectionRange &r : ranges)
        for (int row = r.topRow(); row <= r.bottomRow(); ++row)
            rowSet.insert(row);
    QList<int> rows(rowSet.cbegin(), rowSet.cend());
    std::sort(rows.begin(), rows.end());
    if (rows.isEmpty()) {
        flashMessage(
            QStringLiteral("Select one or more commits first (Ctrl/Shift-click)."),
            true);
        return;
    }

    QStringList subjects;
    QStringList bullets;
    for (int row : std::as_const(rows)) {
        const QTableWidgetItem *sum = m_commitsTable->item(row, kCommitSummaryCol);
        const QTableWidgetItem *hashIt = m_commitsTable->item(row, kCommitHashCol);
        if (!sum)
            continue;
        const QString subject = sum->text().trimmed();
        if (subject.isEmpty())
            continue;
        // Strip the leading "▲ " unsynced marker from the hash cell, if present.
        const QString shortHash =
            hashIt ? hashIt->text().remove(QChar(0x25B2)).trimmed() : QString();
        subjects << subject;
        bullets << (shortHash.isEmpty()
                        ? QStringLiteral("- %1").arg(subject)
                        : QStringLiteral("- %1 (%2)").arg(subject, shortHash));
    }
    if (subjects.isEmpty()) {
        flashMessage(QStringLiteral("Couldn't read the selected commits."), true);
        return;
    }

    const QString repoName =
        (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
            ? m_repositories.at(m_repoDetailIndex).name
            : QString();
    const int n = subjects.size();

    // Release-notes style message.
    const QString message =
        QString::fromUtf8("What\xE2\x80\x99s new%1\n\n%2")
            .arg(repoName.isEmpty() ? QStringLiteral(":")
                                    : QStringLiteral(" in %1:").arg(repoName),
                 bullets.join(QLatin1Char('\n')));

    // X/Twitter post: a header plus as many subjects as fit under 280 characters.
    QString post = QString::fromUtf8("\xF0\x9F\x9A\x80 ");
    post += repoName.isEmpty()
                ? QStringLiteral("%1 update%2").arg(n).arg(n == 1 ? QString() : "s")
                : QStringLiteral("%1: %2 update%3")
                      .arg(repoName)
                      .arg(n)
                      .arg(n == 1 ? QString() : "s");
    const QString tail = QStringLiteral(" #buildinpublic");
    QStringList fragments;
    int budget = 280 - post.size() - tail.size() - 2; // " — " + joins
    for (const QString &s : std::as_const(subjects)) {
        const QString frag = s.length() > 70 ? s.left(67) + QStringLiteral("…") : s;
        const int cost = frag.size() + 2; // "; "
        if (cost > budget)
            break;
        fragments << frag;
        budget -= cost;
    }
    if (!fragments.isEmpty())
        post += QString::fromUtf8(" \xE2\x80\x94 ") + fragments.join(QStringLiteral("; "));
    post += tail;
    if (post.size() > 280)
        post = post.left(279) + QString::fromUtf8("\xE2\x80\xA6");

    // Show both in a copyable dialog.
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Generate post — %1 commit%2")
                              .arg(n)
                              .arg(n == 1 ? QString() : "s"));
    dialog.resize(560, 460);
    auto *outer = new QVBoxLayout(&dialog);

    auto addBlock = [&](const QString &title, const QString &body, bool small) {
        auto *label = new QLabel(title);
        label->setObjectName("sectionLabel");
        outer->addWidget(label);
        auto *edit = new QPlainTextEdit;
        edit->setPlainText(body);
        edit->setReadOnly(true);
        if (small)
            edit->setMaximumHeight(90);
        outer->addWidget(edit, small ? 0 : 1);
        auto *copyBtn = new QPushButton(QStringLiteral("Copy"));
        copyBtn->setProperty("buttonSize", "sm");
        copyBtn->setCursor(Qt::PointingHandCursor);
        connect(copyBtn, &QPushButton::clicked, this, [this, body, title] {
            QApplication::clipboard()->setText(body);
            flashMessage(title + QStringLiteral(" copied to the clipboard."));
        });
        auto *copyRow = new QHBoxLayout;
        copyRow->addStretch();
        copyRow->addWidget(copyBtn);
        outer->addLayout(copyRow);
    };
    addBlock(QString::fromUtf8("X / Twitter post (%1 chars)").arg(post.size()), post,
             true);
    addBlock(QStringLiteral("Release notes"), message, false);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto *closeRow = new QHBoxLayout;
    closeRow->addStretch();
    closeRow->addWidget(closeBtn);
    outer->addLayout(closeRow);

    dialog.exec();
}

// ---------------------------------------------------------------------------
// Top-bar global search ("search everything").
//
// One box in the top bar that, as you type, searches across every place the app
// knows about — sections, relays, nodes, repositories, and (for the open repo)
// its issues, pull requests, branches, files and commit messages — and lists the
// hits in a floating dropdown. Pick one (mouse or arrow keys + Enter) and it
// navigates straight there.
// ---------------------------------------------------------------------------
namespace {
// Payload roles on each dropdown item; the kind drives where activating it goes.
constexpr int kGsKindRole = Qt::UserRole;     // GlobalSearchKind
constexpr int kGsStr1Role = Qt::UserRole + 1; // primary payload (id/hash/path/branch)
constexpr int kGsStr2Role = Qt::UserRole + 2; // secondary payload (e.g. node name)
constexpr int kGsNumRole = Qt::UserRole + 3;  // numeric payload (index / number)
enum GlobalSearchKind {
    GsHeader = 0, // a non-selectable category label
    GsSection,    // m_sectionStack index
    GsRepo,       // m_repositories index
    GsNode,       // roster member (str1 = id, str2 = name)
    GsRelay,      // m_servers index
    GsIssue,      // issue number in the open repo
    GsPull,       // pull-request number in the open repo
    GsBranch,     // branch name (str1) in the open repo
    GsFile,       // path (str1) in the open repo
    GsCommit,     // full hash (str1) in the open repo
    GsDeepSearch, // open the streaming full-search page for the typed query
};
} // namespace

QWidget *MainWindow::createGlobalSearchBox()
{
    m_globalSearch = new QLineEdit;
    m_globalSearch->setObjectName("globalSearch");
    m_globalSearch->setClearButtonEnabled(true);
    m_globalSearch->setPlaceholderText(QString::fromUtf8("Search everything\xE2\x80\xA6"));
    m_globalSearch->setMinimumWidth(220);
    m_globalSearch->setMaximumWidth(440);
    m_globalSearch->addAction(themedOcticon("search", QColor(Theme::kTextTertiary), 14),
                              QLineEdit::LeadingPosition);
    m_globalSearch->setToolTip(QString::fromUtf8(
        "Search everything \xE2\x80\x94 sections, relays, nodes, repositories, and "
        "the open repo's issues, pull requests, branches, files and commits"));
    m_globalSearch->installEventFilter(this);

    // Floating results list. Parented to the window (not the short top bar) so it
    // overlays content below the bar without being clipped, and NoFocus so clicking
    // a result keeps the keyboard in the box (and never fires the box's focus-out).
    m_globalSearchPopup = new QListWidget(this);
    m_globalSearchPopup->setObjectName("globalSearchPopup");
    m_globalSearchPopup->setFocusPolicy(Qt::NoFocus);
    m_globalSearchPopup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_globalSearchPopup->setSelectionMode(QAbstractItemView::SingleSelection);
    m_globalSearchPopup->setMouseTracking(true);
    m_globalSearchPopup->hide();
    connect(m_globalSearchPopup, &QListWidget::itemClicked, this,
            &MainWindow::activateGlobalSearchItem);
    // Keep the keyboard highlight following the mouse so Enter always lands on the
    // row under the cursor.
    connect(m_globalSearchPopup, &QListWidget::itemEntered, this,
            [this](QListWidgetItem *it) {
                if (it && (it->flags() & Qt::ItemIsSelectable))
                    m_globalSearchPopup->setCurrentItem(it);
            });

    // Debounce: rebuild ~140 ms after the last keystroke, so fast typing (and the
    // git-backed commit grep) doesn't run on every character.
    m_globalSearchTimer = new QTimer(this);
    m_globalSearchTimer->setSingleShot(true);
    m_globalSearchTimer->setInterval(140);
    connect(m_globalSearchTimer, &QTimer::timeout, this,
            &MainWindow::rebuildGlobalSearchResults);
    connect(m_globalSearch, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (t.trimmed().isEmpty()) {
            m_globalSearchTimer->stop();
            hideGlobalSearchPopup();
        } else {
            m_globalSearchTimer->start();
        }
    });
    return m_globalSearch;
}

void MainWindow::rebuildGlobalSearchResults()
{
    if (!m_globalSearch || !m_globalSearchPopup)
        return;
    const QString needle = m_globalSearch->text().trimmed().toLower();
    if (needle.isEmpty()) {
        hideGlobalSearchPopup();
        return;
    }
    m_globalSearchPopup->clear();
    // Original-case text drives the deep-search row and the results page.
    const QString query = m_globalSearch->text().trimmed();

    int total = 0;
    constexpr int kMaxTotal = 80;

    auto addHeader = [&](const QString &title) {
        auto *h = new QListWidgetItem(title.toUpper());
        h->setData(kGsKindRole, GsHeader);
        h->setFlags(Qt::NoItemFlags); // not selectable, not hovered
        QFont f = h->font();
        f.setBold(true);
        h->setFont(f);
        h->setForeground(QColor("#8b949e"));
        m_globalSearchPopup->addItem(h);
    };
    auto addResult = [&](const QString &icon, const QColor &iconColor,
                         const QString &text, int kind, const QString &s1,
                         const QString &s2, int num) -> bool {
        if (total >= kMaxTotal)
            return false;
        auto *it = new QListWidgetItem(text);
        if (!icon.isEmpty())
            it->setIcon(themedOcticon(icon, iconColor, 15));
        it->setData(kGsKindRole, kind);
        it->setData(kGsStr1Role, s1);
        it->setData(kGsStr2Role, s2);
        it->setData(kGsNumRole, num);
        m_globalSearchPopup->addItem(it);
        ++total;
        return true;
    };

    // Always-first row: run the full, streaming search over files, commit
    // messages and the diff history (this is what Enter triggers).
    addResult("search", QColor("#58a6ff"),
              QString::fromUtf8("Search files, commits & history for \xE2\x80\x9C%1\xE2\x80\x9D")
                  .arg(query),
              GsDeepSearch, query, QString(), 0);

    // --- Sections / "go to" -------------------------------------------------
    struct Sec { const char *label; const char *icon; int index; };
    static const Sec kSections[] = {
        {"Home / Repositories", "home", 0},
        {"Chat", "comment", 2},
        {"Notifications", "bell", 3},
        {"Network log", "list-unordered", 4},
        {"Leaderboards", "graph", 5},
        {"Settings", "gear", 1},
    };
    bool header = false;
    for (const Sec &s : kSections) {
        if (!QString::fromLatin1(s.label).toLower().contains(needle))
            continue;
        if (!header) { addHeader(QStringLiteral("Go to")); header = true; }
        addResult(QString::fromLatin1(s.icon), QColor("#8b949e"),
                  QString::fromLatin1(s.label), GsSection, QString(), QString(),
                  s.index);
    }

    // --- Repositories -------------------------------------------------------
    header = false;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &r = m_repositories.at(i);
        const QString full = r.owner.isEmpty() ? r.name : r.owner + "/" + r.name;
        if (!full.toLower().contains(needle) &&
            !r.description.toLower().contains(needle))
            continue;
        if (!header) { addHeader(QStringLiteral("Repositories")); header = true; }
        if (!addResult("repo", QColor("#8b949e"), full, GsRepo, QString(),
                       QString(), i))
            break;
    }

    // --- Nodes (roster) -----------------------------------------------------
    header = false;
    for (const MemberInfo &m : std::as_const(m_homeRoster)) {
        if (!m.name.toLower().contains(needle) && !m.id.toLower().contains(needle))
            continue;
        if (!header) { addHeader(QStringLiteral("Nodes")); header = true; }
        QString label = m.name.isEmpty() ? m.id.left(12) : m.name;
        if (m.self)
            label += QStringLiteral("  (you)");
        else if (!m.note.isEmpty())
            label += QStringLiteral("  ") + m.note;
        if (!addResult("person", m.online ? QColor("#3fb950") : QColor("#8b949e"),
                       label, GsNode, m.id, m.name, 0))
            break;
    }

    // --- Relays -------------------------------------------------------------
    header = false;
    for (int i = 0; i < m_servers.size(); ++i) {
        const QString host = serverHost(m_servers.at(i).url);
        if (!host.toLower().contains(needle))
            continue;
        if (!header) { addHeader(QStringLiteral("Relays")); header = true; }
        const QString label =
            i == m_activeServer ? host + QStringLiteral("  (active)") : host;
        if (!addResult("server", QColor("#8b949e"), label, GsRelay, QString(),
                       QString(), i))
            break;
    }

    // --- The open repository's contents -------------------------------------
    const bool repoOpen =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size();
    if (repoOpen) {
        const QString repoName = m_repositories.at(m_repoDetailIndex).name;
        const QString suffix = QString::fromUtf8(" \xC2\xB7 ") + repoName;

        // Issues (already in memory for the open repo).
        header = false;
        for (const Issue &iss : std::as_const(m_currentIssues)) {
            if (iss.isDeleted())
                continue;
            const QString hay =
                QStringLiteral("#%1 %2").arg(iss.number).arg(iss.title);
            if (!hay.toLower().contains(needle))
                continue;
            if (!header) { addHeader(QStringLiteral("Issues") + suffix); header = true; }
            const bool open = iss.status == QLatin1String("open");
            if (!addResult(open ? "issue-opened" : "check-circle",
                           open ? QColor("#3fb950") : QColor("#a371f7"),
                           QStringLiteral("#%1  %2").arg(iss.number).arg(iss.title),
                           GsIssue, QString(), QString(), iss.number))
                break;
        }

        // Pull requests.
        header = false;
        for (const PullRequest &pr : std::as_const(m_currentPulls)) {
            const QString hay =
                QStringLiteral("#%1 %2").arg(pr.number).arg(pr.title);
            if (!hay.toLower().contains(needle))
                continue;
            if (!header) {
                addHeader(QStringLiteral("Pull requests") + suffix);
                header = true;
            }
            const QColor c = pr.status == QLatin1String("merged") ? QColor("#a371f7")
                             : pr.status == QLatin1String("open") ? QColor("#3fb950")
                                                                  : QColor("#8b949e");
            if (!addResult("git-pull-request", c,
                           QStringLiteral("#%1  %2").arg(pr.number).arg(pr.title),
                           GsPull, QString(), QString(), pr.number))
                break;
        }

        // Branches.
        header = false;
        const QString ref = currentRef();
        for (const QString &b : repoBranches()) {
            if (!b.toLower().contains(needle))
                continue;
            if (!header) { addHeader(QStringLiteral("Branches") + suffix); header = true; }
            if (!addResult("repo-forked", QColor("#8b949e"),
                           b == ref ? b + QStringLiteral("  (current)") : b,
                           GsBranch, b, QString(), 0))
                break;
        }

        // Files (reuse the in-memory go-to-file index, no extra git call).
        if (m_fileCompleter && m_fileCompleter->model()) {
            QAbstractItemModel *fm = m_fileCompleter->model();
            header = false;
            int fileHits = 0;
            for (int r = 0; r < fm->rowCount() && fileHits < 12; ++r) {
                const QString path = fm->index(r, 0).data().toString();
                if (!path.toLower().contains(needle))
                    continue;
                if (!header) { addHeader(QStringLiteral("Files") + suffix); header = true; }
                if (!addResult("file", QColor("#8b949e"), path, GsFile, path,
                               QString(), 0))
                    break;
                ++fileHits;
            }
        }

        // Commit messages (one bounded, literal git grep; needs >=2 chars).
        if (needle.size() >= 2) {
            const QString dir = repoGitDir();
            QByteArray out;
            if (!dir.isEmpty() &&
                runGitCapture(dir,
                              {"log", "--fixed-strings", "-i", "--grep=" + needle,
                               "-n", "8", "--format=%H%x1f%h%x1f%s", ref},
                              &out, nullptr)) {
                header = false;
                for (const QByteArray &line : out.split('\n')) {
                    if (line.trimmed().isEmpty())
                        continue;
                    const QStringList f = QString::fromUtf8(line).split(QChar(0x1f));
                    if (f.size() < 3)
                        continue;
                    if (!header) { addHeader(QStringLiteral("Commits") + suffix); header = true; }
                    if (!addResult("git-branch", QColor("#8b949e"),
                                   QStringLiteral("%1  %2").arg(f.at(1), f.at(2)),
                                   GsCommit, f.at(0), QString(), 0))
                        break;
                }
            }
        }
    }

    if (total == 0) {
        auto *none = new QListWidgetItem(QStringLiteral("No matches"));
        none->setFlags(Qt::NoItemFlags);
        none->setForeground(QColor("#8b949e"));
        m_globalSearchPopup->addItem(none);
    }

    // Pre-select the first real result so Enter works without arrowing first.
    for (int r = 0; r < m_globalSearchPopup->count(); ++r) {
        if (m_globalSearchPopup->item(r)->flags() & Qt::ItemIsSelectable) {
            m_globalSearchPopup->setCurrentRow(r);
            break;
        }
    }
    positionGlobalSearchPopup();
    m_globalSearchPopup->raise();
    m_globalSearchPopup->show();
}

void MainWindow::positionGlobalSearchPopup()
{
    if (!m_globalSearch || !m_globalSearchPopup)
        return;
    const QPoint topLeft =
        m_globalSearch->mapTo(this, QPoint(0, m_globalSearch->height() + 4));
    int w = std::max(m_globalSearch->width(), 380);
    w = std::min(w, width() - topLeft.x() - 12);
    w = std::max(w, 200);
    int rowsH = 8;
    for (int r = 0; r < m_globalSearchPopup->count(); ++r)
        rowsH += m_globalSearchPopup->sizeHintForRow(r);
    m_globalSearchPopup->setGeometry(topLeft.x(), topLeft.y(), w,
                                     std::min(rowsH, 440));
}

void MainWindow::moveGlobalSearchSelection(int delta)
{
    if (!m_globalSearchPopup || !m_globalSearchPopup->isVisible())
        return;
    const int count = m_globalSearchPopup->count();
    if (count == 0)
        return;
    int row = m_globalSearchPopup->currentRow();
    for (int step = 0; step < count; ++step) {
        row += delta;
        if (row < 0)
            row = count - 1;
        else if (row >= count)
            row = 0;
        QListWidgetItem *it = m_globalSearchPopup->item(row);
        if (it && (it->flags() & Qt::ItemIsSelectable)) {
            m_globalSearchPopup->setCurrentRow(row);
            m_globalSearchPopup->scrollToItem(it);
            return;
        }
    }
}

void MainWindow::activateGlobalSearchItem(QListWidgetItem *item)
{
    if (!item || !(item->flags() & Qt::ItemIsSelectable))
        return;
    const int kind = item->data(kGsKindRole).toInt();
    const QString s1 = item->data(kGsStr1Role).toString();
    const QString s2 = item->data(kGsStr2Role).toString();
    const int num = item->data(kGsNumRole).toInt();

    hideGlobalSearchPopup();
    if (m_globalSearch) {
        QSignalBlocker block(m_globalSearch); // clearing must not re-trigger a rebuild
        m_globalSearch->clear();
    }

    const bool repoOpen =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size();
    auto clickRepoTab = [this](int idx) {
        if (m_repoDetailTabs && m_repoDetailTabs->button(idx))
            m_repoDetailTabs->button(idx)->click(); // switches tab + loads its data
    };

    switch (kind) {
    case GsDeepSearch:
        openSearchResultsPage(s1); // s1 holds the (pre-clear) query text
        break;
    case GsSection:
        showSection(num);
        break;
    case GsRepo:
        openRepoDetailDeferred(num);
        break;
    case GsNode:
        showSection(0);
        showNodeProfile(s1, s2);
        break;
    case GsRelay:
        switchToServer(num);
        break;
    case GsIssue:
        if (repoOpen) { showSection(0); clickRepoTab(2); showIssue(num); }
        break;
    case GsPull:
        if (repoOpen) { showSection(0); clickRepoTab(4); showPull(num); }
        break;
    case GsBranch:
        if (repoOpen) { showSection(0); setRepoBranch(s1); }
        break;
    case GsFile:
        if (repoOpen) { showSection(0); clickRepoTab(0); openRepoFile(s1); }
        break;
    case GsCommit:
        if (repoOpen) { showSection(0); showOverviewCommits(); showCommit(s1); }
        break;
    default:
        break;
    }
}

void MainWindow::hideGlobalSearchPopup()
{
    if (m_globalSearchPopup)
        m_globalSearchPopup->hide();
}

// ---------------------------------------------------------------------------
// Back / forward navigation trail (sits just left of the search box).
//
// As you move between sections and repositories, scheduleNavRecord() captures
// where you landed onto a browser-style history. Back and Forward replay it
// without recording new entries, so the trail behaves like a web browser's.
// ---------------------------------------------------------------------------
QWidget *MainWindow::createNavHistoryButtons()
{
    auto *holder = new QWidget;
    auto *row = new QHBoxLayout(holder);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);

    auto makeButton = [this](const QString &icon, const QString &tip,
                             void (MainWindow::*slot)()) {
        auto *b = new QPushButton;
        b->setObjectName("relayOpenButton"); // transparent icon-button styling
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(30, 30);
        setOcticon(b, icon, 16);
        b->setToolTip(tip);
        b->setEnabled(false);
        connect(b, &QPushButton::clicked, this, slot);
        return b;
    };
    m_navBackButton = makeButton("chevron-left", QStringLiteral("Back"),
                                 &MainWindow::navigateBack);
    m_navForwardButton = makeButton("chevron-right", QStringLiteral("Forward"),
                                    &MainWindow::navigateForward);
    row->addWidget(m_navBackButton);
    row->addWidget(m_navForwardButton);
    return holder;
}

void MainWindow::scheduleNavRecord()
{
    // A single user action (e.g. picking a search result) can fan out into several
    // showSection()/openRepoDetail() calls; coalesce them into one capture of the
    // settled location once the call stack unwinds.
    if (m_navRestoring || m_navRecordPending)
        return;
    m_navRecordPending = true;
    QTimer::singleShot(0, this, &MainWindow::recordNavLocation);
}

void MainWindow::recordNavLocation()
{
    m_navRecordPending = false;
    if (m_navRestoring)
        return;
    NavPlace here;
    here.section = m_sectionStack ? m_sectionStack->currentIndex() : 0;
    // The repo only distinguishes a place inside the Code section; elsewhere the
    // detail panel isn't shown, so normalise it out to avoid phantom entries.
    here.repoIndex = (here.section == 0) ? m_repoDetailIndex : -1;
    // Inside an open repo, the visible tab (Code / Commits / Issues / Pulls / …)
    // is part of the place too, so switching tabs is a step Back can return to.
    here.detailTab = (here.repoIndex >= 0 && m_repoDetailStack)
                         ? m_repoDetailStack->currentIndex()
                         : -1;

    if (m_navHistoryIndex >= 0 && m_navHistoryIndex < m_navHistory.size() &&
        m_navHistory.at(m_navHistoryIndex) == here)
        return; // already standing here — nothing moved

    // Branching off mid-trail drops everything ahead of the cursor.
    while (m_navHistory.size() > m_navHistoryIndex + 1)
        m_navHistory.removeLast();
    m_navHistory.append(here);
    constexpr int kMaxNavHistory = 50;
    while (m_navHistory.size() > kMaxNavHistory)
        m_navHistory.removeFirst();
    m_navHistoryIndex = m_navHistory.size() - 1;
    updateNavHistoryButtons();
}

void MainWindow::restoreNavEntry(int index)
{
    if (index < 0 || index >= m_navHistory.size())
        return;
    const NavPlace place = m_navHistory.at(index);
    m_navHistoryIndex = index;
    // Replay without recording: the location we land on already equals this entry,
    // and recording would truncate the forward trail we're walking through.
    m_navRestoring = true;
    if (place.section == 0 && place.repoIndex >= 0 &&
        place.repoIndex < m_repositories.size() &&
        place.repoIndex != m_repoDetailIndex) {
        openRepoDetailDeferred(place.repoIndex); // lands on the Code section itself
        // The open runs on the next event-loop turn and settles on its default
        // tab; re-select the recorded tab (and only then drop the guard) once it
        // has, so a single Back lands on the exact tab without recording a step.
        const NavPlace target = place;
        QTimer::singleShot(0, this, [this, target] {
            applyNavDetailTab(target);
            m_navRestoring = false;
            updateNavHistoryButtons();
        });
        return;
    }
    showSection(place.section);
    applyNavDetailTab(place);
    m_navRestoring = false;
    updateNavHistoryButtons();
}

// Re-select the repo tab a recorded place was on, driving it through the same
// click path so the tab's data load runs. No-op outside an open repo, or when
// that tab is already showing.
void MainWindow::applyNavDetailTab(const NavPlace &place)
{
    if (place.section != 0 || place.repoIndex < 0 || place.detailTab < 0)
        return;
    if (!m_repoDetailTabs || !m_repoDetailStack)
        return;
    if (m_repoDetailStack->currentIndex() == place.detailTab)
        return; // already on this tab
    if (QAbstractButton *b = m_repoDetailTabs->button(place.detailTab)) {
        b->click(); // switches the tab and loads its data, like a real click
        return;
    }
    // Agents (id 3) lost its top-bar button when it moved to the footer status
    // strip (adhoc #178), so there's no button here to click and this used to
    // silently no-op — Back/Forward would get stuck whenever the recorded spot
    // was the Agents tab. Drive the stack directly instead, refreshing its data
    // the same way switchToAgentsTab does.
    if (place.detailTab == 3) {
        m_repoDetailStack->setCurrentIndex(place.detailTab);
        reloadAgents();
    }
}

void MainWindow::navigateBack()
{
    if (m_navHistoryIndex > 0)
        restoreNavEntry(m_navHistoryIndex - 1);
}

void MainWindow::navigateForward()
{
    if (m_navHistoryIndex >= 0 && m_navHistoryIndex < m_navHistory.size() - 1)
        restoreNavEntry(m_navHistoryIndex + 1);
}

void MainWindow::updateNavHistoryButtons()
{
    if (m_navBackButton)
        m_navBackButton->setEnabled(m_navHistoryIndex > 0);
    if (m_navForwardButton)
        m_navForwardButton->setEnabled(m_navHistoryIndex >= 0 &&
                                       m_navHistoryIndex < m_navHistory.size() - 1);
}

namespace {
// Result-tree payload roles for the deep-search page.
constexpr int kSrKindRole = Qt::UserRole;     // SrBucket | SrFile | SrCommit
constexpr int kSrPathRole = Qt::UserRole + 1; // file path or commit hash
constexpr int kSrBaseRole = Qt::UserRole + 2; // a bucket's base title (for its count)
constexpr int kSrLineRole = Qt::UserRole + 3; // file match's line number
enum { SrBucket = 0, SrFile = 1, SrCommit = 2 };
enum SearchCat { ScFile = 0, ScMessage, ScHistory };
} // namespace

// Amber-highlight every (case-insensitive) occurrence of `term` in a text
// document, returning selections either editor type can apply. Used to mark the
// spot a search result matched once its file or diff is opened.
static QList<QTextEdit::ExtraSelection> termSelections(QTextDocument *doc,
                                                       const QString &term)
{
    QList<QTextEdit::ExtraSelection> sels;
    if (!doc || term.isEmpty())
        return sels;
    QTextCharFormat fmt;
    fmt.setBackground(QColor("#e3b341"));
    fmt.setForeground(QColor("#0d1117"));
    QTextCursor c = doc->find(term, 0);
    while (!c.isNull()) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = c;
        sel.format = fmt;
        sels.append(sel);
        if (sels.size() >= 5000)
            break; // safety cap on pathological match counts
        c = doc->find(term, c);
    }
    return sels;
}

QWidget *MainWindow::buildSearchResultsSection()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 12, 16, 16);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(8);
    auto *back = new QPushButton(QStringLiteral("Back"));
    back->setObjectName("ghostButton");
    back->setCursor(Qt::PointingHandCursor);
    setOcticon(back, "arrow-left", 16);
    connect(back, &QPushButton::clicked, this, [this] { showSection(0); });
    m_searchResultsTitle = new QLabel;
    m_searchResultsTitle->setObjectName("searchResultsTitle");
    m_searchResultsTitle->setTextFormat(Qt::RichText);
    m_searchResultsStop = new QPushButton(QStringLiteral("Stop"));
    m_searchResultsStop->setObjectName("ghostButton");
    m_searchResultsStop->setCursor(Qt::PointingHandCursor);
    setOcticon(m_searchResultsStop, "x", 14);
    m_searchResultsStop->hide();
    connect(m_searchResultsStop, &QPushButton::clicked, this, [this] { stopSearch(); });
    headerRow->addWidget(back);
    headerRow->addWidget(m_searchResultsTitle, 1);
    headerRow->addWidget(m_searchResultsStop);
    layout->addLayout(headerRow);

    m_searchResultsStatus = new QLabel;
    m_searchResultsStatus->setObjectName("navCaption");
    layout->addWidget(m_searchResultsStatus);

    m_searchResultsTree = new QTreeWidget;
    m_searchResultsTree->setObjectName("searchResultsTree");
    m_searchResultsTree->setHeaderHidden(true);
    m_searchResultsTree->setColumnCount(1);
    m_searchResultsTree->setUniformRowHeights(true);
    m_searchResultsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_searchResultsTree->setExpandsOnDoubleClick(false);
    m_searchResultsTree->setCursor(Qt::PointingHandCursor);
    connect(m_searchResultsTree, &QTreeWidget::itemClicked, this,
            &MainWindow::onSearchResultActivated);
    layout->addWidget(m_searchResultsTree, 1);
    return page;
}

void MainWindow::openSearchResultsPage(const QString &query)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return;
    m_searchPageQuery = q;
    showSection(6);
    if (m_searchResultsTitle)
        m_searchResultsTitle->setText(
            QString::fromUtf8("<b>Results for \xE2\x80\x9C%1\xE2\x80\x9D</b>")
                .arg(q.toHtmlEscaped()));

    stopSearch(); // cancel any run still in flight
    if (m_searchResultsTree)
        m_searchResultsTree->clear();

    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        if (m_searchResultsStatus)
            m_searchResultsStatus->setText(
                QStringLiteral("Open a repository to search its files and history."));
        return;
    }
    const QString ref = currentRef();

    auto makeBucket = [this](const QString &base, const QString &icon) {
        auto *b = new QTreeWidgetItem(m_searchResultsTree);
        b->setData(0, kSrKindRole, SrBucket);
        b->setData(0, kSrBaseRole, base);
        b->setIcon(0, themedOcticon(icon, QColor("#8b949e"), 15));
        QFont f = b->font(0);
        f.setBold(true);
        b->setFont(0, f);
        b->setText(0, base + QString::fromUtf8(" \xE2\x80\x94 0"));
        b->setExpanded(true);
        return b;
    };
    QTreeWidgetItem *fileBucket =
        makeBucket(QStringLiteral("Files (current tree)"), "file");
    QTreeWidgetItem *msgBucket =
        makeBucket(QStringLiteral("Commit messages"), "git-branch");
    QTreeWidgetItem *histBucket = makeBucket(
        QString::fromUtf8("History \xE2\x80\x94 diffs that touched it"),
        "git-pull-request");

    m_searchPending = 0;
    // 1) File text in the current tree (literal, case-insensitive).
    startSearchProcess(dir, {"grep", "-n", "-I", "-i", "-F", "-e", q, ref},
                       fileBucket, ScFile);
    // 2) Commit messages.
    startSearchProcess(dir,
                       {"log", "-i", "--fixed-strings", "--grep=" + q,
                        "--format=%H%x1f%h%x1f%ar%x1f%s", ref},
                       msgBucket, ScMessage);
    // 3) Anywhere in history a diff added or removed a line containing it (git's
    //    "pickaxe"). The literal is escaped into a regex for -G.
    startSearchProcess(dir,
                       {"log", "-i", "-G" + QRegularExpression::escape(q),
                        "--format=%H%x1f%h%x1f%ar%x1f%s", ref},
                       histBucket, ScHistory);
    updateSearchStatus();
}

void MainWindow::startSearchProcess(const QString &dir, const QStringList &gitArgs,
                                    QTreeWidgetItem *bucket, int cat)
{
    if (!bucket)
        return;
    auto *process = new QProcess(this);
    process->setWorkingDirectory(dir);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    auto buffer = std::make_shared<QByteArray>();
    const QString ref = currentRef();

    // Turn one finished output line into a child row under the bucket.
    auto addLine = [this, bucket, cat, ref, process](const QByteArray &raw) {
        if (bucket->childCount() >= 500) { // cap the live list; stop this search
            process->kill();
            return;
        }
        const QString line = QString::fromUtf8(raw);
        if (line.trimmed().isEmpty())
            return;
        if (cat == ScFile) {
            // git grep against a tree: "<ref>:<path>:<lineno>:<text>".
            QString rest = line;
            const QString prefix = ref + QLatin1Char(':');
            if (rest.startsWith(prefix))
                rest = rest.mid(prefix.size());
            const int c1 = rest.indexOf(QLatin1Char(':'));
            const int c2 = c1 < 0 ? -1 : rest.indexOf(QLatin1Char(':'), c1 + 1);
            if (c1 < 0 || c2 < 0)
                return;
            const QString path = rest.left(c1);
            const QString lineNo = rest.mid(c1 + 1, c2 - c1 - 1);
            const QString text = rest.mid(c2 + 1).trimmed();
            auto *child = new QTreeWidgetItem(bucket);
            child->setData(0, kSrKindRole, SrFile);
            child->setData(0, kSrPathRole, path);
            child->setData(0, kSrLineRole, lineNo.toInt());
            const QString shown = text.length() > 160
                                      ? text.left(157) + QString::fromUtf8("\xE2\x80\xA6")
                                      : text;
            child->setText(0, QStringLiteral("%1:%2  %3").arg(path, lineNo, shown));
            child->setToolTip(0, QStringLiteral("%1:%2").arg(path, lineNo));
        } else {
            // git log --format: "<H>\x1f<h>\x1f<ar>\x1f<s>".
            const QStringList f = line.split(QChar(0x1f));
            if (f.size() < 4)
                return;
            auto *child = new QTreeWidgetItem(bucket);
            child->setData(0, kSrKindRole, SrCommit);
            child->setData(0, kSrPathRole, f.at(0));
            child->setText(0, QString::fromUtf8("%1  %2  \xC2\xB7  %3")
                                  .arg(f.at(1), f.at(3), f.at(2)));
            child->setToolTip(0, f.at(3));
        }
        // Live count in the bucket header.
        bucket->setText(0, bucket->data(0, kSrBaseRole).toString() +
                               QString::fromUtf8(" \xE2\x80\x94 %1")
                                   .arg(bucket->childCount()));
    };

    connect(process, &QProcess::readyReadStandardOutput, this,
            [process, buffer, addLine] {
                buffer->append(process->readAllStandardOutput());
                int nl;
                while ((nl = buffer->indexOf('\n')) >= 0) {
                    addLine(buffer->left(nl));
                    buffer->remove(0, nl + 1);
                }
            });
    connect(process, &QProcess::finished, this,
            [this, process, buffer, addLine](int, QProcess::ExitStatus) {
                if (!buffer->isEmpty())
                    addLine(*buffer); // trailing line with no newline
                m_searchProcs.removeOne(process);
                process->deleteLater();
                if (m_searchPending > 0)
                    --m_searchPending;
                updateSearchStatus();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart)
                    return; // a crash still emits finished(); let that path clean up
                m_searchProcs.removeOne(process);
                process->deleteLater();
                if (m_searchPending > 0)
                    --m_searchPending;
                updateSearchStatus();
            });

    m_searchProcs.append(process);
    ++m_searchPending;
    process->start(QStringLiteral("git"), gitArgs);
}

void MainWindow::onSearchResultActivated(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    const int kind = item->data(0, kSrKindRole).toInt();
    if (kind == SrBucket) {
        item->setExpanded(!item->isExpanded()); // click a header to fold / unfold
        return;
    }
    const QString payload = item->data(0, kSrPathRole).toString();
    const bool repoOpen =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size();
    if (!repoOpen || payload.isEmpty())
        return;
    showSection(0);
    const QString term = m_searchPageQuery;
    if (kind == SrFile) {
        if (m_repoDetailTabs && m_repoDetailTabs->button(0))
            m_repoDetailTabs->button(0)->click();
        openRepoFile(payload);
        const int line = item->data(0, kSrLineRole).toInt();
        // Defer so the editor tab is laid out before we scroll to the line.
        QTimer::singleShot(0, this, [this, payload, line, term] {
            auto *edit = qobject_cast<QPlainTextEdit *>(m_openFileTabs.value(payload));
            if (!edit)
                return;
            QList<QTextEdit::ExtraSelection> sels =
                termSelections(edit->document(), term);
            // Emphasise the whole matched line beneath the term highlights.
            QTextCursor lc(edit->document());
            lc.movePosition(QTextCursor::Start);
            if (line > 1)
                lc.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line - 1);
            QTextEdit::ExtraSelection lineSel;
            lineSel.cursor = lc;
            lineSel.format.setBackground(QColor(31, 111, 235, 60));
            lineSel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sels.prepend(lineSel);
            edit->setExtraSelections(sels);
            edit->setTextCursor(lc);
            edit->centerCursor();
            edit->setFocus();
        });
    } else if (kind == SrCommit) {
        showOverviewCommits();
        showCommit(payload);
        // Highlight every occurrence of the term in the rendered diff and scroll
        // to the first, so the matched line/area is obvious.
        QTimer::singleShot(0, this, [this, term] {
            if (!m_commitDiffView)
                return;
            QList<QTextEdit::ExtraSelection> sels =
                termSelections(m_commitDiffView->document(), term);
            m_commitDiffView->setExtraSelections(sels);
            if (!sels.isEmpty()) {
                m_commitDiffView->setTextCursor(sels.first().cursor);
                m_commitDiffView->ensureCursorVisible();
            }
        });
    }
}

void MainWindow::stopSearch()
{
    for (QProcess *p : std::as_const(m_searchProcs)) {
        if (!p)
            continue;
        disconnect(p, nullptr, this, nullptr); // silence callbacks before killing
        p->kill();
        p->deleteLater();
    }
    m_searchProcs.clear();
    m_searchPending = 0;
    updateSearchStatus();
}

void MainWindow::updateSearchStatus()
{
    if (m_searchResultsStop)
        m_searchResultsStop->setVisible(m_searchPending > 0);
    if (!m_searchResultsStatus)
        return;
    int hits = 0;
    if (m_searchResultsTree)
        for (int i = 0; i < m_searchResultsTree->topLevelItemCount(); ++i)
            hits += m_searchResultsTree->topLevelItem(i)->childCount();
    if (m_searchPending > 0)
        m_searchResultsStatus->setText(
            QString::fromUtf8(
                "Searching\xE2\x80\xA6  %1 match%2 so far  \xC2\xB7  %3 still running")
                .arg(hits)
                .arg(hits == 1 ? QString() : QStringLiteral("es"))
                .arg(m_searchPending));
    else
        m_searchResultsStatus->setText(
            QString::fromUtf8("Done \xE2\x80\x94 %1 match%2")
                .arg(hits)
                .arg(hits == 1 ? QString() : QStringLiteral("es")));
}


void MainWindow::showCommitList()
{
    if (m_commitsStack)
        m_commitsStack->setCurrentIndex(0);
}

// Select the newest commit (row 0 — the table sorts Date-descending) and open
// its diff, so entering the Commits tab lands straight on the latest change set
// instead of an unselected list. Falls back to the list view when empty.
void MainWindow::openMostRecentCommit()
{
    if (!m_commitsTable || m_commitsTable->rowCount() == 0) {
        showCommitList();
        return;
    }
    QTableWidgetItem *item = m_commitsTable->item(0, kCommitSummaryCol);
    if (!item) {
        showCommitList();
        return;
    }
    // Highlight row 0 without re-triggering showCommit via currentCellChanged;
    // we call it explicitly below so the selection and the open stay in sync.
    {
        QSignalBlocker block(m_commitsTable);
        m_commitsTable->setCurrentCell(0, kCommitSummaryCol);
    }
    showCommit(item->data(Qt::UserRole).toString());
}

namespace {

// Top-level trees the ForkMesh client owns as serialized data (issues, pull
// requests, commit comments, coves). Conflicts here during a history rewrite are
// expected and safe to resolve mechanically: the client reuses issue/pull
// numbers, so dropping an old commit can leave an earlier "N" in place and a
// later "<thing> N: open" then replays onto it. Source files are not in this set
// and must never be auto-resolved.
bool isInboxDataPath(const QString &rel)
{
    return rel.startsWith(QLatin1String("issues/"))
        || rel.startsWith(QLatin1String("pulls/"))
        || rel.startsWith(QLatin1String("commits/"))
        || rel.startsWith(QLatin1String(".forkmesh/"));
}

// Absolute path of the in-progress interactive-rebase state directory, or empty
// when no rebase is underway (so this also doubles as "is a rebase running?").
QString rebaseMergeDir(const QString &dir)
{
    QByteArray out;
    if (!runGitCapture(dir, {"rev-parse", "--git-path", "rebase-merge"}, &out,
                       nullptr))
        return QString();
    QString path = QString::fromUtf8(out).trimmed();
    if (path.isEmpty())
        return QString();
    if (!QDir::isAbsolutePath(path))
        path = dir + "/" + path;
    return QFileInfo::exists(path) ? path : QString();
}

// Files the current rebase step left unmerged.
QStringList rebaseUnmergedFiles(const QString &dir)
{
    const QByteArray out =
        gitCaptureStdout(dir, {"diff", "--name-only", "--diff-filter=U"});
    return QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
}

// Drive an in-progress "drop commit" rebase to completion. The replay only
// conflicts on the reused-number data files described above, so for each one we
// keep the version from the commit being replayed (its `--theirs` side: rebase
// swaps the conflict sides) and continue. A conflict in any other file is a real
// code conflict we can't safely guess at, so we stop and report which files —
// the caller then aborts, leaving history untouched.
bool driveDeleteCommitRebase(const QString &dir, QString *err)
{
    // The loop exits when the rebase finishes (its state directory disappears);
    // the large cap is only an infinite-loop backstop.
    for (int step = 0; step < 100000; ++step) {
        if (rebaseMergeDir(dir).isEmpty())
            return true; // rebase finished cleanly

        const QStringList unmerged = rebaseUnmergedFiles(dir);
        QStringList blockers;
        for (const QString &f : unmerged)
            if (!isInboxDataPath(f))
                blockers << f;
        if (!blockers.isEmpty()) {
            if (err)
                *err = QStringLiteral(
                           "Can't remove this commit automatically — replaying a "
                           "later commit conflicts in %1. Resolve those files by "
                           "hand, or revert the commit instead. History was left "
                           "unchanged.")
                           .arg(blockers.join(QStringLiteral(", ")));
            return false;
        }

        // Reproduce the replayed commit's own version of each data file.
        for (const QString &f : unmerged) {
            if (runGitCapture(dir, {"checkout", "--theirs", "--", f}, nullptr,
                              nullptr))
                runGitCapture(dir, {"add", "--", f}, nullptr, nullptr);
            else
                // No `--theirs` side: the replayed commit deleted the file.
                runGitCapture(dir, {"rm", "--force", "--", f}, nullptr, nullptr);
        }

        // If the resolution leaves nothing to commit, skip the now-empty step;
        // otherwise continue. core.editor=true stops --continue from blocking on
        // a commit-message editor. The exit code is intentionally ignored:
        // --continue also returns non-zero when it stops at the next conflict,
        // which the next iteration handles.
        const bool nothingToCommit =
            runGitCapture(dir, {"diff", "--cached", "--quiet"}, nullptr, nullptr);
        const QStringList advance =
            nothingToCommit
                ? QStringList{"rebase", "--skip"}
                : QStringList{"-c", "core.editor=true", "rebase", "--continue"};
        runGitCapture(dir, advance, nullptr, nullptr);
    }

    if (err)
        *err = QStringLiteral(
            "Gave up removing the commit after too many conflict steps; history "
            "was left unchanged.");
    return false;
}

} // namespace

// Drop a single commit from the browsed branch's history, replaying every later
// commit onto the one before it. Owner-only (the working copy is the source of
// truth); the rewritten branch then diverges from the served mirror until the
// next publish, so we refresh the publish button afterwards.
void MainWindow::deleteCommit(const QString &hash)
{
    if (hash.isEmpty() || !repoHasWorkingTree())
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    const QString branch = currentRef();

    // Short hash + subject for a recognisable confirmation prompt.
    QByteArray subjOut;
    runGitCapture(dir, {"log", "-1", "--format=%h %s", hash}, &subjOut, nullptr);
    const QString label = QString::fromUtf8(subjOut).trimmed();

    if (QMessageBox::warning(
            this, "Delete commit",
            QStringLiteral(
                "Remove commit \"%1\" from history?\n\n"
                "This rewrites %2 and replays every later commit onto the one "
                "before it. It can't be undone, and you'll need to publish again "
                "to update the network mirror.")
                .arg(label.isEmpty() ? hash.left(8) : label, branch),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    // Need a single parent to replay the descendants onto: the root commit has
    // none, and a merge commit has two (rebasing it would flatten the merge).
    QByteArray parentsOut;
    if (!runGitCapture(dir, {"rev-list", "--parents", "-n", "1", hash},
                       &parentsOut, nullptr)) {
        setRepoDetailNotice("Could not inspect that commit.", true);
        return;
    }
    // rev-list --parents prints "<commit> <parent1> <parent2>..."; the count of
    // trailing fields is the number of parents.
    const QStringList fields = QString::fromUtf8(parentsOut).trimmed().split(
        QLatin1Char(' '), Qt::SkipEmptyParts);
    if (fields.size() < 2) {
        setRepoDetailNotice(
            "Can't delete the first commit — it has no parent to replay onto.", true);
        return;
    }
    if (fields.size() > 2) {
        setRepoDetailNotice(
            "Can't drop a merge commit from history this way (it has two parents).",
            true);
        return;
    }

    // Replay <hash>..<branch> onto <hash>'s parent, dropping <hash> itself. The
    // replay conflicts whenever a later commit reuses an issue/pull number the
    // dropped commit touched; driveDeleteCommitRebase auto-resolves those
    // data-file conflicts and keeps going. Anything else — a dirty tree before
    // the rebase starts, or a real code conflict — aborts and surfaces why.
    QString err;
    if (!runGitCapture(dir, {"rebase", "--onto", hash + "^", hash, branch},
                       nullptr, &err)) {
        if (rebaseMergeDir(dir).isEmpty()) {
            // The rebase never got underway (e.g. a dirty working tree); there is
            // nothing in progress to abort.
            setRepoDetailNotice(
                err.trimmed().isEmpty()
                    ? "Could not remove the commit — the rebase failed. Make "
                      "sure the working tree is clean and try again."
                    : err.trimmed(),
                true);
            return;
        }
        QString driveErr;
        if (!driveDeleteCommitRebase(dir, &driveErr)) {
            runGitCapture(dir, {"rebase", "--abort"}, nullptr, nullptr);
            setRepoDetailNotice(driveErr, true);
            return;
        }
    }

    // A rebase drop only makes the commit unreachable — it (and the pre-rewrite
    // chain the rebase orphaned) stays in the object store, recoverable via the
    // reflog. The commit is meant to disappear, so expire the reflog and prune to
    // physically remove it: no dangling object, no trace in history.
    runGitCapture(dir, {"reflog", "expire", "--expire=now", "--all"}, nullptr,
                  nullptr);
    runGitCapture(dir, {"gc", "--prune=now", "--quiet"}, nullptr, nullptr);

    logSystem(QStringLiteral("Git: removed commit %1 from %2 and pruned it from "
                             "the repository.")
                  .arg(hash.left(8), branch));
    setRepoDetailNotice(QStringLiteral("Removed commit %1 — gone from history.")
                            .arg(hash.left(8)));
    loadCommits();
    updateRepoPushButton();
}

// Undo a commit by recording its inverse as a brand-new commit on top of the
// branch (git revert). Unlike deleteCommit this keeps history intact, so it's
// the safe way to back out a change that's already been published. Owner-only
// (it writes a commit into the working copy); the branch then diverges from the
// served mirror until the next publish, so we refresh the publish button after.
void MainWindow::revertCommit(const QString &hash)
{
    if (hash.isEmpty() || !repoHasWorkingTree())
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    const QString branch = currentRef();

    // Short hash + subject for a recognisable confirmation prompt.
    QByteArray subjOut;
    runGitCapture(dir, {"log", "-1", "--format=%h %s", hash}, &subjOut, nullptr);
    const QString label = QString::fromUtf8(subjOut).trimmed();

    if (QMessageBox::question(
            this, "Restore commit",
            QStringLiteral(
                "Undo commit \"%1\"?\n\n"
                "This adds a new commit to %2 that reverses its changes. History "
                "is kept, so the original commit stays in the log — you'll need to "
                "publish again to update the network mirror.")
                .arg(label.isEmpty() ? hash.left(8) : label, branch),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    // A merge commit has two parents, so git can't tell which side to reverse
    // without an explicit mainline; refuse rather than guess.
    QByteArray parentsOut;
    if (!runGitCapture(dir, {"rev-list", "--parents", "-n", "1", hash},
                       &parentsOut, nullptr)) {
        setRepoDetailNotice("Could not inspect that commit.", true);
        return;
    }
    // rev-list --parents prints "<commit> <parent1> <parent2>..."; trailing
    // fields beyond the first are parents.
    const QStringList fields = QString::fromUtf8(parentsOut).trimmed().split(
        QLatin1Char(' '), Qt::SkipEmptyParts);
    if (fields.size() > 2) {
        setRepoDetailNotice(
            "Can't revert a merge commit this way (it has two parents).", true);
        return;
    }

    // Record the inverse commit. --no-edit takes git's default "Revert \"...\""
    // message instead of opening an editor. A conflict (a later commit touched
    // the same lines) leaves a revert in progress: abort it so the working tree
    // is restored, then surface why. A failure with nothing in progress is a
    // pre-flight refusal — most often a dirty working tree.
    QString err;
    if (!runGitCapture(dir, {"revert", "--no-edit", hash}, nullptr, &err)) {
        if (QFileInfo::exists(dir + QStringLiteral("/.git/REVERT_HEAD")) ||
            QFileInfo::exists(dir + QStringLiteral("/.git/sequencer"))) {
            runGitCapture(dir, {"revert", "--abort"}, nullptr, nullptr);
            setRepoDetailNotice(
                "Couldn't revert cleanly — a later commit changed the same lines. "
                "History was left unchanged.",
                true);
        } else {
            setRepoDetailNotice(
                err.trimmed().isEmpty()
                    ? "Could not revert the commit. Make sure the working tree is "
                      "clean and try again."
                    : err.trimmed(),
                true);
        }
        return;
    }

    logSystem(QStringLiteral("Git: reverted commit %1 on %2 with a new commit.")
                  .arg(hash.left(8), branch));
    setRepoDetailNotice(
        QStringLiteral("Reverted commit %1 — added a commit that undoes it.")
            .arg(hash.left(8)));
    loadCommits();
    updateRepoPushButton();
}

void MainWindow::showCommitsBanner(const QString &html)
{
    if (!m_commitsUnsyncedBanner)
        return;
    if (m_commitsBannerFade)
        m_commitsBannerFade->stop(); // cancel any in-flight fade-out
    if (m_commitsBannerOpacity)
        m_commitsBannerOpacity->setOpacity(1.0);
    m_commitsUnsyncedBanner->setText(html);
    m_commitsUnsyncedBanner->show();
}

void MainWindow::hideCommitsBanner()
{
    if (!m_commitsUnsyncedBanner || m_commitsUnsyncedBanner->isHidden())
        return;
    // Ease the note away instead of snapping it off, so it stays readable a
    // moment longer; the animation's finished handler does the actual hide().
    if (!m_commitsBannerFade || !m_commitsBannerOpacity) {
        m_commitsUnsyncedBanner->hide();
        return;
    }
    m_commitsBannerFade->stop();
    m_commitsBannerFade->setStartValue(m_commitsBannerOpacity->opacity());
    m_commitsBannerFade->setEndValue(0.0);
    m_commitsBannerFade->start();
}

void MainWindow::filterCommits(const QString &query)
{
    if (!m_commitsTable)
        return;
    const QString needle = query.trimmed().toLower();
    // A search should reach well past the lazily-paged window, so a hash or
    // message deeper than the loaded rows still turns up. The first keystroke
    // deepens the table to kCommitSearchDepth commits (bounded — building the
    // whole history froze the UI for 10-20s on big repos); clearing it restores
    // the paged window. loadCommits() re-applies this same filter at its tail, so
    // the deepened pass falls through to the row loop below.
    if (!needle.isEmpty() && !m_commitsShowingAll && m_commitsHasMore) {
        m_commitsShowingAll = true;
        loadCommits();
        return;
    }
    if (needle.isEmpty() && m_commitsShowingAll) {
        m_commitsShowingAll = false;
        m_commitsLimit = 300;
        loadCommits();
        return;
    }
    for (int row = 0; row < m_commitsTable->rowCount(); ++row) {
        bool match = needle.isEmpty();
        if (!match) {
            // The full hash lives on the summary item's UserRole; the visible
            // Commit cell holds the short hash. Match either, plus the summary or
            // the author (so clicking a contributor on Insights filters to them).
            const QTableWidgetItem *summary =
                m_commitsTable->item(row, kCommitSummaryCol);
            const QTableWidgetItem *hash =
                m_commitsTable->item(row, kCommitHashCol);
            const QTableWidgetItem *author = m_commitsTable->item(row, 0);
            if (summary &&
                (summary->text().toLower().contains(needle) ||
                 summary->data(Qt::UserRole).toString().toLower().contains(needle)))
                match = true;
            else if (hash && hash->text().toLower().contains(needle))
                match = true;
            else if (author && author->text().toLower().contains(needle))
                match = true;
        }
        m_commitsTable->setRowHidden(row, !match);
    }
}

void MainWindow::openCommitReference(int number)
{
    openIssueReference(number);
}

void MainWindow::openIssueReference(int number)
{
    if (number <= 0)
        return;
    if (m_repoDetailTabs && m_repoDetailTabs->button(2))
        m_repoDetailTabs->button(2)->click();
    reloadIssues();
    showIssue(number);
}

void MainWindow::openPullReference(int number)
{
    if (number <= 0)
        return;
    switchToPullTab(number);
}

void MainWindow::openCommitHashReference(const QString &hash)
{
    const QString ref = hash.trimmed();
    if (ref.isEmpty())
        return;
    showOverviewCommits();
    showCommit(ref);
}

void MainWindow::openReferenceLink(const QString &href)
{
    if (href.startsWith(QStringLiteral("fm-issue:"))) {
        openIssueReference(href.mid(9).toInt());
        return;
    }
    if (href.startsWith(QStringLiteral("fm-pull:"))) {
        openPullReference(href.mid(8).toInt());
        return;
    }
    if (href.startsWith(QStringLiteral("fm-commit:"))) {
        openCommitHashReference(href.mid(10));
        return;
    }

    const QUrl url(href);
    if (url.scheme() == QLatin1String("forkmesh")) {
        const QString kind = url.host();
        const QStringList parts =
            url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        auto switchLinkedRepo = [&]() {
            if (parts.size() < 2)
                return;
            const int repoIndex = repoIndexFor(parts.at(0), parts.at(1));
            if (repoIndex >= 0 && repoIndex != m_repoDetailIndex)
                openRepoDetail(repoIndex);
        };
        if ((kind == QLatin1String("issue") || kind == QLatin1String("pull")) &&
            parts.size() >= 3) {
            switchLinkedRepo();
            const int number = parts.last().toInt();
            if (kind == QLatin1String("issue"))
                openIssueReference(number);
            else
                openPullReference(number);
            return;
        }
        if (kind == QLatin1String("commit") && parts.size() >= 3) {
            switchLinkedRepo();
            openCommitHashReference(parts.last());
            return;
        }
    }

    QDesktopServices::openUrl(url.isValid() ? url : QUrl::fromUserInput(href));
}

QString MainWindow::autolinkReferences(const QString &markdown)
{
    if (markdown.isEmpty())
        return markdown;
    // Walk line by line so fenced code blocks (``` / ~~~) are left verbatim, then
    // linkify references in each non-fenced line (which also skips inline code and
    // existing links). Kept as markdown source so MarkdownText still formats it.
    QString out;
    out.reserve(markdown.size() + 32);
    bool inFence = false;
    QString fenceMarker;
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    for (int li = 0; li < lines.size(); ++li) {
        if (li > 0)
            out += QLatin1Char('\n');
        const QString &line = lines.at(li);
        const QString trimmed = line.trimmed();
        const bool fenceLine = trimmed.startsWith(QStringLiteral("```")) ||
                               trimmed.startsWith(QStringLiteral("~~~"));
        if (inFence) {
            out += line;
            if (fenceLine && trimmed.startsWith(fenceMarker))
                inFence = false;
        } else if (fenceLine) {
            inFence = true;
            fenceMarker = trimmed.left(3);
            out += line;
        } else {
            out += linkifyReferenceLine(line);
        }
    }
    return out;
}

void MainWindow::openBodyReference(const QString &href)
{
    if (href.startsWith(QStringLiteral("forkmesh-ref:"))) {
        openCommitReference(href.mid(13).toInt()); // PR if one matches, else issue
        return;
    }
    if (href.startsWith(QStringLiteral("forkmesh-commit:"))) {
        const QString sha = href.mid(16);
        if (sha.isEmpty())
            return;
        showOverviewCommits();
        showCommit(sha);
        return;
    }
    if (href.startsWith(QStringLiteral("forkmesh://"))) {
        // forkmesh://<kind>/<owner>/<repo>/<id>[#eventId]
        const QString rest = href.mid(QStringLiteral("forkmesh://").size());
        const QString kind = rest.section(QLatin1Char('/'), 0, 0);
        const QString id =
            rest.section(QLatin1Char('/'), -1).section(QLatin1Char('#'), 0, 0);
        if (kind == QLatin1String("issue")) {
            if (m_repoDetailTabs && m_repoDetailTabs->button(2))
                m_repoDetailTabs->button(2)->click();
            reloadIssues();
            showIssue(id.toInt());
        } else if (kind == QLatin1String("pull")) {
            reloadPulls();
            if (m_repoDetailTabs && m_repoDetailTabs->button(4))
                m_repoDetailTabs->button(4)->click();
            showPull(id.toInt());
        } else if (kind == QLatin1String("commit")) {
            openBodyReference(QStringLiteral("forkmesh-commit:%1").arg(id));
        }
        return;
    }
    QDesktopServices::openUrl(QUrl(href));
}

void MainWindow::copyReferenceLink(const QString &kind, const QString &id)
{
    if (id.isEmpty())
        return;
    QString owner = QStringLiteral("repo");
    QString repo = kind;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        owner = m_repositories.at(m_repoDetailIndex).owner;
        repo = m_repositories.at(m_repoDetailIndex).name;
    }
    QApplication::clipboard()->setText(
        QStringLiteral("forkmesh://%1/%2/%3/%4").arg(kind, owner, repo, id));
    setRepoDetailNotice(QStringLiteral("Link copied \xE2\x80\x94 paste it into a "
                                       "comment to link back here."));
}

void MainWindow::downloadCommitPatch()
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || m_currentCommitHash.isEmpty())
        return;
    // format-patch produces a self-describing patch (author, message, diff) that
    // is still apply-able via git apply, so it round-trips through Import as PR.
    QByteArray patch;
    QString err;
    if (!runGitCapture(dir,
                       {"format-patch", "-1", "--stdout", m_currentCommitHash},
                       &patch, &err) ||
        patch.trimmed().isEmpty()) {
        setRepoDetailNotice(err.isEmpty() ? "Could not generate the patch." : err, true);
        return;
    }
    const QString shortHash = m_currentCommitHash.left(8);
    const QString suggested =
        QDir::homePath() + "/" + shortHash + QStringLiteral(".patch");
    const QString path = QFileDialog::getSaveFileName(
        this, "Download patch", suggested, "Patch files (*.patch *.diff)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(patch) < 0) {
        setRepoDetailNotice("Could not write the patch file.", true);
        return;
    }
    file.close();
    setRepoDetailNotice(QStringLiteral("Saved patch to %1.").arg(path));
    logSystem(QStringLiteral("Saved commit %1 as patch %2.").arg(shortHash, path));
}

void MainWindow::applyCommitIssueClosures()
{
    // Closing an issue authors signed events into the repo's issues/ folder, so
    // it requires a real working tree on this node. Mirror-only repos are
    // read-only here and are skipped.
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite())
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;

    // Full commit messages so we catch closing keywords in the body, not just
    // the subject. Records separated by RS (0x1e); fields by US (0x1f).
    QByteArray out;
    if (!runGitCapture(dir,
                       {"log", "--format=%h%x1f%s%x1f%B%x1e", "-n", "500",
                        currentRef()},
                       &out, nullptr))
        return;

    QList<Issue> issues = store.loadAll();
    QHash<int, const Issue *> byNumber;
    for (const Issue &issue : issues)
        byNumber.insert(issue.number, &issue);

    // GitHub-style closing keywords followed by #<number>.
    static const QRegularExpression closeRe(
        QStringLiteral("\\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\\b\\s*:?\\s*"
                       "#(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);

    int closedCount = 0;
    QString lastClosed;
    for (const QByteArray &record : out.split('\x1e')) {
        if (record.trimmed().isEmpty())
            continue;
        const QStringList f = QString::fromUtf8(record).split('\x1f');
        if (f.size() < 3)
            continue;
        const QString shortHash = f.at(0).trimmed();
        const QString subject = f.at(1).trimmed();
        const QString message = f.at(2);

        auto it = closeRe.globalMatch(message);
        QSet<int> seenInThisCommit; // a commit may name the same issue twice
        while (it.hasNext()) {
            const int number = it.next().captured(1).toInt();
            if (number <= 0 || seenInThisCommit.contains(number))
                continue;
            seenInThisCommit.insert(number);

            const Issue *issue = byNumber.value(number, nullptr);
            if (!issue || issue->status == QLatin1String("closed"))
                continue;

            // Idempotency: skip if a comment already records this commit closing
            // the issue (so reloading the commits tab doesn't re-comment).
            bool alreadyLinked = false;
            for (const IssueEvent &ev : issue->events) {
                if (ev.type == QLatin1String("comment") &&
                    ev.body.contains(shortHash)) {
                    alreadyLinked = true;
                    break;
                }
            }
            if (alreadyLinked)
                continue;

            QString err;
            const QString note =
                QString::fromUtf8("Closed by commit `%1` \xE2\x80\x94 %2")
                    .arg(shortHash, subject);
            if (!store.addComment(number, note, {}, &err)) {
                logSystem(QStringLiteral("Issue #%1: could not link commit %2: %3")
                              .arg(number)
                              .arg(shortHash, err));
                continue;
            }
            if (!store.setStatus(number, QStringLiteral("closed"), &err)) {
                logSystem(QStringLiteral("Issue #%1: could not close: %2")
                              .arg(number)
                              .arg(err));
                continue;
            }
            logSystem(QStringLiteral("Closed issue #%1 via commit %2.")
                          .arg(number)
                          .arg(shortHash));
            ++closedCount;
            lastClosed = QStringLiteral("#%1").arg(number);
            // The store mutated on disk; refresh our snapshot so a later commit
            // in the same pass sees the updated status.
            issues = store.loadAll();
            byNumber.clear();
            for (const Issue &i : issues)
                byNumber.insert(i.number, &i);
        }
    }

    if (closedCount > 0) {
        flashMessage(closedCount == 1
                         ? QStringLiteral("Closed issue %1 from commit message.")
                               .arg(lastClosed)
                         : QStringLiteral("Closed %1 issues from commit messages.")
                               .arg(closedCount));
        reloadIssues();
        updateRepoIssueCount();
    }
}


void MainWindow::updateDiffSplitButton(QPushButton *button)
{
    if (!button)
        return;
    const bool split = diffSplitPref();
    button->setText(split ? QStringLiteral("Side-by-side")
                          : QStringLiteral("Unified"));
    button->setToolTip(split
                           ? QString::fromUtf8("Showing a side-by-side diff \xE2\x80\x94 "
                                            "click for a unified diff")
                           : QString::fromUtf8("Showing a unified diff \xE2\x80\x94 "
                                            "click for a side-by-side diff"));
}

void MainWindow::showCommit(const QString &hash)
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || hash.isEmpty() || !m_commitsStack)
        return;

    // Track this commit's position so Prev/Next can walk the list, and reflect
    // the available directions on the buttons.
    m_currentCommitRow = -1;
    if (m_commitsTable)
        for (int i = 0; i < m_commitsTable->rowCount(); ++i)
            if (m_commitsTable->item(i, kCommitSummaryCol)
                    ->data(Qt::UserRole)
                    .toString() == hash) {
                m_currentCommitRow = i;
                break;
            }
    if (m_commitPrevButton)
        m_commitPrevButton->setEnabled(m_currentCommitRow > 0);
    if (m_commitNextButton)
        m_commitNextButton->setEnabled(m_commitsTable &&
                                       m_currentCommitRow >= 0 &&
                                       m_currentCommitRow < m_commitsTable->rowCount() - 1);
    // Keep the table highlight in sync so the selected row follows Prev/Next.
    if (m_commitsTable && m_currentCommitRow >= 0) {
        QSignalBlocker blk(m_commitsTable);
        m_commitsTable->selectRow(m_currentCommitRow);
    }

    // Last click wins: a new showCommit() supersedes any in-flight async load.
    // (The old code *dropped* clicks that arrived mid-load, which made Prev/Next
    // feel dead; and it blocked the click on two synchronous git reads.) Every
    // async hop below re-checks this generation and bails when superseded.
    const int gen = ++m_commitLoadGen;

    // Land on the diff page and paint a spinner straight away; all git below is
    // asynchronous, so the click itself never blocks the GUI thread.
    m_commitsStack->setCurrentIndex(1);
    startCommitDiffSpin();

    m_currentCommitHash = hash; // refined to the full hash when metadata lands
    if (m_commitComposer)
        m_commitComposer->setMentionCandidates(mentionCandidateNames());
    if (m_commitDownloadButton)
        m_commitDownloadButton->setEnabled(true);
    if (m_commitDeleteButton || m_commitRevertButton) {
        // History rewrites and revert commits only make sense where this node owns
        // the working tree; browse-only mirrors show the buttons disabled (matching
        // the list rows).
        const bool writable = repoHasWorkingTree();
        if (m_commitDeleteButton) {
            m_commitDeleteButton->setEnabled(writable);
            m_commitDeleteButton->setToolTip(
                writable ? QStringLiteral(
                               "Remove this commit from history (rewrites the branch "
                               "and replays the later commits onto its parent)")
                         : QStringLiteral(
                               "Read-only mirror \xE2\x80\x94 no working tree to "
                               "rewrite history in"));
        }
        if (m_commitRevertButton) {
            m_commitRevertButton->setEnabled(writable);
            m_commitRevertButton->setToolTip(
                writable ? QStringLiteral(
                               "Undo this commit by committing the reverse of its "
                               "changes (history is kept)")
                         : QStringLiteral(
                               "Read-only mirror \xE2\x80\x94 no working tree to "
                               "commit a revert into"));
        }
    }
    // --- Metadata (full hash, author, date, parents, subject, body), then the
    // patch, then the render: an async chain, each hop generation-checked.
    runGitDetached(
        dir,
        {QStringLiteral("show"), QStringLiteral("-s"),
         QStringLiteral("--date=format:%b %e, %Y"),
         QStringLiteral("--format=%H%x1f%an%x1f%ad%x1f%P%x1f%s%x1f%b"), hash},
        [this, gen, dir, hash](bool, const QByteArray &meta) {
            if (gen != m_commitLoadGen)
                return;
            const QStringList mf =
                QString::fromUtf8(meta).split(QLatin1Char('\x1f'));
            const QString full = mf.value(0).trimmed();
            const QString fullHash = full.isEmpty() ? hash : full;
            // Diff against the first parent (or the empty tree for a root
            // commit), which matches how a commit page presents merges and
            // initial commits.
            const QString emptyTree =
                QStringLiteral("4b825dc642cb6eb9a060e54bf8d69288fbee4904");
            const QStringList parents = mf.value(3).trimmed().split(
                QLatin1Char(' '), Qt::SkipEmptyParts);
            const QString base =
                parents.isEmpty() ? emptyTree : parents.first();
            runGitDetached(dir,
                           {QStringLiteral("diff"), QStringLiteral("-M"), base,
                            fullHash},
                           [this, gen, dir, hash, mf](bool,
                                                      const QByteArray &patch) {
                               if (gen != m_commitLoadGen)
                                   return;
                               renderCommitDetail(dir, hash, mf, patch);
                           });
        });
}

// The synchronous tail of showCommit(): all git output is in hand (metaFields
// from `show -s`, patchRaw from `diff -M`), so this is pure widget population.
void MainWindow::renderCommitDetail(const QString &dir, const QString &hash,
                                    const QStringList &metaFields,
                                    const QByteArray &patchRaw)
{
    const QString full = metaFields.value(0).trimmed();
    const QString author = metaFields.value(1).trimmed();
    const QString date = metaFields.value(2).trimmed();
    const QStringList parents =
        metaFields.value(3).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString subject = metaFields.value(4).trimmed();
    const QString body = metaFields.value(5).trimmed();
    const QString fullHash = full.isEmpty() ? hash : full;
    const QString emptyTree =
        QStringLiteral("4b825dc642cb6eb9a060e54bf8d69288fbee4904");
    const QString base = parents.isEmpty() ? emptyTree : parents.first();
    m_currentCommitHash = fullHash;

    // --- Header labels.
    if (m_commitTitle)
        m_commitTitle->setText(
            QStringLiteral("Commit <code>%1</code>").arg(fullHash.toHtmlEscaped()));
    if (m_commitMessage) {
        QString msg =
            QStringLiteral("<b>%1</b>").arg(linkifyIssueRefs(subject.toHtmlEscaped()));
        if (!body.isEmpty())
            msg += QStringLiteral(
                       "<br><span style='color:#8b949e; white-space:pre-wrap'>%1</span>")
                       .arg(linkifyIssueRefs(body.toHtmlEscaped()));
        m_commitMessage->setText(msg);
    }

    // --- Render the diff and collect per-file stats.
    QList<DiffFileEntry> files;
    const QString diffHtml =
        renderDiffHtml(QString::fromUtf8(patchRaw), files, dir, base, fullHash);
    int totalAdds = 0, totalDels = 0;
    for (const DiffFileEntry &f : files) {
        totalAdds += f.adds;
        totalDels += f.dels;
    }

    if (m_commitMeta)
        m_commitMeta->setText(
            QString::fromUtf8("%1 committed on %2 \xC2\xB7 %3 parent%4 \xC2\xB7 "
                           "<b>%5</b> file%6 changed "
                           "<span style='color:#3fb950'>+%7</span> "
                           "<span style='color:#f85149'>\xE2\x88\x92%8</span>")
                .arg(author.toHtmlEscaped(), date.toHtmlEscaped(),
                     QString::number(qMax(1, parents.size())),
                     parents.size() == 1 ? "" : "s", QString::number(files.size()),
                     files.size() == 1 ? "" : "s", QString::number(totalAdds),
                     QString::number(totalDels)));

    if (m_commitFilesSummary)
        m_commitFilesSummary->setText(
            QStringLiteral("%1 file%2 changed")
                .arg(files.size())
                .arg(files.size() == 1 ? "" : "s"));

    // --- Left file list (click scrolls the diff to that file).
    if (m_commitFileList) {
        QSignalBlocker block(m_commitFileList);
        m_commitFileList->clear();
        for (const DiffFileEntry &f : files) {
            // Show the basename prominently with the +/- counts; full path on
            // hover. A status-coloured octicon leads each row.
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
            m_commitFileList->addItem(item);
        }
        fitFileListToWidestEntry(m_commitFileList);
    }

    // --- Theme-aware diff styling, then the rendered HTML.
    if (m_commitDiffView) {
        setDiffHtml(m_commitDiffView,
                    diffHtml.isEmpty()
                        ? QStringLiteral("<p style='color:#8b949e'>"
                                         "No changes in this commit.</p>")
                        : diffHtml);
    }

    renderCommitThread(m_currentCommitHash);
    stopCommitDiffSpin();
    if (m_commitsStack)
        m_commitsStack->setCurrentIndex(1);
}

void MainWindow::renderCommitThread(const QString &sha)
{
    if (!m_commitThreadLayout)
        return;
    while (QLayoutItem *item = m_commitThreadLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    // Trailing stretch first so cards flow top to bottom (see renderPullThread).
    m_commitThreadLayout->addStretch();
    if (sha.isEmpty())
        return;
    QString linkOwner = QStringLiteral("repo");
    QString linkRepo = QStringLiteral("commit");
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        linkOwner = m_repositories.at(m_repoDetailIndex).owner;
        linkRepo = m_repositories.at(m_repoDetailIndex).name;
    }
    const QString commitLink =
        QStringLiteral("forkmesh://commit/%1/%2/%3").arg(linkOwner, linkRepo, sha);
    const RepositoryRecord rec =
        (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
            ? writableRecordFor(m_repositories.at(m_repoDetailIndex))
            : RepositoryRecord();
    CommitCommentStore store(rec.localPath, rec.mirrorPath, &m_profileIdentity,
                             m_userName);
    for (const CommitComment &c : store.loadFor(sha)) {
        const QString who = c.authorName.isEmpty() ? c.author.left(10) : c.authorName;
        addConversationCard(
            m_commitThreadLayout, who,
            QStringLiteral("<b>%1</b> <span style='color:#8b949e'>commented %2</span>")
                .arg(who.toHtmlEscaped(), formatIssueRelativeTime(c.ts)),
            c.body, QString(),
            commitLink + QStringLiteral("#%1")
                             .arg(c.id.isEmpty() ? QString::number(c.ts) : c.id),
            c.author);
    }
}

void MainWindow::submitCommitComment()
{
    if (m_currentCommitHash.isEmpty() || !m_commitComposer)
        return;
    const QString body = m_commitComposer->markdown().trimmed();
    if (body.isEmpty()) {
        flashMessage(QStringLiteral("Write a comment first."));
        return;
    }
    const RepositoryRecord rec =
        (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
            ? writableRecordFor(m_repositories.at(m_repoDetailIndex))
            : RepositoryRecord();
    CommitCommentStore store(rec.localPath, rec.mirrorPath, &m_profileIdentity,
                             m_userName);
    if (store.canWrite()) {
        QString error;
        if (!store.addComment(m_currentCommitHash, body, &error)) {
            QMessageBox::warning(this, "Comment", error);
            return;
        }
        // Push the new comment into the bare mirror and tell mirroring peers
        // right away (like issue/PR comments do) so it converges in seconds
        // instead of at the next 5-minute auto-sync. Resolve the writable
        // repo's own index — the open detail may be a read-only preview of a
        // repo we actually host under a different entry.
        const int srcIndex = repoIndexFor(rec.owner, rec.name);
        propagateRepoUpdate(srcIndex >= 0 ? srcIndex : m_repoDetailIndex);
    } else {
        const CommitComment c =
            store.makeSignedComment(m_currentCommitHash, [&] {
                CommitComment x;
                x.body = body;
                return x;
            }());
        submitCommitCommentToInbox(m_currentCommitHash, c);
    }
    m_commitComposer->setMarkdown(QString());
    renderCommitThread(m_currentCommitHash);
}

void MainWindow::loadRepoInsights()
{
    if (!m_insightsSummary)
        return;

    TableRepaintGuard repaintGuard(m_insightsContributors);
    if (m_insightsContributors)
        m_insightsContributors->setRowCount(0);
    if (m_insightsLanguageBar)
        m_insightsLanguageBar->clear();
    if (m_insightsLanguageLegend)
        m_insightsLanguageLegend->clear();
    if (m_insightsActivity)
        m_insightsActivity->clear();
    if (m_insightsActivityAxis)
        m_insightsActivityAxis->clear();

    auto setNoRepo = [this] {
        m_insightsSummary->setText(
            "<b>Repository summary</b><br><span style='color:#8b949e'>Select a "
            "repository with a local checkout or mirror to see insights.</span>");
        if (m_insightsTraffic)
            m_insightsTraffic->setText(
                "<b>ForkMesh traffic</b><br><span style='color:#8b949e'>No "
                "repository selected.</span>");
        if (m_insightsLanguageLegend)
            m_insightsLanguageLegend->setText(
                "<span style='color:#8b949e'>No language data available.</span>");
    };

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        setNoRepo();
        return;
    }

    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString repoKey = repo.owner + "/" + repo.name;
    const QString dir = repoGitDir();
    const QString ref = currentRef();
    QStringList notes;

    QString sourceKind = QStringLiteral("unavailable");
    if (!repo.localPath.isEmpty() && QDir(repo.localPath).exists())
        sourceKind = QStringLiteral("local worktree");
    else if (repo.previewOnly && !repo.mirrorPath.isEmpty() &&
             QDir(repo.mirrorPath).exists())
        sourceKind = QStringLiteral("preview cache");
    else if (!repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists())
        sourceKind = QStringLiteral("bare mirror");

    notes << QStringLiteral("Data source: %1. Selected ref: %2.")
                 .arg(sourceKind, ref);
    if (!repositorySource(repo).isEmpty())
        notes << QStringLiteral("Source: %1.").arg(repositorySource(repo));
    if (repo.lastSyncMs > 0)
        notes << QStringLiteral("Last sync: %1.").arg(formatRepoDate(repo.lastSyncMs));
    if (repo.publishedAtMs > 0)
        notes << QStringLiteral("Published: %1.").arg(formatRepoDate(repo.publishedAtMs));

    qint64 fileCount = 0;
    qint64 totalBytes = 0;
    QHash<QString, qint64> bytesByLanguage;
    qint64 recognizedBytes = 0;

    if (dir.isEmpty()) {
        notes << QStringLiteral("No local checkout or mirror is available for Git history.");
    } else {
        QByteArray treeOut;
        QString treeErr;
        if (runGitCapture(dir, {"ls-tree", "-r", "-l", ref}, &treeOut, &treeErr)) {
            for (const QByteArray &record : treeOut.split('\n')) {
                if (record.trimmed().isEmpty())
                    continue;
                const int tab = record.indexOf('\t');
                if (tab < 0)
                    continue;
                const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
                if (meta.size() < 4 || meta.at(1) != "blob")
                    continue;
                ++fileCount;
                bool ok = false;
                const qint64 size = QString::fromUtf8(meta.at(3)).toLongLong(&ok);
                if (!ok)
                    continue;
                totalBytes += qMax<qint64>(0, size);
                const QString name = QString::fromUtf8(record.mid(tab + 1));
                const QString language = languageForFile(name);
                if (!language.isEmpty() && size > 0) {
                    bytesByLanguage[language] += size;
                    recognizedBytes += size;
                }
            }
        } else {
            notes << QStringLiteral("File composition unavailable: %1.")
                         .arg(treeErr.isEmpty() ? QStringLiteral("git failed") : treeErr.left(160));
        }
    }

    int openIssues = 0;
    int closedIssues = 0;
    for (const Issue &issue : std::as_const(m_currentIssues)) {
        if (issue.isDeleted())
            continue;
        if (issue.status == QStringLiteral("closed"))
            ++closedIssues;
        else
            ++openIssues;
    }

    QString pullError;
    const QList<PullRequest> pulls = pullStoreForCurrentRepo().loadAll(&pullError);
    int openPulls = 0;
    int mergedPulls = 0;
    int closedPulls = 0;
    for (const PullRequest &pr : pulls) {
        if (pr.status == QStringLiteral("merged"))
            ++mergedPulls;
        else if (pr.status == QStringLiteral("closed"))
            ++closedPulls;
        else
            ++openPulls;
    }
    if (!pullError.isEmpty())
        notes << QStringLiteral("Pull request data unavailable: %1.").arg(pullError.left(160));

    const int totalIssues = openIssues + closedIssues;
    const int totalPulls = openPulls + mergedPulls + closedPulls;
    m_insightsSummary->setText(
        "<b>Repository summary</b>" +
        insightMetricsTable({
            insightMetricCell("Contributors", QStringLiteral("0"), "current branch"),
            insightMetricCell("Files", QString::number(fileCount), "tracked blobs"),
            insightMetricCell("Code size", formatInsightBytes(totalBytes), "tracked bytes"),
            insightMetricCell("Issues", QString::number(totalIssues),
                              QStringLiteral("%1 open / %2 closed")
                                  .arg(openIssues)
                                  .arg(closedIssues)),
            insightMetricCell("Pull requests", QString::number(totalPulls),
                              QStringLiteral("%1 open / %2 merged / %3 closed")
                                  .arg(openPulls)
                                  .arg(mergedPulls)
                                  .arg(closedPulls)),
        }));

    const QPair<int, int> stats = m_repoStats.value(repoKey);
    if (m_insightsTraffic) {
        m_insightsTraffic->setText(
            QStringLiteral(
                "<b>ForkMesh traffic</b><br><br>"
                "<span style='font-size:21px; font-weight:800'>%1</span><br>"
                "<span style='color:#8b949e'>served requests</span><br><br>"
                "<span style='font-size:21px; font-weight:800'>%2</span><br>"
                "<span style='color:#8b949e'>clone requests</span><br><br>"
                "<span style='color:#8b949e'>Local-only counters since this app "
                "started tracking.</span>")
                .arg(stats.first)
                .arg(stats.second));
    }

    QList<QPair<QString, qint64>> languages;
    for (auto it = bytesByLanguage.constBegin(); it != bytesByLanguage.constEnd(); ++it)
        languages.append({it.key(), it.value()});
    std::sort(languages.begin(), languages.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    QString languageLegend;
    const int shownLanguages = qMin(5, int(languages.size()));
    for (int i = 0; i < shownLanguages && recognizedBytes > 0; ++i) {
        const double pct = 100.0 * languages.at(i).second / recognizedBytes;
        const QString color = languageColor(languages.at(i).first);
        languageLegend += QString::fromUtf8(
                              "<span style='color:%1'>\xE2\x97\x8F</span> %2 %3% "
                              "<span style='color:#8b949e'>(%4)</span>&nbsp;&nbsp;")
                              .arg(color,
                                   languages.at(i).first.toHtmlEscaped(),
                                   QString::number(pct, 'f', 1),
                                   formatInsightBytes(languages.at(i).second));
    }
    if (m_insightsLanguageBar) {
        m_insightsLanguageBar->setScaledContents(true);
        m_insightsLanguageBar->setPixmap(languageBarPixmap(
            languages, recognizedBytes, shownLanguages, 600, 10));
    }
    if (m_insightsLanguageLegend)
        m_insightsLanguageLegend->setText(
            languageLegend.isEmpty()
                ? "<span style='color:#8b949e'>No recognized code files yet.</span>"
                : languageLegend);

    // --- Contributors & activity (merged) -----------------------------------
    // A single git-log pass over the selected time window yields, per author,
    // a commit count and a bucketed timeline. The window comes from
    // m_insightsRangeCombo (0 = all time). The count, share and the embedded
    // bar chart all reflect that same window so the table reads as one unit.
    // The log is scoped to the selected ref (currentRef) — the same history the
    // "Commits (N)" badge, the file tree and the Files/Code-size figures above
    // count — rather than --all, so the totals match the branch being viewed and
    // don't fold in every stale/unmerged ref in the repo. Merge commits are
    // included (no --no-merges) so the per-author commit counts sum to exactly
    // the same total `git rev-list --count` reports in that badge; excluding them
    // here previously left the table short by every merge (each attributed to
    // whoever performed it), which read as an inaccurate count.
    const int windowDays =
        m_insightsRangeCombo ? m_insightsRangeCombo->currentData().toInt() : 0;
    constexpr int kBuckets = 32;

    struct Contributor {
        QString name;
        int commits = 0;
        QVector<int> buckets;
    };
    QHash<QString, int> indexByName; // author name -> contributors[] index
    QList<Contributor> contributors;
    int contributorCommitTotal = 0;
    qint64 minTs = 0, maxTs = 0;
    int sharedMax = 1;

    if (!dir.isEmpty()) {
        QStringList logArgs{"log", "--format=%an%x1f%ct", "-n", "50000"};
        if (windowDays > 0)
            logArgs << QStringLiteral("--since=%1.days.ago").arg(windowDays);
        logArgs << ref;
        QByteArray logOut;
        QString logErr;
        if (runGitCapture(dir, logArgs, &logOut, &logErr)) {
            struct Stamp {
                QString author;
                qint64 ts;
            };
            QList<Stamp> stamps;
            for (const QByteArray &line : logOut.split('\n')) {
                if (line.trimmed().isEmpty())
                    continue;
                const QStringList f = QString::fromUtf8(line).split(QLatin1Char('\x1f'));
                if (f.size() < 2)
                    continue;
                const QString author = f.at(0).trimmed();
                const qint64 ts = f.at(1).toLongLong();
                if (author.isEmpty() || ts <= 0)
                    continue;
                stamps.append({author, ts});
                if (minTs == 0 || ts < minTs)
                    minTs = ts;
                if (ts > maxTs)
                    maxTs = ts;
            }
            const qint64 span = qMax<qint64>(1, maxTs - minTs);
            for (const Stamp &s : std::as_const(stamps)) {
                int idx = indexByName.value(s.author, -1);
                if (idx < 0) {
                    idx = int(contributors.size());
                    indexByName.insert(s.author, idx);
                    Contributor c;
                    c.name = s.author;
                    c.buckets.resize(kBuckets);
                    contributors.append(c);
                }
                Contributor &c = contributors[idx];
                ++c.commits;
                ++contributorCommitTotal;
                if (maxTs > minTs) {
                    const int b = qBound<int>(
                        0, int((s.ts - minTs) * kBuckets / (span + 1)), kBuckets - 1);
                    ++c.buckets[b];
                    sharedMax = qMax(sharedMax, c.buckets.at(b));
                }
            }
        } else {
            notes << QStringLiteral("Contributor data unavailable: %1.")
                         .arg(logErr.isEmpty() ? QStringLiteral("git failed")
                                               : logErr.left(160));
        }
    }

    std::sort(contributors.begin(), contributors.end(),
              [](const Contributor &a, const Contributor &b) {
                  if (a.commits != b.commits)
                      return a.commits > b.commits;
                  return a.name.localeAwareCompare(b.name) < 0;
              });

    if (m_insightsContributors) {
        // Keep every contributor (no truncation); the table is in a scroll area.
        for (const Contributor &c : std::as_const(contributors)) {
            const int row = m_insightsContributors->rowCount();
            m_insightsContributors->insertRow(row);

            const QString openHint =
                QStringLiteral("Click to view %1's commits").arg(c.name);
            auto *nameItem = new QTableWidgetItem(c.name);
            nameItem->setData(Qt::UserRole, c.name); // read by the reassign menu
            nameItem->setToolTip(openHint);
            m_insightsContributors->setItem(row, 0, nameItem);

            auto *countItem = new QTableWidgetItem;
            countItem->setData(Qt::DisplayRole, c.commits);
            countItem->setToolTip(openHint);
            m_insightsContributors->setItem(row, 1, countItem);

            const double pct = contributorCommitTotal > 0
                                   ? 100.0 * c.commits / contributorCommitTotal
                                   : 0.0;
            m_insightsContributors->setItem(
                row, 2, new QTableWidgetItem(QStringLiteral("%1%").arg(pct, 0, 'f', 1)));

            if (maxTs > minTs) {
                auto *chart = new CommitBarChart(c.buckets, sharedMax,
                                                 insightContributorColor(c.name));
                chart->setToolTip(
                    QStringLiteral("%1 \xE2\x80\x94 %2 commits over time")
                        .arg(c.name)
                        .arg(c.commits));
                m_insightsContributors->setCellWidget(row, 3, chart);
            }
        }
        if (contributors.isEmpty()) {
            const int row = m_insightsContributors->rowCount();
            m_insightsContributors->insertRow(row);
            auto *empty = new QTableWidgetItem(
                windowDays > 0
                    ? QStringLiteral("No commits in the selected range.")
                    : QStringLiteral("No commit history available."));
            empty->setForeground(QColor(0x8b, 0x94, 0x9e));
            m_insightsContributors->setItem(row, 0, empty);
            m_insightsContributors->setSpan(row, 0, 1, 4);
        }
    }

    // Caption under the table: the span the bars cover, oldest to newest.
    if (m_insightsActivityAxis) {
        if (maxTs > minTs) {
            m_insightsActivityAxis->setText(
                QString::fromUtf8(
                    "<span style='color:#8b949e'>%1 &nbsp;&nbsp;\xE2\x86\x90 oldest "
                    "&nbsp;&nbsp;\xC2\xB7&nbsp;&nbsp; newest \xE2\x86\x92&nbsp;&nbsp; "
                    "%2 &nbsp;&nbsp;\xC2\xB7&nbsp;&nbsp; click a contributor to view "
                    "their commits, right-click to reassign attribution</span>")
                    .arg(QDateTime::fromSecsSinceEpoch(minTs).date().toString(
                             "MMM d, yyyy"),
                         QDateTime::fromSecsSinceEpoch(maxTs).date().toString(
                             "MMM d, yyyy")));
        } else {
            m_insightsActivityAxis->clear();
        }
    }

    // Refresh the contributor count now that the log has been parsed.
    m_insightsSummary->setText(
        "<b>Repository summary</b>" +
        insightMetricsTable({
            insightMetricCell("Contributors", QString::number(contributors.size()),
                              windowDays > 0 ? QStringLiteral("in selected range")
                                             : QStringLiteral("current branch")),
            insightMetricCell("Files", QString::number(fileCount), "tracked blobs"),
            insightMetricCell("Code size", formatInsightBytes(totalBytes), "tracked bytes"),
            insightMetricCell("Issues", QString::number(totalIssues),
                              QStringLiteral("%1 open / %2 closed")
                                  .arg(openIssues)
                                  .arg(closedIssues)),
            insightMetricCell("Pull requests", QString::number(totalPulls),
                              QStringLiteral("%1 open / %2 merged / %3 closed")
                                  .arg(openPulls)
                                  .arg(mergedPulls)
                                  .arg(closedPulls)),
        }));

    if (m_insightsActivity) {
        QStringList escapedNotes;
        for (const QString &note : std::as_const(notes))
            escapedNotes << note.toHtmlEscaped();
        m_insightsActivity->setText(escapedNotes.join("<br>"));
    }
}

// Right-click on a contributor row in the merged "Contributors & activity"
// table: offer to reassign that author's commits to another identity.
void MainWindow::showInsightsContributorMenu(const QPoint &pos)
{
    if (!m_insightsContributors)
        return;
    const QModelIndex idx = m_insightsContributors->indexAt(pos);
    if (!idx.isValid())
        return;
    QTableWidgetItem *nameItem = m_insightsContributors->item(idx.row(), 0);
    if (!nameItem)
        return;
    const QString name = nameItem->data(Qt::UserRole).toString().isEmpty()
                             ? nameItem->text()
                             : nameItem->data(Qt::UserRole).toString();
    if (name.isEmpty())
        return;

    QMenu menu(this);
    QAction *reassign =
        menu.addAction(QStringLiteral("Reassign attribution for \"%1\"\xE2\x80\xA6").arg(name));
    QAction *chosen = menu.exec(m_insightsContributors->viewport()->mapToGlobal(pos));
    if (chosen == reassign)
        reassignContributorIdentity(name);
}

// Jump from an Insights contributor row to the commits panel, filtered to that
// author. Drives the existing commit search box (which deepens the list to the
// whole history and re-applies on textChanged); filterCommits matches the author
// column too, so the list narrows to that contributor's commits.
void MainWindow::openCommitsForContributor(const QString &author)
{
    const QString name = author.trimmed();
    if (name.isEmpty())
        return;
    showOverviewCommits();
    if (m_commitSearch) {
        m_commitSearch->setText(name);
        m_commitSearch->setFocus();
    }
}

// Rewrite history so every commit authored (or committed) by `oldName` is
// re-attributed to a chosen name/email, fixing mis-attributed commits. Uses
// git filter-branch, which rewrites all branches and changes commit hashes, so
// it requires a clean local checkout and an explicit confirmation.
void MainWindow::reassignContributorIdentity(const QString &oldName)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString dir = repo.localPath;
    if (dir.isEmpty() || !QDir(dir).exists()) {
        QMessageBox::warning(
            this, QStringLiteral("Reassign attribution"),
            QStringLiteral("Rewriting author identity needs a local checkout of this "
                           "repository. This node only has a mirror or preview cache."));
        return;
    }

    // A history rewrite checks out files at the end; refuse on a dirty tree.
    QByteArray statusOut;
    if (runGitCapture(dir, {"status", "--porcelain"}, &statusOut, nullptr) &&
        !QString::fromUtf8(statusOut).trimmed().isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("Reassign attribution"),
            QStringLiteral("Commit or stash your local changes first \xE2\x80\x94 rewriting "
                           "history requires a clean working tree."));
        return;
    }

    // Collect the distinct author identities already in the repo so the target
    // can be picked from existing ones, and find oldName's primary email.
    QStringList identities;
    QString primaryEmail;
    {
        QByteArray out;
        if (runGitCapture(dir, {"log", "--all", "--format=%an%x1f%ae"}, &out, nullptr)) {
            QSet<QString> seen;
            QHash<QString, int> emailHits;
            for (const QByteArray &line : out.split('\n')) {
                const QStringList f =
                    QString::fromUtf8(line).split(QLatin1Char('\x1f'));
                if (f.size() < 2)
                    continue;
                const QString an = f.at(0).trimmed();
                const QString ae = f.at(1).trimmed();
                if (an.isEmpty())
                    continue;
                const QString display = QStringLiteral("%1 <%2>").arg(an, ae);
                if (!seen.contains(display)) {
                    seen.insert(display);
                    identities << display;
                }
                if (an == oldName && !ae.isEmpty())
                    ++emailHits[ae];
            }
            int best = 0;
            for (auto it = emailHits.constBegin(); it != emailHits.constEnd(); ++it) {
                if (it.value() > best) {
                    best = it.value();
                    primaryEmail = it.key();
                }
            }
        }
    }
    identities.sort(Qt::CaseInsensitive);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Reassign attribution"));
    auto *form = new QVBoxLayout(&dialog);

    auto *intro = new QLabel(
        QStringLiteral("Reassign every commit authored by <b>%1</b> to a different "
                       "name and email.")
            .arg(oldName.toHtmlEscaped()));
    intro->setWordWrap(true);
    form->addWidget(intro);

    auto *pick = new QComboBox;
    pick->addItem(QStringLiteral("\xE2\x80\x94 copy from an existing identity \xE2\x80\x94"));
    for (const QString &id : std::as_const(identities))
        pick->addItem(id);
    form->addWidget(pick);

    auto *grid = new QFormLayout;
    auto *nameEdit = new QLineEdit(oldName);
    auto *emailEdit = new QLineEdit(primaryEmail);
    grid->addRow(QStringLiteral("New name"), nameEdit);
    grid->addRow(QStringLiteral("New email"), emailEdit);
    form->addLayout(grid);

    // Selecting an existing identity fills the fields ("Name <email>").
    connect(pick, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
            [pick, nameEdit, emailEdit](int i) {
                if (i <= 0)
                    return;
                const QString text = pick->itemText(i);
                const int lt = text.lastIndexOf(QLatin1Char('<'));
                const int gt = text.lastIndexOf(QLatin1Char('>'));
                if (lt > 0 && gt > lt) {
                    nameEdit->setText(text.left(lt).trimmed());
                    emailEdit->setText(text.mid(lt + 1, gt - lt - 1).trimmed());
                }
            });

    auto *warn = new QLabel(
        QStringLiteral("<span style='color:#d29922'>This rewrites history on all "
                       "branches and changes commit hashes. It cannot be undone.</span>"));
    warn->setWordWrap(true);
    warn->setTextFormat(Qt::RichText);
    form->addWidget(warn);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Reassign"));
    form->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString newName = nameEdit->text().trimmed();
    const QString newEmail = emailEdit->text().trimmed();
    if (newName.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Reassign attribution"),
                             QStringLiteral("The new name cannot be empty."));
        return;
    }
    if (newName == oldName && newEmail == primaryEmail) {
        QMessageBox::information(this, QStringLiteral("Reassign attribution"),
                                 QStringLiteral("Nothing to change."));
        return;
    }

    if (QMessageBox::question(
            this, QStringLiteral("Reassign attribution"),
            QStringLiteral("Re-attribute all commits by \"%1\" to \"%2 <%3>\"?\n\n"
                           "This rewrites every branch and changes commit hashes "
                           "from the first affected commit onward.")
                .arg(oldName, newName, newEmail),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes)
        return;

    // sh single-quote a value for safe embedding in the env-filter script.
    auto shq = [](const QString &s) {
        QString e = s;
        e.replace(QLatin1Char('\''), QLatin1String("'\\''"));
        return QLatin1Char('\'') + e + QLatin1Char('\'');
    };
    const QString script =
        QStringLiteral(
            "OLD=%1\n"
            "if [ \"$GIT_AUTHOR_NAME\" = \"$OLD\" ]; then "
            "export GIT_AUTHOR_NAME=%2; export GIT_AUTHOR_EMAIL=%3; fi\n"
            "if [ \"$GIT_COMMITTER_NAME\" = \"$OLD\" ]; then "
            "export GIT_COMMITTER_NAME=%2; export GIT_COMMITTER_EMAIL=%3; fi\n")
            .arg(shq(oldName), shq(newName), shq(newEmail));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("FILTER_BRANCH_SQUELCH_WARNING"), QStringLiteral("1"));

    // filter-branch and the gc afterwards can run for many seconds; use a long
    // poll loop that keeps the UI alive rather than runGitCapture's 8s cap.
    auto runLong = [&env](const QString &d, const QStringList &args,
                          QString *err) -> bool {
        QProcess p;
        p.setProcessEnvironment(env);
        p.start("git", QStringList{"-C", d} + args);
        QElapsedTimer timer;
        timer.start();
        while (!p.waitForFinished(50)) {
            if (p.state() == QProcess::NotRunning)
                break;
            if (timer.hasExpired(300000)) {
                p.kill();
                if (err)
                    *err = QStringLiteral("git timed out");
                return false;
            }
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        }
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            if (err)
                *err = QString::fromUtf8(p.readAllStandardError()).trimmed();
            return false;
        }
        return true;
    };

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    bool ok = runLong(dir,
                      {"filter-branch", "--force", "--env-filter", script,
                       "--tag-name-filter", "cat", "--", "--all"},
                      &err);
    if (ok) {
        // Drop filter-branch's backup refs and reclaim the rewritten objects so
        // the new identity is the only one git reports.
        QByteArray origRefs;
        if (runGitCapture(dir, {"for-each-ref", "--format=%(refname)", "refs/original/"},
                          &origRefs, nullptr)) {
            for (const QByteArray &ref : origRefs.split('\n')) {
                const QString r = QString::fromUtf8(ref).trimmed();
                if (!r.isEmpty())
                    runGitCapture(dir, {"update-ref", "-d", r}, nullptr, nullptr);
            }
        }
        runLong(dir, {"reflog", "expire", "--expire=now", "--all"}, nullptr);
        runLong(dir, {"gc", "--prune=now"}, nullptr);
    }
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::critical(
            this, QStringLiteral("Reassign attribution"),
            QStringLiteral("History rewrite failed: %1")
                .arg(err.isEmpty() ? QStringLiteral("git failed") : err.left(400)));
        return;
    }

    QMessageBox::information(
        this, QStringLiteral("Reassign attribution"),
        QStringLiteral("Re-attributed commits by \"%1\" to \"%2 <%3>\".\n\n"
                       "Commit hashes changed; re-publish or re-sync mirrors so "
                       "peers pick up the rewrite.")
            .arg(oldName, newName, newEmail));

    // Reassign is launched from the Insights table, so that page is on screen;
    // loadCommits() refreshes the commit list and, while Insights is visible, its
    // contributor counts too — picking up the re-attributed authorship.
    loadCommits();
}

// Per-repo metadata lives in .forkmesh/info.json (issue #232). Older repos kept
// it at the working-tree root, so reads fall back to that legacy location.
static QString repoInfoJsonWritePath(const QString &localPath)
{
    return QDir(localPath).filePath(QStringLiteral(".forkmesh/info.json"));
}

static QString repoInfoJsonReadPath(const QString &localPath)
{
    const QString preferred = repoInfoJsonWritePath(localPath);
    if (QFileInfo::exists(preferred))
        return preferred;
    const QString legacy = QDir(localPath).filePath(QStringLiteral("info.json"));
    return QFileInfo::exists(legacy) ? legacy : preferred;
}

void MainWindow::loadRepoInfo()
{
    m_repoInfo = RepoInfo();
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);

    QByteArray raw;
    const QString local = QDir(repo.localPath).filePath(kRepoInfoPath);
    if (!repo.localPath.isEmpty() && QFileInfo::exists(local)) {
        QFile file(local);
        if (file.open(QIODevice::ReadOnly))
            raw = file.readAll();
    } else {
        const QString dir = repoGitDir();
        if (!dir.isEmpty())
            runGitCapture(dir, {"show", currentRef() + ":" + kRepoInfoPath}, &raw,
                          nullptr);
    }
    if (raw.isEmpty())
        return;

    const QJsonObject obj = QJsonDocument::fromJson(raw).object();
    m_repoInfo.about = obj.value("about").toString();
    m_repoInfo.website = obj.value("website").toString();
    m_repoInfo.language = obj.value("language").toString();
    m_repoInfo.defaultBranch = obj.value("defaultBranch").toString();
    m_repoInfo.forks = obj.value("forks").toInt(0);
    m_repoInfo.stars = obj.value("stars").toInt(0);
    m_repoInfo.mirrors = obj.value("mirrors").toInt(1);
    for (const QJsonValue &v : obj.value("topics").toArray())
        m_repoInfo.topics << v.toString();
    for (const QJsonValue &v : obj.value("contributors").toArray()) {
        const QJsonObject c = v.toObject();
        if (!c.value("name").toString().isEmpty())
            m_repoInfo.contributorAvatars.insert(c.value("name").toString(),
                                                 c.value("avatar").toString());
    }
}

void MainWindow::editRepoAbout()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    if (!repoHasWorkingTree()) {
        QMessageBox::information(
            this, "Edit repository details",
            "Open a repository with a local working copy to edit its About details.");
        return;
    }

    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    QDialog dialog(this);
    dialog.setWindowTitle("Edit repository details");

    auto *descriptionEdit = new QPlainTextEdit(&dialog);
    descriptionEdit->setPlaceholderText("Short repository description");
    descriptionEdit->setPlainText(m_repoInfo.about.isEmpty() ? repo.description
                                                             : m_repoInfo.about);
    descriptionEdit->setMaximumHeight(96);

    auto *websiteEdit = new QLineEdit(&dialog);
    websiteEdit->setPlaceholderText("https://example.com");
    websiteEdit->setText(m_repoInfo.website);

    auto *form = new QFormLayout;
    form->addRow("Description", descriptionEdit);
    form->addRow("Website", websiteEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save |
                                         QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, &dialog,
                                                         descriptionEdit,
                                                         websiteEdit] {
        QString error;
        if (!saveRepoAboutMetadata(descriptionEdit->toPlainText(),
                                   websiteEdit->text(), &error)) {
            QMessageBox::warning(&dialog, "Edit repository details",
                                 error.isEmpty()
                                     ? QStringLiteral("Could not save details.")
                                     : error);
            return;
        }
        dialog.accept();
    });

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    layout->addLayout(form);
    layout->addWidget(buttons);
    dialog.resize(460, 220);
    dialog.exec();
}

bool MainWindow::saveRepoAboutMetadata(const QString &about,
                                       const QString &websiteInput,
                                       QString *error)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        if (error)
            *error = QStringLiteral("No repository is open.");
        return false;
    }
    if (!repoHasWorkingTree()) {
        if (error)
            *error = QStringLiteral(
                "Open a repository with a local working copy to edit its About details.");
        return false;
    }

    const QString website = normalizedRepoWebsite(websiteInput, error);
    if (!websiteInput.trimmed().isEmpty() && website.isEmpty())
        return false;

    const int index = m_repoDetailIndex;
    RepositoryRecord &repo = m_repositories[index];
    const QDir repoDir(repo.localPath);
    const QString infoPath = repoDir.filePath(kRepoInfoPath);
    QJsonObject obj;
    if (QFileInfo::exists(infoPath)) {
        QFile file(infoPath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Could not read .forkmesh/info.json.");
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            if (error)
                *error = QStringLiteral(".forkmesh/info.json is not valid JSON.");
            return false;
        }
        obj = doc.object();
    }

    const QString aboutText = about.trimmed();
    if (aboutText.isEmpty())
        obj.remove(QStringLiteral("about"));
    else
        obj.insert(QStringLiteral("about"), aboutText);
    if (website.isEmpty())
        obj.remove(QStringLiteral("website"));
    else
        obj.insert(QStringLiteral("website"), website);

    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (!repoDir.mkpath(QStringLiteral(".forkmesh"))) {
        if (error)
            *error = QStringLiteral("Could not create .forkmesh directory.");
        return false;
    }
    QSaveFile file(infoPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not write .forkmesh/info.json.");
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Could not save .forkmesh/info.json.");
        return false;
    }

    // Now that the metadata lives under .forkmesh/, drop any stale root-level
    // info.json so the repo carries a single source of truth (issue #232).
    const QString legacyPath = QDir(repo.localPath).filePath("info.json");
    if (legacyPath != infoPath && QFileInfo::exists(legacyPath))
        QFile::remove(legacyPath);

    repo.description = aboutText;
    saveRepositories();
    m_repoInfo.about = aboutText;
    m_repoInfo.website = website;
    loadAboutSidebar();
    refreshRepositoryList();
    if (repo.publishToNetwork)
        publishRepository(index, false);
    logSystem(QStringLiteral("Updated About details for %1/%2.")
                  .arg(repo.owner, repo.name));
    setRepoDetailNotice(QStringLiteral("Updated repository details."));
    return true;
}

void MainWindow::setRepoBranch(const QString &branch)
{
    m_repoBranch = branch;
    if (m_branchButton)
        m_branchButton->setText(branch);
    loadRepoOverview(QString());
    loadAboutSidebar();
    loadCommits(); // also refreshes the Insights counts when that tab is on screen
}

// The checked-out branch (HEAD), or empty if detached / no working tree.
QString MainWindow::repoHeadBranch() const
{
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return QString();
    QByteArray out;
    if (!runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &out, nullptr))
        return QString();
    const QString b = QString::fromUtf8(out).trimmed();
    return b == QLatin1String("HEAD") ? QString() : b; // "HEAD" => detached
}

// Refresh the commits-page branch button: its label (the branch whose history is
// on screen) and its dropdown (every branch, plus "Create new branch…").
// Selecting a branch *browses* its commits — exactly like the Code-tab switcher —
// rather than checking it out into the working tree. The commit list is driven by
// currentRef() (the browsed ref), so the indicator must track that same ref or the
// two disagree: e.g. after browsing a branch elsewhere the list would show it while
// the button still claimed HEAD, and clicking then did a heavyweight checkout (also
// blockable by uncommitted changes) instead of switching the list. Real checkouts
// live in the Branches panel; here switching is a read-only history view change.
void MainWindow::refreshCommitsBranchButton()
{
    if (!m_commitsBranchButton)
        return;
    // What the list below actually shows: the explicitly browsed branch, or the
    // checked-out HEAD when none is pinned (currentRef() resolves to "HEAD" then).
    const QString browsed = m_repoBranch.isEmpty() ? repoHeadBranch() : m_repoBranch;
    m_commitsBranchButton->setText(
        browsed.isEmpty() ? QStringLiteral("(detached)") : browsed);

    auto *menu = new QMenu(m_commitsBranchButton);
    QAction *create =
        menu->addAction(QString::fromUtf8("\xEF\xBC\x8B  Create new branch\xE2\x80\xA6"));
    connect(create, &QAction::triggered, this, [this] { createAndCheckoutBranch(); });
    menu->addSeparator();
    const QStringList branches = repoBranches();
    for (const QString &b : branches) {
        QAction *a = menu->addAction(b);
        a->setCheckable(true);
        a->setChecked(b == browsed);
        connect(a, &QAction::triggered, this, [this, b] { setRepoBranch(b); });
    }
    if (branches.isEmpty())
        menu->addAction(QStringLiteral("No branches"))->setEnabled(false);
    // Replacing the menu frees the previous one (parented to the button) lazily.
    QMenu *old = m_commitsBranchButton->menu();
    m_commitsBranchButton->setMenu(menu);
    if (old)
        old->deleteLater();
}

// "Create new branch…" — make a branch off HEAD and switch to it.
void MainWindow::createAndCheckoutBranch()
{
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
                             this, QStringLiteral("Create new branch"),
                             QStringLiteral("New branch name:"), QLineEdit::Normal,
                             QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    QByteArray st;
    if (runGitCapture(dir, {"status", "--porcelain"}, &st, nullptr) &&
        !st.trimmed().isEmpty()) {
        setRepoDetailNotice(
            QStringLiteral("Commit or stash your changes before creating a branch."),
            true);
        return;
    }
    QString err;
    if (!runGitCapture(dir, {"checkout", "-b", name}, nullptr, &err)) {
        setRepoDetailNotice(
            QStringLiteral("Could not create %1: %2").arg(name, err.left(200)), true);
        return;
    }
    logSystem(QStringLiteral("Git: created and checked out %1.").arg(name));
    setRepoDetailNotice(QStringLiteral("Created and switched to %1.").arg(name));
    m_branchesCache.clear(); // new branch — bust the cache so it appears in the menu
    setRepoBranch(name);
    refreshCommitsBranchButton();
}

QStringList MainWindow::repoBranches() const
{
    const QString dir = repoGitDir();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (dir == m_branchesCacheDir && !m_branchesCache.isEmpty() &&
        now - m_branchesCacheTime < 5) {
        return m_branchesCache;
    }
    QStringList branches;
    QByteArray out;
    if (!dir.isEmpty() &&
        runGitCapture(dir,
                      {"branch", "--sort=-committerdate",
                       "--format=%(refname:short)"},
                      &out, nullptr)) {
        for (const QString &line : QString::fromUtf8(out).split('\n')) {
            const QString branch = line.trimmed();
            if (!branch.isEmpty() && !branches.contains(branch))
                branches.append(branch);
        }
    }
    m_branchesCacheDir = dir;
    m_branchesCache = branches;
    m_branchesCacheTime = now;
    return branches;
}

QString MainWindow::repoDefaultBranch(const QStringList &branches) const
{
    // The default branch is the stable base every other branch is measured and
    // merged against — it must NOT drift just because the working tree happens to
    // be parked on a feature branch. An explicitly configured default always wins,
    // then the conventional main/master names. Only when none of those exist do we
    // fall back to the checked-out HEAD; otherwise switching branches in the
    // commits area (or a transient checkout during a branches-page merge) would
    // silently change the default branch, which we never want to do on its own.
    const QString configured = m_repoInfo.defaultBranch.trimmed();
    if (!configured.isEmpty() && branches.contains(configured))
        return configured;
    if (branches.contains(QStringLiteral("main")))
        return QStringLiteral("main");
    if (branches.contains(QStringLiteral("master")))
        return QStringLiteral("master");

    QByteArray head;
    const QString dir = repoGitDir();
    if (!dir.isEmpty() &&
        runGitCapture(dir, {"symbolic-ref", "--short", "HEAD"}, &head,
                      nullptr)) {
        const QString branch = QString::fromUtf8(head).trimmed();
        if (branches.contains(branch))
            return branch;
    }
    if (!m_repoBranch.isEmpty() && branches.contains(m_repoBranch))
        return m_repoBranch;
    return branches.isEmpty() ? QString() : branches.first();
}

void MainWindow::loadBranchesAndTags()
{
    const QString dir = repoGitDir();

    // Current branch / default ref.
    QString branch = m_repoInfo.defaultBranch;
    if (branch.isEmpty() && !dir.isEmpty()) {
        QByteArray head;
        if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &head, nullptr))
            branch = QString::fromUtf8(head).trimmed();
    }
    if (branch.isEmpty() || branch == "HEAD")
        branch = QStringLiteral("HEAD");
    m_repoBranch = branch == "HEAD" ? QString() : branch;
    if (m_branchButton)
        m_branchButton->setText(m_repoBranch.isEmpty() ? "HEAD" : m_repoBranch);

    const QStringList branches = repoBranches();
    if (m_branchesButton) {
        m_branchesButton->setText(
            QStringLiteral("%1 %2")
                .arg(formatCount(branches.size()))
                .arg(branches.size() == 1 ? QStringLiteral("branch")
                                          : QStringLiteral("branches")));
        m_branchesButton->setEnabled(!branches.isEmpty());
    }
    if (m_repoBranchesTab)
        m_repoBranchesTab->setText(
            QStringLiteral("Branches (%1)").arg(formatCount(branches.size())));

    // Branch menu.
    if (m_branchButton) {
        auto *menu = new QMenu(m_branchButton);
        for (const QString &branchName : branches)
            menu->addAction(branchName, this,
                            [this, branchName] { setRepoBranch(branchName); });
        if (menu->isEmpty())
            menu->addAction("No branches")->setEnabled(false);
        m_branchButton->setMenu(menu);
    }

    // Tags: count them and note the newest (the current release). The count
    // feeds both the "Tags N" pill and the Releases (N) tab badge eagerly (so it
    // shows without opening the panel, like Branches); the newest tag names the
    // current-release pill floating just above the Releases tab (adhoc #69).
    {
        QByteArray out;
        int count = 0;
        QString currentTag;
        if (!dir.isEmpty() && runGitCapture(dir, {"tag", "--sort=-creatordate"}, &out,
                                            nullptr)) {
            for (const QString &line : QString::fromUtf8(out).split('\n')) {
                const QString tag = line.trimmed();
                if (tag.isEmpty())
                    continue;
                if (currentTag.isEmpty())
                    currentTag = tag; // sorted newest-first
                ++count;
            }
        }
        if (m_tagsButton)
            m_tagsButton->setText(QStringLiteral("Tags %1").arg(formatCount(count)));
        if (m_repoReleasesTab)
            m_repoReleasesTab->setText(
                QStringLiteral("Releases (%1)").arg(formatCount(count)));
        if (m_releaseStrip) {
            m_releaseStrip->setText(currentTag);
            m_releaseStrip->setToolTip(
                currentTag.isEmpty()
                    ? QString()
                    : QStringLiteral("Current release: %1").arg(currentTag));
            positionReleaseStrip(); // anchor + reveal (or hide) over the Releases tab
        }
    }

    // Only refresh the Branches / Releases panels if one is actually on screen.
    // They run a git command per branch/tag, so eagerly refreshing them on every
    // ref change (including at startup) would needlessly slow things down — the
    // tab-switch handler refreshes them when the user opens them.
    if (m_repoDetailStack) {
        const int current = m_repoDetailStack->currentIndex();
        // The branches panel now lives inside the Code overview (index 0), shown
        // when the overview body stack is on its branches page (index 2).
        if (current == 0 && m_overviewBodyStack &&
            m_overviewBodyStack->currentIndex() == 2)
            loadBranchesPanel();
        else if (current == m_releasesTabIndex)
            loadReleasesPanel();
    }
}

bool MainWindow::repoHasWorkingTree() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return false;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    return !repo.previewOnly && !repo.localPath.isEmpty() &&
           QDir(repo.localPath).exists(".git");
}



// ---- Repo-detail & commits UI builders (moved from MainWindowIssues) ----

// ---- Repo detail (files + issues tabs) -------------------------------------

QWidget *MainWindow::buildRepoDetailSection()
{
    auto *page = new QWidget;

    // --- GitHub-style header: title + Public badge, action buttons on the right.
    // The repo identity and public/private state now live in the top-bar repo
    // dropdown, so the old "owner/name  Public" header is omitted here. The label
    // is still created (hidden) because other code sets its text.
    m_repoHeaderTitle = new QLabel("Repository");
    m_repoHeaderTitle->setObjectName("repoHeaderTitle");
    m_repoHeaderTitle->setTextFormat(Qt::RichText);
    m_repoHeaderTitle->hide();

    auto *notifyButton = new QPushButton("Notify");
    notifyButton->setObjectName("repoAction");
    notifyButton->setToolTip("Notifications");
    setOcticon(notifyButton, "bell", 16);
    m_forkButton = new QPushButton("Fork 0");
    m_mirrorButton = new QPushButton("Mirror 1");
    m_sourceButton = new QPushButton("Source");
    // Open-in-browser link, mirroring the relay switcher's open button: takes
    // the active repo to its page on the mainnode website.
    m_repoOpenButton = new QPushButton("Open");
    for (QPushButton *b :
         {notifyButton, m_forkButton, m_mirrorButton, m_sourceButton,
          m_repoOpenButton}) {
        b->setObjectName("repoAction");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_forkButton, "repo-forked", 16);
    setOcticon(m_mirrorButton, "sync", 16);
    setOcticon(m_sourceButton, "code", 16);
    setOcticon(m_repoOpenButton, "link", 16);
    m_repoOpenButton->setToolTip("Open this repository on the web");
    connect(m_repoOpenButton, &QPushButton::clicked, this,
            &MainWindow::openRepositoryWebsite);
    m_mirrorButton->setToolTip("Mirror status and actions");
    m_forkButton->setToolTip("Fork this repository into a local folder");
    m_sourceButton->setToolTip("Download or use this repository's local remote");
    m_mirrorMenu = new QMenu(m_mirrorButton);
    m_sourceMenu = new QMenu(m_sourceButton);
    m_mirrorButton->setMenu(m_mirrorMenu);
    m_sourceButton->setMenu(m_sourceMenu);
    // Fork is a direct action, not a menu: clicking it asks where (which local
    // folder) to fork the repo into, then creates the fork there.
    connect(m_forkButton, &QPushButton::clicked, this,
            &MainWindow::forkCurrentRepo);
    connect(m_mirrorMenu, &QMenu::aboutToShow, this,
            &MainWindow::updateRepoActionMenus);
    connect(m_sourceMenu, &QMenu::aboutToShow, this,
            &MainWindow::updateRepoActionMenus);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(16, 12, 16, 4);
    headerRow->setSpacing(8);
    // Left cluster (Code / Chat / Notifications / Settings) is filled in later by
    // buildBreadcrumb, which creates those buttons and adds them here so they sit
    // on the same line as the repo actions, below the Solana notice.
    m_repoHeaderLeft = new QHBoxLayout;
    m_repoHeaderLeft->setContentsMargins(0, 0, 0, 0);
    m_repoHeaderLeft->setSpacing(8);
    headerRow->addLayout(m_repoHeaderLeft);
    headerRow->addStretch();
    headerRow->addWidget(notifyButton);
    headerRow->addWidget(m_forkButton);
    headerRow->addWidget(m_mirrorButton);
    headerRow->addWidget(m_sourceButton);
    headerRow->addWidget(m_repoOpenButton);

    m_repoDetailNotice = new QLabel;
    m_repoDetailNotice->setObjectName("repoInlineNotice");
    m_repoDetailNotice->setWordWrap(true);
    m_repoDetailNotice->hide();

    auto *metaBand = new QWidget;
    metaBand->setObjectName("repoDetailMeta");
    m_repoDetailStatus = new QLabel;
    m_repoDetailStatus->setObjectName("statusLine");
    m_repoDetailStatus->setWordWrap(true);
    m_repoDetailStatus->setTextFormat(Qt::RichText);

    auto *metaLayout = new QVBoxLayout(metaBand);
    metaLayout->setContentsMargins(16, 4, 16, 8);
    metaLayout->setSpacing(6);
    metaLayout->addWidget(m_repoDetailStatus);
    // Served/clone counts and hosted-since/last-sync now live in the node
    // profile panel, so this band stays hidden in the repo view.
    metaBand->hide();

    // --- Tab bar (GitHub order; Commits lives inside the Code overview).
    struct TabDef {
        const char *label;
        const char *icon;
    };
    // Note: existing pages are index-addressed in several places (idClicked,
    // switchTo*). Branches/Releases are appended after Insights so those indices
    // stay valid. Chat is no longer here — it's a top-level section.
    const QList<TabDef> tabs = {{"Code", "code"},
                                {"Commits", "git-branch"},
                                {"Issues", "issue-opened"},
                                {"Agents", "terminal"},
                                {"Pull requests", "git-pull-request"},
                                {"Discussions", "comment"},
                                {"Actions", "workflow"},
                                {"Security", "shield-check"},
                                {"Quality", "check-circle"},
                                {"Insights", "graph"},
                                {"Branches", "repo-forked"},
                                {"Worktrees", "file-directory"},
                                {"Releases", "tag"},
                                {"Mirror nodes", "server"},
                                {"Artifacts", "package"},
                                {"Shortcuts", "rocket"},
                                {"Settings", "gear"}};
    m_repoDetailTabs = new QButtonGroup(this);
    m_repoDetailTabs->setExclusive(true);
    auto *tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(12, 0, 12, 0);
    tabRow->setSpacing(2);
    for (int i = 0; i < tabs.size(); ++i) {
        // Commits (id 1) no longer gets a top-bar tab: its panel lives inside
        // the Code overview, toggled by the commit strip's "N Commits" button.
        // Agents (id 3) also no longer gets a top-bar tab (adhoc #178) — it's
        // reached via the footer status strip, spinner overlays and issue/PR
        // links instead. Branches (id 10) likewise lost its top-bar tab: its
        // panel now lives inside the Code overview, toggled by the toolbar's
        // "N branches" button. All three entries stay in the list so every later
        // tab keeps its positional id.
        if (i == 1 || i == 3 || i == 10)
            continue;
        const TabDef tab = tabs.at(i);
        auto *b = new QPushButton(QString::fromLatin1(tab.label));
        b->setObjectName("repoTab");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        setOcticon(b, QString::fromLatin1(tab.icon), 16);
        if (i == 0)
            b->setChecked(true);
        if (i == 0)
            m_repoCodeTab = b; // also shows the on-disk repo size
        if (i == 2)
            m_repoIssuesTab = b; // keep a handle for the Issues (N) badge; the
                                 // looper toggle floats just above this tab
                                 // (adhoc #130, created below).
        if (i == 4)
            m_repoPullsTab = b;
        if (i == 5)
            m_repoDiscussionsTab = b;
        if (i == 6)
            m_repoActionsTab = b; // handle for the Actions (N) badge
        if (i == 11)
            m_repoWorktreesTab = b; // handle for the Worktrees (N) badge
        if (i == 12)
            m_repoReleasesTab = b; // handle for the Releases (N) badge
        if (i == 13)
            m_repoMirrorsTab = b; // handle for the Mirror nodes (N) badge
        m_repoDetailTabs->addButton(b, i);
        tabRow->addWidget(b);
    }
    tabRow->addStretch();
    auto *tabBar = new QWidget;
    tabBar->setObjectName("repoTabBar");
    tabBar->setLayout(tabRow);

    // The integrity-pin warning ("clones are being rejected — reset the pin") no
    // longer lives in an in-page banner here; refreshRepoPinBanner surfaces it in
    // the top-bar notification toast (see showPinWarning), where its "Reset
    // integrity pin" and "Why?" actions are clickable links.

    m_repoPushButton = new QPushButton(this);
    m_repoPushButton->setObjectName("primaryButton");
    m_repoPushButton->setCursor(Qt::PointingHandCursor);
    m_repoPushButton->hide();
    m_repoPushButton->setStyleSheet(
        QStringLiteral("QPushButton#primaryButton{padding:3px 10px;font-size:12px;}"));
    setOcticon(m_repoPushButton, "sync", 14);
    connect(m_repoPushButton, &QPushButton::clicked, this,
            &MainWindow::pushCurrentRepoUpstream);
    // The "Sync" button floats in the band just above the Code tab
    // (see positionRepoPushButton) rather than living in the tab row: it's an
    // overlay raised one above the tabs, so showing/hiding it as sync state
    // changes never reflows the tab content below — that shift is what read as the
    // whole view "resizing" on small screens, most visibly on Mirror nodes.
    m_repoPublishBar = nullptr; // no separate row: the button floats over Code

    // Eye icon riding beside the Sync button: a one-click shortcut to the
    // commits panel while the pending-commits state is already on screen.
    m_repoPushEyeButton = new QPushButton(this);
    m_repoPushEyeButton->setObjectName("ghostButton");
    m_repoPushEyeButton->setProperty("buttonSize", "sm");
    m_repoPushEyeButton->setCursor(Qt::PointingHandCursor);
    m_repoPushEyeButton->hide();
    setOcticon(m_repoPushEyeButton, "eye", 14);
    m_repoPushEyeButton->setToolTip("View the commit history");
    connect(m_repoPushEyeButton, &QPushButton::clicked, this, [this] {
        if (m_historyButton && !m_historyButton->isChecked())
            m_historyButton->click();
        else
            showOverviewCommits();
    });

    // Issue-looper toggle (adhoc #130): a compact switch floating in the band
    // just above the Issues tab, mirroring how the Sync button floats over
    // Code. It both shows the loop's state and toggles it, so the loop is
    // controllable and visible from any tab without an in-page banner. Created
    // parented to the window; positionLooperToggle reparents it onto the page.
    auto *looperToggle = new LooperToggle(this);
    looperToggle->hide();
    looperToggle->setOnClick([this] { toggleIssueLooper(); });
    // Clicking the "#N" itself jumps to the agent currently working that issue
    // instead of toggling the loop (adhoc #134).
    looperToggle->setOnNumberClick([this] {
        int sessionId = m_looperSessionId;
        if (sessionId <= 0 && m_looperCurrentIssue > 0)
            if (const AgentSession *s =
                    latestAgentSessionForIssue(m_looperCurrentIssue))
                sessionId = s->id;
        if (sessionId > 0)
            switchToAgentsTab(sessionId);
    });
    m_looperToggle = looperToggle;

    // Live mirror-activity dots floating just above the Mirror nodes tab (adhoc
    // #197): one dot per active node, flashing green for a served clone and
    // orange for codebase browsing. Like the looper toggle over Issues it's an
    // overlay, so it shows from any tab and never reflows the page; the old
    // in-page "Live ›" row was dropped in its favour. Created parented to the
    // window; positionMirrorActivityStrip reparents it onto the page.
    auto *mirrorStrip = new MirrorActivityStrip(this);
    mirrorStrip->setToolTip(QStringLiteral(
        "Active nodes mirroring this repo. A dot flashes green when its node "
        "serves a clone, orange when it serves codebase browsing."));
    mirrorStrip->hide();
    m_mirrorActivityStrip = mirrorStrip;

    // Current-release label floating just above the Releases tab (adhoc #69),
    // mirroring the mirror-activity strip over Mirror nodes: it names the newest
    // release so it's visible from any tab. Rendered as plain text (no pill
    // chrome) at a comfortably readable size; the colour is left to the theme
    // foreground. Created parented to the window; positionReleaseStrip reparents
    // it onto the page and the tag scan (loadBranchesAndTags / loadReleasesPanel)
    // fills in its text.
    m_releaseStrip = new QLabel(this);
    m_releaseStrip->setObjectName("releaseStrip");
    m_releaseStrip->setStyleSheet(
        QStringLiteral("#releaseStrip{background:transparent;border:none;"
                       "padding:0px;font-size:15px;}"));
    m_releaseStrip->hide();

    // --- Inner stack: one page per tab.
    m_repoDetailStack = new QStackedWidget;
    m_repoDetailStack->addWidget(buildRepoFilesPanel());                 // 0 Code
    // 1 — placeholder. The commits panel lives inside the Code overview (built
    // by buildRepoOverviewPage, under the latest-commit bar); this empty page
    // keeps the positional ids of every later tab (Issues=2 …) unchanged.
    m_repoDetailStack->addWidget(new QWidget);
    m_repoDetailStack->addWidget(buildIssuesSection());                  // 2 Issues
    m_repoDetailStack->addWidget(buildAgentsTab());                      // 3 Agents
    m_repoDetailStack->addWidget(buildPullsTab());                       // 4 Pull requests
    m_repoDetailStack->addWidget(buildDiscussionsTab());                 // 5 Discussions
    m_repoDetailStack->addWidget(buildRepoActionsTab());                 // 6 Actions
    m_repoDetailStack->addWidget(buildRepoSecurityTab());                // 7 Security
    m_repoDetailStack->addWidget(buildRepoQualityTab());                 // 8 Quality
    m_insightsTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildInsightsTab());                    // 9
    m_branchesTabIndex = m_repoDetailStack->count();
    // Branches has no top-level tab: its panel lives inside the Code overview
    // (built above in buildRepoOverviewPage). This placeholder keeps the
    // positional ids of every later tab (Worktrees=11 …) unchanged.
    m_repoDetailStack->addWidget(new QWidget);                           // 10 Branches (moved)
    m_worktreesTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildWorktreesTab());                   // 11 Worktrees
    m_releasesTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildReleasesTab());                    // 12 Releases
    m_mirrorNodesTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildMirrorNodesTab());                 // 13 Mirror nodes
    m_artifactsTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildArtifactsTab());                   // 14 Artifacts
    m_shortcutsTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildShortcutsTab());                   // 15 Shortcuts
    m_settingsTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildRepoSettingsTab());                // 16 Settings
    // Chat is no longer part of the repo hierarchy: it's a top-level section
    // (m_sectionStack index 2), reached from the always-visible nav.
    m_chatStackIndex = -1;
    // Record onto the Back / Forward trail whenever the visible repo tab changes,
    // by click or programmatically (opening a pull/issue jumps to its tab), so
    // every such move is a step the arrows can return to. Debounced and guarded
    // against replays, so it coalesces a repo-open's tab churn into one entry.
    connect(m_repoDetailStack, &QStackedWidget::currentChanged, this,
            [this](int) { scheduleNavRecord(); });
    connect(m_repoDetailTabs, &QButtonGroup::idClicked, this, [this](int id) {
        m_repoDetailStack->setCurrentIndex(id);
        // Update the Agents nav button state to show when the Agents tab is active (adhoc #201).
        if (m_agentsNavButton)
            m_agentsNavButton->setChecked(id == 3);
        if (id == 0) {
            // A Code click always lands on the file browser: if the overview
            // body was left on the commits panel, swap it back (and dim the
            // commit strip's toggle). The explorer/overview mode is untouched.
            showOverviewFiles();
        }
        if (id == 2) {
            // Opening Issues: clear any filter the user left set on a prior visit
            // (status/label/milestone/search) so the full list shows again.
            resetIssueFilters();
            if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
                // Drain this repo's inbox now (owner-only) so incoming issues from
                // other nodes show immediately instead of next poll tick.
                drainIssuesInboxFor(m_repositories.at(m_repoDetailIndex), false);
        }
        // id 1 (Commits) has no top-bar button anymore; the equivalent deferred
        // list build lives in the commit strip's "N Commits" click handler
        // (see buildRepoOverviewPage).
        else if (id == 3) {
            // issue #289: reloadAgents() shells two git reads per session to
            // compute Diff cells, blocking the GUI thread for a beat. Run inline
            // it delays the tab's repaint — setCurrentIndex(3) above only queues
            // a paint event, which can't process until this slot returns, so the
            // Agents page visibly appears only *after* the git work ("slight lag"
            // switching from Issues). Defer to the next event-loop tick: the tab
            // paints first (the table keeps its prior rows), then the reload runs.
            // Load pulls first so the agents list can show each session's PR
            // status (open/merged/closed) from m_currentPulls.
            QTimer::singleShot(0, this, [this] {
                reloadPulls();
                reloadAgents();
            });
        }
        else if (id == 4) {
            reloadPulls();
            if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
                drainPullsInboxFor(m_repositories.at(m_repoDetailIndex), false);
        }
        else if (id == 5) {
            reloadDiscussions();
            if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
                drainDiscussionsInboxFor(m_repositories.at(m_repoDetailIndex), false);
        }
        else if (id == 6)
            refreshRepoActions();
        else if (id == 7)
            refreshRepoSecurity();
        else if (id == 8)
            refreshRepoQuality();
        else if (id == m_insightsTabIndex)
            loadRepoInsights();
        // id == m_branchesTabIndex has no top-bar button anymore; the Branches
        // panel is opened by the Code overview's "N branches" toolbar button.
        else if (id == m_worktreesTabIndex)
            loadWorktreesPanel();
        else if (id == m_releasesTabIndex)
            loadReleasesPanel();
        else if (id == m_mirrorNodesTabIndex)
            loadMirrorNodesPanel();
        else if (id == m_artifactsTabIndex)
            loadArtifactsPanel();
        else if (id == m_shortcutsTabIndex)
            loadShortcutsPanel();
        else if (id == m_settingsTabIndex)
            refreshRepoSettings();
        // Hand keyboard focus to the new tab's list so the user can arrow through
        // its rows right away instead of having to click a row first.
        focusRepoDetailTable(id);
    });

    // Land on the Code view; opening a repo refreshes it (see openRepoDetail).
    m_repoDetailStack->setCurrentIndex(0);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addLayout(headerRow);
    layout->addWidget(m_repoDetailNotice);
    layout->addWidget(metaBand);
    layout->addWidget(tabBar);
    layout->addWidget(m_repoDetailStack, 1);
    return page;
}


QWidget *MainWindow::buildRepoCommitsTab()
{
    m_commitsStack = new QStackedWidget;

    // --- Page 0: the commit list.
    auto *listPage = new QWidget;
    m_commitsTable = new QTableWidget(0, 9);
    m_commitsTable->setObjectName("commitsList");
    installColumnHeaderMenu(m_commitsTable); // 3-dots per-column menu (issue #318)
    enableHoverRowHighlight(m_commitsTable); // green outline selection (issue #252)
    m_commitsTable->setHorizontalHeaderLabels(
        {"Author", "Date", "Commit", "Files", "+adds", "-dels", "Summary", "", ""});
    m_commitsTable->verticalHeader()->setVisible(false);
    m_commitsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Extended selection so several commits can be picked (Ctrl/Shift-click) and
    // turned into a summary message / X post; a plain click still opens the diff.
    m_commitsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_commitsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_commitsTable->setShowGrid(false);
    m_commitsTable->setWordWrap(false);
    m_commitsTable->setSortingEnabled(true);
    m_commitsTable->setToolTip("Click a column header to sort");
    m_commitsTable->setTextElideMode(Qt::ElideRight);
    // Fixed default column widths instead of ResizeToContents: the latter
    // rescans every row on each resize, which makes dragging the splitter
    // beside a 300-row table choppy. Interactive sections stay smooth.
    QHeaderView *commitHeader = m_commitsTable->horizontalHeader();
    commitHeader->setHighlightSections(false);
    commitHeader->setSectionResizeMode(QHeaderView::Interactive);
    // Commit column (index 2) is wider so the short hash — plus the leading
    // "▲" unsynced marker — isn't clipped.
    const int commitColWidths[kCommitSummaryCol] = {150, 72, 120, 60, 66, 66};
    for (int i = 0; i < kCommitSummaryCol; ++i) {
        commitHeader->setSectionResizeMode(i, QHeaderView::Interactive);
        commitHeader->resizeSection(i, commitColWidths[i]);
    }
    commitHeader->setSectionResizeMode(kCommitSummaryCol, QHeaderView::Stretch);
    // Trailing action column: a fixed, narrow slot for the per-row delete button.
    commitHeader->setSectionResizeMode(kCommitActionCol, QHeaderView::Fixed);
    commitHeader->resizeSection(kCommitActionCol, 38);
    // Git-graph gutter: a fixed, narrow column drawn by CommitGraphDelegate and
    // moved to the far left so it reads like a git log graph. Its width is
    // recomputed per load once the lane count is known (see loadCommits).
    commitHeader->setSectionResizeMode(kCommitGraphCol, QHeaderView::Fixed);
    commitHeader->resizeSection(kCommitGraphCol, 24);
    commitHeader->moveSection(commitHeader->visualIndex(kCommitGraphCol), 0);
    m_commitsTable->setItemDelegateForColumn(kCommitGraphCol,
                                             new CommitGraphDelegate(m_commitsTable));
    // Freeze the Summary flex column to a draggable width once rows arrive, so
    // every column drags independently like a spreadsheet (#263); the Fixed graph
    // and action columns are left as-is.
    makeColumnsResizable(m_commitsTable);
    // Most recent first: sort by the Date column (which sorts on the raw commit
    // timestamp), matching git-log order so the graph lanes line up.
    m_commitsTable->sortByColumn(1, Qt::DescendingOrder);
    connect(m_commitsTable, &QTableWidget::cellClicked, this,
            [this](int row, int) {
                QTableWidgetItem *item = m_commitsTable->item(row, kCommitSummaryCol);
                if (item)
                    showCommit(item->data(Qt::UserRole).toString());
            });
    // Arrow-key navigation: when the current row changes (e.g. via Up/Down keys),
    // load and display the newly selected commit so the diff view stays in sync.
    connect(m_commitsTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int prevRow, int) {
                if (row == prevRow || row < 0)
                    return;
                QTableWidgetItem *item = m_commitsTable->item(row, kCommitSummaryCol);
                if (item)
                    showCommit(item->data(Qt::UserRole).toString());
            });
    // Banner above the list flagging local commits that haven't reached the
    // network mirror yet (the rows themselves are tagged in the Commit column).
    m_commitsListPage = listPage;
    m_commitsUnsyncedBanner = new QLabel(listPage);
    m_commitsUnsyncedBanner->setObjectName("statusLine");
    m_commitsUnsyncedBanner->setTextFormat(Qt::RichText);
    m_commitsUnsyncedBanner->setWordWrap(true);
    // A card background sets the note apart from the rows beneath it.
    m_commitsUnsyncedBanner->setStyleSheet(
        "#statusLine {"
        "  background-color: rgba(210,153,34,0.16);"
        "  border: 1px solid rgba(210,153,34,0.55);"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "}");
    // Fade-out animation: when the unsynced count drops to zero the note doesn't
    // blink out, it eases away (InCubic stays opaque, then drops) so it lingers
    // and stays readable a moment longer.
    m_commitsBannerOpacity = new QGraphicsOpacityEffect(m_commitsUnsyncedBanner);
    m_commitsBannerOpacity->setOpacity(1.0);
    m_commitsUnsyncedBanner->setGraphicsEffect(m_commitsBannerOpacity);
    m_commitsBannerFade = new QPropertyAnimation(m_commitsBannerOpacity,
                                                 "opacity", this);
    m_commitsBannerFade->setDuration(1500);
    m_commitsBannerFade->setEasingCurve(QEasingCurve::InCubic);
    connect(m_commitsBannerFade, &QPropertyAnimation::finished, this, [this] {
        if (m_commitsUnsyncedBanner)
            m_commitsUnsyncedBanner->hide();
    });
    m_commitsUnsyncedBanner->hide();

    // Search box: type a hash (full or abbreviated) or words from the message to
    // filter the list; clearing it shows every commit again.
    m_commitSearch = new QLineEdit;
    m_commitSearch->setObjectName("issueSearch"); // reuse the search-field styling
    m_commitSearch->setClearButtonEnabled(true);
    m_commitSearch->setPlaceholderText(
        "Search commits by hash, message, or author\xE2\x80\xA6");
    connect(m_commitSearch, &QLineEdit::textChanged, this,
            &MainWindow::filterCommits);

    // Refresh: force a full rebuild that re-checks which commits are still
    // waiting to sync. Switching away and back skips the rebuild when nothing
    // changed, so this is the explicit way to re-scan after a commit/publish.
    m_commitsRefreshButton = new QPushButton("Refresh");
    m_commitsRefreshButton->setObjectName("ghostButton");
    m_commitsRefreshButton->setCursor(Qt::PointingHandCursor);
    // Idle icon drawn by refreshPixmap (angle 0) so the spinning state is the
    // same glyph rotating, not a different icon swapping in.
    m_commitsRefreshButton->setIcon(
        QIcon(refreshPixmap(QColor(Theme::kTextTertiary), 0, 16)));
    m_commitsRefreshButton->setToolTip(
        "Reload the commit list and re-check which commits are waiting to sync");
    connect(m_commitsRefreshButton, &QPushButton::clicked, this, [this] {
        startCommitsRefreshSpin();
        // Defer the (synchronous) git + table rebuild one event-loop turn: the
        // click returns immediately so the button feels responsive and the
        // spinner paints before the reload briefly blocks the UI thread.
        QTimer::singleShot(0, this, [this] {
            loadCommits();
            // The reload is near-instant, so stop on a short delay: that lets the
            // spinner actually rotate a few frames as confirmation. The list is
            // already rebuilt and interactive by now, so this tail is feedback,
            // not blocking latency.
            QTimer::singleShot(250, this, [this] { stopCommitsRefreshSpin(); });
        });
    });

    // Turn the multi-selected commits into a shareable summary / X post.
    m_commitsGenerateButton = new QPushButton("Generate post");
    m_commitsGenerateButton->setObjectName("ghostButton");
    m_commitsGenerateButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_commitsGenerateButton, "broadcast", 16);
    m_commitsGenerateButton->setToolTip(
        "Select one or more commits (Ctrl/Shift-click), then draft a release note "
        "and an X/Twitter post from them");
    connect(m_commitsGenerateButton, &QPushButton::clicked, this,
            &MainWindow::generatePostFromSelectedCommits);

    // Current-branch indicator + switcher: shows the checked-out branch and opens
    // a dropdown to check out another branch (or create one), like a git client.
    m_commitsBranchButton = new QPushButton("main");
    m_commitsBranchButton->setObjectName("ghostButton");
    m_commitsBranchButton->setCursor(Qt::PointingHandCursor);
    m_commitsBranchButton->setToolTip(
        "Branch shown below — click to browse another branch's history or create one");
    setOcticon(m_commitsBranchButton, "git-branch", 16);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    searchRow->addWidget(m_commitsBranchButton);
    searchRow->addWidget(m_commitSearch, 1);
    searchRow->addWidget(m_commitsGenerateButton);
    searchRow->addWidget(m_commitsRefreshButton);

    // Infinite scroll: when the list reaches the bottom and older history remains,
    // deepen the window and rebuild (loadMoreCommits preserves the scroll spot).
    connect(m_commitsTable->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int value) {
                if (!m_commitsTable)
                    return;
                QScrollBar *sb = m_commitsTable->verticalScrollBar();
                if (m_commitsHasMore && !m_commitsLoadingMore && sb->maximum() > 0 &&
                    value >= sb->maximum() - 2)
                    QTimer::singleShot(0, this, [this] { loadMoreCommits(); });
            });

    auto *listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(16, 12, 16, 16);
    // The unsynced banner sits in normal flow between the search row and the
    // table: as a real laid-out widget it pushes the rows down instead of
    // floating over them, so it can never hide the very (newest, top) commits it
    // flags. When hidden it collapses to zero height and the table reclaims it.
    listLayout->addLayout(searchRow);
    listLayout->addWidget(m_commitsUnsyncedBanner);
    listLayout->addWidget(m_commitsTable);

    // --- Page 1: the GitHub-style commit diff view.
    auto *detailPage = new QWidget;

    auto *backButton = new QPushButton("Commits");
    backButton->setObjectName("ghostButton");
    backButton->setCursor(Qt::PointingHandCursor);
    setOcticon(backButton, "arrow-left", 16);
    connect(backButton, &QPushButton::clicked, this, &MainWindow::showCommitList);

    // Prev/Next walk the commit list (newest first): Prev = newer, Next = older.
    m_commitPrevButton = new QPushButton("Prev");
    m_commitNextButton = new QPushButton("Next");
    for (QPushButton *b : {m_commitPrevButton, m_commitNextButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_commitPrevButton, "chevron-down", 16);
    setOcticon(m_commitNextButton, "chevron-right", 16);
    m_commitPrevButton->setToolTip("Show the previous (newer) commit");
    m_commitNextButton->setToolTip("Show the next (older) commit");
    auto goToCommitRow = [this](int row) {
        if (!m_commitsTable || row < 0 || row >= m_commitsTable->rowCount())
            return;
        QTableWidgetItem *it = m_commitsTable->item(row, kCommitSummaryCol);
        if (it)
            showCommit(it->data(Qt::UserRole).toString());
    };
    connect(m_commitPrevButton, &QPushButton::clicked, this,
            [this, goToCommitRow] { goToCommitRow(m_currentCommitRow - 1); });
    connect(m_commitNextButton, &QPushButton::clicked, this,
            [this, goToCommitRow] { goToCommitRow(m_currentCommitRow + 1); });

    m_commitTitle = new QLabel;
    m_commitTitle->setObjectName("repoHeaderTitle");
    m_commitTitle->setTextFormat(Qt::RichText);
    m_commitTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // Copy a forkmesh:// permalink to this commit (issue #154): pasted into a
    // comment it renders as a link back here via autolinkReferences().
    auto *commitCopyLinkButton = new QPushButton("Copy link");
    commitCopyLinkButton->setObjectName("ghostButton");
    commitCopyLinkButton->setCursor(Qt::PointingHandCursor);
    commitCopyLinkButton->setToolTip(
        "Copy a link to this commit you can paste into an issue or PR comment");
    setOcticon(commitCopyLinkButton, "copy", 16);
    connect(commitCopyLinkButton, &QPushButton::clicked, this, [this] {
        copyReferenceLink(QStringLiteral("commit"), m_currentCommitHash);
    });

    m_commitDownloadButton = new QPushButton("Download patch");
    m_commitDownloadButton->setObjectName("ghostButton");
    m_commitDownloadButton->setCursor(Qt::PointingHandCursor);
    m_commitDownloadButton->setToolTip(
        "Save this commit as a .patch file you can re-import as a pull request");
    setOcticon(m_commitDownloadButton, "download", 16);
    connect(m_commitDownloadButton, &QPushButton::clicked, this,
            &MainWindow::downloadCommitPatch);

    // Drop the shown commit from history (same rewrite as the per-row button in
    // the list, but reachable from the diff view). Enabled only where there's a
    // working tree to rewrite; showCommit keeps that in sync.
    m_commitDeleteButton = new QPushButton("Delete commit");
    m_commitDeleteButton->setObjectName("ghostButton");
    m_commitDeleteButton->setCursor(Qt::PointingHandCursor);
    m_commitDeleteButton->setToolTip(
        "Remove this commit from history (rewrites the branch and replays the "
        "later commits onto its parent)");
    setOcticon(m_commitDeleteButton, "trash", 16);
    connect(m_commitDeleteButton, &QPushButton::clicked, this,
            [this] { deleteCommit(m_currentCommitHash); });

    // Undo the shown commit without rewriting history: record a new commit that
    // reverses its changes (git revert). Like delete, needs a working tree to
    // commit into; showCommit keeps the enabled state in sync.
    m_commitRevertButton = new QPushButton("Restore commit");
    m_commitRevertButton->setObjectName("ghostButton");
    m_commitRevertButton->setCursor(Qt::PointingHandCursor);
    m_commitRevertButton->setToolTip(
        "Undo this commit by committing the reverse of its changes (history is "
        "kept)");
    setOcticon(m_commitRevertButton, "history", 16);
    connect(m_commitRevertButton, &QPushButton::clicked, this,
            [this] { revertCommit(m_currentCommitHash); });

    // Switch between unified and side-by-side (split) diff rendering. The choice
    // is a shared, persisted preference (see diffSplitPref) used by both the
    // commit and pull-request diff views.
    m_commitSplitButton = new QPushButton;
    m_commitSplitButton->setObjectName("ghostButton");
    m_commitSplitButton->setCursor(Qt::PointingHandCursor);
    m_commitSplitButton->setCheckable(true);
    m_commitSplitButton->setChecked(diffSplitPref());
    setOcticon(m_commitSplitButton, "diff", 16);
    updateDiffSplitButton(m_commitSplitButton);
    connect(m_commitSplitButton, &QPushButton::clicked, this, [this](bool on) {
        setDiffSplitPref(on);
        updateDiffSplitButton(m_commitSplitButton);
        updateDiffSplitButton(m_pullSplitButton);
        if (m_pullSplitButton)
            m_pullSplitButton->setChecked(on);
        if (!m_currentCommitHash.isEmpty())
            showCommit(m_currentCommitHash);
    });

    auto *navCol = new QVBoxLayout;
    navCol->setContentsMargins(0, 0, 0, 0);
    navCol->setSpacing(4);
    navCol->addWidget(backButton, 0, Qt::AlignRight);
    auto *prevNextRow = new QHBoxLayout;
    prevNextRow->setContentsMargins(0, 0, 0, 0);
    prevNextRow->setSpacing(4);
    prevNextRow->addStretch();
    prevNextRow->addWidget(m_commitSplitButton);
    prevNextRow->addWidget(commitCopyLinkButton);
    prevNextRow->addWidget(m_commitDownloadButton);
    prevNextRow->addWidget(m_commitDeleteButton);
    prevNextRow->addWidget(m_commitRevertButton);
    prevNextRow->addWidget(m_commitPrevButton);
    prevNextRow->addWidget(m_commitNextButton);
    navCol->addLayout(prevNextRow);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->addWidget(m_commitTitle, 1, Qt::AlignTop);
    headerRow->addLayout(navCol);

    m_commitMessage = new QLabel;
    m_commitMessage->setObjectName("commitMessage");
    m_commitMessage->setWordWrap(true);
    m_commitMessage->setTextFormat(Qt::RichText);
    m_commitMessage->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                             Qt::LinksAccessibleByMouse);
    // "#123" references in the message open issues/PRs (ref:), and bare commit
    // SHAs open that commit (commit:) so a message referencing another commit is
    // clickable everywhere a commit is shown. PR/comment bodies use typed
    // Markdown reference links.
    connect(m_commitMessage, &QLabel::linkActivated, this,
            [this](const QString &href) {
                if (href.startsWith(QStringLiteral("ref:")))
                    openCommitReference(href.mid(4).toInt());
                else if (href.startsWith(QStringLiteral("commit:")))
                    openCommitHashReference(href.mid(7));
            });

    m_commitMeta = new QLabel;
    m_commitMeta->setObjectName("statusLine");
    m_commitMeta->setTextFormat(Qt::RichText);
    m_commitMeta->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_commitFilesSummary = new QLabel;
    m_commitFilesSummary->setObjectName("sectionLabel");
    m_commitFilesSummary->setTextFormat(Qt::RichText);

    // Left: changed-files list (click to scroll the diff to that file).
    auto *filesPane = new QWidget;
    filesPane->setMinimumWidth(200);
    filesPane->setMaximumWidth(300);
    m_commitFileList = new QListWidget;
    m_commitFileList->setObjectName("commitFileList");
    connect(m_commitFileList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item && m_commitDiffView)
                    m_commitDiffView->scrollToAnchor(
                        item->data(Qt::UserRole).toString());
            });
    // Small spinner that sits just after the "N files changed" heading while
    // showCommit reads + renders the diff, so a slow commit shows progress here
    // instead of freezing. Hidden until a load starts.
    m_commitDiffSpinner = new BusySpinner(filesPane);
    m_commitDiffSpinner->setToolTip(QString::fromUtf8("Loading diff\xE2\x80\xA6"));
    m_commitDiffSpinner->hide();
    auto *filesSummaryRow = new QHBoxLayout;
    filesSummaryRow->setContentsMargins(0, 0, 0, 0);
    filesSummaryRow->setSpacing(6);
    filesSummaryRow->addWidget(m_commitFilesSummary);
    filesSummaryRow->addWidget(m_commitDiffSpinner);
    filesSummaryRow->addStretch();

    auto *filesLayout = new QVBoxLayout(filesPane);
    filesLayout->setContentsMargins(0, 0, 8, 0);
    filesLayout->setSpacing(6);
    filesLayout->addLayout(filesSummaryRow);
    filesLayout->addWidget(m_commitFileList, 1);

    // Right: the unified diff for the whole commit.
    m_commitDiffView = new QTextBrowser;
    m_commitDiffView->setObjectName("commitDiffView");
    m_commitDiffView->setOpenExternalLinks(false);
    registerDiffView(m_commitDiffView);

    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(filesPane);
    split->addWidget(m_commitDiffView);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);

    // --- Per-commit conversation: comment thread + composer.
    m_commitThreadContainer = new QWidget;
    m_commitThreadLayout = new QVBoxLayout(m_commitThreadContainer);
    m_commitThreadLayout->setContentsMargins(0, 0, 0, 0);
    m_commitThreadLayout->setSpacing(10);
    m_commitThreadLayout->addStretch();
    auto *commitThreadScroll = new QScrollArea;
    commitThreadScroll->setWidgetResizable(true);
    commitThreadScroll->setWidget(m_commitThreadContainer);
    commitThreadScroll->setObjectName("issuePageScroll");
    commitThreadScroll->setFrameShape(QFrame::NoFrame);
    m_commitComposer = new MarkdownEditor;
    m_commitComposer->setPlaceholderText("Leave a comment on this commit\xE2\x80\xA6");
    m_commitComposer->setMinimumHeight(80);
    m_commitCommentButton = new QPushButton("Comment");
    m_commitCommentButton->setObjectName("ghostButton");
    m_commitCommentButton->setProperty("buttonSize", "sm");
    m_commitCommentButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_commitCommentButton, "comment", 16);
    connect(m_commitCommentButton, &QPushButton::clicked, this,
            &MainWindow::submitCommitComment);
    auto *commitComposerButtons = new QHBoxLayout;
    commitComposerButtons->setContentsMargins(0, 0, 0, 0);
    // Speak the comment with the voice engine, just like the footer prompt mic.
    commitComposerButtons->addWidget(makeVoiceButton(m_commitComposer), 0, Qt::AlignLeft);
    commitComposerButtons->addStretch();
    commitComposerButtons->addWidget(m_commitCommentButton);
    auto *commitConversation = new QWidget;
    auto *commitConversationLayout = new QVBoxLayout(commitConversation);
    commitConversationLayout->setContentsMargins(0, 0, 0, 0);
    commitConversationLayout->setSpacing(8);
    commitConversationLayout->addWidget(commitThreadScroll, 1);
    commitConversationLayout->addWidget(m_commitComposer);
    commitConversationLayout->addLayout(commitComposerButtons);

    auto *commitVSplit = new QSplitter(Qt::Vertical);
    commitVSplit->setChildrenCollapsible(false);
    commitVSplit->addWidget(split);
    commitVSplit->addWidget(commitConversation);
    commitVSplit->setStretchFactor(0, 3);
    commitVSplit->setStretchFactor(1, 2);
    commitVSplit->setSizes({440, 240});

    auto *detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(16, 12, 16, 16);
    detailLayout->setSpacing(8);
    detailLayout->addLayout(headerRow);
    detailLayout->addWidget(m_commitMessage);
    detailLayout->addWidget(m_commitMeta);
    detailLayout->addWidget(commitVSplit, 1);

    // Right side: a placeholder until a commit is picked, then the diff view.
    // The commit list (listPage) stays visible in the left splitter pane the
    // whole time, so clicking a commit no longer hides it.
    auto *placeholder = new QLabel("Select a commit to view its diff.");
    placeholder->setObjectName("statusLine");
    placeholder->setAlignment(Qt::AlignCenter);
    m_commitsStack->addWidget(placeholder); // 0
    m_commitsStack->addWidget(detailPage);  // 1

    auto *outerSplit = new QSplitter(Qt::Horizontal);
    outerSplit->addWidget(listPage);
    outerSplit->addWidget(m_commitsStack);
    // #123: open the commit list to half the window width on first load so its
    // columns aren't clipped (the right pane is just a placeholder until a commit
    // is selected). Equal stretch factors keep it ~50/50 at any window width — a
    // zero stretch on the list otherwise handed all the extra space on a wide
    // window to the placeholder and left the list clipped. Still draggable.
    outerSplit->setStretchFactor(0, 1);
    outerSplit->setStretchFactor(1, 1);
    outerSplit->setSizes({1000, 1000});

    // New top panel: a VSCode-style Source Control view for the working tree
    // (compose strip + changes tree + diff), sitting above the committed-history
    // UI (list | diff) in a vertical split.
    auto *scmPanel = buildSourceControlPanel();
    auto *commitsVSplit = new QSplitter(Qt::Vertical);
    commitsVSplit->setChildrenCollapsible(false);
    commitsVSplit->addWidget(scmPanel);
    commitsVSplit->addWidget(outerSplit);
    commitsVSplit->setStretchFactor(0, 2);
    commitsVSplit->setStretchFactor(1, 3);
    commitsVSplit->setSizes({320, 520});

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(commitsVSplit);
    return page;
}



// ---- Repo-detail loading / file-search infra (moved from MainWindowReleases) ----

void MainWindow::loadFileSearchIndex()
{
    if (!m_fileCompleter)
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        m_fileCompleter->setModel(new QStringListModel(QStringList(), m_fileCompleter));
        return;
    }
    // A whole-tree ls-tree -r is the heaviest single git read on a large repo
    // (see the singleShot call site in openRepoDetail) — StallWatchdog caught it
    // freezing the GUI thread even though the caller already ran it under a
    // GitKeepAlive scope. That scope only pumps the event loop between polls; it
    // doesn't bound how long any *one* pumped event (a paint, a text layout
    // elsewhere in the app) can take, so a slow unrelated repaint landing mid-poll
    // still stalled the frame. runGitDetached avoids the blocking wait entirely.
    const int forIndex = m_repoDetailIndex;
    runGitDetached(dir, {"ls-tree", "-r", "--name-only", "-z", currentRef()},
                   [this, forIndex](bool ok, const QByteArray &out) {
                       if (forIndex != m_repoDetailIndex || !m_fileCompleter)
                           return;
                       QStringList paths;
                       if (ok) {
                           for (const QByteArray &record : out.split('\0'))
                               if (!record.isEmpty())
                                   paths << QString::fromUtf8(record);
                       }
                       m_fileCompleter->setModel(
                           new QStringListModel(paths, m_fileCompleter));
                   });
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

    // Total tracked file count for the current ref.
    if (m_filesCountHeader && m_filesCountRow) {
        int fileCount = 0;
        QByteArray out;
        if (!dir.isEmpty() &&
            runGitCapture(dir, {"ls-tree", "-r", "--name-only", currentRef()}, &out,
                          nullptr)) {
            for (const QByteArray &line : out.split('\n')) {
                if (!line.trimmed().isEmpty())
                    ++fileCount;
            }
        }
        m_filesCountRow->setText(
            QStringLiteral("<span style='color:#c9d1d9'>%1</span> "
                            "<span style='color:#8b949e'>%2</span>")
                .arg(formatCount(fileCount), fileCount == 1 ? "file" : "files"));
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
