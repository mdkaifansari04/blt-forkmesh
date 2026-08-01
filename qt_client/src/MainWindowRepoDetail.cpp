








#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "CurrentPageStack.h"
#include "KebabHeaderView.h"

#include <QSignalBlocker>

using namespace forkmesh::ui;






static QString ideRegistryDir()
{
    return QDir::homePath() + QStringLiteral("/.forkmesh/ide");
}



bool MainWindow::ideExtensionActive(QString *ideName) const
{
    QFile f(ideRegistryDir() + QStringLiteral("/registration.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    const qint64 ts = static_cast<qint64>(obj.value("ts").toDouble());
    if (QDateTime::currentMSecsSinceEpoch() - ts > 90'000)
        return false;
    if (ideName)
        *ideName = obj.value("ide").toString(QStringLiteral("your IDE"));
    return true;
}


bool MainWindow::ideIntegrationReady(QString *ideName) const
{
    if (!QSettings().value(kIdeIntegrationSetting, false).toBool())
        return false;
    return ideExtensionActive(ideName);
}


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
                                      QStringLiteral("Changes in Git"));
    };

    if (issue.number <= 0) {
        hideTab();
        return;
    }



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
            item->setData(Qt::UserRole, abs);
            item->setData(Qt::UserRole + 1, f.anchor);
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
            n > 0 ? QStringLiteral("Changes in Git (%1)").arg(n)
                  : QStringLiteral("Changes in Git"));
    if (m_issueFilesChangedSummary)
        m_issueFilesChangedSummary->setText(
            QStringLiteral("%1 file%2 changed").arg(n).arg(n == 1 ? "" : "s"));
}

namespace {
constexpr int kIssueFilesColumn = 15;
constexpr int kAttachmentIconSize = 14;
constexpr int kAttachmentIconGap = 2;
constexpr int kMaxIssueListAttachmentIcons = 8;

QStringList visibleIssueAttachments(const Issue &issue)
{
    QHash<QString, IssueEvent> edits;
    QSet<QString> deleted;
    for (const IssueEvent &ev : issue.events) {
        if (ev.type == QLatin1String("edit") && !ev.target.isEmpty())
            edits.insert(ev.target, ev);
        else if (ev.type == QLatin1String("delete") && !ev.target.isEmpty() &&
                 ev.target != QLatin1String("self"))
            deleted.insert(ev.target);
    }

    QStringList files;
    QSet<QString> seen;
    auto append = [&](const QString &rel) {
        const QString clean = rel.trimmed();
        if (clean.isEmpty() || seen.contains(clean))
            return;
        seen.insert(clean);
        files << clean;
    };

    for (const IssueEvent &ev : issue.events) {
        if (ev.type != QLatin1String("open") && ev.type != QLatin1String("comment"))
            continue;
        if (ev.type == QLatin1String("comment") && deleted.contains(ev.id))
            continue;
        const IssueEvent shown = edits.value(ev.id, ev);
        for (const QString &rel : shown.attachments)
            append(rel);
    }
    return files;
}

QString issueAttachmentToolTip(const QStringList &attachments)
{
    if (attachments.isEmpty())
        return QString();
    QStringList lines;
    lines << QStringLiteral("Attached files:");
    const int shown = qMin(attachments.size(), 20);
    for (int i = 0; i < shown; ++i)
        lines << QStringLiteral("- %1").arg(attachments.at(i));
    if (attachments.size() > shown)
        lines << QStringLiteral("- ... and %1 more").arg(attachments.size() - shown);
    return lines.join(QLatin1Char('\n'));
}

QWidget *makeIssueAttachmentStrip(const QStringList &attachments, QWidget *parent)
{
    auto *strip = new QWidget(parent);
    strip->setAttribute(Qt::WA_TransparentForMouseEvents);
    strip->setToolTip(issueAttachmentToolTip(attachments));
    strip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto *layout = new QHBoxLayout(strip);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(kAttachmentIconGap);
    layout->setAlignment(Qt::AlignCenter);

    const QPixmap icon =
        tintedOcticonPixmap(QStringLiteral("file"), QColor("#58a6ff"),
                            kAttachmentIconSize);
    const int shown = qMin(attachments.size(), kMaxIssueListAttachmentIcons);
    for (int i = 0; i < shown; ++i) {
        auto *label = new QLabel(strip);
        label->setFixedSize(kAttachmentIconSize, kAttachmentIconSize);
        label->setPixmap(icon);
        label->setToolTip(attachments.at(i));
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(label);
    }
    if (attachments.size() > shown) {
        auto *more =
            new QLabel(QStringLiteral("+%1").arg(attachments.size() - shown), strip);
        more->setStyleSheet(QStringLiteral(
            "color:#8b949e; background:transparent; font-size:11px;"));
        more->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(more);
    }
    return strip;
}

int issueAttachmentCellWidth(int count)
{
    if (count <= 0)
        return 24;
    const int shown = qMin(count, kMaxIssueListAttachmentIcons);
    int width = 8 + shown * kAttachmentIconSize +
                qMax(0, shown - 1) * kAttachmentIconGap;
    if (count > shown)
        width += 28;
    return width;
}
}

void MainWindow::populateIssueFilesCell(int row, const Issue &issue)
{
    if (!m_issueTable)
        return;

    const QStringList attachments = visibleIssueAttachments(issue);
    const int count = attachments.size();
    auto *item = new SortTableWidgetItem(count > 0 ? QString::number(count)
                                                   : QString());
    item->setTextAlignment(Qt::AlignCenter);
    item->setData(kTableSortRole, count);
    item->setToolTip(issueAttachmentToolTip(attachments));
    item->setSizeHint(QSize(issueAttachmentCellWidth(count), 20));
    m_issueTable->setItem(row, kIssueFilesColumn, item);
    if (count > 0)
        m_issueTable->setCellWidget(
            row, kIssueFilesColumn,
            makeIssueAttachmentStrip(attachments, m_issueTable));
}

QWidget *MainWindow::buildRepoFilesPanel()
{


    m_filesStack = new CurrentPageStack;
    m_filesStack->addWidget(buildRepoOverviewPage());
    m_filesStack->addWidget(buildRepoEditorPage());
    m_filesStack->addWidget(buildRepoCoveExplorerPage());

    // The three mode toggles take the same icon-over-caption form as the
    // activity rail and the repo tabs (adhoc #6), so the whole mode row —
    // toggles, toolbar counts and trends — reads as one line.
    m_filesModeOverviewButton =
        new VerticalIconButton("Code overview", VerticalIconButton::Tab);
    m_filesModeOverviewButton->setObjectName("repoTab");
    m_filesModeOverviewButton->setCheckable(true);
    m_filesModeOverviewButton->setChecked(true);
    m_filesModeOverviewButton->setCursor(Qt::PointingHandCursor);
    m_filesModeOverviewButton->setToolTip(
        "Show the repository overview (latest commit, file list and README)");
    setOcticon(m_filesModeOverviewButton, "code", 16);
    connect(m_filesModeOverviewButton, &QPushButton::clicked, this,
            [this] { showRepoOverview(); });

    m_filesModeExplorerButton =
        new VerticalIconButton("Explorer", VerticalIconButton::Tab);
    m_filesModeExplorerButton->setObjectName("repoTab");
    m_filesModeExplorerButton->setCheckable(true);
    m_filesModeExplorerButton->setCursor(Qt::PointingHandCursor);
    m_filesModeExplorerButton->setToolTip(
        "Open the file explorer and code editor");
    setOcticon(m_filesModeExplorerButton, "file-directory", 16);
    connect(m_filesModeExplorerButton, &QPushButton::clicked, this,
            [this] { showRepoEditor(); });

    m_filesModeCoveExplorerButton =
        new VerticalIconButton("Cove Explorer", VerticalIconButton::Tab);
    m_filesModeCoveExplorerButton->setObjectName("repoTab");
    m_filesModeCoveExplorerButton->setCheckable(true);
    m_filesModeCoveExplorerButton->setCursor(Qt::PointingHandCursor);
    m_filesModeCoveExplorerButton->setToolTip(
        "Open account-invited secure cove files");
    setOcticon(m_filesModeCoveExplorerButton, "shield-check", 16);
    connect(m_filesModeCoveExplorerButton, &QPushButton::clicked, this,
            [this] { showRepoCoveExplorer(); });

    // Thirty-day repository trends and the Ratchet mode toggle, moved down
    // from the window-chrome line onto this row (adhoc #6). Each sparkline
    // point represents a day; they sit deliberately a little larger than the
    // chrome's live host-resource squares.
    auto *repoSizeChart =
        new ResourceSparkline(QStringLiteral("SIZE"), nullptr, 44, 30);
    auto *repoLinesChart =
        new ResourceSparkline(QStringLiteral("LOC"), nullptr, 44, 30);
    auto *repoFilesChart =
        new ResourceSparkline(QStringLiteral("FILES"), nullptr, 44, 30);
    repoSizeChart->setObjectName(QStringLiteral("repoSizeChart"));
    repoLinesChart->setObjectName(QStringLiteral("repoLinesChart"));
    repoFilesChart->setObjectName(QStringLiteral("repoFilesChart"));
    m_repoSizeChart = repoSizeChart;
    m_repoLinesChart = repoLinesChart;
    m_repoFilesChart = repoFilesChart;
    m_repoRatchetButton = new QToolButton;
    m_repoRatchetButton->setObjectName(QStringLiteral("repoRatchetButton"));
    m_repoRatchetButton->setText(QStringLiteral("Ratchet mode"));
    m_repoRatchetButton->setCheckable(true);
    m_repoRatchetButton->setToolTip(
        QStringLiteral("Keep each commit at or below today's tracked size and require "
                       "at least as many removed lines as added lines."));
    connect(m_repoRatchetButton, &QToolButton::toggled, this,
            &MainWindow::toggleRepositoryRatchet);

    // The git identity that used to sit beside these mode toggles now lives in
    // the bottom status bar (see buildStatusBar). One line holds the mode
    // toggles, the branch/worktree/remote/commit/tag/release counts (moved up
    // from the overview page), the go-to-file box and the repo trends
    // (adhoc #6).
    m_repoFilesModeBar = new QWidget;
    auto *modeRow = new QHBoxLayout(m_repoFilesModeBar);
    modeRow->setContentsMargins(16, 6, 16, 0);
    modeRow->setSpacing(2);
    modeRow->addWidget(m_filesModeOverviewButton);
    modeRow->addWidget(m_filesModeExplorerButton);
    modeRow->addWidget(m_filesModeCoveExplorerButton);
    modeRow->addSpacing(10);
    modeRow->addWidget(m_branchesButton);
    modeRow->addWidget(m_worktreesButton);
    modeRow->addWidget(m_remotesButton);
    // No toolbar Commits button here: it duplicated the commit strip's own
    // "N Commits" toggle and was retired on main (aa108e5cf).
    modeRow->addWidget(m_tagsButton);
    // Releases (adhoc #180): moved out of the top tab bar to sit beside Tags.
    // The button itself is created with the other repo tabs in
    // buildRepoDetailSection (kept in m_repoDetailTabs so tab switching and
    // the Releases badge keep working) — here we just place it in the row.
    if (m_repoReleasesTab)
        modeRow->addWidget(m_repoReleasesTab);
    modeRow->addSpacing(10);
    modeRow->addWidget(m_fileSearch, 1);
    modeRow->addWidget(repoSizeChart);
    modeRow->addWidget(repoLinesChart);
    modeRow->addWidget(repoFilesChart);
    modeRow->addWidget(m_repoRatchetButton);

    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_repoFilesModeBar);
    layout->addWidget(m_filesStack, 1);
    // The trend charts were born just now (this panel builds lazily), so seed
    // them immediately instead of waiting for the next 60s footer tick.
    refreshRepositoryStats();
    return panel;
}

QWidget *MainWindow::buildRepoOverviewPage()
{
    auto *page = new QWidget;

    // Latest-commit bar. Commit history has one entry point: the Git activity
    // rail, so the Code overview cannot open a second copy of the workspace.
    auto *commitCard = new QWidget;
    commitCard->setObjectName("commitBar");
    m_commitBar = new QLabel;
    m_commitBar->setObjectName("commitBarText");
    m_commitBar->setTextFormat(Qt::RichText);
    m_commitBar->setWordWrap(true);
    auto *commitRow = new QHBoxLayout(commitCard);
    commitRow->setContentsMargins(12, 8, 8, 8);
    commitRow->addWidget(m_commitBar, 1);

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
    m_overviewList->setSortingEnabled(false);
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



    m_overviewList->header()->setSectionsClickable(true);
    m_overviewList->header()->setSortIndicatorShown(true);
    m_overviewList->header()->setSortIndicator(0, Qt::AscendingOrder);
    connect(m_overviewList->header(), &QHeaderView::sortIndicatorChanged, this,
            [this](int col, Qt::SortOrder order) {

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
                    loadRepoOverview(path);
                else if (!path.isEmpty())
                    openRepoFile(path);
            });

    m_readmeView = new QTextBrowser;
    m_readmeView->setObjectName("readmeView");
    m_readmeView->setOpenExternalLinks(true);

    // Toolbar: branch counts + tags + "go to file" search, every button the
    // same icon-over-caption form as the activity rail with its count as a
    // corner badge (adhoc #6). The buttons are created here but placed on the
    // Code overview's mode row, beside the Code overview / Explorer / Cove
    // Explorer toggles (see buildRepoFilesPanel). The branch switcher itself
    // moved to the bottom status bar (see buildStatusBar).
    m_branchesButton =
        new VerticalIconButton("Branches", VerticalIconButton::Tab);
    m_branchesButton->setCursor(Qt::PointingHandCursor);
    m_branchesButton->setToolTip(
        "Open the Branches panel to manage branches \xE2\x80\x94 click one to "
        "open it in the Git view, where its history and its diff against the "
        "base branch show side by side");
    setOcticon(m_branchesButton, "git-branch", 16);
    connect(m_branchesButton, &QPushButton::clicked, this, [this] {
        // Branches has no top-level tab anymore: its panel lives inside the Code
        // overview under this toolbar button.
        showOverviewBranches();
        loadBranchesPanel();
    });
    m_worktreesButton =
        new VerticalIconButton("Worktrees", VerticalIconButton::Tab);
    m_worktreesButton->setCursor(Qt::PointingHandCursor);
    m_worktreesButton->setToolTip(
        "Open the Worktrees panel to view agent checkouts and their changes");
    setOcticon(m_worktreesButton, "file-directory", 16);
    connect(m_worktreesButton, &QPushButton::clicked, this, [this] {



        showOverviewWorktrees();
        loadWorktreesPanel();
    });
    m_remotesButton =
        new VerticalIconButton("Remotes", VerticalIconButton::Tab);
    m_remotesButton->setCursor(Qt::PointingHandCursor);
    m_remotesButton->setToolTip(
        "List this repository's git remotes; pick one to copy its URL");
    setOcticon(m_remotesButton, "server", 16);
    // The menu itself is rebuilt with the remote list in loadBranchesAndTags,
    // alongside the branch menu and the branches/worktrees counts.
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


    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);
    toolbar->addWidget(m_branchesButton);
    toolbar->addWidget(m_worktreesButton);
    toolbar->addWidget(m_remotesButton);
    toolbar->addWidget(m_tagsButton);





    if (m_repoReleasesTab)
        toolbar->addWidget(m_repoReleasesTab);
    toolbar->addWidget(m_fileSearch, 1);

    // This internal stack shares the Code layout, but its Git workspace page is
    // reachable only through the activity rail.
    auto *filesBody = new QWidget;
    auto *filesBodyLayout = new QVBoxLayout(filesBody);
    filesBodyLayout->setContentsMargins(0, 0, 0, 0);
    filesBodyLayout->setSpacing(8);
    filesBodyLayout->addWidget(m_overviewCrumb);
    filesBodyLayout->addWidget(m_overviewList, 2);
    filesBodyLayout->addWidget(m_readmeView, 3);

    m_overviewBodyStack = new CurrentPageStack;
    m_overviewBodyStack->addWidget(filesBody);
    m_overviewBodyStack->addWidget(buildRepoCommitsTab());
    m_overviewBodyStack->addWidget(buildBranchesTab());
    m_overviewBodyStack->addWidget(buildWorktreesTab());



    m_overviewBodyStack->setMinimumHeight(0);
    m_overviewBodyStack->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Ignored);

    // Left column: latest commit, then the swappable body. The commit strip
    // lives in its own wrapper so the activity-rail Git view can remove the
    // Code-only upper section and give its commits/changes workspace the full
    // available height. (The toolbar that used to sit above it moved onto the
    // mode row, adhoc #6.)
    m_repoOverviewChrome = new QWidget;
    auto *overviewChromeLayout = new QVBoxLayout(m_repoOverviewChrome);
    overviewChromeLayout->setContentsMargins(0, 0, 0, 0);
    overviewChromeLayout->setSpacing(8);
    overviewChromeLayout->addWidget(commitCard);

    auto *leftColumn = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(m_repoOverviewChrome);
    leftLayout->addWidget(m_overviewBodyStack, 1);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 10, 16, 16);
    layout->setSpacing(8);
    layout->addWidget(leftColumn);
    return page;
}

QWidget *MainWindow::buildRepoEditorPage()
{
    auto *page = new QWidget;

    auto *backRow = new QHBoxLayout;
    backRow->setContentsMargins(8, 4, 8, 0);


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

    m_repoFileTree->setColumnCount(2);
    m_repoFileTree->setHeaderHidden(true);
    m_repoFileTree->header()->setStretchLastSection(false);
    m_repoFileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_repoFileTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_repoFileTree->setMinimumWidth(200);
    m_repoFileTree->setIndentation(14);


    m_repoFileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_repoFileTree, &QWidget::customContextMenuRequested, this,
            &MainWindow::showRepoFileTreeMenu);


    connect(m_repoFileTree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                if (item->data(0, Qt::UserRole + 1).toBool())
                    item->setExpanded(!item->isExpanded());
                else
                    openRepoFile(item->data(0, Qt::UserRole).toString());
            });

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

        if (m_repoFileTabs->count() == 0)
            showRepoOverview();
        updateRepoFileSaveActions();
    });
    connect(m_repoFileTabs, &QTabWidget::currentChanged, this,
            [this] { updateRepoFileSaveActions(); });


    auto *saveShortcut = new QShortcut(QKeySequence::Save, m_repoFileTabs);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] {
        if (m_repoFileCommitButton && m_repoFileCommitButton->isEnabled())
            saveCurrentRepoFile(false);
        else if (m_repoFilePullButton && m_repoFilePullButton->isEnabled())
            saveCurrentRepoFile(true);
    });

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

static bool coveDocPathIsSafe(const QString &path)
{
    const QString clean = QDir::cleanPath(path.trimmed());
    return !clean.isEmpty() && clean != QLatin1String(".") &&
           !QDir::isAbsolutePath(clean) && !clean.startsWith("../") &&
           !clean.contains("/../") && clean != QLatin1String(".git") &&
           !clean.startsWith(".git/");
}

static QString coveDocMimeForPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown"))
        return QStringLiteral("text/markdown");
    if (suffix == QLatin1String("json") || suffix == QLatin1String("jsonc"))
        return QStringLiteral("application/json");
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        return QStringLiteral("text/html");
    return QStringLiteral("text/plain");
}

static QString coveDocTabKey(const QString &coveId, const QString &path)
{
    return coveId + QChar(0x1f) + path;
}

