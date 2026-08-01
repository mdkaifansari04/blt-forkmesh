








#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "FederatedThreadView.h"
#include "KebabHeaderView.h"
#include "MirrorCrypto.h"

#include <QDateEdit>
#include <QLayoutItem>
#include <QPair>
#include <QPixmap>
#include <QStackedLayout>

using namespace forkmesh::ui;



QWidget *MainWindow::buildIssuesSection()
{
    auto *page = new QWidget;


    auto *listPane = new QWidget;
    listPane->setMinimumWidth(260);

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



    m_issueListNewButton = new QPushButton("New issue");
    m_issueListNewButton->setObjectName("primaryButton");
    m_issueListNewButton->setProperty("buttonSize", "sm");
    m_issueListNewButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueListNewButton, "plus", 16);

    auto *headingRow = new QHBoxLayout;
    headingRow->setContentsMargins(0, 0, 0, 0);
    headingRow->addWidget(heading);
    headingRow->addWidget(m_issueListNewButton);



    if (m_looperToggle)
        headingRow->addWidget(m_looperToggle);
    headingRow->addStretch();
    headingRow->addWidget(issuesTab);
    headingRow->addWidget(boardTab);
    headingRow->addWidget(milestonesTab);
    headingRow->addWidget(labelsTab);

    m_issuesRepoCombo = new QComboBox;
    m_issuesRepoCombo->setToolTip("Repository whose issues you are viewing");


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



    m_issuePrioritizeAgentCombo = new QComboBox;
    m_issuePrioritizeAgentCombo->setObjectName("issueControlSm");
    m_issuePrioritizeAgentCombo->addItem(QStringLiteral("Codex"), kCodexProvider);
    m_issuePrioritizeAgentCombo->addItem(QStringLiteral("OpenAI API"),
                                         QStringLiteral("openai"));
    m_issuePrioritizeAgentCombo->addItem(QStringLiteral("Claude API"),
                                         QStringLiteral("claude-api"));
    m_issuePrioritizeAgentCombo->addItem(QStringLiteral("CC"),
                                         QStringLiteral("claude-code"));
    selectDefaultAgentProvider(m_issuePrioritizeAgentCombo);
    m_issuePrioritizeAgentCombo->setToolTip(
        "Agent that ranks/reviews the issues. Defaults to your default agent "
        "(Settings \xE2\x86\x92 Agents).");




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



    headingRow->insertWidget(1, m_issuePrioritizeButton);
    headingRow->insertWidget(2, m_issuePrioritizeAgentCombo);
    headingRow->insertWidget(2, m_issueCompletenessButton);




    auto *bountyAllAmount = new QLineEdit;
    bountyAllAmount->setObjectName("issueControlSm");
    bountyAllAmount->setPlaceholderText("retired");
    bountyAllAmount->setMaximumWidth(70);
    bountyAllAmount->setEnabled(false);
    bountyAllAmount->setToolTip(
        "New bounty escrow is disabled; historical entries are migration-only.");
    auto *bountyAllButton = new QPushButton("Bounties retired");
    bountyAllButton->setObjectName("ghostButton");
    bountyAllButton->setProperty("buttonSize", "sm");
    bountyAllButton->setEnabled(false);
    bountyAllButton->setToolTip(
        "ForkMesh no longer creates or funds Worker-held bounty escrow.");
    setOcticon(bountyAllButton, "tag", 16);

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
    installColumnHeaderMenu(m_issueTable);
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

    m_issueTable->sortByColumn(2, Qt::AscendingOrder);
    m_issueTable->setToolTip("Click a column header to sort. Double-click an "
                             "editable cell (title, priority, status, labels, "
                             "milestone, progress, bounty) to edit it.");
    QHeaderView *header = m_issueTable->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);



    header->setSectionResizeMode(1, QHeaderView::Interactive);
    m_issueTable->setColumnWidth(1, 360);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(8, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(9, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(10, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(11, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(12, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(13, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(14, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(15, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(16, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_issueTable);

    m_issueTable->setItemDelegateForColumn(11, new ProgressBarDelegate(m_issueTable));

    m_issueTable->viewport()->installEventFilter(this);

    m_issueMilestonesTable = new QTableWidget(0, 6);
    m_issueMilestonesTable->setObjectName("issueTable");
    installColumnHeaderMenu(m_issueMilestonesTable);
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
    installColumnHeaderMenu(m_issueLabelsTable);
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
    m_issueListStack->addWidget(m_issueTable);
    m_issueListStack->addWidget(m_issueMilestonesTable);
    m_issueListStack->addWidget(m_issueLabelsTable);
    m_issueListStack->addWidget(buildIssueBoard());





    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(18, 18, 12, 18);
    listLayout->setSpacing(8);




    auto makeOverflowToolbar = [this](QHBoxLayout *row,
                                      const QString &objectName) {
        auto *host = new QWidget;
        host->setLayout(row);
        host->adjustSize();
        auto *scroll = new QScrollArea;
        scroll->setObjectName(objectName);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(host);
        scroll->setWidgetResizable(false);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setMinimumWidth(0);
        scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        scroll->setFixedHeight(
            host->sizeHint().height() +
            style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 2);
        return scroll;
    };
    listLayout->addWidget(
        makeOverflowToolbar(headingRow, QStringLiteral("issueHeadingToolbar")));
    listLayout->addWidget(m_issuesRepoCombo);
    listLayout->addLayout(filterRow);
    listLayout->addWidget(
        makeOverflowToolbar(actionRow, QStringLiteral("issueBulkToolbar")));
    listLayout->addWidget(m_issueListStack, 1);



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
    auto *closeDetailButton = new QPushButton;
    closeDetailButton->setObjectName("issueIconButton");
    closeDetailButton->setFixedSize(30, 30);
    closeDetailButton->setCursor(Qt::PointingHandCursor);
    closeDetailButton->setToolTip("Close issue detail");
    setOcticon(closeDetailButton, "x", 16);
    issueTitleRow->addWidget(closeDetailButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueNewButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueCopyButton, 0, Qt::AlignTop);
    issueTitleRow->addWidget(m_issueCopyAllButton, 0, Qt::AlignTop);
    m_issueMeta = new QLabel;
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
    auto *commentTitle = new QLabel(
        QStringLiteral("Commenting as <b>%1</b>")
            .arg(topBarUserName().toHtmlEscaped()));
    commentTitle->setObjectName("issueCommentTitle");
    commentTitle->setTextFormat(Qt::RichText);
    m_issueComposerTitle = commentTitle;
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

    commentButtonRow->addWidget(makeVoiceButton(m_issueComposer), 0, Qt::AlignLeft);
    commentButtonRow->addStretch();
    commentButtonRow->addWidget(m_issueAskAiButton);
    commentButtonRow->addWidget(m_issueVoteButton);
    commentButtonRow->addWidget(m_issueCloseButton);
    commentButtonRow->addWidget(m_issueCloseCommentButton);
    commentButtonRow->addWidget(m_issueCommentButton);





    auto *commentActions = new QWidget;
    commentActions->setLayout(commentButtonRow);
    commentActions->adjustSize();
    auto *commentActionsScroll = new QScrollArea;
    commentActionsScroll->setObjectName("issueCommentActionsScroll");
    commentActionsScroll->setFrameShape(QFrame::NoFrame);
    commentActionsScroll->setWidget(commentActions);
    commentActionsScroll->setWidgetResizable(false);
    commentActionsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    commentActionsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    commentActionsScroll->setMinimumWidth(0);
    commentActionsScroll->setSizePolicy(QSizePolicy::Ignored,
                                        QSizePolicy::Fixed);
    commentActionsScroll->setFixedHeight(
        commentActions->sizeHint().height() +
        style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 2);
    auto *commentColumn = new QVBoxLayout;
    commentColumn->setContentsMargins(0, 0, 0, 0);
    commentColumn->setSpacing(8);
    commentColumn->addWidget(commentTitle);
    commentColumn->addWidget(m_issueComposer);
    commentColumn->addWidget(commentActionsScroll);
    auto *composerRow = new QHBoxLayout;
    composerRow->setContentsMargins(0, 0, 0, 0);
    composerRow->setSpacing(14);
    composerRow->addWidget(commentAvatar, 0, Qt::AlignTop);
    composerRow->addLayout(commentColumn, 1);

    auto *center = new QWidget;




    center->setMinimumWidth(0);
    center->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
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


    auto *meta = new QWidget;
    meta->setObjectName("issueSidebar");
    meta->setMinimumWidth(190);
    meta->setMaximumWidth(300);
    m_issueAssigneesValue = new QLabel("No one - <a href='#'>Assign yourself</a>");
    m_issueLabelsValue = new QLabel("No labels");
    m_issueMilestoneValue = new QLabel("No milestone");
    m_issueDatesValue = new QLabel("No dates");
    m_issuePriorityValue = new QLabel("No priority");
    m_issueEstimateValue = new QLabel("\xE2\x80\x94");
    m_issueBountyValue = new QLabel("No bounty");


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
                      m_issueMilestoneValue, m_issueDatesValue,
                      m_issuePriorityValue,
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
    m_issueDatesButton = new QPushButton;
    m_issuePriorityButton = new QPushButton;
    m_issueProgressButton = new QPushButton;
    m_issueBountyButton = new QPushButton;
    m_issueAssigneesButton = new QPushButton;
    m_issueDeleteButton = new QPushButton("Delete issue");
    for (QPushButton *b : {m_issueLabelsButton, m_issueMilestoneButton,
                           m_issueDatesButton,
                           m_issuePriorityButton, m_issueProgressButton,
                           m_issueBountyButton,
                           m_issueAssigneesButton}) {
        b->setObjectName("issueIconButton");
        b->setFixedSize(28, 28);
        b->setCursor(Qt::PointingHandCursor);
        setOcticon(b, "gear", 15);
    }
    m_issueBountyButton->setObjectName(
        QStringLiteral("legacyIssueBountyDisabled"));
    m_issueBountyButton->setEnabled(false);
    m_issueBountyButton->setToolTip(
        QStringLiteral("New Worker-held bounty escrow is disabled; existing "
                       "records are retained for migration only."));
    m_issueDeleteButton->setObjectName("issueDangerLink");
    m_issueDeleteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_issueDeleteButton, "trash", 15);


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



    m_issueDatesStack = new QStackedWidget(meta);
    m_issueDatesStack->addWidget(m_issueDatesValue);
    auto *datesEditBox = new QWidget(meta);
    auto *datesEditLayout = new QVBoxLayout(datesEditBox);
    datesEditLayout->setContentsMargins(0, 0, 0, 0);
    datesEditLayout->setSpacing(6);
    auto makeIssueDateRow = [&](const QString &label, QCheckBox *&enableOut,
                                QDateEdit *&editOut) {
        auto *rowWidget = new QWidget(datesEditBox);
        auto *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        enableOut = new QCheckBox(label, rowWidget);
        enableOut->setToolTip("Untick to leave this date unset");
        editOut = new QDateEdit(QDate::currentDate(), rowWidget);
        editOut->setCalendarPopup(true);
        editOut->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        editOut->setEnabled(false);
        connect(enableOut, &QCheckBox::toggled, editOut, &QWidget::setEnabled);
        row->addWidget(enableOut);
        row->addWidget(editOut, 1);
        return rowWidget;
    };
    datesEditLayout->addWidget(
        makeIssueDateRow("Start", m_issueStartDateEnable, m_issueStartDateEdit));
    datesEditLayout->addWidget(
        makeIssueDateRow("End", m_issueEndDateEnable, m_issueEndDateEdit));
    auto *datesSave = makeEditorButton("Save", "primaryButton");
    auto *datesCancel = makeEditorButton("Cancel", "ghostButton");
    datesEditLayout->addWidget(makeInlineButtonRow(datesSave, datesCancel));
    m_issueDatesStack->addWidget(datesEditBox);
    connect(datesSave, &QPushButton::clicked, this,
            &MainWindow::saveIssueDatesInline);
    connect(datesCancel, &QPushButton::clicked, this,
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
    m_issueAgentProvider = new QComboBox(meta);
    m_issueAgentProvider->addItem(QStringLiteral("Codex"), kCodexProvider);
    m_issueAgentProvider->addItem(QStringLiteral("OpenAI API"),
                                  QStringLiteral("openai"));
    m_issueAgentProvider->addItem(QStringLiteral("Claude API"),
                                  QStringLiteral("claude-api"));


    m_issueAgentProvider->addItem(QStringLiteral("Claude Code"),
                                  QStringLiteral("claude-code"));
    selectDefaultAgentProvider(m_issueAgentProvider);
    m_issueAgentProvider->setToolTip("Which agent to run on this issue");



    m_issueAgentModel = new QComboBox(meta);
    m_issueAgentModel->setToolTip("Which model the agent uses");
    m_issueAgentModel->setProperty("claudeModelCombo", true);
    m_issueAgentModel->view()->installEventFilter(this);
    fillAgentFixModelCombo(m_issueAgentModel,
                           m_issueAgentProvider->currentData().toString());
    connect(m_issueAgentProvider,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
                if (m_issueAgentProvider && m_issueAgentModel) {
                    fillAgentFixModelCombo(
                        m_issueAgentModel,
                        m_issueAgentProvider->currentData().toString());
                    applyLiveClaudeModelsToCombos();
                }
            });
    applyLiveClaudeModelsToCombos();
    m_issueAssignAgentButton = makeEditorButton("Assign agent", "ghostButton");
    m_issueAgentCreatePrCheck = new QCheckBox("Create a PR", meta);
    m_issueAgentCreatePrCheck->setToolTip(
        "If the agent produces a patch, create a ForkMesh pull request from it.");
    m_issueAgentViewButton = makeEditorButton("View session", "primaryButton");
    m_issueAgentViewButton->hide();
    setOcticon(m_issueAssignAgentButton, "rocket", 15);
    setOcticon(m_issueAgentViewButton, "chevron-right", 15);



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
    addMetaSection("Dates", m_issueDatesStack, m_issueDatesButton);
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
    addMetaSection("Pings", makeValue("You are receiving pings because you're subscribed to this thread."));
    addMetaSection("Participants", makeValue("No participants"));
    auto *transferIssue = makeAction("Transfer issue", "arrow-left");
    auto *cloneIssue = makeAction("Clone issue", "copy");
    auto *lockIssue = makeAction("Lock conversation", "lock");
    auto *pinIssue = makeAction("Pin issue", "tag");


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
    metaScroll->setMinimumWidth(190);
    metaScroll->setMaximumWidth(315);

    metaScroll->setMinimumHeight(0);
    metaScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    metaScroll->setWidget(meta);



    auto *issueDetailView = new QWidget;
    auto *detailSplit = new QSplitter(Qt::Horizontal);
    detailSplit->setChildrenCollapsible(true);
    detailSplit->addWidget(center);
    detailSplit->addWidget(metaScroll);
    detailSplit->setCollapsible(0, false);
    detailSplit->setCollapsible(1, true);
    detailSplit->setStretchFactor(0, 1);
    detailSplit->setStretchFactor(1, 0);
    detailSplit->setSizes({520, 220});




    m_issueFilesList = new QListWidget;
    m_issueFilesList->setObjectName("agentFilesList");
    m_issueFilesList->setMinimumWidth(140);


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


    m_issueDiffView->setMinimumWidth(0);
    m_issueDiffView->setSizePolicy(QSizePolicy::Ignored,
                                   QSizePolicy::Expanding);
    registerDiffView(m_issueDiffView);


    auto *issueFilesSplit = new QSplitter(Qt::Horizontal);
    issueFilesSplit->setChildrenCollapsible(true);
    issueFilesSplit->addWidget(issueFilesPanel);
    issueFilesSplit->addWidget(m_issueDiffView);
    issueFilesSplit->setCollapsible(0, true);
    issueFilesSplit->setCollapsible(1, false);
    issueFilesSplit->setStretchFactor(0, 0);
    issueFilesSplit->setStretchFactor(1, 1);
    issueFilesSplit->setSizes({220, 700});
    auto *issueFilesPage = new QWidget;
    auto *issueFilesPageLayout = new QVBoxLayout(issueFilesPage);
    issueFilesPageLayout->setContentsMargins(0, 8, 0, 0);
    issueFilesPageLayout->addWidget(issueFilesSplit, 1);

    m_issueDetailTabs = new QTabWidget;
    m_issueDetailTabs->setObjectName("agentDetailTabs");
    m_issueDetailTabs->addTab(detailSplit, QStringLiteral("Issue"));
    m_issueFilesTabIndex =
        m_issueDetailTabs->addTab(issueFilesPage, QStringLiteral("Changes in Git"));
    m_issueDetailTabs->setTabVisible(m_issueFilesTabIndex, false);
    connect(m_issueDetailTabs, &QTabWidget::currentChanged, this,
            [this](int index) {
                if (index != m_issueFilesTabIndex || m_currentIssueNumber <= 0)
                    return;
                // Issues keep their discussion and metadata here; their linked
                // repository changes open in the one Git range pane. Put the tab
                // selection back before navigating so returning to the issue
                // never exposes the legacy duplicate diff widget.
                {
                    QSignalBlocker block(m_issueDetailTabs);
                    m_issueDetailTabs->setCurrentIndex(0);
                }
                if (const AgentSession *session =
                        latestAgentSessionForIssue(m_currentIssueNumber)) {
                    if (!session->branchName.isEmpty()) {
                        switchToAgentBranch(session->id);
                        return;
                    }
                }
                const QList<int> pulls = pullsLinkedToIssue(m_currentIssueNumber);
                if (!pulls.isEmpty())
                    openPullDiffInGitView(pulls.constLast());
            });

    auto *detailLayout = new QVBoxLayout(issueDetailView);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(m_issueDetailTabs);

    m_issueDetailStack = new QStackedWidget;
    m_issueDetailStack->setObjectName(QStringLiteral("issueDetailOverlay"));
    m_issueDetailStack->addWidget(issueDetailView);
    m_issueDetail = m_issueDetailStack;






    m_issueDetail->setMinimumWidth(0);
    m_issueDetail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);




    m_issueDetail->hide();
    auto *layout = new QStackedLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setStackingMode(QStackedLayout::StackAll);
    layout->addWidget(listPane);
    layout->addWidget(m_issueDetail);

    connect(m_issueDetailToggle, &QPushButton::clicked, this, [this] {
        const bool show = !m_issueDetail->isVisible();
        m_issueDetail->setVisible(show);
        if (show)
            m_issueDetail->raise();
        m_issueDetailToggle->setText(show ? "Hide detail" : "Show detail");
    });
    connect(closeDetailButton, &QPushButton::clicked, this, [this] {
        if (m_issueDetail)
            m_issueDetail->hide();
        if (m_issueDetailToggle)
            m_issueDetailToggle->setText("Show detail");
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


    connect(m_issueMilestonesTable, &QTableWidget::cellClicked, this,
            [this](int row, int col) {
        if (col != 1 && col != 2)
            return;
        QTableWidgetItem *titleItem = m_issueMilestonesTable->item(row, 0);
        if (!titleItem)
            return;
        const QString milestone = titleItem->text();

        if (m_issueTabGroup && m_issueTabGroup->button(0))
            m_issueTabGroup->button(0)->setChecked(true);
        selectIssueListTab(0);

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



    connect(m_issueTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int col) {
        QTableWidgetItem *first = m_issueTable->item(row, 0);
        if (!first)
            return;
        showIssue(first->data(Qt::UserRole).toInt());
        if (m_currentIssueNumber < 0)
            return;
        switch (col) {
        case 1:  promptEditIssueTitle(); break;
        case 2:  editIssuePriority();    break;
        case 3:  toggleIssueStatus();    break;
        case 5:  editIssueLabels();      break;
        case 6:  editIssueMilestone();   break;
        case 11: editIssueProgress();    break;
        case 13: editIssueBounty();      break;
        default: break;
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
    connect(m_issueDatesButton, &QPushButton::clicked, this,
            &MainWindow::editIssueDates);
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

    if (!repo.localPath.trimmed().isEmpty() &&
        QFileInfo::exists(repo.localPath + QStringLiteral("/.git")))
        return repo;



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

    const RepositoryRecord &writable = writableRecordFor(repo);
    if (!writable.localPath.trimmed().isEmpty() &&
        QFileInfo::exists(writable.localPath + QStringLiteral("/.git")))
        return writable.localPath;


    if (repo.previewOnly)
        return QString();



    const QString mirror = repo.mirrorPath.trimmed();
    if (!mirror.isEmpty() && QDir(mirror).exists())
        return mirror;
    return QString();
}

IssueStore MainWindow::issueStoreForCurrentRepo() const
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return IssueStore(QString(), QString(), &m_profileIdentity, chatDisplayName());
    const RepositoryRecord &repo = writableRecordFor(m_repositories.at(idx));
    return IssueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity, chatDisplayName());
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


    if (m_agentComposeRepo) {
        const QVariant prev = m_agentComposeRepo->count()
                                  ? m_agentComposeRepo->currentData()
                                  : QVariant();
        QSignalBlocker agentBlocker(m_agentComposeRepo);
        m_agentComposeRepo->clear();
        for (int i = 0; i < m_repositories.size(); ++i) {
            const RepositoryRecord &repo = m_repositories.at(i);
            if (repo.localPath.trimmed().isEmpty())
                continue;
            m_agentComposeRepo->addItem(repo.owner + "/" + repo.name, i);
        }
        if (prev.isValid()) {
            const int restore = m_agentComposeRepo->findData(prev);
            if (restore >= 0)
                m_agentComposeRepo->setCurrentIndex(restore);
        }
    }
}

QIcon MainWindow::issueAssigneeAvatar(const QString &name)
{
    const QString key = name.trimmed();
    if (key.isEmpty())
        return QIcon();
    auto it = m_assigneeAvatarCache.constFind(key);
    if (it != m_assigneeAvatarCache.constEnd())
        return it.value();


    QIcon icon(roundedAvatar(forkMeshAvatarPng(key.toLower()), 18));
    m_assigneeAvatarCache.insert(key, icon);
    return icon;
}

void MainWindow::reloadIssues()
{
    if (!m_issueTable)
        return;
    ++m_issueLoadGen;
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
        m_issuesLoadedSig.clear();
        renderIssueThread(Issue());
        updateIssueActionState();
        updateRepoIssueCount();
        return;
    }
    const IssueStore store = issueStoreForCurrentRepo();






    const QString sig = store.contentSignature();
    if (!sig.isEmpty() && sig == m_issuesLoadedSig)
        return;
    m_issuesLoadedSig = sig;





    m_currentIssues = store.loadAll(nullptr, [] {
        if (keepAliveClock().elapsed() - g_lastKeepAlivePumpMs >= 100)
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
    maybeRestoreIssueLooper();
}

void MainWindow::reloadIssuesInBackground()
{
    if (!m_issueTable || issuesRepoIndex() < 0)
        return;
    const int generation = ++m_issueLoadGen;
    if (m_issueBackgroundLoadInFlight) {
        m_issueBackgroundReloadQueued = true;
        return;
    }
    m_issueBackgroundLoadInFlight = true;
    const int repoIndex = issuesRepoIndex();
    const IssueStore store = issueStoreForCurrentRepo();
    const QString oldSignature = m_issuesLoadedSig;
    struct LoadedIssues {
        QString signature;
        QList<Issue> issues;
        QList<IssueLabel> labels;
        QList<IssueMilestone> milestones;
    };
    runOffThread<LoadedIssues>(
        [store, oldSignature] {
            const forkmesh::BackgroundScope activity(
                QStringLiteral("issues"), QStringLiteral("load issue metadata"),
                forkmesh::ActionTelemetry::Execution::Worker);
            LoadedIssues loaded;
            loaded.signature = store.contentSignature();
            if (!loaded.signature.isEmpty() &&
                loaded.signature == oldSignature)
                return loaded;
            loaded.issues = store.loadAll();
            loaded.labels = store.loadLabels();
            loaded.milestones = store.loadMilestones();
            return loaded;
        },
        [this, generation, repoIndex](LoadedIssues loaded) {
            m_issueBackgroundLoadInFlight = false;
            if (generation == m_issueLoadGen &&
                repoIndex == issuesRepoIndex() &&
                (loaded.signature.isEmpty() ||
                 loaded.signature != m_issuesLoadedSig)) {
                applyLoadedIssues(loaded.signature, std::move(loaded.issues),
                                  std::move(loaded.labels),
                                  std::move(loaded.milestones));
            }
            if (m_issueBackgroundReloadQueued) {
                m_issueBackgroundReloadQueued = false;
                reloadIssuesInBackground();
            }
        });
}

void MainWindow::applyLoadedIssues(const QString &signature, QList<Issue> issues,
                                   QList<IssueLabel> labels,
                                   QList<IssueMilestone> milestones)
{
    m_issuesLoadedSig = signature;
    m_currentIssues = std::move(issues);
    m_currentLabels = std::move(labels);
    m_currentMilestones = std::move(milestones);

    QSignalBlocker labelBlock(m_issueLabelFilter);
    m_issueLabelFilter->clear();
    m_issueLabelFilter->addItem(QStringLiteral("All labels"), QString());
    for (const IssueLabel &label : std::as_const(m_currentLabels))
        m_issueLabelFilter->addItem(label.name, label.name);
    labelBlock.unblock();

    QSignalBlocker milestoneBlock(m_issueMilestoneFilter);
    m_issueMilestoneFilter->clear();
    m_issueMilestoneFilter->addItem(QStringLiteral("All milestones"), QString());
    for (const IssueMilestone &milestone : std::as_const(m_currentMilestones))
        m_issueMilestoneFilter->addItem(milestone.title, milestone.title);
    milestoneBlock.unblock();

    refreshIssueList();
    refreshIssueMilestones();
    refreshIssueLabels();
    updateIssueActionState();
    updateRepoIssueCount();
}

void MainWindow::appendCreatedIssue(const IssueStore &store, const Issue &issue)
{



    ++m_issueLoadGen;




    m_currentIssues.append(issue);
    std::sort(m_currentIssues.begin(), m_currentIssues.end(),
              [](const Issue &a, const Issue &b) { return a.number < b.number; });



    const QString sig = store.contentSignature();
    if (!sig.isEmpty())
        m_issuesLoadedSig = sig;
    refreshIssueList();
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
    const bool tableMode = id == 0;
    const bool boardMode = id == 3;



    m_issueSearch->setVisible(tableMode || boardMode);
    m_issueLabelFilter->setVisible(tableMode || boardMode);
    m_issueMilestoneFilter->setVisible(tableMode || boardMode);
    m_issueStatusFilter->setVisible(tableMode);
    m_issueDetailToggle->setVisible(tableMode || boardMode);
    if (boardMode)
        refreshIssueBoard();
}

namespace {


const QStringList kDefaultBoardColumns = {QStringLiteral("Backlog"),
                                          QStringLiteral("Todo"),
                                          QStringLiteral("In Progress"),
                                          QStringLiteral("Done")};


QString boardStatusLabel(const QString &column)
{
    return QStringLiteral("status:") + column.trimmed().toLower();
}

bool isBoardStatusLabel(const QString &label)
{
    return label.startsWith(QStringLiteral("status:"), Qt::CaseInsensitive);
}
}

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

    if (issue.status == QStringLiteral("closed"))
        return cols.last();

    for (const QString &col : cols) {
        const QString want = boardStatusLabel(col);
        for (const QString &lbl : issue.labels)
            if (lbl.compare(want, Qt::CaseInsensitive) == 0)
                return col;
    }

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
        return;
    const bool toDone = column.compare(cols.last(), Qt::CaseInsensitive) == 0;



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


    while (QLayoutItem *child = m_issueBoardColumns->takeAt(0)) {
        if (QWidget *w = child->widget())
            w->deleteLater();
        delete child;
    }

    const QStringList cols = boardColumns();
    QHash<QString, QString> labelColors;
    for (const IssueLabel &label : std::as_const(m_currentLabels))
        labelColors.insert(label.name, label.color);



    const QString labelFilter =
        m_issueLabelFilter ? m_issueLabelFilter->currentData().toString() : QString();
    const QString msFilter =
        m_issueMilestoneFilter ? m_issueMilestoneFilter->currentData().toString()
                               : QString();
    const QString search =
        m_issueSearch ? m_issueSearch->text().trimmed() : QString();


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


const char *kAgentSpinFrames[] = {"\xE2\xA0\x8B", "\xE2\xA0\x99", "\xE2\xA0\xB9",
                                  "\xE2\xA0\xB8", "\xE2\xA0\xBC", "\xE2\xA0\xB4",
                                  "\xE2\xA0\xA6", "\xE2\xA0\xA7", "\xE2\xA0\x87",
                                  "\xE2\xA0\x8F"};
}

void MainWindow::resetIssueFilters()
{
    if (!m_issueStatusFilter)
        return;


    bool changed = false;
    {
        QSignalBlocker statusBlock(m_issueStatusFilter);
        QSignalBlocker labelBlock(m_issueLabelFilter);
        QSignalBlocker msBlock(m_issueMilestoneFilter);
        if (m_issueStatusFilter->currentIndex() != 0) {
            m_issueStatusFilter->setCurrentIndex(0);
            changed = true;
        }
        if (m_issueLabelFilter->currentIndex() != 0) {
            m_issueLabelFilter->setCurrentIndex(0);
            changed = true;
        }
        if (m_issueMilestoneFilter->currentIndex() != 0) {
            m_issueMilestoneFilter->setCurrentIndex(0);
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


    const int selectNext = m_selectIssueOnReload;
    m_selectIssueOnReload = -1;
    const int keep = selectNext > 0 ? selectNext : m_currentIssueNumber;



    const bool keepCurrent = m_keepCurrentOnReload;
    m_keepCurrentOnReload = false;
    const bool detailWasOpen = m_issueDetail && m_issueDetail->isVisible();





    TableRepaintGuard repaintGuard(m_issueTable);
    m_issueTable->blockSignals(true);
    m_issueTable->setSortingEnabled(false);
    QList<const Issue *> visible;
    visible.reserve(m_currentIssues.size());
    for (const Issue &issue : m_currentIssues) {
        if (statusFilter == "Open" && issue.status != "open")
            continue;
        if (statusFilter == "Closed" && issue.status != "closed")
            continue;
        if (!labelFilter.isEmpty() && !issue.labels.contains(labelFilter))
            continue;
        if (!msFilter.isEmpty() && issue.milestone != msFilter)
            continue;





        if (issue.isDeleted() && statusFilter != "All")
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
        visible.append(&issue);
    }



    m_issueTable->setRowCount(visible.size());
    for (int row = 0; row < visible.size(); ++row) {
        const Issue &issue = *visible.at(row);

        auto *numItem = new QTableWidgetItem;

        numItem->setData(Qt::DisplayRole, issue.number);
        numItem->setData(Qt::UserRole, issue.number);
        m_issueTable->setItem(row, 0, numItem);


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

        priority->setData(kTableSortRole,
                          issue.priority > 0 ? issue.priority : 100);
        priority->setTextAlignment(Qt::AlignCenter);
        priority->setToolTip("1 is highest priority; 99 is lowest");
        m_issueTable->setItem(row, 2, priority);

        QString statusText = issue.status == "closed" ? "Closed" : "Open";
        QColor statusColor(issue.status == "closed" ? "#f85149" : "#3fb950");
        QString statusTip;
        if (issue.isDeleted()) {
            statusText = QStringLiteral("Deleted");
            statusColor = QColor("#8b949e");
            statusTip = QStringLiteral("Deleted by its author.");
        } else if (issue.hasUnauthorizedDeleteAttempt()) {


            statusText += QStringLiteral(" ⚠");
            statusColor = QColor("#d29922");
            statusTip = QStringLiteral(
                "Someone who did not open this issue tried to delete it; the "
                "deletion was not applied.");
        }
        auto *status = new QTableWidgetItem(statusText);
        status->setForeground(statusColor);
        if (!statusTip.isEmpty())
            status->setToolTip(statusTip);
        m_issueTable->setItem(row, 3, status);
        auto *votes = new QTableWidgetItem;
        votes->setData(Qt::DisplayRole, issue.votes);
        votes->setTextAlignment(Qt::AlignCenter);
        m_issueTable->setItem(row, 4, votes);
        m_issueTable->setItem(row, 5, new QTableWidgetItem(issue.labels.join(", ")));
        m_issueTable->setItem(row, 6, new QTableWidgetItem(issue.milestone));


        auto *created = new QTableWidgetItem(
            issue.createdAt > 0
                ? QDateTime::fromMSecsSinceEpoch(issue.createdAt).toString("yyyy-MM-dd HH:mm")
                : QString());
        created->setToolTip(formatIssueRelativeTime(issue.createdAt));
        m_issueTable->setItem(row, 7, created);




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


            QString text = agentProviderName(session->provider);
            const bool working = agentSessionActive(session);
            if (working)
                text = QString::fromUtf8(kAgentSpinFrames[m_issueSpinFrame % 10]) +
                       QStringLiteral(" ") + text;
            auto *agentItem = new QTableWidgetItem(text);

            if (working)
                agentItem->setForeground(QColor(Theme::kRunning));
            agentItem->setData(Qt::UserRole, session->id);
            m_issueTable->setItem(row, 9, agentItem);
        } else {
            m_issueTable->setItem(row, 9, new QTableWidgetItem(QString()));
        }


        const QString author =
            issue.authorName.trimmed().isEmpty()
                ? (issue.author.isEmpty() ? QString::fromUtf8("\xE2\x80\x94")
                                          : issue.author.left(8))
                : issue.authorName.trimmed();
        auto *authorItem = new QTableWidgetItem(author);
        authorItem->setToolTip(issue.author);
        m_issueTable->setItem(row, 10, authorItem);




        const int pct = qBound(0, issue.progress, 100);
        auto *progressItem = new SortTableWidgetItem(QString());
        progressItem->setData(kTableSortRole, pct);
        progressItem->setData(kProgressBarRole, pct);
        progressItem->setToolTip(QStringLiteral("%1% complete").arg(pct));
        m_issueTable->setItem(row, 11, progressItem);


        const double estUsd = openAiEstimateUsd(issue);
        auto *estItem = new SortTableWidgetItem(
            QStringLiteral("$%1").arg(QString::number(estUsd, 'f', 2)));
        estItem->setData(kTableSortRole, estUsd);
        estItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_issueTable->setItem(row, 12, estItem);


        auto *bountyItem = new SortTableWidgetItem(
            issue.bountyUsd > 0
                ? QStringLiteral("$%1").arg(QString::number(issue.bountyUsd, 'f', 2))
                : QString::fromUtf8("\xE2\x80\x94"));
        bountyItem->setData(kTableSortRole, issue.bountyUsd);
        bountyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (!issue.bountyStatus.isEmpty())
            bountyItem->setToolTip(
                QStringLiteral("Legacy bounty (migration-only), historical "
                               "status: %1")
                    .arg(issue.bountyStatus));
        m_issueTable->setItem(row, 13, bountyItem);




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



        populateIssueFilesCell(row, issue);


        m_issueTable->setItem(
            row, 16,
            new QTableWidgetItem(issue.assignees.isEmpty()
                                     ? QString::fromUtf8("\xE2\x80\x94")
                                     : issue.assignees.join(QStringLiteral(", "))));
    }
    m_issueTable->setSortingEnabled(true);
    m_issueTable->blockSignals(false);


    int selRow = -1;
    for (int r = 0; r < m_issueTable->rowCount(); ++r) {
        if (m_issueTable->item(r, 0)->data(Qt::UserRole).toInt() == keep) {
            selRow = r;
            break;
        }
    }
    if (selRow >= 0) {


        m_issueTable->selectRow(selRow);
    } else if (keepCurrent && detailWasOpen && keep > 0) {





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



    if (m_issueListStack && m_issueListStack->currentIndex() == 3)
        refreshIssueBoard();
}



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
                m_issueDetail->raise();
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


    if (m_issueAiTypingTimer)
        m_issueAiTypingTimer->stop();
    m_issueAiTypingTimer = nullptr;
    m_issueAiTypingRow = nullptr;


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
        if (m_issueDatesValue)
            m_issueDatesValue->setText("No dates");
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
    if (m_issueDatesValue) {
        auto dateText = [](qint64 ms) {
            return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms)
                                .toString(QStringLiteral("yyyy-MM-dd"))
                          : QString::fromUtf8("\xE2\x80\x94");
        };
        m_issueDatesValue->setText(
            issue.startDate <= 0 && issue.endDate <= 0
                ? QStringLiteral("No dates")
                : QString::fromUtf8("%1 \xE2\x86\x92 %2")
                      .arg(dateText(issue.startDate), dateText(issue.endDate)));
    }
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
                QStringLiteral("<b>$%1</b> <span style='color:#8b949e'>"
                               "(legacy, migration-only: %2)</span>")
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
    if (m_issueStartDateEdit && m_issueStartDateEnable) {
        m_issueStartDateEnable->setChecked(issue.startDate > 0);
        m_issueStartDateEdit->setDate(
            issue.startDate > 0
                ? QDateTime::fromMSecsSinceEpoch(issue.startDate).date()
                : QDate::currentDate());
    }
    if (m_issueEndDateEdit && m_issueEndDateEnable) {
        m_issueEndDateEnable->setChecked(issue.endDate > 0);
        m_issueEndDateEdit->setDate(
            issue.endDate > 0
                ? QDateTime::fromMSecsSinceEpoch(issue.endDate).date()
                : QDate::currentDate());
    }
    if (m_issuePriorityEdit) {
        const int selected = m_issuePriorityEdit->findData(issue.priority);
        if (selected >= 0)
            m_issuePriorityEdit->setCurrentIndex(selected);
    }
    cancelIssueSidebarEditors();




    QHash<QString, IssueEvent> edits;
    QSet<QString> deleted;
    QString openId;
    for (const IssueEvent &ev : issue.events) {
        if (ev.type == "open")
            openId = ev.id;
        else if (ev.type == "edit" && !ev.target.isEmpty())
            edits.insert(ev.target, ev);
        else if (ev.type == "delete" && !ev.target.isEmpty() && ev.target != "self")
            deleted.insert(ev.target);
    }

    const int idx = issuesRepoIndex();
    const QString imageBase =
        idx >= 0 ? IssueStore::issueDirPath(m_repositories.at(idx).localPath,
                                           issue.number) +
                       "/"
                 : QString();
    const bool haveLocalFiles = !imageBase.isEmpty() &&
                                QFileInfo::exists(
                                    imageBase + QStringLiteral("issue-%1.json")
                                                    .arg(issue.number));
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
                editor->setPreviewBasePath(IssueStore::issueDirPath(
                    m_repositories.at(repoIdx).localPath, num));
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




            auto *cancel = new QPushButton("Cancel", bodyContainer);
            cancel->setObjectName("ghostButton");
            cancel->setCursor(Qt::PointingHandCursor);
            auto *save = new QPushButton("Save", bodyContainer);
            save->setObjectName("primaryButton");
            save->setCursor(Qt::PointingHandCursor);
            buttonRow->addStretch();
            buttonRow->addWidget(cancel);
            buttonRow->addWidget(save);
            bodyLayout->addLayout(buttonRow);







            QPointer<QPushButton> saveGuard(save);
            QTimer::singleShot(0, this, [saveGuard]() {
                if (!saveGuard)
                    return;
                saveGuard->style()->unpolish(saveGuard);
                saveGuard->style()->polish(saveGuard);
                saveGuard->update();
            });
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
        } else if (ev.type == "title")
            addActivity(ev.title.isEmpty()
                            ? QStringLiteral("cleared the title")
                            : QStringLiteral("changed the title to \"%1\"").arg(ev.title),
                        ev.ts, who);
        else if (ev.type == "progress")
            addActivity(QStringLiteral("set progress to %1%").arg(ev.progress), ev.ts, who);
        else if (ev.type == "dates")
            addActivity(QStringLiteral("updated the schedule dates"), ev.ts, who);
        else if (ev.type == "bounty")
            addActivity(ev.bountyUsd > 0
                            ? QStringLiteral("set a $%1 bounty%2")
                                  .arg(QString::number(ev.bountyUsd),
                                       ev.bountyStatus.isEmpty()
                                           ? QString()
                                           : QStringLiteral(" (%1)").arg(ev.bountyStatus))
                            : QStringLiteral("cleared the bounty"),
                        ev.ts, who);
        else if (ev.type == "edit")
            addActivity(ev.target == openId ? QStringLiteral("edited the description")
                                            : QStringLiteral("edited a comment"),
                        ev.ts, who);
        else if (ev.type == "delete")
            addActivity(ev.target == "self" ? QStringLiteral("deleted this issue")
                                            : QStringLiteral("deleted a comment"),
                        ev.ts, who);
        else if (ev.type == "vote")
            addActivity(QStringLiteral("voted on this issue"), ev.ts, who);
        else if (!ev.type.isEmpty())


            addActivity(QStringLiteral("recorded a %1 action").arg(ev.type), ev.ts, who);
    }
    if (m_repoDetailIndex >= 0 &&
        m_repoDetailIndex < m_repositories.size() && m_networkAccess) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        auto *remoteThread =
            new FederatedThreadView(m_networkAccess, m_issueThreadContainer);
        const QUrl server(canonicalServerUrl(
            m_activeServer >= 0 && m_activeServer < m_servers.size()
                ? m_servers.at(m_activeServer).url
                : QString()));
        remoteThread->load(server, repo.owner, repo.name,
                           QStringLiteral("issue"), issue.number);
        m_issueThreadLayout->addWidget(remoteThread);
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




    auto *scroll = new QScrollArea;
    scroll->setObjectName("issueComposeScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    m_issueComposePage = scroll;
    m_issueDetailStack->addWidget(scroll);
    m_issueDetailStack->setCurrentWidget(scroll);
    if (m_issueDetail) {
        m_issueDetail->setVisible(true);
        m_issueDetail->raise();
    }
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
    composeNewIssue(QString(), QString());
}






void MainWindow::promptIssueFromChatMessage(const QString &text,
                                            const QString &senderName,
                                            qint64 timestampMs)
{
    const QString message = text.trimmed();
    if (message.isEmpty())
        return;
    if (m_repositories.isEmpty()) {
        flashMessage("Open a repository before filing an issue from chat.");
        return;
    }



    int repoIndex = m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()
                        ? m_repoDetailIndex
                        : 0;
    if (m_repositories.size() > 1) {
        QStringList labels;
        labels.reserve(m_repositories.size());
        for (const RepositoryRecord &repo : std::as_const(m_repositories))
            labels << repo.owner + "/" + repo.name;
        bool ok = false;
        const QString pick = QInputDialog::getItem(
            this, QStringLiteral("Create issue from message"),
            QStringLiteral("File this message as an issue in:"), labels,
            repoIndex, false, &ok);
        if (!ok || pick.isEmpty())
            return;
        repoIndex = labels.indexOf(pick);
        if (repoIndex < 0)
            return;
    }

    QString title = message.section('\n', 0, 0).simplified();
    if (title.size() > 200)
        title = title.left(199) + QString::fromUtf8("\xE2\x80\xA6");
    const QString when =
        QDateTime::fromMSecsSinceEpoch(timestampMs > 0
                                           ? timestampMs
                                           : QDateTime::currentMSecsSinceEpoch())
            .toUTC()
            .toString(Qt::ISODate);
    const QString channel = m_currentConversation.trimmed();
    const QString body =
        message + "\n\n---\nFiled from a " +
        (channel.isEmpty() ? QString() : channel + " ") + "chat message by " +
        (senderName.trimmed().isEmpty() ? QStringLiteral("someone")
                                        : senderName.trimmed()) +
        " at " + when + ".";

    if (repoIndex != m_repoDetailIndex)
        openRepoDetail(repoIndex);
    showSection(0);

    if (m_repoDetailTabs && m_repoDetailTabs->button(2))
        m_repoDetailTabs->button(2)->click();
    composeNewIssue(title, body);
}

void MainWindow::composeNewIssue(const QString &prefillTitle,
                                 const QString &prefillBody)
{



    if (issuesRepoIndex() < 0)
        return;

    auto *page = new QWidget;

    auto *titleLabel = new QLabel("Add a title <span style='color:#cf222e'>*</span>",
                                  page);
    titleLabel->setTextFormat(Qt::RichText);
    titleLabel->setObjectName("sectionLabel");
    auto *titleEdit = new QLineEdit(page);
    titleEdit->setPlaceholderText("Title");
    titleEdit->setText(prefillTitle);
    auto *bodyEdit = new MarkdownEditor(page);
    bodyEdit->setMentionCandidates(mentionCandidateNames());



    bodyEdit->setMinimumHeight(200);
    bodyEdit->setPlaceholderText("Type your description here...");
    if (!prefillBody.isEmpty())
        bodyEdit->setMarkdown(prefillBody);

    auto *left = new QWidget(page);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(makeComposerIdentity(nullptr, QStringLiteral("Filing")));
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
    sidebar->setMinimumWidth(190);
    sidebar->setMaximumWidth(285);
    sidebar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
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



        if (!store.canWrite()) {
            const QPointer<QWidget> pageGuard(page);
            createButton->setEnabled(false);
            setPageNotice("Sending your issue to the maintainer's inbox...");
            const bool started = submitNewIssueToInbox(
                title, bodyEdit->markdown(), labels, milestone, priority,
                assignees, bodyEdit->pendingAttachments(),
                bodyEdit->pendingAttachmentPlaceholders(),
                [this, pageGuard, createButton, setPageNotice](bool ok,
                                                                const QString &error) {


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
        Issue created;
        const int number = store.createIssue(title, bodyEdit->markdown(), labels,
                                             milestone, priority, assignees,
                                             bodyEdit->pendingAttachments(),
                                             bodyEdit->pendingAttachmentPlaceholders(),
                                             &error, &created);
        if (number < 0) {
            setPageNotice(error.isEmpty() ? "Could not create the issue." : error,
                          true);
            return;
        }
        m_currentIssueNumber = number;
        const bool more = createMore->isChecked();
        removeIssueComposePage();
        appendCreatedIssue(store, created);
        propagateRepoUpdate(issuesRepoIndex());
        setIssueInlineNotice("Issue created.");
        if (more)
            promptNewIssue();
    });





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


    recordQuickAddHistory(title);






    const QString quickAddProvider =
        m_quickAddAgentProvider
            ? m_quickAddAgentProvider->currentData().toString()
            : QStringLiteral("claude-code");
    if (quickAddProvider != QLatin1String("manual")) {
        const QString provider = quickAddProvider;
        const QString model = (provider == QLatin1String("claude-code") ||
                               agentIsCodexProvider(provider))
                                  ? selectedModelComboValue(m_quickAddClaudeModel)
                                  : QString();
        const bool createPr = m_quickAddCreatePr && m_quickAddCreatePr->isChecked();


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




            QStringList details;
            const QString modelLabel = agentModelLabel(model);
            if (!modelLabel.isEmpty())
                details << modelLabel;
            if (m_quickAddModeSelector)
                details << m_quickAddModeSelector->currentText();
            const QString suffix =
                details.isEmpty()
                    ? QString()
                    : QStringLiteral(" (%1)").arg(details.join(QStringLiteral(", ")));
            setIssueInlineNotice(
                QStringLiteral("Started a %1 agent on your prompt%2.")
                    .arg(agentProviderName(provider), suffix));
        }
        return;
    }

    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {


        if (issuesRepoIndex() < 0) {
            setIssueInlineNotice("Pick a repository to add issues.", true);
            return;
        }
        QPointer<QPlainTextEdit> quickAddGuard(m_issueQuickAdd);
        quickAddGuard->setEnabled(false);


        const bool started = submitNewIssueToInbox(
            title, QString(), {}, QString(), 0, {}, m_quickAddImages, {},
            [this, quickAddGuard, title](bool ok, const QString &) {




                if (!quickAddGuard)
                    return;
                quickAddGuard->setEnabled(true);
                if (ok) {
                    quickAddGuard->clear();
                    clearQuickAddImages();
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
    Issue created;

    const int number = store.createIssue(title, QString(), {}, QString(), 0, {},
                                         m_quickAddImages, &error, &created);
    if (number < 0) {
        setIssueInlineNotice(error.isEmpty() ? "Could not create the issue." : error,
                             true);
        return;
    }
    m_issueQuickAdd->clear();
    clearQuickAddImages();
    m_currentIssueNumber = number;
    appendCreatedIssue(store, created);
    propagateRepoUpdate(issuesRepoIndex());


    setIssueInlineNotice(QStringLiteral("Issue #%1 created: %2").arg(number).arg(title));





    showIssue(number);
}

#ifdef FORKMESH_WINDOW_TESTS
int MainWindow::testQuickAddIssueNoAgent(const QString &title)
{
    if (!m_issueQuickAdd)
        return -1;


    if (m_quickAddAgentProvider) {
        const int idx = m_quickAddAgentProvider->findData(QStringLiteral("manual"));
        if (idx >= 0)
            m_quickAddAgentProvider->setCurrentIndex(idx);
    }
    m_issueQuickAdd->setPlainText(title);
    quickAddIssue();
    return m_currentIssueNumber;
}
#endif




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

    QSettings().setValue(kQuickAddHistorySetting, m_quickAddHistory);
}




bool MainWindow::navigateQuickAddHistory(int direction)
{
    if (!m_issueQuickAdd || m_quickAddHistory.isEmpty())
        return false;


    auto showText = [this](const QString &text) {
        m_quickAddHistoryNavigating = true;
        m_issueQuickAdd->setPlainText(text);
        m_issueQuickAdd->moveCursor(QTextCursor::End);
        m_quickAddHistoryNavigating = false;
    };
    const int count = m_quickAddHistory.size();
    if (direction < 0) {
        if (m_quickAddHistoryIndex < 0) {

            m_quickAddDraft = m_issueQuickAdd->toPlainText();
            m_quickAddHistoryIndex = count - 1;
        } else if (m_quickAddHistoryIndex > 0) {
            --m_quickAddHistoryIndex;
        } else {
            return true;
        }
        showText(m_quickAddHistory.at(m_quickAddHistoryIndex));
        return true;
    }

    if (m_quickAddHistoryIndex < 0)
        return false;
    if (m_quickAddHistoryIndex < count - 1) {
        ++m_quickAddHistoryIndex;
        showText(m_quickAddHistory.at(m_quickAddHistoryIndex));
    } else {
        m_quickAddHistoryIndex = -1;
        showText(m_quickAddDraft);
    }
    return true;
}




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




bool MainWindow::tryPasteImageIntoQuickAdd()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasImage())
        return false;


    const QString path =
        saveNewAgentPromptImage(qvariant_cast<QImage>(mime->imageData()));
    if (path.isEmpty())
        return false;
    queueQuickAddImage(path);
    return true;
}


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




void MainWindow::rebuildQuickAddAttachChips()
{
    if (!m_quickAddAttachStrip)
        return;
    auto *row = qobject_cast<QHBoxLayout *>(m_quickAddAttachStrip->layout());
    if (!row)
        return;

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




void MainWindow::showQuickAddImageDetail(const QString &path)
{
    QPixmap pixmap(path);
    if (pixmap.isNull())
        return;

    auto *dialog = new QDialog(this);
    dialog->setObjectName("imageDetailDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QFileInfo(path).fileName());



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



    QHash<QString, IssueEvent> edits;
    QSet<QString> deleted;
    for (const IssueEvent &ev : issue->events) {
        if (ev.type == QLatin1String("edit") && !ev.target.isEmpty())
            edits.insert(ev.target, ev);
        else if (ev.type == QLatin1String("delete") && !ev.target.isEmpty() &&
                 ev.target != QLatin1String("self"))
            deleted.insert(ev.target);
    }




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


    const int insertAt = qMax(0, m_issueThreadLayout->count() - 1);
    m_issueThreadLayout->insertWidget(insertAt, row);
    m_issueAiTypingRow = row;


    m_issueAiTypingTimer = new QTimer(row);
    connect(m_issueAiTypingTimer, &QTimer::timeout, label, [label, n = 0]() mutable {
        n = (n + 1) % 4;
        label->setText(QString::fromUtf8("%1\xF0\x9F\xA4\x96  AI is answering%2")
                           .arg(QString(n, QChar(' ')), QString(n, QChar('.'))));
    });
    m_issueAiTypingTimer->start(400);


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
        m_issueAiTypingTimer = nullptr;
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


                qint64 inTok = 0, outTok = 0;
                const double costUsd = openAiAskCostUsd(obj, &inTok, &outTok);
                const QString costLine =
                    QString::fromUtf8("\n\n*\xF0\x9F\xA4\x96 %1 \xC2\xB7 cost "
                                      "$%2 (%3 in / %4 out tokens)*")
                        .arg(kIssueAskAiModel,
                             QString::number(costUsd, 'f', 4))
                        .arg(inTok)
                        .arg(outTok);


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

        submitIssueCommentToInbox(body, attachments, placeholders);
        return;
    }

    const int number = m_currentIssueNumber;






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



    QTimer::singleShot(0, this, [this, number, body, attachments, placeholders]() {
        IssueStore store = issueStoreForCurrentRepo();
        QString error;
        if (!store.addComment(number, body, attachments, placeholders, &error)) {
            setIssueInlineNotice(error.isEmpty() ? "Could not add the comment." : error,
                                 true);
            reloadIssues();
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

        setIssueInlineNotice("This repo is read-only here; can't close the issue.",
                             true);
        return;
    }


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

namespace {



QString promptDraftText(const QWidget *w)
{
    if (const auto *plain = qobject_cast<const QPlainTextEdit *>(w))
        return plain->toPlainText().trimmed();
    if (const auto *line = qobject_cast<const QLineEdit *>(w))
        return line->text().trimmed();
    if (const auto *rich = qobject_cast<const QTextEdit *>(w))
        return rich->toPlainText().trimmed();
    return QString();
}
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (handleFramelessResizeEvent(obj, event))
        return true;





    if (event->type() == QEvent::Show) {
        if (auto *w = qobject_cast<QWidget *>(obj)) {
            if (auto *combo = qobject_cast<QComboBox *>(w->parent())) {
                if (combo->property("claudeModelCombo").toBool())
                    applyLiveClaudeModelsToCombos();
            }
        }
    }


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



    if (event->type() == QEvent::Resize && m_pullDiff &&
        obj == m_pullDiff->viewport())
        layoutPullStickyHeader();

    if (event->type() == QEvent::Resize && m_scmDiff &&
        obj == m_scmDiff->viewport())
        layoutScmStickyHeader();



    if (event->type() == QEvent::Resize && obj == m_footerUpdateLog) {
        positionFloatingLogButton();


        positionFooterLogPauseButton();
    }


    if (event->type() == QEvent::ContextMenu) {
        if (maybeShowSendToPromptMenu(obj, static_cast<QContextMenuEvent *>(event)))
            return true;
    }




    if (obj == m_navSolanaBalance && event->type() == QEvent::Enter)
        refreshNavSolanaBalance();

    if (obj == m_navSolanaBalance &&
        event->type() == QEvent::MouseButtonRelease &&
        !m_navSolanaBalanceAddress.isEmpty()) {
        cycleNavSolanaCurrency();
        return true;
    }



    if (event->type() == QEvent::MouseButtonRelease) {
        if (auto *w = qobject_cast<QWidget *>(obj)) {
            if (w->property("slashKind").isValid()) {
                activateSlashActionRow(w);
                return true;
            }
        }
    }



    if (obj == m_messageInput && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->matches(QKeySequence::Paste) && trySendClipboardImage())
            return true;
    }












    if (m_mentionCompleterPopup && obj == m_mentionCompleterPopup &&
        event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Tab) {
            const QModelIndex idx = m_mentionCompleterPopup->currentIndex();
            QString name;
            if (idx.isValid())
                name = idx.data(Qt::DisplayRole).toString();
            else if (m_mentionCompleter->setCurrentRow(0))
                name = m_mentionCompleter->currentCompletion();
            if (!name.isEmpty()) {
                insertMention(name);
                m_mentionCompleterPopup->hide();
                return true;
            }
        }
    }



    if (obj == m_issueQuickAdd && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->matches(QKeySequence::Paste) && tryPasteImageIntoQuickAdd())
            return true;








        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            !(ke->modifiers() & Qt::ShiftModifier)) {
            if (quickAddShouldFollowUpAgent() && m_quickAddSendToAgentButton)
                m_quickAddSendToAgentButton->click();
            else
                quickAddIssue();
            return true;
        }


        if (ke->key() == Qt::Key_Up && navigateQuickAddHistory(-1))
            return true;
        if (ke->key() == Qt::Key_Down && navigateQuickAddHistory(1))
            return true;
    }








    if (event->type() == QEvent::FocusOut &&
        (obj == m_issueQuickAdd || obj == m_messageInput)) {
        const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
        if (reason == Qt::OtherFocusReason || reason == Qt::NoFocusReason) {
            QPointer<QWidget> prompt = qobject_cast<QWidget *>(obj);
            if (prompt && !promptDraftText(prompt).isEmpty()) {
                QTimer::singleShot(0, this, [this, prompt] {
                    if (prompt && prompt->isVisible() && prompt->isEnabled() &&
                        isActiveWindow() &&
                        QApplication::focusWidget() != prompt &&
                        !promptDraftText(prompt).isEmpty())
                        prompt->setFocus(Qt::OtherFocusReason);
                });
            }
        }
    }



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

    if (m_issueTable && obj == m_issueTable->viewport() &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonRelease)) {
        if (handleIssueProgressDrag(static_cast<QMouseEvent *>(event)))
            return true;
    }




    if (m_footerUpdateLog && obj == m_footerUpdateLog->viewport() &&
        (event->type() == QEvent::ToolTip ||
         event->type() == QEvent::MouseButtonRelease)) {
        auto lineAt = [this](const QPoint &pos) -> QString {
            const QTextCursor cur = m_footerUpdateLog->cursorForPosition(pos);
            if (auto *data =
                    static_cast<FooterLogLineData *>(cur.block().userData()))
                return data->rawLine;
            return QString();
        };
        if (event->type() == QEvent::ToolTip) {
            auto *he = static_cast<QHelpEvent *>(event);


            if (!logPromptAnchorLine(m_footerUpdateLog->anchorAt(he->pos()))
                     .isEmpty()) {
                QToolTip::showText(he->globalPos(),
                                   QStringLiteral("Add this log entry to the prompt"),
                                   m_footerUpdateLog->viewport());
                return true;
            }
            const QString line = lineAt(he->pos());
            if (!line.isEmpty()) {
                QToolTip::showText(he->globalPos(), line,
                                   m_footerUpdateLog->viewport());
                return true;
            }
        } else {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const QPoint pos = me->position().toPoint();


                const QString promptLine =
                    logPromptAnchorLine(m_footerUpdateLog->anchorAt(pos));
                if (!promptLine.isEmpty()) {
                    appendTextToActivePrompt(promptLine);
                    return true;
                }
                const QString line = lineAt(pos);
                if (!line.isEmpty()) {
                    openFullLogAtFooterLine(line);
                    return true;
                }
            }
        }
    }




    if (m_settingsLog && obj == m_settingsLog->viewport() &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseButtonRelease ||
         event->type() == QEvent::ToolTip)) {
        if (event->type() == QEvent::ToolTip) {
            auto *he = static_cast<QHelpEvent *>(event);
            if (!logPromptAnchorLine(m_settingsLog->anchorAt(he->pos())).isEmpty()) {
                QToolTip::showText(he->globalPos(),
                                   QStringLiteral("Add this log entry to the prompt"),
                                   m_settingsLog->viewport());
                return true;
            }
        } else {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const QString promptLine = logPromptAnchorLine(
                    m_settingsLog->anchorAt(me->position().toPoint()));
                if (!promptLine.isEmpty()) {
                    if (event->type() == QEvent::MouseButtonRelease)
                        appendTextToActivePrompt(promptLine);
                    return true;
                }
            }
        }
    }


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


            hideGlobalSearchPopup();
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

Qt::Edges MainWindow::resizeEdgesAtGlobalPos(const QPoint &globalPos) const
{
    if (!isVisible() || isMaximized() || isFullScreen())
        return {};

    const QPoint pos = mapFromGlobal(globalPos);
    const QRect r = rect();
    if (!r.adjusted(-1, -1, 1, 1).contains(pos))
        return {};

    constexpr int kResizeBorder = 8;
    Qt::Edges edges;
    if (pos.x() <= r.left() + kResizeBorder)
        edges |= Qt::LeftEdge;
    if (pos.x() >= r.right() - kResizeBorder)
        edges |= Qt::RightEdge;
    if (pos.y() <= r.top() + kResizeBorder)
        edges |= Qt::TopEdge;
    if (pos.y() >= r.bottom() - kResizeBorder)
        edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::updateFramelessResizeCursor(Qt::Edges edges)
{
    if (edges == (Qt::LeftEdge | Qt::TopEdge) ||
        edges == (Qt::RightEdge | Qt::BottomEdge)) {
        if (m_framelessResizeCursorActive)
            qApp->changeOverrideCursor(Qt::SizeFDiagCursor);
        else
            qApp->setOverrideCursor(Qt::SizeFDiagCursor);
        m_framelessResizeCursorActive = true;
        return;
    }
    if (edges == (Qt::RightEdge | Qt::TopEdge) ||
        edges == (Qt::LeftEdge | Qt::BottomEdge)) {
        if (m_framelessResizeCursorActive)
            qApp->changeOverrideCursor(Qt::SizeBDiagCursor);
        else
            qApp->setOverrideCursor(Qt::SizeBDiagCursor);
        m_framelessResizeCursorActive = true;
        return;
    }
    if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
        if (m_framelessResizeCursorActive)
            qApp->changeOverrideCursor(Qt::SizeHorCursor);
        else
            qApp->setOverrideCursor(Qt::SizeHorCursor);
        m_framelessResizeCursorActive = true;
        return;
    }
    if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
        if (m_framelessResizeCursorActive)
            qApp->changeOverrideCursor(Qt::SizeVerCursor);
        else
            qApp->setOverrideCursor(Qt::SizeVerCursor);
        m_framelessResizeCursorActive = true;
        return;
    }
    if (m_framelessResizeCursorActive) {
        qApp->restoreOverrideCursor();
        m_framelessResizeCursorActive = false;
    }
}

bool MainWindow::handleFramelessResizeEvent(QObject *obj, QEvent *event)
{
    if (!qApp || !testAttribute(Qt::WA_WState_Created))
        return false;

    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::HoverMove: {
        if (auto *mouse = dynamic_cast<QMouseEvent *>(event)) {
            updateFramelessResizeCursor(
                resizeEdgesAtGlobalPos(mouse->globalPosition().toPoint()));
        } else if (auto *hover = dynamic_cast<QHoverEvent *>(event)) {
            updateFramelessResizeCursor(
                resizeEdgesAtGlobalPos(mapToGlobal(hover->position().toPoint())));
        }
        return false;
    }
    case QEvent::Leave:
        if (!resizeEdgesAtGlobalPos(QCursor::pos()))
            updateFramelessResizeCursor({});
        return false;
    case QEvent::MouseButtonPress: {
        auto *mouse = dynamic_cast<QMouseEvent *>(event);
        if (!mouse || mouse->button() != Qt::LeftButton)
            return false;
        const Qt::Edges edges =
            resizeEdgesAtGlobalPos(mouse->globalPosition().toPoint());
        if (!edges)
            return false;
        if (QWindow *handle = windowHandle()) {
            mouse->accept();
            handle->startSystemResize(edges);
            return true;
        }
        return false;
    }
    case QEvent::MouseButtonRelease:
        updateFramelessResizeCursor(resizeEdgesAtGlobalPos(QCursor::pos()));
        return false;
    case QEvent::WindowStateChange:
        updateFramelessResizeCursor({});
        return false;
    default:
        return false;
    }
}













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




    const QString promptText = QString(selected).replace(QChar(0x2029), QLatin1Char('\n'));
    menu->addSeparator();
    QAction *sendToPrompt = menu->addAction(tr("Send to Prompt"));
    QAction *searchCodebase = menu->addAction(tr("Search Codebase"));
    QAction *chosen = menu->exec(ce->globalPos());
    if (chosen == sendToPrompt)
        appendTextToActivePrompt(promptText);
    else if (chosen == searchCodebase)
        openSearchResultsPage(promptText);
    delete menu;
    return true;
}




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


    const int nextIssue = closing ? nextVisibleIssueAfter(m_currentIssueNumber) : -1;
    if (!store.setStatus(m_currentIssueNumber, closing ? "closed" : "open",
                         &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update issue status." : error,
                             true);
        return;
    }
    if (closing && nextIssue > 0) {

        m_selectIssueOnReload = nextIssue;
    } else {




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

void MainWindow::editIssueDates()
{
    if (m_currentIssueNumber < 0)
        return;
    m_issueDeleteConfirmPending = false;
    setIssueInlineNotice(QString());
    if (m_issueDatesStack)
        m_issueDatesStack->setCurrentIndex(1);
    if (m_issueStartDateEdit)
        m_issueStartDateEdit->setFocus();
}

void MainWindow::saveIssueDatesInline()
{
    if (m_currentIssueNumber < 0 || !m_issueStartDateEdit || !m_issueEndDateEdit)
        return;
    auto dateMs = [](QDateEdit *edit, QCheckBox *enable) -> qint64 {
        if (!enable || !enable->isChecked())
            return 0;
        return edit->date().startOfDay().toMSecsSinceEpoch();
    };
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setDates(m_currentIssueNumber,
                        dateMs(m_issueStartDateEdit, m_issueStartDateEnable),
                        dateMs(m_issueEndDateEdit, m_issueEndDateEnable),
                        &error)) {
        setIssueInlineNotice(error.isEmpty() ? "Could not update dates." : error,
                             true);
        return;
    }
    setIssueInlineNotice("Dates updated.");
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



    const int kHighest = 1;
    const int kLowest = 99;
    const int step = qRound((kLowest - kHighest) * 0.25);
    int original = 0;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.number == m_currentIssueNumber) {
            original = issue.priority;
            break;
        }


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
        return true;
    }
    if (m_issueProgressDragRow < 0)
        return false;
    if (ev->type() == QEvent::MouseMove) {
        if (!(ev->buttons() & Qt::LeftButton))
            return false;
        applyIssueProgressDragAt(ev->pos());
        return true;
    }

    commitIssueProgressDrag();
    m_issueProgressDragRow = -1;
    return true;
}



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


    int chars = issue.title.size();
    for (const IssueEvent &ev : issue.events) {
        if (ev.type == QLatin1String("open")) {
            chars += ev.body.size();
            break;
        }
    }



    const double specTokens = chars / 4.0;
    const double inputTokens = 12000.0 + specTokens * 3.0;
    const double outputTokens = 3000.0 + specTokens * 2.0;
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

    if (issue.status == QLatin1String("closed") ||
        mergedIssues.contains(issue.number))
        return 100;


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


    QSet<int> mergedIssues;
    for (const PullRequest &pr : pullStoreForCurrentRepo().loadAll())
        if (pr.status == QLatin1String("merged"))
            for (const int number : issuesLinkedFromPull(pr))
                mergedIssues.insert(number);



    QList<Issue> todo;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.status != QLatin1String("closed") && issue.priority == 0)
            todo.append(issue);


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

    if (readme.size() > 8000)
        readme = readme.left(8000) + QStringLiteral("\n\n[README truncated]");



    const QString provider = m_issuePrioritizeAgentCombo
                                 ? m_issuePrioritizeAgentCombo->currentData()
                                       .toString()
                                 : defaultAgentProvider();
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
        setIssueInlineNotice(
            claudeCode
                ? "Sign in to Claude Code first (run `claude` and log in)."
                : claude ? "Add a Claude API key in Settings first."
                         : "Add an OpenAI API key in Settings first.",
            true);
        return;
    }


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



    if (!issueStoreForCurrentRepo().canWrite()) {
        setIssueInlineNotice("This repo is read-only here; can't update issues.",
                             true);
        return;
    }


    QList<Issue> open;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.status != QLatin1String("closed"))
            open.append(issue);
    if (open.isEmpty()) {
        setIssueInlineNotice("No open issues to analyze.");
        return;
    }



    QString readme = currentRepoReadme().trimmed();
    if (readme.size() > 8000)
        readme = readme.left(8000) + QStringLiteral("\n\n[README truncated]");




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



    const QString provider = m_issuePrioritizeAgentCombo
                                 ? m_issuePrioritizeAgentCombo->currentData()
                                       .toString()
                                 : defaultAgentProvider();
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
        setIssueInlineNotice(
            claudeCode
                ? "Sign in to Claude Code first (run `claude` and log in)."
                : claude ? "Add a Claude API key in Settings first."
                         : "Add an OpenAI API key in Settings first.",
            true);
        return;
    }


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


    const QString model =
        claude ? QStringLiteral("claude-opus-4-8") : kIssueAskAiModel;

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



        QHash<int, Issue> openByNumber;
        for (const Issue &issue : std::as_const(m_currentIssues))
            if (issue.status != QLatin1String("closed"))
                openByNumber.insert(issue.number, issue);



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


            const QString rating = o.value("rating").toString().toLower();
            QString label;
            if (rating.contains(QLatin1String("incomplete")))
                label = QStringLiteral("Incomplete");
            else if (rating.contains(QLatin1String("complete")))
                label = QStringLiteral("Complete");
            else if (rating.contains(QLatin1String("partial")))
                label = QStringLiteral("Partial");
            const int pct = qBound(0, o.value("completeness").toInt(), 100);


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



    const QStringList current = m_issueAssigneesEdit
                                    ? splitIssueFieldList(m_issueAssigneesEdit->text())
                                    : QStringList();
    QSet<QString> selected(current.begin(), current.end());



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



    const QSet<QString> before(current.begin(), current.end());
    if (selected == before || !m_issueAssigneesEdit)
        return;


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
    if (m_issueDatesStack)
        m_issueDatesStack->setCurrentIndex(0);
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

