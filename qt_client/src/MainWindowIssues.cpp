// MainWindowIssues: MainWindow feature methods, split out of MainWindow.cpp.
// Issues: the issues list/board UI plus all issue logic — filters, board
// columns, detail thread, compose/quick-add, comments, labels, milestones,
// priority, voting, burnup, and AI assist.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"

#include <QLayoutItem>
#include <QPair>
#include <QPixmap>

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
    // A second "New issue" button at the top of the list pane so filing an issue
    // doesn't require first selecting one to reach the button in the detail header
    // (adhoc #11). Shares promptNewIssue and the same enable/disable rule.
    m_issueListNewButton = new QPushButton("New issue");
    m_issueListNewButton->setObjectName("primaryButton");
    m_issueListNewButton->setProperty("buttonSize", "sm");
    m_issueListNewButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueListNewButton, "plus", 16);

    auto *headingRow = new QHBoxLayout;
    headingRow->setContentsMargins(0, 0, 0, 0);
    headingRow->addWidget(heading);
    headingRow->addWidget(m_issueListNewButton);
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
    installColumnHeaderMenu(m_issueTable); // 3-dots per-column menu (adhoc #73)
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
    installColumnHeaderMenu(m_issueMilestonesTable); // 3-dots per-column menu (adhoc #73)
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
    installColumnHeaderMenu(m_issueLabelsTable); // 3-dots per-column menu (adhoc #73)
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
    // Speak the comment with the voice engine, just like the footer prompt mic.
    commentButtonRow->addWidget(makeVoiceButton(m_issueComposer), 0, Qt::AlignLeft);
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
    // Model picker beneath the provider so a run can target a specific model
    // (e.g. Opus / Sonnet / Haiku for Claude), refilled when the provider
    // changes. Empty "Default model" leaves the provider's own default in place.
    m_issueAgentModel = new QComboBox(meta);
    m_issueAgentModel->setToolTip("Which model the agent uses");
    m_issueAgentModel->setProperty("claudeModelCombo", true);
    m_issueAgentModel->view()->installEventFilter(this);
    fillAgentFixModelCombo(m_issueAgentModel,
                           m_issueAgentProvider->currentData().toString());
    connect(m_issueAgentProvider,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
                if (m_issueAgentProvider && m_issueAgentModel)
                    fillAgentFixModelCombo(
                        m_issueAgentModel,
                        m_issueAgentProvider->currentData().toString());
            });
    refreshClaudeModelCombo();
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
    agentLayout->addWidget(m_issueAgentModel);
    agentLayout->addWidget(m_issueAssignAgentButton);
    agentLayout->addWidget(m_issueAgentCreatePrCheck);
    agentLayout->addWidget(m_issueAgentViewButton, 0, Qt::AlignLeft);
    agentLayout->addWidget(m_issueIdeLabel);
    agentLayout->addLayout(ideRow);
    connect(m_issueAssignAgentButton, &QPushButton::clicked, this, [this] {
        if (m_issueAgentProvider)
            assignIssueToAgent(m_issueAgentProvider->currentData().toString(),
                               m_issueAgentModel
                                   ? m_issueAgentModel->currentData().toString()
                                   : QString());
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
    // Tighter left gutter: the splitter handle (+ the thread pane's own right
    // margin) already separate the sidebar from the issue thread, so a large
    // left margin here just left an empty channel down every section. Keep it
    // close to the right margin so the sections read as balanced.
    metaLayout->setContentsMargins(12, 22, 10, 22);
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
    registerDiffView(m_issueDiffView);
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
    connect(m_issueListNewButton, &QPushButton::clicked, this,
            &MainWindow::promptNewIssue);
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


// ---- Issue logic (list/board/detail/compose/comments/labels) ----
// Consolidated here from the old releases-region split.

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
    // Keep the agent-list compose row's repo picker (adhoc #234) in sync with
    // the same repository list.
    if (m_agentComposeRepo) {
        const QVariant prev = m_agentComposeRepo->count()
                                  ? m_agentComposeRepo->currentData()
                                  : QVariant();
        QSignalBlocker agentBlocker(m_agentComposeRepo);
        m_agentComposeRepo->clear();
        for (int i = 0; i < m_repositories.size(); ++i) {
            const RepositoryRecord &repo = m_repositories.at(i);
            if (repo.localPath.trimmed().isEmpty())
                continue; // no checkout to run an agent in
            m_agentComposeRepo->addItem(repo.owner + "/" + repo.name, i);
        }
        if (prev.isValid()) {
            const int restore = m_agentComposeRepo->findData(prev);
            if (restore >= 0)
                m_agentComposeRepo->setCurrentIndex(restore);
        }
    }
    reloadIssues();
}

QIcon MainWindow::issueAssigneeAvatar(const QString &name)
{
    const QString key = name.trimmed();
    if (key.isEmpty())
        return QIcon();
    auto it = m_assigneeAvatarCache.constFind(key);
    if (it != m_assigneeAvatarCache.constEnd())
        return it.value();
    // A deterministic procedural face keyed by the lower-cased name, matching the
    // contributor avatars elsewhere so the same person reads consistently.
    QIcon icon(roundedAvatar(forkMeshAvatarPng(key.toLower()), 18));
    m_assigneeAvatarCache.insert(key, icon);
    return icon;
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
        m_issuesLoadedSig.clear(); // force a full reload when a repo reopens
        renderIssueThread(Issue());
        updateIssueActionState();
        updateRepoIssueCount();
        return;
    }
    const IssueStore store = issueStoreForCurrentRepo();
    // Skip re-reading git and rebuilding the table when issues/ is byte-for-byte
    // unchanged since the last load (the common case: this fires on every push, but
    // a code-only commit doesn't touch issues/). Tearing the rows down and back up
    // mid-interaction drops the click/keystroke the user aimed at a row or the
    // search box, which is what made the app feel unresponsive. Filter changes call
    // refreshIssueList() directly, so they still re-filter the live data.
    const QString sig = store.contentSignature();
    if (!sig.isEmpty() && sig == m_issuesLoadedSig)
        return;
    m_issuesLoadedSig = sig;
    // Reading every issue's files is a long synchronous loop on a big repo (~2.5s
    // for a few hundred issues). When this runs inside an interactive load
    // (openRepoDetail's GitKeepAlive scope), pump the GUI between reads — same
    // throttle as waitForGit — so the window keeps breathing and the stall
    // watchdog doesn't fire. Outside such a scope the tick is a no-op, so a plain
    // post-push reload behaves exactly as before.
    m_currentIssues = store.loadAll(nullptr, [] {
        if (g_gitKeepAliveDepth > 0 &&
            keepAliveClock().elapsed() - g_lastKeepAlivePumpMs >= 100)
            pumpKeepAlive();
    });
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
        // Title, prefixed with the assignee's avatar (just the picture, between
        // the # column and the title text). Hovering it names who is assigned.
        auto *titleItem = new QTableWidgetItem(issue.title);
        if (!issue.assignees.isEmpty()) {
            titleItem->setIcon(issueAssigneeAvatar(issue.assignees.first()));
            titleItem->setToolTip(
                QStringLiteral("Assigned to %1").arg(issue.assignees.join(", ")));
        }
        m_issueTable->setItem(row, 1, titleItem);

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
        // Created date/time: ISO yyyy-MM-dd HH:mm sorts chronologically as plain
        // text; the tooltip carries the friendly "x ago" form.
        auto *created = new QTableWidgetItem(
            issue.createdAt > 0
                ? QDateTime::fromMSecsSinceEpoch(issue.createdAt).toString("yyyy-MM-dd HH:mm")
                : QString());
        created->setToolTip(formatIssueRelativeTime(issue.createdAt));
        m_issueTable->setItem(row, 7, created);

        // Updated date/time: the most recent activity on the issue (latest signed
        // event, falling back to the created time). ISO yyyy-MM-dd HH:mm sorts
        // chronologically as plain text; the tooltip carries the "x ago" form.
        qint64 updatedAt = issue.createdAt;
        for (const IssueEvent &ev : issue.events)
            updatedAt = qMax(updatedAt, ev.ts);
        auto *updated = new QTableWidgetItem(
            updatedAt > 0
                ? QDateTime::fromMSecsSinceEpoch(updatedAt).toString("yyyy-MM-dd HH:mm")
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
            // Parent at construction (rather than picking one up implicitly when
            // added to the layout) so the button is never briefly its own
            // top-level widget — that transient state can leave the "primaryButton"
            // QSS rule's background/text unpainted (border-only) on first show.
            auto *cancel = new QPushButton("Cancel", bodyContainer);
            cancel->setObjectName("ghostButton");
            cancel->setCursor(Qt::PointingHandCursor);
            auto *save = new QPushButton("Save", bodyContainer);
            save->setObjectName("primaryButton");
            save->setCursor(Qt::PointingHandCursor);
            save->style()->unpolish(save);
            save->style()->polish(save);
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
        // Comments can be deleted (the description/open event is removed by
        // deleting the whole issue, handled elsewhere). Deletion appends a
        // signed delete event that folds the comment out everywhere and syncs
        // to peers; the text stays in git history.
        if (writable && !isOpen) {
            menu->addSeparator();
            QAction *deleteComment = menu->addAction("Delete comment");
            connect(deleteComment, &QAction::triggered, this, [this, num, eid]() {
                if (QMessageBox::question(
                        this, QStringLiteral("Delete comment"),
                        QStringLiteral(
                            "Delete this comment? It will be hidden everywhere "
                            "and the deletion syncs to peers; the comment stays "
                            "in git history."),
                        QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::No) != QMessageBox::Yes)
                    return;
                IssueStore store = issueStoreForCurrentRepo();
                QString error;
                if (!store.deleteEvent(num, eid, &error)) {
                    setIssueInlineNotice(
                        error.isEmpty() ? "Could not delete the comment." : error,
                        true);
                    return;
                }
                setIssueInlineNotice("Comment deleted.");
                reloadIssues();
            });
        }
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
    if (m_issueListNewButton)
        m_issueListNewButton->setEnabled(writable || issuesRepoIndex() >= 0);
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
    if (m_issueAgentModel)
        m_issueAgentModel->setEnabled(writable && haveIssue);
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
    createButton->setToolTip("Create the issue (Ctrl+Enter)");
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
            const QPointer<QWidget> pageGuard(page);
            createButton->setEnabled(false);
            setPageNotice("Sending your issue to the maintainer's inbox...");
            const bool started = submitNewIssueToInbox(
                title, bodyEdit->markdown(), labels, milestone, priority,
                assignees,
                [this, pageGuard, createButton, setPageNotice](bool ok,
                                                                const QString &error) {
                    // The compose page may have been cancelled/closed while the
                    // POST was in flight — don't touch its (now-deleted) widgets.
                    if (!pageGuard) {
                        if (ok)
                            setIssueInlineNotice(
                                "Your signed issue was sent to the maintainer's "
                                "inbox. It appears once they sync it.");
                        return;
                    }
                    if (ok) {
                        removeIssueComposePage();
                        setIssueInlineNotice("Your signed issue was sent to the "
                                             "maintainer's inbox. It appears once "
                                             "they sync it.");
                    } else {
                        createButton->setEnabled(true);
                        setPageNotice(error.isEmpty()
                                          ? "Could not send the issue."
                                          : "Could not send the issue: " + error,
                                      true);
                    }
                });
            if (!started) {
                createButton->setEnabled(true);
                setPageNotice("Open a repository you can reach to file an issue.",
                              true);
            }
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

    // Cmd/Ctrl+Enter from anywhere in the form submits it, matching the muscle
    // memory from GitHub's new-issue page (issue #307). QKeySequence maps Ctrl to
    // Command on macOS, so this is Cmd+Return there; WidgetWithChildren scope lets
    // it fire whether focus is in the title or the description editor.
    auto *submitShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), page);
    submitShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(submitShortcut, &QShortcut::activated, createButton, &QPushButton::click);

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
    // This is the default (adhoc #99): "Create issue" is off unless the user
    // turns it on, so most quick-add prompts skip issue filing entirely.
    if (!m_quickAddCreateIssue || !m_quickAddCreateIssue->isChecked()) {
        const QString provider =
            m_quickAddAgentProvider
                ? m_quickAddAgentProvider->currentData().toString()
                : QStringLiteral("claude-code");
        // The quick-add model picker only shows/applies for Claude Code (adhoc
        // #99 hides it for the other providers), so only feed it through then —
        // otherwise the session's model stays empty like before.
        const QString model = (provider == QLatin1String("claude-code") &&
                               m_quickAddClaudeModel)
                                  ? m_quickAddClaudeModel->currentData().toString()
                                  : QString();
        const bool createPr = m_quickAddCreatePr && m_quickAddCreatePr->isChecked();
        // Hand any attached images to the agent the same way the new-agent
        // composer does: an "Attached image: <path>" line per file (issue #79).
        QString prompt = title;
        for (const QString &img : m_quickAddImages) {
            if (!prompt.endsWith(QLatin1Char('\n')))
                prompt += QLatin1Char('\n');
            prompt += QStringLiteral("Attached image: %1").arg(img);
        }
        if (startAdHocAgentForRepo(issuesRepoIndex(), prompt, provider, createPr,
                                   model) > 0) {
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
        QPointer<QPlainTextEdit> quickAddGuard(m_issueQuickAdd);
        quickAddGuard->setEnabled(false);
        const bool started = submitNewIssueToInbox(
            title, QString(), {}, QString(), 0, {},
            [this, quickAddGuard, title](bool ok, const QString &) {
                // submitNewIssueToInbox already flashes the failure toast; on
                // success, clear the box now that the maintainer actually has
                // it — clearing it up front (the old behavior) lost the draft
                // if the submission silently failed.
                if (!quickAddGuard)
                    return;
                quickAddGuard->setEnabled(true);
                if (ok) {
                    quickAddGuard->clear();
                    setIssueInlineNotice("Your signed issue was sent to the "
                                         "maintainer's inbox. It appears once "
                                         "they sync it.");
                } else {
                    quickAddGuard->setPlainText(title);
                }
            });
        if (!started)
            quickAddGuard->setEnabled(true);
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
        const QString model = (provider == QLatin1String("claude-code") &&
                               m_quickAddClaudeModel)
                                  ? m_quickAddClaudeModel->currentData().toString()
                                  : QString();
        const bool oldCreatePr =
            m_issueAgentCreatePrCheck && m_issueAgentCreatePrCheck->isChecked();
        if (m_issueAgentCreatePrCheck) {
            const QSignalBlocker block(m_issueAgentCreatePrCheck);
            m_issueAgentCreatePrCheck->setChecked(m_quickAddCreatePr &&
                                                  m_quickAddCreatePr->isChecked());
            assignIssueToAgent(provider, model);
            m_issueAgentCreatePrCheck->setChecked(oldCreatePr);
        } else {
            assignIssueToAgent(provider, model);
        }
    } else {
        // Issue #203: with no agent to hand off to, land the user on the issue
        // they just created -- open its detail pane, mirroring how the agent path
        // jumps straight to the new session. reloadIssues() above re-selects the
        // row in table mode, but call showIssue() explicitly so the detail opens
        // regardless of the active list view (board, cards, a filtered table).
        showIssue(number);
    }
}