QWidget *MainWindow::buildRepoCoveExplorerPage()
{
    auto *page = new QWidget;

    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(8, 4, 8, 4);
    toolbar->setSpacing(6);

    m_coveExplorerStatus = new QLabel;
    m_coveExplorerStatus->setObjectName("statusLine");
    m_coveExplorerStatus->setText(QStringLiteral("Cove Explorer"));
    m_coveExplorerStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_coveExplorerSelector = new QComboBox;
    m_coveExplorerSelector->setObjectName("repoPicker");
    m_coveExplorerSelector->setMinimumWidth(220);
    m_coveExplorerSelector->setToolTip(
        QStringLiteral("Account-invited coves in this repository"));
    connect(m_coveExplorerSelector,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                m_coveExplorerCurrentId =
                    m_coveExplorerSelector
                        ? m_coveExplorerSelector->currentData().toString()
                        : QString();
                refreshCoveExplorerTree();
            });

    m_coveExplorerNewButton = new QPushButton("New cove");
    m_coveExplorerNewButton->setObjectName("ghostButton");
    m_coveExplorerNewButton->setCursor(Qt::PointingHandCursor);
    m_coveExplorerNewButton->setToolTip(
        QStringLiteral("Create an account-scoped cove in this repo"));
    setOcticon(m_coveExplorerNewButton, "plus", 16);
    connect(m_coveExplorerNewButton, &QPushButton::clicked, this,
            &MainWindow::createCoveExplorerCove);

    m_coveExplorerInviteButton = new QPushButton("Invite");
    m_coveExplorerInviteButton->setObjectName("ghostButton");
    m_coveExplorerInviteButton->setCursor(Qt::PointingHandCursor);
    m_coveExplorerInviteButton->setToolTip(
        QStringLiteral("Invite a ForkMesh account to this cove"));
    setOcticon(m_coveExplorerInviteButton, "person", 16);
    connect(m_coveExplorerInviteButton, &QPushButton::clicked, this,
            &MainWindow::inviteUserToCurrentCove);

    m_coveExplorerNewFileButton = new QPushButton("New file");
    m_coveExplorerNewFileButton->setObjectName("ghostButton");
    m_coveExplorerNewFileButton->setCursor(Qt::PointingHandCursor);
    m_coveExplorerNewFileButton->setToolTip(
        QStringLiteral("Add a document to the selected cove"));
    setOcticon(m_coveExplorerNewFileButton, "file", 16);
    connect(m_coveExplorerNewFileButton, &QPushButton::clicked, this,
            &MainWindow::createCoveExplorerDocument);

    m_coveExplorerDeleteButton = new QPushButton("Delete");
    m_coveExplorerDeleteButton->setObjectName("ghostButton");
    m_coveExplorerDeleteButton->setCursor(Qt::PointingHandCursor);
    m_coveExplorerDeleteButton->setToolTip(
        QStringLiteral("Delete the selected cove document"));
    setOcticon(m_coveExplorerDeleteButton, "trash", 16);
    connect(m_coveExplorerDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteCurrentCoveExplorerDocument);

    m_coveExplorerSaveButton = new QPushButton("Save");
    m_coveExplorerSaveButton->setObjectName("primaryButton");
    m_coveExplorerSaveButton->setCursor(Qt::PointingHandCursor);
    m_coveExplorerSaveButton->setToolTip(
        QStringLiteral("Save the current cove document"));
    setOcticon(m_coveExplorerSaveButton, "check-circle", 16);
    connect(m_coveExplorerSaveButton, &QPushButton::clicked, this,
            &MainWindow::saveCurrentCoveExplorerDocument);

    toolbar->addWidget(m_coveExplorerSelector);
    toolbar->addWidget(m_coveExplorerNewButton);
    toolbar->addWidget(m_coveExplorerInviteButton);
    toolbar->addWidget(m_coveExplorerNewFileButton);
    toolbar->addWidget(m_coveExplorerDeleteButton);
    toolbar->addWidget(m_coveExplorerSaveButton);
    toolbar->addStretch();
    toolbar->addWidget(m_coveExplorerStatus);

    m_coveExplorerTree = new QTreeWidget;
    m_coveExplorerTree->setObjectName("fileTree");
    enableHoverRowHighlight(m_coveExplorerTree);
    m_coveExplorerTree->setColumnCount(2);
    m_coveExplorerTree->setHeaderHidden(true);
    m_coveExplorerTree->header()->setStretchLastSection(false);
    m_coveExplorerTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_coveExplorerTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_coveExplorerTree->setMinimumWidth(220);
    m_coveExplorerTree->setIndentation(14);
    connect(m_coveExplorerTree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                if (item->data(0, Qt::UserRole + 1).toBool()) {
                    item->setExpanded(!item->isExpanded());
                    return;
                }
                openCoveExplorerDocument(item->data(0, Qt::UserRole).toString());
            });
    connect(m_coveExplorerTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (item && !item->data(0, Qt::UserRole + 1).toBool())
                    openCoveExplorerDocument(item->data(0, Qt::UserRole).toString());
            });
    connect(m_coveExplorerTree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) {
                if (item && item->data(0, Qt::UserRole + 1).toBool())
                    item->setIcon(0, iconForDir(true));
            });
    connect(m_coveExplorerTree, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) {
                if (item && item->data(0, Qt::UserRole + 1).toBool())
                    item->setIcon(0, iconForDir(false));
            });

    m_coveExplorerTabs = new QTabWidget;
    m_coveExplorerTabs->setObjectName("fileTabs");
    m_coveExplorerTabs->setDocumentMode(true);
    m_coveExplorerTabs->setMovable(true);
    m_coveExplorerTabs->setTabsClosable(true);
    connect(m_coveExplorerTabs, &QTabWidget::tabCloseRequested, this,
            [this](int index) {
                QWidget *w = m_coveExplorerTabs->widget(index);
                m_openCoveExplorerTabs.remove(m_openCoveExplorerTabs.key(w));
                m_coveExplorerTabs->removeTab(index);
                if (w)
                    w->deleteLater();
                refreshCoveExplorerTree();
            });
    connect(m_coveExplorerTabs, &QTabWidget::currentChanged, this,
            [this] { refreshCoveExplorerTree(); });

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName("filesSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_coveExplorerTree);
    splitter->addWidget(m_coveExplorerTabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 700});

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(toolbar);
    layout->addWidget(splitter, 1);
    return page;
}

QString MainWindow::iconsDir() const
{
    static bool resolved = false;
    static QString cached;
    if (resolved)
        return cached;
    resolved = true;




    static const QString kResourceDir = QStringLiteral(":/icons/tree");
    if (QDir(kResourceDir).exists()) {
        cached = kResourceDir;
        return cached;
    }
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



    const QString parentDir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose a folder to fork %1 into").arg(name),
        QDir::homePath());
    if (parentDir.isEmpty())
        return;
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
    fork.workflowNodes = src.workflowNodes;
    fork.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    fork.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(owner, QStringLiteral("owner")) + "-" +
                      repoSegment(name, QStringLiteral("repository")) + ".git";



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
    trackProcessActivity(process, QStringLiteral("fork"),
                         QStringLiteral("Forking %1/%2").arg(owner, name));
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











bool MainWindow::bindRepoDetailToRepo(int repoIndex)
{
    if (repoIndex < 0 || repoIndex >= m_repositories.size())
        return false;



    const QString owner = m_repositories.at(repoIndex).owner;
    const QString name = m_repositories.at(repoIndex).name;
    if (repoIndex != m_repoDetailIndex)
        openRepoDetail(repoIndex);
    return m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()
           && m_repositories.at(m_repoDetailIndex).owner == owner
           && m_repositories.at(m_repoDetailIndex).name == name;
}

void MainWindow::openRepoDetail(int repoIndex)
{
    if (repoIndex < 0 || repoIndex >= m_repositories.size())
        return;
    ensureRepoDetailSectionBuilt();
    ensureRepoDetailTabBuilt(kRepoLandingTab);



    ensureRepoDetailTabBuilt(4);



    if (m_repoDetailLoading)
        return;
    m_repoDetailLoading = true;



    GitKeepAlive keepAlive;
    m_repoDetailIndex = repoIndex;



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



    m_repoInfo = RepoInfo();
    m_repoBranch.clear();
    loadRepoInfo();
    loadBranchesAndTags();
    // Fork/Mirror counts ride the icons' corners as rail-style badges
    // (adhoc #6) instead of living in the captions.
    if (auto *fork = dynamic_cast<VerticalIconButton *>(m_forkButton)) {
        fork->setText(QStringLiteral("Fork"));
        fork->setBadgeCount(m_repoInfo.forks);
    }
    if (auto *mirror = dynamic_cast<VerticalIconButton *>(m_mirrorButton)) {
        if (repo.previewOnly) {
            mirror->setText(QStringLiteral("Mirror it"));
            mirror->setBadgeCount(0);
            mirror->setToolTip(
                "Clone this preview into your local mirrors and host it");
        } else {
            mirror->setText(QStringLiteral("Mirror"));
            mirror->setBadgeCount(qMax(1, m_repoInfo.mirrors));
            mirror->setToolTip("Sync this repository's mirror now");
        }
    }
    updateRepoDetailStatus();
    updateRepoActionMenus();
    refreshRepoSettings();
    updateRepoCodeSize();
    updateFooterGitIdentity();
    updateFooterCommitInfo();


    refreshIssuesRepoCombo();
    if (m_issuesRepoCombo) {
        const int combo = m_issuesRepoCombo->findData(repoIndex);
        if (combo >= 0)
            m_issuesRepoCombo->setCurrentIndex(combo);
    }


    if (m_agentComposeRepo) {
        const int combo = m_agentComposeRepo->findData(repoIndex);
        if (combo >= 0)
            m_agentComposeRepo->setCurrentIndex(combo);
    }
    logStartup(QStringLiteral("  openRepo: info+branches+codeSize done"));



    m_currentPulls.clear();
    if (m_pullTable)
        m_pullTable->setRowCount(0);
    updateRepoPullCount();
    reloadPullsInBackground();




    m_currentIssues.clear();
    m_issuesLoadedSig.clear();
    if (m_issueTable)
        m_issueTable->setRowCount(0);
    updateRepoIssueCount();
    m_currentDiscussions.clear();
    if (m_discussionTable)
        m_discussionTable->setRowCount(0);
    updateRepoDiscussionCount();
    logStartup(QStringLiteral("  openRepo: selected metadata scheduled"));







    if (m_repoDetailTabs && m_repoDetailTabs->button(kRepoLandingTab))
        m_repoDetailTabs->button(kRepoLandingTab)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(kRepoLandingTab);
    if (m_repoFileTabs) {
        m_repoFileTabs->clear();
        m_openFileTabs.clear();
    }
    if (m_repoFileTree)
        m_repoFileTree->clear();
    if (m_coveExplorerTabs) {
        m_coveExplorerTabs->clear();
        m_openCoveExplorerTabs.clear();
    }
    if (m_coveExplorerTree)
        m_coveExplorerTree->clear();
    if (m_coveExplorerSelector)
        m_coveExplorerSelector->clear();
    m_coveExplorerCoves.clear();
    m_coveExplorerCurrentId.clear();
    m_treeLoadedForIndex = -1;









    const int searchIndexFor = m_repoDetailIndex;
    QTimer::singleShot(0, this, [this, searchIndexFor] {
        if (m_repoDetailIndex != searchIndexFor)
            return;
        loadFileSearchIndex();
    });
    nodeSwitchStep(QStringLiteral("Loading commit history…"));
    // Building the commit table is the single heaviest piece of per-open UI work
    // (up to 300 rows, each with cell widgets, plus several git reads). An open
    // lands on the Code overview and never shows it, so let the Git activity-
    // rail destination build the table on demand.
    // The previous repo's rows and cached tip are cleared so commitsListIsCurrent()
    // forces a rebuild for this repo when its Commits tab is first opened.
    // A previously open repo may have left the Git workspace showing.
    showOverviewFiles();
    if (m_commitsTable)
        m_commitsTable->setRowCount(0);
    m_commitsLoadedTip.clear();



    ++m_commitsLoadGen;
    logStartup(QStringLiteral("  openRepo: commits loaded"));





    m_overviewLoadedKey.clear();
    if (m_overviewList)
        m_overviewList->clear();
    nodeSwitchStep(QStringLiteral("Rendering overview…"));
    loadRepoOverview(QString());
    logStartup(QStringLiteral("  openRepo: overview loaded"));
    showRepoOverview();


    showSection(0);
    updateBreadcrumb();


    m_repoWorkflows.clear();
    if (m_actionsTable)
        m_actionsTable->setRowCount(0);
    updateActionsTabIndicator();







    const int countsFor = m_repoDetailIndex;
    QTimer::singleShot(0, this, [this, countsFor] {
        if (m_repoDetailIndex == countsFor)
            refreshRepoTabCounts();
    });
    refreshRepoPinBanner();



    refreshRepoChangeBadge();
    m_repoDetailLoading = false;
}

void MainWindow::updateRepoCodeSize()
{




    if (m_repoCodeTab)
        m_repoCodeTab->setText(QStringLiteral("Code"));
    m_repoCodeSizePath.clear();
}

void MainWindow::updateRepoIssueCount()
{
    if (m_repoIssuesTab) {
        int openCount = 0;
        for (const Issue &issue : std::as_const(m_currentIssues)) {


            if (issue.status != "closed" && !issue.isDeleted())
                ++openCount;
        }
        // The open count rides the icon's corner as a rail-style badge
        // (adhoc #6) rather than living in the caption.
        if (auto *b = dynamic_cast<VerticalIconButton *>(m_repoIssuesTab))
            b->setBadgeCount(openCount);
    }
}

void MainWindow::updateRepoDiscussionCount()
{
    if (auto *b = dynamic_cast<VerticalIconButton *>(m_repoDiscussionsTab))
        b->setBadgeCount(m_currentDiscussions.size());
}

void MainWindow::updateRepoPullCount()
{
    if (auto *b = dynamic_cast<VerticalIconButton *>(m_repoPullsTab))
        b->setBadgeCount(m_currentPulls.size());
}











void MainWindow::refreshRepoTabCounts()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    reloadIssuesInBackground();
    reloadDiscussionCountInBackground();
    reloadWorkflowCountInBackground();
}

void MainWindow::loadRepoFileTree()
{
    if (!m_repoFileTree)
        return;




    QSet<QString> expanded;
    std::function<void(QTreeWidgetItem *)> collectExpanded =
        [&](QTreeWidgetItem *parent) {
            for (int i = 0; i < parent->childCount(); ++i) {
                QTreeWidgetItem *child = parent->child(i);
                if (child->isExpanded()) {
                    const QString p = child->data(0, Qt::UserRole).toString();
                    if (!p.isEmpty())
                        expanded.insert(p);
                }
                collectExpanded(child);
            }
        };
    collectExpanded(m_repoFileTree->invisibleRootItem());
    const int scrollValue = m_repoFileTree->verticalScrollBar()
                                ? m_repoFileTree->verticalScrollBar()->value()
                                : 0;

    m_repoFileTree->clear();

    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        new QTreeWidgetItem(m_repoFileTree,
                            {"No local copy of this repository to browse."});
        return;
    }
    QByteArray out;
    QString err;



    if (!runGitCapture(dir, {"ls-tree", "-r", "-l", "-z", currentRef()}, &out,
                       &err)) {
        new QTreeWidgetItem(m_repoFileTree,
                            {err.isEmpty() ? "This repository has no commits yet."
                                           : "Could not read files: " + err.left(120)});
        return;
    }

    QStringList paths;
    QHash<QString, qint64> fileSize;
    QHash<QString, qint64> dirSize;
    for (const QByteArray &record : out.split('\0')) {
        if (record.isEmpty())
            continue;
        const int tab = record.indexOf('\t');
        if (tab < 0)
            continue;
        const QString path = QString::fromUtf8(record.mid(tab + 1));
        const QList<QByteArray> meta = record.left(tab).simplified().split(' ');
        const qint64 size =
            meta.size() >= 4 ? QString::fromUtf8(meta.at(3)).toLongLong() : 0;
        paths << path;
        fileSize.insert(path, size);

        const QStringList parts = path.split('/', Qt::SkipEmptyParts);
        QString acc;
        for (int i = 0; i + 1 < parts.size(); ++i) {
            acc = acc.isEmpty() ? parts.at(i) : acc + "/" + parts.at(i);
            dirSize[acc] += size;
        }
    }
    paths.sort(Qt::CaseInsensitive);


    auto setSize = [](QTreeWidgetItem *item, qint64 bytes) {
        item->setText(1, formatByteSize(bytes));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setForeground(1, QBrush(QColor("#8b949e")));
    };

    QHash<QString, QTreeWidgetItem *> dirs;
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



    if (!expanded.isEmpty()) {
        std::function<void(QTreeWidgetItem *)> restoreExpanded =
            [&](QTreeWidgetItem *parent) {
                for (int i = 0; i < parent->childCount(); ++i) {
                    QTreeWidgetItem *child = parent->child(i);
                    if (child->data(0, Qt::UserRole + 1).toBool() &&
                        expanded.contains(child->data(0, Qt::UserRole).toString()))
                        child->setExpanded(true);
                    restoreExpanded(child);
                }
            };
        restoreExpanded(m_repoFileTree->invisibleRootItem());
    }
    if (m_repoFileTree->verticalScrollBar())
        m_repoFileTree->verticalScrollBar()->setValue(scrollValue);
}


static bool repoRelPathIsSafe(const QString &cleanPath)
{
    return !cleanPath.isEmpty() && cleanPath != QLatin1String(".") &&
           !cleanPath.startsWith("../") && !cleanPath.contains("/../") &&
           !QDir::isAbsolutePath(cleanPath) && cleanPath != QLatin1String(".git") &&
           !cleanPath.startsWith(".git/");
}






void MainWindow::showRepoFileTreeMenu(const QPoint &pos)
{
    if (!m_repoFileTree)
        return;
    QTreeWidgetItem *item = m_repoFileTree->itemAt(pos);
    const QString path = item ? item->data(0, Qt::UserRole).toString() : QString();
    const bool isDir = item && item->data(0, Qt::UserRole + 1).toBool();

    const bool isEntry = item && !path.isEmpty();
    const bool canWrite = repoHasWorkingTree();


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


    const int filesPage = m_filesStack ? m_filesStack->currentIndex() : 0;
    refreshOpenRepoDetail();


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


    if (path.endsWith(QStringLiteral(".cove"), Qt::CaseInsensitive) &&
        path.startsWith(CoveStore::covesDirRel() + "/")) {
        openCove(path);
        return;
    }

    if (m_treeLoadedForIndex != m_repoDetailIndex) {
        loadRepoFileTree();
        m_treeLoadedForIndex = m_repoDetailIndex;
    }
    if (m_filesStack)
        m_filesStack->setCurrentIndex(1);


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


    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor, name](bool modified) {
                const int idx = m_repoFileTabs ? m_repoFileTabs->indexOf(editor) : -1;
                if (idx >= 0)
                    m_repoFileTabs->setTabText(
                        idx, modified ? QString::fromUtf8("\xE2\x97\x8F ") + name : name);
            });
    updateRepoFileSaveActions();
}

void MainWindow::updateRepoFileSaveActions()
{
    const QWidget *w = m_repoFileTabs ? m_repoFileTabs->currentWidget() : nullptr;
    const auto *editor = qobject_cast<const QPlainTextEdit *>(w);
    const QString path = w ? w->property("previewPath").toString() : QString();
    const bool haveFile = editor && !path.isEmpty();
    const bool editable = haveFile && !editor->isReadOnly();


    if (m_repoFilePreviewButton) {
        const bool isMarkdown =
            haveFile && previewSyntaxForPath(path) == PreviewSyntax::Markdown;
        const auto *preview = dynamic_cast<const CodePreviewEditor *>(w);
        m_repoFilePreviewButton->setEnabled(isMarkdown);
        m_repoFilePreviewButton->setVisible(isMarkdown);
        m_repoFilePreviewButton->setChecked(preview && preview->markdownPreviewVisible());
    }


    if (m_repoFileHistoryButton)
        m_repoFileHistoryButton->setEnabled(haveFile);


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

QString MainWindow::coveAccountName()
{



    return hostLinkUserName();
}

bool MainWindow::coveInviteAccountValid(const QString &accountName, QString *error)
{
    const QString account = accountName.trimmed().toLower();
    if (account.isEmpty()) {
        if (error)
            *error = QStringLiteral("Enter a ForkMesh account name.");
        return false;
    }
    int status = 0;
    const QJsonObject lookup = getAccountSync(account, &status);
    if (status != 200 || !lookup.value("exists").toBool()) {
        if (error)
            *error = QStringLiteral("%1 is not a ForkMesh account.").arg(account);
        return false;
    }
    if (lookup.value("status").toString() != QLatin1String("active")) {
        if (error)
            *error = QStringLiteral("%1 is not an active ForkMesh account.").arg(account);
        return false;
    }
    return true;
}

void MainWindow::loadCoveExplorer()
{
    if (!m_coveExplorerTree || !m_coveExplorerSelector)
        return;

    const QString keepId = m_coveExplorerCurrentId;
    m_coveExplorerCoves.clear();
    m_openCoveExplorerTabs.clear();
    if (m_coveExplorerTabs)
        m_coveExplorerTabs->clear();
    m_coveExplorerTree->clear();

    QSignalBlocker block(m_coveExplorerSelector);
    m_coveExplorerSelector->clear();

    CoveStore store = coveStoreForRepo(m_repoDetailIndex);
    const bool canCreate = store.canWrite();
    const QString account = coveAccountName();
    if (account.isEmpty()) {
        if (m_coveExplorerStatus)
            m_coveExplorerStatus->setText(
                QStringLiteral("Sign in with a ForkMesh account."));
        if (m_coveExplorerNewButton)
            m_coveExplorerNewButton->setEnabled(false);
        if (m_coveExplorerInviteButton)
            m_coveExplorerInviteButton->setEnabled(false);
        if (m_coveExplorerNewFileButton)
            m_coveExplorerNewFileButton->setEnabled(false);
        if (m_coveExplorerDeleteButton)
            m_coveExplorerDeleteButton->setEnabled(false);
        if (m_coveExplorerSaveButton)
            m_coveExplorerSaveButton->setEnabled(false);
        new QTreeWidgetItem(m_coveExplorerTree,
                            {QStringLiteral("(sign in required)")});
        return;
    }

    QString err;
    const QList<Cove> envelopes = store.listCoves(&err);
    for (Cove cove : envelopes) {


        if (!CoveStore::unlockForAccount(cove, account))
            continue;
        if (cove.name.trimmed().isEmpty())
            cove.name = QStringLiteral("Secure cove");
        m_coveExplorerCoves << cove;
    }

    int selectIndex = -1;
    for (int i = 0; i < m_coveExplorerCoves.size(); ++i) {
        const Cove &cove = m_coveExplorerCoves.at(i);
        m_coveExplorerSelector->addItem(cove.name, cove.id);
        if (!keepId.isEmpty() && cove.id == keepId)
            selectIndex = i;
    }
    if (selectIndex < 0 && !m_coveExplorerCoves.isEmpty())
        selectIndex = 0;
    if (selectIndex >= 0) {
        m_coveExplorerSelector->setCurrentIndex(selectIndex);
        m_coveExplorerCurrentId = m_coveExplorerCoves.at(selectIndex).id;
    } else {
        m_coveExplorerCurrentId.clear();
    }

    if (m_coveExplorerStatus) {
        m_coveExplorerStatus->setText(
            m_coveExplorerCoves.isEmpty()
                ? QStringLiteral("No invited account coves in this repo.")
                : QStringLiteral("%1 invited cove%2")
                      .arg(m_coveExplorerCoves.size())
                      .arg(m_coveExplorerCoves.size() == 1 ? QString() : QStringLiteral("s")));
    }
    if (m_coveExplorerNewButton)
        m_coveExplorerNewButton->setEnabled(canCreate);
    refreshCoveExplorerTree();
}

void MainWindow::refreshCoveExplorerTree()
{
    if (!m_coveExplorerTree)
        return;
    m_coveExplorerTree->clear();

    int coveIndex = -1;
    for (int i = 0; i < m_coveExplorerCoves.size(); ++i) {
        if (m_coveExplorerCoves.at(i).id == m_coveExplorerCurrentId) {
            coveIndex = i;
            break;
        }
    }
    const QString account = coveAccountName();
    const bool haveCove = coveIndex >= 0;
    const bool canWrite = haveCove && coveStoreForRepo(m_repoDetailIndex).canWrite();
    const bool isOwner =
        haveCove &&
        m_coveExplorerCoves.at(coveIndex).creatorAccount.compare(account, Qt::CaseInsensitive) == 0;
    if (m_coveExplorerInviteButton)
        m_coveExplorerInviteButton->setEnabled(canWrite && isOwner);
    if (m_coveExplorerNewFileButton)
        m_coveExplorerNewFileButton->setEnabled(canWrite && haveCove);
    if (m_coveExplorerDeleteButton)
        m_coveExplorerDeleteButton->setEnabled(canWrite && haveCove);

    QWidget *currentTab =
        m_coveExplorerTabs ? m_coveExplorerTabs->currentWidget() : nullptr;
    auto *currentEditor = qobject_cast<QPlainTextEdit *>(currentTab);
    const bool currentEditable = currentEditor && !currentEditor->isReadOnly() &&
                                 !currentTab->property("coveDocPath").toString().isEmpty();
    if (m_coveExplorerSaveButton)
        m_coveExplorerSaveButton->setEnabled(currentEditable);

    if (!haveCove) {
        new QTreeWidgetItem(m_coveExplorerTree,
                            {QStringLiteral("(no invited coves)")});
        return;
    }

    const Cove &cove = m_coveExplorerCoves.at(coveIndex);
    if (cove.documents.isEmpty()) {
        new QTreeWidgetItem(m_coveExplorerTree,
                            {QStringLiteral("(empty cove)")});
        return;
    }

    QHash<QString, QTreeWidgetItem *> dirs;
    QTreeWidgetItem *root = m_coveExplorerTree->invisibleRootItem();
    for (const CoveDocument &doc : std::as_const(cove.documents)) {
        QString path = QDir::cleanPath(doc.name.trimmed());
        if (!coveDocPathIsSafe(path))
            path = doc.id.isEmpty() ? QStringLiteral("untitled.txt")
                                    : doc.id + QStringLiteral(".txt");
        const QStringList parts = path.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;
        QTreeWidgetItem *parent = root;
        QString dirPath;
        for (int i = 0; i < parts.size() - 1; ++i) {
            dirPath = dirPath.isEmpty() ? parts.at(i) : dirPath + "/" + parts.at(i);
            QTreeWidgetItem *folder = dirs.value(dirPath, nullptr);
            if (!folder) {
                folder = new QTreeWidgetItem(parent, {parts.at(i), QString()});
                folder->setIcon(0, iconForDir(false));
                folder->setData(0, Qt::UserRole, dirPath);
                folder->setData(0, Qt::UserRole + 1, true);
                dirs.insert(dirPath, folder);
            }
            parent = folder;
        }
        const QString leaf = parts.last();
        const QString updated =
            doc.updatedAtMs > 0
                ? QDateTime::fromMSecsSinceEpoch(doc.updatedAtMs)
                      .toString(QStringLiteral("yyyy-MM-dd"))
                : QString();
        auto *item = new QTreeWidgetItem(parent, {leaf, updated});
        item->setIcon(0, iconForFile(leaf));
        item->setData(0, Qt::UserRole, path);
        item->setData(0, Qt::UserRole + 1, false);
        item->setToolTip(0, path);
    }
    m_coveExplorerTree->sortItems(0, Qt::AscendingOrder);
    m_coveExplorerTree->expandToDepth(0);
}

void MainWindow::openCoveExplorerDocument(const QString &path)
{
    if (path.isEmpty() || !m_coveExplorerTabs)
        return;
    int coveIndex = -1;
    for (int i = 0; i < m_coveExplorerCoves.size(); ++i) {
        if (m_coveExplorerCoves.at(i).id == m_coveExplorerCurrentId) {
            coveIndex = i;
            break;
        }
    }
    if (coveIndex < 0)
        return;
    Cove &cove = m_coveExplorerCoves[coveIndex];
    int docIndex = -1;
    for (int i = 0; i < cove.documents.size(); ++i) {
        if (QDir::cleanPath(cove.documents.at(i).name) == path) {
            docIndex = i;
            break;
        }
    }
    if (docIndex < 0)
        return;

    const QString key = coveDocTabKey(cove.id, path);
    if (m_openCoveExplorerTabs.contains(key)) {
        m_coveExplorerTabs->setCurrentWidget(m_openCoveExplorerTabs.value(key));
        return;
    }

    const CoveDocument &doc = cove.documents.at(docIndex);
    auto *editor = new CodePreviewEditor(path);
    editor->setProperty("coveId", cove.id);
    editor->setProperty("coveDocPath", path);
    editor->setReadOnly(!coveStoreForRepo(m_repoDetailIndex).canWrite());
    editor->setPlainText(doc.body);
    editor->document()->setModified(false);
    new CodePreviewHighlighter(editor->document(), path);
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this](bool) { refreshCoveExplorerTree(); });

    const QString name = path.section('/', -1);
    const int index = m_coveExplorerTabs->addTab(editor, iconForFile(name), name);
    m_coveExplorerTabs->setTabToolTip(index, path);
    m_coveExplorerTabs->setCurrentIndex(index);
    m_openCoveExplorerTabs.insert(key, editor);
    refreshCoveExplorerTree();
}