QUrl MainWindow::sharesApiUrl(const RepositoryRecord &repo) const
{

    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/shares");
    return url;
}

void MainWindow::shareRepoRequest(const RepositoryRecord &repo,
                                  const QString &grantee, const QString &action)
{




    if (!m_networkAccess || !m_profileIdentity.isValid() ||
        !hasOwnerSigningCapability(repo.owner))
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
            [this, reply, repo, grantee, action] {
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




                requestPrivateRecipientBundles(
                    repo,  true,
                    [this, repo, grantee, action](
                        bool recipientsReady,
                        QList<QJsonObject> recipientBundles,
                        QStringList, QString recipientError) {
                        if (!recipientsReady) {
                            flashMessage(
                                QStringLiteral(
                                    "The access list changed, but the encrypted "
                                    "replica was not rotated: %1")
                                    .arg(recipientError),
                                true);
                            return;
                        }
                        resealPrivateRepositoryRecipients(
                            repo, recipientBundles,
                            [this, grantee, action](bool rotated,
                                                   QString rotateError) {
                                if (!rotated) {
                                    flashMessage(
                                        QStringLiteral(
                                            "The access list changed, but the "
                                            "encrypted replica was not "
                                            "republished: %1")
                                            .arg(rotateError),
                                        true);
                                    return;
                                }
                                const QString verb =
                                    action == QLatin1String("add")
                                        ? QStringLiteral("Added")
                                        : QStringLiteral("Removed");
                                logSystem(
                                    QStringLiteral("%1 collaborator %2 and "
                                                   "rotated the encrypted "
                                                   "replica.")
                                        .arg(verb, grantee));
                                flashMessage(
                                    QStringLiteral(
                                        "%1 collaborator %2; private mirror "
                                        "keys rotated.")
                                        .arg(verb, grantee));
                            });
                    });
            });
}

