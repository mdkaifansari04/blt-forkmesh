// MainWindowIssues: MainWindow feature methods, split out of MainWindow.cpp.
// Issues: the issues list/board and the repo-detail (files + issues) tabs.
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

// ---- Issues section --------------------------------------------------------

QWidget *MainWindow::buildIssuesSection()
{
    auto *page = new QWidget;

    // --- Left: a sortable issue table with filters above.
    auto *listPane = new QWidget;
    listPane->setMinimumWidth(360);

    auto *heading = new QLabel("Issues");
    heading->setObjectName("channelTitle");

    auto *issueTabGroup = new QButtonGroup(page);
    m_issueTabGroup = issueTabGroup;
    issueTabGroup->setExclusive(true);
    auto *issuesTab = new QPushButton;
    issuesTab->setObjectName("ghostButton");
    issuesTab->setProperty("buttonSize", "sm");
    issuesTab->setCheckable(true);
    issuesTab->setChecked(true);
    issuesTab->setCursor(Qt::PointingHandCursor);
    issuesTab->setToolTip("Issues");
    setOcticon(issuesTab, "issue-opened", 16);
    auto *milestonesTab = new QPushButton("Milestones");
    milestonesTab->setObjectName("ghostButton");
    milestonesTab->setProperty("buttonSize", "sm");
    milestonesTab->setCheckable(true);
    milestonesTab->setCursor(Qt::PointingHandCursor);
    auto *labelsTab = new QPushButton("Labels");
    labelsTab->setObjectName("ghostButton");
    labelsTab->setProperty("buttonSize", "sm");
    labelsTab->setCheckable(true);
    labelsTab->setCursor(Qt::PointingHandCursor);
    auto *boardTab = new QPushButton("Board");
    boardTab->setObjectName("ghostButton");
    boardTab->setProperty("buttonSize", "sm");
    boardTab->setCheckable(true);
    boardTab->setCursor(Qt::PointingHandCursor);
    boardTab->setToolTip("Kanban board: drag issues between status columns");
    setOcticon(milestonesTab, "graph", 16);
    setOcticon(labelsTab, "tag", 16);
    setOcticon(boardTab, "workflow", 16);
    issueTabGroup->addButton(issuesTab, 0);
    issueTabGroup->addButton(milestonesTab, 1);
    issueTabGroup->addButton(labelsTab, 2);
    issueTabGroup->addButton(boardTab, 3);
    auto *headingRow = new QHBoxLayout;
    headingRow->setContentsMargins(0, 0, 0, 0);
    headingRow->addWidget(heading);
    headingRow->addStretch();
    headingRow->addWidget(issuesTab);
    headingRow->addWidget(boardTab);
    headingRow->addWidget(milestonesTab);
    headingRow->addWidget(labelsTab);

    m_issuesRepoCombo = new QComboBox;
    m_issuesRepoCombo->setToolTip("Repository whose issues you are viewing");
    // The repo is fixed by the repo-detail view that hosts this panel; the combo
    // is kept for state but hidden from the user.
    m_issuesRepoCombo->hide();

    m_issueSearch = new QLineEdit;
    m_issueSearch->setObjectName("issueSearch");
    m_issueSearch->setPlaceholderText("Search issues\xE2\x80\xA6");
    m_issueSearch->setClearButtonEnabled(true);

    m_issueStatusFilter = new QComboBox;
    m_issueStatusFilter->setObjectName("issueControlSm");
    m_issueStatusFilter->addItems({"Open", "Closed", "All"});
    m_issueLabelFilter = new QComboBox;
    m_issueLabelFilter->setObjectName("issueControlSm");
    m_issueMilestoneFilter = new QComboBox;
    m_issueMilestoneFilter->setObjectName("issueControlSm");
    auto *filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->addWidget(m_issueSearch, 1);
    filterRow->addWidget(m_issueLabelFilter);
    filterRow->addWidget(m_issueMilestoneFilter);

    m_issueNewButton = new QPushButton("New issue");
    m_issueNewButton->setObjectName("primaryButton");
    m_issueNewButton->setProperty("buttonSize", "sm");
    m_issueNewButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueNewButton, "plus", 16);
    m_issueSyncButton = new QPushButton("Sync inbox");
    m_issueSyncButton->setObjectName("ghostButton");
    m_issueSyncButton->setProperty("buttonSize", "sm");
    m_issueSyncButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueSyncButton, "sync", 16);
    m_issueSyncButton->setToolTip(
        "Pull issue/comment submissions filed by other nodes and merge them");
    auto *issueBurnupButton = new QPushButton("Burn-up chart");
    issueBurnupButton->setObjectName("ghostButton");
    issueBurnupButton->setProperty("buttonSize", "sm");
    issueBurnupButton->setCursor(Qt::PointingHandCursor);
    issueBurnupButton->setToolTip(
        "Show open and closed issue totals over time");
    setOcticon(issueBurnupButton, "graph", 16);
    m_issueDetailToggle = new QPushButton("Show detail");
    m_issueDetailToggle->setObjectName("ghostButton");
    m_issueDetailToggle->setCursor(Qt::PointingHandCursor);
    m_issueDetailToggle->setToolTip("Show/hide the issue detail panel");
    m_issueCreditsLabel = new QLabel;
    m_issueCreditsLabel->setObjectName("statusLine");
    m_issueCreditsLabel->setToolTip("Voting credits — earn 1 per hour online");
    auto *reprioritizeButton = new QPushButton("Reprioritize");
    reprioritizeButton->setObjectName("ghostButton");
    reprioritizeButton->setProperty("buttonSize", "sm");
    reprioritizeButton->setCursor(Qt::PointingHandCursor);
    reprioritizeButton->setToolTip(
        "Assign a priority + MVP/Phase 2 label to open issues with no priority, "
        "and estimate each issue's progress from whether the work has landed");
    setOcticon(reprioritizeButton, "sort-desc", 16);
    connect(reprioritizeButton, &QPushButton::clicked, this,
            &MainWindow::reprioritizeBacklog);

    // Issue looper (adhoc #92): the one-click "work through the backlog" control
    // now lives in a compact toggle floating just above the Issues tab (adhoc
    // #130, see m_looperToggle), so it is no longer a button in this action row.

    // Issue #286: hand the README and the open backlog to the default agent and
    // let it rank the issues. The instruction is editable in Settings -> Agents.
    // Sits at the top of the panel next to the title (a primary action, not lost
    // among the ghost buttons of the crowded filter/bulk row below).
    m_issuePrioritizeButton = new QPushButton("Prioritize from README");
    m_issuePrioritizeButton->setObjectName("primaryButton");
    m_issuePrioritizeButton->setProperty("buttonSize", "sm");
    m_issuePrioritizeButton->setCursor(Qt::PointingHandCursor);
    m_issuePrioritizeButton->setToolTip(
        "Ask the default agent to rank the open issues against the project's "
        "README and rewrite each issue's priority. The prompt is editable in "
        "Settings \xE2\x86\x92 Agents.");
    setOcticon(m_issuePrioritizeButton, "rocket", 16);
    connect(m_issuePrioritizeButton, &QPushButton::clicked, this,
            &MainWindow::prioritizeIssuesFromReadme);

    // Agent picker next to the button so a run can target any provider, not just
    // the saved default. Seeded from the default agent (Settings -> Agents).
    m_issuePrioritizeAgentCombo = new QComboBox;
    m_issuePrioritizeAgentCombo->setObjectName("issueControlSm");
    m_issuePrioritizeAgentCombo->addItem(QStringLiteral("OpenAI API"),
                                         QStringLiteral("openai"));
    m_issuePrioritizeAgentCombo->addItem(QStringLiteral("Claude API"),
                                         QStringLiteral("claude-api"));
    m_issuePrioritizeAgentCombo->addItem(QStringLiteral("Claude Code"),
                                         QStringLiteral("claude-code"));
    selectDefaultAgentProvider(m_issuePrioritizeAgentCombo);
    m_issuePrioritizeAgentCombo->setToolTip(
        "Agent that ranks/reviews the issues. Defaults to your default agent "
        "(Settings \xE2\x86\x92 Agents).");

    // Adhoc #139: sibling of "Prioritize from README" that, instead of ranking,
    // asks the same picked agent to judge how complete/actionable each open issue
    // is and shows the verdict in a report dialog. Reuses the agent picker above.
    m_issueCompletenessButton = new QPushButton("Analyze completeness");
    m_issueCompletenessButton->setObjectName("ghostButton");
    m_issueCompletenessButton->setProperty("buttonSize", "sm");
    m_issueCompletenessButton->setCursor(Qt::PointingHandCursor);
    m_issueCompletenessButton->setToolTip(
        "Ask the picked agent to rate how complete each open issue is (clear "
        "problem, enough detail, acceptance criteria), then label each issue "
        "Complete/Partial/Incomplete and set its progress from the verdict.");
    setOcticon(m_issueCompletenessButton, "list-unordered", 16);
    connect(m_issueCompletenessButton, &QPushButton::clicked, this,
            &MainWindow::analyzeIssueCompleteness);

    // Place them right after the "Issues" heading, ahead of the view-tab toggles:
    // prioritize, then completeness, then the shared agent picker.
    headingRow->insertWidget(1, m_issuePrioritizeButton);
    headingRow->insertWidget(2, m_issuePrioritizeAgentCombo);
    headingRow->insertWidget(2, m_issueCompletenessButton);

    // Bulk bounty: pledge the same amount on every open issue at once. Bounties
    // are pledged only (funded on merge), so this never moves money.
    auto *bountyAllAmount = new QLineEdit;
    bountyAllAmount->setObjectName("issueControlSm");
    bountyAllAmount->setPlaceholderText("$ all");
    bountyAllAmount->setMaximumWidth(70);
    bountyAllAmount->setToolTip("Bounty amount (USD) to pledge on every open issue");
    auto *bountyAllButton = new QPushButton("Bounty all");
    bountyAllButton->setObjectName("ghostButton");
    bountyAllButton->setProperty("buttonSize", "sm");
    bountyAllButton->setCursor(Qt::PointingHandCursor);
    bountyAllButton->setToolTip(
        "Pledge this bounty on every open issue (funded when each PR is merged)");
    setOcticon(bountyAllButton, "tag", 16);
    auto applyBountyAll = [this, bountyAllAmount] {
        bool ok = false;
        const double amount = bountyAllAmount->text().trimmed().toDouble(&ok);
        if (!ok || amount < 1.0) {
            setIssueInlineNotice(
                "Enter a bounty amount (USD \xE2\x89\xA5 1) to apply to all open "
                "issues.",
                true);
            return;
        }
        bountyAllOpenIssues(amount);
    };
    connect(bountyAllButton, &QPushButton::clicked, this, applyBountyAll);
    connect(bountyAllAmount, &QLineEdit::returnPressed, this, applyBountyAll);

    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->addWidget(m_issueSyncButton);
    actionRow->addWidget(issueBurnupButton);
    actionRow->addWidget(reprioritizeButton);
    actionRow->addWidget(bountyAllAmount);
    actionRow->addWidget(bountyAllButton);
    actionRow->addWidget(m_issueStatusFilter);
    actionRow->addStretch();
    actionRow->addWidget(m_issueCreditsLabel);
    actionRow->addWidget(m_issueDetailToggle);

    m_issueTable = new QTableWidget(0, 17);
    m_issueTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_issueTable);
    m_issueTable->setHorizontalHeaderLabels(
        {"#", "Title", "Priority", "Status", "Votes", "Labels", "Milestone",
         "Created", "Updated", "Agent", "Author", "Progress", "Est. cost",
         "Bounty", "Comments", "Files", "Assignee"});
    m_issueTable->verticalHeader()->setVisible(false);
    m_issueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_issueTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_issueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_issueTable->setShowGrid(false);
    m_issueTable->setWordWrap(false);
    m_issueTable->setSortingEnabled(true);
    // The launch backlog opens in execution order: 1 is highest, 99 lowest.
    m_issueTable->sortByColumn(2, Qt::AscendingOrder);
    m_issueTable->setToolTip("Click a column header to sort. Double-click an "
                             "editable cell (title, priority, status, labels, "
                             "milestone, progress, bounty) to edit it.");
    QHeaderView *header = m_issueTable->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents); // #
    // Title is user-expandable: a draggable Interactive column with a generous
    // default width rather than a locked Stretch flex column, so long titles can
    // be widened (or narrowed) to taste instead of being elided with no recourse.
    header->setSectionResizeMode(1, QHeaderView::Interactive);      // Title
    m_issueTable->setColumnWidth(1, 360);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents); // Priority
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents); // Status
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents); // Votes
    header->setSectionResizeMode(5, QHeaderView::ResizeToContents); // Labels
    header->setSectionResizeMode(6, QHeaderView::ResizeToContents); // Milestone
    header->setSectionResizeMode(7, QHeaderView::ResizeToContents);  // Created
    header->setSectionResizeMode(8, QHeaderView::ResizeToContents);  // Updated
    header->setSectionResizeMode(9, QHeaderView::ResizeToContents);  // Agent
    header->setSectionResizeMode(10, QHeaderView::ResizeToContents); // Author
    header->setSectionResizeMode(11, QHeaderView::ResizeToContents); // Progress
    header->setSectionResizeMode(12, QHeaderView::ResizeToContents); // Est. cost
    header->setSectionResizeMode(13, QHeaderView::ResizeToContents); // Bounty
    header->setSectionResizeMode(14, QHeaderView::ResizeToContents); // Comments
    header->setSectionResizeMode(15, QHeaderView::ResizeToContents); // Files
    header->setSectionResizeMode(16, QHeaderView::ResizeToContents); // Assignee
    makeColumnsResizable(m_issueTable);
    // Render the Progress column as a mini bar (keeps row hover via the subclass).
    m_issueTable->setItemDelegateForColumn(11, new ProgressBarDelegate(m_issueTable));
    // Drag along a Progress cell to set the value (handled in eventFilter).
    m_issueTable->viewport()->installEventFilter(this);

    m_issueMilestonesTable = new QTableWidget(0, 6);
    m_issueMilestonesTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_issueMilestonesTable);
    m_issueMilestonesTable->setHorizontalHeaderLabels(
        {"Milestone", "Open", "Closed", "Progress", "Due", "Status"});
    m_issueMilestonesTable->verticalHeader()->setVisible(false);
    m_issueMilestonesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_issueMilestonesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_issueMilestonesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_issueMilestonesTable->setShowGrid(false);
    m_issueMilestonesTable->setSortingEnabled(true);
    QHeaderView *msHeader = m_issueMilestonesTable->horizontalHeader();
    msHeader->setHighlightSections(false);
    msHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 6; ++i)
        msHeader->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_issueMilestonesTable);

    m_issueLabelsTable = new QTableWidget(0, 5);
    m_issueLabelsTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_issueLabelsTable);
    m_issueLabelsTable->setHorizontalHeaderLabels(
        {"Label", "Open", "Closed", "Total", ""});
    m_issueLabelsTable->verticalHeader()->setVisible(false);
    m_issueLabelsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_issueLabelsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_issueLabelsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_issueLabelsTable->setShowGrid(false);
    m_issueLabelsTable->setSortingEnabled(true);
    QHeaderView *labelHeader = m_issueLabelsTable->horizontalHeader();
    labelHeader->setHighlightSections(false);
    labelHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 5; ++i)
        labelHeader->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_issueLabelsTable);

    m_issueListStack = new QStackedWidget;
    m_issueListStack->addWidget(m_issueTable);          // 0 Issues
    m_issueListStack->addWidget(m_issueMilestonesTable); // 1 Milestones
    m_issueListStack->addWidget(m_issueLabelsTable);     // 2 Labels
    m_issueListStack->addWidget(buildIssueBoard());      // 3 Board (Kanban)

    // The "looper running" status now lives in the floating toggle above the
    // Issues tab (adhoc #130), so the in-page banner that used to sit here has
    // been removed from the issues page.

    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(18, 18, 12, 18);
    listLayout->setSpacing(8);
    listLayout->addLayout(headingRow);
    listLayout->addWidget(m_issuesRepoCombo);
    listLayout->addLayout(filterRow);
    listLayout->addLayout(actionRow);
    listLayout->addWidget(m_issueListStack, 1);

    // Center: GitHub-style selected issue page: title header, status, timeline and
    // comment composer.
    m_issueTitle = new QLabel("Select an issue");
    m_issueTitle->setObjectName("issuePageTitle");
    m_issueTitle->setTextFormat(Qt::RichText);
    m_issueTitle->setWordWrap(true);
    m_issueTitleEditor = new QLineEdit;
    m_issueTitleEditor->setObjectName("issueTitleEditor");
    m_issueTitleEditor->setPlaceholderText("Issue title");
    m_issueTitleEditor->hide();
    m_issueTitleEditButton = new QPushButton;
    m_issueTitleEditButton->setObjectName("issueIconButton");
    m_issueTitleEditButton->setFixedSize(30, 30);
    m_issueTitleEditButton->setCursor(Qt::PointingHandCursor);
    m_issueTitleEditButton->setToolTip("Edit title");
    setOcticon(m_issueTitleEditButton, "pencil", 15);
    m_issueTitleSaveButton = new QPushButton("Save");
    m_issueTitleSaveButton->setObjectName("primaryButton");
    m_issueTitleSaveButton->setProperty("buttonSize", "sm");
    m_issueTitleSaveButton->setCursor(Qt::PointingHandCursor);
    m_issueTitleSaveButton->hide();
    m_issueTitleCancelButton = new QPushButton("Cancel");
    m_issueTitleCancelButton->setObjectName("ghostButton");
    m_issueTitleCancelButton->setProperty("buttonSize", "sm");
    m_issueTitleCancelButton->setCursor(Qt::PointingHandCursor);
    m_issueTitleCancelButton->hide();
    m_issueCopyButton = new QPushButton;
    m_issueCopyButton->setObjectName("issueIconButton");
    m_issueCopyButton->setFixedSize(30, 30);
    m_issueCopyButton->setCursor(Qt::PointingHandCursor);
    m_issueCopyButton->setToolTip("Copy the issue title to the clipboard");
    setOcticon(m_issueCopyButton, "copy", 16);
    m_issueCopyAllButton = new QPushButton;
    m_issueCopyAllButton->setObjectName("issueIconButton");
    m_issueCopyAllButton->setFixedSize(30, 30);
    m_issueCopyAllButton->setCursor(Qt::PointingHandCursor);
    m_issueCopyAllButton->setToolTip(
        "Copy the issue and all comments to the clipboard");
    setOcticon(m_issueCopyAllButton, "comment", 16);
    m_issueVoteButton = new QPushButton("Vote");
    m_issueVoteButton->setObjectName("ghostButton");
    m_issueVoteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueVoteButton, "thumbsup", 16);
    m_issueVoteButton->setToolTip("Upvote this issue (spends 1 voting credit)");
    auto *issueTitleRow = new QHBoxLayout;
    issueTitleRow->setContentsMargins(0, 0, 0, 0);
    issueTitleRow->setSpacing(8);
    issueTitleRow->addWidget(m_issueTitle, 1);
    issueTitleRow->addWidget(m_issueTitleEditor, 1);
    issueTitleRow->addWidget(m_issueTitleSaveButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueTitleCancelButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueTitleEditButton, 0, Qt::AlignTop);
    issueTitleRow->addStretch();
    issueTitleRow->addWidget(m_issueNewButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueCopyButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueCopyAllButton, 0, Qt::AlignTop);
    m_issueMeta = new QLabel; // Open/Closed status pill
    m_issueMeta->setObjectName("issueStatusPill");
    m_issueMeta->setTextFormat(Qt::PlainText);
    m_issueMeta->setAlignment(Qt::AlignCenter);
    m_issueMeta->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_issueReadonlyNote = new QLabel;
    m_issueReadonlyNote->setObjectName("statusLine");
    m_issueReadonlyNote->setWordWrap(true);
    m_issueReadonlyNote->hide();
    m_issueInlineNotice = new QLabel;
    m_issueInlineNotice->setObjectName("issueInlineNotice");
    m_issueInlineNotice->setWordWrap(true);
    m_issueInlineNotice->hide();

    m_issueThreadContainer = new QWidget;
    m_issueThreadLayout = new QVBoxLayout(m_issueThreadContainer);
    m_issueThreadLayout->setContentsMargins(0, 0, 0, 0);
    m_issueThreadLayout->setSpacing(10);
    m_issueThreadLayout->addStretch();
    m_issueThreadScroll = new QScrollArea;
    m_issueThreadScroll->setWidgetResizable(true);
    m_issueThreadScroll->setWidget(m_issueThreadContainer);
    m_issueThreadScroll->setObjectName("issuePageScroll");
    m_issueThreadScroll->setFrameShape(QFrame::NoFrame);

    auto *commentAvatar = new QLabel("FM");
    commentAvatar->setObjectName("issueAvatar");
    commentAvatar->setAlignment(Qt::AlignCenter);
    commentAvatar->setFixedSize(36, 36);
    commentAvatar->setScaledContents(true);
    m_issueComposerAvatar = commentAvatar;
    refreshIssueComposerAvatar();
    auto *commentTitle = new QLabel("Add a comment");
    commentTitle->setObjectName("issueCommentTitle");
    m_issueComposer = new MarkdownEditor;
    m_issueComposer->setObjectName("issueCommentEditor");
    m_issueComposer->setMinimumHeight(190);
    m_issueComposer->setPlaceholderText("Use Markdown to format your comment");
    m_issueAttachButton = new QPushButton("Paste, drop, or click to add files");
    m_issueAskAiButton = new QPushButton("Ask AI");
    m_issueCloseButton = new QPushButton("Close issue");
    m_issueCloseCommentButton = new QPushButton("Close with comment");
    m_issueCloseCommentButton->setToolTip(
        "Post the comment above and close the issue in one step");
    m_issueCommentButton = new QPushButton("Comment");
    m_issueCommentButton->setObjectName("primaryButton");
    m_issueCommentButton->setCursor(Qt::PointingHandCursor);
    for (QPushButton *b : {m_issueAttachButton, m_issueAskAiButton,
                           m_issueCloseButton, m_issueCloseCommentButton,
                           m_issueVoteButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_issueAttachButton, "paperclip", 16);
    setOcticon(m_issueAskAiButton, "comment", 16);
    m_issueAskAiButton->setToolTip(
        "Ask OpenAI a question from the comment box and post the answer");
    auto *commentButtonRow = new QHBoxLayout;
    commentButtonRow->setContentsMargins(0, 0, 0, 0);
    commentButtonRow->addWidget(m_issueAttachButton, 0, Qt::AlignLeft);
    commentButtonRow->addStretch();
    commentButtonRow->addWidget(m_issueAskAiButton);
    commentButtonRow->addWidget(m_issueVoteButton);
    commentButtonRow->addWidget(m_issueCloseButton);
    commentButtonRow->addWidget(m_issueCloseCommentButton);
    commentButtonRow->addWidget(m_issueCommentButton);
    auto *commentColumn = new QVBoxLayout;
    commentColumn->setContentsMargins(0, 0, 0, 0);
    commentColumn->setSpacing(8);
    commentColumn->addWidget(commentTitle);
    commentColumn->addWidget(m_issueComposer);
    commentColumn->addLayout(commentButtonRow);
    auto *composerRow = new QHBoxLayout;
    composerRow->setContentsMargins(0, 0, 0, 0);
    composerRow->setSpacing(14);
    composerRow->addWidget(commentAvatar, 0, Qt::AlignTop);
    composerRow->addLayout(commentColumn, 1);

    auto *center = new QWidget;
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(24, 22, 22, 22);
    centerLayout->setSpacing(12);
    centerLayout->addLayout(issueTitleRow);
    centerLayout->addWidget(m_issueMeta);
    centerLayout->addWidget(m_issueReadonlyNote);
    centerLayout->addWidget(m_issueInlineNotice);
    auto *issueDivider = new QWidget;
    issueDivider->setObjectName("issueDivider");
    issueDivider->setFixedHeight(1);
    centerLayout->addWidget(issueDivider);
    centerLayout->addWidget(m_issueThreadScroll, 1);
    centerLayout->addLayout(composerRow);

    // Right: GitHub-style metadata sidebar.
    auto *meta = new QWidget;
    meta->setObjectName("issueSidebar");
    meta->setMinimumWidth(265);
    meta->setMaximumWidth(315);
    m_issueAssigneesValue = new QLabel("No one - <a href='#'>Assign yourself</a>");
    m_issueLabelsValue = new QLabel("No labels");
    m_issueMilestoneValue = new QLabel("No milestone");
    m_issuePriorityValue = new QLabel("No priority");
    m_issueEstimateValue = new QLabel("\xE2\x80\x94");
    m_issueBountyValue = new QLabel("No bounty");
    // Draggable progress bar (drag along the track to set percent complete); the
    // store write and reload happen once on release.
    auto *progressSlider = new ProgressSlider;
    m_issueProgressSlider = progressSlider;
    progressSlider->onCommitted = [this](int pct) {
        if (m_currentIssueNumber < 0)
            return;
        IssueStore store = issueStoreForCurrentRepo();
        QString error;
        if (!store.setProgress(m_currentIssueNumber, pct, &error)) {
            setIssueInlineNotice(error.isEmpty() ? "Could not update progress." : error,
                                 true);
            return;
        }
        setIssueInlineNotice(QStringLiteral("Progress set to %1%.").arg(pct));
        reloadIssues();
    };
    for (QLabel *v : {m_issueAssigneesValue, m_issueLabelsValue,
                      m_issueMilestoneValue, m_issuePriorityValue,
                      m_issueEstimateValue,
                      m_issueBountyValue}) {
        v->setObjectName("statusLine");
        v->setWordWrap(true);
        v->setTextFormat(Qt::RichText);
        v->setOpenExternalLinks(false);
        v->setTextInteractionFlags(Qt::TextBrowserInteraction);
    }
    connect(m_issueAssigneesValue, &QLabel::linkActivated, this, [this] {
        editIssueAssignees();
        if (!m_issueAssigneesEdit)
            return;
        const QString who = m_userName.trimmed();
        if (who.isEmpty()) {
            setIssueInlineNotice("Set your profile name before assigning yourself.", true);
            return;
        }
        QStringList assignees = splitIssueFieldList(m_issueAssigneesEdit->text());
        if (!assignees.contains(who))
            assignees << who;
        m_issueAssigneesEdit->setText(assignees.join(", "));
    });
    m_issueLabelsButton = new QPushButton;
    m_issueMilestoneButton = new QPushButton;
    m_issuePriorityButton = new QPushButton;
    m_issueProgressButton = new QPushButton;
    m_issueBountyButton = new QPushButton;
    m_issueAssigneesButton = new QPushButton;
    m_issueDeleteButton = new QPushButton("Delete issue");
    for (QPushButton *b : {m_issueLabelsButton, m_issueMilestoneButton,
                           m_issuePriorityButton, m_issueProgressButton,
                           m_issueBountyButton,
                           m_issueAssigneesButton}) {
        b->setObjectName("issueIconButton");
        b->setFixedSize(28, 28);
        b->setCursor(Qt::PointingHandCursor);
        setOcticon(b, "gear", 15);
    }
    m_issueDeleteButton->setObjectName("issueDangerLink");
    m_issueDeleteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueDeleteButton, "trash", 15);
    // Quick priority nudges (a quarter of the 1..99 span per click) shown beside
    // the Priority gear: chevron-up raises priority, chevron-down lowers it.
    m_issuePriorityRaiseButton = new QPushButton;
    m_issuePriorityLowerButton = new QPushButton;
    for (QPushButton *b : {m_issuePriorityRaiseButton, m_issuePriorityLowerButton}) {
        b->setObjectName("issueIconButton");
        b->setFixedSize(28, 28);
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_issuePriorityRaiseButton, "chevron-up", 15);
    setOcticon(m_issuePriorityLowerButton, "chevron-down", 15);
    m_issuePriorityRaiseButton->setToolTip("Raise priority (more important)");
    m_issuePriorityLowerButton->setToolTip("Lower priority (less important)");
    connect(m_issuePriorityRaiseButton, &QPushButton::clicked, this,
            [this] { nudgeIssuePriority(-1); });
    connect(m_issuePriorityLowerButton, &QPushButton::clicked, this,
            [this] { nudgeIssuePriority(1); });
    // Quick "in progress" nudge: bump completion by 10% beside the Progress gear.
    m_issueProgressBoostButton = new QPushButton;
    m_issueProgressBoostButton->setObjectName("issueIconButton");
    m_issueProgressBoostButton->setFixedSize(28, 28);
    m_issueProgressBoostButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueProgressBoostButton, "plus", 15);
    m_issueProgressBoostButton->setToolTip("Mark in progress (+10%)");
    connect(m_issueProgressBoostButton, &QPushButton::clicked, this,
            [this] { nudgeIssueProgress(10); });
    auto makeValue = [](const QString &text) {
        auto *label = new QLabel(text);
        label->setObjectName("statusLine");
        label->setWordWrap(true);
        label->setTextFormat(Qt::RichText);
        return label;
    };
    auto makeGear = [&]() {
        auto *button = new QPushButton;
        button->setObjectName("issueIconButton");
        button->setFixedSize(28, 28);
        button->setEnabled(false);
        setOcticon(button, "gear", 15);
        return button;
    };
    auto makeAction = [&](const QString &text, const QString &icon = QString()) {
        auto *button = new QPushButton(text);
        button->setObjectName("issueSidebarAction");
        button->setCursor(Qt::PointingHandCursor);
        if (!icon.isEmpty())
            setOcticon(button, icon, 15);
        return button;
    };
    auto makeEditorButton = [&](const QString &text, const char *objectName) {
        auto *button = new QPushButton(text, meta);
        button->setObjectName(objectName);
        button->setProperty("buttonSize", "sm");
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    auto makeInlineButtonRow = [&](QPushButton *save, QPushButton *cancel) {
        auto *rowWidget = new QWidget(meta);
        auto *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        row->addStretch();
        row->addWidget(cancel);
        row->addWidget(save);
        return rowWidget;
    };

    m_issueAssigneesStack = new QStackedWidget(meta);
    m_issueAssigneesStack->addWidget(m_issueAssigneesValue);
    auto *assigneesEditBox = new QWidget(meta);
    auto *assigneesEditLayout = new QVBoxLayout(assigneesEditBox);
    assigneesEditLayout->setContentsMargins(0, 0, 0, 0);
    assigneesEditLayout->setSpacing(6);
    m_issueAssigneesEdit = new QLineEdit(meta);
    m_issueAssigneesEdit->setPlaceholderText("No one");
    auto *assignSelf = makeEditorButton("Assign yourself", "ghostButton");
    auto *assigneesSave = makeEditorButton("Save", "primaryButton");
    auto *assigneesCancel = makeEditorButton("Cancel", "ghostButton");
    assigneesEditLayout->addWidget(m_issueAssigneesEdit);
    assigneesEditLayout->addWidget(assignSelf, 0, Qt::AlignLeft);
    assigneesEditLayout->addWidget(makeInlineButtonRow(assigneesSave, assigneesCancel));
    m_issueAssigneesStack->addWidget(assigneesEditBox);
    connect(assignSelf, &QPushButton::clicked, this, [this] {
        if (!m_issueAssigneesEdit)
            return;
        const QString who = m_userName.trimmed();
        if (who.isEmpty()) {
            setIssueInlineNotice("Set your profile name before assigning yourself.", true);
            return;
        }
        QStringList assignees = splitIssueFieldList(m_issueAssigneesEdit->text());
        if (!assignees.contains(who))
            assignees << who;
        m_issueAssigneesEdit->setText(assignees.join(", "));
    });
    connect(assigneesSave, &QPushButton::clicked, this,
            &MainWindow::saveIssueAssigneesInline);
    connect(assigneesCancel, &QPushButton::clicked, this,
            &MainWindow::cancelIssueSidebarEditors);
    connect(m_issueAssigneesEdit, &QLineEdit::returnPressed, this,
            &MainWindow::saveIssueAssigneesInline);

    m_issueLabelsStack = new QStackedWidget(meta);
    m_issueLabelsStack->addWidget(m_issueLabelsValue);
    auto *labelsEditBox = new QWidget(meta);
    auto *labelsEditLayout = new QVBoxLayout(labelsEditBox);
    labelsEditLayout->setContentsMargins(0, 0, 0, 0);
    labelsEditLayout->setSpacing(6);
    m_issueLabelsEdit = new QLineEdit(meta);
    m_issueLabelsEdit->setPlaceholderText("No labels");
    auto *labelsSave = makeEditorButton("Save", "primaryButton");
    auto *labelsCancel = makeEditorButton("Cancel", "ghostButton");
    labelsEditLayout->addWidget(m_issueLabelsEdit);
    labelsEditLayout->addWidget(makeInlineButtonRow(labelsSave, labelsCancel));
    m_issueLabelsStack->addWidget(labelsEditBox);
    connect(labelsSave, &QPushButton::clicked, this,
            &MainWindow::saveIssueLabelsInline);
    connect(labelsCancel, &QPushButton::clicked, this,
            &MainWindow::cancelIssueSidebarEditors);
    connect(m_issueLabelsEdit, &QLineEdit::returnPressed, this,
            &MainWindow::saveIssueLabelsInline);

    m_issueMilestoneStack = new QStackedWidget(meta);
    m_issueMilestoneStack->addWidget(m_issueMilestoneValue);
    auto *milestoneEditBox = new QWidget(meta);
    auto *milestoneEditLayout = new QVBoxLayout(milestoneEditBox);
    milestoneEditLayout->setContentsMargins(0, 0, 0, 0);
    milestoneEditLayout->setSpacing(6);
    m_issueMilestoneEdit = new QComboBox(meta);
    auto *milestoneSave = makeEditorButton("Save", "primaryButton");
    auto *milestoneCancel = makeEditorButton("Cancel", "ghostButton");
    milestoneEditLayout->addWidget(m_issueMilestoneEdit);
    milestoneEditLayout->addWidget(makeInlineButtonRow(milestoneSave, milestoneCancel));
    m_issueMilestoneStack->addWidget(milestoneEditBox);
    connect(milestoneSave, &QPushButton::clicked, this,
            &MainWindow::saveIssueMilestoneInline);
    connect(milestoneCancel, &QPushButton::clicked, this,
            &MainWindow::cancelIssueSidebarEditors);

    m_issuePriorityStack = new QStackedWidget(meta);
    m_issuePriorityStack->addWidget(m_issuePriorityValue);
    auto *priorityEditBox = new QWidget(meta);
    auto *priorityEditLayout = new QVBoxLayout(priorityEditBox);
    priorityEditLayout->setContentsMargins(0, 0, 0, 0);
    priorityEditLayout->setSpacing(6);
    m_issuePriorityEdit = new QComboBox(meta);
    m_issuePriorityEdit->addItem("No priority", 0);
    for (int priority = 1; priority <= 99; ++priority)
        m_issuePriorityEdit->addItem(QString::number(priority), priority);
    m_issuePriorityEdit->setToolTip("1 is highest priority; 99 is lowest");
    auto *prioritySave = makeEditorButton("Save", "primaryButton");
    auto *priorityCancel = makeEditorButton("Cancel", "ghostButton");
    priorityEditLayout->addWidget(m_issuePriorityEdit);
    priorityEditLayout->addWidget(makeInlineButtonRow(prioritySave, priorityCancel));
    m_issuePriorityStack->addWidget(priorityEditBox);
    connect(prioritySave, &QPushButton::clicked, this,
            &MainWindow::saveIssuePriorityInline);
    connect(priorityCancel, &QPushButton::clicked, this,
            &MainWindow::cancelIssueSidebarEditors);

    auto *agentBox = new QWidget(meta);
    auto *agentLayout = new QVBoxLayout(agentBox);
    agentLayout->setContentsMargins(0, 0, 0, 0);
    agentLayout->setSpacing(6);
    m_issueAgentValue = new QLabel("No agent assigned", meta);
    m_issueAgentValue->setObjectName("statusLine");
    m_issueAgentValue->setWordWrap(true);
    m_issueAgentValue->setTextFormat(Qt::RichText);
    // The same two API-key providers as the issue-list quick-add.
    m_issueAgentProvider = new QComboBox(meta);
    m_issueAgentProvider->addItem(QStringLiteral("OpenAI API"),
                                  QStringLiteral("openai"));
    m_issueAgentProvider->addItem(QStringLiteral("Claude API"),
                                  QStringLiteral("claude-api"));
    // "Claude Code" drives the real `claude` CLI headlessly (no input) in a
    // tracked agent session, working until ForkMesh can open a PR from its diff.
    m_issueAgentProvider->addItem(QStringLiteral("Claude Code"),
                                  QStringLiteral("claude-code"));
    selectDefaultAgentProvider(m_issueAgentProvider);
    m_issueAgentProvider->setToolTip("Which agent to run on this issue");
    m_issueAssignAgentButton = makeEditorButton("Assign agent", "ghostButton");
    m_issueAgentCreatePrCheck = new QCheckBox("Create a PR", meta);
    m_issueAgentCreatePrCheck->setToolTip(
        "If the agent produces a patch, create a ForkMesh pull request from it.");
    m_issueAgentViewButton = makeEditorButton("View session", "primaryButton");
    m_issueAgentViewButton->hide();
    setOcticon(m_issueAssignAgentButton, "rocket", 15);
    setOcticon(m_issueAgentViewButton, "chevron-right", 15);
    // IDE hand-off: shown only when "IDE integration" is enabled in Settings and
    // the ForkMesh VS Code / Codeium extension is detected running. These run the
    // issue through the IDE's Claude Code / Codex rather than the API-key agents.
    m_issueIdeLabel = new QLabel("Run in IDE", meta);
    m_issueIdeLabel->setObjectName("statusLine");
    m_issueIdeClaudeButton = makeEditorButton("Claude Code", "ghostButton");
    m_issueIdeCodexButton = makeEditorButton("Codex", "ghostButton");
    setOcticon(m_issueIdeClaudeButton, "rocket", 15);
    setOcticon(m_issueIdeCodexButton, "terminal", 15);
    m_issueIdeClaudeButton->setToolTip(
        "Start Claude Code on this issue in your connected IDE");
    m_issueIdeCodexButton->setToolTip(
        "Start Codex on this issue in your connected IDE");
    auto *ideRow = new QHBoxLayout;
    ideRow->setContentsMargins(0, 0, 0, 0);
    ideRow->setSpacing(6);
    ideRow->addWidget(m_issueIdeClaudeButton);
    ideRow->addWidget(m_issueIdeCodexButton);
    ideRow->addStretch();

    agentLayout->addWidget(m_issueAgentValue);
    agentLayout->addWidget(m_issueAgentProvider);
    agentLayout->addWidget(m_issueAssignAgentButton);
    agentLayout->addWidget(m_issueAgentCreatePrCheck);
    agentLayout->addWidget(m_issueAgentViewButton, 0, Qt::AlignLeft);
    agentLayout->addWidget(m_issueIdeLabel);
    agentLayout->addLayout(ideRow);
    connect(m_issueAssignAgentButton, &QPushButton::clicked, this, [this] {
        if (m_issueAgentProvider)
            assignIssueToAgent(m_issueAgentProvider->currentData().toString());
    });
    connect(m_issueAgentViewButton, &QPushButton::clicked, this,
            &MainWindow::openAgentSessionFromIssue);
    connect(m_issueIdeClaudeButton, &QPushButton::clicked, this, [this] {
        startIssueInIde(m_currentIssueNumber, m_currentIssueTitle,
                        QStringLiteral("claude"));
    });
    connect(m_issueIdeCodexButton, &QPushButton::clicked, this, [this] {
        startIssueInIde(m_currentIssueNumber, m_currentIssueTitle,
                        QStringLiteral("codex"));
    });
    m_issueIdeLabel->hide();
    m_issueIdeClaudeButton->hide();
    m_issueIdeCodexButton->hide();

    auto *metaLayout = new QVBoxLayout(meta);
    metaLayout->setContentsMargins(22, 22, 10, 22);
    metaLayout->setSpacing(0);
    auto addDivider = [&] {
        auto *line = new QWidget(meta);
        line->setObjectName("issueSidebarDivider");
        line->setFixedHeight(1);
        metaLayout->addWidget(line);
    };
    auto addMetaSection = [&](const QString &label, QWidget *value,
                              QPushButton *btn = nullptr,
                              const QList<QPushButton *> &extraBtns = {}) {
        auto *header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        auto *l = new QLabel(label);
        l->setObjectName("issueSidebarHeading");
        header->addWidget(l);
        header->addStretch();
        for (QPushButton *extra : extraBtns)
            header->addWidget(extra);
        if (btn)
            header->addWidget(btn);
        metaLayout->addLayout(header);
        metaLayout->addWidget(value);
        metaLayout->addSpacing(14);
        addDivider();
        metaLayout->addSpacing(14);
    };
    addMetaSection("Assignees", m_issueAssigneesStack, m_issueAssigneesButton);
    addMetaSection("Assign to agent", agentBox);
    addMetaSection("Labels", m_issueLabelsStack, m_issueLabelsButton);
    addMetaSection("Type", makeValue("No type"), makeGear());
    addMetaSection("Priority", m_issuePriorityStack, m_issuePriorityButton,
                   {m_issuePriorityRaiseButton, m_issuePriorityLowerButton});
    addMetaSection("Progress", m_issueProgressSlider, m_issueProgressButton,
                   {m_issueProgressBoostButton});
    addMetaSection("Est. OpenAI cost", m_issueEstimateValue);
    addMetaSection("Bounty", m_issueBountyValue, m_issueBountyButton);
    addMetaSection("Projects", makeValue("No projects"), makeGear());
    addMetaSection("Milestone", m_issueMilestoneStack, m_issueMilestoneButton);
    addMetaSection("Relationships", makeValue("None yet"), makeGear());
    m_issueDevelopmentValue = makeValue("No linked pull requests.");
    m_issueDevelopmentValue->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_issueDevelopmentValue->setOpenExternalLinks(false);
    connect(m_issueDevelopmentValue, &QLabel::linkActivated, this,
            [this](const QString &href) {
                const int n = href.section(QLatin1Char(':'), 1).toInt();
                if (n > 0)
                    switchToPullTab(n);
            });
    m_issueLinkPullButton = new QPushButton;
    m_issueLinkPullButton->setObjectName("issueIconButton");
    m_issueLinkPullButton->setFixedSize(28, 28);
    m_issueLinkPullButton->setCursor(Qt::PointingHandCursor);
    m_issueLinkPullButton->setToolTip("Link a pull request to this issue");
    setOcticon(m_issueLinkPullButton, "git-pull-request", 15);
    connect(m_issueLinkPullButton, &QPushButton::clicked, this,
            &MainWindow::linkPullToIssueFromIssuePage);
    addMetaSection("Development", m_issueDevelopmentValue, m_issueLinkPullButton);
    addMetaSection("Notifications", makeValue("You are receiving notifications because you're subscribed to this thread."));
    addMetaSection("Participants", makeValue("No participants"));
    auto *transferIssue = makeAction("Transfer issue", "arrow-left");
    auto *cloneIssue = makeAction("Clone issue", "copy");
    auto *lockIssue = makeAction("Lock conversation", "lock");
    auto *pinIssue = makeAction("Pin issue", "tag");
    // Copy a forkmesh:// permalink to this issue (issue #154): pasted into a
    // comment it renders as a link back here via autolinkReferences().
    auto *copyIssueLink = makeAction("Copy link", "link");
    copyIssueLink->setToolTip(
        "Copy a link to this issue you can paste into an issue or PR comment");
    connect(copyIssueLink, &QPushButton::clicked, this, [this] {
        if (m_currentIssueNumber > 0)
            copyReferenceLink(QStringLiteral("issue"),
                              QString::number(m_currentIssueNumber));
    });
    auto *feedbackIssue = makeAction("Give feedback", "comment");
    for (QPushButton *action :
         {transferIssue, cloneIssue, lockIssue, pinIssue, copyIssueLink,
          m_issueDeleteButton, feedbackIssue})
        metaLayout->addWidget(action);
    metaLayout->addStretch();

    auto *metaScroll = new QScrollArea;
    metaScroll->setObjectName("issueSidebarScroll");
    metaScroll->setFrameShape(QFrame::NoFrame);
    metaScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    metaScroll->setWidgetResizable(true);
    metaScroll->setMinimumWidth(265);
    metaScroll->setMaximumWidth(335);
    // The long issue sidebar should scroll, not force the Issues tab height.
    metaScroll->setMinimumHeight(0);
    metaScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    metaScroll->setWidget(meta);

    // Collapsible detail panel: the issue thread (center) + metadata sidebar,
    // sharing their own draggable divider.
    auto *issueDetailView = new QWidget;
    auto *detailSplit = new QSplitter(Qt::Horizontal);
    detailSplit->setChildrenCollapsible(false);
    detailSplit->addWidget(center);
    detailSplit->addWidget(metaScroll);
    detailSplit->setStretchFactor(0, 1);
    detailSplit->setStretchFactor(1, 0);
    detailSplit->setSizes({520, 220});

    // Issue #145: a "Files changed" tab beside the issue thread, mirroring the
    // agent detail. A file list scrolls a diff viewer; selecting a file jumps to
    // its hunk, activating one opens it (when it lives in a worktree on disk).
    m_issueFilesList = new QListWidget;
    m_issueFilesList->setObjectName("agentFilesList");
    m_issueFilesList->setMinimumWidth(190);
    // Green-outline selection (like the agents list) so the selected file stays
    // legible; click-to-scroll / scroll-to-select handled by DiffFileNavigator.
    m_issueFilesList->setItemDelegate(
        new SelectionBorderRowDelegate(m_issueFilesList));
    connect(m_issueFilesList, &QListWidget::itemActivated, this,
            [](QListWidgetItem *it) {
                const QString path = it->data(Qt::UserRole).toString();
                if (!path.isEmpty() && QFileInfo::exists(path))
                    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            });
    m_issueFilesChangedSummary = new QLabel;
    m_issueFilesChangedSummary->setObjectName("agentFilesHeading");
    auto *issueFilesV = new QVBoxLayout;
    issueFilesV->setContentsMargins(0, 0, 0, 0);
    issueFilesV->setSpacing(4);
    issueFilesV->addWidget(m_issueFilesChangedSummary);
    issueFilesV->addWidget(m_issueFilesList, 1);
    auto *issueFilesPanel = new QWidget;
    issueFilesPanel->setLayout(issueFilesV);

    m_issueDiffView = new QTextBrowser;
    m_issueDiffView->setObjectName("diffView");
    m_issueDiffView->setOpenExternalLinks(false);
    m_issueDiffView->setLineWrapMode(QTextEdit::NoWrap);
    // DiffFileNavigator is created lazily on first render (complete type in scope).

    auto *issueFilesSplit = new QSplitter(Qt::Horizontal);
    issueFilesSplit->setChildrenCollapsible(false);
    issueFilesSplit->addWidget(issueFilesPanel);
    issueFilesSplit->addWidget(m_issueDiffView);
    issueFilesSplit->setStretchFactor(0, 0);
    issueFilesSplit->setStretchFactor(1, 1);
    issueFilesSplit->setSizes({220, 700});
    auto *issueFilesPage = new QWidget;
    auto *issueFilesPageLayout = new QVBoxLayout(issueFilesPage);
    issueFilesPageLayout->setContentsMargins(0, 8, 0, 0);
    issueFilesPageLayout->addWidget(issueFilesSplit, 1);

    m_issueDetailTabs = new QTabWidget;
    m_issueDetailTabs->setObjectName("agentDetailTabs"); // reuse the agent tab style
    m_issueDetailTabs->addTab(detailSplit, QStringLiteral("Issue"));
    m_issueFilesTabIndex =
        m_issueDetailTabs->addTab(issueFilesPage, QStringLiteral("Files changed"));
    m_issueDetailTabs->setTabVisible(m_issueFilesTabIndex, false);

    auto *detailLayout = new QVBoxLayout(issueDetailView);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(m_issueDetailTabs);

    m_issueDetailStack = new QStackedWidget;
    m_issueDetailStack->addWidget(issueDetailView);
    m_issueDetail = m_issueDetailStack;

    // The table and the detail panel share a draggable divider; hiding the
    // detail lets the table use the full width.
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName("issuesSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(listPane);
    splitter->addWidget(m_issueDetail);
    // The table is the primary surface: it keeps the width and the detail panel
    // opens at a minimal size beside it (the divider is still draggable).
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({900, 440});
    // Open full width: the table fills the page until an issue is selected, at
    // which point showIssue() reveals the detail pane beside it.
    m_issueDetail->hide();

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter);

    connect(m_issueDetailToggle, &QPushButton::clicked, this, [this] {
        const bool show = !m_issueDetail->isVisible();
        m_issueDetail->setVisible(show);
        m_issueDetailToggle->setText(show ? "Hide detail" : "Show detail");
    });
    connect(m_issuesRepoCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { reloadIssues(); });
    connect(m_issueStatusFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshIssueList(); });
    connect(m_issueLabelFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshIssueList(); });
    connect(m_issueMilestoneFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshIssueList(); });
    connect(m_issueSearch, &QLineEdit::textChanged, this,
            [this] { refreshIssueList(); });
    connect(issueTabGroup, &QButtonGroup::idClicked, this,
            [this](int id) { selectIssueListTab(id); });
    // Clicking a milestone's Open (col 1) or Closed (col 2) count jumps to the
    // Issues tab filtered to that milestone + status (issue #172).
    connect(m_issueMilestonesTable, &QTableWidget::cellClicked, this,
            [this](int row, int col) {
        if (col != 1 && col != 2)
            return;
        QTableWidgetItem *titleItem = m_issueMilestonesTable->item(row, 0);
        if (!titleItem)
            return;
        const QString milestone = titleItem->text();
        // Switch to the Issues table tab (also restores the filter controls).
        if (m_issueTabGroup && m_issueTabGroup->button(0))
            m_issueTabGroup->button(0)->setChecked(true);
        selectIssueListTab(0);
        // Match the milestone, and Open/Closed for the column clicked.
        const int msIndex = m_issueMilestoneFilter->findText(milestone);
        if (msIndex >= 0)
            m_issueMilestoneFilter->setCurrentIndex(msIndex);
        m_issueStatusFilter->setCurrentText(col == 1 ? "Open" : "Closed");
        refreshIssueList();
    });
    connect(m_issueTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const QModelIndexList rows = m_issueTable->selectionModel()->selectedRows();
        if (rows.isEmpty())
            return;
        QTableWidgetItem *first = m_issueTable->item(rows.first().row(), 0);
        if (first)
            showIssue(first->data(Qt::UserRole).toInt());
    });
    // Double-clicking a cell opens that field's editor for the row's issue (the
    // same signed-write editors the detail-panel buttons use). Columns that hold
    // derived or signed-immutable values just open the issue in the detail pane.
    connect(m_issueTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int col) {
        QTableWidgetItem *first = m_issueTable->item(row, 0);
        if (!first)
            return;
        showIssue(first->data(Qt::UserRole).toInt());
        if (m_currentIssueNumber < 0)
            return;
        switch (col) {
        case 1:  promptEditIssueTitle(); break; // Title
        case 2:  editIssuePriority();    break; // Priority
        case 3:  toggleIssueStatus();    break; // Status (open <-> closed)
        case 5:  editIssueLabels();      break; // Labels
        case 6:  editIssueMilestone();   break; // Milestone
        case 11: editIssueProgress();    break; // Progress
        case 13: editIssueBounty();      break; // Bounty
        default: break; // #, Votes, Created, Updated, Agent, Author, Est. cost
        }
    });
    connect(m_issueLabelsTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { editIssueLabelDefinition(row); });
    connect(m_issueNewButton, &QPushButton::clicked, this, &MainWindow::promptNewIssue);
    connect(m_issueSyncButton, &QPushButton::clicked, this,
            &MainWindow::syncIssuesInbox);
    connect(issueBurnupButton, &QPushButton::clicked, this,
            &MainWindow::showIssueBurnupChart);
    connect(m_issueTitleEditButton, &QPushButton::clicked, this,
            &MainWindow::promptEditIssueTitle);
    connect(m_issueTitleSaveButton, &QPushButton::clicked, this,
            &MainWindow::saveIssueTitleEdit);
    connect(m_issueTitleCancelButton, &QPushButton::clicked, this,
            &MainWindow::cancelIssueTitleEdit);
    connect(m_issueTitleEditor, &QLineEdit::returnPressed, this,
            &MainWindow::saveIssueTitleEdit);
    connect(m_issueCopyButton, &QPushButton::clicked, this,
            &MainWindow::copyIssueToClipboard);
    connect(m_issueCopyAllButton, &QPushButton::clicked, this,
            &MainWindow::copyIssueThreadToClipboard);
    connect(m_issueVoteButton, &QPushButton::clicked, this,
            &MainWindow::voteOnCurrentIssue);
    connect(m_issueCommentButton, &QPushButton::clicked, this,
            &MainWindow::addIssueComment);
    connect(m_issueAskAiButton, &QPushButton::clicked, this,
            &MainWindow::askAiForCurrentIssue);
    connect(m_issueAttachButton, &QPushButton::clicked, this,
            &MainWindow::attachIssueImage);
    connect(m_issueCloseButton, &QPushButton::clicked, this,
            &MainWindow::toggleIssueStatus);
    connect(m_issueCloseCommentButton, &QPushButton::clicked, this,
            &MainWindow::closeIssueWithComment);
    connect(m_issueDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteCurrentIssue);
    connect(m_issueLabelsButton, &QPushButton::clicked, this,
            &MainWindow::editIssueLabels);
    connect(m_issueMilestoneButton, &QPushButton::clicked, this,
            &MainWindow::editIssueMilestone);
    connect(m_issuePriorityButton, &QPushButton::clicked, this,
            &MainWindow::editIssuePriority);
    connect(m_issueProgressButton, &QPushButton::clicked, this,
            &MainWindow::editIssueProgress);
    connect(m_issueBountyButton, &QPushButton::clicked, this,
            &MainWindow::editIssueBounty);
    connect(m_issueAssigneesButton, &QPushButton::clicked, this,
            &MainWindow::pickIssueAssignees);
    return page;
}

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

    // --- Tab bar (GitHub order; Commits gets its own tab).
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
                                {"Security and quality", "shield-check"},
                                {"Insights", "graph"},
                                {"Branches", "repo-forked"},
                                {"Worktrees", "file-directory"},
                                {"Releases", "tag"},
                                {"Mirror nodes", "server"},
                                {"Settings", "gear"}};
    m_repoDetailTabs = new QButtonGroup(this);
    m_repoDetailTabs->setExclusive(true);
    auto *tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(12, 0, 12, 0);
    tabRow->setSpacing(2);
    for (int i = 0; i < tabs.size(); ++i) {
        const TabDef tab = tabs.at(i);
        auto *b = new QPushButton(QString::fromLatin1(tab.label));
        b->setObjectName("repoTab");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        setOcticon(b, QString::fromLatin1(tab.icon), 16);
        if (i == 0)
            b->setChecked(true);
        if (i == 0)
            m_repoCodeTab = b; // visible pill next to Commits; also shows repo size
        if (i == 1)
            m_repoCommitsTab = b;
        if (i == 2)
            m_repoIssuesTab = b; // keep a handle for the Issues (N) badge; the
                                 // looper toggle floats just above this tab
                                 // (adhoc #130, created below).
        if (i == 3) {
            m_repoAgentsTab = b; // handle for the Agents (N) badge + spinner strip
            // Purple braille snake overlaid at the tab's right edge while an agent
            // runs; kept separate so "Agents (N)" stays its normal colour.
            m_agentSnake = new QLabel(b);
            m_agentSnake->setObjectName("agentSnake");
            m_agentSnake->setAlignment(Qt::AlignCenter);
            m_agentSnake->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_agentSnake->setStyleSheet(
                "#agentSnake{color:#a371f7;background:transparent;}");
            m_agentSnake->hide();
        }
        if (i == 4)
            m_repoPullsTab = b;
        if (i == 5)
            m_repoDiscussionsTab = b;
        if (i == 6)
            m_repoActionsTab = b; // handle for the Actions (N) badge
        if (i == 9)
            m_repoBranchesTab = b; // handle for the Branches (N) badge
        if (i == 10)
            m_repoWorktreesTab = b; // handle for the Worktrees (N) badge
        if (i == 12)
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
    // The "Sync changes" button floats in the band just above the Commits tab
    // (see positionRepoPushButton) rather than living in the tab row: it's an
    // overlay raised one above the tabs, so showing/hiding it as sync state
    // changes never reflows the tab content below — that shift is what read as the
    // whole view "resizing" on small screens, most visibly on Mirror nodes.
    m_repoPublishBar = nullptr; // no separate row: the button floats over Commits

    // Issue-looper toggle (adhoc #130): a compact switch floating in the band
    // just above the Issues tab, mirroring how the Sync button floats over
    // Commits. It both shows the loop's state and toggles it, so the loop is
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

    // --- Inner stack: one page per tab.
    m_repoDetailStack = new QStackedWidget;
    m_repoDetailStack->addWidget(buildRepoFilesPanel());                 // 0 Code
    m_repoDetailStack->addWidget(buildRepoCommitsTab());                 // 1 Commits
    m_repoDetailStack->addWidget(buildIssuesSection());                  // 2 Issues
    m_repoDetailStack->addWidget(buildAgentsTab());                      // 3 Agents
    m_repoDetailStack->addWidget(buildPullsTab());                       // 4 Pull requests
    m_repoDetailStack->addWidget(buildDiscussionsTab());                 // 5 Discussions
    m_repoDetailStack->addWidget(buildRepoActionsTab());                 // 6 Actions
    m_repoDetailStack->addWidget(buildRepoSecurityTab());                // 7 Security and quality
    m_insightsTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildInsightsTab());                    // 8
    m_branchesTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildBranchesTab());                    // 9 Branches
    m_worktreesTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildWorktreesTab());                   // 10 Worktrees
    m_releasesTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildReleasesTab());                    // 11 Releases
    m_mirrorNodesTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildMirrorNodesTab());                 // 12 Mirror nodes
    m_settingsTabIndex = m_repoDetailStack->count();
    m_repoDetailStack->addWidget(buildRepoSettingsTab());                // 13 Settings
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
        if (id == 2) {
            // Opening Issues: clear any filter the user left set on a prior visit
            // (status/label/milestone/search) so the full list shows again.
            resetIssueFilters();
            if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
                // Drain this repo's inbox now (owner-only) so incoming issues from
                // other nodes show immediately instead of next poll tick.
                drainIssuesInboxFor(m_repositories.at(m_repoDetailIndex), false);
        }
        if (id == 1) {
            // The repo's commits were already loaded when it opened, so a tab
            // click usually rebuilds an identical 300-row table (4 git
            // subprocesses + per-row widgets). Skip that when nothing changed.
            //
            // Run inline, this whole block (git status + the openMostRecentCommit
            // diff read) blocks before the tab can paint — setCurrentIndex(1)
            // above only queues the paint, which can't process until this slot
            // returns, so the Commits page stays blank (still showing the prior
            // tab) for the delay, then snaps in. Defer to the next event-loop tick
            // like the Agents tab below: the tab paints first, then the work runs
            // (openMostRecentCommit shows its own spinner across the diff read).
            QTimer::singleShot(0, this, [this] {
                if (commitsListIsCurrent()) {
                    // The commit list may be current, but the working tree can
                    // still have moved (an agent staged/edited files) — always
                    // rescan the changes panel so it's fresh on tab open.
                    refreshSourceControl();
                } else {
                    loadCommits();
                }
                // Land on the newest commit's change view, not an empty list.
                openMostRecentCommit();
            });
        }
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
            loadRepoInsights();
        else if (id == m_branchesTabIndex)
            loadBranchesPanel();
        else if (id == m_worktreesTabIndex)
            loadWorktreesPanel();
        else if (id == m_releasesTabIndex)
            loadReleasesPanel();
        else if (id == m_mirrorNodesTabIndex)
            loadMirrorNodesPanel();
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
    m_commitsBranchButton->setToolTip("Current branch — click to switch or create one");
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
    // "#123" references in the message are rendered as ref: links and open
    // issues first. PR/comment bodies use typed Markdown reference links.
    connect(m_commitMessage, &QLabel::linkActivated, this,
            [this](const QString &href) {
                if (href.startsWith(QStringLiteral("ref:")))
                    openCommitReference(href.mid(4).toInt());
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