void MainWindow::saveCurrentCoveExplorerDocument()
{
    QWidget *w = m_coveExplorerTabs ? m_coveExplorerTabs->currentWidget() : nullptr;
    auto *editor = qobject_cast<QPlainTextEdit *>(w);
    if (!editor || editor->isReadOnly())
        return;
    const QString coveId = w->property("coveId").toString();
    const QString path = w->property("coveDocPath").toString();
    if (coveId.isEmpty() || path.isEmpty())
        return;

    for (Cove &cove : m_coveExplorerCoves) {
        if (cove.id != coveId)
            continue;
        for (CoveDocument &doc : cove.documents) {
            if (QDir::cleanPath(doc.name) != path)
                continue;
            doc.body = editor->toPlainText();
            doc.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
            CoveAccessEntry entry;
            entry.who = m_profileIdentity.publicKey();
            entry.name = coveAccountName();
            entry.ts = doc.updatedAtMs;
            entry.action = QStringLiteral("edit");
            CoveStore::appendAccess(cove, entry);

            QString err;
            if (!coveStoreForRepo(m_repoDetailIndex).saveAccountCove(cove, &err)) {
                QMessageBox::warning(this, QStringLiteral("Save cove file"),
                                     QStringLiteral("Could not save: ") + err);
                return;
            }
            editor->document()->setModified(false);
            setRepoDetailNotice(QStringLiteral("Saved %1 in cove.").arg(path));
            refreshCoveExplorerTree();
            return;
        }
    }
}

void MainWindow::createCoveExplorerCove()
{
    const QString account = coveAccountName();
    if (account.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("New cove"),
                             QStringLiteral("Sign in with a ForkMesh account "
                                            "before creating a cove."));
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("New cove"),
                              QStringLiteral("Cove name:"), QLineEdit::Normal,
                              QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return;
    CoveStore store = coveStoreForRepo(m_repoDetailIndex);
    Cove created;
    QString err;
    if (!store.createAccountCove(name, account, {account}, false, {}, &created, &err)) {
        QMessageBox::warning(this, QStringLiteral("New cove"),
                             QStringLiteral("Could not create cove: ") + err);
        return;
    }
    m_coveExplorerCurrentId = created.id;
    loadCoveExplorer();
    setRepoDetailNotice(QStringLiteral("Created account cove."));
}

void MainWindow::inviteUserToCurrentCove()
{
    const QString account = coveAccountName();
    if (account.isEmpty())
        return;
    int coveIndex = -1;
    for (int i = 0; i < m_coveExplorerCoves.size(); ++i) {
        if (m_coveExplorerCoves.at(i).id == m_coveExplorerCurrentId) {
            coveIndex = i;
            break;
        }
    }
    if (coveIndex < 0)
        return;
    Cove &cove = m_coveExplorerCoves[coveIndex];
    if (cove.creatorAccount.compare(account, Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, QStringLiteral("Invite"),
                             QStringLiteral("Only the cove creator can invite users."));
        return;
    }
    QStringList candidates = mentionCandidateNames();
    for (const RepositoryRecord &repo : std::as_const(m_repositories))
        candidates << repo.owner;
    for (const QString &invitee : std::as_const(cove.invitedAccounts))
        candidates << invitee;
    candidates << cove.creatorAccount << account;

    QSet<QString> seenCandidates;
    QStringList inviteCandidates;
    for (const QString &raw : std::as_const(candidates)) {
        const QString name = raw.trimmed().toLower();
        if (name.isEmpty() || seenCandidates.contains(name))
            continue;
        seenCandidates.insert(name);
        if (name == account || name == cove.creatorAccount ||
            cove.invitedAccounts.contains(name))
            continue;
        inviteCandidates << name;
    }
    inviteCandidates.sort(Qt::CaseInsensitive);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Invite to cove"));
    dialog.setModal(true);
    auto *form = new QFormLayout(&dialog);
    auto *edit = new QLineEdit;
    edit->setPlaceholderText(QStringLiteral("account-name"));
    edit->setClearButtonEnabled(true);
    auto *model = new QStringListModel(inviteCandidates, &dialog);
    auto *completer = new QCompleter(model, &dialog);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setFilterMode(Qt::MatchContains);
    edit->setCompleter(completer);
    form->addRow(QStringLiteral("ForkMesh account"), edit);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Invite"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QTimer::singleShot(0, edit, [edit] {
        edit->setFocus();
        if (QCompleter *c = edit->completer())
            c->complete();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString grantee = edit->text().trimmed().toLower();
    if (grantee.isEmpty())
        return;
    if (grantee == account || cove.invitedAccounts.contains(grantee)) {
        QMessageBox::information(this, QStringLiteral("Invite"),
                                 QStringLiteral("%1 can already view this cove.")
                                     .arg(grantee));
        return;
    }
    QString err;
    if (!coveInviteAccountValid(grantee, &err)) {
        QMessageBox::warning(this, QStringLiteral("Invite"), err);
        return;
    }
    cove.invitedAccounts << grantee;
    cove.invitedAccounts.removeDuplicates();
    cove.invitedAccounts.sort(Qt::CaseInsensitive);

    if (!coveStoreForRepo(m_repoDetailIndex).saveAccountCove(cove, &err)) {
        QMessageBox::warning(this, QStringLiteral("Invite"),
                             QStringLiteral("Could not save invitation: ") + err);
        return;
    }
    if (m_backend)
        m_backend->notifyCoveInvited(grantee, cove.id, cove.name, account,
                                     QDateTime::currentMSecsSinceEpoch());
    m_coveExplorerCurrentId = cove.id;
    loadCoveExplorer();
    setRepoDetailNotice(QStringLiteral("Invited %1 to the cove.").arg(grantee));
}

void MainWindow::createCoveExplorerDocument()
{
    int coveIndex = -1;
    for (int i = 0; i < m_coveExplorerCoves.size(); ++i) {
        if (m_coveExplorerCoves.at(i).id == m_coveExplorerCurrentId) {
            coveIndex = i;
            break;
        }
    }
    if (coveIndex < 0)
        return;
    bool ok = false;
    const QString path =
        QInputDialog::getText(this, QStringLiteral("New cove file"),
                              QStringLiteral("File path:"), QLineEdit::Normal,
                              QStringLiteral("notes.md"), &ok)
            .trimmed();
    if (!ok || path.isEmpty())
        return;
    const QString clean = QDir::cleanPath(path);
    if (!coveDocPathIsSafe(clean)) {
        QMessageBox::warning(this, QStringLiteral("New cove file"),
                             QStringLiteral("Refusing to create that path."));
        return;
    }

    Cove &cove = m_coveExplorerCoves[coveIndex];
    for (const CoveDocument &doc : std::as_const(cove.documents)) {
        if (QDir::cleanPath(doc.name) == clean) {
            QMessageBox::warning(this, QStringLiteral("New cove file"),
                                 QStringLiteral("%1 already exists.").arg(clean));
            return;
        }
    }
    CoveDocument doc;
    doc.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    doc.name = clean;
    doc.mime = coveDocMimeForPath(clean);
    doc.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    cove.documents << doc;

    QString err;
    if (!coveStoreForRepo(m_repoDetailIndex).saveAccountCove(cove, &err)) {
        QMessageBox::warning(this, QStringLiteral("New cove file"),
                             QStringLiteral("Could not save: ") + err);
        return;
    }
    m_coveExplorerCurrentId = cove.id;
    loadCoveExplorer();
    openCoveExplorerDocument(clean);
}

void MainWindow::deleteCurrentCoveExplorerDocument()
{
    int coveIndex = -1;
    for (int i = 0; i < m_coveExplorerCoves.size(); ++i) {
        if (m_coveExplorerCoves.at(i).id == m_coveExplorerCurrentId) {
            coveIndex = i;
            break;
        }
    }
    if (coveIndex < 0)
        return;
    Cove &cove = m_coveExplorerCoves[coveIndex];
    QString path;
    QWidget *w = m_coveExplorerTabs ? m_coveExplorerTabs->currentWidget() : nullptr;
    if (w && w->property("coveId").toString() == cove.id)
        path = w->property("coveDocPath").toString();
    if (path.isEmpty() && m_coveExplorerTree && m_coveExplorerTree->currentItem()) {
        QTreeWidgetItem *item = m_coveExplorerTree->currentItem();
        if (!item->data(0, Qt::UserRole + 1).toBool())
            path = item->data(0, Qt::UserRole).toString();
    }
    if (path.isEmpty())
        return;

    if (QMessageBox::question(this, QStringLiteral("Delete cove file"),
                              QStringLiteral("Delete \"%1\" from this cove?")
                                  .arg(path),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    for (int i = 0; i < cove.documents.size(); ++i) {
        if (QDir::cleanPath(cove.documents.at(i).name) != path)
            continue;
        cove.documents.removeAt(i);
        QString err;
        if (!coveStoreForRepo(m_repoDetailIndex).saveAccountCove(cove, &err)) {
            QMessageBox::warning(this, QStringLiteral("Delete cove file"),
                                 QStringLiteral("Could not save: ") + err);
            return;
        }
        loadCoveExplorer();
        setRepoDetailNotice(QStringLiteral("Deleted %1 from cove.").arg(path));
        return;
    }
}

void MainWindow::toggleRepoFileMarkdownPreview()
{
    QWidget *w = m_repoFileTabs ? m_repoFileTabs->currentWidget() : nullptr;
    auto *editor = dynamic_cast<CodePreviewEditor *>(w);
    if (!editor)
        return;


    editor->setMarkdownPreviewVisible(!editor->markdownPreviewVisible());
    updateRepoFileSaveActions();
}

void MainWindow::showRepoFileHistory(const QString &path)
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || path.isEmpty())
        return;




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

        QByteArray mbox;
        runGitCapture(dir, {"format-patch", "--stdout", base + ".." + branch}, &mbox,
                      nullptr);
        runGitCapture(dir, {"checkout", base}, nullptr, nullptr);
        PullStore store = pullStoreForCurrentRepo();
        QString error;
        const int number = store.createPull(title, description, base, branch,
                                            QString::fromUtf8(diff),
                                            QString::fromUtf8(mbox),
                                             true, &error);
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



    if (!m_filesStack)
        return;
    loadRepoOverview(m_overviewPath);
    m_filesStack->setCurrentIndex(0);
    if (m_filesModeOverviewButton)
        m_filesModeOverviewButton->setChecked(true);
    if (m_filesModeExplorerButton)
        m_filesModeExplorerButton->setChecked(false);
    if (m_filesModeCoveExplorerButton)
        m_filesModeCoveExplorerButton->setChecked(false);
    // "Code overview" always means the file list + README: if the Git
    // workspace was left showing, swap it back.
    showOverviewFiles();
}

void MainWindow::showRepoEditor()
{


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
    if (m_filesModeCoveExplorerButton)
        m_filesModeCoveExplorerButton->setChecked(false);
}

void MainWindow::showRepoCoveExplorer()
{
    if (!m_filesStack)
        return;
    loadCoveExplorer();
    m_filesStack->setCurrentIndex(2);
    if (m_filesModeOverviewButton)
        m_filesModeOverviewButton->setChecked(false);
    if (m_filesModeExplorerButton)
        m_filesModeExplorerButton->setChecked(false);
    if (m_filesModeCoveExplorerButton)
        m_filesModeCoveExplorerButton->setChecked(true);
}

// Show the universal Git workspace. Pure navigation — no
// loading — so callers that jump straight to one commit (showCommit) aren't
// clobbered by an automatic selection; each route layers its list build on top.
void MainWindow::showOverviewCommits()
{


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
    if (m_filesModeCoveExplorerButton)
        m_filesModeCoveExplorerButton->setChecked(false);
    if (m_overviewBodyStack)
        m_overviewBodyStack->setCurrentIndex(1);
    // This is an activity-rail destination, not a Code Overview sub-page. Some
    // routes arrive while every stack is already on index 0/1, so no
    // currentChanged signal fires and the Code chrome would otherwise remain
    // visible above the Git workspace. Apply the ownership state explicitly
    // after all stacks have moved, then reassert it on the next event-loop turn
    // in case a queued repository refresh completed during navigation.
    updateRepoActivityRail();
    QTimer::singleShot(0, this, [this] {
        const bool stillOnGit =
            m_repoDetailStack && m_repoDetailStack->currentIndex() == 0 &&
            m_filesStack && m_filesStack->currentIndex() == 0 &&
            m_overviewBodyStack && m_overviewBodyStack->currentIndex() == 1;
        if (stillOnGit)
            updateRepoActivityRail();
    });
}

// Swap the overview body back to the file list + README.
void MainWindow::showOverviewFiles()
{
    if (m_overviewBodyStack)
        m_overviewBodyStack->setCurrentIndex(0);
}




void MainWindow::showOverviewBranches()
{


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
    if (m_filesModeCoveExplorerButton)
        m_filesModeCoveExplorerButton->setChecked(false);
    if (m_overviewBodyStack)
        m_overviewBodyStack->setCurrentIndex(2);
}