void MainWindow::requestPrivateRecipientBundles(
    const RepositoryRecord &repo, bool updateCollaboratorList,
    std::function<void(bool, QList<QJsonObject>, QStringList, QString)> onDone)
{
    auto finish =
        [onDone = std::move(onDone)](
            bool ok, QList<QJsonObject> bundles = {},
            QStringList grantees = {}, const QString &error = QString()) mutable {
            if (onDone)
                onDone(ok, std::move(bundles), std::move(grantees), error);
        };
    if (!repo.isPrivate || !m_networkAccess ||
        !m_profileIdentity.isValid() ||
        !hasOwnerSigningCapability(repo.owner)) {
        finish(false, {}, {},
               QStringLiteral("the owner recipient list is unavailable"));
        return;
    }

    const QString ts =
        QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-shares-list-v1\n" + repo.owner + "\n" + repo.name +
         "\n" + ts)
            .toUtf8();
    QUrl url = sharesApiUrl(repo);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ts"), ts);
    query.addQueryItem(QStringLiteral("sig"),
                       m_profileIdentity.signData(canonical));
    url.setQuery(query);
    QNetworkReply *reply =
        m_networkAccess->get(QNetworkRequest(url));
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, repo, updateCollaboratorList,
         finish = std::move(finish)]() mutable {
            const QByteArray body = reply->readAll();
            const int status =
                reply->attribute(
                         QNetworkRequest::HttpStatusCodeAttribute)
                    .toInt();
            const auto networkError = reply->error();
            reply->deleteLater();
            const QJsonObject object =
                QJsonDocument::fromJson(body).object();
            const QJsonArray granteeValues =
                object.value(QStringLiteral("grantees")).toArray();
            QStringList grantees;
            QSet<QString> granteeSet;
            bool granteesValid = true;
            for (const QJsonValue &value : granteeValues) {
                const QString grantee =
                    value.toString().trimmed().toLower();
                if (grantee.isEmpty() ||
                    grantee.contains(QLatin1Char('/')) ||
                    granteeSet.contains(grantee)) {
                    granteesValid = false;
                    break;
                }
                granteeSet.insert(grantee);
                grantees.append(grantee);
            }

            const bool showingSameRepository =
                m_repoDetailIndex >= 0 &&
                m_repoDetailIndex < m_repositories.size() &&
                m_repositories.at(m_repoDetailIndex).owner == repo.owner &&
                m_repositories.at(m_repoDetailIndex).name == repo.name;
            if (updateCollaboratorList && showingSameRepository &&
                m_collabList) {
                m_collabList->clear();
                for (const QString &grantee : std::as_const(grantees))
                    m_collabList->addItem(grantee);
                if (m_collabEmptyHint)
                    m_collabEmptyHint->setVisible(grantees.isEmpty());
            }

            const QJsonArray recipientValues =
                object.value(QStringLiteral("recipients")).toArray();
            QList<QJsonObject> bundles;
            QSet<QString> recipientGrantees;
            QSet<QString> keyIds;
            bool recipientsValid =
                recipientValues.size() == grantees.size();
            for (const QJsonValue &value : recipientValues) {
                const QJsonObject recipient = value.toObject();
                const QString grantee =
                    recipient.value(QStringLiteral("grantee"))
                        .toString()
                        .trimmed()
                        .toLower();
                const QString keyId =
                    recipient.value(QStringLiteral("keyId"))
                        .toString();
                const QJsonObject publicBundle =
                    recipient.value(QStringLiteral("publicBundle"))
                        .toObject();
                const QString computedKeyId =
                    MirrorCrypto::publicKeyId(publicBundle);
                if (grantee.isEmpty() ||
                    !granteeSet.contains(grantee) ||
                    recipientGrantees.contains(grantee) ||
                    keyId.isEmpty() || keyIds.contains(keyId) ||
                    computedKeyId != keyId) {
                    recipientsValid = false;
                    break;
                }
                recipientGrantees.insert(grantee);
                keyIds.insert(keyId);
                bundles.append(publicBundle);
            }
            const bool ready =
                networkError == QNetworkReply::NoError &&
                status >= 200 && status < 300 &&
                object.value(QStringLiteral("ok")).toBool() &&
                !object.value(QStringLiteral("privateKeysStored"))
                     .toBool(true) &&
                object.value(QStringLiteral("encryptionReady"))
                    .toBool(false) &&
                object.value(QStringLiteral("missingEncryptionKeys"))
                    .toArray()
                    .isEmpty() &&
                granteesValid && recipientsValid &&
                recipientGrantees == granteeSet;
            if (!ready) {
                finish(
                    false, {}, grantees,
                    object
                            .value(QStringLiteral(
                                "missingEncryptionKeys"))
                            .toArray()
                            .isEmpty()
                        ? QStringLiteral(
                              "the public-only recipient keys could not be "
                              "verified")
                        : QStringLiteral(
                              "a collaborator must sign in with the desktop "
                              "client once to register their encryption key"));
                return;
            }
            finish(true, bundles, grantees, QString());
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


    const RepositoryRecord blank;
    const RepositoryRecord &repo =
        haveRepo ? m_repositories.at(m_repoDetailIndex) : blank;
    const bool show = haveRepo && !accountOwner().isEmpty() &&
                      repo.owner == accountOwner() && repo.isPrivate &&
                      repo.publishToNetwork &&
                      hasOwnerSigningCapability(repo.owner);
    m_collabSection->setVisible(show);
    if (m_collabList)
        m_collabList->clear();
    if (!show || !m_networkAccess || !m_profileIdentity.isValid())
        return;
    requestPrivateRecipientBundles(
        repo,  true,
        [this](bool ready, QList<QJsonObject>, QStringList grantees,
               QString) {
            if (!m_collabEmptyHint)
                return;
            m_collabEmptyHint->setVisible(grantees.isEmpty());
            if (!ready && !grantees.isEmpty())
                m_collabEmptyHint->setText(
                    QStringLiteral(
                        "A collaborator encryption key is not ready; private "
                        "mirror publication remains blocked."));
            else
                m_collabEmptyHint->setText(
                    QStringLiteral("No collaborators yet."));
        });
}