#ifdef FORKMESH_WINDOW_TESTS
int MainWindow::testQuickAddIssueNoAgent(const QString &title)
{
    if (!m_issueQuickAdd)
        return -1;
    // Type the title and make sure the agent hand-off is off but issue creation
    // is on, so the plain create-and-open path (issue #203) runs rather than the
    // agent / no-issue one.
    if (m_quickAddAssignAgent)
        m_quickAddAssignAgent->setChecked(false);
    if (m_quickAddCreateIssue)
        m_quickAddCreateIssue->setChecked(true);
    m_issueQuickAdd->setPlainText(title);
    quickAddIssue();
    return m_currentIssueNumber;
}
#endif

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

// Drop a single queued attachment — wired to the "x" on its chip.
void MainWindow::removeQuickAddImage(const QString &path)
{
    if (!m_quickAddImages.removeOne(path))
        return;
    updateQuickAddImageButton();
    setIssueInlineNotice(
        m_quickAddImages.isEmpty()
            ? QStringLiteral("Attachment removed.")
            : QStringLiteral("Attachment removed (%1 left).")
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
    rebuildQuickAddAttachChips();
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

// Rebuild the attachment chips beside the paperclip: one chip per queued image,
// each a small thumbnail with a little "x" to remove just that image. Hidden when
// nothing is attached.
void MainWindow::rebuildQuickAddAttachChips()
{
    if (!m_quickAddAttachStrip)
        return;
    auto *row = qobject_cast<QHBoxLayout *>(m_quickAddAttachStrip->layout());
    if (!row)
        return;
    // Tear down the previous chips.
    while (QLayoutItem *item = row->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    for (const QString &path : std::as_const(m_quickAddImages)) {
        auto *chip = new QWidget(m_quickAddAttachStrip);
        chip->setObjectName("quickAddAttachChip");
        chip->setToolTip(QFileInfo(path).fileName());
        chip->setStyleSheet(
            "QWidget#quickAddAttachChip{background:#21262d;border:1px solid #30363d;"
            "border-radius:4px;}");
        auto *chipRow = new QHBoxLayout(chip);
        chipRow->setContentsMargins(2, 1, 1, 1);
        chipRow->setSpacing(2);

        // Preview thumbnail (issue #348): kept shorter than the surrounding
        // bottom bar so an attached image doesn't make the chip taller than
        // the icons beside it. Clicking it opens the original full-size in a
        // lightbox (issue #319).
        QPixmap pm(path);
        if (!pm.isNull()) {
            auto *thumb = new QToolButton(chip);
            thumb->setIcon(QIcon(pm));
            thumb->setIconSize(QSize(18, 18));
            thumb->setAutoRaise(true);
            thumb->setCursor(Qt::PointingHandCursor);
            thumb->setToolTip(QStringLiteral("Click to view full size"));
            thumb->setStyleSheet(
                "QToolButton{border:none;background:transparent;padding:0;}");
            connect(thumb, &QToolButton::clicked, this,
                    [this, path]() { showQuickAddImageDetail(path); });
            chipRow->addWidget(thumb);
        } else {
            auto *thumb = new QLabel(QFileInfo(path).fileName(), chip);
            chipRow->addWidget(thumb);
        }

        // The little "x": removes only this attachment.
        auto *remove = new QPushButton(QString::fromUtf8("\xC3\x97"), chip);
        remove->setObjectName("quickAddAttachRemove");
        remove->setCursor(Qt::PointingHandCursor);
        remove->setFixedSize(14, 14);
        remove->setToolTip(QStringLiteral("Remove this attachment"));
        remove->setStyleSheet(
            "QPushButton#quickAddAttachRemove{color:#8b949e;border:none;"
            "background:transparent;font-size:13px;font-weight:bold;padding:0;}"
            "QPushButton#quickAddAttachRemove:hover{color:#f85149;}");
        connect(remove, &QPushButton::clicked, this,
                [this, path]() { removeQuickAddImage(path); });
        chipRow->addWidget(remove);

        row->addWidget(chip);
    }
    m_quickAddAttachStrip->setVisible(!m_quickAddImages.isEmpty());
}

// Open a queued quick-add attachment full-size in a lightbox dialog (issue
// #319). Mirrors showChatImageDetail, but reads the original file straight off
// disk rather than from in-memory message bytes.
void MainWindow::showQuickAddImageDetail(const QString &path)
{
    QPixmap pixmap(path);
    if (pixmap.isNull())
        return;

    auto *dialog = new QDialog(this);
    dialog->setObjectName("imageDetailDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QFileInfo(path).fileName());

    // Cap the displayed size to most of the available screen so huge images
    // don't open larger than the monitor; smaller images show at native size.
    QSize maxSize(1200, 800);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QSize avail = screen->availableSize();
        maxSize = QSize(avail.width() * 9 / 10, avail.height() * 9 / 10);
    }
    QPixmap shown = pixmap;
    if (pixmap.width() > maxSize.width() || pixmap.height() > maxSize.height())
        shown = pixmap.scaled(maxSize, Qt::KeepAspectRatio,
                              Qt::SmoothTransformation);

    auto *imageLabel = new QLabel;
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setPixmap(shown);

    auto *scroll = new QScrollArea;
    scroll->setObjectName("messageView");
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setWidget(imageLabel);

    auto *closeButton = new QPushButton(QStringLiteral("Close"));
    closeButton->setObjectName("primaryButton");
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(new QLabel(
        QStringLiteral("%1 \xC3\x97 %2").arg(pixmap.width()).arg(pixmap.height())));
    buttonRow->addStretch();
    buttonRow->addWidget(closeButton);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->addWidget(scroll, 1);
    layout->addLayout(buttonRow);

    dialog->resize(qMin(shown.width() + 48, maxSize.width()),
                   qMin(shown.height() + 96, maxSize.height()));
    dialog->show();
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
    // A Claude model combo's popup list view was shown: re-fetch the live model
    // list so the dropdown always reflects the provider's current line-up.
    if (event->type() == QEvent::Show) {
        if (auto *w = qobject_cast<QWidget *>(obj)) {
            if (auto *combo = qobject_cast<QComboBox *>(w->parent())) {
                if (combo->property("claudeModelCombo").toBool())
                    refreshClaudeModelCombo();
            }
        }
    }
    // First expose of the top-level window: its first frame is now on screen, so
    // it's safe to run the deferred git-backed startup without a black frame.
    if (event->type() == QEvent::Expose && obj == windowHandle()) {
        if (QWindow *handle = windowHandle(); handle && handle->isExposed())
            QTimer::singleShot(0, this, &MainWindow::runDeferredStartup);
        return QMainWindow::eventFilter(obj, event); // never consume expose
    }
    // Ctrl + mouse wheel over any registered diff viewer zooms its text size,
    // mirroring the +/- buttons (issue #254). Consume so the view doesn't scroll.
    if (event->type() == QEvent::Wheel &&
        (static_cast<QWheelEvent *>(event)->modifiers() & Qt::ControlModifier)) {
        for (QTextEdit *view : m_diffViews) {
            if (view && obj == view->viewport()) {
                adjustDiffFont(
                    static_cast<QWheelEvent *>(event)->angleDelta().y() > 0 ? 1 : -1);
                return true;
            }
        }
    }
    // Right-click on selected text anywhere in the app: offer "Send to
    // Prompt" alongside the widget's normal Copy/Select-All menu (adhoc #126).
    if (event->type() == QEvent::ContextMenu) {
        if (maybeShowSendToPromptMenu(obj, static_cast<QContextMenuEvent *>(event)))
            return true;
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
    // Click a row (or effort dot) in the footer slash-actions popup (adhoc
    // #116): every activatable widget in that popup carries a "slashKind"
    // dynamic property, dispatched generically in activateSlashActionRow.
    if (event->type() == QEvent::MouseButtonRelease) {
        if (auto *w = qobject_cast<QWidget *>(obj)) {
            if (w->property("slashKind").isValid()) {
                activateSlashActionRow(w);
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
        // With an agent session already open above, Enter follows up on that
        // agent instead of starting a brand-new one — the same routing the
        // up-arrow "send to agent" button next to it already does, just bound
        // to the more natural key.
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            !(ke->modifiers() & Qt::ShiftModifier)) {
            if (m_selectedAgentSessionId >= 0 && m_quickAddSendToAgentButton)
                m_quickAddSendToAgentButton->click();
            else
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
    // Slash-actions popup filter box (adhoc #116): Up/Down walk the visible
    // rows, Enter activates the selected one, Escape closes the popup —
    // mirrors the global-search dropdown's keyboard handling below.
    if (obj == m_slashActionsFilter && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Down:
            moveSlashActionsSelection(1);
            return true;
        case Qt::Key_Up:
            moveSlashActionsSelection(-1);
            return true;
        case Qt::Key_Escape:
            if (m_slashActionsPopup)
                m_slashActionsPopup->hide();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_slashActionSelected >= 0 &&
                m_slashActionSelected < m_slashActionRows.size())
                activateSlashActionRow(m_slashActionRows.at(m_slashActionSelected));
            return true;
        default:
            break;
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

// Right-click on selected text anywhere in the app (a transcript reply, a
// diff line, a README, a log) offers "Send to Prompt" so the user can grab it
// straight into the agent prompt box instead of a manual copy/paste
// round-trip (adhoc #126). Handles the two families of selectable text used
// across the UI:
//   - QLabel with Qt::TextSelectableByMouse (transcript bubbles, message rows)
//   - QTextEdit / QTextBrowser / QPlainTextEdit (diffs, README, logs, editors)
// QAbstractScrollArea-based widgets deliver ContextMenu events to their
// viewport, not the widget itself, so we walk up to find the real owner.
// Returns true only when it took over and showed a menu; false lets the
// widget's default handling run (e.g. nothing selected).
bool MainWindow::maybeShowSendToPromptMenu(QObject *obj, QContextMenuEvent *ce)
{
    QWidget *w = qobject_cast<QWidget *>(obj);
    if (!w || !ce)
        return false;

    QString selected;
    QMenu *menu = nullptr;

    if (auto *label = qobject_cast<QLabel *>(w)) {
        if (!(label->textInteractionFlags() & Qt::TextSelectableByMouse))
            return false;
        selected = label->selectedText();
        if (selected.isEmpty())
            return false;
        menu = new QMenu(this);
        QAction *copy = menu->addAction(tr("Copy"));
        connect(copy, &QAction::triggered, label,
                [label] { QApplication::clipboard()->setText(label->selectedText()); });
    } else {
        QWidget *host = w;
        while (host && !qobject_cast<QTextEdit *>(host) && !qobject_cast<QPlainTextEdit *>(host))
            host = host->parentWidget();
        // Don't offer to send the prompt box's own text back into itself.
        if (!host || host == m_issueQuickAdd)
            return false;
        if (auto *te = qobject_cast<QTextEdit *>(host)) {
            selected = te->textCursor().selectedText();
            if (selected.isEmpty())
                return false;
            menu = te->createStandardContextMenu(ce->pos());
        } else if (auto *pte = qobject_cast<QPlainTextEdit *>(host)) {
            selected = pte->textCursor().selectedText();
            if (selected.isEmpty())
                return false;
            menu = pte->createStandardContextMenu(ce->pos());
        } else {
            return false;
        }
    }
    if (!menu)
        return false;

    // QTextCursor::selectedText() encodes paragraph breaks as U+2029; put
    // real newlines back so a multi-line selection reads naturally once
    // pasted into the prompt box.
    const QString promptText = QString(selected).replace(QChar(0x2029), QLatin1Char('\n'));
    menu->addSeparator();
    QAction *sendToPrompt = menu->addAction(tr("Send to Prompt"));
    QAction *chosen = menu->exec(ce->globalPos());
    if (chosen == sendToPrompt)
        appendTextToActivePrompt(promptText);
    delete menu;
    return true;
}

// Appends text to the footer's always-present global quick-add box — the only
// prompt target left since the per-agent composer's input field was removed
// (adhoc #139; the quick-add bar's "send to agent" button covers that case).
void MainWindow::appendTextToActivePrompt(const QString &text)
{
    QPlainTextEdit *target = m_issueQuickAdd;
    if (!target)
        return;
    QTextCursor cursor = target->textCursor();
    cursor.movePosition(QTextCursor::End);
    const QString existing = target->toPlainText();
    if (!existing.isEmpty() && !existing.endsWith(QLatin1Char('\n')))
        cursor.insertText(QStringLiteral("\n"));
    cursor.insertText(text);
    target->setTextCursor(cursor);
    target->setFocus();
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
                                    double amountUsd, const QString &amountSol,
                                    const QString &kind)
{
    // "pr" bounties (issue #347) aren't tracked in the issue store, so the paid
    // state is reported in the dialog only; issue bounties (kind "") also stamp
    // the issue record.
    const bool isPr = kind == QLatin1String("pr");
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
        QJsonObject payload{{"action", "status"},
                            {"owner", repo.owner},
                            {"repo", repo.name},
                            {"number", number}};
        if (isPr)
            payload.insert("kind", QStringLiteral("pr"));
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
            if (!isPr) {
                IssueStore writeStore = issueStoreForCurrentRepo();
                QString err;
                writeStore.setBounty(number, amountUsd,
                                     obj.value("payee").toString(),
                                     QStringLiteral("paid"), &err);
                if (m_repoDetailIndex == issuesRepoIndex())
                    reloadIssues();
            }
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
        pollBountyPayout(repo, number, amountUsd, kind);
}

void MainWindow::showBountyWalletDialog()
{
    // Issue #347: fetch (mint on first use) the owner's inbuilt bounty wallet and
    // show its deposit address + QR + live balance so it can be pre-funded. Used
    // to pay per-PR bounties in "wallet" mode without a per-merge QR.
    const QString owner = accountOwner();
    if (owner.isEmpty() || !m_profileIdentity.isValid()) {
        QMessageBox::information(
            this, QStringLiteral("Bounty wallet"),
            QStringLiteral("Register and sign in to a ForkMesh account first — the "
                           "inbuilt wallet is tied to your account."));
        return;
    }
    // The wallet is owner-scoped but the endpoint is repo-scoped; route through any
    // repository this account owns.
    RepositoryRecord ownedRepo;
    bool haveOwned = false;
    for (const RepositoryRecord &r : std::as_const(m_repositories))
        if (r.owner == owner) {
            ownedRepo = r;
            haveOwned = true;
            break;
        }
    if (!haveOwned || !m_networkAccess) {
        QMessageBox::information(
            this, QStringLiteral("Bounty wallet"),
            QStringLiteral("Create or import a repository you own first — the "
                           "inbuilt wallet is set up through one of your repos."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Inbuilt bounty wallet"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(QStringLiteral(
        "Pre-fund this wallet with a little SOL. When \"Reward every merged pull "
        "request\" is set to <b>use the inbuilt wallet</b>, each merged PR is paid "
        "from here automatically — no per-merge QR."));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    auto *qrLabel = new QLabel;
    qrLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(qrLabel);
    auto *addr = new QLabel(QStringLiteral("Loading…"));
    addr->setObjectName("statusLine");
    addr->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addr->setAlignment(Qt::AlignCenter);
    addr->setWordWrap(true);
    layout->addWidget(addr);
    auto *balance = new QLabel(QStringLiteral("Balance: …"));
    balance->setObjectName("modeHint");
    balance->setAlignment(Qt::AlignCenter);
    layout->addWidget(balance);

    auto *copyBtn = new QPushButton(QStringLiteral("Copy address"));
    copyBtn->setEnabled(false);
    auto *refreshBtn = new QPushButton(QStringLiteral("Refresh"));
    auto *closeBtn = new QPushButton(QStringLiteral("Close"));
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto *row = new QHBoxLayout;
    row->addWidget(copyBtn);
    row->addWidget(refreshBtn);
    row->addStretch();
    row->addWidget(closeBtn);
    layout->addLayout(row);

    auto walletAddress = std::make_shared<QString>();
    const auto fetch = [this, owner, ownedRepo, qrLabel, addr, balance, copyBtn,
                        walletAddress] {
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        const QByteArray canonical =
            ("forkmesh-bounty-wallet-v1\n" + owner + "\n" + ts).toUtf8();
        const QJsonObject payload{{"action", "wallet"},
                                  {"owner", ownedRepo.owner},
                                  {"repo", ownedRepo.name},
                                  {"ts", ts},
                                  {"sig", m_profileIdentity.signData(canonical)}};
        QNetworkRequest request(bountyApiUrl(ownedRepo));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *reply = m_networkAccess->post(
            request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, qrLabel,
                [reply, qrLabel, addr, balance, copyBtn, walletAddress] {
            const QByteArray body = reply->readAll();
            const auto err = reply->error();
            const QString errStr = reply->errorString();
            reply->deleteLater();
            const QJsonObject obj = QJsonDocument::fromJson(body).object();
            const QString address = obj.value("address").toString();
            if (err != QNetworkReply::NoError || address.isEmpty()) {
                addr->setText(QStringLiteral("Could not load wallet: %1")
                                  .arg(obj.value("error").toString(errStr)));
                return;
            }
            *walletAddress = address;
            addr->setText(address);
            copyBtn->setEnabled(true);
            balance->setText(QStringLiteral("Balance: %1 SOL")
                                 .arg(obj.value("balanceSol").toString(
                                     QStringLiteral("0"))));
            const QString uri = obj.value("uri").toString(
                QStringLiteral("solana:%1").arg(address));
            const QImage qr = QrCode::encodeToImage(uri, 5, 3);
            if (!qr.isNull())
                qrLabel->setPixmap(QPixmap::fromImage(qr));
        });
    };
    connect(copyBtn, &QPushButton::clicked, &dialog, [walletAddress] {
        if (!walletAddress->isEmpty())
            QGuiApplication::clipboard()->setText(*walletAddress);
    });
    connect(refreshBtn, &QPushButton::clicked, &dialog, fetch);
    fetch();
    dialog.exec();
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

bool MainWindow::submitNewIssueToInbox(
    const QString &title, const QString &body, const QStringList &labels,
    const QString &milestone, int priority, const QStringList &assignees,
    std::function<void(bool ok, const QString &error)> onDone)
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, onDone] {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError;
        // Don't claim success until the worker actually accepted the submission
        // into its inbox — an optimistic "sent" message here left a rejected or
        // dropped submission looking like it worked, with the real failure only
        // ever shown as an easy-to-miss toast after the compose form had already
        // closed (the issue never reaching the source of truth, silently).
        if (!ok) {
            setIssueInlineNotice("Could not send the issue: " + reply->errorString(),
                                 true);
        }
        if (onDone)
            onDone(ok, ok ? QString() : reply->errorString());
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

    QUrl url = issuesApiUrl(repo);
    // Auto-polls back off exponentially while the relay is failing (offline /
    // HTTP 429); a manual "Sync inbox" (interactive) always tries immediately.
    const QString backoffKey = url.toString();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!interactive && !m_pollBackoff.ready(backoffKey, nowMs))
        return;

    const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString ts = QString::number(nowMs);
    const QByteArray canonical =
        ("forkmesh-issues-pull-v1\n" + owner + "\n" + ts).toUtf8();
    const QString sig = m_profileIdentity.signData(canonical);

    QUrlQuery query;
    query.addQueryItem("owner", owner);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", sig);
    url.setQuery(query);

    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, url, repo, writable, interactive, backoffKey] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_pollBackoff.noteFailure(backoffKey,
                                      QDateTime::currentMSecsSinceEpoch());
            if (interactive)
                setIssueInlineNotice("Could not reach the inbox: " +
                                         reply->errorString(),
                                     true);
            return;
        }
        m_pollBackoff.noteSuccess(backoffKey);
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
        // New issues the submitter (the repo owner, filing from the website)
        // asked to have auto-assigned to an agent once merged. Matched against
        // the merged store below by open-event identity, since a freshly merged
        // web submission's real issue number isn't known until after the merge
        // (proposed number 0 gets reassigned inside applyRemoteEvent).
        struct AgentRequest {
            IssueEvent event;
            QString model;
            QString provider; // empty = node default (adhoc #234)
        };
        QList<AgentRequest> agentRequests;
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
            meta.wantsAgent = metaObj.value("wantsAgent").toBool();
            meta.wantsAgentModel = metaObj.value("model").toString();
            meta.wantsAgentProvider = metaObj.value("provider").toString();
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
                    if (meta.wantsAgent)
                        agentRequests << AgentRequest{ev, meta.wantsAgentModel,
                                                      meta.wantsAgentProvider};
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
        // Start an agent on each new issue the owner flagged for auto-assignment
        // when they filed it. Look the issue back up by its open event's identity
        // (see agentRequests above) to get the real, post-merge issue number.
        // repoHint keeps this pointed at repo/store above regardless of what the
        // Issues tab currently shows (adhoc #105).
        if (!agentRequests.isEmpty()) {
            const QList<Issue> mergedIssues = store.loadAll();
            for (const auto &request : std::as_const(agentRequests)) {
                const IssueEvent &wanted = request.event;
                const QString &wantedModel = request.model;
                // The web submitter's provider choice (adhoc #234), falling back
                // to this node's default when they left it unset.
                const QString wantedProvider = request.provider.trimmed().isEmpty()
                                                   ? defaultAgentProvider()
                                                   : request.provider.trimmed();
                for (const Issue &candidate : mergedIssues) {
                    if (candidate.isDeleted())
                        continue;
                    bool matched = false;
                    for (const IssueEvent &e : candidate.events) {
                        if (e.type != QLatin1String("open"))
                            continue;
                        const bool sameSig =
                            !wanted.sig.isEmpty() && e.sig == wanted.sig;
                        const bool sameAuthorTs =
                            wanted.sig.isEmpty() && e.author == wanted.author &&
                            e.ts == wanted.ts && e.title == wanted.title;
                        if (sameSig || sameAuthorTs) {
                            matched = true;
                            break;
                        }
                    }
                    if (matched) {
                        // A redelivered inbox item (e.g. the previous drain's ack
                        // delete failed after a successful merge) would otherwise
                        // start a second agent on the same issue — skip if one is
                        // already recorded on it.
                        bool alreadyAssigned = false;
                        for (const IssueEvent &e : candidate.events) {
                            if (e.type == QLatin1String("agent") && e.agentSessionId > 0) {
                                alreadyAssigned = true;
                                break;
                            }
                        }
                        if (!alreadyAssigned)
                            startAgentForIssue(candidate, wantedProvider,
                                               /*createPr=*/true, /*quiet=*/true,
                                               wantedModel, &repo);
                        break;
                    }
                }
            }
        }
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
    // Invite people into the current private room. Hidden for public channels
    // and DMs (there's no one to "invite" to those); toggled in switchConversation.
    m_inviteButton = new QPushButton(QStringLiteral("Invite"));
    m_inviteButton->setObjectName("ghostButton");
    m_inviteButton->setCursor(Qt::PointingHandCursor);
    m_inviteButton->setToolTip(QStringLiteral("Invite a member to this private room"));
    m_inviteButton->hide();
    connect(m_inviteButton, &QPushButton::clicked, this,
            &MainWindow::promptInviteToPrivateChannel);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 12, 18, 12);
    headerLayout->addWidget(m_channelTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(m_inviteButton);
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
    // "+ Add chat" offers a public channel (visible to the whole network) or an
    // invite-only private room. A plain channel is the common case, so it's first.
    connect(addChannelButton, &QPushButton::clicked, this, [this, addChannelButton] {
        QMenu menu(this);
        connect(menu.addAction(QStringLiteral("New channel (public)")),
                &QAction::triggered, this, &MainWindow::promptAddChannel);
        connect(menu.addAction(QStringLiteral("New private room\xE2\x80\xA6")),
                &QAction::triggered, this, &MainWindow::promptAddPrivateChannel);
        menu.exec(addChannelButton->mapToGlobal(
            QPoint(0, addChannelButton->height())));
    });
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