void MainWindow::showOverviewWorktrees()
{
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
    if (m_filesModeCoveExplorerButton)
        m_filesModeCoveExplorerButton->setChecked(false);
    if (m_overviewBodyStack)
        m_overviewBodyStack->setCurrentIndex(3);
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






    if (m_overviewLoading)
        return;

    const QString dir = repoGitDir();






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
    m_overviewLoadedKey.clear();




    ScopedFlag loading(m_overviewLoading);
    GitKeepAlive keepAlive;

    m_overviewPath = path;








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


            m_commitBarStatusHash = fullHash;
            m_commitBarBodyHtml =
                QStringLiteral("<b>%1</b> &nbsp; <span style='color:#8b949e'>%2 "
                               "committed %3</span>")
                    .arg(subject.toHtmlEscaped(), author.toHtmlEscaped(),
                         when.toHtmlEscaped());
            commitBarText = commitStatusGlyph(fullHash) + m_commitBarBodyHtml;
        } else {
            m_commitBarStatusHash.clear();
            m_commitBarBodyHtml.clear();
            commitBarText = "<span style='color:#8b949e'>No commits yet</span>";
        }
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
    QString messageRow;
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








    struct OverviewTreeCache {
        QString key;
        QByteArray sizeOut;
        QByteArray locOut;
        QHash<QString, QByteArray> entryLog;
    };
    static OverviewTreeCache treeCache;
    const QString treeKey =
        head.isEmpty() ? QString() : dir + QLatin1Char('|') + head;
    if (treeKey.isEmpty() || treeCache.key != treeKey)
        treeCache = OverviewTreeCache{treeKey, {}, {}, {}};




    QHash<QString, qint64> childBytes;



    QHash<QString, qint64> childFiles;
    QByteArray sizeOut = treeCache.sizeOut;
    if (messageRow.isEmpty() && sizeOut.isEmpty() &&
        runGitCapture(dir, {"ls-tree", "-r", "-l", "-z", currentRef()}, &sizeOut,
                      nullptr))
        treeCache.sizeOut = sizeOut;
    if (messageRow.isEmpty() && !sizeOut.isEmpty()) {
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
            const qint64 sz = meta.at(3).toLongLong();
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






    QHash<QString, qint64> childLoc;
    QByteArray locOut = treeCache.locOut;
    static const QByteArray kEmptyTree = "4b825dc642cb6eb9a060e54bf8d69288fbee4904";
    if (messageRow.isEmpty() && locOut.isEmpty() &&
        runGitCapture(dir,
                      {"diff", "--numstat", "--no-renames", "-z",
                       QString::fromLatin1(kEmptyTree), currentRef()},
                      &locOut, nullptr))
        treeCache.locOut = locOut;
    if (messageRow.isEmpty() && !locOut.isEmpty()) {
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
            if (added == "-")
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




            QByteArray logOut = treeCache.entryLog.value(e.path);
            if (logOut.isEmpty() &&
                runGitCapture(dir,
                              {"log", "-1", "--format=%ct%x1f%cr%x1f%s", currentRef(),
                               "--", e.path},
                              &logOut, nullptr))
                treeCache.entryLog.insert(e.path, logOut);
            if (!logOut.trimmed().isEmpty()) {
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


    QString readmeMarkdown;
    if (!readmePath.isEmpty()) {
        QByteArray readme;
        if (runGitCapture(dir, {"show", currentRef() + ":" + readmePath}, &readme,
                          nullptr) &&
            !readme.contains('\0'))
            readmeMarkdown = QString::fromUtf8(readme);
    }




    QWidget *page = m_filesStack ? m_filesStack->widget(0) : nullptr;
    if (page)
        page->setUpdatesEnabled(false);
    if (m_commitBar)
        m_commitBar->setText(commitBarText);
    if (m_overviewCrumb)
        m_overviewCrumb->setText(crumbText);
    m_overviewRows = rows;
    m_overviewRepoBytes = repoBytes;
    if (!messageRow.isEmpty()) {
        m_overviewList->clear();
        new QTreeWidgetItem(m_overviewList, {messageRow});
    } else {
        populateOverviewTree();
        m_overviewLoadedKey = key;
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



    m_overviewList->setUpdatesEnabled(false);
    m_overviewList->clear();


    if (!m_overviewPath.isEmpty()) {
        auto *up = new QTreeWidgetItem(m_overviewList);
        up->setIcon(0, iconForDir(false));
        up->setText(0, "..");
        const int cut = m_overviewPath.lastIndexOf('/');
        up->setData(0, Qt::UserRole, cut < 0 ? QString() : m_overviewPath.left(cut));
        up->setData(0, Qt::UserRole + 1, 1);
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
                  } else {
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


    if (repo.localPath.isEmpty() || !QDir(repo.localPath).exists() ||
        repo.mirrorPath.isEmpty() || !QDir(repo.mirrorPath).exists())
        return result;
    const QString mirrorTip = currentMirrorTip();
    if (mirrorTip.isEmpty())
        return result;



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


    if (!m_commitsTable || m_commitsTable->rowCount() == 0 ||
        m_commitsLoadedTip.isEmpty())
        return false;
    if (m_commitsLoadedRef != currentRef())
        return false;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return false;


    QByteArray out;
    if (!runGitCapture(dir, {"rev-parse", currentRef()}, &out, nullptr))
        return false;
    if (QString::fromUtf8(out).trimmed() != m_commitsLoadedTip)
        return false;



    return currentMirrorTip() == m_commitsLoadedMirrorTip;
}

void MainWindow::refreshCommitMarkersIfStale()
{





    if (!m_commitsListPage || !m_commitsListPage->isVisible())
        return;
    if (!commitsListIsCurrent())
        loadCommits();
}




constexpr int kCommitSearchDepth = 5000;

void MainWindow::loadCommits()
{





    GitKeepAlive keepAlive;
    refreshSourceControl();
    if (!m_commitsTable)
        return;



    const int loadGen = ++m_commitsLoadGen;
    QSignalBlocker block(m_commitsTable);







    TableRepaintGuard repaintGuard(m_commitsTable);
    m_commitsTable->setRowCount(0);
    showCommitList(); // always land on the list when (re)loading
    // The Insights "Contributors & activity" counts are derived from the same
    // history; keep them in step when it moves underneath an open Insights tab
    // (a background sync, agent commit, revert or commit can advance it). A tab
    // click reloads them anyway, so only refresh when that page is on screen.
    if (m_repoDetailStack && m_insightsTabIndex >= 0 &&
        m_repoDetailStack->currentIndex() == m_insightsTabIndex)
        loadRepoInsights();
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    QByteArray out;


    if (currentRef() != m_commitsLoadedRef)
        m_commitsLimit = 300;
    m_commitsHasMore = false;













    const int rowLimit = m_commitsShowingAll ? kCommitSearchDepth : m_commitsLimit;
    QStringList logArgs{
        "log",
        "--topo-order",
        "--decorate=full",
        "--format=%x1e%H%x1f%h%x1f%an%x1f%ar%x1f%ct%x1f%s%x1f%P%x1f%D%x1f%b"};
    logArgs << "-n" << QString::number(rowLimit + 1);
    logArgs << currentRef();
    if (!runGitCapture(dir, logArgs, &out, nullptr))
        return;


    const QSet<QString> unpushed = unpushedCommitHashes();
    m_commitsUnsyncedHashes.clear();


    QString loadedTip;



    QList<QString> activeLanes;



    for (const QByteArray &record : out.split('\x1e')) {
        if (record.trimmed().isEmpty())
            continue;



        if (m_commitsTable->rowCount() >= rowLimit) {
            m_commitsHasMore = true;
            break;
        }



        const QStringList f =
            QString::fromUtf8(record).split(QLatin1Char('\x1f'));
        if (f.size() < 6)
            continue;
        if (loadedTip.isEmpty())
            loadedTip = f.at(0);
        const bool isUnpushed = unpushed.contains(f.at(0));




        const QString hash = f.at(0);
        const QStringList parents =
            f.value(6).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        int nodeLane = activeLanes.indexOf(hash);
        if (nodeLane < 0) {
            nodeLane = activeLanes.indexOf(QString());
            if (nodeLane < 0) {
                nodeLane = activeLanes.size();
                activeLanes.append(hash);
            } else {
                activeLanes[nodeLane] = hash;
            }
        }

        QVariantList laneCols;
        for (int i = 0; i < activeLanes.size(); ++i) {
            if (!activeLanes.at(i).isEmpty())
                laneCols.append(i);
        }

        for (int i = 0; i < activeLanes.size(); ++i)
            if (i != nodeLane && activeLanes.at(i) == hash)
                activeLanes[i].clear();
        if (parents.isEmpty()) {
            activeLanes[nodeLane].clear();
        } else {
            activeLanes[nodeLane] = parents.at(0);
            for (int k = 1; k < parents.size(); ++k) {
                if (activeLanes.indexOf(parents.at(k)) >= 0)
                    continue;
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
            activeLanes.removeLast();



        QVariantList botLaneCols;
        for (int i = 0; i < activeLanes.size(); ++i) {
            if (!activeLanes.at(i).isEmpty())
                botLaneCols.append(i);
        }

        const int row = m_commitsTable->rowCount();
        m_commitsTable->insertRow(row);

        auto *graphItem = new QTableWidgetItem;
        graphItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        graphItem->setData(kGraphLanesRole, laneCols);
        graphItem->setData(kGraphNodeLaneRole, nodeLane);
        graphItem->setData(kGraphBottomLanesRole, botLaneCols);

        graphItem->setData(kGraphIsMergeRole, parents.size() > 1);
        m_commitsTable->setItem(row, kCommitGraphCol, graphItem);
        auto *summary = new SortTableWidgetItem(f.at(5));
        summary->setData(Qt::UserRole, f.at(0));
        summary->setData(kTableSortRole, f.at(5).toLower());


        summary->setData(kCommitRowKindRole, 0);
        summary->setData(kCommitExpandedRole, false);
        summary->setData(kCommitAuthorRole, f.at(2));
        summary->setData(kCommitUnsyncedRole, isUnpushed);


        const QString msgBody = f.value(8).trimmed();
        if (!msgBody.isEmpty())
            summary->setData(kCommitBodyRole, msgBody);



        {
            QStringList refs;
            QStringList refKinds;
            const QStringList rawRefs = f.value(7).split(
                QStringLiteral(", "), Qt::SkipEmptyParts);
            for (QString ref : rawRefs) {
                ref = ref.trimmed();
                QString kind = QStringLiteral("local");
                if (ref.startsWith(QLatin1String("HEAD -> "))) {
                    ref = ref.mid(8);
                } else if (ref == QLatin1String("HEAD")) {
                    continue; // detached HEAD marker, not a ref
                } else if (ref.startsWith(QLatin1String("tag: "))) {
                    ref = ref.mid(5);
                    kind = QStringLiteral("tag");
                }
                if (ref.startsWith(QLatin1String("refs/heads/"))) {
                    ref = ref.mid(11);
                    kind = QStringLiteral("local");
                } else if (ref.startsWith(QLatin1String("refs/remotes/"))) {
                    ref = ref.mid(13);
                    kind = QStringLiteral("remote");
                } else if (ref.startsWith(QLatin1String("refs/tags/"))) {
                    ref = ref.mid(10);
                    kind = QStringLiteral("tag");
                }
                refs << ref;
                refKinds << kind;
                if (refs.size() >= 3)
                    break;
            }
            if (!refs.isEmpty()) {
                summary->setData(kCommitRefsRole, refs);
                summary->setData(kCommitRefKindsRole, refKinds);
            }
        }


        switch (commitStatusCode(f.at(0))) {
        case 1:
            summary->setIcon(themedOcticon("check-circle", QColor("#3fb950"), 14));
            break;
        case 2:
            summary->setIcon(themedOcticon("x", QColor("#f85149"), 14));
            break;
        case 3:
            summary->setIcon(themedOcticon("sync", QColor("#58a6ff"), 14));
            break;
        default:
            break;
        }


        m_commitsTable->setItem(row, kCommitSummaryCol, summary);

        auto *author = new SortTableWidgetItem(f.at(2));
        author->setData(kTableSortRole, f.at(2).toLower());
        m_commitsTable->setItem(row, 0, author);
        const qint64 commitTs = f.at(4).toLongLong();
        auto *date = new SortTableWidgetItem(formatShortRelativeTime(commitTs));
        date->setData(kTableSortRole, commitTs);
        date->setToolTip(f.at(3));
        m_commitsTable->setItem(row, 1, date);

        auto *hashItem = new SortTableWidgetItem(
            isUnpushed ? QString::fromUtf8("\xE2\x96\xB2 ") + f.at(1) : f.at(1));
        hashItem->setData(kTableSortRole, f.at(1));
        if (isUnpushed) {
            m_commitsUnsyncedHashes << f.at(0);
            hashItem->setForeground(QColor("#d29922"));
            hashItem->setToolTip(
                QStringLiteral("%1 — not yet synced to the network mirror").arg(f.at(1)));
        } else {
            hashItem->setToolTip(f.at(1));
        }
        m_commitsTable->setItem(row, kCommitHashCol, hashItem);



        const QString pending = QString::fromUtf8("\xC2\xB7");
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


        updateCommitRowHover(row);
    }










    updateCommitsUnsyncedFilesPanel();

    if (m_commitsPullButton) {
        const bool writable = repoHasWorkingTree();
        m_commitsPullButton->setEnabled(writable);
        m_commitsPullButton->setToolTip(
            writable ? QStringLiteral("Fast-forward the working tree to the "
                                      "latest fetched history (git pull --ff-only)")
                     : QStringLiteral("Read-only mirror \xE2\x80\x94 no working "
                                      "tree to pull into"));
    }



    if (m_commitSearch && !m_commitSearch->text().trimmed().isEmpty())
        filterCommits(m_commitSearch->text());





    m_commitsLoadedRef = currentRef();
    m_commitsLoadedTip = loadedTip;
    m_commitsLoadedMirrorTip = currentMirrorTip();
    refreshCommitsBranchButton();





    QTimer::singleShot(0, this, [this, loadGen] { fillCommitStats(loadGen); });





    QTimer::singleShot(0, this, [this] { applyCommitIssueClosures(); });
}




struct CommitStatTotals {
    int files = 0;
    int adds = 0;
    int dels = 0;
    QStringList preview;
};

void MainWindow::fillCommitStats(int loadGen)
{



    if (loadGen != m_commitsLoadGen || !m_commitsTable)
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;






    QStringList args{"log", "--numstat", "--format=%x1e%H"};




    args << "--diff-merges=first-parent";
    args << "-n"
         << QString::number(m_commitsShowingAll ? kCommitSearchDepth
                                                : m_commitsLimit);
    args << currentRef();

    runOffThread<QHash<QString, CommitStatTotals>>(
        [dir, args] {
            QHash<QString, CommitStatTotals> stats;
            QByteArray out;
            if (!runGitCapture(dir, args, &out, nullptr)) {
                QStringList plain = args;
                plain.removeAll(QStringLiteral("--diff-merges=first-parent"));
                if (!runGitCapture(dir, plain, &out, nullptr))
                    return stats;
            }



            constexpr int kHoverFilePreview = 12;
            for (const QByteArray &record : out.split('\x1e')) {
                const QStringList lines = QString::fromUtf8(record).split(
                    QLatin1Char('\n'), Qt::SkipEmptyParts);
                if (lines.isEmpty())
                    continue;
                CommitStatTotals s;
                for (int i = 1; i < lines.size(); ++i) {
                    const QStringList cols = lines.at(i).split(QLatin1Char('\t'));
                    if (cols.size() < 3)
                        continue;
                    ++s.files;
                    bool ok = false;
                    const int addCount =
                        cols.at(0).toInt(&ok);
                    if (ok)
                        s.adds += addCount;
                    const int delCount = cols.at(1).toInt(&ok);
                    if (ok)
                        s.dels += delCount;
                    if (s.preview.size() < kHoverFilePreview)
                        s.preview << QStringLiteral("%1\t%2\t%3")
                                         .arg(cols.mid(2).join(QLatin1Char('\t')),
                                              cols.at(0), cols.at(1));
                }

                stats.insert(lines.first(), s);
            }
            return stats;
        },
        [this, loadGen](QHash<QString, CommitStatTotals> stats) {


            if (loadGen != m_commitsLoadGen || !m_commitsTable)
                return;





            QSignalBlocker block(m_commitsTable);
            const bool wasSorting = m_commitsTable->isSortingEnabled();
            m_commitsTable->setSortingEnabled(false);
            for (int row = 0; row < m_commitsTable->rowCount(); ++row) {
                QTableWidgetItem *sum = m_commitsTable->item(row, kCommitSummaryCol);
                if (!sum || sum->data(kCommitRowKindRole).toInt() != 0)
                    continue;
                const auto it = stats.constFind(sum->data(Qt::UserRole).toString());
                if (it == stats.constEnd())
                    continue;
                const CommitStatTotals &s = it.value();
                sum->setData(kCommitFilesRole, s.preview);
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
                updateCommitRowHover(row);
            }
            m_commitsTable->setSortingEnabled(wasSorting);
        });
}

void MainWindow::loadMoreCommits()
{
    if (m_commitsLoadingMore || !m_commitsHasMore || !m_commitsTable)
        return;
    m_commitsLoadingMore = true;


    const int scrollVal = m_commitsTable->verticalScrollBar()->value();
    m_commitsLimit += 300;
    loadCommits();
    m_commitsTable->verticalScrollBar()->setValue(scrollVal);
    m_commitsLoadingMore = false;
}



struct CommitFileStat {
    QString path;
    int adds = 0;
    int dels = 0;
};

static QList<CommitFileStat> commitFileStats(const QString &dir,
                                             const QString &hash)
{
    QList<CommitFileStat> files;
    QByteArray out;





    const QStringList base{QStringLiteral("diff-tree"), QStringLiteral("--root"),
                           QStringLiteral("--no-commit-id"),
                           QStringLiteral("--numstat"), QStringLiteral("-r"),
                           QStringLiteral("-M")};
    if (!runGitCapture(dir,
                       base + QStringList{
                                  QStringLiteral("--diff-merges=first-parent"),
                                  hash},
                       &out, nullptr) ||
        out.isEmpty()) {
        if (!runGitCapture(dir, base + QStringList{hash}, &out, nullptr))
            return files;
    }
    const QStringList lines =
        QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList cols = line.split(QLatin1Char('\t'));
        if (cols.size() < 3)
            continue;
        CommitFileStat f;
        f.path = cols.mid(2).join(QLatin1Char('\t'));
        f.adds = cols.at(0).toInt();
        f.dels = cols.at(1).toInt();
        files << f;
    }
    return files;
}

void MainWindow::toggleCommitFilesRows(int row)
{
    if (!m_commitsTable)
        return;
    QTableWidgetItem *sum = m_commitsTable->item(row, kCommitSummaryCol);
    if (!sum || sum->data(kCommitRowKindRole).toInt() != 0)
        return;
    const QString hash = sum->data(Qt::UserRole).toString();
    QSignalBlocker block(m_commitsTable);
    if (sum->data(kCommitExpandedRole).toBool()) {
        while (row + 1 < m_commitsTable->rowCount()) {
            QTableWidgetItem *next =
                m_commitsTable->item(row + 1, kCommitSummaryCol);
            if (!next || next->data(kCommitRowKindRole).toInt() != 1)
                break;
            m_commitsTable->removeRow(row + 1);
        }
        sum->setData(kCommitExpandedRole, false);
        return;
    }
    const QString dir = repoGitDir();
    if (dir.isEmpty() || hash.isEmpty())
        return;
    const QList<CommitFileStat> files = commitFileStats(dir, hash);
    if (files.isEmpty())
        return;



    const QTableWidgetItem *graphIt = m_commitsTable->item(row, kCommitGraphCol);
    const QVariantList lanes =
        graphIt ? graphIt->data(kGraphBottomLanesRole).toList() : QVariantList();
    const int lane = graphIt ? graphIt->data(kGraphNodeLaneRole).toInt() : 0;
    int at = row + 1;
    for (const CommitFileStat &f : files) {
        m_commitsTable->insertRow(at);
        auto *graph = new QTableWidgetItem;
        graph->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        graph->setData(kGraphLanesRole, lanes);
        graph->setData(kGraphBottomLanesRole, lanes);
        graph->setData(kGraphNodeLaneRole, -1);
        m_commitsTable->setItem(at, kCommitGraphCol, graph);
        auto *item = new QTableWidgetItem(f.path);
        item->setIcon(iconForFile(f.path.section(QLatin1Char('/'), -1)));
        item->setData(kCommitRowKindRole, 1);
        item->setData(Qt::UserRole, hash);
        item->setData(kCommitFilePathRole, f.path);
        item->setData(kCommitFileAddsRole, f.adds);
        item->setData(kCommitFileDelsRole, f.dels);
        item->setData(kGraphNodeLaneRole, lane);
        item->setToolTip(
            QString::fromUtf8("%1 \xC2\xB7 +%2 \xE2\x88\x92%3<br>"
                              "Click to open this file's diff")
                .arg(f.path.toHtmlEscaped())
                .arg(f.adds)
                .arg(f.dels));
        m_commitsTable->setItem(at, kCommitSummaryCol, item);
        ++at;
    }
    sum->setData(kCommitExpandedRole, true);
}

void MainWindow::collapseAllCommitFileRows()
{
    if (!m_commitsTable)
        return;
    QSignalBlocker block(m_commitsTable);
    for (int row = m_commitsTable->rowCount() - 1; row >= 0; --row) {
        QTableWidgetItem *sum = m_commitsTable->item(row, kCommitSummaryCol);
        if (!sum)
            continue;
        if (sum->data(kCommitRowKindRole).toInt() == 1)
            m_commitsTable->removeRow(row);
        else
            sum->setData(kCommitExpandedRole, false);
    }
}

void MainWindow::updateCommitRowHover(int row)
{
    if (!m_commitsTable)
        return;
    QTableWidgetItem *sum = m_commitsTable->item(row, kCommitSummaryCol);
    if (!sum || sum->data(kCommitRowKindRole).toInt() != 0)
        return;
    const QTableWidgetItem *author = m_commitsTable->item(row, 0);
    const QTableWidgetItem *date = m_commitsTable->item(row, 1);
    const QTableWidgetItem *hashIt = m_commitsTable->item(row, kCommitHashCol);
    const QTableWidgetItem *filesIt = m_commitsTable->item(row, 3);
    const QTableWidgetItem *addsIt = m_commitsTable->item(row, 4);
    const QTableWidgetItem *delsIt = m_commitsTable->item(row, 5);
    const QString hash = sum->data(Qt::UserRole).toString();
    const QString shortHash =
        hashIt ? hashIt->data(kTableSortRole).toString() : hash.left(8);

    const QString when = date ? date->toolTip() : QString();
    QString html = QStringLiteral("<b>%1</b>").arg(sum->text().toHtmlEscaped());


    QString msgBody = sum->data(kCommitBodyRole).toString();
    if (!msgBody.isEmpty()) {
        if (msgBody.size() > 1500) {
            msgBody.truncate(1500);
            msgBody += QChar(0x2026);
        }
        html += QStringLiteral("<br><span style='color:#8b949e; "
                               "white-space:pre-wrap'>%1</span>")
                    .arg(msgBody.toHtmlEscaped());
    }
    html += QStringLiteral("<br>%1 committed %2")
                .arg((author ? author->text() : QString()).toHtmlEscaped(),
                     when.toHtmlEscaped());
    html += QStringLiteral("<br><code>%1</code>").arg(shortHash.toHtmlEscaped());
    const QStringList refs = sum->data(kCommitRefsRole).toStringList();
    if (!refs.isEmpty())
        html += QStringLiteral("<br><span style='color:#58a6ff'>%1</span>")
                    .arg(refs.join(QString::fromUtf8(" \xC2\xB7 ")).toHtmlEscaped());


    const QString filesTxt = filesIt ? filesIt->text() : QString();
    if (!filesTxt.isEmpty() && filesTxt != QString::fromUtf8("\xC2\xB7")) {
        html += QString::fromUtf8("<br>%1 file%2 changed \xC2\xB7 "
                                  "<span style='color:#3fb950'>%3</span> "
                                  "<span style='color:#f85149'>%4</span>")
                    .arg(filesTxt, filesTxt == QStringLiteral("1") ? "" : "s",
                         addsIt ? addsIt->text() : QString(),
                         delsIt ? delsIt->text() : QString());



        const QStringList preview = sum->data(kCommitFilesRole).toStringList();
        for (const QString &entry : preview) {
            const QStringList cols = entry.split(QLatin1Char('\t'));
            if (cols.isEmpty())
                continue;
            html += QString::fromUtf8(
                        "<br><span style='color:#8b949e'>%1</span> "
                        "<span style='color:#3fb950'>+%2</span> "
                        "<span style='color:#f85149'>\xE2\x88\x92%3</span>")
                        .arg(cols.first().toHtmlEscaped())
                        .arg(cols.value(1).toInt())
                        .arg(cols.value(2).toInt());
        }
        const int total = filesTxt.toInt();
        if (total > preview.size() && !preview.isEmpty())
            html += QString::fromUtf8("<br><span style='color:#8b949e'>"
                                      "and %1 more\xE2\x80\xA6</span>")
                        .arg(total - preview.size());
    }
    switch (commitStatusCode(hash)) {
    case 1:
        html += QString::fromUtf8(
            "<br><span style='color:#3fb950'>Checks passed</span>");
        break;
    case 2:
        html += QString::fromUtf8(
            "<br><span style='color:#f85149'>Checks failed</span>");
        break;
    case 3:
        html += QString::fromUtf8(
            "<br><span style='color:#58a6ff'>Checks running</span>");
        break;
    default:
        break;
    }
    if (sum->data(kCommitUnsyncedRole).toBool())
        html += QString::fromUtf8("<br><span style='color:#d29922'>\xE2\x96\xB2 "
                                  "Not yet synced to the network mirror</span>");
    html += QString::fromUtf8("<br><span style='color:#8b949e'>Click to show "
                              "files \xC2\xB7 double-click for the full "
                              "diff</span>");
    sum->setToolTip(html);
}

void MainWindow::updateCommitsUnsyncedFilesPanel()
{
    const int pending = m_commitsUnsyncedHashes.size();
    if (m_commitsUnsyncedBanner) {
        if (pending > 0)
            showCommitsBanner(
                QString::fromUtf8(
                    "<span style='color:#d29922'>\xE2\x96\xB2 %1 commit%2 pending "
                    "sync to the network mirror.</span> "
                    "<a href='files' style='color:#d29922'>%3</a>")
                    .arg(pending)
                    .arg(pending == 1 ? QString() : QStringLiteral("s"))
                    .arg(m_commitsUnsyncedExpanded
                             ? QString::fromUtf8("Hide files \xE2\x96\xB4")
                             : QString::fromUtf8("Show files \xE2\x96\xBE")));
        else
            hideCommitsBanner();
    }
    if (!m_commitsUnsyncedFiles)
        return;
    if (pending == 0 || !m_commitsUnsyncedExpanded) {
        m_commitsUnsyncedFiles->hide();
        return;
    }
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    m_commitsUnsyncedFiles->clear();


    const int cap = qMin(pending, 20);
    for (int i = 0; i < cap; ++i) {
        const QString hash = m_commitsUnsyncedHashes.at(i);
        QByteArray meta;
        runGitCapture(dir,
                      {QStringLiteral("show"), QStringLiteral("--no-patch"),
                       QStringLiteral("--format=%h%x1f%s"), hash},
                      &meta, nullptr);
        const QStringList mf =
            QString::fromUtf8(meta).trimmed().split(QLatin1Char('\x1f'));
        auto *top = new QTreeWidgetItem(
            m_commitsUnsyncedFiles,
            {QStringLiteral("%1  %2").arg(mf.value(0), mf.value(1))});
        top->setIcon(0, themedOcticon("upload", QColor("#d29922"), 14));
        top->setData(0, Qt::UserRole, hash);
        top->setToolTip(0, QStringLiteral(
                               "Waiting to sync \xE2\x80\x94 click to view the "
                               "commit"));
        const QList<CommitFileStat> files = commitFileStats(dir, hash);
        for (const CommitFileStat &f : files) {
            auto *child = new QTreeWidgetItem(
                top, {QString::fromUtf8("%1   +%2 \xE2\x88\x92%3")
                          .arg(f.path)
                          .arg(f.adds)
                          .arg(f.dels)});
            child->setData(0, Qt::UserRole, hash);
            child->setData(0, Qt::UserRole + 1, f.path);
            child->setToolTip(0, f.path);
        }
        top->setExpanded(true);
    }
    if (pending > cap)
        new QTreeWidgetItem(
            m_commitsUnsyncedFiles,
            {QString::fromUtf8("\xE2\x80\xA6 and %1 more commit%2")
                 .arg(pending - cap)
                 .arg(pending - cap == 1 ? QString() : QStringLiteral("s"))});
    m_commitsUnsyncedFiles->show();
}

void MainWindow::fetchCurrentRepo()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);


    syncRepository(m_repoDetailIndex,  true);
    const bool hasWorkTree = !repo.localPath.isEmpty() &&
                             QDir(repo.localPath).exists(QStringLiteral(".git"));
    if (!hasWorkTree) {
        flashMessage(QStringLiteral("Fetching %1/%2 from the network\xE2\x80\xA6")
                         .arg(repo.owner, repo.name));
        return;
    }
    if (m_commitsFetchButton)
        m_commitsFetchButton->setEnabled(false);

    runGitDetached(repo.localPath,
                   {QStringLiteral("fetch"), QStringLiteral("--all"),
                    QStringLiteral("--prune"), QStringLiteral("--tags")},
                   [this](bool ok, const QByteArray &) {
                       if (m_commitsFetchButton)
                           m_commitsFetchButton->setEnabled(true);
                       flashMessage(ok ? QStringLiteral(
                                             "Fetched the latest history.")
                                       : QStringLiteral("Fetch failed \xE2\x80\x94 "
                                                        "check the remotes."),
                                    !ok);
                       if (ok)
                           loadCommits();
                   });
}

void MainWindow::pullCurrentRepo()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    if (repo.localPath.isEmpty() ||
        !QDir(repo.localPath).exists(QStringLiteral(".git"))) {
        flashMessage(QStringLiteral("Read-only mirror \xE2\x80\x94 no working "
                                    "tree to pull into."),
                     true);
        return;
    }



    QStringList args{QStringLiteral("pull"), QStringLiteral("--ff-only")};
    if (!runGitCapture(repo.localPath,
                       {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                        QStringLiteral("--symbolic-full-name"),
                        QStringLiteral("@{upstream}")},
                       nullptr, nullptr)) {
        if (repo.mirrorPath.isEmpty() || !QDir(repo.mirrorPath).exists()) {
            flashMessage(QStringLiteral("No upstream branch or mirror to pull "
                                        "from."),
                         true);
            return;
        }
        QByteArray branch;
        runGitCapture(repo.localPath,
                      {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                       QStringLiteral("HEAD")},
                      &branch, nullptr);
        args << repo.mirrorPath << QString::fromUtf8(branch).trimmed();
    }
    if (m_commitsPullButton)
        m_commitsPullButton->setEnabled(false);
    runGitDetached(repo.localPath, args,
                   [this](bool ok, const QByteArray &out) {
                       if (m_commitsPullButton)
                           m_commitsPullButton->setEnabled(true);
                       const QString text = QString::fromUtf8(out).trimmed();
                       if (ok)
                           flashMessage(
                               text.contains(QLatin1String("Already up to date"))
                                   ? QStringLiteral("Already up to date.")
                                   : QStringLiteral("Pulled the latest history."));
                       else
                           flashMessage(QStringLiteral(
                                            "Pull failed \xE2\x80\x94 the branch "
                                            "has diverged or has nothing to pull "
                                            "from (fetch first, or resolve by "
                                            "hand)."),
                                        true);
                       if (ok) {
                           refreshSourceControl(true);
                           loadCommits();
                       }
                   });
}










namespace {

constexpr int kGsKindRole = Qt::UserRole;
constexpr int kGsStr1Role = Qt::UserRole + 1;
constexpr int kGsStr2Role = Qt::UserRole + 2;
constexpr int kGsNumRole = Qt::UserRole + 3;
enum GlobalSearchKind {
    GsHeader = 0,
    GsSection,
    GsRepo,
    GsNode,
    GsRelay,
    GsIssue,
    GsPull,
    GsBranch,
    GsFile,
    GsCommit,
    GsDeepSearch,
};
}

QWidget *MainWindow::createGlobalSearchBox()
{
    m_globalSearch = new QLineEdit;
    m_globalSearch->setObjectName("globalSearch");
    m_globalSearch->setClearButtonEnabled(true);
    m_globalSearch->setPlaceholderText(QString::fromUtf8("Search\xE2\x80\xA6"));
    m_globalSearch->setMinimumWidth(150);
    m_globalSearch->setMaximumWidth(360);
    m_globalSearch->addAction(themedOcticon("search", QColor(Theme::kTextTertiary), 14),
                              QLineEdit::LeadingPosition);
    m_globalSearch->setToolTip(QString::fromUtf8(
        "Search everything \xE2\x80\x94 sections, relays, nodes, repositories, and "
        "the open repo's issues, pull requests, branches, files and commits"));
    m_globalSearch->installEventFilter(this);




    m_globalSearchPopup = new QListWidget(this);
    m_globalSearchPopup->setObjectName("globalSearchPopup");
    m_globalSearchPopup->setFocusPolicy(Qt::NoFocus);
    m_globalSearchPopup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_globalSearchPopup->setSelectionMode(QAbstractItemView::SingleSelection);
    m_globalSearchPopup->setMouseTracking(true);
    m_globalSearchPopup->hide();
    connect(m_globalSearchPopup, &QListWidget::itemClicked, this,
            &MainWindow::activateGlobalSearchItem);


    connect(m_globalSearchPopup, &QListWidget::itemEntered, this,
            [this](QListWidgetItem *it) {
                if (it && (it->flags() & Qt::ItemIsSelectable))
                    m_globalSearchPopup->setCurrentItem(it);
            });



    m_globalSearchTimer = new QTimer(this);
    m_globalSearchTimer->setSingleShot(true);
    m_globalSearchTimer->setInterval(140);
    connect(m_globalSearchTimer, &QTimer::timeout, this,
            &MainWindow::rebuildGlobalSearchResults);
    connect(m_globalSearch, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (t.trimmed().isEmpty()) {
            m_globalSearchTimer->stop();
            hideGlobalSearchPopup();


            syncGitCommitFilter();
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


    syncGitCommitFilter();
    const QString needle = m_globalSearch->text().trimmed().toLower();
    if (needle.isEmpty()) {
        hideGlobalSearchPopup();
        return;
    }
    m_globalSearchPopup->clear();

    const QString query = m_globalSearch->text().trimmed();

    int total = 0;
    constexpr int kMaxTotal = 80;

    auto addHeader = [&](const QString &title) {
        auto *h = new QListWidgetItem(title.toUpper());
        h->setData(kGsKindRole, GsHeader);
        h->setFlags(Qt::NoItemFlags);
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



    addResult("search", QColor("#58a6ff"),
              QString::fromUtf8("Search files, commits & history for \xE2\x80\x9C%1\xE2\x80\x9D")
                  .arg(query),
              GsDeepSearch, query, QString(), 0);


    struct Sec { const char *label; const char *icon; int index; };
    static const Sec kSections[] = {
        {"Home / Repositories", "home", 0},
        {"Chat", "comment", 2},
        {"Pings", "bell", 3},
        {"Network log", "list-unordered", 4},
        {"Hosts", "server", 7},
        {"Relays", "broadcast", 8},
        {"Network", "workflow", kNetworkDiagnosticsSectionIndex},
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


    const bool repoOpen =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size();
    if (repoOpen) {
        const QString repoName = m_repositories.at(m_repoDetailIndex).name;
        const QString suffix = QString::fromUtf8(" \xC2\xB7 ") + repoName;


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
        QSignalBlocker block(m_globalSearch);
        m_globalSearch->clear();
    }

    const bool repoOpen =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size();
    auto clickRepoTab = [this](int idx) {
        if (m_repoDetailTabs && m_repoDetailTabs->button(idx))
            m_repoDetailTabs->button(idx)->click();
    };

    switch (kind) {
    case GsDeepSearch:
        openSearchResultsPage(s1);
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








QWidget *MainWindow::createNavHistoryButtons()
{
    auto *holder = new QWidget;
    auto *row = new QHBoxLayout(holder);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);

    auto makeButton = [this](const QString &icon, const QString &tip,
                             void (MainWindow::*slot)()) {
        auto *b = new QPushButton;
        b->setObjectName("navHistoryButton");
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


    here.repoIndex = (here.section == 0) ? m_repoDetailIndex : -1;


    here.detailTab = (here.repoIndex >= 0 && m_repoDetailStack)
                         ? m_repoDetailStack->currentIndex()
                         : -1;
    if (here.detailTab == 0 && m_filesStack && m_filesStack->currentIndex() == 0 &&
        m_overviewBodyStack)
        here.overviewPage = m_overviewBodyStack->currentIndex();
    if (here.overviewPage == 1)
        here.branch = m_repoBranch.isEmpty() ? repoHeadBranch() : m_repoBranch;




    if (here.detailTab >= 0)
        QSettings().setValue(kLastRepoDetailTabSetting, here.detailTab);

    if (m_navHistoryIndex >= 0 && m_navHistoryIndex < m_navHistory.size() &&
        m_navHistory.at(m_navHistoryIndex) == here)
        return;


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


    m_navRestoring = true;
    if (place.section == 0 && place.repoIndex >= 0 &&
        place.repoIndex < m_repositories.size() &&
        place.repoIndex != m_repoDetailIndex) {
        openRepoDetailDeferred(place.repoIndex);



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




void MainWindow::applyNavDetailTab(const NavPlace &place)
{
    if (place.section != 0 || place.repoIndex < 0 || place.detailTab < 0)
        return;
    if (!m_repoDetailTabs || !m_repoDetailStack)
        return;
    if (m_repoDetailStack->currentIndex() != place.detailTab) {
        if (QAbstractButton *b = m_repoDetailTabs->button(place.detailTab))
            b->click(); // switches the tab and loads its data, like a real click
    }
    if (place.detailTab == 0) {
        if (place.overviewPage == 2) {
            showOverviewBranches();
            loadBranchesPanel();
            return;
        }
        if (place.overviewPage == 3) {
            showOverviewWorktrees();
            loadWorktreesPanel();
            return;
        }
        if (place.overviewPage != 1) {
            showOverviewFiles();
            return;
        }
        showOverviewCommits();
        const QString base = repoDefaultBranchFast();
        if (!place.branch.isEmpty() && place.branch != base) {
            switchToBranch(place.branch);
        } else {
            closeBranchCompareView();
            showOverviewCommits();
            if (commitsListIsCurrent())
                refreshSourceControl();
            else
                loadCommits();
        }
        return;
    }
    if (m_repoDetailStack->currentIndex() == place.detailTab)
        return;
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

QString MainWindow::navPlaceLabel(const NavPlace &place) const
{
    QString repo;
    if (place.repoIndex >= 0 && place.repoIndex < m_repositories.size()) {
        const RepositoryRecord &record = m_repositories.at(place.repoIndex);
        repo = record.owner + QLatin1Char('/') + record.name;
    }

    QString destination;
    if (place.section == 0 && place.repoIndex >= 0) {
        if (place.detailTab == 0) {
            switch (place.overviewPage) {
            case 1:
                destination = QStringLiteral("Git · %1")
                                  .arg(place.branch.isEmpty()
                                           ? QStringLiteral("main")
                                           : place.branch);
                break;
            case 2:
                destination = QStringLiteral("Branches");
                break;
            case 3:
                destination = QStringLiteral("Worktrees");
                break;
            default:
                destination = QStringLiteral("Code");
                break;
            }
        } else if (m_repoDetailTabs && m_repoDetailTabs->button(place.detailTab)) {
            destination = m_repoDetailTabs->button(place.detailTab)->text();
        } else {
            destination = QStringLiteral("Repository");
        }
    } else {
        switch (place.section) {
        case 2:
            destination = QStringLiteral("Chat");
            break;
        default:
            destination = QStringLiteral("Home");
            break;
        }
    }

    return repo.isEmpty() ? destination
                          : QStringLiteral("%1 · %2").arg(repo, destination);
}

void MainWindow::updateNavHistoryButtons()
{
    const bool canBack = m_navHistoryIndex > 0;
    const bool canForward = m_navHistoryIndex >= 0 &&
                            m_navHistoryIndex < m_navHistory.size() - 1;
    if (m_navBackButton) {
        m_navBackButton->setEnabled(canBack);
        m_navBackButton->setToolTip(
            canBack ? QStringLiteral("Back to %1")
                          .arg(navPlaceLabel(m_navHistory.at(m_navHistoryIndex - 1)))
                    : QStringLiteral("Back"));
    }
    if (m_navForwardButton) {
        m_navForwardButton->setEnabled(canForward);
        m_navForwardButton->setToolTip(
            canForward ? QStringLiteral("Forward to %1")
                             .arg(navPlaceLabel(m_navHistory.at(m_navHistoryIndex + 1)))
                       : QStringLiteral("Forward"));
    }
}

#ifdef FORKMESH_WINDOW_TESTS
QString MainWindow::testNavBackToolTip() const
{
    return m_navBackButton ? m_navBackButton->toolTip() : QString();
}

QString MainWindow::testNavForwardToolTip() const
{
    return m_navForwardButton ? m_navForwardButton->toolTip() : QString();
}
#endif

namespace {

constexpr int kSrKindRole = Qt::UserRole;
constexpr int kSrPathRole = Qt::UserRole + 1;
constexpr int kSrBaseRole = Qt::UserRole + 2;
constexpr int kSrLineRole = Qt::UserRole + 3;
enum { SrBucket = 0, SrFile = 1, SrCommit = 2 };
enum SearchCat { ScFile = 0, ScMessage, ScHistory };
}




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
            break;
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

    stopSearch();
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

    startSearchProcess(dir, {"grep", "-n", "-I", "-i", "-F", "-e", q, ref},
                       fileBucket, ScFile);

    startSearchProcess(dir,
                       {"log", "-i", "--fixed-strings", "--grep=" + q,
                        "--format=%H%x1f%h%x1f%ar%x1f%s", ref},
                       msgBucket, ScMessage);


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


    auto addLine = [this, bucket, cat, ref, process](const QByteArray &raw) {
        if (bucket->childCount() >= 500) {
            process->kill();
            return;
        }
        const QString line = QString::fromUtf8(raw);
        if (line.trimmed().isEmpty())
            return;
        if (cat == ScFile) {

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
                    addLine(*buffer);
                m_searchProcs.removeOne(process);
                process->deleteLater();
                if (m_searchPending > 0)
                    --m_searchPending;
                updateSearchStatus();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart)
                    return;
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
        item->setExpanded(!item->isExpanded());
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

        QTimer::singleShot(0, this, [this, payload, line, term] {
            auto *edit = qobject_cast<QPlainTextEdit *>(m_openFileTabs.value(payload));
            if (!edit)
                return;
            QList<QTextEdit::ExtraSelection> sels =
                termSelections(edit->document(), term);

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
        disconnect(p, nullptr, this, nullptr);
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


// Switch only the Git view's right pane. The universal source-control composer,
// changes tree, and commit graph stay in place for every diff kind.
void MainWindow::setCommitWorkspacePage(int page)
{
    if (!m_commitsStack)
        return;
    // Commit and range diffs are valid only inside the standalone Git
    // destination. Centralising the transition here makes it impossible for a
    // caller to expose one beneath Code Overview chrome by forgetting the
    // navigation step first.
    if (page == kCommitWorkspaceCommitPage ||
        page == kCommitWorkspaceRangePage)
        showOverviewCommits();
    m_commitsStack->setCurrentIndex(page);
    if (m_gitFilesSlot)
        m_gitFilesSlot->setCurrentIndex(0);
    if (m_gitHistorySlot)
        m_gitHistorySlot->setCurrentIndex(0);
    updateCommitsCompareIndicator();
    updateRepoActivityRail();
}

// The "<branch> -> <base>" compare indicator on the graph's branch row: while a
// branch/PR comparison is open on the right pane, the arrow and the base button
// appear after the branch button, so the branch button reads as the left end of
// the comparison and the base button as the right (adhoc #16). Both are
// dropdowns, so either end can be moved. Hidden on every other page, leaving
// the branch button alone as a plain history-browsing switcher.
void MainWindow::updateCommitsCompareIndicator()
{
    if (!m_commitsCompareBaseButton || !m_commitsCompareArrow)
        return;
    const bool comparing =
        m_commitsStack &&
        m_commitsStack->currentIndex() == kCommitWorkspaceRangePage &&
        !m_branchDiffBranch.isEmpty();
    if (!comparing) {
        m_commitsCompareArrow->hide();
        m_commitsCompareBaseButton->hide();
        return;
    }
    const QString base = branchCompareBase();
    const QString label = base.isEmpty() ? QStringLiteral("(no base)") : base;
    // The button elides long agent branch names; the full story goes on the
    // tooltip (mirrors the branch button beside it).
    if (auto *elider =
            dynamic_cast<ElidingPushButton *>(m_commitsCompareBaseButton))
        elider->setFullText(label);
    else
        m_commitsCompareBaseButton->setText(label);
    m_commitsCompareBaseButton->setToolTip(
        QString::fromUtf8("Comparing %1 against %2 \xE2\x80\x94 the diff on the "
                          "right is everything %1 adds over %2. Click to compare "
                          "against a different branch.")
            .arg(m_branchDiffBranch, label));

    // Rebuild the base dropdown so it lists this repo's branches, ticking the
    // one currently on the right-hand end of the comparison.
    auto *menu = new QMenu(m_commitsCompareBaseButton);
    const QStringList branches = repoBranches();
    for (const QString &b : branches) {
        if (b == m_branchDiffBranch)
            continue; // a branch can't be compared against itself
        QAction *a = menu->addAction(b);
        a->setCheckable(true);
        a->setChecked(b == base);
        connect(a, &QAction::triggered, this,
                [this, b] { setBranchCompareBase(b); });
    }
    if (menu->isEmpty())
        menu->addAction(QStringLiteral("No other branches"))->setEnabled(false);
    QMenu *old = m_commitsCompareBaseButton->menu();
    m_commitsCompareBaseButton->setMenu(menu);
    if (old)
        old->deleteLater();

    m_commitsCompareArrow->show();
    m_commitsCompareBaseButton->show();
}

void MainWindow::showCommitList()
{
    if (!m_commitsStack)
        return;
    // A branch/PR comparison parked on the range page survives commit-list
    // reloads (adhoc #107): it re-renders itself, so only the single-commit page
    // needs resetting to the working-tree changes.
    if (m_commitsStack->currentIndex() == kCommitWorkspaceRangePage)
        return;
    setCommitWorkspacePage(kCommitWorkspaceChangesPage);
}




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


    {
        QSignalBlocker block(m_commitsTable);
        m_commitsTable->setCurrentCell(0, kCommitSummaryCol);
    }
    showCommit(item->data(Qt::UserRole).toString());
}

namespace {







bool isInboxDataPath(const QString &rel)
{
    return rel.startsWith(QLatin1String(".forkmesh/issues/"))
        || rel.startsWith(QLatin1String("pulls/"))
        || rel.startsWith(QLatin1String(".forkmesh/commits/"))
        || rel.startsWith(QLatin1String(".forkmesh/"));
}



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


QStringList rebaseUnmergedFiles(const QString &dir)
{
    const QByteArray out =
        gitCaptureStdout(dir, {"diff", "--name-only", "--diff-filter=U"});
    return QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
}







bool driveDeleteCommitRebase(const QString &dir, QString *err)
{


    for (int step = 0; step < 100000; ++step) {
        if (rebaseMergeDir(dir).isEmpty())
            return true;

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


        for (const QString &f : unmerged) {
            if (runGitCapture(dir, {"checkout", "--theirs", "--", f}, nullptr,
                              nullptr))
                runGitCapture(dir, {"add", "--", f}, nullptr, nullptr);
            else

                runGitCapture(dir, {"rm", "--force", "--", f}, nullptr, nullptr);
        }






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

}





void MainWindow::deleteCommit(const QString &hash)
{
    if (hash.isEmpty() || !repoHasWorkingTree())
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    const QString branch = currentRef();


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



    QByteArray parentsOut;
    if (!runGitCapture(dir, {"rev-list", "--parents", "-n", "1", hash},
                       &parentsOut, nullptr)) {
        setRepoDetailNotice("Could not inspect that commit.", true);
        return;
    }


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






    QString err;
    if (!runGitCapture(dir, {"rebase", "--onto", hash + "^", hash, branch},
                       nullptr, &err)) {
        if (rebaseMergeDir(dir).isEmpty()) {


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





    runGitCapture(dir, {"reflog", "expire", "--expire=now", "--all"}, nullptr,
                  nullptr);
    runGitCapture(dir, {"gc", "--prune=now", "--quiet"}, nullptr, nullptr);

    logSystem(QStringLiteral("Git: removed commit %1 from %2 and pruned it from "
                             "the repository.")
                  .arg(hash.left(8), branch));
    setRepoDetailNotice(QStringLiteral("Removed commit %1 — gone from history.")
                            .arg(hash.left(8)));
    loadCommits();
    refreshRepoSyncIndicators();
}






void MainWindow::revertCommit(const QString &hash)
{
    if (hash.isEmpty() || !repoHasWorkingTree())
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;
    const QString branch = currentRef();


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



    QByteArray parentsOut;
    if (!runGitCapture(dir, {"rev-list", "--parents", "-n", "1", hash},
                       &parentsOut, nullptr)) {
        setRepoDetailNotice("Could not inspect that commit.", true);
        return;
    }


    const QStringList fields = QString::fromUtf8(parentsOut).trimmed().split(
        QLatin1Char(' '), Qt::SkipEmptyParts);
    if (fields.size() > 2) {
        setRepoDetailNotice(
            "Can't revert a merge commit this way (it has two parents).", true);
        return;
    }






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
    refreshRepoSyncIndicators();
}

void MainWindow::showCommitsBanner(const QString &html)
{
    if (!m_commitsUnsyncedBanner)
        return;
    if (m_commitsBannerFade)
        m_commitsBannerFade->stop();
    if (m_commitsBannerOpacity)
        m_commitsBannerOpacity->setOpacity(1.0);
    m_commitsUnsyncedBanner->setText(html);
    m_commitsUnsyncedBanner->show();
}

void MainWindow::hideCommitsBanner()
{

    if (m_commitsUnsyncedFiles)
        m_commitsUnsyncedFiles->hide();
    if (!m_commitsUnsyncedBanner || m_commitsUnsyncedBanner->isHidden())
        return;


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


    collapseAllCommitFileRows();
    const QString needle = query.trimmed().toLower();






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
        openCommitReference(href.mid(13).toInt());
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

        const QString rest = href.mid(QStringLiteral("forkmesh://").size());
        const QString kind = rest.section(QLatin1Char('/'), 0, 0);
        const QString id =
            rest.section(QLatin1Char('/'), -1).section(QLatin1Char('#'), 0, 0);
        if (kind == QLatin1String("issue")) {
            if (m_repoDetailTabs && m_repoDetailTabs->button(2))
                m_repoDetailTabs->button(2)->click();
            reloadIssuesInBackground();
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



    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite())
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return;



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
        QSet<int> seenInThisCommit;
        while (it.hasNext()) {
            const int number = it.next().captured(1).toInt();
            if (number <= 0 || seenInThisCommit.contains(number))
                continue;
            seenInThisCommit.insert(number);

            const Issue *issue = byNumber.value(number, nullptr);
            if (!issue || issue->status == QLatin1String("closed"))
                continue;



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




    if (m_commitsTable && m_currentCommitRow >= 0 &&
        m_pendingCommitFileScroll.isEmpty()) {
        QSignalBlocker blk(m_commitsTable);
        m_commitsTable->selectRow(m_currentCommitRow);
    }





    const int gen = ++m_commitLoadGen;



    setCommitWorkspacePage(kCommitWorkspaceCommitPage);
    startCommitDiffSpin();

    m_currentCommitHash = hash;
    if (m_commitDownloadButton)
        m_commitDownloadButton->setEnabled(true);
    if (m_commitDeleteButton || m_commitRevertButton) {



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





static void refreshCommitMessageLabel(QLabel *label)
{
    if (!label)
        return;
    const QString subject = label->property("subjectHtml").toString();
    const QString body = label->property("bodyHtml").toString();
    const bool expanded = label->property("expanded").toBool();
    QString msg;
    if (!body.isEmpty())
        msg += QStringLiteral("<a href='toggle-msg' "
                              "style='color:#8b949e;text-decoration:none'>%1</a> ")
                   .arg(expanded ? QString::fromUtf8("\xE2\x96\xBE")
                                 : QString::fromUtf8("\xE2\x96\xB8"));
    msg += QStringLiteral("<b>%1</b>").arg(subject);
    if (expanded && !body.isEmpty())
        msg += QStringLiteral(
                   "<br><span style='color:#8b949e; white-space:pre-wrap'>%1</span>")
                   .arg(body);
    label->setText(msg);
}



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


    if (m_commitTitle) {
        QString breakableHash;
        breakableHash.reserve(fullHash.size() + fullHash.size() / 8 * 7);
        for (int i = 0; i < fullHash.size(); ++i) {
            breakableHash += fullHash.mid(i, 1).toHtmlEscaped();
            if ((i + 1) % 8 == 0 && i + 1 < fullHash.size())
                breakableHash += QStringLiteral("&#8203;");
        }
        m_commitTitle->setText(
            QStringLiteral("Commit <code>%1</code>").arg(breakableHash));
    }
    if (m_commitMessage) {


        m_commitMessage->setProperty("subjectHtml",
                                     linkifyIssueRefs(subject.toHtmlEscaped()));
        m_commitMessage->setProperty("bodyHtml",
                                     linkifyIssueRefs(body.toHtmlEscaped()));
        m_commitMessage->setProperty("expanded", false);
        m_commitMessage->setToolTip(
            body.isEmpty() ? subject
                           : subject + QStringLiteral("\n\n") + body);
        refreshCommitMessageLabel(m_commitMessage);
    }


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


    if (m_commitDiffView) {
        setDiffHtml(m_commitDiffView,
                    diffHtml.isEmpty()
                        ? QStringLiteral("<p style='color:#8b949e'>"
                                         "No changes in this commit.</p>")
                        : diffHtml);
    }




    if (m_commitDiffView && !m_pendingCommitFileScroll.isEmpty()) {
        for (const DiffFileEntry &f : files) {
            if (f.path == m_pendingCommitFileScroll) {

                flushDiffStream(m_commitDiffView);
                m_commitDiffView->scrollToAnchor(f.anchor);
                break;
            }
        }
        m_pendingCommitFileScroll.clear();
    }

    stopCommitDiffSpin();
    setCommitWorkspacePage(kCommitWorkspaceCommitPage);
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














    const int windowDays =
        m_insightsRangeCombo ? m_insightsRangeCombo->currentData().toInt() : 0;
    constexpr int kBuckets = 32;

    struct Contributor {
        QString name;
        int commits = 0;
        QVector<int> buckets;
    };
    QHash<QString, int> indexByName;
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

        for (const Contributor &c : std::as_const(contributors)) {
            const int row = m_insightsContributors->rowCount();
            m_insightsContributors->insertRow(row);

            const QString openHint =
                QStringLiteral("Click to view %1's commits").arg(c.name);
            auto *nameItem = new QTableWidgetItem(c.name);
            nameItem->setData(Qt::UserRole, c.name);
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






void MainWindow::openCommitsForContributor(const QString &author)
{
    const QString name = author.trimmed();
    if (name.isEmpty())
        return;
    showOverviewCommits();
    if (m_globalSearch) {
        m_globalSearch->setText(name);
        m_globalSearch->setFocus();
    }
    if (m_commitSearch)
        m_commitSearch->setText(name);
}





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


    QByteArray statusOut;
    if (runGitCapture(dir, {"status", "--porcelain"}, &statusOut, nullptr) &&
        !QString::fromUtf8(statusOut).trimmed().isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("Reassign attribution"),
            QStringLiteral("Commit or stash your local changes first \xE2\x80\x94 rewriting "
                           "history requires a clean working tree."));
        return;
    }



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




    loadCommits();
}



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






    auto *fediGroup = new QGroupBox("ActivityPub federation", &dialog);
    auto *fediLayout = new QVBoxLayout(fediGroup);
    auto *federateBox = new QCheckBox(
        "Federate this repository (fediverse actor and handle)", fediGroup);
    auto *broadcastBox = new QCheckBox(
        "Post new issues, pull requests, discussions and releases to followers",
        fediGroup);
    auto *commentsBox = new QCheckBox(
        "Accept fediverse replies as federated comments", fediGroup);
    for (QCheckBox *box : {federateBox, broadcastBox, commentsBox}) {
        box->setChecked(true);
        box->setEnabled(false);
        fediLayout->addWidget(box);
    }
    auto fediLoaded = std::make_shared<bool>(false);
    if (repo.publishToNetwork) {
        const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
        const QString name =
            repoSegment(repo.name, QStringLiteral("repository"));
        QNetworkReply *reply = m_networkAccess->get(
            QNetworkRequest(repoAboutApiUrl(owner, name)));
        QPointer<QCheckBox> fedPtr(federateBox);
        QPointer<QCheckBox> broadPtr(broadcastBox);
        QPointer<QCheckBox> comPtr(commentsBox);
        connect(reply, &QNetworkReply::finished, this,
                [reply, fedPtr, broadPtr, comPtr, fediLoaded] {
                    reply->deleteLater();
                    if (!fedPtr || !broadPtr || !comPtr ||
                        reply->error() != QNetworkReply::NoError)
                        return;
                    const QJsonObject settings =
                        QJsonDocument::fromJson(reply->readAll())
                            .object()
                            .value(QStringLiteral("fediverse"))
                            .toObject()
                            .value(QStringLiteral("settings"))
                            .toObject();
                    if (settings.isEmpty())
                        return;
                    fedPtr->setChecked(
                        settings.value(QStringLiteral("federate")).toBool(true));
                    broadPtr->setChecked(
                        settings.value(QStringLiteral("broadcastEvents"))
                            .toBool(true));
                    comPtr->setChecked(
                        settings.value(QStringLiteral("acceptComments"))
                            .toBool(true));
                    for (QCheckBox *box :
                         {fedPtr.data(), broadPtr.data(), comPtr.data()})
                        box->setEnabled(true);
                    *fediLoaded = true;
                });
    } else {
        fediGroup->setToolTip(
            "Publish this repository to the network to give it a fediverse "
            "presence.");
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save |
                                         QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, &dialog,
                                                         descriptionEdit,
                                                         websiteEdit,
                                                         federateBox,
                                                         broadcastBox,
                                                         commentsBox,
                                                         fediLoaded] {
        QString error;
        if (!saveRepoAboutMetadata(descriptionEdit->toPlainText(),
                                   websiteEdit->text(), &error)) {
            QMessageBox::warning(&dialog, "Edit repository details",
                                 error.isEmpty()
                                     ? QStringLiteral("Could not save details.")
                                     : error);
            return;
        }


        if (*fediLoaded && m_repoDetailIndex >= 0 &&
            m_repoDetailIndex < m_repositories.size()) {
            const RepositoryRecord &current =
                m_repositories.at(m_repoDetailIndex);
            saveRepoFediverseSettings(
                repoSegment(current.owner, QStringLiteral("owner")),
                repoSegment(current.name, QStringLiteral("repository")),
                federateBox->isChecked(), broadcastBox->isChecked(),
                commentsBox->isChecked());
        }
        dialog.accept();
    });

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    layout->addLayout(form);
    layout->addWidget(fediGroup);
    layout->addWidget(buttons);
    dialog.resize(520, 340);
    dialog.exec();
}

QUrl MainWindow::repoAboutApiUrl(const QString &owner, const QString &name) const
{
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/repo/%1/%2/about").arg(owner, name));
    return url;
}

void MainWindow::saveRepoFediverseSettings(const QString &owner,
                                           const QString &name,
                                           bool federate, bool broadcastEvents,
                                           bool acceptComments)
{
    if (owner.isEmpty() || name.isEmpty())
        return;
    if (!hasOwnerSigningCapability(owner))
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        logSystem("Fediverse: could not load identity to save federation "
                  "settings.");
        return;
    }



    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-repo-about-v1\n" + owner + "\n" + name + "\n" + ts).toUtf8();
    QJsonObject settings;
    settings.insert(QStringLiteral("federate"), federate);
    settings.insert(QStringLiteral("broadcastEvents"), broadcastEvents);
    settings.insert(QStringLiteral("acceptComments"), acceptComments);
    QJsonObject body;
    body.insert(QStringLiteral("ts"), ts);
    body.insert(QStringLiteral("ownerSig"),
                m_profileIdentity.signData(canonical));
    body.insert(QStringLiteral("fediverse"), settings);
    QNetworkRequest request(repoAboutApiUrl(owner, name));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, owner, name] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            logSystem(QStringLiteral(
                          "Fediverse: saved federation settings for %1/%2.")
                          .arg(owner, name));
        else
            logSystem(QStringLiteral("Fediverse: could not save federation "
                                     "settings for %1/%2 (%3).")
                          .arg(owner, name, reply->errorString()));
    });
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
    if (!applyRepoAboutMetadataAt(m_repoDetailIndex, about, websiteInput, error))
        return false;
    setRepoDetailNotice(QStringLiteral("Updated repository details."));
    return true;
}




bool MainWindow::applyRepoAboutMetadataAt(int index, const QString &about,
                                          const QString &websiteInput,
                                          QString *error)
{
    if (index < 0 || index >= m_repositories.size()) {
        if (error)
            *error = QStringLiteral("Unknown repository.");
        return false;
    }
    if (m_repositories.at(index).localPath.isEmpty() ||
        !QDir(m_repositories.at(index).localPath)
             .exists(QStringLiteral(".git"))) {
        if (error)
            *error = QStringLiteral(
                "This repository has no local working copy on this node.");
        return false;
    }

    const QString website = normalizedRepoWebsite(websiteInput, error);
    if (!websiteInput.trimmed().isEmpty() && website.isEmpty())
        return false;

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



    const QString legacyPath = QDir(repo.localPath).filePath("info.json");
    if (legacyPath != infoPath && QFileInfo::exists(legacyPath))
        QFile::remove(legacyPath);

    repo.description = aboutText;
    saveRepositories();
    if (index == m_repoDetailIndex) {
        m_repoInfo.about = aboutText;
        m_repoInfo.website = website;
    }
    refreshRepositoryList();
    if (repo.publishToNetwork)
        publishRepositoryAfterMirrorRefresh(index, false);
    logSystem(QStringLiteral("Updated About details for %1/%2.")
                  .arg(repo.owner, repo.name));
    return true;
}

void MainWindow::setRepoBranch(const QString &branch)
{
    m_repoBranch = branch;
    if (m_branchButton)
        m_branchButton->setText(branch);
    updateFooterCommitInfo();
    loadRepoOverview(QString());
    loadCommits();
}


QString MainWindow::repoHeadBranch() const
{
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return QString();
    QByteArray out;
    if (!runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &out, nullptr))
        return QString();
    const QString b = QString::fromUtf8(out).trimmed();
    return b == QLatin1String("HEAD") ? QString() : b;
}










void MainWindow::refreshCommitsBranchButton()
{
    if (!m_commitsBranchButton)
        return;


    const QString browsed = m_repoBranch.isEmpty() ? repoHeadBranch() : m_repoBranch;
    const QString label =
        browsed.isEmpty() ? QStringLiteral("(detached)") : browsed;

    if (auto *elider = dynamic_cast<ElidingPushButton *>(m_commitsBranchButton))
        elider->setFullText(label);
    else
        m_commitsBranchButton->setText(label);
    m_commitsBranchButton->setToolTip(
        QString::fromUtf8("%1 \xE2\x80\x94 click to open another branch (its "
                          "history here, its diff against the base on the "
                          "right) or create one")
            .arg(label));
    // The compare indicator beside it names the base end of the comparison and
    // only shows while one is open (adhoc #16).
    updateCommitsCompareIndicator();

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
        // switchToBranch, not setRepoBranch: picking a branch here opens the
        // combined view — its history in this graph plus its diff against the
        // compare base on the right pane (adhoc #16).
        connect(a, &QAction::triggered, this, [this, b] { switchToBranch(b); });
    }
    if (branches.isEmpty())
        menu->addAction(QStringLiteral("No branches"))->setEnabled(false);

    QMenu *old = m_commitsBranchButton->menu();
    m_commitsBranchButton->setMenu(menu);
    if (old)
        old->deleteLater();
}


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
    m_branchesCache.clear();
    setRepoBranch(name);
    refreshCommitsBranchButton();
}



QStringList MainWindow::listRepoBranches(const QString &dir)
{
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
    return branches;
}

QStringList MainWindow::repoBranches() const
{
    const QString dir = repoGitDir();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (dir == m_branchesCacheDir && !m_branchesCache.isEmpty() &&
        now - m_branchesCacheTime < 5) {
        return m_branchesCache;
    }
    const QStringList branches = listRepoBranches(dir);
    m_branchesCacheDir = dir;
    m_branchesCache = branches;
    m_branchesCacheTime = now;
    return branches;
}



QString MainWindow::chooseDefaultBranch(const QStringList &branches,
                                        const QString &configured,
                                        const QString &dir,
                                        const QString &checkedOut)
{
    if (!configured.isEmpty() && branches.contains(configured))
        return configured;
    if (branches.contains(QStringLiteral("main")))
        return QStringLiteral("main");
    if (branches.contains(QStringLiteral("master")))
        return QStringLiteral("master");

    QByteArray head;
    if (!dir.isEmpty() &&
        runGitCapture(dir, {"symbolic-ref", "--short", "HEAD"}, &head, nullptr)) {
        const QString branch = QString::fromUtf8(head).trimmed();
        if (branches.contains(branch))
            return branch;
    }
    if (!checkedOut.isEmpty() && branches.contains(checkedOut))
        return checkedOut;
    return branches.isEmpty() ? QString() : branches.first();
}

QString MainWindow::repoDefaultBranch(const QStringList &branches) const
{







    return chooseDefaultBranch(branches, m_repoInfo.defaultBranch.trimmed(),
                               repoGitDir(), m_repoBranch);
}

QString MainWindow::repoDefaultBranchFast() const
{

    const QString configured = m_repoInfo.defaultBranch.trimmed();
    if (!configured.isEmpty())
        return configured;





    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return QString();











    const qint64 now = QDateTime::currentSecsSinceEpoch();
    auto stamp = [&dir](const QString &leaf) {
        return QString::number(QFileInfo(QDir(dir).filePath(leaf))
                                   .lastModified()
                                   .toMSecsSinceEpoch());
    };
    const QString fingerprint = stamp(QStringLiteral("refs/heads")) +
                                QLatin1Char('|') +
                                stamp(QStringLiteral("packed-refs")) +
                                QLatin1Char('|') + stamp(QStringLiteral("HEAD"));
    if (dir == m_defaultBranchFastCacheDir &&
        !m_defaultBranchFastCache.isEmpty() &&
        (now - m_defaultBranchFastCacheTime < 5 ||
         fingerprint == m_defaultBranchFastCacheSig)) {
        return m_defaultBranchFastCache;
    }




    QStringList branches;
    QByteArray probe;
    if (runGitCapture(dir,
                      {"for-each-ref", "--format=%(refname:short)",
                       "refs/heads/main", "refs/heads/master"},
                      &probe, nullptr)) {
        for (const QString &line :
             QString::fromUtf8(probe).split('\n', Qt::SkipEmptyParts))
            branches.append(line.trimmed());
    }
    if (!branches.contains(QStringLiteral("main")) &&
        !branches.contains(QStringLiteral("master"))) {
        branches.clear();
        QByteArray out;
        if (runGitCapture(dir,
                          {"for-each-ref", "--format=%(refname:short)", "refs/heads/"},
                          &out, nullptr)) {
            for (const QString &line : QString::fromUtf8(out).split('\n')) {
                const QString branch = line.trimmed();
                if (!branch.isEmpty() && !branches.contains(branch))
                    branches.append(branch);
            }
        }
    }
    const QString base = repoDefaultBranch(branches);
    m_defaultBranchFastCacheDir = dir;
    m_defaultBranchFastCache = base;
    m_defaultBranchFastCacheTime = now;
    m_defaultBranchFastCacheSig = fingerprint;
    return base;
}

void MainWindow::loadBranchesAndTags()
{

















    m_repoBranch = repoDefaultBranchFast();
    if (m_branchButton) {
        m_branchButton->setText(
            m_repoBranch.isEmpty() ? QStringLiteral("HEAD") : m_repoBranch);
        m_branchButton->setToolTip(QStringLiteral("Switch branch"));
    }





    if (m_repoDetailStack) {
        const int current = m_repoDetailStack->currentIndex();


        if (current == 0 && m_overviewBodyStack &&
            m_overviewBodyStack->currentIndex() == 2)
            loadBranchesPanel();

        else if (current == 0 && m_overviewBodyStack &&
                 m_overviewBodyStack->currentIndex() == 3)
            loadWorktreesPanel();
        else if (current == m_releasesTabIndex)
            loadReleasesPanel();
    }



    if (m_branchesTagsLoading) {
        m_branchesTagsReloadQueued = true;
        return;
    }
    m_branchesTagsLoading = true;

    BranchesTagsSnapshot snap;
    snap.dir = repoGitDir();
    snap.localPath =
        (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
            ? m_repositories.at(m_repoDetailIndex).localPath
            : QString();
    snap.configuredDefault = m_repoInfo.defaultBranch.trimmed();
    snap.checkedOut = m_repoBranch;

    runOffThread<BranchesTagsSnapshot>(
        [snap] { return readBranchesTagsGit(snap); },
        [this](BranchesTagsSnapshot loaded) {
            m_branchesTagsLoading = false;


            if (loaded.dir == repoGitDir())
                applyBranchesTags(loaded);
            if (m_branchesTagsReloadQueued) {
                m_branchesTagsReloadQueued = false;
                loadBranchesAndTags();
            }
        });
}



MainWindow::BranchesTagsSnapshot
MainWindow::readBranchesTagsGit(BranchesTagsSnapshot snap)
{
    const QString &dir = snap.dir;
    if (dir.isEmpty())
        return snap;

    snap.branches = listRepoBranches(dir);
    snap.base = chooseDefaultBranch(snap.branches, snap.configuredDefault, dir,
                                    snap.checkedOut);



    QByteArray head;
    if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"}, &head, nullptr)) {
        const QString b = QString::fromUtf8(head).trimmed();
        if (b != QLatin1String("HEAD"))
            snap.head = b;
    }




    QByteArray wtOut;
    if (!snap.localPath.isEmpty() &&
        runGitCapture(snap.localPath, {"worktree", "list", "--porcelain"}, &wtOut,
                      nullptr)) {
        QString curBranch;
        auto flush = [&] {
            if (curBranch != QLatin1String("forkmesh/pulls"))
                ++snap.worktreeCount;
        };
        bool inEntry = false;
        for (const QString &raw : QString::fromUtf8(wtOut).split(QLatin1Char('\n'))) {
            const QString line = raw.trimmed();
            if (line.isEmpty()) {
                if (inEntry) flush();
                inEntry = false;
                curBranch.clear();
                continue;
            }
            if (line.startsWith(QLatin1String("worktree ")))
                inEntry = true;
            else if (line.startsWith(QLatin1String("branch ")))
                curBranch = line.mid(7).replace(QLatin1String("refs/heads/"),
                                                QString());
        }
        if (inEntry) flush();
    }


    QByteArray remoteOut;
    if (runGitCapture(dir, {"remote", "-v"}, &remoteOut, nullptr)) {
        for (const QString &raw :
             QString::fromUtf8(remoteOut).split('\n', Qt::SkipEmptyParts)) {
            const int tab = raw.indexOf('\t');
            if (tab <= 0)
                continue;
            const QString name = raw.left(tab).trimmed();
            QString rest = raw.mid(tab + 1).trimmed();
            const bool isPush = rest.endsWith(QLatin1String("(push)"));
            rest.remove(QLatin1String("(fetch)"));
            rest.remove(QLatin1String("(push)"));
            rest = rest.trimmed();
            auto it = std::find_if(snap.remotes.begin(), snap.remotes.end(),
                                   [&name](const BranchesTagsSnapshot::Remote &e) {
                                       return e.name == name;
                                   });
            if (it == snap.remotes.end()) {
                snap.remotes.append({name, QString(), QString()});
                it = snap.remotes.end() - 1;
            }
            if (isPush)
                it->pushUrl = rest;
            else
                it->fetchUrl = rest;
        }
    }






    QByteArray tags;
    if (runGitCapture(dir, {"tag", "--sort=-creatordate"}, &tags, nullptr)) {
        for (const QString &line : QString::fromUtf8(tags).split('\n'))
            if (!line.trimmed().isEmpty())
                ++snap.tagCount;
    }
    return snap;
}



void MainWindow::applyBranchesTags(const BranchesTagsSnapshot &snap)
{



    if (!snap.branches.isEmpty()) {
        m_branchesCacheDir = snap.dir;
        m_branchesCache = snap.branches;
        m_branchesCacheTime = QDateTime::currentSecsSinceEpoch();
    }




    if (!snap.base.isEmpty() && m_repoBranch == snap.checkedOut &&
        m_repoBranch != snap.base)
        m_repoBranch = snap.base;
    if (m_branchButton) {
        const QString label =
            m_repoBranch.isEmpty() ? QStringLiteral("HEAD") : m_repoBranch;
        m_branchButton->setText(label);




        m_branchButton->setToolTip(
            snap.head.isEmpty() || snap.head == m_repoBranch
                ? QStringLiteral("Switch branch")
                : QStringLiteral("Browsing %1 — the checkout is on %2")
                      .arg(label, snap.head));
    }

    // Counts ride each button's icon corner as rail-style badges (adhoc #6);
    // the captions stay the fixed words set at construction.
    if (m_branchesButton) {
        if (auto *b = dynamic_cast<VerticalIconButton *>(m_branchesButton))
            b->setBadgeCount(snap.branches.size());
        m_branchesButton->setEnabled(!snap.branches.isEmpty());
    }
    if (m_repoBranchesTab)
        m_repoBranchesTab->setText(
            QStringLiteral("Branches (%1)").arg(formatCount(snap.branches.size())));

    if (auto *b = dynamic_cast<VerticalIconButton *>(m_worktreesButton))
        b->setBadgeCount(snap.worktreeCount);



    if (m_remotesButton) {
        if (auto *b = dynamic_cast<VerticalIconButton *>(m_remotesButton))
            b->setBadgeCount(snap.remotes.size());
        auto *menu = new QMenu(m_remotesButton);
        menu->setToolTipsVisible(true);
        for (const BranchesTagsSnapshot::Remote &remote : snap.remotes) {
            const QString url =
                remote.fetchUrl.isEmpty() ? remote.pushUrl : remote.fetchUrl;
            QAction *action = menu->addAction(
                url.isEmpty() ? remote.name
                              : QStringLiteral("%1 \xE2\x80\x94 %2")
                                    .arg(remote.name, url),
                this, [this, name = remote.name, url] {
                    if (url.isEmpty())
                        return;
                    QApplication::clipboard()->setText(url);
                    setRepoDetailNotice(
                        QStringLiteral("Copied %1 URL: %2").arg(name, url));
                });
            if (remote.pushUrl.isEmpty() || remote.pushUrl == remote.fetchUrl)
                action->setToolTip(url);
            else
                action->setToolTip(QStringLiteral("fetch %1\npush %2")
                                       .arg(remote.fetchUrl, remote.pushUrl));
        }
        if (snap.remotes.isEmpty())
            menu->addAction("No remotes")->setEnabled(false);
        menu->addSeparator();
        menu->addAction("Manage remotes\xE2\x80\xA6", this, [this] {

            if (m_settingsTabIndex >= 0 && m_repoDetailTabs &&
                m_repoDetailTabs->button(m_settingsTabIndex)) {
                m_repoDetailTabs->button(m_settingsTabIndex)->setChecked(true);
                m_repoDetailStack->setCurrentIndex(m_settingsTabIndex);
                refreshRepoSettings();
            }
        });
        QMenu *old = m_remotesButton->menu();
        m_remotesButton->setMenu(menu);
        if (old)
            old->deleteLater();
    }


    if (m_branchButton) {
        auto *menu = new QMenu(m_branchButton);
        for (const QString &branchName : snap.branches)
            menu->addAction(branchName, this,
                            [this, branchName] { setRepoBranch(branchName); });
        if (menu->isEmpty())
            menu->addAction("No branches")->setEnabled(false);
        QMenu *old = m_branchButton->menu();
        m_branchButton->setMenu(menu);
        if (old)
            old->deleteLater();
    }

    if (auto *b = dynamic_cast<VerticalIconButton *>(m_tagsButton))
        b->setBadgeCount(snap.tagCount);
    if (auto *b = dynamic_cast<VerticalIconButton *>(m_repoReleasesTab))
        b->setBadgeCount(snap.tagCount);
}

bool MainWindow::repoHasWorkingTree() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return false;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    return !repo.previewOnly && !repo.localPath.isEmpty() &&
           QDir(repo.localPath).exists(".git");
}







void MainWindow::ensureRepoDetailSectionBuilt()
{
    if (m_repoDetailStack)
        return;

    QWidget *placeholder = m_repoDetailSection;
    QWidget *home = placeholder ? placeholder->parentWidget() : nullptr;
    QLayout *homeLayout = home ? home->layout() : nullptr;
    QWidget *detail = buildRepoDetailSection();
    if (homeLayout && placeholder)
        homeLayout->replaceWidget(placeholder, detail);
    else if (homeLayout)
        homeLayout->addWidget(detail);
    m_repoDetailSection = detail;
    if (placeholder)
        placeholder->deleteLater();
}

void MainWindow::ensureRepoDetailTabBuilt(int index)
{
    if (!m_repoDetailStack || index < 0 ||
        index >= m_repoDetailStack->count())
        return;
    QWidget *placeholder = m_repoDetailStack->widget(index);
    if (!placeholder ||
        !placeholder->property("forkmeshDeferredRepoTab").toBool())
        return;
    QElapsedTimer buildTimer;
    buildTimer.start();
    const forkmesh::BackgroundScope buildAction(
        QStringLiteral("ui-build"),
        QStringLiteral("build repository tab %1").arg(index),
        forkmesh::ActionTelemetry::Execution::UiBlocking);

    QWidget *page = nullptr;
    if (index == 0)
        page = buildRepoFilesPanel();
    else if (index == 2)
        page = buildIssuesSection();
    else if (index == 3)
        page = buildAgentsTab();
    else if (index == 4)
        page = buildPullsTab();
    else if (index == 5)
        page = buildDiscussionsTab();
    else if (index == 6)
        page = buildRepoActionsTab();
    else if (index == 7)
        page = buildRepoSecurityTab();
    else if (index == 8)
        page = buildRepoQualityTab();
    else if (index == m_insightsTabIndex)
        page = buildInsightsTab();
    else if (index == m_releasesTabIndex)
        page = buildReleasesTab();
    else if (index == m_mirrorNodesTabIndex)
        page = buildMirrorNodesTab();
    else if (index == m_artifactsTabIndex)
        page = buildArtifactsTab();
    else if (index == m_shortcutsTabIndex)
        page = buildShortcutsTab();
    else if (index == m_settingsTabIndex)
        page = buildRepoSettingsTab();
    else if (index == m_projectsTabIndex)
        page = buildProjectsSection();
    else if (index == m_sizeMapTabIndex)
        page = buildSizeMapTab();
    if (!page)
        return;

    const bool wasCurrent = m_repoDetailStack->currentIndex() == index;
    m_repoDetailStack->insertWidget(index, page);
    m_repoDetailStack->removeWidget(placeholder);
    placeholder->deleteLater();
    if (wasCurrent)
        m_repoDetailStack->setCurrentIndex(index);



    if (index == 2)
        refreshIssuesRepoCombo();
    logStartup(QStringLiteral("  repo tab %1 built in %2ms")
                   .arg(index)
                   .arg(buildTimer.elapsed()));
}

QWidget *MainWindow::buildRepoDetailSection()
{
    auto *page = new QWidget;





    m_repoHeaderTitle = new QLabel("Repository");
    m_repoHeaderTitle->setObjectName("repoHeaderTitle");
    m_repoHeaderTitle->setTextFormat(Qt::RichText);
    m_repoHeaderTitle->hide();




    auto *notifyButton =
        new VerticalIconButton("Notify", VerticalIconButton::Action);
    m_notifyButton = notifyButton;
    notifyButton->setToolTip("Pings");
    setOcticon(notifyButton, "bell", 16);
    m_forkButton = new VerticalIconButton("Fork", VerticalIconButton::Action);
    m_mirrorButton =
        new VerticalIconButton("Mirror", VerticalIconButton::Action);
    m_sourceButton =
        new VerticalIconButton("Source", VerticalIconButton::Action);


    m_repoOpenButton =
        new VerticalIconButton("Open", VerticalIconButton::Action);
    for (QPushButton *b :
         {static_cast<QPushButton *>(notifyButton), m_forkButton,
          m_mirrorButton, m_sourceButton, m_repoOpenButton}) {
        b->setObjectName("repoActionStack");
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


    connect(m_forkButton, &QPushButton::clicked, this,
            &MainWindow::forkCurrentRepo);
    connect(m_mirrorMenu, &QMenu::aboutToShow, this,
            &MainWindow::updateRepoActionMenus);
    connect(m_sourceMenu, &QMenu::aboutToShow, this,
            &MainWindow::updateRepoActionMenus);

    // No dedicated header row any more (adhoc #6): the owner/repo switcher
    // moved up onto the window-chrome line (buildBreadcrumb, between the
    // instance logo and the SOL balance) and the action cluster (Notify / Fork
    // / Mirror / Source / Open) rides the right end of the tab row below.

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


    metaBand->hide();


    struct TabDef {
        const char *label;
        const char *icon;
    };



    const QList<TabDef> tabs = {{"Code", "code"},
                                {"Commits", "git-branch"},
                                {"Issues", "issue-opened"},
                                {"Agents", "terminal"},
                                {"PRs", "git-pull-request"},
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
                                {"Settings", "gear"},



                                {"Projects", "list-unordered"},



                                {"Size map", "pie-chart"}};
    m_repoDetailTabs = new QButtonGroup(this);
    m_repoDetailTabs->setExclusive(true);
    auto *tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(12, 0, 12, 0);
    tabRow->setSpacing(10);
    for (int i = 0; i < tabs.size(); ++i) {
        // Commits (id 1) no longer gets a top-bar tab: the Git workspace is
        // reached only from the activity rail.
        // Agents (id 3) also no longer gets a top-bar tab (adhoc #178) — it's
        // reached via the footer status strip, spinner overlays and issue/PR
        // links instead. Branches (id 10) likewise lost its top-bar tab: its
        // panel now lives inside the Code overview, toggled by the toolbar's
        // "N branches" button. Worktrees (id 11) followed Branches into the Code
        // overview (adhoc #170), toggled by the toolbar's "N worktrees" button.
        // All four entries stay in the list so every later tab keeps its
        // positional id.
        if (i == 1 || i == 3 || i == 10 || i == 11)
            continue;
        const TabDef tab = tabs.at(i);
        // Releases (id 12) lives in the Code overview's mode row next to Tags
        // (adhoc #180), not the tab row, but takes the same form as every real
        // tab: icon stacked over a small caption (adhoc #91), the same shape
        // as the activity rail, so both rows read compact.
        QPushButton *b = new VerticalIconButton(QString::fromLatin1(tab.label),
                                                VerticalIconButton::Tab);
        b->setObjectName("repoTab");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        setOcticon(b, QString::fromLatin1(tab.icon), 16);
        if (i == 0)
            b->setChecked(true);
        if (i == 0)
            m_repoCodeTab = b;
        if (i == 2)
            m_repoIssuesTab = b;


        if (i == 4)
            m_repoPullsTab = b;
        if (i == 5)
            m_repoDiscussionsTab = b;
        if (i == 6)
            m_repoActionsTab = b;


        if (i == 12)
            m_repoReleasesTab = b;
        if (i == 13)
            m_repoMirrorsTab = b;
        if (i == 17)
            m_repoProjectsTab = b;
        m_repoDetailTabs->addButton(b, i);
        if (i == 12)





            (void)b;
        else if (i == 17)


            tabRow->insertWidget(2, b);
        else if (i == 18)




            tabRow->insertWidget(9, b);
        else if (i == 14)






            tabRow->insertWidget(9, b);
        else
            tabRow->addWidget(b);
    }
    tabRow->addStretch();
    // Repo actions (Notify / Fork / Mirror / Source / Open) on the same line
    // as the tabs (adhoc #6), right-aligned past the stretch. Same
    // icon-over-caption form as everything else in the row; Fork/Mirror carry
    // their counts as corner badges.
    tabRow->addWidget(notifyButton);
    tabRow->addWidget(m_forkButton);
    tabRow->addWidget(m_mirrorButton);
    tabRow->addWidget(m_sourceButton);
    tabRow->addWidget(m_repoOpenButton);
    auto *tabBar = new QWidget;
    tabBar->setObjectName("repoTabBar");
    tabBar->setLayout(tabRow);
    tabBar->setMinimumWidth(0);
    tabBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *tabBarScroll = new QScrollArea;
    tabBarScroll->setObjectName("repoTabBarScroll");
    tabBarScroll->setWidget(tabBar);
    tabBarScroll->setWidgetResizable(true);
    tabBarScroll->setFrameShape(QFrame::NoFrame);
    tabBarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tabBarScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabBarScroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    tabBarScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tabBarScroll->setMinimumWidth(0);


    tabBarScroll->setFixedHeight(56);















    auto *looperToggle = new LooperToggle(this);
    looperToggle->setOnClick([this] { toggleIssueLooper(); });


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











    m_repoDetailStack = new CurrentPageStack;



    connect(m_repoDetailStack, &QStackedWidget::currentChanged, this,
            [this](int) { updateQuickAddEnterTarget(); });
    auto addDeferredRepoTab = [this] {
        auto *placeholder = new QWidget;
        placeholder->setProperty("forkmeshDeferredRepoTab", true);
        m_repoDetailStack->addWidget(placeholder);
    };
    addDeferredRepoTab();



    m_repoDetailStack->addWidget(new QWidget);
    addDeferredRepoTab();
    addDeferredRepoTab();
    addDeferredRepoTab();
    addDeferredRepoTab();
    addDeferredRepoTab();
    addDeferredRepoTab();
    addDeferredRepoTab();
    m_insightsTabIndex = m_repoDetailStack->count();
    addDeferredRepoTab();
    m_branchesTabIndex = m_repoDetailStack->count();



    m_repoDetailStack->addWidget(new QWidget);
    m_worktreesTabIndex = m_repoDetailStack->count();



    m_repoDetailStack->addWidget(new QWidget);
    m_releasesTabIndex = m_repoDetailStack->count();
    addDeferredRepoTab();
    m_mirrorNodesTabIndex = m_repoDetailStack->count();
    addDeferredRepoTab();
    m_artifactsTabIndex = m_repoDetailStack->count();
    addDeferredRepoTab();
    m_shortcutsTabIndex = m_repoDetailStack->count();
    addDeferredRepoTab();
    m_settingsTabIndex = m_repoDetailStack->count();
    addDeferredRepoTab();
    m_projectsTabIndex = m_repoDetailStack->count();
    addDeferredRepoTab();
    m_sizeMapTabIndex = m_repoDetailStack->count();
    addDeferredRepoTab();
    connect(m_repoDetailStack, &QStackedWidget::currentChanged, this,
            [this](int index) { ensureRepoDetailTabBuilt(index); });


    m_chatStackIndex = -1;




    connect(m_repoDetailStack, &QStackedWidget::currentChanged, this,
            [this](int) { scheduleNavRecord(); });
    connect(m_repoDetailTabs, &QButtonGroup::idClicked, this, [this](int id) {
        ensureRepoDetailTabBuilt(id);
        m_repoDetailStack->setCurrentIndex(id);

        if (m_agentsNavButton)
            m_agentsNavButton->setChecked(id == 3);
        if (id == 0) {
            // A Code click always lands on the file browser: if the overview
            // body was left on the Git workspace, swap it back. The
            // explorer/overview mode is untouched.
            showOverviewFiles();
            if (m_overviewLoadedKey.isEmpty())
                QTimer::singleShot(0, this,
                                   [this] { loadRepoOverview(QString()); });
        }
        if (id == 2) {


            resetIssueFilters();
            reloadIssuesInBackground();
            if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())


                drainIssuesInboxFor(m_repositories.at(m_repoDetailIndex), false);
        }
        // id 1 is a compatibility placeholder; Git lives in the activity rail.
        else if (id == 3) {









            QTimer::singleShot(0, this, [this] {
                reloadPullsInBackground();
                reloadAgents();
            });
        }
        else if (id == 4) {
            reloadPullsInBackground();
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
        else if (id == m_projectsTabIndex)
            reloadProjects();
        else if (id == m_sizeMapTabIndex)


            refreshSizeMapTab(false);


        focusRepoDetailTable(id);
    });


    m_repoDetailStack->setCurrentIndex(0);







    m_repoDetailStack->setMinimumHeight(0);
    m_repoDetailStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);







    m_railCodeButton = new ActivityRailButton(QStringLiteral("code"),
                                              QStringLiteral("Code"));
    m_railCodeButton->setToolTip(QStringLiteral("Browse the repository files"));
    connect(m_railCodeButton, &QPushButton::clicked, this, [this] {
        showSection(0);

        if (m_repoDetailTabs && m_repoDetailTabs->button(0))
            m_repoDetailTabs->button(0)->click();
        updateRepoActivityRail();
        scheduleNavRecord();
    });
    m_railGitButton = new ActivityRailButton(QStringLiteral("git-branch"),
                                             QStringLiteral("Git"));
    m_railGitButton->setToolTip(
        QStringLiteral("Source control \xE2\x80\x94 view the current changes"));
    connect(m_railGitButton, &QPushButton::clicked, this, [this] {
        showSection(0);
        // Git always starts from the default branch's source-control view.
        // Branch/worktree/PR comparisons remain available from their links,
        // but they never become a second persistent Git destination.
        closeBranchCompareView();
        showOverviewCommits();
        QTimer::singleShot(0, this, [this] {
            if (commitsListIsCurrent())
                refreshSourceControl();
            else
                loadCommits();
        });
        updateRepoActivityRail();
        scheduleNavRecord();
    });
    if (m_appNavigationRailLayout) {
        // Code heads the rail with the global Agents entry directly beneath it
        // (adhoc #6 swapped the two), then Git. indexOf() rather than a literal
        // 0/2 so the pair still lands at the top if the rail hasn't been built
        // with Agents yet.
        const int codeAt =
            m_agentsNavButton
                ? m_appNavigationRailLayout->indexOf(m_agentsNavButton)
                : 0;
        m_appNavigationRailLayout->insertWidget(
            codeAt, m_railCodeButton, 0, Qt::AlignLeft);
        m_appNavigationRailLayout->insertWidget(
            m_agentsNavButton ? codeAt + 2 : codeAt + 1, m_railGitButton, 0,
            Qt::AlignLeft);
    }


    connect(m_repoDetailStack, &QStackedWidget::currentChanged, this,
            [this](int) { updateRepoActivityRail(); });
    if (m_overviewBodyStack)
        connect(m_overviewBodyStack, &QStackedWidget::currentChanged, this,
                [this](int) {
                    updateRepoActivityRail();
                    scheduleNavRecord();
                });
    if (m_filesStack)
        connect(m_filesStack, &QStackedWidget::currentChanged, this,
                [this](int) { updateRepoActivityRail(); });
    updateRepoActivityRail();

    m_repoDetailChrome = new QWidget;
    auto *chromeLayout = new QVBoxLayout(m_repoDetailChrome);
    chromeLayout->setContentsMargins(0, 0, 0, 0);
    chromeLayout->setSpacing(6);
    chromeLayout->addWidget(m_repoDetailNotice);
    chromeLayout->addWidget(metaBand);
    chromeLayout->addWidget(tabBarScroll);

    auto *content = new QVBoxLayout;
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(6);
    content->addWidget(m_repoDetailChrome);
    content->addWidget(m_repoDetailStack, 1);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(content, 1);
    return page;
}





void MainWindow::updateRepoActivityRail()
{
    if (!m_railCodeButton || !m_railGitButton)
        return;
    const bool onHome =
        !m_sectionStack || m_sectionStack->currentIndex() == 0;
    const bool onCode =
        onHome && m_repoDetailStack && m_repoDetailStack->currentIndex() == 0;
    const bool onChanges =
        onCode && m_filesStack && m_filesStack->currentIndex() == 0 &&
        m_overviewBodyStack && m_overviewBodyStack->currentIndex() == 1;
    const bool onAgents =
        onHome && m_repoDetailStack && m_repoDetailStack->currentIndex() == 3;







    if (m_repoDetailChrome)
        m_repoDetailChrome->setVisible(!onChanges && !onAgents);
    if (m_repoFilesModeBar)
        m_repoFilesModeBar->setVisible(!onChanges);
    if (m_repoOverviewChrome)
        m_repoOverviewChrome->setVisible(!onChanges);
    if (m_footerDock)
        m_footerDock->setVisible(!onChanges);
    m_railCodeButton->setChecked(onCode && !onChanges);
    m_railGitButton->setChecked(onChanges);
    if (m_agentsNavButton)
        m_agentsNavButton->setChecked(onAgents);


    if (m_globalSearch)
        m_globalSearch->setPlaceholderText(
            onChanges ? QString::fromUtf8("Search commits\xE2\x80\xA6")
                      : QString::fromUtf8("Search\xE2\x80\xA6"));


    syncGitCommitFilter();
}





void MainWindow::syncGitCommitFilter()
{
    if (!m_commitSearch)
        return;
    const bool onGit = m_railGitButton && m_railGitButton->isChecked();
    const QString query =
        onGit && m_globalSearch ? m_globalSearch->text().trimmed() : QString();
    if (m_commitSearch->text() == query)
        return;
    m_commitSearch->setText(query);
}


QWidget *MainWindow::buildRepoCommitsTab()
{
    m_commitsStack = new QStackedWidget;


    auto *listPage = new QWidget;





    m_commitsTable = new QTableWidget(0, 9);
    m_commitsTable->setObjectName("commitsList");
    enableHoverRowHighlight(m_commitsTable);



    m_commitsTable->setFrameShape(QFrame::NoFrame);
    m_commitsTable->setStyleSheet(m_commitsTable->styleSheet() +
                                  QStringLiteral("#commitsList{border:none;}"));
    m_commitsTable->horizontalHeader()->setVisible(false);
    m_commitsTable->verticalHeader()->setVisible(false);


    m_commitsTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_commitsTable->verticalHeader()->setDefaultSectionSize(24);
    m_commitsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_commitsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_commitsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_commitsTable->setShowGrid(false);
    m_commitsTable->setWordWrap(false);


    m_commitsTable->setSortingEnabled(false);
    m_commitsTable->setTextElideMode(Qt::ElideRight);
    QHeaderView *commitHeader = m_commitsTable->horizontalHeader();
    commitHeader->setHighlightSections(false);
    for (int i = 0; i < kCommitSummaryCol; ++i)
        m_commitsTable->setColumnHidden(i, true);
    m_commitsTable->setColumnHidden(kCommitActionCol, true);
    commitHeader->setSectionResizeMode(kCommitSummaryCol, QHeaderView::Stretch);




    m_commitsTable->setColumnHidden(kCommitGraphCol, true);
    m_commitsTable->setItemDelegateForColumn(
        kCommitSummaryCol, new CommitSummaryDelegate(m_commitsTable));


    connect(m_commitsTable, &QTableWidget::cellClicked, this,
            [this](int row, int) {
                QTableWidgetItem *item = m_commitsTable->item(row, kCommitSummaryCol);
                if (!item)
                    return;
                if (item->data(kCommitRowKindRole).toInt() == 1) {
                    m_pendingCommitFileScroll =
                        item->data(kCommitFilePathRole).toString();
                    showCommit(item->data(Qt::UserRole).toString());
                    return;
                }
                toggleCommitFilesRows(row);
            });


    connect(m_commitsTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                QTableWidgetItem *item = m_commitsTable->item(row, kCommitSummaryCol);
                if (item && item->data(kCommitRowKindRole).toInt() == 0)
                    showCommit(item->data(Qt::UserRole).toString());
            });
    connect(m_commitsTable, &QTableWidget::itemActivated, this,
            [this](QTableWidgetItem *it) {
                QTableWidgetItem *item =
                    it ? m_commitsTable->item(it->row(), kCommitSummaryCol) : nullptr;
                if (item && item->data(kCommitRowKindRole).toInt() == 0)
                    showCommit(item->data(Qt::UserRole).toString());
            });


    m_commitsListPage = listPage;
    m_commitsUnsyncedBanner = new QLabel(listPage);
    m_commitsUnsyncedBanner->setObjectName("statusLine");
    m_commitsUnsyncedBanner->setTextFormat(Qt::RichText);
    m_commitsUnsyncedBanner->setWordWrap(true);


    m_commitsUnsyncedBanner->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(m_commitsUnsyncedBanner, &QLabel::linkActivated, this,
            [this](const QString &) {
                m_commitsUnsyncedExpanded = !m_commitsUnsyncedExpanded;
                updateCommitsUnsyncedFilesPanel();
            });

    m_commitsUnsyncedBanner->setStyleSheet(
        "#statusLine {"
        "  background-color: rgba(210,153,34,0.16);"
        "  border: 1px solid rgba(210,153,34,0.55);"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "}");



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







    m_commitSearch = new QLineEdit(listPage);
    m_commitSearch->hide();
    connect(m_commitSearch, &QLineEdit::textChanged, this,
            &MainWindow::filterCommits);





    m_commitsFetchButton = new QPushButton("Fetch");
    m_commitsFetchButton->setObjectName("ghostButton");
    m_commitsFetchButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_commitsFetchButton, "cloud", 16);
    m_commitsFetchButton->setToolTip(
        "Fetch the latest history from the network without changing your "
        "working tree, then reload this list");
    connect(m_commitsFetchButton, &QPushButton::clicked, this,
            &MainWindow::fetchCurrentRepo);

    m_commitsPullButton = new QPushButton("Pull");
    m_commitsPullButton->setObjectName("ghostButton");
    m_commitsPullButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_commitsPullButton, "download", 16);
    m_commitsPullButton->setToolTip(
        "Fast-forward the working tree to the latest fetched history "
        "(git pull --ff-only)");
    connect(m_commitsPullButton, &QPushButton::clicked, this,
            &MainWindow::pullCurrentRepo);





    auto *branchButton = new ElidingPushButton;
    branchButton->setFullText(QStringLiteral("main"));
    m_commitsBranchButton = branchButton;
    m_commitsBranchButton->setObjectName("ghostButton");
    m_commitsBranchButton->setCursor(Qt::PointingHandCursor);
    m_commitsBranchButton->setToolTip(
        "Branch shown below — click to browse another branch's history or create one");
    setOcticon(m_commitsBranchButton, "git-branch", 16);

    // "<branch> -> <base>": while a branch/PR comparison is open on the right
    // pane, an arrow and the base branch follow the branch button, so the row
    // reads as the comparison itself rather than a plain branch indicator
    // (adhoc #16). Text, dropdown and visibility live in
    // updateCommitsCompareIndicator().
    m_commitsCompareArrow = new QLabel(QString::fromUtf8("\xE2\x86\x92"));
    m_commitsCompareArrow->setObjectName("hintLabel");
    m_commitsCompareArrow->setToolTip(
        QStringLiteral("The branch on the left is being compared against the "
                       "branch on the right"));
    m_commitsCompareArrow->hide();

    auto *compareBaseButton = new ElidingPushButton;
    m_commitsCompareBaseButton = compareBaseButton;
    m_commitsCompareBaseButton->setObjectName("ghostButton");
    m_commitsCompareBaseButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_commitsCompareBaseButton, "git-merge", 16);
    m_commitsCompareBaseButton->hide();

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    searchRow->addWidget(m_commitsBranchButton, 1);
    searchRow->addWidget(m_commitsCompareArrow);
    searchRow->addWidget(m_commitsCompareBaseButton, 1);
    searchRow->addWidget(m_commitsFetchButton);
    searchRow->addWidget(m_commitsPullButton);



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




    listLayout->addLayout(searchRow);
    listLayout->addWidget(m_commitsUnsyncedBanner);


    m_commitsUnsyncedFiles = new QTreeWidget(listPage);
    m_commitsUnsyncedFiles->setObjectName("commitsUnsyncedFiles");
    m_commitsUnsyncedFiles->setHeaderHidden(true);
    m_commitsUnsyncedFiles->setRootIsDecorated(true);
    m_commitsUnsyncedFiles->setMaximumHeight(180);
    enableHoverRowHighlight(m_commitsUnsyncedFiles);
    connect(m_commitsUnsyncedFiles, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                const QString hash = item->data(0, Qt::UserRole).toString();
                if (hash.isEmpty())
                    return;
                m_pendingCommitFileScroll =
                    item->data(0, Qt::UserRole + 1).toString();
                showCommit(hash);
            });
    m_commitsUnsyncedFiles->hide();
    listLayout->addWidget(m_commitsUnsyncedFiles);
    listLayout->addWidget(m_commitsTable);


    auto *detailPage = new QWidget;


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


    auto goToCommitRow = [this](int row, int dir) {
        if (!m_commitsTable)
            return;
        while (row >= 0 && row < m_commitsTable->rowCount()) {
            QTableWidgetItem *it = m_commitsTable->item(row, kCommitSummaryCol);
            if (it && it->data(kCommitRowKindRole).toInt() == 0) {
                showCommit(it->data(Qt::UserRole).toString());
                return;
            }
            row += dir;
        }
    };
    connect(m_commitPrevButton, &QPushButton::clicked, this,
            [this, goToCommitRow] { goToCommitRow(m_currentCommitRow - 1, -1); });
    connect(m_commitNextButton, &QPushButton::clicked, this,
            [this, goToCommitRow] { goToCommitRow(m_currentCommitRow + 1, +1); });

    m_commitTitle = new QLabel;
    m_commitTitle->setObjectName("repoHeaderTitle");
    m_commitTitle->setTextFormat(Qt::RichText);
    m_commitTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_commitTitle->setWordWrap(true);
    m_commitTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);



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




    m_commitDeleteButton = new QPushButton("Delete commit");
    m_commitDeleteButton->setObjectName("ghostButton");
    m_commitDeleteButton->setCursor(Qt::PointingHandCursor);
    m_commitDeleteButton->setToolTip(
        "Remove this commit from history (rewrites the branch and replays the "
        "later commits onto its parent)");
    setOcticon(m_commitDeleteButton, "trash", 16);
    connect(m_commitDeleteButton, &QPushButton::clicked, this,
            [this] { deleteCommit(m_currentCommitHash); });




    m_commitRevertButton = new QPushButton("Restore commit");
    m_commitRevertButton->setObjectName("ghostButton");
    m_commitRevertButton->setCursor(Qt::PointingHandCursor);
    m_commitRevertButton->setToolTip(
        "Undo this commit by committing the reverse of its changes (history is "
        "kept)");
    setOcticon(m_commitRevertButton, "history", 16);
    connect(m_commitRevertButton, &QPushButton::clicked, this,
            [this] { revertCommit(m_currentCommitHash); });




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
        for (QPushButton *b : {m_pullSplitButton, m_branchSplitButton}) {
            if (b) {
                b->setChecked(on);
                updateDiffSplitButton(b);
            }
        }
        if (!m_currentCommitHash.isEmpty())
            showCommit(m_currentCommitHash);
    });
    for (QPushButton *b :
         {commitCopyLinkButton, m_commitDownloadButton, m_commitDeleteButton,
          m_commitRevertButton, m_commitPrevButton, m_commitNextButton}) {
        b->setProperty("buttonSize", "sm");
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        b->setFixedWidth(qMax(72, b->sizeHint().width()));
    }

    auto *navCol = new QVBoxLayout;
    navCol->setContentsMargins(0, 0, 0, 0);
    navCol->setSpacing(4);






    auto *historyActionRow = new QHBoxLayout;
    historyActionRow->setContentsMargins(0, 0, 0, 0);
    historyActionRow->setSpacing(4);
    historyActionRow->addWidget(m_commitDeleteButton);
    historyActionRow->addWidget(m_commitRevertButton);
    historyActionRow->addStretch();
    navCol->addLayout(historyActionRow);

    auto *commitToolRow = new QHBoxLayout;
    commitToolRow->setContentsMargins(0, 0, 0, 0);
    commitToolRow->setSpacing(4);
    commitToolRow->addWidget(m_commitSplitButton);
    commitToolRow->addWidget(commitCopyLinkButton);
    commitToolRow->addWidget(m_commitDownloadButton);
    commitToolRow->addWidget(m_commitPrevButton);
    commitToolRow->addWidget(m_commitNextButton);
    commitToolRow->addStretch();
    navCol->addLayout(commitToolRow);

    auto *headerRow = new QVBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->addLayout(navCol);
    headerRow->addWidget(m_commitTitle, 0, Qt::AlignTop);

    m_commitMessage = new QLabel;
    m_commitMessage->setObjectName("commitMessage");
    m_commitMessage->setWordWrap(true);
    m_commitMessage->setTextFormat(Qt::RichText);
    m_commitMessage->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                             Qt::LinksAccessibleByMouse);




    connect(m_commitMessage, &QLabel::linkActivated, this,
            [this](const QString &href) {
                if (href == QLatin1String("toggle-msg")) {

                    m_commitMessage->setProperty(
                        "expanded",
                        !m_commitMessage->property("expanded").toBool());
                    refreshCommitMessageLabel(m_commitMessage);
                } else if (href.startsWith(QStringLiteral("ref:")))
                    openCommitReference(href.mid(4).toInt());
                else if (href.startsWith(QStringLiteral("commit:")))
                    openCommitHashReference(href.mid(7));
            });

    m_commitMeta = new QLabel;
    m_commitMeta->setObjectName("statusLine");
    m_commitMeta->setTextFormat(Qt::RichText);
    m_commitMeta->setTextInteractionFlags(Qt::TextSelectableByMouse);



    m_commitFilesSummary = nullptr;





    m_commitFileList = nullptr;


    m_commitDiffView = new QTextBrowser;
    m_commitDiffView->setObjectName("commitDiffView");
    m_commitDiffView->setOpenExternalLinks(false);
    registerDiffView(m_commitDiffView);




    m_commitDiffSpinner = new BusySpinner;
    m_commitDiffSpinner->setToolTip(QString::fromUtf8("Loading diff\xE2\x80\xA6"));
    m_commitDiffSpinner->hide();
    auto *diffSpinRow = new QHBoxLayout;
    diffSpinRow->setContentsMargins(0, 0, 0, 0);
    diffSpinRow->addStretch();
    diffSpinRow->addWidget(m_commitDiffSpinner);



    auto *split = new QWidget;
    auto *splitLayout = new QVBoxLayout(split);
    splitLayout->setContentsMargins(0, 0, 0, 0);
    splitLayout->setSpacing(4);
    splitLayout->addLayout(diffSpinRow);
    splitLayout->addWidget(m_commitDiffView, 1);

    auto *detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(16, 12, 16, 16);
    detailLayout->setSpacing(8);
    detailLayout->addLayout(headerRow);
    detailLayout->addWidget(m_commitMessage);
    detailLayout->addWidget(m_commitMeta);
    detailLayout->addWidget(split, 1);

    // Left column: working-tree changes above commit history. The column is one
    // resizable splitter pane, so the user can give lists just enough room and
    // keep the right side dedicated to the active diff/detail view. (Each half
    // is a stack for historical reasons — the range review used to borrow the
    // two slots (adhoc #110); it no longer does (adhoc #12), so each hosts its
    // single working-tree page.)
    auto *scmPanel = buildSourceControlPanel();
    m_gitFilesSlot = new QStackedWidget;
    m_gitFilesSlot->addWidget(scmPanel);
    m_gitHistorySlot = new QStackedWidget;
    m_gitHistorySlot->addWidget(listPage);
    auto *leftSplit = new QSplitter(Qt::Vertical);
    leftSplit->setChildrenCollapsible(false);
    leftSplit->addWidget(m_gitFilesSlot);
    leftSplit->addWidget(m_gitHistorySlot);




    constexpr int kGitFilesSlotMinHeight = 130;
    m_gitFilesSlot->setMinimumHeight(kGitFilesSlotMinHeight);
    leftSplit->setStretchFactor(0, 2);
    leftSplit->setStretchFactor(1, 3);
    leftSplit->setSizes({320, 520});



    auto *changesPage = new QWidget;
    auto *changesLayout = new QVBoxLayout(changesPage);
    changesLayout->setContentsMargins(16, 12, 16, 16);
    changesLayout->setSpacing(8);
    m_scmDiff = new QTextBrowser;
    m_scmDiff->setObjectName("diffView");
    registerDiffView(m_scmDiff);




    m_scmDiff->setFrameShape(QFrame::NoFrame);
    m_scmDiff->setStyleSheet(m_scmDiff->styleSheet() +
                             QStringLiteral("#diffView{border:none;}"));


    setupScmDiffPane();
    m_scmDiff->setHtml(QStringLiteral(
        "<p style='color:#8b949e'>No working-tree changes to review.</p>"));
    changesLayout->addWidget(m_scmDiff, 1);

    m_commitsStack->addWidget(changesPage); // kCommitWorkspaceChangesPage
    m_commitsStack->addWidget(detailPage);  // kCommitWorkspaceCommitPage
    // The branch/PR range review pane lives only on the right. The source-control
    // composer and working changes stay visible above the graph on the left.
    m_commitsStack->addWidget(buildBranchRangePane()); // kCommitWorkspaceRangePage
    m_commitsStack->setCurrentIndex(kCommitWorkspaceChangesPage);

    auto *workspaceSplit = new QSplitter(Qt::Horizontal);
    workspaceSplit->setChildrenCollapsible(false);
    workspaceSplit->addWidget(leftSplit);
    workspaceSplit->addWidget(m_commitsStack);
    workspaceSplit->setStretchFactor(0, 0);
    workspaceSplit->setStretchFactor(1, 1);
    workspaceSplit->setSizes({430, 950});

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(workspaceSplit);
    return page;
}