void MainWindow::showBountyQrDialog(const RepositoryRecord &repo, int number,
                                    const QString &uri, const QString &address,
                                    double amountUsd, const QString &amountSol,
                                    const QString &kind, const QString &payee)
{



    QMessageBox::information(
        this, QStringLiteral("Legacy bounty funding disabled"),
        QStringLiteral(
            "No funding request was created. ForkMesh has retired Worker-held "
            "bounty escrow; historical bounty addresses are migration-only and "
            "must not receive new funds. A future reward must use explicit "
            "approval in an external self-custodial wallet or a separately "
            "reviewed program/multisig flow."));
    return;

#if 0








































































































































































































































































#endif
}

void MainWindow::showBountyWalletDialog()
{



    QMessageBox::information(
        this, QStringLiteral("Legacy bounty wallet disabled"),
        QStringLiteral(
            "ForkMesh no longer creates, funds, or spends from Worker-held "
            "bounty wallets. Do not send new funds to a legacy address.\n\n"
            "No replacement issue or pull-request escrow is active. A future "
            "reward flow must use a direct external-wallet approval or reviewed "
            "program/multisig contract. The separate community reward pool is "
            "managed from Control node: its imported signer remains encrypted "
            "on the first-instance owner's device and every transfer requires "
            "explicit local review.\n\n"
            "An operator must use the documented legacy-custody migration "
            "procedure to inventory and recover any historical balance."));
}

void MainWindow::editIssueBounty()
{
    if (m_currentIssueNumber < 0)
        return;
    setIssueInlineNotice(
        QStringLiteral(
            "New issue bounties are disabled. The former funding flow depended "
            "on Worker-held escrow keys; historical entries are read-only and "
            "must be handled through the operator migration procedure."),
        true);
}

void MainWindow::bountyAllOpenIssues(double amountUsd)
{
    Q_UNUSED(amountUsd);
    setIssueInlineNotice(
        QStringLiteral(
            "Bulk bounties are disabled because the former payout path used "
            "Worker-held escrow keys. No pledge or transfer was created."),
        true);
}

namespace {




QJsonArray remoteAttachmentDataJson(const QList<RemoteAttachment> &attachments)
{
    QJsonArray array;
    for (const RemoteAttachment &att : attachments)
        array.append(QJsonObject{{"name", att.name},
                                 {"data", QString::fromLatin1(att.data.toBase64())}});
    return array;
}
}

void MainWindow::submitIssueCommentToInbox(const QString &body,
                                           const QStringList &attachmentSrcPaths,
                                           const QStringList &attachmentPlaceholders)
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);

    const QList<RemoteAttachment> attachmentData =
        IssueStore::readAttachmentsForRemoteSubmit(attachmentSrcPaths);
    QStringList attachmentNames;
    for (const RemoteAttachment &att : attachmentData)
        attachmentNames << att.name;

    QString text = IssueStore::substituteAttachmentPlaceholders(
        body, attachmentSrcPaths, attachmentPlaceholders, attachmentNames);
    while (text.endsWith('\n') || text.endsWith('\r'))
        text.chop(1);

    IssueStore store = issueStoreForCurrentRepo();
    IssueEvent ev;
    ev.type = "comment";
    ev.body = text;
    ev.attachments = attachmentNames;
    ev = store.makeSignedEvent(m_currentIssueNumber, ev);

    QJsonObject eventJson = ev.toJson();
    eventJson.insert("body", ev.body);
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", m_currentIssueNumber},
                              {"event", eventJson},
                              {"attachmentData", remoteAttachmentDataJson(attachmentData)}};

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
    const QStringList &attachmentSrcPaths, const QStringList &attachmentPlaceholders,
    std::function<void(bool ok, const QString &error)> onDone)
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return false;
    const RepositoryRecord &repo = m_repositories.at(idx);




    int proposed = 1;
    for (const Issue &issue : std::as_const(m_currentIssues))
        if (issue.number >= proposed)
            proposed = issue.number + 1;

    const QList<RemoteAttachment> attachmentData =
        IssueStore::readAttachmentsForRemoteSubmit(attachmentSrcPaths);
    QStringList attachmentNames;
    for (const RemoteAttachment &att : attachmentData)
        attachmentNames << att.name;

    IssueStore store = issueStoreForCurrentRepo();
    IssueEvent ev;
    ev.type = "open";
    ev.id = QStringLiteral("open-%1").arg(proposed);
    ev.title = title;
    ev.attachments = attachmentNames;
    ev.body = IssueStore::substituteAttachmentPlaceholders(
        body, attachmentSrcPaths, attachmentPlaceholders, attachmentNames);
    while (ev.body.endsWith('\n') || ev.body.endsWith('\r'))
        ev.body.chop(1);
    ev = store.makeSignedEvent(proposed, ev);

    QJsonObject eventJson = ev.toJson();
    eventJson.insert("body", ev.body);


    QJsonObject meta{{"labels", QJsonArray::fromStringList(labels)},
                     {"milestone", milestone},
                     {"priority", priority},
                     {"assignees", QJsonArray::fromStringList(assignees)}};
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", proposed},
                              {"titleIfNew", title},
                              {"event", eventJson},
                              {"meta", meta},
                              {"attachmentData", remoteAttachmentDataJson(attachmentData)}};

    QNetworkRequest request(issuesApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, onDone] {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError;





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
    const int earned = int((m_totalConnectionMs + live) / 3600000);
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

        submitIssueVoteToInbox();
        setIssueInlineNotice("Your signed vote was sent to the maintainer's inbox.");
    }




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
    drainIssuesInboxFor(m_repositories.at(idx),  true);
}