void MainWindow::loadFileSearchIndex()
{
    if (!m_fileCompleter)
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        m_fileCompleter->setModel(new QStringListModel(QStringList(), m_fileCompleter));
        return;
    }







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
            QIcon(refreshPixmap(QColor(Theme::kRunning), *angle, size)));
    });
    timer->start(60);


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



        const QPixmap frame = button->property("fmSpinHourglass").toBool()
                                   ? hourglassPixmap(QColor(Theme::kRunning), *angle, size)
                                   : refreshPixmap(QColor(Theme::kRunning), *angle, size);
        button->setIcon(QIcon(frame));
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
    button->setProperty("fmSpinHourglass", false);
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




void MainWindow::setRestartSpinHourglass(bool hourglass)
{
    if (m_restartSpinButton)
        m_restartSpinButton->setProperty("fmSpinHourglass", hourglass);
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
                QIcon(refreshPixmap(QColor(Theme::kRunning), m_refreshAngle, 22)));
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
                QIcon(refreshPixmap(QColor(Theme::kRunning),
                                    m_nodeSwitchAngle, 16)));
        });
    }
    m_nodeSwitchSpinTimer->start(60);




    if (!m_nodeSwitchProgress) {
        m_nodeSwitchProgress =
            new QProgressBar(m_nodeMenuButton->parentWidget());
        m_nodeSwitchProgress->setObjectName("nodeSwitchProgress");
        m_nodeSwitchProgress->setRange(0, 0);
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
                QIcon(refreshPixmap(QColor(Theme::kRunning),
                                    m_repoSwitchAngle, 16)));
        });
    }
    m_repoMenuButton->setIcon(
        QIcon(refreshPixmap(QColor(Theme::kRunning), 0, 16)));
    m_repoSwitchSpinTimer->start(60);
}