void MainWindow::drainIssuesInboxFor(RepositoryRecord repo, bool interactive)
{






    const RepositoryRecord writable = writableRecordFor(repo);
    bool ownerIntake = false;
    {
        IssueStore probe(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                         m_userName);
        ownerIntake = probe.canWrite();
    }
    const QString mirrorPath = repo.mirrorPath.trimmed();
    const bool mirrorIntake =
        !ownerIntake && !repo.previewOnly && !repo.isPrivate &&
        repo.publishToNetwork && !mirrorPath.isEmpty() &&
        QDir(mirrorPath).exists();
    if (!ownerIntake && !mirrorIntake)
        return;
    if (!hasOwnerSigningCapability())
        return;




    const QString signer = mirrorIntake
        ? accountOwner().trimmed().toLower()
        : repoSegment(repo.owner, QStringLiteral("owner"));
    if (signer.isEmpty() || !hasOwnerSigningCapability(signer))
        return;

    QUrl url = issuesApiUrl(repo);


    const QString intakeKey =
        repo.owner.trimmed().toLower() + QLatin1Char('/') +
        repo.name.trimmed().toLower();
    const QString backoffKey =
        url.toString() +
        (mirrorIntake ? QStringLiteral("|mirror:") + signer : QString());
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!interactive && !m_pollBackoff.ready(backoffKey, nowMs))
        return;
    if (mirrorIntake) {
        if (m_mirrorIssueIntakeInFlight.contains(intakeKey))
            return;
        m_mirrorIssueIntakeInFlight.insert(intakeKey);
    }

    const QString ts = QString::number(nowMs);
    const QByteArray canonical =
        ("forkmesh-issues-pull-v1\n" + signer + "\n" + ts).toUtf8();
    const QString sig = m_profileIdentity.signData(canonical);

    QUrlQuery query;
    query.addQueryItem("owner", signer);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", sig);
    if (mirrorIntake)
        query.addQueryItem(QStringLiteral("mirror"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, repo, interactive, backoffKey, intakeKey,
             mirrorIntake, signer] {
        reply->deleteLater();
        if (mirrorIntake)
            m_mirrorIssueIntakeInFlight.remove(intakeKey);
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
        const QJsonArray pending =
            QJsonDocument::fromJson(reply->readAll())
                .object()
                .value("pending")
                .toArray();
        if (!mirrorIntake) {
            applyIssuesInboxPayload(repo, pending, interactive);
            return;
        }
        if (pending.isEmpty()) {
            if (interactive)
                setIssueInlineNotice("No pending submissions.");
            return;
        }





        QTemporaryDir worktree(
            QDir::tempPath() + QStringLiteral(
                "/forkmesh-mirror-issues-XXXXXX"));
        const QString branch = mirrorHeadBranch(repo.mirrorPath);
        QString gitError;
        if (!worktree.isValid() || branch.isEmpty() ||
            !runGitCapture(
                repo.mirrorPath,
                {QStringLiteral("worktree"), QStringLiteral("add"),
                 QStringLiteral("--force"), worktree.path(), branch},
                nullptr, &gitError)) {
            m_pollBackoff.noteFailure(
                backoffKey, QDateTime::currentMSecsSinceEpoch());
            logSystem(
                QStringLiteral("Mirror issue intake could not open %1/%2: %3")
                    .arg(repo.owner, repo.name,
                         gitError.trimmed().right(240)));
            if (interactive)
                setIssueInlineNotice(
                    "Could not prepare the mirror issue worktree.", true);
            return;
        }
        const QString author =
            chatDisplayName().trimmed().isEmpty()
                ? signer
                : chatDisplayName().trimmed();
        runGitCapture(worktree.path(),
                      {QStringLiteral("config"), QStringLiteral("user.name"),
                       author.left(80)},
                      nullptr, nullptr);
        runGitCapture(
            worktree.path(),
            {QStringLiteral("config"), QStringLiteral("user.email"),
             signer.left(63) +
                 QStringLiteral("@users.noreply.forkmesh.com")},
            nullptr, nullptr);

        RepositoryRecord materialized = repo;
        materialized.localPath = worktree.path();
        applyIssuesInboxPayload(
            materialized, pending, interactive,  true);
        runGitCapture(
            repo.mirrorPath,
            {QStringLiteral("worktree"), QStringLiteral("remove"),
             QStringLiteral("--force"), worktree.path()},
            nullptr, nullptr);
        runGitCapture(repo.mirrorPath,
                      {QStringLiteral("worktree"), QStringLiteral("prune")},
                      nullptr, nullptr);
    });
}

void MainWindow::pollMirrorIssueInboxes()
{
    if (!m_networkAccess || !hasOwnerSigningCapability(accountOwner()))
        return;
    QSet<QString> seen;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly || repo.isPrivate || !repo.publishToNetwork ||
            repo.mirrorPath.trimmed().isEmpty() ||
            !QDir(repo.mirrorPath).exists())
            continue;
        const RepositoryRecord writable = writableRecordFor(repo);
        IssueStore probe(writable.localPath, writable.mirrorPath,
                         &m_profileIdentity, m_userName);
        if (probe.canWrite())
            continue;
        const QString key =
            repo.owner.trimmed().toLower() + QLatin1Char('/') +
            repo.name.trimmed().toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        drainIssuesInboxFor(repo,  false);
        drainPullsInboxFor(repo,  false);
        drainDiscussionsInboxFor(repo,  false);
    }
}




void MainWindow::applyIssuesInboxPayload(const RepositoryRecord &repo,
                                         const QJsonArray &pending,
                                         bool interactive,
                                         bool mirrorIntake)
{
    if (!hasOwnerSigningCapability())
        return;
    if (pending.isEmpty()) {
        if (interactive)
            setIssueInlineNotice("No pending submissions.");
        return;
    }
    const RepositoryRecord writable = writableRecordFor(repo);
    IssueStore store(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                     m_userName);
    if (!store.canWrite())
        return;
    int merged = 0;
    int comments = 0;
    int newIssues = 0;
    QString lastCommentAuthor;
    int lastCommentNumber = 0;
    QString lastCommentBody;
    QString lastIssueAuthor;
    QString lastIssueTitle;
    int lastIssueNumber = 0;





    struct AgentRequest {
        IssueEvent event;
        QString model;
        QString provider;
    };
    QList<AgentRequest> agentRequests;




    struct CommentAgentRequest {
        int number = 0;
        QString model;
        QString provider;
    };
    QList<CommentAgentRequest> commentAgentRequests;



    struct FediverseMaterialization {
        QString mentionId;
        QString eventId;
        QString inboxId;
    };
    QList<FediverseMaterialization> fediverseMaterializations;




    QStringList drainedIds;
    for (const QJsonValue &value : pending) {
        const QJsonObject item = value.toObject();
        const QJsonValue idVal = item.value("id");
        const QString inboxId =
            idVal.isDouble()
                ? QString::number(static_cast<qint64>(idVal.toDouble()))
                : QString();
        const int number = item.value("number").toInt();
        const QJsonObject eventObj = item.value("event").toObject();
        IssueEvent ev = IssueEvent::fromJson(eventObj);
        ev.body = eventObj.value("body").toString();
        const QString mentionId =
            item.value("fediverseMentionId").toString().toLower();
        const bool validMentionId =
            ev.type == QLatin1String("open") && !ev.id.isEmpty() &&
            mentionId.size() == 32 &&
            std::all_of(
                mentionId.cbegin(), mentionId.cend(), [](QChar ch) {
                    return ch.isDigit() ||
                           (ch >= QLatin1Char('a') &&
                            ch <= QLatin1Char('f'));
                });
        if (validMentionId) {




            fediverseMaterializations.append(
                FediverseMaterialization{mentionId, ev.id, inboxId});
        }
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



        QList<RemoteAttachment> attachmentData;
        for (const QJsonValue &a : item.value("attachmentData").toArray()) {
            const QJsonObject obj = a.toObject();
            attachmentData.append(RemoteAttachment{
                obj.value("name").toString(),
                QByteArray::fromBase64(obj.value("data").toString().toLatin1())});
        }
        if (store.applyRemoteEvent(number, ev, titleIfNew, nullptr, meta,
                                   attachmentData)) {









            const bool ownerOnlyRow =
                validMentionId || (mirrorIntake && meta.wantsAgent);
            if (!ownerOnlyRow && !inboxId.isEmpty() &&
                !drainedIds.contains(inboxId))
                drainedIds << inboxId;
            ++merged;
            const QString who =
                ev.authorName.isEmpty() ? ev.author.left(8) : ev.authorName;
            if (ev.type == QLatin1String("comment")) {
                ++comments;
                lastCommentAuthor = who;
                lastCommentNumber = number;
                lastCommentBody = ev.body.simplified();
                if (meta.wantsAgent && number > 0)
                    commentAgentRequests << CommentAgentRequest{
                        number, meta.wantsAgentModel, meta.wantsAgentProvider};
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
    QStringList materialized;
    if (!fediverseMaterializations.isEmpty()) {
        const QList<Issue> mergedIssues = store.loadAll();
        for (const FediverseMaterialization &wanted :
             std::as_const(fediverseMaterializations)) {
            for (const Issue &candidate : mergedIssues) {
                const bool found = std::any_of(
                    candidate.events.cbegin(),
                    candidate.events.cend(),
                    [&wanted](const IssueEvent &event) {
                        return event.type == QLatin1String("open") &&
                               event.id == wanted.eventId;
                    });
                if (!found)
                    continue;
                materialized.append(
                    wanted.mentionId + QLatin1Char(':') +
                    QString::number(candidate.number));
                if (!wanted.inboxId.isEmpty() &&
                    !drainedIds.contains(wanted.inboxId))
                    drainedIds << wanted.inboxId;
                break;
            }
        }
    }




    if (!drainedIds.isEmpty()) {
        QUrl ackUrl = issuesApiUrl(repo);
        const QString ackSigner = mirrorIntake
            ? accountOwner().trimmed().toLower()
            : repoSegment(repo.owner, QStringLiteral("owner"));
        QUrlQuery ackQuery = signedInboxQuery(ackSigner);
        if (mirrorIntake) {
            ackQuery.addQueryItem(
                QStringLiteral("mirror"), QStringLiteral("1"));
            appendMirrorStateAttestation(&ackQuery, repo, ackSigner);
        }
        ackQuery.addQueryItem("ids", drainedIds.join(QStringLiteral(",")));
        if (!mirrorIntake && !materialized.isEmpty())
            ackQuery.addQueryItem(
                QStringLiteral("materialized"),
                materialized.join(QStringLiteral(",")));
        ackUrl.setQuery(ackQuery);
        m_networkAccess->deleteResource(QNetworkRequest(ackUrl));
    }

    const int curIdx = issuesRepoIndex();
    if (curIdx >= 0 &&
        m_repositories.at(curIdx).owner == repo.owner &&
        m_repositories.at(curIdx).name == repo.name)
        reloadIssues();





    if (!mirrorIntake &&
        (!agentRequests.isEmpty() || !commentAgentRequests.isEmpty())) {
        const QList<Issue> mergedIssues = store.loadAll();



        const auto issueHasAgent = [](const Issue &issue) {
            for (const IssueEvent &e : issue.events) {
                if (e.type == QLatin1String("agent") && e.agentSessionId > 0)
                    return true;
            }
            return false;
        };
        for (const auto &request : std::as_const(commentAgentRequests)) {
            const QString provider = request.provider.trimmed().isEmpty()
                                         ? defaultAgentProvider()
                                         : request.provider.trimmed();
            for (const Issue &candidate : mergedIssues) {
                if (candidate.number != request.number || candidate.isDeleted())
                    continue;
                if (!issueHasAgent(candidate))
                    startAgentForIssue(candidate, provider,
                                        true,  true,
                                       request.model, &repo);
                break;
            }
        }
        for (const auto &request : std::as_const(agentRequests)) {
            const IssueEvent &wanted = request.event;
            const QString &wantedModel = request.model;


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
                    if (!issueHasAgent(candidate))
                        startAgentForIssue(candidate, wantedProvider,
                                            true,  true,
                                           wantedModel, &repo);
                    break;
                }
            }
        }
    }


    if (merged > 0) {
        propagateRepoUpdate(repoIndexFor(repo.owner, repo.name));

        if (!mirrorIntake)
            scanRepoMentionsFor(writable);
        else {
            m_mirrorAdvertSig.clear();
            refreshMirrorAdverts();
        }
    }
    if (interactive)
        setIssueInlineNotice(
            mirrorIntake
                ? QStringLiteral(
                      "Merged %1 submission(s) into this mirror.")
                      .arg(merged)
                : QStringLiteral(
                      "Merged %1 submission(s) into .forkmesh/issues/.")
                      .arg(merged));



    if (!mirrorIntake && newIssues > 0) {
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
    if (!mirrorIntake && comments > 0) {
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
}

QWidget *MainWindow::buildChatSection()
{
    auto *page = new QWidget;


    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(280);



    m_statusLine = new QLabel(sidebar);
    m_statusLine->setObjectName("statusLine");
    m_statusLine->setWordWrap(true);
    m_statusLine->hide();

    m_channelList = new QListWidget;
    m_channelList->setMinimumHeight(0);
    m_channelList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_channelList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_channelList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    auto *addChannelButton = new QPushButton("+ Add chat");
    addChannelButton->setObjectName("ghostButton");
    addChannelButton->setCursor(Qt::PointingHandCursor);

    auto *dmsLabel = new QLabel("DIRECT MESSAGES");
    dmsLabel->setObjectName("sectionLabel");
    m_dmList = new QListWidget;
    m_dmList->setMinimumHeight(0);
    m_dmList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_dmList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_dmList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);



    auto *roomsScroll = new QScrollArea;
    roomsScroll->setObjectName("messageView");
    roomsScroll->setWidgetResizable(true);
    roomsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    roomsScroll->setFrameShape(QFrame::NoFrame);
    roomsScroll->setMinimumHeight(0);
    roomsScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    auto *roomsContainer = new QWidget;
    auto *roomsLayout = new QVBoxLayout(roomsContainer);
    roomsLayout->setContentsMargins(0, 0, 0, 0);
    roomsLayout->setSpacing(6);
    roomsLayout->addWidget(m_channelList, 2);
    roomsLayout->addWidget(addChannelButton);
    roomsLayout->addWidget(dmsLabel);
    roomsLayout->addWidget(m_dmList, 1);
    roomsLayout->addStretch();
    roomsScroll->setWidget(roomsContainer);

    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(14, 16, 14, 12);
    sidebarLayout->setSpacing(6);
    sidebarLayout->addWidget(roomsScroll, 1);


    auto *header = new QWidget;
    header->setObjectName("chatHeader");
    m_channelTitle = new QLabel("#general");
    m_channelTitle->setObjectName("channelTitle");


    m_encryptionLabel = new QLabel;
    m_encryptionLabel->setObjectName("encryptionLabel");
    m_encryptionLabel->setPixmap(
        tintedOcticonPixmap("lock", QColor("#8b949e"), 16));
    m_encryptionLabel->setToolTip(
        "Authenticated shared-key encryption; the relay can read default rooms");


    m_inviteButton = new QPushButton(QStringLiteral("Invite"));
    m_inviteButton->setObjectName("ghostButton");
    m_inviteButton->setCursor(Qt::PointingHandCursor);
    m_inviteButton->setToolTip(QStringLiteral("Invite a member to this private room"));
    m_inviteButton->hide();
    connect(m_inviteButton, &QPushButton::clicked, this,
            &MainWindow::promptInviteToPrivateChannel);
    m_chatMembersButton = new QPushButton(QStringLiteral("0"));
    m_chatMembersButton->setObjectName("ghostButton");
    m_chatMembersButton->setProperty("buttonSize", "sm");
    m_chatMembersButton->setCursor(Qt::PointingHandCursor);
    m_chatMembersButton->setToolTip(
        QStringLiteral("Show users in this room"));
    setOcticon(m_chatMembersButton, "people", 15);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 12, 18, 12);
    headerLayout->addWidget(m_channelTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(m_inviteButton);
    headerLayout->addWidget(m_chatMembersButton);
    headerLayout->addWidget(m_encryptionLabel);



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





    m_chatUnreadBanner = new QWidget;
    m_chatUnreadBanner->setObjectName("chatUnreadBanner");
    m_chatUnreadBannerLabel = new QLabel;
    auto *unreadReadButton = new QPushButton(QStringLiteral("Mark all read"));
    unreadReadButton->setObjectName("chatUnreadBannerButton");
    unreadReadButton->setCursor(Qt::PointingHandCursor);
    unreadReadButton->setToolTip(
        QStringLiteral("Mark every conversation read and jump to the newest messages"));
    setOcticon(unreadReadButton, "chevron-up", 16);
    auto *unreadLayout = new QHBoxLayout(m_chatUnreadBanner);
    unreadLayout->setContentsMargins(18, 6, 12, 6);
    unreadLayout->setSpacing(10);
    unreadLayout->addWidget(m_chatUnreadBannerLabel, 1);
    unreadLayout->addWidget(unreadReadButton);
    m_chatUnreadBanner->hide();
    connect(unreadReadButton, &QPushButton::clicked, this, &MainWindow::markAllChatRead);



    m_messageScroll = new QScrollArea;
    m_messageScroll->setObjectName("messageView");
    m_messageScroll->setWidgetResizable(true);
    m_messageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageScroll->setMinimumHeight(0);
    m_messageScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_messageContainer = new QWidget;
    m_messageContainer->setObjectName("messageContainer");
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(4, 8, 4, 8);
    m_messageLayout->setSpacing(0);
    m_messageLayout->addStretch();
    m_messageScroll->setWidget(m_messageContainer);






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
    composer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);



    auto *inputRow = new QWidget;
    inputRow->setObjectName("composerInputRow");
    inputRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
    m_messageInput->setMinimumWidth(0);
    m_messageInput->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);


    m_messageInput->installEventFilter(this);





    m_mentionModel = new QStringListModel(this);
    m_mentionCompleter = new QCompleter(m_mentionModel, this);
    m_mentionCompleter->setWidget(m_messageInput);
    m_mentionCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_mentionCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_mentionCompleter->setFilterMode(Qt::MatchContains);
    connect(m_mentionCompleter,
            QOverload<const QString &>::of(&QCompleter::activated), this,
            &MainWindow::insertMention);









    QAbstractItemView *mentionPopup = m_mentionCompleter->popup();
    mentionPopup->installEventFilter(this);
    m_mentionCompleterPopup = mentionPopup;
    refreshMentionCandidates();

    auto *emojiButton = new QPushButton(QString::fromUtf8("\xF0\x9F\x99\x82"));
    emojiButton->setObjectName("iconButton");
    emojiButton->setCursor(Qt::PointingHandCursor);
    emojiButton->setToolTip("Insert emoji");
    connect(emojiButton, &QPushButton::clicked, this,
            [this, emojiButton] { showEmojiPicker(emojiButton); });
    auto *inputRowLayout = new QHBoxLayout(inputRow);
    inputRowLayout->setContentsMargins(6, 2, 6, 2);
    inputRowLayout->setSpacing(2);

    auto *selfAvatar = new QLabel("FM");
    selfAvatar->setObjectName("issueAvatar");
    selfAvatar->setAlignment(Qt::AlignCenter);
    selfAvatar->setFixedSize(24, 24);
    selfAvatar->setScaledContents(true);
    selfAvatar->setToolTip(topBarUserName());
    {
        const QPixmap selfPm = roundedAvatar(effectiveAvatar(), 24);
        if (!selfPm.isNull()) {
            selfAvatar->setText(QString());
            selfAvatar->setPixmap(selfPm);
        }
    }
    inputRowLayout->addWidget(selfAvatar, 0, Qt::AlignVCenter);
    inputRowLayout->addWidget(attachButton);
    inputRowLayout->addWidget(m_messageInput, 1);
    inputRowLayout->addWidget(emojiButton);
    auto *sendButton = new QPushButton("Send");
    sendButton->setObjectName("primaryButton");
    sendButton->setCursor(Qt::PointingHandCursor);
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(14, 10, 14, 12);
    composerLayout->setSpacing(8);


    composerLayout->addWidget(inputRow, 1);
    composerLayout->addWidget(sendButton);

    m_typingLabel = new QLabel;
    m_typingLabel->setObjectName("typingLabel");
    m_typingLabel->setFixedHeight(20);
    m_typingLabel->setText(QString());

    auto *mainColumnHost = new QWidget;
    mainColumnHost->setObjectName("chatMainColumn");
    mainColumnHost->setMinimumHeight(0);
    mainColumnHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    auto *mainColumn = new QVBoxLayout(mainColumnHost);
    mainColumn->setContentsMargins(0, 0, 0, 0);
    mainColumn->setSpacing(0);
    mainColumn->addWidget(header);
    mainColumn->addWidget(m_firewallBanner);
    mainColumn->addWidget(m_chatUnreadBanner);
    mainColumn->addWidget(m_messageScroll, 1);
    mainColumn->addWidget(m_typingLabel);
    mainColumn->addWidget(composer);



    auto *membersPanel = new QWidget;
    membersPanel->setObjectName("chatMembersPopup");
    membersPanel->setFixedWidth(270);
    m_chatMembersHeading =
        new QLabel("DATABASE USERS IN THIS ROOM \xE2\x80\x94 0");
    m_chatMembersHeading->setObjectName("sectionLabel");
    auto *membersScroll = new QScrollArea;
    membersScroll->setObjectName("messageView");
    membersScroll->setWidgetResizable(true);
    membersScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    membersScroll->setFrameShape(QFrame::NoFrame);
    membersScroll->setFixedHeight(460);
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
    auto *membersMenu = new QMenu(m_chatMembersButton);
    membersMenu->setObjectName(QStringLiteral("chatMembersMenu"));
    auto *membersAction = new QWidgetAction(membersMenu);
    membersAction->setDefaultWidget(membersPanel);
    membersMenu->addAction(membersAction);
    connect(m_chatMembersButton, &QPushButton::clicked, this,
            [membersMenu, button = m_chatMembersButton] {
                membersMenu->adjustSize();
                const QPoint below =
                    button->mapToGlobal(QPoint(button->width(), button->height()));
                membersMenu->popup(
                    QPoint(below.x() - membersMenu->sizeHint().width(), below.y()));
            });

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sidebar);
    layout->addWidget(mainColumnHost, 1);

    refreshChatMembers();
    refreshChatUserDirectory();

    connect(m_channelList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    switchConversation(item->data(Qt::UserRole).toString());
            });

    m_channelList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_channelList, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QListWidgetItem *item = m_channelList->itemAt(pos);
                if (!item)
                    return;
                const QString channel = item->data(Qt::UserRole).toString();
                QMenu menu(m_channelList);
                QAction *del = menu.addAction(QStringLiteral("Delete room"));
                if (menu.exec(m_channelList->viewport()->mapToGlobal(pos)) == del)
                    promptDeleteRoom(channel);
            });
    connect(m_dmList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    switchConversation(item->data(Qt::UserRole).toString());
            });


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


    connect(m_messageInput, &QLineEdit::cursorPositionChanged, this,
            [this] { updateMentionPopup(); });
    connect(m_messageInput, &QLineEdit::returnPressed, this, &MainWindow::sendCurrentMessage);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendCurrentMessage);

    return page;
}

void MainWindow::updateHomeStats()
{







    if (m_connectedAtMs > 0) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_uptimePersistedAtMs >= 5 * 60 * 1000) {
            m_uptimePersistedAtMs = now;
            const qint64 totalMs = m_totalConnectionMs + (now - m_connectedAtMs);
            QSettings().setValue(kConnectionTotalSetting, totalMs);
        }
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