void MainWindow::stopRepoSwitchSpin()
{
    if (m_repoSwitchSpinTimer)
        m_repoSwitchSpinTimer->stop();

    if (m_repoMenuButton)
        m_repoMenuButton->setIcon(QIcon());
    updateRepoSwitcher();
}

void MainWindow::openRepoDetailDeferred(int repoIndex)
{
    if (repoIndex < 0 || repoIndex >= m_repositories.size())
        return;

    if (repoIndex == m_repoDetailIndex && !m_repoDetailLoading) {
        showSection(0);
        return;
    }


    if (m_repoOpenPending == repoIndex)
        return;
    m_repoOpenPending = repoIndex;


    startRepoSwitchSpin();
    showLoadStatus(QStringLiteral("Opening repository…"));
    QApplication::setOverrideCursor(Qt::BusyCursor);
    QTimer::singleShot(0, this, [this, repoIndex] {
        m_repoOpenPending = -1;
        QElapsedTimer timer;
        timer.start();


        m_repoLoadActive = true;
        openRepoDetail(repoIndex);
        m_repoLoadActive = false;
        finishLoadStepTiming();
        stopRepoSwitchSpin();
        QApplication::restoreOverrideCursor();


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






    if (!m_nodeSwitching && !m_repoLoadActive)
        return;




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



    m_topMessage->setText(
        QStringLiteral("<span style='color:#58a6ff'>%1 %2</span>")
            .arg(QString::fromUtf8("\xE2\x9F\xB3"),
                 what.toHtmlEscaped()));
    m_topMessage->setWordWrap(false);
    m_topMessage->show();
    if (m_topMessageContainer)
        m_topMessageContainer->show();
    m_loadStatusShowing = true;
    m_topMessageElided = false;
    m_topMessageExpanded = false;
    if (m_topMessageTimer)
        m_topMessageTimer->stop();
    if (m_topMessageOverlay)
        m_topMessageOverlay->hide();
    if (m_topMessageExpand)
        m_topMessageExpand->hide();
    if (m_topMessageCopy)
        m_topMessageCopy->hide();
    if (m_topMessageClose)
        m_topMessageClose->hide();
}
