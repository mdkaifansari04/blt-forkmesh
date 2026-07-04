// MainWindowPulls: MainWindow feature methods, split out of MainWindow.cpp.
// Pull requests, including AI conflict auto-resolution.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"
#include "PullAiReview.h"

using namespace forkmesh::ui;

namespace {
// Item-data roles for the PR commits list (renderPullCommits). kCommitShaRole is
// the openable full SHA (the commit resolves in the local repo); kCommitCopyShaRole
// is a copy-only SHA for cross-node commits not present locally, so right-click can
// still copy them without making the row open a missing commit.
constexpr int kCommitShaRole = Qt::UserRole;
constexpr int kCommitMessageRole = Qt::UserRole + 1;
constexpr int kCommitCopyShaRole = Qt::UserRole + 2;
// File-list item role (issue #365): true when an agent-stamped commit touched the
// file, so the authorship filter can hide/show it without re-reading the mbox.
constexpr int kPullFileAgentRole = Qt::UserRole + 1;

// Locates a named HTML anchor (<a name="...">) inside a QTextDocument.
// QTextDocument::find only searches visible text, and an anchor carries none,
// so finding one means walking fragments and checking their char format for it
// directly. Used by applyAutoMarkViewedOnScroll to find each file's on-screen
// position without an anchor-to-position API.
bool locateAnchorCursor(QTextDocument *doc, const QString &name, QTextCursor &out)
{
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (frag.isValid() && frag.charFormat().isAnchor() &&
                frag.charFormat().anchorNames().contains(name)) {
                out = QTextCursor(doc);
                out.setPosition(frag.position());
                return true;
            }
        }
    }
    return false;
}
} // namespace

// ---- Pull requests ---------------------------------------------------------

QWidget *MainWindow::buildPullsTab()
{
    auto *page = new QWidget;

    // Left: toolbar + sortable PR table.
    auto *listPane = new QWidget;
    listPane->setMinimumWidth(360);
    auto *heading = new QLabel("Pull requests");
    heading->setObjectName("channelTitle");
    // "Hide detail" toggle (issue #274): collapse the detail panel so the PR
    // list spans the full tab width. Re-checking restores it for the open row.
    m_pullHideDetailButton = new QPushButton("Hide detail");
    m_pullHideDetailButton->setObjectName("ghostButton");
    m_pullHideDetailButton->setCursor(Qt::PointingHandCursor);
    m_pullHideDetailButton->setCheckable(true);
    m_pullHideDetailButton->setToolTip(
        "Hide the detail panel and show the pull-request list full width");
    setOcticon(m_pullHideDetailButton, "chevron-right", 16);
    connect(m_pullHideDetailButton, &QPushButton::toggled, this, [this](bool hidden) {
        m_pullDetailHidden = hidden;
        m_pullHideDetailButton->setText(hidden ? "Show detail" : "Hide detail");
        setOcticon(m_pullHideDetailButton, hidden ? "arrow-left" : "chevron-right", 16);
        if (hidden) {
            if (m_pullDetail)
                m_pullDetail->hide();
        } else if (m_pullDetail && m_currentPullNumber > 0) {
            m_pullDetail->show(); // reopen for the still-selected row
        }
    });
    auto *headingRow = new QHBoxLayout;
    headingRow->setContentsMargins(0, 0, 0, 0);
    headingRow->setSpacing(8);
    headingRow->addWidget(heading, 1);
    headingRow->addWidget(m_pullHideDetailButton, 0, Qt::AlignTop);
    m_pullNewButton = new QPushButton("New pull request");
    m_pullChooseDirButton = new QPushButton("Choose directory");
    m_pullImportButton = new QPushButton("Import patch");
    m_pullSyncButton = new QPushButton("Sync inbox");
    for (QPushButton *b : {m_pullNewButton, m_pullChooseDirButton, m_pullImportButton,
                           m_pullSyncButton}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_pullNewButton, "plus", 16);
    setOcticon(m_pullChooseDirButton, "file-directory", 16);
    setOcticon(m_pullImportButton, "download", 16);
    setOcticon(m_pullSyncButton, "sync", 16);
    m_pullChooseDirButton->setToolTip("Create a pull request from another local checkout of this repository");
    m_pullImportButton->setToolTip("Open a .patch/.diff file (e.g. a downloaded commit) as a pull request");
    m_pullSyncButton->setToolTip("Pull PR submissions filed by other nodes and merge them");
    connect(m_pullImportButton, &QPushButton::clicked, this,
            &MainWindow::importPatchAsPull);
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(m_pullNewButton);
    toolbar->addWidget(m_pullChooseDirButton);
    toolbar->addWidget(m_pullImportButton);
    toolbar->addWidget(m_pullSyncButton);
    toolbar->addStretch();

    m_pullSearch = new QLineEdit;
    m_pullSearch->setObjectName("issueSearch");
    m_pullSearch->setPlaceholderText("Search pull requests\xE2\x80\xA6");
    m_pullSearch->setClearButtonEnabled(true);

    m_pullTable = new QTableWidget(0, 10);
    m_pullTable->setObjectName("issueTable");
    installColumnHeaderMenu(m_pullTable); // 3-dots per-column menu (issue #318)
    enableHoverRowHighlight(m_pullTable);
    m_pullTable->setHorizontalHeaderLabels(
        {"#", "Title", "Base \xE2\x86\x90 Head", "Status", "Files", "\xC2\xB1",
         "Author", "Agent cost", "Created", "Modified"});
    m_pullTable->verticalHeader()->setVisible(false);
    m_pullTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pullTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pullTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pullTable->setShowGrid(false);
    m_pullTable->setWordWrap(false);
    m_pullTable->setSortingEnabled(true);
    QHeaderView *ph = m_pullTable->horizontalHeader();
    ph->setHighlightSections(false);
    ph->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ph->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c < m_pullTable->columnCount(); ++c)
        ph->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_pullTable);

    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(18, 18, 12, 18);
    listLayout->setSpacing(8);
    listLayout->addLayout(headingRow);
    listLayout->addLayout(toolbar);
    listLayout->addWidget(m_pullSearch);
    listLayout->addWidget(m_pullTable, 1);

    // Right: PR detail — header + changed-files explorer + diff viewer.
    m_pullDetail = new QWidget;
    m_pullTitle = new QLabel("Select a pull request");
    m_pullTitle->setObjectName("channelTitle");
    m_pullTitle->setWordWrap(true);
    m_pullUpdateButton = new QPushButton("Update branch");
    m_pullMergeButton = new QPushButton("Merge");
    m_pullResolveButton = new QPushButton("Resolve conflicts\xE2\x80\xA6");
    m_pullFixButton = new QPushButton("Fix with agent");
    m_pullFixConflictsButton = new QPushButton("Fix conflicts with agent");
    m_pullEditFileButton = new QPushButton("Edit file\xE2\x80\xA6");
    m_pullDeleteFileButton = new QPushButton("Delete file\xE2\x80\xA6");
    m_pullCloseButton = new QPushButton("Close");
    m_pullReopenButton = new QPushButton("Reopen");
    m_pullSendToSourceButton = new QPushButton("Send to source of truth");
    m_pullDeleteButton = new QPushButton("Delete");
    m_pullDeleteBranchButton = new QPushButton("Delete PR + branch");
    m_pullMergeDeleteButton = new QPushButton("Merge + delete branch");
    m_pullPreviewButton = new QPushButton("Build & preview");
    m_pullLinkIssueButton = new QPushButton("Link issue");
    m_pullSplitButton = new QPushButton;
    for (QPushButton *b : {m_pullUpdateButton, m_pullMergeButton, m_pullResolveButton,
                           m_pullFixButton, m_pullFixConflictsButton,
                           m_pullEditFileButton, m_pullDeleteFileButton,
                           m_pullCloseButton, m_pullReopenButton,
                           m_pullSendToSourceButton, m_pullDeleteButton,
                           m_pullDeleteBranchButton, m_pullMergeDeleteButton,
                           m_pullPreviewButton,
                           m_pullLinkIssueButton, m_pullSplitButton}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    // Unified <-> side-by-side toggle for this PR's diff (shared preference).
    m_pullSplitButton->setCheckable(true);
    m_pullSplitButton->setChecked(diffSplitPref());
    setOcticon(m_pullSplitButton, "diff", 16);
    updateDiffSplitButton(m_pullSplitButton);
    connect(m_pullSplitButton, &QPushButton::clicked, this, [this](bool on) {
        setDiffSplitPref(on);
        updateDiffSplitButton(m_pullSplitButton);
        updateDiffSplitButton(m_commitSplitButton);
        if (m_commitSplitButton)
            m_commitSplitButton->setChecked(on);
        if (m_pullFiles && m_pullFiles->count() > 0)
            renderPullDiff();
    });
    m_pullMergeButton->setObjectName("primaryButton");
    setOcticon(m_pullUpdateButton, "sync", 16);
    setOcticon(m_pullMergeButton, "check-circle", 16);
    setOcticon(m_pullResolveButton, "git-pull-request", 16);
    setOcticon(m_pullLinkIssueButton, "link", 16);
    m_pullLinkIssueButton->setToolTip("Link an issue to this pull request");
    connect(m_pullLinkIssueButton, &QPushButton::clicked, this,
            &MainWindow::linkIssueToPullFromPullPage);
    // Copy a forkmesh:// permalink to this PR (issue #154): pasted into a comment
    // it renders as a link back here via autolinkReferences().
    auto *pullCopyLinkButton = new QPushButton("Copy link");
    pullCopyLinkButton->setObjectName("ghostButton");
    pullCopyLinkButton->setProperty("buttonSize", "sm");
    pullCopyLinkButton->setCursor(Qt::PointingHandCursor);
    setOcticon(pullCopyLinkButton, "copy", 16);
    pullCopyLinkButton->setToolTip(
        "Copy a link to this pull request you can paste into an issue or PR comment");
    connect(pullCopyLinkButton, &QPushButton::clicked, this, [this] {
        if (m_currentPullNumber > 0)
            copyReferenceLink(QStringLiteral("pull"),
                              QString::number(m_currentPullNumber));
    });
    setOcticon(m_pullCloseButton, "circle-slash", 16);
    setOcticon(m_pullReopenButton, "issue-reopened", 16);
    m_pullReopenButton->setToolTip("Reopen this pull request");
    m_pullReopenButton->hide(); // only shown when the PR is closed or merged
    setOcticon(m_pullSendToSourceButton, "upload", 16);
    m_pullSendToSourceButton->setToolTip(
        "Deliver this pull request to the repository owner's inbox. The relay "
        "holds it, so it reaches the source of truth even while that node is "
        "offline.");
    m_pullSendToSourceButton->hide(); // only shown on mirror nodes (can't merge here)
    setOcticon(m_pullDeleteButton, "trash", 16);
    m_pullDeleteButton->setToolTip("Permanently delete this pull request");
    setOcticon(m_pullDeleteBranchButton, "trash", 16);
    m_pullDeleteBranchButton->setToolTip(
        "Permanently delete this pull request and its head branch");
    setOcticon(m_pullMergeDeleteButton, "check-circle", 16);
    m_pullMergeDeleteButton->setToolTip(
        "Merge this pull request, then permanently delete it and its head branch");
    setOcticon(m_pullPreviewButton, "device-desktop", 16);
    m_pullPreviewButton->setToolTip(
        "Check out this pull request, build the app from it, and launch the result "
        "as an isolated preview \xE2\x80\x94 try the change running before merging");
    m_pullPreviewButton->hide(); // only shown for buildable ForkMesh checkouts
    connect(m_pullPreviewButton, &QPushButton::clicked, this,
            &MainWindow::buildAndPreviewCurrentPull);
    m_pullUpdateButton->setToolTip("Merge the base branch into this pull request branch");
    m_pullResolveButton->setToolTip(
        "Open a merge editor to resolve this pull request's conflicts and commit "
        "the fix to its branch (the PR stays open, ready to merge)");
    m_pullResolveButton->hide(); // only shown when the PR has conflicts
    connect(m_pullResolveButton, &QPushButton::clicked, this,
            &MainWindow::resolveCurrentPullConflicts);
    // One-click AI conflict resolution: an agent rewrites the conflicting files
    // and the fix is committed straight to the PR's branch (no new PR). The three
    // providers (Claude API, OpenAI API, Claude Code) live in a single dropdown
    // so the PR header stays compact (issue #150).
    setOcticon(m_pullFixButton, "rocket", 16);
    m_pullFixButton->setToolTip(
        "Let an agent resolve these conflicts and commit the fix to this pull "
        "request's branch \xE2\x80\x94 watch it run on the Agents tab");
    m_pullFixButton->hide(); // only shown when the PR has conflicts
    m_pullFixMenu = new QMenu(m_pullFixButton);
    m_pullFixMenu->setToolTipsVisible(true);
    m_pullFixClaudeAction = m_pullFixMenu->addAction(QStringLiteral("Claude API"));
    m_pullFixOpenAiAction = m_pullFixMenu->addAction(QStringLiteral("OpenAI API"));
    m_pullFixClaudeCodeAction =
        m_pullFixMenu->addAction(QStringLiteral("Claude Code"));
    connect(m_pullFixClaudeAction, &QAction::triggered, this,
            [this] { fixCurrentPullConflictsWithAi(QStringLiteral("claude")); });
    connect(m_pullFixOpenAiAction, &QAction::triggered, this,
            [this] { fixCurrentPullConflictsWithAi(QStringLiteral("openai")); });
    connect(m_pullFixClaudeCodeAction, &QAction::triggered, this,
            [this] { fixCurrentPullConflictsWithAi(QStringLiteral("claude-code")); });
    m_pullFixButton->setMenu(m_pullFixMenu);
    // Continue the agent session that authored this branch, same as the agent
    // detail view's "Fix conflicts with agent" button: it keeps the run's own
    // context/history and full tool access instead of a fresh, conflict-only
    // rewrite. Only shown when such a session is attached (see
    // updatePullActionState / agentSessionForPull).
    m_pullFixConflictsButton->setObjectName("primaryButton");
    setOcticon(m_pullFixConflictsButton, "git-merge", 16);
    m_pullFixConflictsButton->hide();
    connect(m_pullFixConflictsButton, &QPushButton::clicked, this,
            &MainWindow::fixCurrentPullConflictsWithOriginatingAgent);
    setOcticon(m_pullEditFileButton, "pencil", 16);
    m_pullEditFileButton->setToolTip(
        "Edit the selected file and commit the change to this pull request's "
        "branch (the PR stays open, ready to merge)");
    connect(m_pullEditFileButton, &QPushButton::clicked, this,
            &MainWindow::editCurrentPullFile);
    setOcticon(m_pullDeleteFileButton, "trash", 16);
    m_pullDeleteFileButton->setToolTip(
        "Delete the selected file and commit the deletion to this pull request's "
        "branch (the PR stays open, ready to merge)");
    connect(m_pullDeleteFileButton, &QPushButton::clicked, this,
            &MainWindow::deleteCurrentPullFile);
    // AI code review (adhoc #82): a prominent "Review with AI" button on every
    // open PR. Findings come back as review threads attached to the lines of
    // code they concern; safe fixes carry a one-click "Apply fix & commit"
    // suggestion. "Fix all with AI" appears once unresolved review threads
    // exist and hands the whole list — including findings without a quick fix —
    // to a Claude Code agent that commits to the PR's branch.
    m_pullReviewAiButton = new QPushButton("Review with AI");
    m_pullFixAllAiButton = new QPushButton("Fix all with AI");
    for (QPushButton *b : {m_pullReviewAiButton, m_pullFixAllAiButton}) {
        b->setObjectName("primaryButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_pullReviewAiButton, "eye", 16);
    m_pullReviewAiButton->setToolTip(
        "Ask an AI to review this pull request's diff. Each finding is posted "
        "as a review thread on the lines of code it concerns; findings with a "
        "safe mechanical fix get a one-click \"Apply fix & commit\".");
    connect(m_pullReviewAiButton, &QPushButton::clicked, this,
            &MainWindow::reviewCurrentPullWithAi);
    setOcticon(m_pullFixAllAiButton, "rocket", 16);
    m_pullFixAllAiButton->setToolTip(
        "Let a Claude Code agent work through every unresolved review finding "
        "\xE2\x80\x94 including the ones without a quick fix \xE2\x80\x94 and "
        "commit the fixes to this pull request's branch");
    m_pullFixAllAiButton->hide(); // only shown when unresolved findings exist
    connect(m_pullFixAllAiButton, &QPushButton::clicked, this,
            &MainWindow::fixCurrentPullFindingsWithAgent);
    // The title gets its own line above the action buttons (issue #261): with this
    // many buttons a single shared row squeezed the title into a sliver. The button
    // row below packs left (trailing stretch) so it reads as a toolbar.
    auto *pullHeaderRow = new QHBoxLayout;
    pullHeaderRow->setContentsMargins(0, 0, 0, 0);
    pullHeaderRow->addWidget(m_pullSplitButton, 0, Qt::AlignTop);
    // The AI review pair leads the toolbar so it reads as the page's headline
    // action (adhoc #82).
    pullHeaderRow->addWidget(m_pullReviewAiButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullFixAllAiButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullPreviewButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullUpdateButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullResolveButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullFixConflictsButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullFixButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullEditFileButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullDeleteFileButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullMergeButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullMergeDeleteButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullReopenButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullSendToSourceButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullLinkIssueButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullCloseButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullDeleteButton, 0, Qt::AlignTop);
    pullHeaderRow->addWidget(m_pullDeleteBranchButton, 0, Qt::AlignTop);
    pullHeaderRow->addStretch(1);
    m_pullMeta = new QLabel;
    m_pullMeta->setObjectName("statusLine");
    m_pullMeta->setTextFormat(Qt::RichText);
    m_pullMeta->setWordWrap(true);
    m_pullMeta->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                        Qt::LinksAccessibleByMouse);
    // The "agent" reference (adhoc #78) jumps to the producing session's detail;
    // the base/head branch names open that branch's row in the Branches tab so a
    // branch is clickable here too (issue #204).
    connect(m_pullMeta, &QLabel::linkActivated, this, [this](const QString &href) {
        if (href.startsWith(kAgentLinkScheme))
            switchToAgentsTab(href.mid(kAgentLinkScheme.size()).toInt());
        else if (href.startsWith(kBranchLinkScheme))
            switchToBranch(QUrl::fromPercentEncoding(
                href.mid(kBranchLinkScheme.size()).toUtf8()));
    });
    // Merge-readiness banner: a dry-run of the patch tells the reviewer whether
    // it applies cleanly (or which files conflict) before they hit Merge.
    m_pullMergeStatus = new QLabel;
    m_pullMergeStatus->setObjectName("statusLine");
    m_pullMergeStatus->setTextFormat(Qt::RichText);
    m_pullMergeStatus->setWordWrap(true);
    m_pullMergeStatus->hide();

    m_pullReviewSummary = new QLabel;
    m_pullReviewSummary->setObjectName("pullReviewSummary");
    m_pullReviewSummary->setTextFormat(Qt::RichText);
    m_pullReviewSummary->setWordWrap(true);
    m_pullReviewSummary->setOpenExternalLinks(false);
    m_pullReviewSummary->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_pullReviewSummary->setContentsMargins(14, 10, 14, 10);
    m_pullReviewSummary->hide();
    connect(m_pullReviewSummary, &QLabel::linkActivated, this,
            [this](const QString &href) {
                int index = -1;
                QPushButton *button = nullptr;
                if (href == QLatin1String("tab:conversation")) {
                    index = 0;
                    button = m_pullTabConversation;
                } else if (href == QLatin1String("tab:checks")) {
                    index = 2;
                    button = m_pullTabChecks;
                } else if (href == QLatin1String("tab:files")) {
                    index = 3;
                    button = m_pullTabFiles;
                }
                if (index < 0 || !m_pullSubStack)
                    return;
                if (button)
                    button->setChecked(true);
                m_pullSubStack->setCurrentIndex(index);
            });

    m_pullFiles = new QListWidget;
    m_pullFiles->setObjectName("overviewList");
    enableHoverRowHighlight(m_pullFiles); // green outline selection (issue #252)
    m_pullFiles->setMinimumWidth(180);
    connect(m_pullFiles, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                // The whole PR is rendered into one scrollable view; picking a
                // file just scrolls to its section (issue #250). The guard skips
                // that scroll when the selection is itself following the scroll.
                if (item && !m_pullSuppressFileScroll)
                    scrollPullDiffToFile(item->data(Qt::UserRole).toString());
                if (!item) {
                    if (m_pullEditFileButton)
                        m_pullEditFileButton->setEnabled(false);
                    if (m_pullDeleteFileButton)
                        m_pullDeleteFileButton->setEnabled(false);
                }
            });

    // ---- Files changed page: file explorer | diff viewer.
    // The diff viewer shows every changed file in one scrollable view; Prev/Next
    // jump the scroll between consecutive changes (hunks) across all of the
    // files, and the file list selects whichever file is on screen (issue #250).
    m_pullPrevButton = new QPushButton;
    m_pullPrevButton->setToolTip("Previous change");
    setOcticon(m_pullPrevButton, "chevron-up", 14);
    connect(m_pullPrevButton, &QPushButton::clicked, this,
            [this] { pullSelectAdjacentChange(-1); });
    m_pullNextButton = new QPushButton;
    m_pullNextButton->setToolTip("Next change");
    setOcticon(m_pullNextButton, "chevron-down", 14);
    connect(m_pullNextButton, &QPushButton::clicked, this,
            [this] { pullSelectAdjacentChange(1); });
    // +/- zoom for the diff text size (shared by every diff view via diffStyleSheet).
    m_diffFontPt = qBound(8, QSettings().value(kDiffFontPtSetting, 12).toInt(), 28);
    auto *diffZoomOut = new QPushButton(QString::fromUtf8("\xE2\x88\x92")); // −
    diffZoomOut->setToolTip("Smaller diff text");
    connect(diffZoomOut, &QPushButton::clicked, this, [this] { adjustDiffFont(-1); });
    auto *diffZoomIn = new QPushButton(QStringLiteral("+"));
    diffZoomIn->setToolTip("Larger diff text");
    connect(diffZoomIn, &QPushButton::clicked, this, [this] { adjustDiffFont(1); });
    // Auto-mark-viewed toggle: while checked, files scrolled entirely above the
    // diff viewport get checked off "Viewed" without hand-clicking each one.
    m_pullAutoViewedButton = new QPushButton;
    m_pullAutoViewedButton->setCheckable(true);
    m_pullAutoViewedButton->setChecked(autoMarkViewedOnScrollPref());
    setOcticon(m_pullAutoViewedButton, "eye", 14);
    m_pullAutoViewedButton->setToolTip(
        "Automatically mark files as viewed while scrolling");
    connect(m_pullAutoViewedButton, &QPushButton::clicked, this, [this](bool on) {
        setAutoMarkViewedOnScrollPref(on);
        if (on)
            applyAutoMarkViewedOnScroll(); // catch up on the current scroll position
    });
    for (QPushButton *b : {m_pullPrevButton, m_pullNextButton, diffZoomOut, diffZoomIn,
                          m_pullAutoViewedButton}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    auto *filesHeader = new QHBoxLayout;
    filesHeader->setContentsMargins(0, 0, 0, 0);
    auto *filesHeaderLabel = new QLabel("FILES");
    filesHeaderLabel->setObjectName("sectionLabel");
    filesHeader->addWidget(filesHeaderLabel);
    filesHeader->addStretch();
    filesHeader->addWidget(diffZoomOut);
    filesHeader->addWidget(diffZoomIn);
    filesHeader->addWidget(m_pullAutoViewedButton);
    filesHeader->addWidget(m_pullPrevButton);
    filesHeader->addWidget(m_pullNextButton);

    // Authorship filter (issue #365): narrow the file list to agent- or
    // human-authored files. Only shown for PRs whose commits mix the two.
    m_pullFileAuthorFilter = new QComboBox;
    m_pullFileAuthorFilter->addItem(QStringLiteral("All authors"));
    m_pullFileAuthorFilter->addItem(QStringLiteral("Agent-authored"));
    m_pullFileAuthorFilter->addItem(QStringLiteral("Human-authored"));
    m_pullFileAuthorFilter->setToolTip(
        QStringLiteral("Filter changed files by whether an agent authored them"));
    m_pullFileAuthorFilter->hide();
    connect(m_pullFileAuthorFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyPullFileAuthorFilter(); });

    auto *filesPane = new QWidget;
    auto *filesPaneLayout = new QVBoxLayout(filesPane);
    filesPaneLayout->setContentsMargins(0, 0, 0, 0);
    filesPaneLayout->setSpacing(6);
    filesPaneLayout->addLayout(filesHeader);
    filesPaneLayout->addWidget(m_pullFileAuthorFilter);
    filesPaneLayout->addWidget(m_pullFiles, 1);

    m_pullDiff = new QTextBrowser;
    m_pullDiff->setObjectName("diffView");
    m_pullDiff->setOpenExternalLinks(false);
    m_pullDiff->setOpenLinks(false); // we handle "cmt:" anchors ourselves
    m_pullDiff->setLineWrapMode(QTextEdit::NoWrap);
    connect(m_pullDiff, &QTextBrowser::anchorClicked, this,
            &MainWindow::onPullDiffAnchorClicked);
    registerDiffView(m_pullDiff);
    // Debounce the auto-mark-viewed scan off scroll ticks: re-rendering (which
    // collapses newly-viewed files) is too heavy to run on every pixel of a
    // fast scroll, so wait for scrolling to settle before checking.
    m_pullAutoViewedDebounce = new QTimer(this);
    m_pullAutoViewedDebounce->setSingleShot(true);
    m_pullAutoViewedDebounce->setInterval(400);
    connect(m_pullAutoViewedDebounce, &QTimer::timeout, this,
            &MainWindow::applyAutoMarkViewedOnScroll);
    connect(m_pullDiff->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] {
                if (m_pullAutoViewedButton && m_pullAutoViewedButton->isChecked())
                    m_pullAutoViewedDebounce->start();
            });

    auto *diffSplit = new QSplitter(Qt::Horizontal);
    diffSplit->setChildrenCollapsible(false);
    diffSplit->addWidget(filesPane);
    diffSplit->addWidget(m_pullDiff);
    diffSplit->setStretchFactor(0, 0);
    diffSplit->setStretchFactor(1, 1);
    diffSplit->setSizes({240, 600});

    // ---- Find bar (issue #333): Ctrl+F over the Files-changed pane toggles a
    // bar that highlights every occurrence of the typed text in the combined
    // diff and steps between matches. Hidden until summoned.
    m_pullDiffSearchInput = new QLineEdit;
    m_pullDiffSearchInput->setObjectName("issueSearch");
    m_pullDiffSearchInput->setPlaceholderText("Find in diff\xE2\x80\xA6");
    m_pullDiffSearchInput->setClearButtonEnabled(true);
    connect(m_pullDiffSearchInput, &QLineEdit::textChanged, this,
            [this] { pullDiffSearchRecompute(); });
    connect(m_pullDiffSearchInput, &QLineEdit::returnPressed, this, [this] {
        pullDiffSearchGoTo(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier
                               ? -1
                               : 1);
    });
    m_pullDiffSearchCount = new QLabel;
    m_pullDiffSearchCount->setObjectName("hintLabel");
    auto *searchPrev = new QPushButton;
    searchPrev->setToolTip("Previous match");
    setOcticon(searchPrev, "chevron-up", 14);
    connect(searchPrev, &QPushButton::clicked, this,
            [this] { pullDiffSearchGoTo(-1); });
    auto *searchNext = new QPushButton;
    searchNext->setToolTip("Next match");
    setOcticon(searchNext, "chevron-down", 14);
    connect(searchNext, &QPushButton::clicked, this,
            [this] { pullDiffSearchGoTo(1); });
    auto *searchClose = new QPushButton;
    searchClose->setToolTip("Close find bar");
    setOcticon(searchClose, "x", 14);
    connect(searchClose, &QPushButton::clicked, this,
            [this] { togglePullDiffSearch(false); });
    for (QPushButton *b : {searchPrev, searchNext, searchClose}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    m_pullDiffSearchBar = new QWidget;
    auto *searchBarLayout = new QHBoxLayout(m_pullDiffSearchBar);
    searchBarLayout->setContentsMargins(0, 0, 0, 6);
    searchBarLayout->addWidget(m_pullDiffSearchInput, 1);
    searchBarLayout->addWidget(m_pullDiffSearchCount);
    searchBarLayout->addWidget(searchPrev);
    searchBarLayout->addWidget(searchNext);
    searchBarLayout->addWidget(searchClose);
    m_pullDiffSearchBar->setVisible(false);

    auto *filesPage = new QWidget;
    auto *filesPageLayout = new QVBoxLayout(filesPage);
    filesPageLayout->setContentsMargins(0, 0, 0, 0);
    filesPageLayout->addWidget(m_pullDiffSearchBar);
    filesPageLayout->addWidget(diffSplit);

    auto *findShortcut = new QShortcut(QKeySequence::Find, filesPage);
    connect(findShortcut, &QShortcut::activated, this,
            [this] { togglePullDiffSearch(true); });
    auto *closeSearchShortcut = new QShortcut(QKeySequence(Qt::Key_Escape),
                                              m_pullDiffSearchInput);
    closeSearchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeSearchShortcut, &QShortcut::activated, this,
            [this] { togglePullDiffSearch(false); });

    // ---- Commits page: every commit that makes up this PR; clicking one opens
    // it in the repo's commit view.
    m_pullCommitsList = new QListWidget;
    m_pullCommitsList->setObjectName("overviewList");
    enableHoverRowHighlight(m_pullCommitsList); // green outline selection (issue #252)
    connect(m_pullCommitsList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                const QString sha = item ? item->data(kCommitShaRole).toString()
                                         : QString();
                if (sha.isEmpty())
                    return;
                showOverviewCommits();
                showCommit(sha);
            });
    // Right-click a commit to copy its full hash or message — the row only shows
    // the abbreviated hash, so this is how the full SHA gets out (issue #46).
    m_pullCommitsList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_pullCommitsList, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QListWidgetItem *item = m_pullCommitsList->itemAt(pos);
                if (!item)
                    return;
                QString sha = item->data(kCommitShaRole).toString();
                if (sha.isEmpty())
                    sha = item->data(kCommitCopyShaRole).toString();
                const QString message = item->data(kCommitMessageRole).toString();
                QMenu menu(m_pullCommitsList);
                QAction *copyHash =
                    sha.isEmpty() ? nullptr
                                  : menu.addAction(QStringLiteral("Copy commit hash"));
                QAction *copyMessage =
                    message.isEmpty()
                        ? nullptr
                        : menu.addAction(QStringLiteral("Copy commit message"));
                if (!copyHash && !copyMessage)
                    return;
                QAction *chosen =
                    menu.exec(m_pullCommitsList->viewport()->mapToGlobal(pos));
                if (chosen && chosen == copyHash) {
                    QApplication::clipboard()->setText(sha);
                    flashMessage("Commit hash copied.");
                } else if (chosen && chosen == copyMessage) {
                    QApplication::clipboard()->setText(message);
                    flashMessage("Commit message copied.");
                }
            });

    // ---- Checks page: action runs for this PR's commits + a manual trigger.
    m_pullRunChecksButton = new QPushButton("Run checks against this PR");
    m_pullRunChecksButton->setObjectName("ghostButton");
    m_pullRunChecksButton->setProperty("buttonSize", "sm");
    m_pullRunChecksButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_pullRunChecksButton, "workflow", 16);
    m_pullRunChecksButton->setToolTip(
        "Queue this repository's push workflows against the pull request's head commit");
    connect(m_pullRunChecksButton, &QPushButton::clicked, this,
            &MainWindow::runChecksForCurrentPull);
    auto *checksToolbar = new QHBoxLayout;
    checksToolbar->setContentsMargins(0, 0, 0, 0);
    checksToolbar->addWidget(m_pullRunChecksButton);
    checksToolbar->addStretch();
    m_pullChecksTable = new QTableWidget(0, 4);
    m_pullChecksTable->setObjectName("issueTable");
    installColumnHeaderMenu(m_pullChecksTable); // 3-dots per-column menu (issue #318)
    enableHoverRowHighlight(m_pullChecksTable);
    m_pullChecksTable->setHorizontalHeaderLabels(
        {"Status", "Workflow", "Commit", "Duration"});
    m_pullChecksTable->verticalHeader()->setVisible(false);
    m_pullChecksTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pullChecksTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pullChecksTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pullChecksTable->setShowGrid(false);
    m_pullChecksTable->setWordWrap(false);
    QHeaderView *ch = m_pullChecksTable->horizontalHeader();
    ch->setHighlightSections(false);
    ch->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ch->setSectionResizeMode(1, QHeaderView::Stretch);
    ch->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ch->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_pullChecksTable);
    connect(m_pullChecksTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const QModelIndexList rows = m_pullChecksTable->selectionModel()->selectedRows();
        if (rows.isEmpty())
            return;
        if (QTableWidgetItem *first = m_pullChecksTable->item(rows.first().row(), 0))
            showPullCheckLog(first->data(Qt::UserRole).toInt());
    });
    m_pullChecksLog = new QPlainTextEdit;
    m_pullChecksLog->setObjectName("actionLog");
    m_pullChecksLog->setReadOnly(true);
    m_pullChecksLog->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto *checksSplit = new QSplitter(Qt::Vertical);
    checksSplit->setChildrenCollapsible(false);
    checksSplit->addWidget(m_pullChecksTable);
    checksSplit->addWidget(m_pullChecksLog);
    checksSplit->setStretchFactor(0, 1);
    checksSplit->setStretchFactor(1, 1);
    auto *checksPage = new QWidget;
    auto *checksPageLayout = new QVBoxLayout(checksPage);
    checksPageLayout->setContentsMargins(0, 0, 0, 0);
    checksPageLayout->setSpacing(8);
    checksPageLayout->addLayout(checksToolbar);
    checksPageLayout->addWidget(checksSplit, 1);

    // ---- Conversation page: review thread, an inline checks summary, and the
    // composer — all in one scroll area so the whole section scrolls together.
    m_pullThreadContainer = new QWidget;
    m_pullThreadLayout = new QVBoxLayout(m_pullThreadContainer);
    m_pullThreadLayout->setContentsMargins(0, 0, 0, 0);
    m_pullThreadLayout->setSpacing(10);
    m_pullThreadLayout->addStretch();

    m_pullChecksSummary = new QLabel;
    m_pullChecksSummary->setObjectName("issueTimelineCard");
    m_pullChecksSummary->setTextFormat(Qt::RichText);
    m_pullChecksSummary->setWordWrap(true);
    m_pullChecksSummary->setContentsMargins(16, 12, 16, 12);
    m_pullChecksSummary->hide();
    connect(m_pullChecksSummary, &QLabel::linkActivated, this, [this](const QString &) {
        if (m_pullTabChecks)
            m_pullTabChecks->setChecked(true);
        if (m_pullSubStack)
            m_pullSubStack->setCurrentIndex(2);
    });

    // Linked issues card: shown inline in the thread, with links that jump to the
    // referenced issue on the Issues tab.
    m_pullLinksValue = new QLabel;
    m_pullLinksValue->setObjectName("issueTimelineCard");
    m_pullLinksValue->setTextFormat(Qt::RichText);
    m_pullLinksValue->setWordWrap(true);
    m_pullLinksValue->setContentsMargins(16, 12, 16, 12);
    m_pullLinksValue->setOpenExternalLinks(false);
    m_pullLinksValue->hide();
    connect(m_pullLinksValue, &QLabel::linkActivated, this, [this](const QString &href) {
        const int n = href.section(QLatin1Char(':'), 1).toInt();
        if (n <= 0)
            return;
        if (m_repoDetailTabs && m_repoDetailTabs->button(2))
            m_repoDetailTabs->button(2)->click();
        reloadIssues();
        showIssue(n);
    });

    m_pullComposer = new MarkdownEditor;
    m_pullComposer->setPlaceholderText("Leave a comment or review\xE2\x80\xA6");
    m_pullComposer->setMinimumHeight(90);
    m_pullCommentButton = new QPushButton("Comment");
    m_pullApproveButton = new QPushButton("Approve");
    m_pullRequestChangesButton = new QPushButton("Request changes");
    for (QPushButton *b : {m_pullCommentButton, m_pullApproveButton,
                           m_pullRequestChangesButton}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_pullCommentButton, "comment", 16);
    setOcticon(m_pullApproveButton, "check-circle", 16);
    setOcticon(m_pullRequestChangesButton, "alert", 16);
    connect(m_pullCommentButton, &QPushButton::clicked, this,
            &MainWindow::submitPullComment);
    connect(m_pullApproveButton, &QPushButton::clicked, this,
            [this] { submitPullReview(QStringLiteral("approved")); });
    connect(m_pullRequestChangesButton, &QPushButton::clicked, this,
            [this] { submitPullReview(QStringLiteral("changes_requested")); });
    auto *composerButtons = new QHBoxLayout;
    composerButtons->setContentsMargins(0, 0, 0, 0);
    // Speak the comment with the voice engine, just like the footer prompt mic.
    composerButtons->addWidget(makeVoiceButton(m_pullComposer), 0, Qt::AlignLeft);
    composerButtons->addStretch();
    composerButtons->addWidget(m_pullRequestChangesButton);
    composerButtons->addWidget(m_pullApproveButton);
    composerButtons->addWidget(m_pullCommentButton);
    auto *composerBlock = new QWidget;
    auto *composerBlockLayout = new QVBoxLayout(composerBlock);
    composerBlockLayout->setContentsMargins(0, 0, 0, 0);
    composerBlockLayout->setSpacing(6);
    composerBlockLayout->addWidget(m_pullComposer);
    composerBlockLayout->addLayout(composerButtons);

    // Conflicting-files card: surfaces, inline above the comment composer, which
    // files of a conflicted PR no longer apply to the base. Populated (and shown
    // only when the PR is conflicted) in updatePullActionState(). The link jumps
    // to the Files changed tab so the reviewer can inspect them.
    m_pullConflictDetails = new QLabel;
    m_pullConflictDetails->setObjectName("issueTimelineCard");
    m_pullConflictDetails->setTextFormat(Qt::RichText);
    m_pullConflictDetails->setWordWrap(true);
    m_pullConflictDetails->setContentsMargins(16, 12, 16, 12);
    m_pullConflictDetails->setOpenExternalLinks(false);
    m_pullConflictDetails->hide();
    connect(m_pullConflictDetails, &QLabel::linkActivated, this,
            [this](const QString &) {
                if (m_pullTabFiles)
                    m_pullTabFiles->setChecked(true);
                if (m_pullSubStack)
                    m_pullSubStack->setCurrentIndex(3);
            });

    auto *conversationInner = new QWidget;
    auto *conversationInnerLayout = new QVBoxLayout(conversationInner);
    conversationInnerLayout->setContentsMargins(0, 0, 0, 0);
    conversationInnerLayout->setSpacing(10);
    conversationInnerLayout->addWidget(m_pullThreadContainer);
    conversationInnerLayout->addWidget(m_pullLinksValue);
    conversationInnerLayout->addWidget(m_pullChecksSummary);
    conversationInnerLayout->addWidget(m_pullConflictDetails);
    conversationInnerLayout->addWidget(composerBlock);

    // Agent revision row: shown only when this PR was created by an agent session.
    // Lets the reviewer type feedback and send it back to the agent for revisions.
    m_pullAgentRevisionRow = new QWidget;
    m_pullAgentRevisionRow->setObjectName("agentRevisionRow");
    auto *agentRevisionLayout = new QHBoxLayout(m_pullAgentRevisionRow);
    agentRevisionLayout->setContentsMargins(0, 4, 0, 0);
    agentRevisionLayout->setSpacing(6);
    m_pullAgentRevisionEdit = new QLineEdit;
    m_pullAgentRevisionEdit->setPlaceholderText(
        "Describe the revision for the agentâ¦");
    m_pullSendToAgentButton = new QPushButton("Send to agent");
    m_pullSendToAgentButton->setObjectName("primaryButton");
    m_pullSendToAgentButton->setProperty("buttonSize", "sm");
    m_pullSendToAgentButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_pullSendToAgentButton, "rocket", 16);
    m_pullSendToAgentButton->setToolTip(
        "Post this note as a PR comment and re-queue the agent with the revision "
        "instructions so it continues work on the same branch");
    agentRevisionLayout->addWidget(m_pullAgentRevisionEdit, 1);
    agentRevisionLayout->addWidget(m_pullSendToAgentButton);
    connect(m_pullSendToAgentButton, &QPushButton::clicked,
            this, &MainWindow::sendPullRevisionToAgent);
    connect(m_pullAgentRevisionEdit, &QLineEdit::returnPressed,
            this, &MainWindow::sendPullRevisionToAgent);
    m_pullAgentRevisionRow->hide(); // only visible when this PR has a linked agent
    conversationInnerLayout->addWidget(m_pullAgentRevisionRow);

    conversationInnerLayout->addStretch();

    m_pullThreadScroll = new QScrollArea;
    m_pullThreadScroll->setWidgetResizable(true);
    m_pullThreadScroll->setWidget(conversationInner);
    m_pullThreadScroll->setObjectName("issuePageScroll");
    m_pullThreadScroll->setFrameShape(QFrame::NoFrame);

    // ---- Sub-tab bar + stack (Conversation / Commits / Checks / Files changed).
    m_pullSubStack = new QStackedWidget;
    m_pullSubStack->addWidget(m_pullThreadScroll); // 0 Conversation
    m_pullSubStack->addWidget(m_pullCommitsList);  // 1 Commits
    m_pullSubStack->addWidget(checksPage);         // 2 Checks
    m_pullSubStack->addWidget(filesPage);          // 3 Files changed

    m_pullSubTabs = new QButtonGroup(this);
    m_pullSubTabs->setExclusive(true);
    auto *subTabRow = new QHBoxLayout;
    subTabRow->setContentsMargins(0, 0, 0, 0);
    subTabRow->setSpacing(2);
    const QList<QPair<QString, const char *>> subTabs = {
        {QStringLiteral("Conversation"), "comment"},
        {QStringLiteral("Commits"), "git-branch"},
        {QStringLiteral("Checks"), "workflow"},
        {QStringLiteral("Files changed"), "file-diff"}};
    for (int i = 0; i < subTabs.size(); ++i) {
        auto *b = new QPushButton(subTabs.at(i).first);
        b->setObjectName("repoTab");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        setOcticon(b, QString::fromLatin1(subTabs.at(i).second), 16);
        if (i == 0)
            b->setChecked(true);
        m_pullSubTabs->addButton(b, i);
        subTabRow->addWidget(b);
    }
    subTabRow->addStretch();
    m_pullTabConversation = qobject_cast<QPushButton *>(m_pullSubTabs->button(0));
    m_pullTabCommits = qobject_cast<QPushButton *>(m_pullSubTabs->button(1));
    m_pullTabChecks = qobject_cast<QPushButton *>(m_pullSubTabs->button(2));
    m_pullTabFiles = qobject_cast<QPushButton *>(m_pullSubTabs->button(3));
    connect(m_pullSubTabs, &QButtonGroup::idClicked, this, [this](int id) {
        m_pullSubStack->setCurrentIndex(id);
        if (id == 2) // refresh the Checks table when it's brought forward
            for (const PullRequest &pr : std::as_const(m_currentPulls))
                if (pr.number == m_currentPullNumber)
                    renderPullChecks(pr);
    });

    auto *detailLayout = new QVBoxLayout(m_pullDetail);
    detailLayout->setContentsMargins(18, 18, 18, 18);
    detailLayout->setSpacing(8);
    detailLayout->addWidget(m_pullTitle);
    detailLayout->addLayout(pullHeaderRow);
    detailLayout->addWidget(m_pullMeta);
    detailLayout->addWidget(m_pullMergeStatus);
    detailLayout->addWidget(m_pullReviewSummary);
    detailLayout->addLayout(subTabRow);
    detailLayout->addWidget(m_pullSubStack, 1);

    // Open full width: the table fills the page until a pull request is
    // selected, at which point showPull() reveals the detail pane beside it.
    m_pullDetail->hide();

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(listPane);
    splitter->addWidget(m_pullDetail);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({460, 620});

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter);

    connect(m_pullSearch, &QLineEdit::textChanged, this,
            [this] { refreshPullList(); });
    connect(m_pullTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const QModelIndexList rows = m_pullTable->selectionModel()->selectedRows();
        if (rows.isEmpty())
            return;
        if (QTableWidgetItem *first = m_pullTable->item(rows.first().row(), 0))
            showPull(first->data(Qt::UserRole).toInt());
    });
    connect(m_pullNewButton, &QPushButton::clicked, this, &MainWindow::promptNewPull);
    connect(m_pullChooseDirButton, &QPushButton::clicked,
            this, &MainWindow::promptNewPullFromDirectory);
    connect(m_pullSyncButton, &QPushButton::clicked, this, &MainWindow::syncPullsInbox);
    connect(m_pullUpdateButton, &QPushButton::clicked,
            this, &MainWindow::updateCurrentPullBranch);
    connect(m_pullMergeButton, &QPushButton::clicked, this, &MainWindow::mergeCurrentPull);
    connect(m_pullMergeDeleteButton, &QPushButton::clicked, this,
            &MainWindow::mergeAndDeleteCurrentPull);
    connect(m_pullCloseButton, &QPushButton::clicked, this, &MainWindow::closeCurrentPull);
    connect(m_pullReopenButton, &QPushButton::clicked, this, &MainWindow::reopenCurrentPull);
    connect(m_pullSendToSourceButton, &QPushButton::clicked, this,
            &MainWindow::sendCurrentPullToSource);
    connect(m_pullDeleteButton, &QPushButton::clicked, this, &MainWindow::deleteCurrentPull);
    connect(m_pullDeleteBranchButton, &QPushButton::clicked, this,
            &MainWindow::deleteCurrentPullAndBranch);
    return page;
}

PullStore MainWindow::pullStoreForCurrentRepo() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return PullStore(QString(), QString(), &m_profileIdentity, m_userName);
    const RepositoryRecord &repo =
        writableRecordFor(m_repositories.at(m_repoDetailIndex));
    return PullStore(repo.localPath, repo.mirrorPath, &m_profileIdentity, m_userName);
}

QString MainWindow::pullPatchFingerprint(const QString &patch)
{
    return QString::number(patch.size()) + QLatin1Char(':') +
           QString::number(qHash(patch));
}

void MainWindow::reloadPulls()
{
    if (!m_pullTable)
        return;
    const PullStore store = pullStoreForCurrentRepo();
    m_currentPulls = store.loadAll();
    // Pre-compute which open PRs no longer apply cleanly so refreshPullList() can
    // badge their rows. Done here (not per refresh) so typing in the search box
    // doesn't re-spawn the dry-run apply for every open PR. Only meaningful when
    // we have a working tree to test the patch against.
    //
    // Bump the generation so any in-flight async conflict pass aborts, and reset
    // the pending queue: a fresh reload rebuilds both the badges and the work to do.
    const quint64 gen = ++m_pullConflictGen;
    m_pendingPullConflictChecks.clear();
    m_pullConflictByNumber.clear();
    if (store.canWrite()) {
        // The dry-run apply only changes when the base tip or a PR's patch moves,
        // so cache it: otherwise every reloadPulls() (each push, search keystroke
        // path, or merge of a different PR) re-queues `git apply --check` for every
        // open PR. Invalidate wholesale when the repo or the base tip changes;
        // per-PR entries re-check when the patch fingerprint differs. Cache misses
        // are resolved asynchronously below rather than inline.
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        const QString cacheKey = repo.owner + QLatin1Char('/') + repo.name +
                                 QLatin1Char('@') + store.baseTip();
        if (cacheKey != m_pullConflictCacheBaseTip) {
            m_pullConflictCacheBaseTip = cacheKey;
            m_pullConflictCache.clear();
        }
        QSet<int> openNumbers;
        for (const PullRequest &pr : std::as_const(m_currentPulls)) {
            if (pr.status != QLatin1String("open"))
                continue;
            openNumbers.insert(pr.number);
            const QString fingerprint = pullPatchFingerprint(pr.patch);
            const auto cached = m_pullConflictCache.constFind(pr.number);
            if (cached != m_pullConflictCache.constEnd() &&
                cached->fingerprint == fingerprint) {
                // Cache hit: badge straight from the stored result.
                if (cached->conflict)
                    m_pullConflictByNumber.insert(pr.number, true);
            } else {
                // Cold/stale entry: defer the (slow) `git apply --check` dry-run to
                // the async pass below so a full reload never blocks the GUI thread.
                m_pendingPullConflictChecks.append(qMakePair(pr.number, fingerprint));
            }
        }
        // Drop cache entries for PRs that have since closed/merged or been deleted
        // so the map can't grow without bound across a long session.
        for (auto it = m_pullConflictCache.begin();
             it != m_pullConflictCache.end();) {
            if (openNumbers.contains(it.key()))
                ++it;
            else
                it = m_pullConflictCache.erase(it);
        }
    }
    updateRepoPullCount();
    refreshPullList();
    updatePullActionState();
    // Fill in any uncached conflict badges off the critical path, one PR per
    // event-loop turn, so a cold cache never freezes the UI (the list above is
    // already on screen; the badges drop in as each dry-run finishes).
    if (!m_pendingPullConflictChecks.isEmpty())
        QTimer::singleShot(0, this, [this, gen] { processPendingPullConflicts(gen); });
}

void MainWindow::processPendingPullConflicts(quint64 gen)
{
    // A newer reloadPulls() (repo switch, push, merge, ...) supersedes this pass.
    if (gen != m_pullConflictGen || m_pendingPullConflictChecks.isEmpty())
        return;
    // One dry-run runs off-thread at a time. A redundant drain (a double
    // singleShot, or queuePullConflictCheck firing while a worker runs) just
    // returns here — the in-flight worker reschedules us when it finishes, so the
    // queue still drains in order.
    if (m_pullConflictCheckInFlight)
        return;
    const QPair<int, QString> item = m_pendingPullConflictChecks.takeFirst();
    const int number = item.first;
    const QString fingerprint = item.second;
    const PullStore store = pullStoreForCurrentRepo();
    if (!store.canWrite()) {
        // No working tree to test the patch against; skip and drain the rest.
        if (gen == m_pullConflictGen && !m_pendingPullConflictChecks.isEmpty())
            QTimer::singleShot(0, this, [this, gen] { processPendingPullConflicts(gen); });
        return;
    }
    // A single cold `git apply --check` can run well over the 1.5s stall
    // threshold, so run it on a worker thread rather than on the GUI thread. The
    // old keepGuiAlive path pumped the event loop in place, which could itself
    // re-enter a heavy QTextDocumentLayout and block just as long (see stall
    // reports). PullStore is copied by value and only shells out to git, so the
    // worker touches no shared Qt state; results come back via shared_ptr to the
    // finished handler, which runs on the main thread.
    m_pullConflictCheckInFlight = true;
    auto clean = std::make_shared<bool>(false);
    auto mergeable = std::make_shared<bool>(false);
    auto conflictFiles = std::make_shared<QStringList>();
    QThread *worker = QThread::create([store, number, clean, mergeable,
                                       conflictFiles]() mutable {
        *mergeable = store.checkMergeable(number, clean.get(), conflictFiles.get(),
                                          nullptr, /*keepGuiAlive=*/false);
    });
    connect(worker, &QThread::finished, this,
            [this, worker, gen, number, fingerprint, clean, mergeable,
             conflictFiles]() {
                m_pullConflictCheckInFlight = false;
                worker->deleteLater();
                const bool conflict = *mergeable && !*clean;
                // Apply the result only if this repo/list is still current; a
                // reloadPulls() may have bumped the generation while git ran.
                if (gen == m_pullConflictGen) {
                    m_pullConflictCache.insert(
                        number, {fingerprint, conflict, *conflictFiles});
                    if (conflict)
                        m_pullConflictByNumber.insert(number, true);
                    else
                        m_pullConflictByNumber.remove(number);
                    setPullConflictBadge(number, conflict);
                    // If this dry-run was for the PR currently on screen, refresh
                    // its merge UI now that the result is cached.
                    if (number == m_currentPullNumber)
                        updatePullActionState();
                }
                // Drain the next pending check under the *current* generation,
                // whether or not this result was superseded: a superseding
                // reloadPulls() rebuilt the queue and is waiting on this worker to
                // release the in-flight guard.
                if (!m_pendingPullConflictChecks.isEmpty()) {
                    const quint64 current = m_pullConflictGen;
                    QTimer::singleShot(0, this, [this, current] {
                        processPendingPullConflicts(current);
                    });
                }
            });
    worker->start();
}

void MainWindow::queuePullConflictCheck(int number, const QString &fingerprint)
{
    // Already queued for this PR? The pending entry will produce a fresh result,
    // so don't append a duplicate dry-run.
    for (const QPair<int, QString> &p : std::as_const(m_pendingPullConflictChecks))
        if (p.first == number)
            return;
    // Whenever the pending list is non-empty a drain is already running or
    // scheduled (reloadPulls / processPendingPullConflicts keep that invariant),
    // so only kick off a new pass when we're appending to an idle queue.
    const bool wasIdle = m_pendingPullConflictChecks.isEmpty();
    m_pendingPullConflictChecks.append(qMakePair(number, fingerprint));
    if (wasIdle) {
        const quint64 gen = m_pullConflictGen;
        QTimer::singleShot(0, this, [this, gen] { processPendingPullConflicts(gen); });
    }
}

void MainWindow::setPullConflictBadge(int number, bool conflict)
{
    if (!m_pullTable)
        return;
    for (int row = 0; row < m_pullTable->rowCount(); ++row) {
        const QTableWidgetItem *num = m_pullTable->item(row, 0);
        if (!num || num->data(Qt::UserRole).toInt() != number)
            continue;
        QTableWidgetItem *st = m_pullTable->item(row, 3);
        if (!st)
            return;
        if (conflict) {
            st->setIcon(themedOcticon("alert", QColor("#f85149"), 13));
            st->setToolTip(QStringLiteral("This pull request has merge conflicts"));
        } else {
            st->setIcon(QIcon());
            st->setToolTip(QString());
        }
        return;
    }
}

void MainWindow::refreshPullList()
{
    if (!m_pullTable)
        return;
    const QString search = m_pullSearch ? m_pullSearch->text().trimmed() : QString();
    const int keep = m_currentPullNumber;
    TableRepaintGuard repaintGuard(m_pullTable);
    m_pullTable->setSortingEnabled(false);
    m_pullTable->setRowCount(0);
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (!search.isEmpty()) {
            const QString hay = QStringLiteral("#%1 %2 %3 %4 %5")
                                    .arg(pr.number)
                                    .arg(pr.title, pr.base, pr.head, pr.authorName);
            if (!hay.contains(search, Qt::CaseInsensitive))
                continue;
        }
        const int row = m_pullTable->rowCount();
        m_pullTable->insertRow(row);
        auto *num = new QTableWidgetItem;
        num->setData(Qt::DisplayRole, pr.number);
        num->setData(Qt::UserRole, pr.number);
        m_pullTable->setItem(row, 0, num);
        auto *titleItem = new QTableWidgetItem(pr.title);
        m_pullTable->setItem(row, 1, titleItem);
        m_pullTable->setItem(row, 2,
                             new QTableWidgetItem(pr.base + QString::fromUtf8(" \xE2\x86\x90 ") +
                                                  pr.head));
        auto *st = new QTableWidgetItem(pr.status);
        st->setForeground(QColor(pr.status == "merged"  ? "#a371f7"
                                 : pr.status == "closed" ? "#f85149"
                                                         : "#3fb950"));
        // Badge open PRs whose patch no longer applies to the base with a small
        // conflict icon so the list flags them without opening the detail pane.
        if (m_pullConflictByNumber.value(pr.number, false)) {
            st->setIcon(themedOcticon("alert", QColor("#f85149"), 13));
            st->setToolTip(
                QStringLiteral("This pull request has merge conflicts"));
        }
        m_pullTable->setItem(row, 3, st);
        auto *files = new QTableWidgetItem;
        files->setData(Qt::DisplayRole, pr.filesChanged);
        m_pullTable->setItem(row, 4, files);
        m_pullTable->setItem(row, 5,
                             new QTableWidgetItem(QStringLiteral("+%1 -%2")
                                                      .arg(formatCount(pr.additions))
                                                      .arg(formatCount(pr.deletions))));
        // Author: the node that filed the PR (the submitter for inbox PRs).
        const QString author =
            pr.authorName.trimmed().isEmpty()
                ? (pr.author.isEmpty() ? QString::fromUtf8("\xE2\x80\x94")
                                       : pr.author.left(8))
                : pr.authorName.trimmed();
        auto *authorItem = new QTableWidgetItem(author);
        authorItem->setToolTip(pr.author);
        m_pullTable->setItem(row, 6, authorItem);
        // An agent attached to this PR — by recorded PR number, or through the
        // head branch it ran on (issue #257). Badge the title so the list flags
        // it at a glance, and surface the task's cost.
        const AgentSession *agent = agentSessionForPull(pr.number, pr.head);
        if (agent) {
            titleItem->setIcon(agentStatusOcticon(*agent, 14));
            titleItem->setToolTip(
                QStringLiteral("Agent attached (%1)").arg(
                    agent->prNumber == pr.number
                        ? QStringLiteral("this PR")
                        : QStringLiteral("branch %1").arg(pr.head)));
        } else if (const PullAgentProvenance prov = pullAgentProvenance(pr);
                   prov.isAgent) {
            // No local session (e.g. an agent PR from another node), but the signed
            // commit trailer still attributes authorship (issue #365).
            titleItem->setIcon(themedOcticon("person", QColor("#a371f7"), 14));
            titleItem->setToolTip(
                QStringLiteral("Agent-authored \xE2\x80\x94 %1")
                    .arg(agentProviderName(prov.tool)));
        }
        auto *costItem = new QTableWidgetItem;
        if (agent) {
            costItem->setData(Qt::DisplayRole, agentCostText(agent->costUsd));
            costItem->setData(Qt::UserRole, agent->costUsd);
            costItem->setToolTip(
                QStringLiteral("Estimated cost of the agent task for this PR"));
        } else {
            costItem->setData(Qt::DisplayRole, QString::fromUtf8("\xE2\x80\x94"));
            costItem->setData(Qt::UserRole, 0.0);
        }
        m_pullTable->setItem(row, 7, costItem);

        // Created date: full ISO-ish "yyyy-MM-dd HH:mm" sorts chronologically
        // as plain text and shows the exact moment at a glance; the tooltip
        // carries the friendly "x ago" form. Mirrors the issue list.
        auto *created = new QTableWidgetItem(
            pr.ts > 0
                ? QDateTime::fromMSecsSinceEpoch(pr.ts).toString("yyyy-MM-dd HH:mm")
                : QString());
        created->setToolTip(formatIssueRelativeTime(pr.ts));
        m_pullTable->setItem(row, 8, created);

        // Modified date: the most recent activity on the PR (latest signed
        // event, falling back to the created time).
        qint64 updatedAt = pr.ts;
        for (const PullEvent &ev : pr.events)
            updatedAt = qMax(updatedAt, ev.ts);
        auto *modified = new QTableWidgetItem(
            updatedAt > 0
                ? QDateTime::fromMSecsSinceEpoch(updatedAt).toString("yyyy-MM-dd HH:mm")
                : QString());
        modified->setToolTip(formatIssueRelativeTime(updatedAt));
        m_pullTable->setItem(row, 9, modified);
    }
    m_pullTable->setSortingEnabled(true);
    int selRow = -1;
    for (int r = 0; r < m_pullTable->rowCount(); ++r)
        if (m_pullTable->item(r, 0)->data(Qt::UserRole).toInt() == keep) {
            selRow = r;
            break;
        }
    if (selRow < 0 && m_pullTable->rowCount() > 0)
        selRow = 0;
    if (selRow >= 0)
        m_pullTable->selectRow(selRow);
    else {
        m_currentPullNumber = -1;
        showPull(-1);
    }
}

void MainWindow::renderPullReviewSummary(const PullRequest &pr)
{
    if (!m_pullReviewSummary)
        return;
    if (pr.number <= 0) {
        m_pullReviewSummary->hide();
        return;
    }

    const PullReviewSnapshot snapshot = buildPullReviewSnapshot(pr);
    const auto gate = [](const QString &label, const QString &value,
                         const QString &color, const QString &href) {
        return QStringLiteral(
                   "<a href='%4' style='color:%3;text-decoration:none'>"
                   "<b>%1</b>: %2</a>")
            .arg(label.toHtmlEscaped(), value.toHtmlEscaped(), color, href);
    };

    QString reviewValue = QStringLiteral("Waiting");
    QString reviewColor = QStringLiteral("#8b949e");
    if (snapshot.reviewSummary == QLatin1String("approved")) {
        reviewValue = QStringLiteral("Approved");
        reviewColor = QStringLiteral("#3fb950");
    } else if (snapshot.reviewSummary == QLatin1String("changes_requested")) {
        reviewValue = QStringLiteral("Changes requested");
        reviewColor = QStringLiteral("#f85149");
    }

    int passed = 0, failed = 0, running = 0, pending = 0;
    for (const int id : runIdsForPull(pr)) {
        const ActionRun *run = findRun(id);
        if (!run)
            continue;
        if (run->status == ActionStatus::Success)
            ++passed;
        else if (run->status == ActionStatus::Failed ||
                 run->status == ActionStatus::Rejected ||
                 run->status == ActionStatus::Cancelled)
            ++failed;
        else if (run->status == ActionStatus::Running)
            ++running;
        else
            ++pending;
    }
    const int totalChecks = passed + failed + running + pending;
    QString checksValue = QStringLiteral("Not run");
    QString checksColor = QStringLiteral("#8b949e");
    if (failed > 0) {
        checksValue = QStringLiteral("%1 failed").arg(failed);
        checksColor = QStringLiteral("#f85149");
    } else if (running > 0) {
        checksValue = QStringLiteral("%1 running").arg(running);
        checksColor = QStringLiteral("#58a6ff");
    } else if (pending > 0) {
        checksValue = QStringLiteral("%1 pending").arg(pending);
    } else if (totalChecks > 0) {
        checksValue = QStringLiteral("%1 passed").arg(passed);
        checksColor = QStringLiteral("#3fb950");
    }

    QString mergeValue =
        pr.status == QLatin1String("merged")
            ? QStringLiteral("Merged")
            : pr.status == QLatin1String("closed")
                  ? QStringLiteral("Closed")
                  : QStringLiteral("Open");
    QString mergeColor =
        pr.status == QLatin1String("merged")
            ? QStringLiteral("#a371f7")
            : pr.status == QLatin1String("closed") ? QStringLiteral("#f85149")
                                                    : QStringLiteral("#3fb950");

    const QString threadsValue =
        snapshot.totalThreads == 0
            ? QStringLiteral("No threads")
            : QStringLiteral("%1 unresolved, %2 resolved")
                  .arg(snapshot.unresolvedThreads)
                  .arg(snapshot.resolvedThreads);
    const QString threadsColor =
        snapshot.unresolvedThreads > 0 ? QStringLiteral("#d29922")
                                       : QStringLiteral("#3fb950");
    const int linkedIssues = issuesLinkedFromPull(pr).size();
    const QString linksValue =
        linkedIssues == 0
            ? QStringLiteral("None")
            : QStringLiteral("%1 issue%2")
                  .arg(linkedIssues)
                  .arg(linkedIssues == 1 ? QString() : QStringLiteral("s"));

    const QStringList gates{
        gate(QStringLiteral("Review"), reviewValue, reviewColor,
             QStringLiteral("tab:conversation")),
        gate(QStringLiteral("Checks"), checksValue, checksColor,
             QStringLiteral("tab:checks")),
        gate(QStringLiteral("Merge"), mergeValue, mergeColor,
             QStringLiteral("tab:conversation")),
        gate(QStringLiteral("Threads"), threadsValue, threadsColor,
             QStringLiteral("tab:files")),
        gate(QStringLiteral("Links"), linksValue, QStringLiteral("#58a6ff"),
             QStringLiteral("tab:conversation"))};
    m_pullReviewSummary->setText(
        QStringLiteral("<b>Review summary</b><br>%1")
            .arg(gates.join(QStringLiteral(" &nbsp; "))));
    m_pullReviewSummary->show();
}

void MainWindow::showPull(int number)
{
    const PullRequest *found = nullptr;
    for (const PullRequest &pr : m_currentPulls)
        if (pr.number == number)
            found = &pr;
    m_currentPullNumber = found ? number : -1;
    m_pullFiles->clear();
    m_pullFileDiffs.clear();
    togglePullDiffSearch(false); // opening a different PR clears any find-in-diff state
    m_pullFileAuthorship.clear();
    if (m_pullFileAuthorFilter)
        m_pullFileAuthorFilter->hide();

    if (!found) {
        m_pullTitle->setText("Select a pull request");
        m_pullMeta->clear();
        m_pullDiff->clear();
        m_pullDiffRenderKey.clear(); // widget no longer shows a rendered diff
        if (m_pullCommitsList)
            m_pullCommitsList->clear();
        renderPullThread(PullRequest());
        renderPullChecks(PullRequest());
        renderPullChecksSummary(PullRequest());
        renderPullReviewSummary(PullRequest());
        updatePullSubTabCounts(PullRequest());
        if (m_pullComposer)
            m_pullComposer->setEnabled(false);
        for (QPushButton *b : {m_pullCommentButton, m_pullApproveButton,
                               m_pullRequestChangesButton})
            if (b)
                b->setEnabled(false);
        updatePullActionState();
        return;
    }

    // Keep the detail panel collapsed while "Hide detail" is engaged (issue
    // #274); its contents below still update for when the user reopens it.
    if (m_pullDetail && !m_pullDetailHidden)
        m_pullDetail->show();
    if (m_pullComposer) {
        m_pullComposer->setEnabled(true);
        m_pullComposer->setMentionCandidates(mentionCandidateNames());
    }
    for (QPushButton *b : {m_pullCommentButton, m_pullApproveButton,
                           m_pullRequestChangesButton})
        if (b)
            b->setEnabled(true);
    m_pullTitle->setText(QStringLiteral("#%1  %2").arg(found->number).arg(found->title));
    m_pullMeta->setText(
        QString::fromUtf8("<b>%1</b> \xE2\x86\x90 <b>%2</b> \xC2\xB7 %3 \xC2\xB7 %4 files "
                       "<span style='color:#3fb950'>+%5</span> "
                       "<span style='color:#f85149'>-%6</span> \xC2\xB7 by %7")
            .arg(branchLinkHtml(found->base), branchLinkHtml(found->head),
                 found->status)
            .arg(formatCount(found->filesChanged))
            .arg(formatCount(found->additions))
            .arg(formatCount(found->deletions))
            .arg((found->authorName.isEmpty() ? found->author.left(10)
                                              : found->authorName)
                     .toHtmlEscaped()));
    // Surface where the head branch sits relative to its base: when it trails the
    // base, say by how much so the reviewer knows there's something to pull in
    // before merging (the "Update branch" button merges the base in — issue #72).
    if (found->status == QLatin1String("open")) {
        const PullStore store = pullStoreForCurrentRepo();
        bool behind = false;
        int behindCount = 0;
        if (store.canWrite() &&
            store.isBranchBehindBase(found->number, &behind, nullptr, &behindCount) &&
            behind) {
            m_pullMeta->setText(
                m_pullMeta->text() +
                QString::fromUtf8(" \xC2\xB7 <span style='color:#d29922'>%1 commit%2 "
                                  "behind %3</span>")
                    .arg(behindCount)
                    .arg(behindCount == 1 ? QString() : QStringLiteral("s"),
                         found->base.toHtmlEscaped()));
        }
    }
    const QString review = found->reviewSummary();
    if (review == QLatin1String("approved"))
        m_pullMeta->setText(m_pullMeta->text() +
                            QString::fromUtf8(" \xC2\xB7 <span style='color:#3fb950'>"
                                           "\xE2\x9C\x93 Approved</span>"));
    else if (review == QLatin1String("changes_requested"))
        m_pullMeta->setText(m_pullMeta->text() +
                            QString::fromUtf8(" \xC2\xB7 <span style='color:#f85149'>"
                                           "\xE2\x9A\xA0 Changes requested</span>"));
    // If an agent task produced this PR — by recorded PR number or through its
    // head branch (issue #257) — link the header back to that session on the
    // Agents tab (adhoc #78) and surface its estimated cost.
    if (const AgentSession *agent =
            agentSessionForPull(found->number, found->head)) {
        const QString href = kAgentLinkScheme + QString::number(agent->id);
        const QString link =
            QStringLiteral("<a href=\"%1\" style=\"color:#58a6ff;"
                           "text-decoration:none\">agent</a>")
                .arg(href);
        m_pullMeta->setText(
            m_pullMeta->text() +
            QString::fromUtf8(" \xC2\xB7 %1 cost ~%2")
                .arg(link, agentCostText(agent->costUsd)));
    }
    // The description is shown as the Conversation's opening card (renderPullThread),
    // so it is not repeated in the header.

    // Split the unified diff into per-file sections.
    const PullReviewSnapshot reviewSnapshot = buildPullReviewSnapshot(*found);
    QString currentFile;
    QStringList currentLines;
    const auto flush = [&] {
        if (!currentFile.isEmpty())
            m_pullFileDiffs.insert(currentFile, currentLines.join('\n'));
        currentLines.clear();
    };
    for (const QString &line : found->patch.split('\n')) {
        if (line.startsWith("diff --git ")) {
            flush();
            // "diff --git a/<path> b/<path>"
            currentFile = line.section(" b/", 1);
        }
        if (!currentFile.isEmpty())
            currentLines << line;
    }
    flush();

    // Per-file authorship from the signed commit trailers, so the file list can be
    // filtered by whether an agent touched each file (issue #365).
    m_pullFileAuthorship = pullFileAuthorship(*found);

    for (auto it = m_pullFileDiffs.constBegin(); it != m_pullFileDiffs.constEnd(); ++it) {
        const QString name = it.key().section('/', -1);
        QString label = it.key();
        const auto summaryIt = reviewSnapshot.files.constFind(it.key());
        if (summaryIt != reviewSnapshot.files.constEnd() &&
            summaryIt->unresolvedThreads > 0) {
            label += QStringLiteral("  (%1 unresolved)")
                         .arg(summaryIt->unresolvedThreads);
        }
        auto *item = new QListWidgetItem(iconForFile(name), label);
        item->setData(Qt::UserRole, it.key());
        item->setData(kPullFileAgentRole,
                      m_pullFileAuthorship.value(it.key(), false));
        if (summaryIt != reviewSnapshot.files.constEnd()) {
            item->setToolTip(QStringLiteral("%1 thread(s), %2 unresolved, %3 resolved, %4 suggestion(s)")
                                 .arg(summaryIt->totalThreads)
                                 .arg(summaryIt->unresolvedThreads)
                                 .arg(summaryIt->resolvedThreads)
                                 .arg(summaryIt->suggestions));
        }
        m_pullFiles->addItem(item);
    }
    m_pullFiles->sortItems();
    // Offer the authorship filter only when the PR actually mixes agent and human
    // authorship — otherwise there is nothing to narrow.
    if (m_pullFileAuthorFilter) {
        bool anyAgent = false, anyHuman = false;
        for (auto it = m_pullFileAuthorship.constBegin();
             it != m_pullFileAuthorship.constEnd(); ++it)
            (it.value() ? anyAgent : anyHuman) = true;
        const QSignalBlocker block(m_pullFileAuthorFilter);
        m_pullFileAuthorFilter->setCurrentIndex(0);
        m_pullFileAuthorFilter->setVisible(anyAgent && anyHuman);
    }
    fitFileListToWidestEntry(m_pullFiles); // open wide enough for the longest path
    if (m_pullFiles->count() > 0) {
        // Render every file into the one scrollable view, then select the first
        // file without scrolling the diff (it already starts at the top).
        renderPullDiff();
        m_pullSuppressFileScroll = true;
        m_pullFiles->setCurrentRow(0);
        m_pullSuppressFileScroll = false;
    } else {
        m_pullDiff->setPlainText("(no changes)");
        m_pullDiff->setProperty("fm_diffSource", QString()); // not a rendered diff
        m_pullDiffRenderKey.clear(); // widget no longer shows a rendered diff
        m_pullFileAnchors.clear();
        m_pullFileOrder.clear();
    }
    renderPullCommits(*found);
    renderPullThread(*found);
    renderPullChecks(*found);
    renderPullChecksSummary(*found);
    renderPullReviewSummary(*found);
    updatePullSubTabCounts(*found);
    updatePullActionState();
}

// Show only files matching the selected authorship (issue #365): index 1 keeps
// agent-authored files, 2 keeps human-authored, 0 shows everything.
void MainWindow::applyPullFileAuthorFilter()
{
    if (!m_pullFiles || !m_pullFileAuthorFilter)
        return;
    const int mode = m_pullFileAuthorFilter->currentIndex();
    for (int row = 0; row < m_pullFiles->count(); ++row) {
        QListWidgetItem *item = m_pullFiles->item(row);
        const bool agent = item->data(kPullFileAgentRole).toBool();
        const bool show = mode == 0 || (mode == 1 && agent) || (mode == 2 && !agent);
        item->setHidden(!show);
    }
    // Keep a visible row selected so the diff view follows the filter.
    if (QListWidgetItem *cur = m_pullFiles->currentItem();
        !cur || cur->isHidden()) {
        for (int row = 0; row < m_pullFiles->count(); ++row)
            if (!m_pullFiles->item(row)->isHidden()) {
                m_pullFiles->setCurrentRow(row);
                break;
            }
    }
}

void MainWindow::switchToPullTab(int pullNumber)
{
    if (m_repoDetailTabs && m_repoDetailTabs->button(4))
        m_repoDetailTabs->button(4)->setChecked(true);
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(4);
    m_currentPullNumber = pullNumber;
    reloadPulls();
    if (!m_pullTable)
        return;
    for (int row = 0; row < m_pullTable->rowCount(); ++row) {
        QTableWidgetItem *number = m_pullTable->item(row, 0);
        if (number && number->data(Qt::UserRole).toInt() == pullNumber) {
            m_pullTable->selectRow(row);
            showPull(pullNumber);
            return;
        }
    }
    showPull(pullNumber);
}

// Property holding a diff view's last-set source HTML, so a font-size change can
// re-render it in place at the new size without re-running its renderer (#254).
static const char *kDiffSourceProp = "fm_diffSource";

// Track a diff viewer for the shared text-size zoom: the +/- buttons and
// Ctrl+wheel re-render every registered view at the new size (issue #254).
void MainWindow::registerDiffView(QTextEdit *view)
{
    if (!view || m_diffViews.contains(view))
        return;
    m_diffViews.append(view);
    if (view->toolTip().isEmpty())
        view->setToolTip(QStringLiteral("Ctrl+scroll to change the text size"));
    view->viewport()->installEventFilter(this); // Ctrl+wheel, see eventFilter
    connect(view, &QObject::destroyed, this, [this](QObject *o) {
        m_diffViews.removeAll(static_cast<QTextEdit *>(o));
    });
}

// QTextDocument's rich-text layout is synchronous, GUI-thread work that grows
// super-linearly with document size: a ~2.6M-char commit diff blocked the event
// loop for ~6.7 s (adhoc #112). Above this many chars, cut the rendered diff at
// a file boundary — every file block from the diff renderers ends with
// "</table></div>", so the truncated document stays well-formed — and append a
// notice saying how much was cut.
static QString truncateDiffHtmlForLayout(const QString &html)
{
    constexpr int kMaxChars = 400 * 1000;
    static const QLatin1String fileEnd("</table></div>");
    if (html.size() <= kMaxChars)
        return html;
    int cut = html.lastIndexOf(fileEnd, kMaxChars);
    QString shown;
    if (cut >= 0) {
        shown = html.left(cut + fileEnd.size());
    } else {
        // One giant file with no earlier boundary: cut at a row and close its
        // table/div by hand so the markup stays balanced.
        static const QLatin1String rowEnd("</tr>");
        cut = html.lastIndexOf(rowEnd, kMaxChars);
        if (cut < 0)
            return html; // not the diff renderers' markup; leave it alone
        shown = html.left(cut + rowEnd.size()) + fileEnd;
    }
    // Per-file blocks each open one difftable; the counts tell the reader how
    // many files made it in. (Collapsed "Viewed" files emit no table, so this
    // slightly undercounts them — fine for a notice.)
    static const QLatin1String tableOpen("<table class='difftable'");
    const int shownFiles = shown.count(tableOpen);
    const int totalFiles = html.count(tableOpen);
    shown += QString::fromUtf8(
                 "<p style='color:#8b949e'>&#9888; Diff too large to display in "
                 "full \xE2\x80\x94 showing the first %1 of %2 files. Open the "
                 "remaining files individually or view the diff externally.</p>")
                 .arg(shownFiles)
                 .arg(totalFiles);
    return shown;
}

// Set a diff viewer's HTML, remembering the source so adjustDiffFont can later
// re-render it at a new text size. Use this for every diff viewer's content so
// the zoom works everywhere (issue #254).
void MainWindow::setDiffHtml(QTextEdit *view, const QString &html)
{
    if (!view)
        return;
    // Cap what reaches setHtml so one giant diff can't freeze the window; the
    // capped source is also what adjustDiffFont later re-renders, so zooming
    // never re-pays the full-document layout either.
    const QString shown = truncateDiffHtmlForLayout(html);
    // Rich-text parse + layout runs synchronously on the GUI thread and is the
    // slow half of showing a diff; name it so a stall report points here instead
    // of an anonymous harfbuzz/QTextDocumentLayout backtrace.
    BlockingCallScope crumb(QStringLiteral("diff html layout (%1 chars, %2)")
                                .arg(shown.size())
                                .arg(view->objectName().isEmpty()
                                         ? QStringLiteral("unnamed view")
                                         : view->objectName()));
    view->setProperty(kDiffSourceProp, shown);
    view->document()->setDefaultStyleSheet(diffStyleSheet(m_diffFontPt));
    view->setHtml(shown);
}

// +/- or Ctrl+wheel zoom: change the diff text size and re-render every diff
// viewer that currently shows a diff, in place, at the new size (issue #254).
void MainWindow::adjustDiffFont(int delta)
{
    const int next = qBound(8, m_diffFontPt + delta, 28);
    if (next == m_diffFontPt)
        return;
    m_diffFontPt = next;
    QSettings().setValue(kDiffFontPtSetting, m_diffFontPt);
    const QString css = diffStyleSheet(m_diffFontPt);
    for (QTextEdit *view : m_diffViews) {
        if (!view || view->document()->isEmpty())
            continue;
        const QString src = view->property(kDiffSourceProp).toString();
        if (src.isEmpty())
            continue; // plain text (e.g. "(no changes)") -- nothing to re-scale
        QScrollBar *vbar = view->verticalScrollBar();
        const int scroll = vbar ? vbar->value() : 0;
        view->document()->setDefaultStyleSheet(css);
        view->setHtml(src);
        if (vbar)
            vbar->setValue(scroll);
    }
    m_pullDiffRenderKey.clear(); // the pull view's skip-relayout cache is now stale
    m_scmDiffCache.clear();      // re-render any cached SCM diff at the new size
    // The re-scaled m_pullDiff got a fresh document too; rescan an open find
    // bar's matches against it (issue #333).
    if (m_pullDiffSearchBar && m_pullDiffSearchBar->isVisible())
        pullDiffSearchRecompute();
}

void MainWindow::renderPullDiff()
{
    if (!m_pullDiff)
        return;

    // Collect already-posted review threads for every file, keyed by
    // path\x1fside:line, so the renderer can drop each beneath the line it
    // annotates even though every file now shares one rendered view (#250).
    QHash<QString, QString> notes;
    const PullRequest *pr = nullptr;
    for (const PullRequest &p : m_currentPulls)
        if (p.number == m_currentPullNumber)
            pr = &p;
    if (pr) {
        const auto htmlBody = [](QString text) {
            text = text.toHtmlEscaped();
            text.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
            return text;
        };
        // Whether this node can take a thread's one-click fix right now: the
        // apply commits to the PR's branch, so it needs an open PR and a
        // working tree (adhoc #82).
        const bool canApplyFixes = pr->status == QLatin1String("open") &&
                                   pullStoreForCurrentRepo().canWrite();
        const PullReviewSnapshot snapshot = buildPullReviewSnapshot(*pr);
        for (const PullReviewThread &thread : snapshot.threads) {
            if (thread.lineStart <= 0)
                continue;
            const QString side =
                thread.side.isEmpty() ? QStringLiteral("new") : thread.side;
            const QString key = thread.path + QLatin1Char('\x1f') + side +
                                QStringLiteral(":") +
                                QString::number(thread.lineStart);
            QString note =
                QStringLiteral("<div class='reviewthread'><div class='threadhead'>"
                               "<b>Review thread</b> on %1 line %2 "
                               "<span class='threadstate %3'>%4</span></div>")
                    .arg(side.toHtmlEscaped())
                    .arg(thread.lineStart)
                    .arg(thread.resolved ? QStringLiteral("resolved")
                                         : QStringLiteral("unresolved"),
                         thread.resolved ? QStringLiteral("Resolved")
                                         : QStringLiteral("Unresolved"));
            for (const PullEvent &ev : thread.events) {
                const QString who =
                    ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
                const QString when = formatIssueRelativeTime(ev.ts);
                if (ev.type == QLatin1String("thread-state")) {
                    const QString action =
                        ev.state == QLatin1String("resolved")
                            ? QStringLiteral("resolved this thread")
                            : QStringLiteral("reopened this thread");
                    note += QStringLiteral(
                                "<div class='threadsystem'><b>%1</b> %2 %3</div>")
                                .arg(who.toHtmlEscaped(), action, when);
                    continue;
                }
                if (ev.type == QLatin1String("suggestion-state")) {
                    const QString action =
                        ev.state == QLatin1String("applied")
                            ? QStringLiteral("marked the suggestion applied")
                            : QStringLiteral("updated the suggestion");
                    note += QStringLiteral(
                                "<div class='threadsystem'><b>%1</b> %2 %3</div>")
                                .arg(who.toHtmlEscaped(), action, when);
                    if (!ev.body.isEmpty())
                        note += QStringLiteral("<div class='threadbody'>%1</div>")
                                    .arg(htmlBody(ev.body));
                    continue;
                }
                QString verb = QStringLiteral("commented");
                if (ev.type == QLatin1String("thread-comment"))
                    verb = QStringLiteral("started this thread");
                else if (ev.type == QLatin1String("thread-reply"))
                    verb = QStringLiteral("replied");
                note += QStringLiteral("<div class='threadevent'>"
                                       "<div class='notehdr'><b>%1</b> %2 %3</div>"
                                       "<div class='threadbody'>%4</div>")
                            .arg(who.toHtmlEscaped(), verb, when, htmlBody(ev.body));
                if (!ev.suggestionPatch.isEmpty()) {
                    note += QStringLiteral(
                                "<pre class='suggestion'>%1</pre>")
                                .arg(ev.suggestionPatch.toHtmlEscaped());
                }
                note += QStringLiteral("</div>");
            }
            if (!thread.id.isEmpty() && !thread.id.startsWith(QLatin1String("legacy:"))) {
                note += QStringLiteral("<div class='threadactions'>"
                                       "<a href='thread:reply:%1'>Reply</a>")
                            .arg(thread.id.toHtmlEscaped());
                if (thread.resolved) {
                    note += QStringLiteral(
                        " &nbsp; <a href='thread:unresolve:%1'>Reopen</a>")
                                .arg(thread.id.toHtmlEscaped());
                } else {
                    note += QStringLiteral(
                        " &nbsp; <a href='thread:resolve:%1'>Resolve</a>")
                                .arg(thread.id.toHtmlEscaped());
                }
                // One-click commit of the thread's suggestion patch (adhoc #82).
                if (canApplyFixes && !thread.resolved && thread.hasSuggestion &&
                    thread.suggestionState != QLatin1String("applied"))
                    note += QStringLiteral(" &nbsp; <a href='thread:applyfix:%1'>"
                                           "Apply fix &amp; commit</a>")
                                .arg(thread.id.toHtmlEscaped());
                note += QStringLiteral("</div>");
            }
            note += QStringLiteral("</div>");
            notes[key] += note;
        }
    }

    // Render the whole PR — every changed file — into one scrollable view. The
    // PR patch carries no git object context for image previews, so pass empty
    // dir/base/head (the renderer just lays out the text diff). A non-empty
    // anchorFile turns on the clickable comment gutters for every file.
    QList<DiffFileEntry> files;
    const QSet<QString> viewed =
        loadDiffViewed(QStringLiteral("pull/") + QString::number(m_currentPullNumber));
    const QString fullPatch = pr ? pr->patch : QString();
    const QString html = renderDiffHtml(fullPatch, files, QString(), QString(),
                                        QString(), QStringLiteral("*"), notes, viewed);

    // Map each file path to its "file-N" anchor so the list and Prev/Next can
    // scroll straight to a file's section in the combined view. m_pullFileOrder
    // keeps the same paths in on-screen order for auto-mark-viewed-on-scroll,
    // which needs to know what's above/below the current file.
    m_pullFileAnchors.clear();
    m_pullFileOrder.clear();
    for (const DiffFileEntry &f : files) {
        m_pullFileAnchors.insert(f.path, f.anchor);
        m_pullFileOrder.append(f.path);
    }

    const QString styleSheet = diffStyleSheet(m_diffFontPt);
    // Handing an enormous diff to QTextEdit::setHtml() parses, styles and lays it
    // all out on the GUI thread, freezing the window for seconds (issue #244,
    // same cause as the branch-diff cap in #187). Rendering every file at once
    // raises the ceiling, so cap the combined HTML: past a sane size show a
    // notice instead so opening a huge PR stays responsive. Capping body here
    // also feeds the skip-when-unchanged key below.
    constexpr int kMaxDiffHtmlChars = 3'000'000;
    const QString body =
        html.size() > kMaxDiffHtmlChars
            ? QStringLiteral("<p style='color:#d29922'>This pull request's diff is "
                             "too large to render here (%1 KB). View it in your "
                             "editor.</p>")
                  .arg(fullPatch.size() / 1024)
            : html.isEmpty()
                  ? QStringLiteral("<p style='color:#8b949e'>(no changes)</p>")
                  : html;
    if (html.size() > kMaxDiffHtmlChars) {
        m_pullFileAnchors.clear(); // notice has no per-file anchors to jump to
        m_pullFileOrder.clear();
    }

    // Laying out a large diff's HTML table in QTextDocument can block the GUI
    // thread for a second or more. A background PR refresh re-runs showPull(),
    // which repopulates the file list and re-renders the diff. Skip the
    // re-layout when nothing the document depends on (the PR, theme/font via the
    // stylesheet, split toggle, review notes and viewed state via the html) has
    // changed.
    const QString key = QString::number(m_currentPullNumber) +
                        QLatin1Char('\x1f') + styleSheet + QLatin1Char('\x1f') +
                        body;
    if (key == m_pullDiffRenderKey)
        return;
    m_pullDiffRenderKey = key;

    setDiffHtml(m_pullDiff, body);
    // setDiffHtml just replaced the document, invalidating any cursors an open
    // find bar was holding onto (issue #333) — rescan against the new one.
    if (m_pullDiffSearchBar && m_pullDiffSearchBar->isVisible())
        pullDiffSearchRecompute();
}

// Scroll the all-files diff so the given file's section sits at the top.
void MainWindow::scrollPullDiffToFile(const QString &filePath)
{
    if (!m_pullDiff)
        return;
    const QString anchor = m_pullFileAnchors.value(filePath);
    if (anchor.isEmpty())
        return;
    m_pullDiff->scrollToAnchor(anchor);
}

// Debounced off the diff view's scrollbar (see m_pullAutoViewedDebounce): marks
// every file that has scrolled entirely above the viewport as "Viewed" (only
// while m_pullAutoViewedButton is checked), matching GitHub's "Automatically
// mark files as viewed" toggle. Re-renders once for the whole batch — not per
// file — and then restores the scroll position to whichever file is still on
// screen, since collapsing viewed files above it shifts the document up.
void MainWindow::applyAutoMarkViewedOnScroll()
{
    if (!m_pullAutoViewedButton || !m_pullAutoViewedButton->isChecked())
        return;
    if (!m_pullDiff || m_currentPullNumber < 0 || m_pullFileOrder.isEmpty())
        return;
    QScrollBar *vbar = m_pullDiff->verticalScrollBar();
    if (!vbar)
        return;

    const int viewTop = vbar->value();
    QTextDocument *doc = m_pullDiff->document();
    const int docHeight = doc->documentLayout()->documentSize().height();

    // Absolute document y-position (viewport-independent) of each file's header,
    // in on-screen order; -1 when a file's anchor wasn't found (e.g. dropped from
    // a size-capped render).
    QList<int> tops;
    tops.reserve(m_pullFileOrder.size());
    for (const QString &path : std::as_const(m_pullFileOrder)) {
        const QString anchor = m_pullFileAnchors.value(path);
        QTextCursor cur;
        tops.append(!anchor.isEmpty() && locateAnchorCursor(doc, anchor, cur)
                        ? m_pullDiff->cursorRect(cur).top() + viewTop
                        : -1);
    }

    const QString context =
        QStringLiteral("pull/") + QString::number(m_currentPullNumber);
    const QSet<QString> viewed = loadDiffViewed(context);
    QString currentFile; // first file still at least partly on screen
    QStringList newlyViewed;
    for (int i = 0; i < m_pullFileOrder.size(); ++i) {
        if (tops[i] < 0)
            continue;
        // A file's content runs to the next file's header, or the document end
        // for the last file.
        const int bottom = (i + 1 < tops.size() && tops[i + 1] >= 0) ? tops[i + 1]
                                                                     : docHeight;
        if (bottom <= viewTop) {
            if (!viewed.contains(m_pullFileOrder.at(i)))
                newlyViewed << m_pullFileOrder.at(i);
        } else if (currentFile.isEmpty()) {
            currentFile = m_pullFileOrder.at(i);
        }
    }
    if (newlyViewed.isEmpty())
        return;

    for (const QString &path : std::as_const(newlyViewed))
        setDiffViewed(context, path, true);
    renderPullDiff();
    if (!currentFile.isEmpty())
        scrollPullDiffToFile(currentFile);
}

void MainWindow::pullSelectAdjacentChange(int delta)
{
    // Every file is in one scrollable view, so a change is just the next/previous
    // hunk header anywhere in the PR — pullScrollToAdjacentHunk handles crossing
    // file boundaries on its own.
    pullScrollToAdjacentHunk(delta);
}

bool MainWindow::pullScrollToAdjacentHunk(int delta)
{
    if (!m_pullDiff)
        return false;
    QScrollBar *vbar = m_pullDiff->verticalScrollBar();
    if (!vbar)
        return false;
    // Walk every hunk header — each renders as "@@ -old +new @@ …", so the
    // "@@ -" prefix occurs once per hunk — and jump to the nearest one strictly
    // below (next) or above (prev) the current scroll position. Anchoring on the
    // viewport, not a persisted cursor, keeps Prev/Next consistent after the
    // reviewer scrolls the diff by hand (issue #250).
    const int curTop = vbar->value();
    int target = delta > 0 ? std::numeric_limits<int>::max()
                           : std::numeric_limits<int>::min();
    QTextCursor cur(m_pullDiff->document());
    while (true) {
        cur = m_pullDiff->document()->find(QStringLiteral("@@ -"), cur);
        if (cur.isNull())
            break;
        QTextCursor lineCur(cur);
        lineCur.setPosition(cur.selectionStart());
        lineCur.movePosition(QTextCursor::StartOfLine);
        // cursorRect is in viewport coordinates; add the scroll offset to get the
        // hunk's position within the document.
        const int y = m_pullDiff->cursorRect(lineCur).top() + curTop;
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

// Show or hide the find-in-diff bar (issue #333). Hiding clears both the
// search text and the highlights, so re-opening it always starts fresh.
void MainWindow::togglePullDiffSearch(bool show)
{
    if (!m_pullDiffSearchBar || !m_pullDiffSearchInput)
        return;
    m_pullDiffSearchBar->setVisible(show);
    if (show) {
        m_pullDiffSearchInput->setFocus();
        m_pullDiffSearchInput->selectAll();
    } else {
        m_pullDiffSearchInput->clear(); // triggers pullDiffSearchRecompute to clear highlights
        if (m_pullDiff)
            m_pullDiff->setFocus();
    }
}

// Rebuild m_pullDiff's extra selections from m_pullDiffSearchMatches, painting
// the active match in a brighter color than the rest, and update the "n/m"
// count label.
static void applyPullDiffSearchHighlights(QTextBrowser *diff,
                                          const QList<QTextCursor> &matches,
                                          int activeIndex, QLabel *countLabel,
                                          bool termEmpty)
{
    QList<QTextEdit::ExtraSelection> sels;
    QTextCharFormat matchFmt;
    matchFmt.setBackground(QColor("#e3b341"));
    matchFmt.setForeground(QColor("#0d1117"));
    QTextCharFormat currentFmt;
    currentFmt.setBackground(QColor("#f78166"));
    currentFmt.setForeground(QColor("#0d1117"));
    for (int i = 0; i < matches.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = matches.at(i);
        sel.format = (i == activeIndex) ? currentFmt : matchFmt;
        sels.append(sel);
    }
    diff->setExtraSelections(sels);

    if (!countLabel)
        return;
    countLabel->setText(termEmpty
                            ? QString()
                            : matches.isEmpty()
                                  ? QStringLiteral("No results")
                                  : QStringLiteral("%1/%2")
                                        .arg(activeIndex + 1)
                                        .arg(matches.size()));
}

// Re-scan the combined diff for the current search text and highlight every
// match. Called on every keystroke and after each re-render, since a
// re-render replaces the document and invalidates previously-found cursors.
void MainWindow::pullDiffSearchRecompute()
{
    if (!m_pullDiff)
        return;
    m_pullDiffSearchMatches.clear();
    m_pullDiffSearchIndex = -1;

    const QString term =
        m_pullDiffSearchInput ? m_pullDiffSearchInput->text() : QString();
    if (!term.isEmpty()) {
        QTextCursor cur = m_pullDiff->document()->find(term);
        while (!cur.isNull()) {
            m_pullDiffSearchMatches.append(cur);
            if (m_pullDiffSearchMatches.size() >= 5000)
                break; // safety cap on pathological match counts
            cur = m_pullDiff->document()->find(term, cur);
        }
        if (!m_pullDiffSearchMatches.isEmpty())
            m_pullDiffSearchIndex = 0;
    }

    applyPullDiffSearchHighlights(m_pullDiff, m_pullDiffSearchMatches,
                                  m_pullDiffSearchIndex, m_pullDiffSearchCount,
                                  term.isEmpty());
    if (m_pullDiffSearchIndex >= 0)
        pullDiffSearchGoTo(0);
}

// Step the active match by delta (wrapping), re-highlight, and scroll it into
// view. delta of 0 just scrolls to the current match (used right after a
// recompute).
void MainWindow::pullDiffSearchGoTo(int delta)
{
    if (!m_pullDiff || m_pullDiffSearchMatches.isEmpty())
        return;
    QScrollBar *vbar = m_pullDiff->verticalScrollBar();
    if (!vbar)
        return;

    const int count = m_pullDiffSearchMatches.size();
    m_pullDiffSearchIndex =
        ((m_pullDiffSearchIndex + delta) % count + count) % count;
    applyPullDiffSearchHighlights(m_pullDiff, m_pullDiffSearchMatches,
                                  m_pullDiffSearchIndex, m_pullDiffSearchCount,
                                  false);

    // Same viewport-relative-to-absolute trick as pullScrollToAdjacentHunk:
    // cursorRect is always reported relative to the current viewport, so
    // adding the current scroll offset gives the match's absolute position.
    const QTextCursor &target = m_pullDiffSearchMatches.at(m_pullDiffSearchIndex);
    QTextCursor lineCur(target);
    lineCur.setPosition(target.selectionStart());
    const int y = m_pullDiff->cursorRect(lineCur).top() + vbar->value();
    const int centered = y - m_pullDiff->viewport()->height() / 3;
    vbar->setValue(std::clamp(centered, vbar->minimum(), vbar->maximum()));
}

void MainWindow::onPullDiffAnchorClicked(const QUrl &url)
{
    const QString href = url.toString(QUrl::FullyDecoded);
    if (href.startsWith(QLatin1String("thread:"))) {
        const QStringList parts = href.split(QLatin1Char(':'));
        if (parts.size() < 3)
            return;
        const QString action = parts.at(1);
        const QString threadId = parts.mid(2).join(QStringLiteral(":"));
        if (action == QLatin1String("reply"))
            submitPullThreadReply(threadId);
        else if (action == QLatin1String("resolve"))
            setPullThreadState(threadId, QStringLiteral("resolved"));
        else if (action == QLatin1String("unresolve"))
            setPullThreadState(threadId, QStringLiteral("unresolved"));
        else if (action == QLatin1String("applyfix"))
            applyPullSuggestionFix(threadId);
        return;
    }
    // "viewed:<path>" toggles a file's reviewed state and re-renders the diff,
    // keeping the toggled file in view (its section collapses/expands in place).
    if (url.scheme() == QLatin1String("viewed")) {
        const QString path = url.path();
        const QString context =
            QStringLiteral("pull/") + QString::number(m_currentPullNumber);
        const QSet<QString> cur = loadDiffViewed(context);
        setDiffViewed(context, path, !cur.contains(path));
        renderPullDiff();
        scrollPullDiffToFile(path);
        return;
    }
    // "filecomment:<path>" posts a file-level comment on the pull request.
    if (url.scheme() == QLatin1String("filecomment")) {
        if (m_currentPullNumber < 0)
            return;
        const QString path = url.path();
        bool ok = false;
        const QString body = QInputDialog::getMultiLineText(
            this, QStringLiteral("Comment on %1").arg(path),
            QStringLiteral("Comment"), QString(), &ok);
        if (!ok || body.trimmed().isEmpty())
            return;
        PullStore store = pullStoreForCurrentRepo();
        if (store.canWrite()) {
            QString error;
            if (!store.addLineComment(m_currentPullNumber, path,
                                      QStringLiteral("new"), 0, body.trimmed(),
                                      &error)) {
                QMessageBox::warning(this, "Comment", error);
                return;
            }
        } else {
            PullEvent ev;
            ev.type = QStringLiteral("line-comment");
            ev.path = path;
            ev.side = QStringLiteral("new");
            ev.line = 0;
            ev.body = body.trimmed();
            ev = store.makeSignedEvent(m_currentPullNumber, ev);
            submitPullEventToInbox(m_currentPullNumber, ev);
        }
        reloadPulls();
        showPull(m_currentPullNumber);
        return;
    }
    // Anchor format: "cmt:<path>?s=<side>&l=<line>" (side is old|new). The path
    // is carried in the anchor so the comment lands on the right file even though
    // every file shares one rendered view (issue #250).
    if (url.scheme() != QLatin1String("cmt"))
        return;
    const QString filePath = url.path();
    const QUrlQuery cmtQuery(url);
    const QString side = cmtQuery.queryItemValue(QStringLiteral("s"));
    const int line = cmtQuery.queryItemValue(QStringLiteral("l")).toInt();
    if (m_currentPullNumber < 0 || filePath.isEmpty() || line <= 0)
        return;

    bool ok = false;
    const QString body = QInputDialog::getMultiLineText(
        this, QStringLiteral("Comment on %1:%2").arg(filePath).arg(line),
        QStringLiteral("Comment"), QString(), &ok);
    if (!ok || body.trimmed().isEmpty())
        return;

    PullStore store = pullStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        if (!store.addThreadComment(m_currentPullNumber, filePath, side, line,
                                    line, body.trimmed(), QString(), &error)) {
            QMessageBox::warning(this, "Comment", error);
            return;
        }
    } else {
        PullEvent ev;
        ev.type = QStringLiteral("thread-comment");
        ev.path = filePath;
        ev.side = side;
        ev.lineStart = line;
        ev.lineEnd = line;
        ev.body = body.trimmed();
        ev = store.makeSignedEvent(m_currentPullNumber, ev);
        submitPullEventToInbox(m_currentPullNumber, ev);
    }
    reloadPulls();
    showPull(m_currentPullNumber);
}

void MainWindow::submitPullThreadReply(const QString &threadId)
{
    if (m_currentPullNumber < 0 || threadId.isEmpty())
        return;
    QString parentId;
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (pr.number != m_currentPullNumber)
            continue;
        const PullReviewSnapshot snapshot = buildPullReviewSnapshot(pr);
        for (const PullReviewThread &thread : snapshot.threads) {
            if (thread.id != threadId)
                continue;
            for (int i = thread.events.size() - 1; i >= 0; --i) {
                if (!thread.events.at(i).id.isEmpty()) {
                    parentId = thread.events.at(i).id;
                    break;
                }
            }
            break;
        }
        break;
    }

    bool ok = false;
    const QString body = QInputDialog::getMultiLineText(
        this, QStringLiteral("Reply to review thread"),
        QStringLiteral("Reply"), QString(), &ok);
    if (!ok || body.trimmed().isEmpty())
        return;

    PullStore store = pullStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        if (!store.addThreadReply(m_currentPullNumber, threadId, parentId,
                                  body.trimmed(), &error)) {
            QMessageBox::warning(this, "Reply", error);
            return;
        }
    } else {
        PullEvent ev;
        ev.type = QStringLiteral("thread-reply");
        ev.threadId = threadId;
        ev.parentId = parentId;
        ev.body = body.trimmed();
        ev = store.makeSignedEvent(m_currentPullNumber, ev);
        submitPullEventToInbox(m_currentPullNumber, ev);
    }
    reloadPulls();
    showPull(m_currentPullNumber);
}

void MainWindow::setPullThreadState(const QString &threadId, const QString &state)
{
    if (m_currentPullNumber < 0 || threadId.isEmpty() || state.isEmpty())
        return;
    PullStore store = pullStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        if (!store.setThreadState(m_currentPullNumber, threadId, state,
                                  QString(), &error)) {
            QMessageBox::warning(this, "Thread", error);
            return;
        }
    } else {
        PullEvent ev;
        ev.type = QStringLiteral("thread-state");
        ev.threadId = threadId;
        ev.state = state;
        ev = store.makeSignedEvent(m_currentPullNumber, ev);
        submitPullEventToInbox(m_currentPullNumber, ev);
    }
    reloadPulls();
    showPull(m_currentPullNumber);
}

void MainWindow::addConversationCard(QVBoxLayout *layout, const QString &author,
                                     const QString &headerHtml, const QString &body,
                                     const QString &accent, const QString &copyLink,
                                     const QString &authorId)
{
    if (!layout)
        return;
    const QString who = author.isEmpty() ? QStringLiteral("?") : author;
    auto *row = new QWidget;
    row->setObjectName("issueTimelineRow");
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(14);
    auto *avatar = new QLabel(who.left(2).toUpper());
    avatar->setObjectName("issueAvatar");
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(36, 36);
    // Prefer the author's real picture over the initials tile, matching the
    // issue timeline. Peer avatars are broadcast over chat and cached in
    // m_avatars keyed by the Ed25519 pubkey that also signs events (authorId);
    // our own avatar may not be in that cache yet, so fall back to
    // effectiveAvatar() for our own cards. Authors we have no real picture for
    // (peers and agents whose avatar hasn't been broadcast) get the deterministic
    // procedural face used for contributor and assignee avatars, keyed by their
    // pubkey, so every PR card shows an avatar instead of bare initials.
    QPixmap authorAvatar;
    if (!authorId.isEmpty()) {
        const QPixmap cached = m_avatars.value(authorId);
        if (!cached.isNull())
            authorAvatar = roundedRectPixmap(cached, 36, 36 * 0.28);
        else if (authorId == m_profileIdentity.publicKey())
            authorAvatar = roundedAvatar(effectiveAvatar(), 36);
    }
    if (authorAvatar.isNull()) {
        const QString seed = authorId.isEmpty() ? who.toLower() : authorId;
        authorAvatar = roundedAvatar(forkMeshAvatarPng(seed), 36);
    }
    if (!authorAvatar.isNull()) {
        avatar->setText(QString());
        avatar->setPixmap(authorAvatar);
    }
    rowLayout->addWidget(avatar, 0, Qt::AlignTop);

    auto *card = new QWidget;
    card->setObjectName("issueTimelineCard");
    if (!accent.isEmpty())
        card->setStyleSheet(QStringLiteral("#issueTimelineCard { border-left:3px solid %1; }")
                                .arg(accent));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);
    auto *headerBox = new QWidget(card);
    headerBox->setObjectName("issueTimelineHeader");
    auto *headerRow = new QHBoxLayout(headerBox);
    headerRow->setContentsMargins(16, 8, 10, 8);
    headerRow->setSpacing(8);
    auto *header = new QLabel(headerHtml);
    header->setTextFormat(Qt::RichText);
    headerRow->addWidget(header);
    headerRow->addStretch();
    if (!copyLink.isEmpty() || !body.trimmed().isEmpty()) {
        auto *menu = new QMenu(card);
        if (!copyLink.isEmpty()) {
            QAction *copyLinkAction = menu->addAction("Copy link");
            connect(copyLinkAction, &QAction::triggered, this, [this, copyLink]() {
                QApplication::clipboard()->setText(copyLink);
                flashMessage("Link copied.");
            });
        }
        if (!body.trimmed().isEmpty()) {
            QAction *copyMarkdownAction = menu->addAction("Copy Markdown");
            connect(copyMarkdownAction, &QAction::triggered, this, [this, body]() {
                QApplication::clipboard()->setText(body);
                flashMessage("Markdown copied.");
            });
        }
        auto *actionsButton = new QToolButton(headerBox);
        actionsButton->setObjectName("issueActionButton");
        actionsButton->setText("...");
        actionsButton->setCursor(Qt::PointingHandCursor);
        actionsButton->setPopupMode(QToolButton::InstantPopup);
        actionsButton->setMenu(menu);
        headerRow->addWidget(actionsButton);
    }
    cardLayout->addWidget(headerBox);

    if (!body.trimmed().isEmpty()) {
        auto *bodyLabel = new QLabel;
        bodyLabel->setTextFormat(Qt::MarkdownText);
        bodyLabel->setText(autolinkReferences(body));
        bodyLabel->setWordWrap(true);
        bodyLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
        // Reference links (#N, commit SHAs, forkmesh:// permalinks) resolve in app;
        // real external links fall through to the system browser.
        bodyLabel->setOpenExternalLinks(false);
        connect(bodyLabel, &QLabel::linkActivated, this,
                [this](const QString &href) {
                    // "applyfix:<threadId>" is the review-suggestion quick fix
                    // (adhoc #82); everything else is a normal body reference.
                    if (href.startsWith(QLatin1String("applyfix:")))
                        applyPullSuggestionFix(href.mid(9));
                    else
                        openBodyReference(href);
                });
        bodyLabel->setContentsMargins(16, 12, 16, 14);
        cardLayout->addWidget(bodyLabel);
    }
    rowLayout->addWidget(card, 1);
    // Insert before the trailing stretch.
    layout->insertWidget(layout->count() - 1, row);
}

void MainWindow::renderPullThread(const PullRequest &pr)
{
    if (!m_pullThreadLayout)
        return;
    while (QLayoutItem *item = m_pullThreadLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    // Trailing stretch first: addConversationCard inserts each card just above
    // it, so cards flow oldest-to-newest (top to bottom).
    m_pullThreadLayout->addStretch();
    if (pr.number == 0) {
        if (m_pullLinksValue)
            m_pullLinksValue->hide();
        return;
    }

    // Linked issues card (reflects "closes #N" references and explicit links).
    if (m_pullLinksValue) {
        const QList<int> issues = issuesLinkedFromPull(pr);
        if (issues.isEmpty()) {
            m_pullLinksValue->hide();
        } else {
            QStringList links;
            for (const int n : issues)
                links << QStringLiteral(
                             "<a href='issue:%1' style='color:#58a6ff;"
                             "text-decoration:none'>issue #%1</a>")
                             .arg(n);
            m_pullLinksValue->setText(
                QString::fromUtf8("<b>Linked issues</b> \xC2\xB7 %1")
                    .arg(links.join(QString::fromUtf8(" \xC2\xB7 "))));
            m_pullLinksValue->show();
        }
    }

    // The PR description as the opening card.
    const QString opener = pr.authorName.isEmpty() ? pr.author.left(10) : pr.authorName;
    QString linkOwner = QStringLiteral("repo");
    QString linkRepo = QStringLiteral("pull");
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        linkOwner = m_repositories.at(m_repoDetailIndex).owner;
        linkRepo = m_repositories.at(m_repoDetailIndex).name;
    }
    const QString pullLink =
        QStringLiteral("forkmesh://pull/%1/%2/%3").arg(linkOwner, linkRepo).arg(pr.number);
    addConversationCard(
        m_pullThreadLayout, opener,
        QStringLiteral("<b>%1</b> <span style='color:#8b949e'>opened this pull "
                       "request %2</span>")
            .arg(opener.toHtmlEscaped(), formatIssueRelativeTime(pr.ts)),
        pr.description, QString(), pullLink + QStringLiteral("#open"), pr.author);

    // Threads whose suggestion can still be applied and committed in one click
    // (adhoc #82): unresolved, not yet applied, on an open PR this node can
    // commit to. Their cards get an "Apply fix & commit" action.
    QSet<QString> applicableFixes;
    if (pr.status == QLatin1String("open") &&
        pullStoreForCurrentRepo().canWrite()) {
        const PullReviewSnapshot snapshot = buildPullReviewSnapshot(pr);
        for (const PullReviewThread &thread : snapshot.threads)
            if (!thread.resolved && thread.hasSuggestion &&
                thread.suggestionState != QLatin1String("applied"))
                applicableFixes.insert(thread.id);
    }

    for (const PullEvent &ev : pr.events) {
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        const QString when = formatIssueRelativeTime(ev.ts);
        QString verb = QStringLiteral("commented");
        QString accent;
        QString body = ev.body;
        if (ev.type == QLatin1String("line-comment")) {
            verb = QStringLiteral("commented on <code>%1:%2</code>")
                       .arg(ev.path.toHtmlEscaped())
                       .arg(ev.line);
        } else if (ev.type == QLatin1String("thread-comment")) {
            verb = QStringLiteral("started a review thread on <code>%1:%2</code>")
                       .arg(ev.path.toHtmlEscaped())
                       .arg(ev.lineStart);
            accent = ev.suggestionPatch.isEmpty() ? QStringLiteral("#d29922")
                                                  : QStringLiteral("#58a6ff");
            if (!ev.suggestionPatch.isEmpty()) {
                body += QStringLiteral("\n\n```diff\n%1\n```").arg(ev.suggestionPatch);
                // One-click commit of the suggested fix (adhoc #82); the card's
                // link handler routes "applyfix:" to applyPullSuggestionFix.
                if (applicableFixes.contains(ev.threadId))
                    body += QString::fromUtf8(
                                "\n\n[\xE2\x9A\xA1 Apply fix & commit](applyfix:%1)")
                                .arg(ev.threadId);
            }
        } else if (ev.type == QLatin1String("thread-reply")) {
            verb = QStringLiteral("replied in a review thread");
        } else if (ev.type == QLatin1String("thread-state")) {
            if (ev.state == QLatin1String("resolved")) {
                verb = QStringLiteral("<span style='color:#3fb950'>resolved a "
                                      "review thread</span>");
                accent = QStringLiteral("#3fb950");
            } else {
                verb = QStringLiteral("reopened a review thread");
                accent = QStringLiteral("#d29922");
            }
        } else if (ev.type == QLatin1String("suggestion-state")) {
            verb = ev.state == QLatin1String("applied")
                       ? QStringLiteral("<span style='color:#3fb950'>applied a "
                                        "suggested change</span>")
                       : QStringLiteral("updated a suggested change");
            accent = QStringLiteral("#58a6ff");
        } else if (ev.type == QLatin1String("review")) {
            if (ev.state == QLatin1String("approved")) {
                verb = QStringLiteral("<span style='color:#3fb950'>approved these "
                                      "changes</span>");
                accent = QStringLiteral("#3fb950");
            } else if (ev.state == QLatin1String("changes_requested")) {
                verb = QStringLiteral("<span style='color:#f85149'>requested "
                                      "changes</span>");
                accent = QStringLiteral("#f85149");
            } else {
                verb = QStringLiteral("reviewed");
            }
        }
        // Pass the decorated body — the suggestion diff and its apply link were
        // appended above (passing ev.body here silently dropped them).
        addConversationCard(
            m_pullThreadLayout, who,
            QStringLiteral("<b>%1</b> %2 <span style='color:#8b949e'>%3</span>")
                .arg(who.toHtmlEscaped(), verb, when),
            body, accent,
            pullLink + QStringLiteral("#%1")
                           .arg(ev.id.isEmpty() ? QString::number(ev.ts) : ev.id),
            ev.author);
    }
}

void MainWindow::renderPullCommits(const PullRequest &pr)
{
    if (!m_pullCommitsList)
        return;
    m_pullCommitsList->clear();
    const QString dir = repoGitDir();
    // PRs are patch-based; list the commits on the head branch since the base
    // when both refs resolve in this repo. Otherwise show a single synthetic row.
    bool listed = false;
    // Resolve which repo this PR belongs to, so each commit row can carry the
    // result of any action (CI) runs whose pushed commit matches it (issue #46).
    QString repoOwner, repoName;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        repoOwner = m_repositories.at(m_repoDetailIndex).owner;
        repoName = m_repositories.at(m_repoDetailIndex).name;
    }
    // Most significant action status across the runs for one commit SHA: a failure
    // outranks a run still going, which outranks a queued/pending run, which
    // outranks a plain success. Empty when no run targeted this commit.
    const auto checkStatusFor = [&](const QString &sha) -> QString {
        if (sha.isEmpty() || repoOwner.isEmpty())
            return QString();
        const auto rank = [](const QString &s) {
            if (s == ActionStatus::Failed || s == ActionStatus::Rejected ||
                s == ActionStatus::Cancelled)
                return 4;
            if (s == ActionStatus::Running)
                return 3;
            if (s == ActionStatus::Queued || s == ActionStatus::AwaitingApproval)
                return 2;
            if (s == ActionStatus::Success)
                return 1;
            return 0;
        };
        QString best;
        int bestRank = 0;
        for (const ActionRun &run : std::as_const(m_actionRuns)) {
            if (run.owner != repoOwner || run.name != repoName || run.commit != sha)
                continue;
            if (const int r = rank(run.status); r > bestRank) {
                bestRank = r;
                best = run.status;
            }
        }
        return best;
    };
    // Leading glyph that conveys a commit's check result at a glance.
    const auto checkGlyph = [](const QString &status) -> QString {
        if (status == ActionStatus::Success)
            return QString::fromUtf8("\xE2\x9C\x93 "); // check mark
        if (status == ActionStatus::Failed || status == ActionStatus::Rejected ||
            status == ActionStatus::Cancelled)
            return QString::fromUtf8("\xE2\x9C\x97 "); // ballot X
        if (status == ActionStatus::Running)
            return QString::fromUtf8("\xE2\x97\x8F "); // filled circle
        if (status == ActionStatus::Queued || status == ActionStatus::AwaitingApproval)
            return QString::fromUtf8("\xE2\x97\x8B "); // hollow circle
        return QString();
    };
    if (!dir.isEmpty() && !pr.base.isEmpty() && !pr.head.isEmpty()) {
        QByteArray out;
        if (runGitCapture(dir,
                          {"log", "--no-merges", "--date=format:%Y-%m-%d %H:%M",
                           "--pretty=%H\x1f%h\x1f%s\x1f%an\x1f%ad\x1f%ct\x1f"
                           "%(trailers:key=ForkMesh-Agent,valueonly,separator=%x2C)",
                           pr.base + ".." + pr.head},
                          &out, nullptr) &&
            !out.trimmed().isEmpty()) {
            for (const QString &line :
                 QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts)) {
                const QStringList f = line.split(QLatin1Char('\x1f'));
                if (f.size() < 6)
                    continue;
                const QString &sha = f.at(0);
                // Relative "x ago" from the committer timestamp, alongside the
                // absolute date+time (issue #275) the row already carried.
                const QString rel = formatShortRelativeTime(f.at(5).toLongLong());
                const QString status = checkStatusFor(sha);
                QString text = checkGlyph(status);
                text += QString::fromUtf8("%1  %2 \xC2\xB7 %3 \xC2\xB7 %4")
                            .arg(f.at(1), f.at(2), f.at(3), f.at(4));
                if (!rel.isEmpty())
                    text += QString::fromUtf8(" \xC2\xB7 %1 ago").arg(rel);
                auto *item = new QListWidgetItem(text);
                item->setData(kCommitShaRole, sha);
                item->setData(kCommitMessageRole, f.at(2));
                // Tooltip: full SHA, author + full timestamp, and the check result.
                QString tip = QString::fromUtf8("%1\n%2 committed %3")
                                  .arg(sha, f.at(3), f.at(4));
                if (!rel.isEmpty())
                    tip += QString::fromUtf8(" (%1 ago)").arg(rel);
                if (!status.isEmpty())
                    tip += QString::fromUtf8("\nChecks: %1").arg(actionStatusText(status));
                // Agent-authored commit: the ForkMesh-Agent trailer (issue #365).
                const QString agentTrailer = f.size() > 6 ? f.at(6).trimmed() : QString();
                if (!agentTrailer.isEmpty()) {
                    item->setIcon(themedOcticon("person", QColor("#a371f7"), 14));
                    tip += QString::fromUtf8("\nAgent-authored: %1").arg(agentTrailer);
                }
                item->setToolTip(tip);
                m_pullCommitsList->addItem(item);
                listed = true;
            }
        }
    }
    // Cross-node fallback: the head ref isn't present on this node (the log above
    // found nothing), but the signed format-patch mbox carries every commit with
    // its original author/date/subject — parse those so attribution still shows.
    if (!listed && !pr.commits.isEmpty()) {
        static const QRegularExpression boundary(
            QStringLiteral("^From ([0-9a-f]{7,40}) "));
        static const QRegularExpression patchTag(
            QStringLiteral("^\\[PATCH[^\\]]*\\]\\s*"));
        QString author, subject, date, sha, agentTrailer;
        bool inHeaders = false;
        const auto flush = [&] {
            if (subject.isEmpty() && author.isEmpty())
                return;
            // The mbox Date: header is RFC 2822; reformat to a compact date+time
            // so the row carries it like the local-log path above (issue #275),
            // falling back to the raw header if it doesn't parse.
            QString when = date;
            qint64 committedSecs = 0;
            const QDateTime dt = QDateTime::fromString(date, Qt::RFC2822Date);
            if (dt.isValid()) {
                when = dt.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
                committedSecs = dt.toSecsSinceEpoch();
            }
            const QString rel =
                committedSecs > 0 ? formatShortRelativeTime(committedSecs) : QString();
            const QString subj =
                subject.isEmpty() ? QStringLiteral("(no subject)") : subject;
            const QString auth = author.isEmpty() ? QStringLiteral("unknown") : author;
            const QString status = checkStatusFor(sha);
            QString text = checkGlyph(status);
            text += subj + QString::fromUtf8(" \xC2\xB7 ") + auth;
            if (!when.isEmpty())
                text += QString::fromUtf8(" \xC2\xB7 ") + when;
            if (!rel.isEmpty())
                text += QString::fromUtf8(" \xC2\xB7 %1 ago").arg(rel);
            auto *item = new QListWidgetItem(text);
            // The commit isn't on this node, so the row can't open it — but the
            // signed mbox still carries its SHA, so right-click can copy it.
            if (!sha.isEmpty())
                item->setData(kCommitCopyShaRole, sha);
            item->setData(kCommitMessageRole, subj);
            QString tip = sha.isEmpty() ? QString() : sha + QLatin1Char('\n');
            tip += QString::fromUtf8("%1 committed %2")
                       .arg(auth, when.isEmpty() ? date : when);
            if (!rel.isEmpty())
                tip += QString::fromUtf8(" (%1 ago)").arg(rel);
            if (!status.isEmpty())
                tip += QString::fromUtf8("\nChecks: %1").arg(actionStatusText(status));
            if (!agentTrailer.isEmpty()) {
                item->setIcon(themedOcticon("person", QColor("#a371f7"), 14));
                tip += QString::fromUtf8("\nAgent-authored: %1").arg(agentTrailer);
            }
            item->setToolTip(tip);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            m_pullCommitsList->addItem(item);
            listed = true;
            author.clear();
            subject.clear();
            date.clear();
            sha.clear();
            agentTrailer.clear();
        };
        for (const QString &line : pr.commits.split('\n')) {
            if (const QRegularExpressionMatch m = boundary.match(line);
                m.hasMatch()) {
                flush();
                sha = m.captured(1);
                inHeaders = true;
                continue;
            }
            if (!inHeaders) {
                // Commit-message body: catch the provenance trailer (issue #365).
                if (line.startsWith(QLatin1String("ForkMesh-Agent:")))
                    agentTrailer = line.mid(15).trimmed();
                continue;
            }
            if (line.isEmpty()) { // blank line ends the header block
                inHeaders = false;
            } else if (line.startsWith(QLatin1String("From: "))) {
                author = line.mid(6).section(QLatin1String(" <"), 0, 0).trimmed();
            } else if (line.startsWith(QLatin1String("Date: "))) {
                date = line.mid(6).trimmed();
            } else if (line.startsWith(QLatin1String("Subject: "))) {
                subject = line.mid(9).trimmed();
                subject.remove(patchTag);
            }
        }
        flush();
    }
    if (!listed) {
        auto *item = new QListWidgetItem(
            QString::fromUtf8("%1 file(s) changed \xC2\xB7 +%2 -%3")
                .arg(formatCount(pr.filesChanged))
                .arg(formatCount(pr.additions))
                .arg(formatCount(pr.deletions)));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        m_pullCommitsList->addItem(item);
    }
}

// Full commit hashes that make up this PR (base..head), when both refs resolve
// in the open repo. Used to tie action runs to the PR and to count commits.
QStringList MainWindow::pullCommitShas(const PullRequest &pr) const
{
    QStringList shas;
    const QString dir = repoGitDir();
    if (dir.isEmpty() || pr.base.isEmpty() || pr.head.isEmpty())
        return shas;
    QByteArray out;
    if (runGitCapture(dir, {"log", "--no-merges", "--pretty=%H",
                            pr.base + ".." + pr.head},
                      &out, nullptr))
        shas = QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
    return shas;
}

// Ids of action runs whose pushed commit belongs to this PR (any of its commits
// or its resolved head tip), scoped to the open repo's owner/name. Newest first,
// matching m_actionRuns ordering.
QList<int> MainWindow::runIdsForPull(const PullRequest &pr) const
{
    QList<int> ids;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return ids;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QStringList commitShas = pullCommitShas(pr);
    QSet<QString> shas(commitShas.cbegin(), commitShas.cend());
    // Also include the head tip in case base..head couldn't be enumerated.
    const QString dir = repoGitDir();
    if (!dir.isEmpty() && !pr.head.isEmpty()) {
        QByteArray tip;
        if (runGitCapture(dir, {"rev-parse", pr.head}, &tip, nullptr))
            shas.insert(QString::fromUtf8(tip).trimmed());
    }
    if (shas.isEmpty())
        return ids;
    for (const ActionRun &run : std::as_const(m_actionRuns)) {
        if (run.owner == repo.owner && run.name == repo.name &&
            shas.contains(run.commit))
            ids.append(run.id);
    }
    return ids;
}

void MainWindow::renderPullChecks(const PullRequest &pr)
{
    if (!m_pullChecksTable)
        return;
    TableRepaintGuard repaintGuard(m_pullChecksTable);
    m_pullChecksTable->setRowCount(0);
    if (m_pullRunChecksButton)
        m_pullRunChecksButton->setEnabled(pr.number > 0 && pr.status == "open");
    if (pr.number <= 0) {
        if (m_pullChecksLog)
            m_pullChecksLog->clear();
        return;
    }
    // Preserve the user's selected run across live refreshes.
    int keepRunId = -1;
    if (const QModelIndexList sel = m_pullChecksTable->selectionModel()->selectedRows();
        !sel.isEmpty())
        if (QTableWidgetItem *it = m_pullChecksTable->item(sel.first().row(), 0))
            keepRunId = it->data(Qt::UserRole).toInt();
    const QList<int> ids = runIdsForPull(pr);
    for (const int id : ids) {
        const ActionRun *run = findRun(id);
        if (!run)
            continue;
        const int row = m_pullChecksTable->rowCount();
        m_pullChecksTable->insertRow(row);
        auto *status = new QTableWidgetItem(actionStatusText(run->status));
        status->setForeground(actionStatusColor(run->status));
        status->setData(Qt::UserRole, run->id);
        m_pullChecksTable->setItem(row, 0, status);
        m_pullChecksTable->setItem(
            row, 1, new QTableWidgetItem(run->workflowName.isEmpty()
                                             ? run->workflowPath
                                             : run->workflowName));
        m_pullChecksTable->setItem(row, 2, new QTableWidgetItem(run->commit.left(8)));
        const qint64 dur = run->finishedAtMs > run->startedAtMs && run->startedAtMs > 0
                               ? run->finishedAtMs - run->startedAtMs
                               : 0;
        m_pullChecksTable->setItem(
            row, 3,
            new QTableWidgetItem(dur > 0 ? formatDuration(dur)
                                         : QString::fromUtf8("\xE2\x80\x94")));
    }
    if (m_pullChecksTable->rowCount() > 0) {
        int keepRow = 0;
        if (keepRunId >= 0)
            for (int r = 0; r < m_pullChecksTable->rowCount(); ++r)
                if (m_pullChecksTable->item(r, 0)->data(Qt::UserRole).toInt() == keepRunId) {
                    keepRow = r;
                    break;
                }
        m_pullChecksTable->selectRow(keepRow);
    } else if (m_pullChecksLog)
        m_pullChecksLog->setPlainText(
            "No checks have run for this pull request yet. Use \"Run checks against "
            "this PR\" to queue this repository's push workflows.");
}

// Compact pass/fail/running line shown inline at the end of the Conversation,
// with a link that jumps to the Checks tab. Hidden when there are no runs.
void MainWindow::renderPullChecksSummary(const PullRequest &pr)
{
    if (!m_pullChecksSummary)
        return;
    if (pr.number <= 0) {
        m_pullChecksSummary->hide();
        return;
    }
    int passed = 0, failed = 0, running = 0, pending = 0;
    for (const int id : runIdsForPull(pr)) {
        const ActionRun *run = findRun(id);
        if (!run)
            continue;
        if (run->status == ActionStatus::Success)
            ++passed;
        else if (run->status == ActionStatus::Failed ||
                 run->status == ActionStatus::Rejected ||
                 run->status == ActionStatus::Cancelled)
            ++failed;
        else if (run->status == ActionStatus::Running)
            ++running;
        else
            ++pending; // queued / awaiting approval
    }
    const int total = passed + failed + running + pending;
    if (total == 0) {
        m_pullChecksSummary->hide();
        return;
    }
    QStringList parts;
    if (passed)
        parts << QString::fromUtf8("<span style='color:#3fb950'>\xE2\x9C\x93 %1 passed</span>")
                     .arg(passed);
    if (failed)
        parts << QString::fromUtf8("<span style='color:#f85149'>\xE2\x9C\x97 %1 failed</span>")
                     .arg(failed);
    if (running)
        parts << QString::fromUtf8("<span style='color:#58a6ff'>\xE2\x97\x8F %1 running</span>")
                     .arg(running);
    if (pending)
        parts << QStringLiteral("<span style='color:#8b949e'>%1 pending</span>").arg(pending);
    m_pullChecksSummary->setText(
        QString::fromUtf8("<b>Checks</b> \xC2\xB7 %1 \xC2\xB7 <a href='#checks' "
                       "style='color:#58a6ff;text-decoration:none'>details</a>")
            .arg(parts.join(QString::fromUtf8(" \xC2\xB7 "))));
    m_pullChecksSummary->show();
}

void MainWindow::showPullCheckLog(int runId)
{
    if (!m_pullChecksLog)
        return;
    const ActionRun *run = findRun(runId);
    if (!run) {
        m_pullChecksLog->clear();
        return;
    }
    m_pullChecksLog->setPlainText(m_actionStore ? m_actionStore->readLog(*run)
                                                : QString());
    m_pullChecksLog->moveCursor(QTextCursor::End);
}

void MainWindow::runChecksForCurrentPull()
{
    if (m_currentPullNumber < 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const PullRequest *pr = nullptr;
    for (const PullRequest &p : std::as_const(m_currentPulls))
        if (p.number == m_currentPullNumber)
            pr = &p;
    if (!pr || pr->head.isEmpty())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    // Resolve the PR head to a concrete commit the runner can check out.
    const QString dir = repoGitDir();
    QByteArray tip;
    if (dir.isEmpty() ||
        !runGitCapture(dir, {"rev-parse", pr->head}, &tip, nullptr) ||
        tip.trimmed().isEmpty()) {
        setRepoDetailNotice(
            "Could not resolve the pull request's head commit to run checks.", true);
        return;
    }
    queueWorkflowsForCommit(m_repoDetailIndex, repo.owner, repo.name,
                            QString::fromUtf8(tip).trimmed(),
                            QStringLiteral("refs/heads/") + pr->head);
    renderPullChecks(*pr);
    renderPullChecksSummary(*pr);
    renderPullReviewSummary(*pr);
    updatePullSubTabCounts(*pr);
}

void MainWindow::buildAndPreviewCurrentPull()
{
    if (m_currentPullNumber < 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const PullRequest *pr = nullptr;
    for (const PullRequest &p : std::as_const(m_currentPulls))
        if (p.number == m_currentPullNumber)
            pr = &p;
    if (!pr || pr->head.isEmpty()) {
        setRepoDetailNotice("This pull request has no head branch to build.", true);
        return;
    }
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    const QString gitDir = repoGitDir();
    if (gitDir.isEmpty()) {
        setRepoDetailNotice("No local copy of this repository to build from.", true);
        return;
    }
    // Resolve the PR head to a concrete commit (as runChecksForCurrentPull does)
    // so the worktree is checked out at exactly what the PR proposes.
    QByteArray tip;
    if (!runGitCapture(gitDir, {QStringLiteral("rev-parse"), pr->head}, &tip,
                       nullptr) ||
        tip.trimmed().isEmpty()) {
        setRepoDetailNotice(
            "Could not resolve this pull request's head commit to build it.", true);
        return;
    }
    const QString commit = QString::fromUtf8(tip).trimmed();
    const int number = pr->number;

    // A stable per-PR worktree under temp, reused across rebuilds so the CMake
    // build directory (untracked, so a plain checkout never disturbs it) survives
    // and later previews build incrementally.
    QString slug = repo.owner + QLatin1Char('-') + repo.name;
    slug.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                 QStringLiteral("_"));
    const QString previewDir =
        QDir::tempPath() + QStringLiteral("/forkmesh-pr-preview/%1-pr%2")
                               .arg(slug).arg(number);
    const QString clientDir = previewDir + QStringLiteral("/qt_client");
    const QString buildDir = clientDir + QStringLiteral("/build");
    const bool haveWorktree = QFileInfo::exists(previewDir + QStringLiteral("/.git"));

    // A live build-log dialog (one at a time; replace any previous run's window).
    if (m_pullPreviewDialog) {
        m_pullPreviewDialog->deleteLater();
        m_pullPreviewDialog = nullptr;
    }
    auto *dialog = new QDialog(this);
    m_pullPreviewDialog = dialog;
    // Closing the window cancels the in-flight build (its QProcess children are
    // parented to the dialog) and clears our handle to it.
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QObject::destroyed, this, [this] { m_pullPreviewDialog = nullptr; });
    dialog->setWindowTitle(
        QStringLiteral("Build & preview pull request #%1").arg(number));
    dialog->resize(760, 480);
    auto *status = new QLabel(QString::fromUtf8("Preparing\xE2\x80\xA6"), dialog);
    status->setObjectName("statusLine");
    status->setWordWrap(true);
    auto *log = new QPlainTextEdit(dialog);
    log->setReadOnly(true);
    log->setObjectName("codeEditor");
    log->setLineWrapMode(QPlainTextEdit::NoWrap);
    applyLogFont(log);
    auto *closeBtn = new QPushButton(QStringLiteral("Close"), dialog);
    closeBtn->setObjectName("ghostButton");
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::close);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addStretch();
    buttonRow->addWidget(closeBtn);
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(status);
    layout->addWidget(log, 1);
    layout->addLayout(buttonRow);
    dialog->show();

    QPointer<QDialog> dlg(dialog);
    QPointer<QLabel> statusPtr(status);
    QPointer<QPlainTextEdit> logPtr(log);
    auto appendLog = [logPtr](const QString &text) {
        if (!logPtr)
            return;
        logPtr->moveCursor(QTextCursor::End);
        logPtr->insertPlainText(text);
        logPtr->moveCursor(QTextCursor::End);
    };

    // What to do once the build succeeds: launch the freshly built binary as an
    // isolated node (its own XDG dirs) so the preview never touches the running
    // app's identity, repos or settings.
    auto launchPreview = [this, dlg, statusPtr, appendLog, buildDir, previewDir,
                          number] {
        const QString binary = builtExecutablePath(buildDir);
        if (!QFileInfo::exists(binary)) {
            if (statusPtr)
                statusPtr->setText(
                    QStringLiteral("Build finished but the binary was not found at %1.")
                        .arg(binary));
            return;
        }
        const QString sandbox = previewDir + QStringLiteral("/preview-home");
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_DATA_HOME"), sandbox + QStringLiteral("/data"));
        env.insert(QStringLiteral("XDG_CONFIG_HOME"),
                   sandbox + QStringLiteral("/config"));
        env.insert(QStringLiteral("XDG_CACHE_HOME"), sandbox + QStringLiteral("/cache"));
        env.insert(QStringLiteral("XDG_STATE_HOME"), sandbox + QStringLiteral("/state"));
        QProcess launcher;
        launcher.setProgram(binary);
        launcher.setProcessEnvironment(env);
        launcher.setWorkingDirectory(QFileInfo(binary).absolutePath());
        appendLog(QString::fromUtf8("\n\xE2\x86\x92 launching %1\n").arg(binary));
        if (launcher.startDetached()) {
            if (statusPtr)
                statusPtr->setText(
                    QStringLiteral("Launched the ForkMesh preview for pull request "
                                   "#%1.").arg(number));
            flashMessage(QStringLiteral("Launched preview of pull request #%1.")
                             .arg(number));
        } else if (statusPtr) {
            statusPtr->setText(QStringLiteral("Could not launch %1.").arg(binary));
        }
    };

    // A fresh worktree needs its parent dir clearing first (its registration is
    // dropped by the `worktree prune` step the pipeline opens with).
    if (!haveWorktree) {
        QDir(previewDir).removeRecursively(); // clear any stale, unregistered dir
        QDir().mkpath(QFileInfo(previewDir).absolutePath());
    }
    // Sequential build pipeline streamed into the dialog. Captured by a shared
    // recursive lambda so each step starts the next only on success.
    auto steps = std::make_shared<QList<PullPreviewStep>>(
        pullPreviewSteps(gitDir, previewDir, clientDir, buildDir, commit,
                         haveWorktree, QThread::idealThreadCount()));

    auto runNext = std::make_shared<std::function<void(int)>>();
    *runNext = [this, steps, runNext, dlg, statusPtr, appendLog,
                launchPreview](int index) {
        if (!dlg)
            return; // dialog closed — abandon the build
        if (index >= steps->size()) {
            launchPreview();
            return;
        }
        const PullPreviewStep st = steps->at(index);
        if (statusPtr)
            statusPtr->setText(st.status);
        appendLog(QStringLiteral("\n$ %1 %2\n  (in %3)\n")
                      .arg(st.program, st.args.join(QLatin1Char(' ')), st.dir));
        auto *proc = new QProcess(dlg);
        proc->setWorkingDirectory(st.dir);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, &QProcess::readyReadStandardOutput, this, [proc, appendLog] {
            appendLog(QString::fromUtf8(proc->readAllStandardOutput()));
        });
        connect(proc, &QProcess::finished, this,
                [proc, runNext, index, appendLog, statusPtr](
                    int code, QProcess::ExitStatus exitStatus) {
                    appendLog(QString::fromUtf8(proc->readAllStandardOutput()));
                    proc->deleteLater();
                    if (exitStatus != QProcess::NormalExit || code != 0) {
                        if (statusPtr)
                            statusPtr->setText(
                                QStringLiteral("Build failed (exit %1). See the log "
                                               "above.").arg(code));
                        return;
                    }
                    (*runNext)(index + 1);
                });
        connect(proc, &QProcess::errorOccurred, this,
                [proc, statusPtr](QProcess::ProcessError) {
                    if (statusPtr && proc->state() != QProcess::Running)
                        statusPtr->setText(
                            QString::fromUtf8("Could not run %1 \xE2\x80\x94 is it "
                                              "installed?").arg(proc->program()));
                });
        proc->start(st.program, st.args);
    };
    (*runNext)(0);
}

void MainWindow::updatePullSubTabCounts(const PullRequest &pr)
{
    const auto label = [](QPushButton *b, const QString &name, int n) {
        if (b)
            b->setText(n > 0 ? QStringLiteral("%1 %2").arg(name).arg(n) : name);
    };
    if (pr.number <= 0) {
        label(m_pullTabConversation, QStringLiteral("Conversation"), 0);
        label(m_pullTabCommits, QStringLiteral("Commits"), 0);
        label(m_pullTabChecks, QStringLiteral("Checks"), 0);
        label(m_pullTabFiles, QStringLiteral("Files changed"), 0);
        return;
    }
    const PullReviewSnapshot snapshot = buildPullReviewSnapshot(pr);
    label(m_pullTabConversation, QStringLiteral("Conversation"),
          snapshot.topLevelItems + snapshot.totalThreads);
    label(m_pullTabCommits, QStringLiteral("Commits"), pullCommitShas(pr).size());
    label(m_pullTabChecks, QStringLiteral("Checks"), runIdsForPull(pr).size());
    label(m_pullTabFiles, QStringLiteral("Files changed"), pr.filesChanged);
}

void MainWindow::submitPullComment()
{
    if (m_currentPullNumber < 0 || !m_pullComposer)
        return;
    const QString body = m_pullComposer->markdown().trimmed();
    if (body.isEmpty()) {
        flashMessage(QStringLiteral("Write a comment first."));
        return;
    }
    PullStore store = pullStoreForCurrentRepo();
    PullEvent ev;
    ev.type = QStringLiteral("comment");
    ev.body = body;
    ev = store.makeSignedEvent(m_currentPullNumber, ev);
    QString error;
    if (store.canWrite()) {
        if (!store.addComment(m_currentPullNumber, body, &error)) {
            QMessageBox::warning(this, "Comment", error);
            return;
        }
    } else {
        submitPullEventToInbox(m_currentPullNumber, ev);
    }
    m_pullComposer->setMarkdown(QString());
    reloadPulls();
    showPull(m_currentPullNumber);
}

void MainWindow::sendPullRevisionToAgent()
{
    if (m_currentPullNumber < 0 || !m_pullAgentRevisionEdit || !m_agentStore)
        return;
    const QString feedback = m_pullAgentRevisionEdit->text().trimmed();
    if (feedback.isEmpty()) {
        flashMessage(QStringLiteral("Enter revision feedback first."));
        return;
    }

    // Find the agent session linked to this PR — by PR number or through its head
    // branch (issue #257). Resolve via the shared helper, then re-find the mutable
    // session so it can be re-queued.
    QString head;
    for (const PullRequest &pr : m_currentPulls)
        if (pr.number == m_currentPullNumber) {
            head = pr.head;
            break;
        }
    const AgentSession *linked = agentSessionForPull(m_currentPullNumber, head);
    AgentSession *session = linked ? findAgentSession(linked->id) : nullptr;
    if (!session) {
        flashMessage(QStringLiteral("No agent session found for this pull request."),
                     true);
        return;
    }

    // Post the feedback as a PR comment so it appears in the thread.
    PullStore store = pullStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        store.addComment(m_currentPullNumber, feedback, &error);
    }

    // Append the revision note to the agent log and re-queue.
    m_agentStore->appendLog(
        *session,
        QStringLiteral("\n==> Revision feedback from PR #%1:\n%2")
            .arg(m_currentPullNumber)
            .arg(feedback));
    session->status = AgentStatus::Queued;
    session->lastError.clear();
    session->finishedAtMs = 0;
    m_agentStore->saveSession(*session);

    const int sessionId = session->id;
    if (!m_agentQueue.contains(sessionId))
        m_agentQueue.append(sessionId);

    m_pullAgentRevisionEdit->clear();
    reloadAgents();
    reloadPulls();
    showPull(m_currentPullNumber);
    flashMessage(QStringLiteral("Revision sent to agent session #%1.").arg(sessionId));
    processAgentQueue();
}

void MainWindow::submitPullReview(const QString &state)
{
    if (m_currentPullNumber < 0 || !m_pullComposer)
        return;
    const QString body = m_pullComposer->markdown().trimmed();
    PullStore store = pullStoreForCurrentRepo();
    PullEvent ev;
    ev.type = QStringLiteral("review");
    ev.state = state;
    ev.body = body;
    ev = store.makeSignedEvent(m_currentPullNumber, ev);
    QString error;
    if (store.canWrite()) {
        if (!store.addReview(m_currentPullNumber, state, body, &error)) {
            QMessageBox::warning(this, "Review", error);
            return;
        }
    } else {
        submitPullEventToInbox(m_currentPullNumber, ev);
    }
    m_pullComposer->setMarkdown(QString());
    reloadPulls();
    showPull(m_currentPullNumber);
}

void MainWindow::updatePullActionState()
{
    const PullStore store = pullStoreForCurrentRepo();
    const bool writable = store.canWrite();
    const bool have = m_currentPullNumber >= 0;
    bool open = false;
    bool closed = false;
    bool merged = false;
    QString head;
    QString base;
    QString patch;
    QString reviewSummary;
    for (const PullRequest &pr : m_currentPulls) {
        if (pr.number == m_currentPullNumber) {
            open   = pr.status == "open";
            closed = pr.status == "closed";
            merged = pr.status == "merged";
            head   = pr.head;
            base   = pr.base;
            patch  = pr.patch;
            reviewSummary = pr.reviewSummary();
        }
    }
    const bool mergeable = writable && have && open;
    // An unresolved "request changes" review holds the merge: a human reviewer's
    // objection gates the button until it's approved (or the review cleared) —
    // just as a failed check would, but for review state (issue #359).
    const bool reviewBlocks =
        reviewSummary == QLatin1String("changes_requested");
    bool behind = false;
    if (mergeable)
        store.isBranchBehindBase(m_currentPullNumber, &behind);
    // Dry-run the patch so the reviewer sees conflicts before merging. reloadPulls()
    // already ran this apply for every open PR and cached the result, so reuse the
    // cached entry for the current PR instead of re-spawning `git apply --check`
    // here (that synchronous re-check blocked the UI for ~2s on every selection).
    // On a cache miss don't run the dry-run inline — that's the slow call that
    // froze the GUI. Queue it for the async pass and show a neutral "checking"
    // state; processPendingPullConflicts() re-runs us once the result lands.
    bool mergeClean = true;
    bool conflictPending = false;
    QStringList conflictFiles;
    if (mergeable) {
        const QString fingerprint = pullPatchFingerprint(patch);
        const auto cached = m_pullConflictCache.constFind(m_currentPullNumber);
        if (cached != m_pullConflictCache.constEnd() &&
            cached->fingerprint == fingerprint) {
            mergeClean = !cached->conflict;
            conflictFiles = cached->conflictFiles;
        } else {
            conflictPending = true;
            queuePullConflictCheck(m_currentPullNumber, fingerprint);
        }
    }
    if (m_pullMergeStatus) {
        if (!mergeable) {
            m_pullMergeStatus->hide();
        } else if (conflictPending) {
            m_pullMergeStatus->setText(QString::fromUtf8(
                "<span style='color:#8b949e'>Checking for conflicts\xE2\x80\xA6"
                "</span>"));
            m_pullMergeStatus->show();
        } else if (reviewBlocks) {
            m_pullMergeStatus->setText(QString::fromUtf8(
                "<span style='color:#f85149'>\xE2\x9A\xA0 Changes requested "
                "\xE2\x80\x94 a reviewer is blocking this merge. Resolve their "
                "review (approve, or clear the request) to merge.</span>"));
            m_pullMergeStatus->show();
        } else if (mergeClean) {
            m_pullMergeStatus->setText(QString::fromUtf8(
                "<span style='color:#3fb950'>\xE2\x9C\x93 No conflicts \xE2\x80\x94 "
                "ready to merge.</span>"));
            m_pullMergeStatus->show();
        } else {
            const QString detail =
                conflictFiles.isEmpty()
                    ? QStringLiteral("the patch does not apply to the current base")
                    : QStringLiteral("conflicts in %1 file(s): %2")
                          .arg(conflictFiles.size())
                          .arg(conflictFiles.join(QStringLiteral(", ")).toHtmlEscaped());
            m_pullMergeStatus->setText(
                QString::fromUtf8(
                    "<span style='color:#f85149'>\xE2\x9A\xA0 Cannot merge cleanly "
                    "\xE2\x80\x94 %1. Update the branch from its base, then retry."
                    "</span>")
                    .arg(detail));
            m_pullMergeStatus->show();
        }
    }
    if (m_pullNewButton)
        m_pullNewButton->setEnabled(m_repoDetailIndex >= 0);
    if (m_pullChooseDirButton)
        m_pullChooseDirButton->setEnabled(m_repoDetailIndex >= 0);
    if (m_pullImportButton)
        m_pullImportButton->setEnabled(writable);
    if (m_pullSyncButton)
        m_pullSyncButton->setEnabled(writable);
    if (m_pullUpdateButton) {
        m_pullUpdateButton->setVisible(writable && have && open && behind);
        m_pullUpdateButton->setEnabled(writable && have && open && behind);
    }
    if (m_pullMergeButton) {
        m_pullMergeButton->setEnabled(mergeable && mergeClean && !conflictPending &&
                                      !reviewBlocks);
        m_pullMergeButton->setToolTip(
            conflictPending
                ? QStringLiteral("Checking whether this pull request still applies "
                                 "cleanly…")
                : mergeable && reviewBlocks
                      ? QStringLiteral("A reviewer has requested changes — resolve "
                                       "their review before merging.")
                      : mergeable && !mergeClean
                            ? QStringLiteral("This pull request has conflicts — use "
                                             "\"Resolve conflicts\" to commit a fix to "
                                             "its branch, then merge.")
                            : QStringLiteral("Apply and merge this pull request"));
    }
    // "Merge + delete branch" gates on the same merge-readiness as Merge (it
    // merges first), and on no delete worker already running.
    if (m_pullMergeDeleteButton)
        m_pullMergeDeleteButton->setEnabled(mergeable && mergeClean &&
                                            !conflictPending && !reviewBlocks &&
                                            !m_pullDeleteInProgress);
    // "Build & preview" only makes sense when this repo's local checkout is a
    // ForkMesh source tree we know how to build (qt_client/CMakeLists.txt) and the
    // PR has a head branch to check out. Hidden everywhere else.
    if (m_pullPreviewButton) {
        const bool buildable =
            m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size() &&
            !m_repositories.at(m_repoDetailIndex).localPath.isEmpty() &&
            QFileInfo::exists(m_repositories.at(m_repoDetailIndex).localPath +
                              QStringLiteral("/qt_client/CMakeLists.txt"));
        const bool canPreview = have && !head.isEmpty() && buildable;
        m_pullPreviewButton->setVisible(canPreview);
        m_pullPreviewButton->setEnabled(canPreview);
    }
    const bool conflicted = mergeable && !mergeClean;
    // Conflicting-files card in the conversation, above the comment composer:
    // list every file that no longer applies so the reviewer sees what to fix
    // without leaving the thread. Hidden whenever the PR merges cleanly.
    if (m_pullConflictDetails) {
        if (!conflicted) {
            m_pullConflictDetails->hide();
        } else {
            QString body;
            if (conflictFiles.isEmpty()) {
                body = QStringLiteral(
                    "The patch no longer applies to the current base.");
            } else {
                QStringList items;
                for (const QString &f : std::as_const(conflictFiles))
                    items << QStringLiteral("<li><code>%1</code></li>")
                                 .arg(f.toHtmlEscaped());
                body = QStringLiteral(
                           "%1 conflicting file%2:<ul style='margin:6px 0 0 0;"
                           "-qt-list-indent:1'>%3</ul>")
                           .arg(conflictFiles.size())
                           .arg(conflictFiles.size() == 1 ? QString()
                                                          : QStringLiteral("s"),
                                items.join(QString()));
            }
            m_pullConflictDetails->setText(
                QString::fromUtf8(
                    "<b style='color:#f85149'>\xE2\x9A\xA0 Merge conflicts</b>"
                    "<br>%1<br><a href='tab:files' "
                    "style='color:#58a6ff;text-decoration:none'>"
                    "View files changed \xE2\x86\x92</a>")
                    .arg(body));
            m_pullConflictDetails->show();
        }
    }
    // A running AI fix holds the working tree in a git-am session for this PR, so
    // every conflict action stays disabled until it lands or aborts.
    const bool aiFixBusy = m_aiFix && m_aiFix->number == m_currentPullNumber;
    if (m_pullResolveButton) {
        m_pullResolveButton->setVisible(conflicted);
        m_pullResolveButton->setEnabled(conflicted && !aiFixBusy);
    }
    if (m_pullFixButton) {
        // The button shows whenever the PR conflicts; each dropdown entry then
        // enables only when its provider is usable. The API providers need a key
        // in Settings; Claude Code authenticates through the local `claude` CLI
        // login, so it stays available without one.
        const bool haveClaudeKey =
            !QSettings().value(kClaudeApiKeySetting).toString().trimmed().isEmpty();
        const bool haveOpenAiKey =
            !QSettings().value(kCodexApiKeySetting).toString().trimmed().isEmpty();
        m_pullFixButton->setVisible(conflicted);
        m_pullFixButton->setEnabled(conflicted && !aiFixBusy);
        if (m_pullFixClaudeAction) {
            m_pullFixClaudeAction->setEnabled(haveClaudeKey);
            m_pullFixClaudeAction->setToolTip(
                haveClaudeKey ? QStringLiteral("Resolve with the Claude API")
                              : QStringLiteral("Add a Claude API key in Settings "
                                               "to auto-resolve conflicts."));
        }
        if (m_pullFixOpenAiAction) {
            m_pullFixOpenAiAction->setEnabled(haveOpenAiKey);
            m_pullFixOpenAiAction->setToolTip(
                haveOpenAiKey ? QStringLiteral("Resolve with the OpenAI API")
                              : QStringLiteral("Add an OpenAI API key in Settings "
                                               "to auto-resolve conflicts."));
        }
        if (m_pullFixClaudeCodeAction)
            m_pullFixClaudeCodeAction->setToolTip(
                QStringLiteral("Resolve with the Claude Code CLI (uses your local "
                               "`claude` login)"));
    }
    if (m_pullFixConflictsButton) {
        // Only offer to continue the agent that actually authored this branch
        // (found by PR number or head branch, issue #257) and only while it still
        // has a worktree to run in and isn't already busy — mirrors the agent
        // detail view's "Fix conflicts with agent" button (adhoc #28).
        const AgentSession *agent =
            conflicted ? agentSessionForPull(m_currentPullNumber, head) : nullptr;
        const bool continuable =
            agent && !agent->branchName.isEmpty() && !isExternalSession(agent->id);
        const bool agentBusy =
            agent && (agent->status == AgentStatus::Running ||
                      agent->status == AgentStatus::Queued ||
                      runnerForSession(agent->id));
        m_pullFixConflictsButton->setVisible(conflicted && continuable);
        m_pullFixConflictsButton->setEnabled(conflicted && continuable &&
                                             !agentBusy && !aiFixBusy);
        m_pullFixConflictsButton->setToolTip(
            agentBusy
                ? QStringLiteral("The agent for this pull request is already "
                                 "running \xE2\x80\x94 watch it on the Agents tab")
                : QStringLiteral(
                      "Ask the %1 session that authored this branch to merge "
                      "`%2` in and resolve the conflicts itself")
                      .arg(agent ? agentProviderName(agent->provider)
                                 : QStringLiteral("agent"),
                           base.isEmpty() ? QStringLiteral("main") : base));
    }
    // AI review (adhoc #82): "Review with AI" shows on any open PR with a diff —
    // it only reads the patch, so mirror nodes get it too (their findings travel
    // to the owner's inbox as signed events). "Fix all with AI" appears once
    // unresolved review threads exist; the agent commits to the PR's branch, so
    // it needs a working tree.
    if (m_pullReviewAiButton) {
        const bool reviewable = have && open && !patch.trimmed().isEmpty();
        m_pullReviewAiButton->setVisible(reviewable);
        m_pullReviewAiButton->setEnabled(reviewable && !m_aiReview);
    }
    if (m_pullFixAllAiButton) {
        int unresolved = 0;
        if (have && open)
            for (const PullRequest &pr : std::as_const(m_currentPulls))
                if (pr.number == m_currentPullNumber)
                    unresolved = buildPullReviewSnapshot(pr).unresolvedThreads;
        const bool fixable = writable && have && open && unresolved > 0;
        m_pullFixAllAiButton->setVisible(fixable);
        m_pullFixAllAiButton->setEnabled(fixable && !m_aiFix && !m_aiReview);
        if (fixable)
            m_pullFixAllAiButton->setText(
                QStringLiteral("Fix all with AI (%1)").arg(unresolved));
    }
    if (m_pullEditFileButton)
        m_pullEditFileButton->setEnabled(writable && have && open && m_pullFiles &&
                                         m_pullFiles->currentItem());
    if (m_pullDeleteFileButton)
        m_pullDeleteFileButton->setEnabled(writable && have && open && m_pullFiles &&
                                           m_pullFiles->currentItem());
    if (m_pullCloseButton)
        m_pullCloseButton->setEnabled(writable && have && open);
    if (m_pullReopenButton) {
        m_pullReopenButton->setVisible(writable && have && (closed || merged));
        m_pullReopenButton->setEnabled(writable && have && (closed || merged));
    }
    // "Send to source of truth" only makes sense on a mirror node (no working tree
    // to merge in): the owner holds the real pulls/ tree, so re-deliver the open PR
    // to their inbox where the relay queues it until they come online.
    if (m_pullSendToSourceButton) {
        const bool offerSend = !writable && have && open;
        m_pullSendToSourceButton->setVisible(offerSend);
        m_pullSendToSourceButton->setEnabled(offerSend);
    }
    if (m_pullDeleteButton)
        m_pullDeleteButton->setEnabled(writable && have);
    if (m_pullDeleteBranchButton)
        m_pullDeleteBranchButton->setEnabled(writable && have);
    // Show the agent revision row only when this PR was produced by an agent
    // session (by PR number or through its head branch — issue #257).
    if (m_pullAgentRevisionRow) {
        const bool hasAgent =
            have && agentSessionForPull(m_currentPullNumber, head) != nullptr;
        m_pullAgentRevisionRow->setVisible(hasAgent);
        if (m_pullSendToAgentButton)
            m_pullSendToAgentButton->setEnabled(hasAgent && writable);
    }
}

void MainWindow::promptNewPull()
{
    promptNewPullFromSource(QString());
}

void MainWindow::importPatchAsPull()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const QString dir = repoGitDir();
    PullStore store = pullStoreForCurrentRepo();
    if (dir.isEmpty() || !store.canWrite()) {
        setRepoDetailNotice(
            "Importing a patch needs a writable local checkout of this repo.", true);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, "Import patch as pull request", QDir::homePath(),
        "Patch files (*.patch *.diff);;All files (*)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setRepoDetailNotice("Could not read the patch file.", true);
        return;
    }
    const QByteArray raw = file.readAll();
    file.close();
    const QString patch = QString::fromUtf8(raw);
    if (patch.trimmed().isEmpty()) {
        setRepoDetailNotice("That patch file is empty.", true);
        return;
    }

    // Derive a title: prefer the format-patch "Subject:" line (minus the
    // [PATCH] prefix), else fall back to the file name.
    QString title;
    for (const QString &line : patch.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("Subject:"))) {
            title = line.mid(8).trimmed();
            title.remove(QRegularExpression(QStringLiteral("^\\[PATCH[^\\]]*\\]\\s*")));
            break;
        }
        if (line.startsWith(QLatin1String("diff --git ")))
            break; // reached the diff with no Subject
    }
    if (title.isEmpty())
        title = QFileInfo(path).completeBaseName();

    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);

    // Sanity-check that the patch applies to the base before opening the PR.
    QString applyErr;
    QProcess check;
    check.setProgram("git");
    check.setArguments({"-C", dir, "apply", "--check", "--3way", path});
    check.start();
    check.waitForFinished(8000);
    if (check.exitStatus() != QProcess::NormalExit || check.exitCode() != 0) {
        const QString detail =
            QString::fromUtf8(check.readAllStandardError()).trimmed();
        if (QMessageBox::warning(
                this, "Import patch",
                QStringLiteral("This patch does not apply cleanly onto %1:\n\n%2\n\n"
                               "Open the pull request anyway?")
                    .arg(base, detail.isEmpty() ? "(no details)" : detail),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
    }

    // Synthesize a head label from the title; the patch itself carries the change.
    QString head = QStringLiteral("imported/") +
                   title.toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                                           QStringLiteral("-"));
    head = head.left(60);
    if (head.endsWith(QLatin1Char('-')))
        head.chop(1);

    QString error;
    const int number = store.createPull(
        title, QStringLiteral("Imported from patch file `%1`.").arg(QFileInfo(path).fileName()),
        base, head, patch, QString(), /*branchBacked=*/false, &error);
    if (number < 0) {
        setRepoDetailNotice(error.isEmpty() ? "Could not create the pull request."
                                            : error,
                            true);
        return;
    }
    logSystem(QStringLiteral("Imported patch %1 as pull #%2.").arg(path).arg(number));
    setRepoDetailNotice(QStringLiteral("Imported patch as pull #%1.").arg(number));
    m_currentPullNumber = number;
    switchToPullTab(number);
    propagateRepoUpdate(m_repoDetailIndex);
}

void MainWindow::promptNewPullFromDirectory()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    RepositoryRecord &currentRepo = m_repositories[m_repoDetailIndex];
    const QString startDir = currentRepo.localPath.isEmpty()
                                 ? QDir::homePath()
                                 : currentRepo.localPath;
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Choose a Git repository for this pull request", startDir);
    if (chosen.isEmpty())
        return;
    promptNewPullFromSource(chosen);
}

void MainWindow::promptNewPullFromSource(const QString &sourceDir,
                                         const QString &preferredBase,
                                         const QString &preferredHead)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    RepositoryRecord &currentRepo = m_repositories[m_repoDetailIndex];
    const QString dir = sourceDir.trimmed().isEmpty() ? repoGitDir() : sourceDir.trimmed();
    if (dir.isEmpty()) {
        QMessageBox::warning(this, "New pull request",
                             "No local copy of this repository to diff.");
        return;
    }
    if (!sourceDir.trimmed().isEmpty()) {
        const bool looksLikeGit =
            QDir(dir).exists(".git") || QDir(dir).exists("HEAD");
        if (!looksLikeGit) {
            QMessageBox::warning(
                this, "New pull request",
                "That folder is not a Git repository. Choose a folder created by "
                "\"git init\" or \"git clone\".");
            return;
        }
        QString selectedName = repoNameFromUrl(dir);
        QByteArray origin;
        if (runGitCapture(dir, {"config", "--get", "remote.origin.url"}, &origin, nullptr) &&
            !origin.trimmed().isEmpty())
            selectedName = repoNameFromUrl(QString::fromUtf8(origin).trimmed());
        const QString currentName =
            repoSegment(currentRepo.name, QStringLiteral("repository"));
        if (selectedName != currentName) {
            QMessageBox::warning(
                this, "New pull request",
                QStringLiteral("That directory appears to be %1, but this page is for %2.")
                    .arg(selectedName, currentName));
            return;
        }
        if (currentRepo.localPath.isEmpty() || !QDir(currentRepo.localPath).exists()) {
            currentRepo.localPath = dir;
            saveRepositories();
            refreshRepositoryList();
        }
    }
    // Enumerate branches for the base/head pickers.
    QByteArray out;
    QStringList branches;
    if (runGitCapture(dir, {"branch", "--format=%(refname:short)"}, &out, nullptr))
        for (const QString &b : QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts))
            branches << b.trimmed();
    if (branches.size() < 1) {
        QMessageBox::warning(this, "New pull request", "This repository has no branches.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("New pull request");
    auto *targetCombo = new QComboBox(&dialog);
    targetCombo->setEditable(true);
    targetCombo->addItem(currentRepo.owner);
    QSet<QString> targetOwners{currentRepo.owner};
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.name == currentRepo.name && !targetOwners.contains(repo.owner)) {
            targetCombo->addItem(repo.owner);
            targetOwners.insert(repo.owner);
        }
    }
    targetCombo->setToolTip("Destination node that will receive this pull request");
    auto *baseCombo = new QComboBox(&dialog);
    auto *headCombo = new QComboBox(&dialog);
    baseCombo->addItems(branches);
    headCombo->addItems(branches);
    const int preferredBaseIndex = baseCombo->findText(preferredBase);
    if (preferredBaseIndex >= 0)
        baseCombo->setCurrentIndex(preferredBaseIndex);
    const int preferredHeadIndex = headCombo->findText(preferredHead);
    if (preferredHeadIndex >= 0) {
        headCombo->setCurrentIndex(preferredHeadIndex);
    } else {
        QByteArray currentBranchOut;
        if (runGitCapture(dir, {"rev-parse", "--abbrev-ref", "HEAD"},
                          &currentBranchOut, nullptr)) {
            const int currentIndex = headCombo->findText(
                QString::fromUtf8(currentBranchOut).trimmed());
            if (currentIndex >= 0)
                headCombo->setCurrentIndex(currentIndex);
            else if (branches.size() > 1)
                headCombo->setCurrentIndex(1);
        } else if (branches.size() > 1) {
            headCombo->setCurrentIndex(1);
        }
    }
    auto *titleEdit = new QLineEdit(&dialog);
    titleEdit->setPlaceholderText("Title");
    if (!preferredHead.isEmpty()) {
        QByteArray subject;
        if (runGitCapture(dir, {"log", "-1", "--format=%s", preferredHead},
                          &subject, nullptr))
            titleEdit->setText(QString::fromUtf8(subject).trimmed());
    }
    auto *bodyEdit = new QPlainTextEdit(&dialog);
    bodyEdit->setPlaceholderText("Describe the change\xE2\x80\xA6");
    auto *form = new QFormLayout;
    form->addRow("Target node", targetCombo);
    form->addRow("Base", baseCombo);
    form->addRow("Head", headCombo);
    form->addRow("Title", titleEdit);
    form->addRow("Description", bodyEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *dl = new QVBoxLayout(&dialog);
    dl->addLayout(form);
    dl->addWidget(buttons);
    dialog.resize(520, 420);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString base = baseCombo->currentText();
    const QString head = headCombo->currentText();
    const QString targetText = targetCombo->currentText().trimmed();
    const QString targetOwner = repoSegment(targetText, QString());
    const QString title = titleEdit->text().trimmed();
    if (targetOwner.isEmpty()) {
        QMessageBox::warning(this, "New pull request", "A target node is required.");
        return;
    }
    if (title.isEmpty()) {
        QMessageBox::warning(this, "New pull request", "A title is required.");
        return;
    }
    if (base == head) {
        QMessageBox::warning(this, "New pull request", "Base and head must differ.");
        return;
    }
    QByteArray diff;
    QString diffError;
    const bool fromRange = sourceDir.trimmed().isEmpty();
    const bool haveDiff = fromRange
                              ? runGitCapture(dir, {"diff", "--binary", base + ".." + head},
                                              &diff, &diffError)
                              : buildWorkingTreeDiff(dir, base, &diff, &diffError);
    if (!haveDiff || diff.trimmed().isEmpty()) {
        QMessageBox::warning(this, "New pull request",
                             diffError.isEmpty()
                                 ? QStringLiteral("No differences between %1 and %2.")
                                       .arg(base, head)
                                 : QStringLiteral("Could not compare files: %1")
                                       .arg(diffError));
        return;
    }
    PullRequest pr;
    pr.title = title;
    pr.description = bodyEdit->toPlainText();
    pr.base = base;
    pr.head = head;
    pr.patch = QString::fromUtf8(diff);
    // For a branch-range PR, also carry the format-patch series so the owner can
    // replay it with `git am` and keep each commit's author/date/message. A
    // working-tree diff has no commits, so it stays a flat patch (git apply).
    if (fromRange) {
        QByteArray mbox;
        if (runGitCapture(dir, {"format-patch", "--stdout", base + ".." + head}, &mbox,
                          nullptr))
            pr.commits = QString::fromUtf8(mbox);
    }

    PullStore store = pullStoreForCurrentRepo();
    if (store.canWrite() && targetOwner == currentRepo.owner) {
        QString error;
        const int number = store.createPull(pr.title, pr.description, pr.base, pr.head,
                                             pr.patch, pr.commits,
                                             /*branchBacked=*/fromRange, &error);
        if (number < 0) {
            QMessageBox::warning(this, "New pull request", error);
            return;
        }
        m_currentPullNumber = number;
        switchToPullTab(number);
        propagateRepoUpdate(m_repoDetailIndex);
    } else {
        RepositoryRecord targetRepo = currentRepo;
        targetRepo.owner = targetOwner;
        // Cross-node submission: label the head with this node's name so the
        // owner can tell which mirror node the PR came from (two nodes may both
        // submit from "main"). The owner merges from the carried patch/commits,
        // never by resolving head, so "<node>:<branch>" is purely informational
        // on their side. Prefix before signing so the signature covers the label.
        const QString nodeName = accountNameFromInput(m_userName, QString());
        if (!nodeName.isEmpty() && !pr.head.contains(QLatin1Char(':')))
            pr.head = nodeName + QLatin1Char(':') + pr.head;
        submitPullToInbox(store.makeSignedPull(pr), targetRepo);
    }
}

void MainWindow::updateCurrentPullBranch()
{
    if (m_currentPullNumber < 0)
        return;
    if (QMessageBox::question(this, "Update branch",
                              QStringLiteral("Merge the base branch into pull request #%1?")
                                  .arg(m_currentPullNumber)) != QMessageBox::Yes)
        return;
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (!store.updateBranchFromBase(m_currentPullNumber, &error)) {
        QMessageBox::warning(this, "Update branch", error);
        return;
    }
    logSystem(QStringLiteral("Updated pull request #%1 from its base branch.")
                  .arg(m_currentPullNumber));
    reloadPulls();
}

void MainWindow::mergeCurrentPull()
{
    if (m_currentPullNumber < 0)
        return;
    PullRequest current;
    bool found = false;
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (pr.number == m_currentPullNumber) {
            current = pr;
            found = true;
            break;
        }
    }
    if (!found)
        return;
    if (current.reviewSummary() == QLatin1String("changes_requested")) {
        QMessageBox::warning(
            this, "Merge pull request",
            QStringLiteral("Pull request #%1 has an unresolved \"request changes\" "
                           "review. Resolve the review (approve it, or clear the "
                           "request) before merging.")
                .arg(m_currentPullNumber));
        return;
    }
    if (QMessageBox::question(
            this, "Merge pull request",
            QStringLiteral("Apply and merge pull request #%1?")
                .arg(m_currentPullNumber)) != QMessageBox::Yes)
        return;
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (!store.mergePull(m_currentPullNumber, &error)) {
        QMessageBox::warning(this, "Merge pull request", error);
        return;
    }
    logSystem(QStringLiteral("Merged pull request #%1.").arg(m_currentPullNumber));
    closeIssuesLinkedFromPull(current);
    fundBountiesForMergedPull(current);
    autoBountyForMergedPull(current);
    reloadPulls();
    // Issue #291: flag the agent session behind this PR as landed in main (after
    // reloadPulls so the agent table's PR column also reflects the merge).
    markAgentSessionsMerged(current.number, current.head);
    // Push the merge (closed PR + any linked issue closes) to the mirror and
    // notify peers.
    propagateRepoUpdate(m_repoDetailIndex);
}

// Modal merge-conflict editor shared by the pull-request and branch merge flows.
// Lists the conflicted files, lets the reviewer accept ours/theirs/both per
// region or edit freely, and enables Commit only once every marker is gone.
// Files are read from / written to workTree. Returns true if the user committed
// (commitFn succeeded); false if they cancelled — the caller owns starting the
// merge and, on a false return, aborting it.
bool MainWindow::runMergeConflictEditor(
    const QString &title, const QString &introHtml, const QString &workTree,
    const QStringList &conflictedFiles, const QString &commitButtonText,
    const std::function<bool(QString *)> &commitFn)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.resize(960, 640);

    auto *intro = new QLabel(introHtml);
    intro->setObjectName("statusLine");
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);

    auto *fileList = new QListWidget;
    fileList->setObjectName("overviewList");
    enableHoverRowHighlight(fileList); // green outline selection (issue #252)
    fileList->setMinimumWidth(220);

    auto *editor = new QPlainTextEdit;
    editor->setObjectName("codeEditor");
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    applyLogFont(editor);
    new ConflictHighlighter(editor->document());

    auto *oursBtn = new QPushButton(QStringLiteral("Accept ours"));
    auto *theirsBtn = new QPushButton(QStringLiteral("Accept theirs"));
    auto *bothBtn = new QPushButton(QStringLiteral("Accept both"));
    auto *allTheirsBtn = new QPushButton(QStringLiteral("Accept all theirs"));
    auto *prevBtn = new QPushButton(QString::fromUtf8("\xE2\x86\x91 Prev"));
    auto *nextBtn = new QPushButton(QString::fromUtf8("\xE2\x86\x93 Next"));
    for (QPushButton *b : {oursBtn, theirsBtn, bothBtn, allTheirsBtn, prevBtn, nextBtn}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    oursBtn->setToolTip("Keep our version of this conflict");
    theirsBtn->setToolTip("Take their version of this conflict");
    bothBtn->setToolTip("Keep both sides (ours first, then theirs)");
    allTheirsBtn->setToolTip("Take their version of every conflict in every file");
    // Tint the accept buttons to echo ConflictHighlighter's side colours: blue
    // for "ours", green for "theirs", so they read against the highlighted diff.
    // Give them their own objectName: the per-widget stylesheet below targets
    // that ID so it outranks the global "#ghostButton { background: transparent }"
    // rule on specificity (an ID selector), otherwise the tint never shows.
    oursBtn->setObjectName("conflictOursBtn");
    theirsBtn->setObjectName("conflictTheirsBtn");
    allTheirsBtn->setObjectName("conflictAllTheirsBtn");
    const bool darkConflict = currentThemeIsDark();
    const auto tintConflictBtn = [](QPushButton *b, const QString &bg,
                                    const QString &border, const QString &fg,
                                    const QString &hoverBg) {
        b->setStyleSheet(QStringLiteral(
                             "QPushButton#%1 { background:%2; border:1px solid %3;"
                             " color:%4; font-weight:600; padding:3px 10px;"
                             " border-radius:6px; }"
                             "QPushButton#%1:hover { background:%5; color:%4; }")
                             .arg(b->objectName(), bg, border, fg, hoverBg));
    };
    if (darkConflict) {
        tintConflictBtn(oursBtn, "#0b2a4a", "#1f6feb", "#cae3ff", "#10395f");
        tintConflictBtn(theirsBtn, "#0b3a1e", "#238636", "#aff5b8", "#114a26");
        tintConflictBtn(allTheirsBtn, "#0b3a1e", "#238636", "#aff5b8", "#114a26");
    } else {
        tintConflictBtn(oursBtn, "#ddf4ff", "#54aeff", "#0969da", "#cae8ff");
        tintConflictBtn(theirsBtn, "#e6ffec", "#4ac26b", "#1a7f37", "#d2f8d9");
        tintConflictBtn(allTheirsBtn, "#e6ffec", "#4ac26b", "#1a7f37", "#d2f8d9");
    }
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(oursBtn);
    toolbar->addWidget(theirsBtn);
    toolbar->addWidget(bothBtn);
    toolbar->addWidget(allTheirsBtn);
    toolbar->addStretch();
    toolbar->addWidget(prevBtn);
    toolbar->addWidget(nextBtn);

    auto *commitBtn = new QPushButton(commitButtonText);
    commitBtn->setObjectName("primaryButton");
    commitBtn->setCursor(Qt::PointingHandCursor);
    auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"));
    cancelBtn->setObjectName("ghostButton");
    cancelBtn->setCursor(Qt::PointingHandCursor);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addStretch();
    buttonRow->addWidget(cancelBtn);
    buttonRow->addWidget(commitBtn);

    auto *editorCol = new QVBoxLayout;
    editorCol->setContentsMargins(0, 0, 0, 0);
    editorCol->addLayout(toolbar);
    editorCol->addWidget(editor, 1);
    auto *editorPane = new QWidget;
    editorPane->setLayout(editorCol);
    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(fileList);
    split->addWidget(editorPane);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({240, 680});

    auto *outer = new QVBoxLayout(&dlg);
    outer->addWidget(intro);
    outer->addWidget(split, 1);
    outer->addLayout(buttonRow);

    // ---- State + helpers ----------------------------------------------------
    auto currentPath = std::make_shared<QString>();
    const auto hasMarkers = [](const QString &text) {
        return text.startsWith(QLatin1String("<<<<<<< ")) ||
               text.contains(QLatin1String("\n<<<<<<< ")) ||
               text.contains(QLatin1String("\n>>>>>>> "));
    };
    const auto readFile = [workTree](const QString &rel) {
        QFile f(workTree + "/" + rel);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    const auto saveCurrent = [=] {
        if (currentPath->isEmpty())
            return;
        QFile f(workTree + "/" + *currentPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(editor->toPlainText().toUtf8());
    };
    const auto refreshStatus = [=] {
        const QString currentText = editor->toPlainText();
        bool allClean = true;
        for (int i = 0; i < fileList->count(); ++i) {
            QListWidgetItem *it = fileList->item(i);
            const QString rel = it->data(Qt::UserRole).toString();
            const bool markers =
                rel == *currentPath ? hasMarkers(currentText) : hasMarkers(readFile(rel));
            it->setText((markers ? QString::fromUtf8("\xE2\x9A\xA0 ")
                                 : QString::fromUtf8("\xE2\x9C\x93 ")) +
                        rel);
            if (markers)
                allClean = false;
        }
        commitBtn->setEnabled(allClean);
        commitBtn->setToolTip(allClean
                                  ? QStringLiteral("Commit the resolved merge")
                                  : QStringLiteral("Resolve every conflict first"));
    };

    for (const QString &rel : conflictedFiles) {
        auto *it = new QListWidgetItem(rel);
        it->setData(Qt::UserRole, rel);
        fileList->addItem(it);
    }

    connect(fileList, &QListWidget::currentItemChanged, &dlg,
            [=](QListWidgetItem *item, QListWidgetItem *) {
                saveCurrent();
                if (!item) {
                    currentPath->clear();
                    editor->clear();
                    return;
                }
                *currentPath = item->data(Qt::UserRole).toString();
                editor->setPlainText(readFile(*currentPath));
                refreshStatus();
            });
    connect(editor, &QPlainTextEdit::textChanged, &dlg, [=] { refreshStatus(); });

    // Apply ours(0)/theirs(1)/both(2) to the conflict at the cursor.
    const auto applyResolution = [=](int which) {
        QStringList lines = editor->toPlainText().split('\n');
        const QList<ConflictRegion> regions = findConflicts(lines);
        if (regions.isEmpty())
            return;
        const int cursorLine = editor->textCursor().blockNumber();
        int idx = -1;
        for (int i = 0; i < regions.size(); ++i)
            if (cursorLine >= regions.at(i).startLine &&
                cursorLine <= regions.at(i).endLine) {
                idx = i;
                break;
            }
        if (idx < 0)
            for (int i = 0; i < regions.size(); ++i)
                if (regions.at(i).startLine >= cursorLine) {
                    idx = i;
                    break;
                }
        if (idx < 0)
            idx = 0;
        const ConflictRegion r = regions.at(idx);
        const QStringList ours = lines.mid(r.startLine + 1, r.sepLine - r.startLine - 1);
        const QStringList theirs = lines.mid(r.sepLine + 1, r.endLine - r.sepLine - 1);
        QStringList repl = which == 0 ? ours : which == 1 ? theirs : (ours + theirs);
        const QStringList out =
            lines.mid(0, r.startLine) + repl + lines.mid(r.endLine + 1);
        editor->setPlainText(out.join('\n'));
        QTextCursor c = editor->textCursor();
        c.movePosition(QTextCursor::Start);
        c.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor,
                       qMin(r.startLine, qMax(0, out.size() - 1)));
        editor->setTextCursor(c);
    };
    connect(oursBtn, &QPushButton::clicked, &dlg, [=] { applyResolution(0); });
    connect(theirsBtn, &QPushButton::clicked, &dlg, [=] { applyResolution(1); });
    connect(bothBtn, &QPushButton::clicked, &dlg, [=] { applyResolution(2); });

    // Bulk action: take their side of every conflict across every file, not just
    // the one at the cursor. Each resolved file is written straight to the work
    // tree; the visible editor is then reloaded for the current file.
    const auto takeAllTheirs = [](const QString &text) {
        QStringList lines = text.split('\n');
        const QList<ConflictRegion> regions = findConflicts(lines);
        // Rewrite from the last region back so earlier line offsets stay valid.
        for (int i = regions.size() - 1; i >= 0; --i) {
            const ConflictRegion r = regions.at(i);
            const QStringList theirs =
                lines.mid(r.sepLine + 1, r.endLine - r.sepLine - 1);
            lines = lines.mid(0, r.startLine) + theirs + lines.mid(r.endLine + 1);
        }
        return lines.join('\n');
    };
    connect(allTheirsBtn, &QPushButton::clicked, &dlg, [=] {
        saveCurrent(); // flush the visible editor to disk before re-reading
        for (int i = 0; i < fileList->count(); ++i) {
            const QString rel = fileList->item(i)->data(Qt::UserRole).toString();
            const QString resolved = takeAllTheirs(readFile(rel));
            QFile f(workTree + "/" + rel);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(resolved.toUtf8());
        }
        if (!currentPath->isEmpty())
            editor->setPlainText(readFile(*currentPath));
        refreshStatus();
    });

    const auto jump = [=](int dir) {
        const QStringList lines = editor->toPlainText().split('\n');
        const QList<ConflictRegion> regions = findConflicts(lines);
        if (regions.isEmpty())
            return;
        const int cursorLine = editor->textCursor().blockNumber();
        int target = -1;
        if (dir > 0) {
            for (const ConflictRegion &r : regions)
                if (r.startLine > cursorLine) {
                    target = r.startLine;
                    break;
                }
            if (target < 0)
                target = regions.first().startLine;
        } else {
            for (int i = regions.size() - 1; i >= 0; --i)
                if (regions.at(i).startLine < cursorLine) {
                    target = regions.at(i).startLine;
                    break;
                }
            if (target < 0)
                target = regions.last().startLine;
        }
        QTextCursor c = editor->textCursor();
        c.movePosition(QTextCursor::Start);
        c.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, target);
        editor->setTextCursor(c);
        editor->centerCursor();
    };
    connect(nextBtn, &QPushButton::clicked, &dlg, [=] { jump(1); });
    connect(prevBtn, &QPushButton::clicked, &dlg, [=] { jump(-1); });

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    bool committed = false;
    connect(commitBtn, &QPushButton::clicked, &dlg, [&] {
        saveCurrent();
        QString err;
        if (!commitFn(&err)) {
            QMessageBox::warning(&dlg, title,
                                 err.isEmpty()
                                     ? QStringLiteral("Could not commit the merge.")
                                     : err);
            refreshStatus();
            return;
        }
        committed = true;
        dlg.accept();
    });

    if (fileList->count() > 0)
        fileList->setCurrentRow(0);
    refreshStatus();
    dlg.exec();
    return committed;
}

// ---- AI conflict auto-resolution ------------------------------------------
// One-click alternative to the manual merge editor: a low-cost model rewrites
// each conflicting file and the result is committed straight to the PR's own
// branch (no new PR). The whole thing is surfaced as a live agent session so the
// user can watch it work; PullStore's git-am session is held open across the
// async API round-trips and finalized once every file resolves.

void MainWindow::aiFixLog(const QString &text)
{
    if (!m_aiFix || !m_agentStore)
        return;
    if (AgentSession *s = findAgentSession(m_aiFix->sessionId))
        m_agentStore->appendLog(*s, text);
    onAgentLog(m_aiFix->sessionId, text); // live-append if this session is shown
}

void MainWindow::aiFixSetSessionStatus(const QString &status, const QString &error)
{
    if (!m_aiFix || !m_agentStore)
        return;
    AgentSession *s = findAgentSession(m_aiFix->sessionId);
    if (!s)
        return;
    s->status = status;
    s->costUsd = m_aiFix->costUsd;
    s->promptTokens = int(m_aiFix->inTokens);
    s->completionTokens = int(m_aiFix->outTokens);
    s->totalTokens = int(m_aiFix->inTokens + m_aiFix->outTokens);
    if (!error.isEmpty())
        s->lastError = error;
    if (status == AgentStatus::Success || status == AgentStatus::Failed ||
        status == AgentStatus::Stopped)
        s->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_agentStore->saveSession(*s);
    reloadAgents();
    if (m_aiFix->sessionId == m_selectedAgentSessionId)
        showAgentSession(m_aiFix->sessionId);
}

// ---- AI code review (adhoc #82) ---------------------------------------------
// "Review with AI" sends the PR's diff to a model in one shot. Each finding the
// model reports lands as a signed review thread anchored to the file+line it
// concerns; findings where the model supplied both the original lines and a
// replacement carry a suggestion patch the reviewer applies and commits in one
// click. A summary review event records the run's outcome in the conversation.

void MainWindow::reviewCurrentPullWithAi()
{
    if (m_aiReview) {
        flashMessage("An AI review is already running; wait for it to finish.",
                     true);
        return;
    }
    if (m_currentPullNumber < 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const int number = m_currentPullNumber;
    PullRequest current;
    for (const PullRequest &pr : std::as_const(m_currentPulls))
        if (pr.number == number)
            current = pr;
    if (current.number == 0 || current.patch.trimmed().isEmpty()) {
        flashMessage("This pull request has no diff to review.", true);
        return;
    }

    // Provider: a configured API key wins (one request over the diff suffices
    // and the reply is easiest to keep to strict JSON); without one, fall back
    // to the Claude Code CLI, which authenticates through its local login.
    const QString claudeKey =
        QSettings().value(kClaudeApiKeySetting).toString().trimmed();
    const QString openAiKey =
        QSettings().value(kCodexApiKeySetting).toString().trimmed();
    QString provider = QStringLiteral("claude-code");
    QString model;
    if (!claudeKey.isEmpty()) {
        provider = QStringLiteral("claude");
        model = QStringLiteral("claude-opus-4-8");
    } else if (!openAiKey.isEmpty()) {
        provider = QStringLiteral("openai");
        model = QStringLiteral("gpt-4.1-mini");
    }

    // A visible agent session so the run shows up on the Agents tab.
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    AgentSession session;
    session.owner = repo.owner;
    session.name = repo.name;
    session.issueNumber = 0;
    session.issueTitle = QStringLiteral("AI review of PR #%1").arg(number);
    session.provider = provider;
    session.model = model;
    session.prNumber = number;
    session.status = AgentStatus::Running;
    session = m_agentStore->createSession(session);
    session.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_agentStore->saveSession(session);

    m_aiReview = new AiPullReview;
    m_aiReview->number = number;
    m_aiReview->repoIndex = m_repoDetailIndex;
    m_aiReview->sessionId = session.id;
    m_aiReview->provider = provider;
    m_aiReview->model = model;

    aiReviewLog(QStringLiteral(
                    "==> %1 reviewing pull request #%2 (%3 file(s), +%4 -%5).\n")
                    .arg(agentProviderName(provider))
                    .arg(number)
                    .arg(current.filesChanged)
                    .arg(current.additions)
                    .arg(current.deletions));
    if (m_pullMergeStatus) {
        m_pullMergeStatus->setText(QString::fromUtf8(
            "<span style='color:#58a6ff'>\xF0\x9F\xA4\x96 %1 is reviewing this "
            "pull request\xE2\x80\xA6 findings will be attached to the lines "
            "they concern.</span>").arg(agentProviderName(provider)));
        m_pullMergeStatus->show();
    }
    updatePullActionState();

    const QString prompt =
        buildAiReviewPrompt(current.title, current.description, current.patch);

    if (provider == QLatin1String("claude-code")) {
        aiReviewRunClaudeCode(prompt);
        return;
    }

    const bool claude = provider == QLatin1String("claude");
    // Findings are a few KB of JSON even on a big PR; 8K output is plenty.
    constexpr int kReviewOutTokens = 8000;
    QNetworkReply *reply = nullptr;
    if (claude) {
        QJsonObject payload;
        payload.insert("model", m_aiReview->model);
        payload.insert("max_tokens", kReviewOutTokens);
        QJsonArray messages;
        QJsonObject um;
        um.insert("role", "user");
        um.insert("content", prompt);
        messages.append(um);
        payload.insert("messages", messages);
        QNetworkRequest req(
            QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
        req.setRawHeader("x-api-key", claudeKey.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    } else {
        QJsonObject payload;
        payload.insert("model", m_aiReview->model);
        payload.insert("input", prompt);
        payload.insert("max_output_tokens", kReviewOutTokens);
        QNetworkRequest req = openAiRequest(
            QUrl(QStringLiteral("https://api.openai.com/v1/responses")),
            openAiKey);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, claude] {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!m_aiReview) // torn down (e.g. app closing) — nothing to do
            return;
        if (reply->error() != QNetworkReply::NoError) {
            aiReviewFail(
                QStringLiteral("API error: %1").arg(apiErrorSummary(reply, body)));
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
            const QJsonObject usage = obj.value("usage").toObject();
            const qint64 in = usage.value("input_tokens").toInt();
            const qint64 out = usage.value("output_tokens").toInt();
            m_aiReview->inTokens += in;
            m_aiReview->outTokens += out;
            m_aiReview->costUsd += in / 1e6 * 5.0 + out / 1e6 * 25.0; // Opus 4.8
        } else {
            text = openAiResponseText(obj);
            qint64 in = 0, out = 0;
            m_aiReview->costUsd += openAiAskCostUsd(obj, &in, &out);
            m_aiReview->inTokens += in;
            m_aiReview->outTokens += out;
        }
        aiReviewHandleReply(text);
    });
}

// Claude Code path for the review: run the local `claude` CLI once with the
// review prompt. It has repo context (cwd is the checkout when one exists) but
// is told to change nothing and print only the findings JSON; the chatter its
// wrapper adds is tolerated by parseAiReviewFindings' bracket extraction.
void MainWindow::aiReviewRunClaudeCode(const QString &prompt)
{
    if (!m_aiReview)
        return;
    const QString workTree =
        (m_aiReview->repoIndex >= 0 && m_aiReview->repoIndex < m_repositories.size())
            ? writableRecordFor(m_repositories.at(m_aiReview->repoIndex)).localPath
            : QString();
    const QString promptPath = QDir::temp().filePath(
        QStringLiteral("forkmesh-review-%1.md").arg(m_aiReview->number));
    m_aiReview->promptFile = promptPath;
    QFile pf(promptPath);
    if (!pf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        aiReviewFail(QStringLiteral("Could not write the review prompt file."));
        return;
    }
    pf.write(prompt.toUtf8());
    pf.write(QByteArray("\n\nDo NOT modify any file and do NOT run any git "
                        "command - this is a read-only review. Print ONLY the "
                        "JSON array as your final output.\n"));
    pf.close();

    QString promptQuoted = promptPath;
    promptQuoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    promptQuoted = QLatin1Char('\'') + promptQuoted + QLatin1Char('\'');
    QString command = claudeCodeCommandSetting();
    if (command.contains(QStringLiteral("{promptFile}")))
        command.replace(QStringLiteral("{promptFile}"), promptQuoted);
    else
        command += QStringLiteral(" < ") + promptQuoted;

    auto *process = new QProcess(this);
    m_aiReview->process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(workTree.isEmpty() ? QDir::tempPath() : workTree);
    process->setStandardInputFile(QProcess::nullDevice());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("ANTHROPIC_API_KEY"));
    const QString home = QDir::homePath();
    const QString extraPath = home + QStringLiteral("/.local/bin:") + home +
                              QStringLiteral("/.cargo/bin:") + home +
                              QStringLiteral("/.npm-global/bin");
    env.insert(QStringLiteral("PATH"),
               extraPath + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
    process->setProcessEnvironment(env);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (!m_aiReview || m_aiReview->process != process)
            return;
        const QString chunk = QString::fromUtf8(process->readAllStandardOutput());
        m_aiReview->output += chunk;
        aiReviewLog(chunk);
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError err) {
                if (!m_aiReview || m_aiReview->process != process)
                    return;
                if (err == QProcess::FailedToStart) {
                    m_aiReview->process = nullptr;
                    process->deleteLater();
                    QFile::remove(m_aiReview->promptFile);
                    aiReviewFail(QStringLiteral(
                        "Could not start the `claude` CLI \xE2\x80\x94 install "
                        "Claude Code or set its command in Settings."));
                }
            });
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus) {
                if (!m_aiReview || m_aiReview->process != process)
                    return;
                const QByteArray tail = process->readAllStandardOutput();
                if (!tail.isEmpty()) {
                    m_aiReview->output += QString::fromUtf8(tail);
                    aiReviewLog(QString::fromUtf8(tail));
                }
                m_aiReview->process = nullptr;
                process->deleteLater();
                QFile::remove(m_aiReview->promptFile);
                if (exitCode != 0) {
                    aiReviewFail(QStringLiteral(
                                     "Claude Code exited with code %1 before "
                                     "finishing the review.")
                                     .arg(exitCode));
                    return;
                }
                aiReviewHandleReply(m_aiReview->output);
            });

    aiReviewLog(QStringLiteral(
        "==> Running Claude Code over the pull request diff\xE2\x80\xA6\n"));
#ifdef Q_OS_WIN
    process->start(QStringLiteral("cmd"), {QStringLiteral("/c"), command});
#else
    const QString shell = QFile::exists(QStringLiteral("/bin/bash"))
                              ? QStringLiteral("/bin/bash")
                              : QStringLiteral("/bin/sh");
    process->start(shell, {QStringLiteral("-lc"), command});
#endif
}

// Turn the model's reply into review threads on the PR. Owner nodes commit the
// signed events straight into pulls/<N>/; mirror nodes route them through the
// relay inbox like any hand-written review comment.
void MainWindow::aiReviewHandleReply(const QString &text)
{
    if (!m_aiReview)
        return;
    bool parsed = false;
    QList<AiReviewFinding> findings = parseAiReviewFindings(text, &parsed);
    if (!parsed) {
        aiReviewFail(QStringLiteral(
                         "The model's reply carried no findings JSON:\n%1")
                         .arg(text.left(400)));
        return;
    }
    // A runaway reply must not flood the PR with threads.
    constexpr int kMaxFindings = 25;
    if (findings.size() > kMaxFindings)
        findings = findings.mid(0, kMaxFindings);

    const int number = m_aiReview->number;
    if (m_aiReview->repoIndex < 0 ||
        m_aiReview->repoIndex >= m_repositories.size()) {
        aiReviewFail(QStringLiteral("The repository is no longer open."));
        return;
    }
    const RepositoryRecord &repo =
        writableRecordFor(m_repositories.at(m_aiReview->repoIndex));
    PullStore store(repo.localPath, repo.mirrorPath, &m_profileIdentity,
                    m_userName);

    int quickFixes = 0;
    for (const AiReviewFinding &f : std::as_const(findings)) {
        const QString body = QString::fromUtf8("**\xF0\x9F\xA4\x96 AI review "
                                               "\xC2\xB7 %1:** %2")
                                 .arg(f.severity, f.comment);
        if (!f.suggestionPatch.isEmpty())
            ++quickFixes;
        if (store.canWrite()) {
            QString postError;
            store.addThreadComment(number, f.path, QStringLiteral("new"),
                                   f.lineStart, f.lineEnd, body,
                                   f.suggestionPatch, &postError);
        } else {
            PullEvent ev;
            ev.type = QStringLiteral("thread-comment");
            ev.path = f.path;
            ev.side = QStringLiteral("new");
            ev.lineStart = f.lineStart;
            ev.lineEnd = f.lineEnd;
            ev.body = body;
            ev.suggestionPatch = f.suggestionPatch;
            ev = store.makeSignedEvent(number, ev);
            submitPullEventToInbox(number, ev);
        }
        aiReviewLog(QStringLiteral("==> %1:%2 [%3] %4%5\n")
                        .arg(f.path)
                        .arg(f.lineStart)
                        .arg(f.severity, f.comment.left(120),
                             f.suggestionPatch.isEmpty()
                                 ? QString()
                                 : QStringLiteral(" (quick fix)")));
    }

    // A summary review event so the conversation records the outcome. Posted as
    // "commented" — an AI approving/blocking under the node's own signature
    // would distort the human review summary.
    QString summary;
    if (findings.isEmpty())
        summary = QString::fromUtf8(
            "\xF0\x9F\xA4\x96 AI review found no issues in this diff.");
    else
        summary =
            QString::fromUtf8(
                "\xF0\x9F\xA4\x96 AI review found %1 issue(s); %2 carry a "
                "one-click \"Apply fix & commit\" suggestion. Use \"Fix all "
                "with AI\" to hand the open findings to an agent.")
                .arg(findings.size())
                .arg(quickFixes);
    if (store.canWrite()) {
        QString postError;
        store.addReview(number, QStringLiteral("commented"), summary, &postError);
    } else {
        PullEvent ev;
        ev.type = QStringLiteral("review");
        ev.state = QStringLiteral("commented");
        ev.body = summary;
        ev = store.makeSignedEvent(number, ev);
        submitPullEventToInbox(number, ev);
    }

    aiReviewLog(QStringLiteral("==> Review finished: %1 finding(s), %2 with a "
                               "quick fix (cost ~$%3).\n")
                    .arg(findings.size())
                    .arg(quickFixes)
                    .arg(QString::number(m_aiReview->costUsd, 'f', 4)));
    aiReviewSetSessionStatus(AgentStatus::Success);
    const int repoIndex = m_aiReview->repoIndex;
    delete m_aiReview;
    m_aiReview = nullptr;

    logSystem(QStringLiteral("AI review of pull request #%1 finished.").arg(number));
    if (repoIndex == m_repoDetailIndex) {
        reloadPulls();
        showPull(number);
    }
    flashMessage(findings.isEmpty()
                     ? QStringLiteral("AI review: no issues found on PR #%1.")
                           .arg(number)
                     : QStringLiteral("AI review: %1 finding(s) attached to "
                                      "PR #%2's code.")
                           .arg(findings.size())
                           .arg(number));
}

void MainWindow::aiReviewFail(const QString &message)
{
    if (!m_aiReview)
        return;
    const int number = m_aiReview->number;
    const int repoIndex = m_aiReview->repoIndex;
    aiReviewLog(QStringLiteral("!! %1\n").arg(message));
    aiReviewSetSessionStatus(AgentStatus::Failed, message);
    delete m_aiReview;
    m_aiReview = nullptr;
    flashMessage(QStringLiteral("AI review failed: %1").arg(message), true);
    if (repoIndex == m_repoDetailIndex) {
        reloadPulls();
        showPull(number);
    }
}

void MainWindow::aiReviewLog(const QString &text)
{
    if (!m_aiReview || !m_agentStore)
        return;
    if (AgentSession *s = findAgentSession(m_aiReview->sessionId))
        m_agentStore->appendLog(*s, text);
    onAgentLog(m_aiReview->sessionId, text); // live-append if shown
}

void MainWindow::aiReviewSetSessionStatus(const QString &status,
                                          const QString &error)
{
    if (!m_aiReview || !m_agentStore)
        return;
    AgentSession *s = findAgentSession(m_aiReview->sessionId);
    if (!s)
        return;
    s->status = status;
    s->costUsd = m_aiReview->costUsd;
    s->promptTokens = int(m_aiReview->inTokens);
    s->completionTokens = int(m_aiReview->outTokens);
    s->totalTokens = int(m_aiReview->inTokens + m_aiReview->outTokens);
    if (!error.isEmpty())
        s->lastError = error;
    if (status == AgentStatus::Success || status == AgentStatus::Failed ||
        status == AgentStatus::Stopped)
        s->finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_agentStore->saveSession(*s);
    reloadAgents();
    if (m_aiReview->sessionId == m_selectedAgentSessionId)
        showAgentSession(m_aiReview->sessionId);
}

// One-click "Apply fix & commit" on a review thread's suggestion (adhoc #82):
// re-checks out the PR's branch with the PR applied, applies the suggestion
// patch (verifying the exact lines it replaces, relocating if the recorded
// line drifted), commits the edit to the branch and records the suggestion as
// applied + the thread as resolved. The PR stays open and mergeable.
void MainWindow::applyPullSuggestionFix(const QString &threadId)
{
    if (m_currentPullNumber < 0 || threadId.isEmpty())
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    if (m_aiFix) {
        flashMessage("An AI fix is already running; wait for it to finish.",
                     true);
        return;
    }
    PullRequest current;
    for (const PullRequest &pr : std::as_const(m_currentPulls))
        if (pr.number == m_currentPullNumber)
            current = pr;
    if (current.number == 0)
        return;
    PullReviewThread target;
    const PullReviewSnapshot snapshot = buildPullReviewSnapshot(current);
    for (const PullReviewThread &thread : snapshot.threads)
        if (thread.id == threadId)
            target = thread;
    QString suggestion;
    for (const PullEvent &ev : std::as_const(target.events))
        if (ev.type == QLatin1String("thread-comment") &&
            !ev.suggestionPatch.isEmpty())
            suggestion = ev.suggestionPatch;
    if (target.path.isEmpty() || suggestion.isEmpty()) {
        flashMessage("This thread has no applicable suggestion.", true);
        return;
    }
    if (target.suggestionState == QLatin1String("applied")) {
        flashMessage("This suggestion is already applied.");
        return;
    }

    PullStore store = pullStoreForCurrentRepo();
    if (!store.canWrite()) {
        flashMessage("This repository is read-only on this node.", true);
        return;
    }
    QString error;
    QString content;
    if (!store.startPullFileEdit(m_currentPullNumber, target.path, &content,
                                 &error)) {
        QMessageBox::warning(this, "Apply fix", error);
        return;
    }
    if (!applySuggestionToContent(&content, target.lineStart, suggestion,
                                  &error)) {
        store.abortConflictMerge();
        QMessageBox::warning(this, "Apply fix", error);
        return;
    }
    if (!store.finishPullFileEdit(m_currentPullNumber, target.path, content,
                                  &error)) {
        store.abortConflictMerge();
        QMessageBox::warning(this, "Apply fix", error);
        return;
    }

    // Record the applied state with the commit that carries it (the PR
    // branch's refreshed tip), and resolve the thread like an accepted
    // suggestion elsewhere would be.
    QString appliedSha;
    const QString workTree =
        writableRecordFor(m_repositories.at(m_repoDetailIndex)).localPath;
    for (const PullRequest &p : store.loadAll())
        if (p.number == m_currentPullNumber && !p.head.isEmpty()) {
            QByteArray out;
            if (runGitCapture(workTree, {"rev-parse", p.head}, &out, nullptr))
                appliedSha = QString::fromUtf8(out).trimmed();
        }
    store.setSuggestionState(m_currentPullNumber, threadId,
                             QStringLiteral("applied"), appliedSha,
                             QStringLiteral("Applied the suggested fix."),
                             &error);
    store.setThreadState(m_currentPullNumber, threadId,
                         QStringLiteral("resolved"), QString(), &error);
    reloadPulls();
    showPull(m_currentPullNumber);
    propagateRepoUpdate(m_repoDetailIndex);
    flashMessage(QStringLiteral("Fix applied and committed to PR #%1's branch.")
                     .arg(m_currentPullNumber));
}

// "Fix all with AI" (adhoc #82): check the PR's branch out with the PR applied
// and hand every unresolved review thread — the quick-fixable ones and the
// ones that need real work alike — to a Claude Code run that edits the tree;
// finishPullAgentEdit commits the lot back to the branch. Reuses the m_aiFix
// machinery (process handling, session log, finish/fail) in agentEdit mode.
void MainWindow::fixCurrentPullFindingsWithAgent()
{
    if (m_aiFix) {
        flashMessage("An AI fix is already running; wait for it to finish.",
                     true);
        return;
    }
    if (m_aiReview) {
        flashMessage("Wait for the AI review to finish first.", true);
        return;
    }
    if (m_currentPullNumber < 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const int number = m_currentPullNumber;
    PullRequest current;
    for (const PullRequest &pr : std::as_const(m_currentPulls))
        if (pr.number == number)
            current = pr;
    if (current.number == 0)
        return;

    // Every unresolved thread goes into the prompt: path:lines, the comments,
    // and any suggested fix (the agent may apply or better it).
    const PullReviewSnapshot snapshot = buildPullReviewSnapshot(current);
    QStringList findingBlocks;
    QStringList paths;
    for (const PullReviewThread &thread : snapshot.threads) {
        if (thread.resolved ||
            thread.suggestionState == QLatin1String("applied"))
            continue;
        QStringList lines;
        lines << QStringLiteral("- %1:%2%3")
                     .arg(thread.path)
                     .arg(thread.lineStart)
                     .arg(thread.lineEnd > thread.lineStart
                              ? QStringLiteral("-%1").arg(thread.lineEnd)
                              : QString());
        for (const PullEvent &ev : thread.events) {
            if (ev.type != QLatin1String("thread-comment") &&
                ev.type != QLatin1String("thread-reply") &&
                ev.type != QLatin1String("line-comment"))
                continue;
            if (!ev.body.trimmed().isEmpty())
                lines << QStringLiteral("  %1").arg(
                    ev.body.trimmed().left(600).replace(
                        QLatin1Char('\n'), QStringLiteral("\n  ")));
            if (!ev.suggestionPatch.isEmpty())
                lines << QStringLiteral("  Suggested fix:\n  %1").arg(
                    QString(ev.suggestionPatch)
                        .replace(QLatin1Char('\n'), QStringLiteral("\n  ")));
        }
        findingBlocks << lines.join(QLatin1Char('\n'));
        if (!thread.path.isEmpty() && !paths.contains(thread.path))
            paths << thread.path;
    }
    if (findingBlocks.isEmpty()) {
        flashMessage("No unresolved review findings to fix.");
        return;
    }

    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString workTree = writableRecordFor(repo).localPath;
    if (workTree.isEmpty()) {
        QMessageBox::warning(this, "Fix all with AI",
                             "This repository is read-only on this node.");
        return;
    }

    // Open the PR's branch with the PR applied; the store carries that state
    // across the async run, so it lives on the heap until finish/fail.
    auto *store = new PullStore(pullStoreForCurrentRepo());
    QString error;
    if (!store->startPullAgentEdit(number, &error)) {
        delete store;
        QMessageBox::warning(this, "Fix all with AI", error);
        return;
    }

    AgentSession session;
    session.owner = repo.owner;
    session.name = repo.name;
    session.issueNumber = 0;
    session.issueTitle =
        QStringLiteral("Fix review findings on PR #%1").arg(number);
    session.provider = QStringLiteral("claude-code");
    session.prNumber = number;
    session.branchName = current.head.isEmpty()
                             ? QStringLiteral("pull/%1").arg(number)
                             : current.head;
    session.status = AgentStatus::Running;
    session = m_agentStore->createSession(session);
    session.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_agentStore->saveSession(session);
    m_agentStore->appendLog(
        session,
        QStringLiteral("==> Claude Code fixing %1 review finding(s) on pull "
                       "request #%2.\n")
            .arg(findingBlocks.size())
            .arg(number));

    m_aiFix = new AiConflictFix;
    m_aiFix->store = store;
    m_aiFix->number = number;
    m_aiFix->repoIndex = m_repoDetailIndex;
    m_aiFix->sessionId = session.id;
    m_aiFix->provider = QStringLiteral("claude-code");
    m_aiFix->workTree = workTree;
    m_aiFix->files = paths;
    m_aiFix->claudeCode = true;
    m_aiFix->agentEdit = true;
    m_aiFix->agentEditFindings = findingBlocks.size();

    QStringList prompt;
    prompt << QStringLiteral(
        "You are addressing code-review findings on a pull request. Its branch "
        "is checked out in this repository with the pull request applied.");
    prompt << QString();
    prompt << QStringLiteral(
        "The findings, each anchored to file:line(s) of the current checkout:");
    prompt << findingBlocks.join(QStringLiteral("\n\n"));
    prompt << QString();
    prompt << QStringLiteral(
        "Edit the files to properly fix every finding. Keep the changes "
        "minimal and in the spirit of the pull request.");
    prompt << QStringLiteral(
        "Do NOT run any git command, do NOT commit, and do NOT touch unrelated "
        "code \xE2\x80\x94 ForkMesh commits the result for you once you are "
        "done.");
    m_aiFix->agentEditPrompt = prompt.join(QLatin1Char('\n'));

    if (m_pullMergeStatus) {
        m_pullMergeStatus->setText(QString::fromUtf8(
            "<span style='color:#58a6ff'>\xF0\x9F\xA4\x96 Claude Code is fixing "
            "the review findings\xE2\x80\xA6 watch it on the Agents tab."
            "</span>"));
        m_pullMergeStatus->show();
    }
    updatePullActionState();
    switchToAgentsTab(session.id);
    aiFixRunClaudeCode();
}

void MainWindow::fixCurrentPullConflictsWithAi(const QString &provider)
{
    if (m_aiFix) {
        flashMessage("An AI conflict fix is already running; wait for it to finish.",
                     true);
        return;
    }
    if (m_currentPullNumber < 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const int number = m_currentPullNumber;

    // "claude-code" drives the real `claude` CLI (no API key, authenticates via the
    // local login); the two API providers POST each file to their endpoint.
    const bool claudeCode = provider == QLatin1String("claude-code");
    const bool claude = provider == QLatin1String("claude");
    const QString model =
        claudeCode ? QString()
                   : claude ? QStringLiteral("claude-haiku-4-5")
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

    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QString workTree = writableRecordFor(repo).localPath;
    if (workTree.isEmpty()) {
        QMessageBox::warning(this, "Fix conflicts",
                             "This repository is read-only on this node.");
        return;
    }

    // Open the PR's isolated resolve branch and lay down the conflict markers.
    // The store instance must outlive the async API calls (it carries the git-am
    // state), so it lives on the heap and is freed in finish/fail.
    auto *store = new PullStore(pullStoreForCurrentRepo());
    QStringList conflicted;
    bool resolvedClean = false;
    QString error;
    if (!store->startConflictMerge(number, &conflicted, &resolvedClean, &error)) {
        delete store;
        QMessageBox::warning(this, "Fix conflicts", error);
        return;
    }

    // Spin up a visible agent session so the run shows up on the Agents tab with a
    // live "Running" indicator the moment work starts.
    AgentSession session;
    session.owner = repo.owner;
    session.name = repo.name;
    session.issueNumber = 0; // PR-scoped, not issue-scoped
    session.issueTitle = QStringLiteral("Resolve conflicts on PR #%1").arg(number);
    session.provider = provider; // "claude" | "openai" | "claude-code"
    session.prNumber = number;
    session.branchName = QStringLiteral("pull/%1").arg(number);
    session.status = AgentStatus::Running;
    session = m_agentStore->createSession(session);
    session.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_agentStore->saveSession(session);
    m_agentStore->appendLog(
        session,
        claudeCode
            ? QStringLiteral("==> %1 resolving merge conflicts on pull request #%2.\n")
                  .arg(agentProviderName(provider))
                  .arg(number)
            : QStringLiteral(
                  "==> %1 (%2) resolving merge conflicts on pull request #%3.\n")
                  .arg(agentProviderName(provider), model)
                  .arg(number));

    m_aiFix = new AiConflictFix;
    m_aiFix->store = store;
    m_aiFix->number = number;
    m_aiFix->repoIndex = m_repoDetailIndex;
    m_aiFix->sessionId = session.id;
    m_aiFix->provider = provider;
    m_aiFix->model = model;
    m_aiFix->apiKey = apiKey;
    m_aiFix->workTree = workTree;
    m_aiFix->files = conflicted;
    m_aiFix->claudeCode = claudeCode;

    // Immediate "being worked on" indicator on the PR banner, plus jump to the
    // Agents tab so the user can watch it run.
    if (m_pullMergeStatus) {
        m_pullMergeStatus->setText(QString::fromUtf8(
            "<span style='color:#58a6ff'>\xF0\x9F\xA4\x96 %1 is resolving conflicts\xE2\x80\xA6 "
            "watch it on the Agents tab.</span>").arg(agentProviderName(provider)));
        m_pullMergeStatus->show();
    }
    updatePullActionState();
    switchToAgentsTab(session.id);

    if (resolvedClean) {
        // No markers to edit — startConflictMerge already committed the apply on
        // the branch; just finalize.
        aiFixLog(QStringLiteral(
            "==> Patch applied cleanly with no conflicts left to resolve.\n"));
        aiFixFinish();
        return;
    }

    aiFixLog(QStringLiteral("==> %1 file(s) to resolve: %2\n")
                 .arg(conflicted.size())
                 .arg(conflicted.join(QStringLiteral(", "))));
    if (m_aiFix->claudeCode)
        aiFixRunClaudeCode();
    else
        aiFixResolveNextFile();
}

// "Fix conflicts with agent" on the PR page: rather than spinning up a fresh,
// conflict-only run (fixCurrentPullConflictsWithAi above), continue the actual
// agent session that authored this branch — same prompt and flow as the agent
// detail view's own "Fix conflicts with agent" button, so the agent keeps its
// full context/history and tool access (it can rebuild, run tests, etc. before
// committing). Only reachable when updatePullActionState found such a session.
void MainWindow::fixCurrentPullConflictsWithOriginatingAgent()
{
    if (m_currentPullNumber < 0)
        return;
    QString head, base;
    for (const PullRequest &pr : m_currentPulls) {
        if (pr.number == m_currentPullNumber) {
            head = pr.head;
            base = pr.base;
        }
    }
    const AgentSession *agent = agentSessionForPull(m_currentPullNumber, head);
    if (!agent || agent->branchName.isEmpty() || isExternalSession(agent->id)) {
        flashMessage("No agent session is attached to this pull request.", true);
        return;
    }
    if (agent->status == AgentStatus::Running ||
        agent->status == AgentStatus::Queued || runnerForSession(agent->id)) {
        flashMessage("The agent for this pull request is already running.", true);
        return;
    }

    const int sessionId = agent->id;
    const QString provider = agent->provider;
    const QString prompt =
        QStringLiteral("Merge `%1` into your branch and resolve all merge conflicts. "
                       "Make sure the build and tests still pass, then commit.")
            .arg(base.isEmpty() ? QStringLiteral("main") : base);
    m_pendingSteerMessage.insert(sessionId, prompt);
    if (provider == QLatin1String("claude-code"))
        applyTranscriptEvent(
            sessionId,
            QJsonObject{{QStringLiteral("type"), QStringLiteral("_local_user")},
                        {QStringLiteral("text"), prompt}});
    switchToAgentsTab(sessionId);
    continueSelectedAgentSession();
}

void MainWindow::aiFixResolveNextFile()
{
    if (!m_aiFix)
        return;
    if (m_aiFix->index >= m_aiFix->files.size()) {
        aiFixFinish();
        return;
    }
    const QString rel = m_aiFix->files.at(m_aiFix->index);
    QFile f(m_aiFix->workTree + QLatin1Char('/') + rel);
    if (!f.open(QIODevice::ReadOnly)) {
        aiFixFail(QStringLiteral("Could not read %1 from the working tree.").arg(rel));
        return;
    }
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    // A single-shot rewrite can't reliably reproduce a very large file within the
    // output budget, so bail to manual resolution rather than truncate.
    if (content.size() > 60000) {
        aiFixFail(QStringLiteral(
                      "%1 is too large to auto-resolve \xE2\x80\x94 use \"Resolve "
                      "conflicts\xE2\x80\xA6\" for this one.").arg(rel));
        return;
    }

    aiFixLog(QStringLiteral("==> [net] Resolving %1 (%2/%3) with %4\xE2\x80\xA6\n")
                 .arg(rel)
                 .arg(m_aiFix->index + 1)
                 .arg(m_aiFix->files.size())
                 .arg(m_aiFix->model));

    const bool claude = m_aiFix->provider == QLatin1String("claude");
    const QString system = QStringLiteral(
        "You are a careful software engineer resolving a Git merge conflict. You "
        "output only the complete, fully merged file contents.");
    const QString task =
        QStringLiteral(
            "The file `%1` contains Git merge conflict markers (<<<<<<<, =======, "
            ">>>>>>>). Resolve every conflict by combining both sides into one "
            "correct, coherent file. Keep all non-conflicting content exactly as "
            "it is. Remove every conflict marker. Output ONLY the complete resolved "
            "file contents \xE2\x80\x94 no explanation, no markdown code fences."
            "\n\n----- BEGIN FILE -----\n%2\n----- END FILE -----")
            .arg(rel, content);
    // Budget enough output to reproduce the whole file (~1 token per 3 chars) with
    // headroom, capped so a low-cost model stays low-cost.
    const int outTok = qBound(1024, content.size() / 3 + 1024, 16000);

    QNetworkReply *reply = nullptr;
    if (claude) {
        QJsonObject payload;
        payload.insert("model", m_aiFix->model);
        payload.insert("max_tokens", outTok);
        QJsonArray messages;
        QJsonObject um;
        um.insert("role", "user");
        um.insert("content", system + "\n\n" + task);
        messages.append(um);
        payload.insert("messages", messages);
        QNetworkRequest req(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
        req.setRawHeader("x-api-key", m_aiFix->apiKey.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    } else {
        QJsonObject payload;
        payload.insert("model", m_aiFix->model);
        payload.insert("instructions", system);
        payload.insert("input", task);
        payload.insert("max_output_tokens", outTok);
        QNetworkRequest req =
            openAiRequest(QUrl(QStringLiteral("https://api.openai.com/v1/responses")),
                          m_aiFix->apiKey);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, claude] {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!m_aiFix) // torn down (e.g. app closing) — nothing to do
            return;
        if (reply->error() != QNetworkReply::NoError) {
            aiFixFail(QStringLiteral("API error: %1").arg(apiErrorSummary(reply, body)));
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
            const QJsonObject usage = obj.value("usage").toObject();
            const qint64 in = usage.value("input_tokens").toInt();
            const qint64 out = usage.value("output_tokens").toInt();
            m_aiFix->inTokens += in;
            m_aiFix->outTokens += out;
            m_aiFix->costUsd += in / 1e6 * 1.0 + out / 1e6 * 5.0; // Haiku 4.5 rates
        } else {
            text = openAiResponseText(obj);
            qint64 in = 0, out = 0;
            m_aiFix->costUsd += openAiAskCostUsd(obj, &in, &out);
            m_aiFix->inTokens += in;
            m_aiFix->outTokens += out;
        }
        aiFixApplyResolved(text);
    });
}

// Claude Code path: rather than POST each file to an API, run the real `claude`
// CLI once over the whole conflict-marked tree. The same git-am session is open,
// so the agent edits files in place and finishConflictMerge commits the result.
void MainWindow::aiFixRunClaudeCode()
{
    if (!m_aiFix)
        return;

    // A focused prompt. Conflict mode: resolve the listed files' conflict
    // markers and nothing else (the git-am session is open in this very tree).
    // Review-fix mode (adhoc #82): the pre-built findings prompt from
    // fixCurrentPullFindingsWithAgent. Either way the agent must not run git or
    // commit — finishConflictMerge/finishPullAgentEdit stage and commit after.
    const QString promptPath =
        m_aiFix->workTree + (m_aiFix->agentEdit
                                 ? QStringLiteral("/.forkmesh-review-fix-prompt.md")
                                 : QStringLiteral("/.forkmesh-conflict-prompt.md"));
    m_aiFix->promptFile = promptPath;
    QString promptText;
    if (m_aiFix->agentEdit) {
        promptText = m_aiFix->agentEditPrompt;
    } else {
        QStringList prompt;
        prompt << QStringLiteral(
            "You are resolving Git merge conflicts in this repository checkout.");
        prompt << QStringLiteral("These files contain conflict markers "
                                 "(<<<<<<<, =======, >>>>>>>):");
        for (const QString &rel : std::as_const(m_aiFix->files))
            prompt << QStringLiteral("  - %1").arg(rel);
        prompt << QString();
        prompt << QStringLiteral(
            "Edit each of those files so every conflict is resolved by combining both "
            "sides into one correct, coherent result. Remove every conflict marker and "
            "keep all non-conflicting content exactly as it is.");
        prompt << QStringLiteral(
            "Do NOT run any git command, do NOT commit, and do NOT touch any other "
            "file \xE2\x80\x94 ForkMesh commits the result for you once you are done.");
        promptText = prompt.join(QLatin1Char('\n'));
    }
    QFile pf(promptPath);
    if (!pf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        aiFixFail(QStringLiteral("Could not write the agent prompt file."));
        return;
    }
    pf.write(promptText.toUtf8());
    pf.close();

    // Expand the configured Claude Code command, substituting the prompt file.
    QString promptQuoted = promptPath;
    promptQuoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    promptQuoted = QLatin1Char('\'') + promptQuoted + QLatin1Char('\'');
    QString command = claudeCodeCommandSetting();
    // Pass the model the user picked in the "Fix with agent" dropdown (adhoc #60)
    // through to the CLI as --model. The alias ("opus"/"sonnet"/"haiku") is added
    // before the prompt redirection so it stays ahead of any trailing `< file`;
    // an empty model leaves the CLI on its own default.
    if (!m_aiFix->model.isEmpty())
        command += QStringLiteral(" --model ") + m_aiFix->model;
    if (command.contains(QStringLiteral("{promptFile}")))
        command.replace(QStringLiteral("{promptFile}"), promptQuoted);
    else
        command += QStringLiteral(" < ") + promptQuoted;

    auto *process = new QProcess(this);
    m_aiFix->process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(m_aiFix->workTree);
    // The prompt is delivered in argv (or redirected from the prompt file inside
    // the command itself), so this run never reads our stdin. Point stdin at the
    // null device so `claude` doesn't sit waiting on an empty, never-closed stdin
    // pipe for 3s and emit a "no stdin data received" warning before proceeding.
    process->setStandardInputFile(QProcess::nullDevice());

    // Claude Code authenticates through its own login; strip any inherited API key
    // so it never silently uses a stale/foreign one, and widen PATH to the usual
    // user install dirs (matches AgentRunner).
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("ANTHROPIC_API_KEY"));
    const QString home = QDir::homePath();
    const QString extraPath = home + QStringLiteral("/.local/bin:") + home +
                              QStringLiteral("/.cargo/bin:") + home +
                              QStringLiteral("/.npm-global/bin");
    env.insert(QStringLiteral("PATH"),
               extraPath + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
    process->setProcessEnvironment(env);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (!m_aiFix || m_aiFix->process != process)
            return;
        aiFixLog(QString::fromUtf8(process->readAllStandardOutput()));
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError err) {
                if (!m_aiFix || m_aiFix->process != process)
                    return;
                if (err == QProcess::FailedToStart) {
                    m_aiFix->process = nullptr;
                    process->deleteLater();
                    QFile::remove(m_aiFix->promptFile);
                    aiFixFail(QStringLiteral(
                        "Could not start the `claude` CLI \xE2\x80\x94 install Claude "
                        "Code or set its command in Settings."));
                }
            });
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus) {
                if (!m_aiFix || m_aiFix->process != process)
                    return;
                const QByteArray tail = process->readAllStandardOutput();
                if (!tail.isEmpty())
                    aiFixLog(QString::fromUtf8(tail));
                const QString workTree = m_aiFix->workTree;
                const QStringList files = m_aiFix->files;
                const bool agentEdit = m_aiFix->agentEdit;
                m_aiFix->process = nullptr;
                process->deleteLater();
                QFile::remove(m_aiFix->promptFile);
                if (exitCode != 0) {
                    aiFixFail(QStringLiteral(
                                  "Claude Code exited with code %1 before %2.")
                                  .arg(exitCode)
                                  .arg(agentEdit
                                           ? QStringLiteral("fixing the findings")
                                           : QStringLiteral(
                                                 "resolving the conflicts")));
                    return;
                }
                // Conflict mode only: the CLI claims success — make sure no
                // marker survived before finishConflictMerge commits (it rejects
                // markers too, but a clear message here is friendlier). A
                // review-fix run starts from a marker-free tree, so there is
                // nothing to scan for.
                if (!agentEdit) {
                    for (const QString &rel : files) {
                        QFile f(workTree + QLatin1Char('/') + rel);
                        if (!f.open(QIODevice::ReadOnly))
                            continue;
                        const QString text = QString::fromUtf8(f.readAll());
                        if (text.contains(QStringLiteral("\n<<<<<<< ")) ||
                            text.startsWith(QStringLiteral("<<<<<<< ")) ||
                            text.contains(QStringLiteral("\n>>>>>>> "))) {
                            aiFixFail(QStringLiteral(
                                          "Claude Code left conflict markers in %1 "
                                          "\xE2\x80\x94 resolve it manually instead.")
                                          .arg(rel));
                            return;
                        }
                    }
                }
                aiFixLog(agentEdit
                             ? QStringLiteral("==> Claude Code finished; "
                                              "committing the review fixes.\n")
                             : QStringLiteral("==> Claude Code finished; "
                                              "committing the resolution.\n"));
                aiFixFinish();
            });

    aiFixLog(m_aiFix->agentEdit
                 ? QStringLiteral("==> Running Claude Code over the pull "
                                  "request's branch\xE2\x80\xA6\n")
                 : QStringLiteral(
                       "==> Running Claude Code over the conflict tree\xE2\x80\xA6\n"));
#ifdef Q_OS_WIN
    process->start(QStringLiteral("cmd"), {QStringLiteral("/c"), command});
#else
    const QString shell = QFile::exists(QStringLiteral("/bin/bash"))
                              ? QStringLiteral("/bin/bash")
                              : QStringLiteral("/bin/sh");
    process->start(shell, {QStringLiteral("-lc"), command});
#endif
}

void MainWindow::aiFixApplyResolved(const QString &resolvedIn)
{
    if (!m_aiFix)
        return;
    const QString rel = m_aiFix->files.at(m_aiFix->index);
    QString resolved = resolvedIn;
    // Strip an accidental ```lang ... ``` fence if the model added one.
    if (resolved.startsWith(QStringLiteral("```"))) {
        const int nl = resolved.indexOf(QLatin1Char('\n'));
        if (nl >= 0)
            resolved = resolved.mid(nl + 1);
        if (resolved.endsWith(QStringLiteral("```")))
            resolved.chop(3);
        else if (resolved.endsWith(QStringLiteral("```\n")))
            resolved.chop(4);
    }
    if (resolved.trimmed().isEmpty()) {
        aiFixFail(QStringLiteral("The model returned no content for %1.").arg(rel));
        return;
    }
    // The model must not have left any conflict markers behind.
    if (resolved.contains(QStringLiteral("<<<<<<< ")) ||
        resolved.contains(QStringLiteral("\n>>>>>>> ")) ||
        resolved.startsWith(QStringLiteral(">>>>>>> "))) {
        aiFixFail(QStringLiteral(
                      "The model left conflict markers in %1 \xE2\x80\x94 resolve it "
                      "manually instead.").arg(rel));
        return;
    }
    if (!resolved.endsWith(QLatin1Char('\n')))
        resolved.append(QLatin1Char('\n'));
    QFile out(m_aiFix->workTree + QLatin1Char('/') + rel);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        aiFixFail(QStringLiteral("Could not write the resolved %1.").arg(rel));
        return;
    }
    out.write(resolved.toUtf8());
    out.close();
    aiFixLog(QStringLiteral("==> Resolved %1.\n").arg(rel));
    m_aiFix->index++;
    aiFixResolveNextFile();
}

void MainWindow::aiFixFinish()
{
    if (!m_aiFix)
        return;
    // Branch-merge mode: commit the resolved merge onto the checked-out branch,
    // then restore the branch we came from. No PullStore involved.
    if (m_aiFix->branchMerge) {
        const QString dir = m_aiFix->workTree;
        const QString branch = m_aiFix->branch;
        const QString base = m_aiFix->baseBranch;
        const QString restore = m_aiFix->restoreBranch;
        const int repoIndex = m_aiFix->repoIndex;
        const double cost = m_aiFix->costUsd;
        QString error;
        if (!runGitCapture(dir, {"add", "-A"}, nullptr, &error) ||
            !runGitCapture(dir, {"commit", "--no-edit"}, nullptr, &error)) {
            aiFixFail(error.isEmpty() ? QStringLiteral("Could not commit the merge.")
                                      : error);
            return;
        }
        if (!restore.isEmpty() && restore != branch)
            runGitCapture(dir, {"checkout", restore}, nullptr, nullptr);
        aiFixLog(QStringLiteral("==> Committed the merge of %1 into %2 (cost ~$%3).\n")
                     .arg(base, branch, QString::number(cost, 'f', 4)));
        aiFixSetSessionStatus(AgentStatus::Success);
        delete m_aiFix;
        m_aiFix = nullptr;
        logSystem(QStringLiteral("AI resolved conflicts merging %1 into %2.")
                      .arg(base, branch));
        if (repoIndex == m_repoDetailIndex) {
            loadBranchesAndTags();
            loadBranchesPanel();
        }
        if (repoIndex >= 0)
            propagateRepoUpdate(repoIndex);
        flashMessage(
            QStringLiteral("Resolved conflicts: %1 merged into %2.").arg(base, branch));
        return;
    }
    const int number = m_aiFix->number;
    const int repoIndex = m_aiFix->repoIndex;
    const bool agentEdit = m_aiFix->agentEdit;
    QString error;
    const bool committed =
        agentEdit
            ? m_aiFix->store->finishPullAgentEdit(
                  number,
                  QStringLiteral("pull #%1: apply AI review fixes").arg(number),
                  &error)
            : m_aiFix->store->finishConflictMerge(number, &error);
    if (!committed) {
        aiFixFail(error.isEmpty() ? QStringLiteral("Could not commit the fix.")
                                  : error);
        return;
    }
    aiFixLog(QStringLiteral(
                 "==> Committed the %1 to pull request #%2's branch "
                 "(cost ~$%3).\n")
                 .arg(agentEdit ? QStringLiteral("review fixes")
                                : QStringLiteral("conflict fix"))
                 .arg(number)
                 .arg(QString::number(m_aiFix->costUsd, 'f', 4)));
    aiFixSetSessionStatus(AgentStatus::Success);

    // Leave a trace in the PR conversation so reviewers know the branch moved
    // and can re-check + resolve the threads the agent addressed (adhoc #82).
    if (agentEdit) {
        QString commentError;
        m_aiFix->store->addComment(
            number,
            QString::fromUtf8(
                "\xF0\x9F\xA4\x96 An agent worked through %1 unresolved review "
                "finding(s) and committed fixes to this pull request's branch "
                "\xE2\x80\x94 re-check the threads and resolve the ones that "
                "are addressed.")
                .arg(m_aiFix->agentEditFindings),
            &commentError);
    }

    delete m_aiFix->store;
    delete m_aiFix;
    m_aiFix = nullptr;

    logSystem(agentEdit
                  ? QStringLiteral("AI fixed review findings on pull request "
                                   "#%1's branch; re-check the threads.")
                        .arg(number)
                  : QStringLiteral(
                        "AI resolved conflicts on pull request #%1's branch; it "
                        "is updated and ready to merge.")
                        .arg(number));
    if (repoIndex == m_repoDetailIndex) {
        reloadPulls();
        showPull(number);
    }
    if (repoIndex >= 0)
        propagateRepoUpdate(repoIndex);
    flashMessage(agentEdit
                     ? QStringLiteral(
                           "Review fixes on PR #%1 committed to its branch.")
                           .arg(number)
                     : QStringLiteral("Conflicts on PR #%1 fixed and committed.")
                           .arg(number));
}

void MainWindow::aiFixFail(const QString &message)
{
    if (!m_aiFix)
        return;
    // Branch-merge mode: abort the in-progress merge and restore the branch we
    // came from. No PullStore involved.
    if (m_aiFix->branchMerge) {
        const QString dir = m_aiFix->workTree;
        const QString branch = m_aiFix->branch;
        const QString restore = m_aiFix->restoreBranch;
        const int repoIndex = m_aiFix->repoIndex;
        aiFixLog(QStringLiteral("!! %1\n").arg(message));
        aiFixSetSessionStatus(AgentStatus::Failed, message);
        runGitCapture(dir, {"merge", "--abort"}, nullptr, nullptr);
        if (!restore.isEmpty() && restore != branch)
            runGitCapture(dir, {"checkout", restore}, nullptr, nullptr);
        delete m_aiFix;
        m_aiFix = nullptr;
        flashMessage(QStringLiteral("AI conflict fix failed: %1").arg(message), true);
        if (repoIndex == m_repoDetailIndex) {
            loadBranchesAndTags();
            loadBranchesPanel();
        }
        return;
    }
    const int number = m_aiFix->number;
    const int repoIndex = m_aiFix->repoIndex;
    const bool agentEdit = m_aiFix->agentEdit;
    aiFixLog(QStringLiteral("!! %1\n").arg(message));
    aiFixSetSessionStatus(AgentStatus::Failed, message);
    m_aiFix->store->abortConflictMerge(); // restore the working tree + drop the branch

    delete m_aiFix->store;
    delete m_aiFix;
    m_aiFix = nullptr;

    flashMessage(QStringLiteral("%1 failed: %2")
                     .arg(agentEdit ? QStringLiteral("AI review fix")
                                    : QStringLiteral("AI conflict fix"),
                          message),
                 true);
    if (repoIndex == m_repoDetailIndex) {
        reloadPulls();
        showPull(number);
    }
}

void MainWindow::resolveCurrentPullConflicts()
{
    if (m_currentPullNumber < 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    PullRequest current;
    bool found = false;
    for (const PullRequest &pr : std::as_const(m_currentPulls))
        if (pr.number == m_currentPullNumber) {
            current = pr;
            found = true;
            break;
        }
    if (!found)
        return;
    const int number = m_currentPullNumber;
    const QString workTree =
        writableRecordFor(m_repositories.at(m_repoDetailIndex)).localPath;
    if (workTree.isEmpty()) {
        QMessageBox::warning(this, "Resolve conflicts",
                             "This repository is read-only on this node.");
        return;
    }

    // Shared tail run after the fix lands on the PR's branch. The PR stays open
    // and becomes cleanly mergeable; issue-closing and bounty payout happen only
    // on the later, explicit Merge (see mergeCurrentPull). The refreshed PR is
    // propagated so peers/the contributor see the conflict-resolved version.
    const auto finalizeResolved = [this, current] {
        logSystem(QStringLiteral("Resolved conflicts on pull request #%1's branch; "
                                 "it is updated and ready to merge.")
                      .arg(current.number));
        reloadPulls();
        propagateRepoUpdate(m_repoDetailIndex);
    };

    PullStore store = pullStoreForCurrentRepo();
    QStringList conflicted;
    bool resolvedClean = false;
    QString error;
    if (!store.startConflictMerge(number, &conflicted, &resolvedClean, &error)) {
        QMessageBox::warning(this, "Resolve conflicts", error);
        return;
    }
    if (resolvedClean) {
        // Applied with no markers to edit — the fix is already committed on the
        // PR's branch; just finalize.
        finalizeResolved();
        return;
    }

    // Hand off to the shared merge editor; commit = finish the PR-branch merge.
    const QString intro = QStringLiteral(
        "Resolve each conflict, then commit the fix to the pull request's branch. "
        "<b>Ours</b> is your base branch; <b>theirs</b> is the pull request. You "
        "can also edit the text directly. The pull request stays open and becomes "
        "ready to merge \xE2\x80\x94 your base branch is left untouched.");
    const bool committed = runMergeConflictEditor(
        QString::fromUtf8("Resolve conflicts \xE2\x80\x94 pull #%1").arg(number),
        intro, workTree, conflicted, QStringLiteral("Commit to branch"),
        [&store, number](QString *err) { return store.finishConflictMerge(number, err); });
    if (committed) {
        finalizeResolved();
    } else {
        store.abortConflictMerge();
        logSystem(QStringLiteral("Cancelled conflict resolution for pull #%1.").arg(number));
        reloadPulls();
    }
}

void MainWindow::editCurrentPullFile()
{
    if (m_currentPullNumber < 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    bool found = false;
    for (const PullRequest &pr : std::as_const(m_currentPulls))
        if (pr.number == m_currentPullNumber) {
            found = true;
            break;
        }
    if (!found)
        return;
    const int number = m_currentPullNumber;
    if (writableRecordFor(m_repositories.at(m_repoDetailIndex)).localPath.isEmpty()) {
        QMessageBox::warning(this, "Edit file",
                             "This repository is read-only on this node.");
        return;
    }
    QListWidgetItem *item = m_pullFiles ? m_pullFiles->currentItem() : nullptr;
    if (!item) {
        QMessageBox::information(this, "Edit file",
                                 "Select a file from this pull request to edit.");
        return;
    }
    const QString relPath = item->data(Qt::UserRole).toString();

    // Check out the PR's branch with the PR applied and read the file to edit.
    PullStore store = pullStoreForCurrentRepo();
    QString content;
    QString error;
    if (!store.startPullFileEdit(number, relPath, &content, &error)) {
        QMessageBox::warning(this, "Edit file", error);
        return;
    }

    // ---- Editor dialog -----------------------------------------------------
    QDialog dlg(this);
    dlg.setWindowTitle(
        QString::fromUtf8("Edit %1 \xE2\x80\x94 pull #%2").arg(relPath).arg(number));
    dlg.resize(900, 620);

    auto *intro = new QLabel(
        QStringLiteral("Editing <b>%1</b>. Saving commits the change to this pull "
                       "request's branch \xE2\x80\x94 the PR stays open and ready "
                       "to merge; your base branch is left untouched.")
            .arg(relPath.toHtmlEscaped()));
    intro->setObjectName("statusLine");
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);

    auto *editor = new QPlainTextEdit;
    editor->setObjectName("codeEditor");
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    applyLogFont(editor);
    editor->setPlainText(content);

    auto *saveBtn = new QPushButton(QStringLiteral("Commit to branch"));
    saveBtn->setObjectName("primaryButton");
    saveBtn->setCursor(Qt::PointingHandCursor);
    auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"));
    cancelBtn->setObjectName("ghostButton");
    cancelBtn->setCursor(Qt::PointingHandCursor);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addStretch();
    buttonRow->addWidget(cancelBtn);
    buttonRow->addWidget(saveBtn);

    auto *outer = new QVBoxLayout(&dlg);
    outer->addWidget(intro);
    outer->addWidget(editor, 1);
    outer->addLayout(buttonRow);

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    bool committed = false;
    connect(saveBtn, &QPushButton::clicked, &dlg, [&] {
        QString err;
        if (store.finishPullFileEdit(number, relPath, editor->toPlainText(), &err)) {
            committed = true;
            dlg.accept();
            return;
        }
        // Either nothing changed (the branch was already torn down) or a real
        // failure — surface it and end the session; the safe move is to close.
        QMessageBox::warning(&dlg, "Edit file", err);
        dlg.reject();
    });

    dlg.exec();
    if (committed) {
        logSystem(QStringLiteral("Committed an edit to %1 on pull request #%2's "
                                 "branch; it is updated and ready to merge.")
                      .arg(relPath)
                      .arg(number));
        reloadPulls();
        propagateRepoUpdate(m_repoDetailIndex);
    } else {
        store.abortConflictMerge();
        reloadPulls();
    }
}

void MainWindow::deleteCurrentPullFile()
{
    if (m_currentPullNumber < 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    bool found = false;
    for (const PullRequest &pr : std::as_const(m_currentPulls))
        if (pr.number == m_currentPullNumber) {
            found = true;
            break;
        }
    if (!found)
        return;
    const int number = m_currentPullNumber;
    if (writableRecordFor(m_repositories.at(m_repoDetailIndex)).localPath.isEmpty()) {
        QMessageBox::warning(this, "Delete file",
                             "This repository is read-only on this node.");
        return;
    }
    QListWidgetItem *item = m_pullFiles ? m_pullFiles->currentItem() : nullptr;
    if (!item) {
        QMessageBox::information(this, "Delete file",
                                 "Select a file from this pull request to delete.");
        return;
    }
    const QString relPath = item->data(Qt::UserRole).toString();
    if (QMessageBox::warning(
            this, "Delete file",
            QStringLiteral("Delete \"%1\" on pull request #%2's branch? The deletion "
                           "is committed to the PR (which stays open and ready to "
                           "merge); your base branch is left untouched.")
                .arg(relPath)
                .arg(number),
            QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
        return;

    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (store.deletePullFile(number, relPath, &error)) {
        logSystem(QStringLiteral("Deleted %1 on pull request #%2's branch; it is "
                                 "updated and ready to merge.")
                      .arg(relPath)
                      .arg(number));
        reloadPulls();
        propagateRepoUpdate(m_repoDetailIndex);
    } else {
        store.abortConflictMerge();
        QMessageBox::warning(this, "Delete file",
                             error.isEmpty() ? "Could not delete the file." : error);
        reloadPulls();
    }
}

void MainWindow::closeIssuesLinkedFromPull(const PullRequest &pr)
{
    // issuesLinkedFromPull() is the single source of truth for what this PR
    // resolves: an agent session's attached issue (matched by PR number or head
    // branch — issue #257) plus any "closes #N" reference in the PR text or
    // comments. Re-scanning only the text here would silently skip an agent branch
    // whose attached issue is never mentioned in prose.
    const QList<int> closed = closeIssuesForMerge(
        issuesLinkedFromPull(pr),
        QStringLiteral("Closed by merged pull request #%1.").arg(pr.number),
        QStringLiteral("pull request #%1").arg(pr.number));

    if (closed.size() == 1)
        flashMessage(QStringLiteral("Closed issue #%1 from merged pull request.")
                         .arg(closed.first()));
    else if (closed.size() > 1)
        flashMessage(QStringLiteral("Closed %1 issues from merged pull request.")
                         .arg(closed.size()));
}

QList<int> MainWindow::closeIssuesForMerge(const QList<int> &numbers,
                                           const QString &comment, const QString &via)
{
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite() || numbers.isEmpty())
        return {};

    QHash<int, Issue> byNumber;
    for (const Issue &issue : store.loadAll())
        byNumber.insert(issue.number, issue);

    QList<int> closed;
    for (const int number : numbers) {
        const Issue issue = byNumber.value(number);
        if (issue.number <= 0 || issue.status == QLatin1String("closed"))
            continue;

        QString err;
        if (!store.addComment(number, comment, {}, &err)) {
            logSystem(QStringLiteral("Issue #%1: could not link %2: %3")
                          .arg(number)
                          .arg(via, err));
            continue;
        }
        if (!store.setStatus(number, QStringLiteral("closed"), &err)) {
            logSystem(QStringLiteral("Issue #%1: could not close after %2: %3")
                          .arg(number)
                          .arg(via, err));
            continue;
        }
        logSystem(
            QStringLiteral("Closed issue #%1 via %2.").arg(number).arg(via));
        closed.append(number);
    }

    if (!closed.isEmpty()) {
        reloadIssues();
        updateRepoIssueCount();
    }
    return closed;
}

QList<int> MainWindow::issuesLinkedFromPull(const PullRequest &pr) const
{
    QSet<int> linked;
    // An agent-created PR carries the issue number directly (matched by PR number
    // or through its head branch — issue #257).
    if (const AgentSession *session = agentSessionForPull(pr.number, pr.head))
        if (session->issueNumber > 0)
            linked.insert(session->issueNumber);
    // Plus any "closes #N" style reference in the PR text.
    static const QRegularExpression issueRefRe(
        QStringLiteral("\\bissue[-\\s]+#?(\\d+)\\b|"
                       "\\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\\b\\s*:?\\s*#(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    // Scan the PR text and every comment/review body so explicit "Linked issue
    // #N" notes posted into the conversation are picked up too.
    QStringList haystackParts{pr.title, pr.description, pr.head, pr.base};
    for (const PullEvent &ev : pr.events)
        if (!ev.body.isEmpty())
            haystackParts << ev.body;
    const QString haystack = haystackParts.join('\n');
    auto it = issueRefRe.globalMatch(haystack);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int number =
            (match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)).toInt();
        if (number > 0)
            linked.insert(number);
    }
    QList<int> result = linked.values();
    std::sort(result.begin(), result.end());
    return result;
}

QList<int> MainWindow::pullsLinkedToIssue(int issueNumber) const
{
    if (issueNumber <= 0)
        return {};
    QSet<int> linked;
    // Primary direction: any PR whose references point back at this issue.
    for (const PullRequest &pr : m_currentPulls)
        if (issuesLinkedFromPull(pr).contains(issueNumber))
            linked.insert(pr.number);
    // Secondary direction: explicit "pull request #M" / "PR #M" notes left in
    // this issue's own thread.
    static const QRegularExpression pullRefRe(
        QStringLiteral("\\b(?:pull[-\\s]request|pr)[-\\s]*#?(\\d+)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    for (const Issue &issue : m_currentIssues) {
        if (issue.number != issueNumber)
            continue;
        for (const IssueEvent &ev : issue.events) {
            if (ev.body.isEmpty())
                continue;
            auto pit = pullRefRe.globalMatch(ev.body);
            while (pit.hasNext()) {
                const int number = pit.next().captured(1).toInt();
                if (number > 0)
                    linked.insert(number);
            }
        }
        break;
    }
    QList<int> result = linked.values();
    std::sort(result.begin(), result.end());
    return result;
}

void MainWindow::postIssueLinkComment(int issueNumber, const QString &body)
{
    if (issueNumber <= 0)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        if (!store.addComment(issueNumber, body, {}, &error))
            logSystem(QStringLiteral("Issue #%1: could not post link note: %2")
                          .arg(issueNumber)
                          .arg(error));
        return;
    }
    // Read-only mirror: deliver a signed comment to the maintainer's inbox.
    const int idx = issuesRepoIndex();
    if (idx < 0 || !m_networkAccess)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);
    IssueEvent ev;
    ev.type = QStringLiteral("comment");
    ev.body = body;
    ev = store.makeSignedEvent(issueNumber, ev);
    ev.bodyFile = "comments/" + ev.id + ".md";
    QJsonObject eventJson = ev.toJson();
    eventJson.insert("body", ev.body);
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", issueNumber},
                              {"event", eventJson}};
    QNetworkRequest request(issuesApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void MainWindow::postPullLinkComment(int pullNumber, const QString &body)
{
    if (pullNumber <= 0)
        return;
    PullStore store = pullStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        if (!store.addComment(pullNumber, body, &error))
            logSystem(QStringLiteral("Pull request #%1: could not post link note: %2")
                          .arg(pullNumber)
                          .arg(error));
        return;
    }
    PullEvent ev;
    ev.type = QStringLiteral("comment");
    ev.body = body;
    ev = store.makeSignedEvent(pullNumber, ev);
    submitPullEventToInbox(pullNumber, ev);
}

void MainWindow::linkAgentPullToIssue(const AgentSession &session, int prNumber)
{
    if (session.issueNumber <= 0 || prNumber <= 0)
        return;
    const int ri = repoIndexFor(session.owner, session.name);
    if (ri < 0)
        return;
    // The agent's PR already lives in this repo, so its issue store is writable on
    // this node too; write the link note straight into the issue's thread.
    const RepositoryRecord &repo = writableRecordFor(m_repositories.at(ri));
    IssueStore store(repo.localPath, repo.mirrorPath, &m_profileIdentity, m_userName);
    if (!store.canWrite())
        return;
    QString error;
    if (!store.addComment(session.issueNumber,
                          QStringLiteral("Linked pull request #%1.").arg(prNumber),
                          {}, &error)) {
        if (m_agentStore)
            m_agentStore->appendLog(
                session,
                QStringLiteral("!! Could not link PR #%1 to issue #%2: %3\n")
                    .arg(prNumber)
                    .arg(session.issueNumber)
                    .arg(error));
        return;
    }
    if (ri == m_repoDetailIndex)
        reloadIssues();
}

void MainWindow::linkPullToIssueFromIssuePage()
{
    if (m_currentIssueNumber <= 0) {
        setIssueInlineNotice("Open an issue first.", true);
        return;
    }
    const int issueNumber = m_currentIssueNumber;
    const QList<int> linked = pullsLinkedToIssue(issueNumber);
    const QSet<int> already(linked.cbegin(), linked.cend());
    QStringList labels;
    QList<int> numbers;
    for (const PullRequest &pr : m_currentPulls) {
        if (already.contains(pr.number))
            continue;
        labels << QStringLiteral("#%1  %2").arg(pr.number).arg(pr.title);
        numbers << pr.number;
    }
    int chosen = -1;
    if (numbers.isEmpty()) {
        // No (unlinked) PRs loaded: let the user type a number directly.
        bool ok = false;
        const int n = QInputDialog::getInt(
            this, QStringLiteral("Link pull request"),
            QStringLiteral("Pull request number to link to issue #%1:").arg(issueNumber),
            1, 1, 1000000, 1, &ok);
        if (!ok)
            return;
        chosen = n;
    } else {
        bool ok = false;
        const QString pick = QInputDialog::getItem(
            this, QStringLiteral("Link pull request"),
            QStringLiteral("Link a pull request to issue #%1:").arg(issueNumber),
            labels, 0, false, &ok);
        if (!ok || pick.isEmpty())
            return;
        chosen = numbers.at(labels.indexOf(pick));
    }
    if (chosen <= 0)
        return;
    postIssueLinkComment(issueNumber,
                         QStringLiteral("Linked pull request #%1.").arg(chosen));
    postPullLinkComment(chosen,
                        QStringLiteral("Linked issue #%1.").arg(issueNumber));
    reloadIssues();
    reloadPulls();
    showIssue(issueNumber);
    flashMessage(QStringLiteral("Linked pull request #%1 to issue #%2.")
                     .arg(chosen)
                     .arg(issueNumber));
}

void MainWindow::linkIssueToPullFromPullPage()
{
    if (m_currentPullNumber <= 0) {
        flashMessage(QStringLiteral("Open a pull request first."));
        return;
    }
    const int pullNumber = m_currentPullNumber;
    QSet<int> already;
    for (const PullRequest &pr : m_currentPulls)
        if (pr.number == pullNumber) {
            const QList<int> linked = issuesLinkedFromPull(pr);
            already = QSet<int>(linked.cbegin(), linked.cend());
        }
    QStringList labels;
    QList<int> numbers;
    for (const Issue &issue : m_currentIssues) {
        if (already.contains(issue.number) || issue.isDeleted())
            continue;
        labels << QStringLiteral("#%1  %2").arg(issue.number).arg(issue.title);
        numbers << issue.number;
    }
    int chosen = -1;
    if (numbers.isEmpty()) {
        bool ok = false;
        const int n = QInputDialog::getInt(
            this, QStringLiteral("Link issue"),
            QStringLiteral("Issue number to link to pull request #%1:").arg(pullNumber),
            1, 1, 1000000, 1, &ok);
        if (!ok)
            return;
        chosen = n;
    } else {
        bool ok = false;
        const QString pick = QInputDialog::getItem(
            this, QStringLiteral("Link issue"),
            QStringLiteral("Link an issue to pull request #%1:").arg(pullNumber),
            labels, 0, false, &ok);
        if (!ok || pick.isEmpty())
            return;
        chosen = numbers.at(labels.indexOf(pick));
    }
    if (chosen <= 0)
        return;
    postPullLinkComment(pullNumber,
                        QStringLiteral("Linked issue #%1.").arg(chosen));
    postIssueLinkComment(chosen,
                         QStringLiteral("Linked pull request #%1.").arg(pullNumber));
    reloadIssues();
    reloadPulls();
    showPull(pullNumber);
    flashMessage(QStringLiteral("Linked issue #%1 to pull request #%2.")
                     .arg(chosen)
                     .arg(pullNumber));
}

void MainWindow::fundBountiesForMergedPull(const PullRequest &pr)
{
    const int idx = issuesRepoIndex();
    if (idx < 0 || !m_networkAccess)
        return;
    const RepositoryRecord repo = m_repositories.at(idx);
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite())
        return;

    QHash<int, Issue> byNumber;
    for (const Issue &issue : store.loadAll())
        byNumber.insert(issue.number, issue);

    for (const int number : issuesLinkedFromPull(pr)) {
        const Issue issue = byNumber.value(number);
        if (issue.number <= 0 || issue.bountyUsd <= 0 ||
            issue.bountyStatus == QLatin1String("paid"))
            continue;
        const double amount = issue.bountyUsd;
        const QString question =
            QStringLiteral("Issue #%1 has a $%2 bounty. Show the funding QR now?\n\n"
                           "Send the SOL to the escrow address; on payout 90%% goes "
                           "to the pull request author and 10%% to the ForkMesh "
                           "treasury.")
                .arg(number)
                .arg(QString::number(amount, 'f', 2));
        if (QMessageBox::question(this, QStringLiteral("Fund bounty"), question) !=
            QMessageBox::Yes)
            continue;

        // Bounties are pledged on the issue without paying up front; the escrow
        // deposit address is minted here, at merge time, and its funding QR is
        // shown so the maintainer can fund the now-completed work. The PR author
        // is passed as the payee so the worker can split a funded escrow to the
        // author + treasury automatically (no second manual step).
        // The relay only lets the repo owner create a bounty and authorize its
        // payee, so sign owner/repo/number/payee/ts with the node identity. The
        // payee identifier is the PR author's node name (the relay resolves it to
        // their registered payout wallet); lowercased to match the relay's
        // canonical form.
        const QString payeeNode = pr.authorName.trimmed().toLower();
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        const QByteArray canonical =
            ("forkmesh-bounty-create-v1\n" + repo.owner + "\n" + repo.name + "\n" +
             QString::number(number) + "\n" + payeeNode + "\n" + ts).toUtf8();
        const QString sig = m_profileIdentity.signData(canonical);
        const QJsonObject payload{{"action", "create"},
                                  {"owner", repo.owner},
                                  {"repo", repo.name},
                                  {"number", number},
                                  {"amountUsd", amount},
                                  {"payeeNode", payeeNode},
                                  {"ts", ts},
                                  {"sig", sig}};
        QNetworkRequest request(bountyApiUrl(repo));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *reply = m_networkAccess->post(
            request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, repo, number, amount] {
                    const QByteArray body = reply->readAll();
                    reply->deleteLater();
                    const QJsonObject obj = QJsonDocument::fromJson(body).object();
                    const QString address = obj.value("address").toString();
                    if (reply->error() != QNetworkReply::NoError || address.isEmpty()) {
                        flashMessage(
                            QStringLiteral("Could not create the bounty deposit for "
                                           "#%1: %2")
                                .arg(number)
                                .arg(obj.value("error").toString(
                                    reply->errorString())),
                            true);
                        return;
                    }
                    const QString uri = obj.value("uri").toString(
                        QStringLiteral("solana:%1").arg(address));
                    // The worker prices the USD bounty into SOL (via the live SOL
                    // price) and bakes the amount into the Solana Pay URI; show
                    // that exact SOL figure so the funder sends the right amount.
                    const QString amountSol = obj.value("amountSol").toString();
                    // Record the escrow address on the issue (still unpaid).
                    IssueStore writeStore = issueStoreForCurrentRepo();
                    QString error;
                    writeStore.setBounty(number, amount, address,
                                         QStringLiteral("open"), &error);
                    logSystem(QString::fromUtf8("Bounty escrow for issue #%1 ready to "
                                             "fund ($%2 \xE2\x89\x88 %3 SOL).")
                                  .arg(number)
                                  .arg(QString::number(amount, 'f', 2))
                                  .arg(amountSol));
                    if (m_repoDetailIndex == issuesRepoIndex())
                        reloadIssues();
                    // The dialog shows the QR, polls for the deposit, and on
                    // confirmation records the paid split; if the funder closes it
                    // early, a background watcher (and the worker cron) still pay.
                    showBountyQrDialog(repo, number, uri, address, amount, amountSol);
                });
    }
}

void MainWindow::autoBountyForMergedPull(const PullRequest &pr)
{
    // Issue #347: reward every merged PR's author with the configured fixed
    // bounty, independent of any issue bounty. Only the repo owner can create a
    // bounty (the worker requires an owner signature), so this is a no-op on a
    // node that doesn't own the repo.
    if (!QSettings().value(kAutoPrBountyEnabledSetting, false).toBool())
        return;
    if (!m_networkAccess || !m_profileIdentity.isValid())
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    if (repo.owner.isEmpty() || repo.owner != accountOwner())
        return;
    const QString payeeNode = pr.authorName.trimmed().toLower();
    if (payeeNode.isEmpty())
        return;
    // The bounty amount reuses the USD-priced issue-bounty pipeline (min $1).
    const double amount = QSettings().value(kAutoPrBountyAmountSetting, 1.0).toDouble();
    if (amount < 1.0)
        return;
    const bool walletMode =
        QSettings().value(kAutoPrBountyModeSetting).toString() ==
        QLatin1String("wallet");
    const int number = pr.number;

    // Owner-signed create, keyed to the PR (kind "pr"). The canonical matches the
    // issue-bounty flow (it binds owner/repo/number/payee); "pr" only affects the
    // worker's storage key so an issue and a PR sharing a number don't collide.
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-bounty-create-v1\n" + repo.owner + "\n" + repo.name + "\n" +
         QString::number(number) + "\n" + payeeNode + "\n" + ts).toUtf8();
    const QJsonObject payload{{"action", "create"},
                              {"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", number},
                              {"kind", QStringLiteral("pr")},
                              {"amountUsd", amount},
                              {"payeeNode", payeeNode},
                              {"fromWallet", walletMode},
                              {"ts", ts},
                              {"sig", m_profileIdentity.signData(canonical)}};
    QNetworkRequest request(bountyApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, repo, number, amount, walletMode] {
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        if (reply->error() != QNetworkReply::NoError) {
            const QString err = obj.value("error").toString(reply->errorString());
            if (err == QLatin1String("insufficient_wallet_balance"))
                flashMessage(
                    QStringLiteral("Pull #%1 merged, but the inbuilt bounty wallet "
                                   "is low on SOL — top it up in Settings to pay "
                                   "its $%2 reward.")
                        .arg(number)
                        .arg(QString::number(amount, 'f', 2)),
                    true);
            else if (err == QLatin1String("no_wallet"))
                flashMessage(
                    QStringLiteral("Pull #%1 merged, but no inbuilt bounty wallet is "
                                   "funded yet — set one up in Settings.")
                        .arg(number),
                    true);
            else if (err == QLatin1String("payee_unresolved"))
                flashMessage(
                    QStringLiteral("Pull #%1 merged, but its author has no Solana "
                                   "payout address, so no bounty was paid.")
                        .arg(number),
                    true);
            else
                flashMessage(
                    QStringLiteral("Could not reward pull #%1: %2").arg(number).arg(err),
                    true);
            return;
        }
        if (walletMode) {
            // The worker debits the inbuilt wallet and pays out in one step.
            logSystem(QStringLiteral("Rewarded pull #%1's author with a $%2 bounty "
                                     "from the inbuilt wallet (tx %3).")
                          .arg(number)
                          .arg(QString::number(amount, 'f', 2))
                          .arg(obj.value("payoutSig").toString().left(12)));
            flashMessage(QStringLiteral("Paid pull #%1's author a $%2 bounty from the "
                                        "inbuilt wallet.")
                             .arg(number)
                             .arg(QString::number(amount, 'f', 2)));
            return;
        }
        // Pay-per-PR: mint the escrow and show the funding QR for this merge.
        const QString address = obj.value("address").toString();
        if (address.isEmpty()) {
            flashMessage(QStringLiteral("Could not create the reward deposit for "
                                        "pull #%1.").arg(number),
                         true);
            return;
        }
        const QString uri = obj.value("uri").toString(
            QStringLiteral("solana:%1").arg(address));
        const QString amountSol = obj.value("amountSol").toString();
        logSystem(QString::fromUtf8("Reward escrow for pull #%1 ready to fund "
                                    "($%2 \xE2\x89\x88 %3 SOL).")
                      .arg(number)
                      .arg(QString::number(amount, 'f', 2))
                      .arg(amountSol));
        showBountyQrDialog(repo, number, uri, address, amount, amountSol,
                           QStringLiteral("pr"));
    });
}

void MainWindow::pollBountyPayout(const RepositoryRecord &repo, int number,
                                  double amount, const QString &kind)
{
    if (!m_networkAccess)
        return;
    const bool isPr = kind == QLatin1String("pr");
    // Poll the escrow status; the worker auto-splits a funded escrow to the
    // author + treasury when status is checked. Stop once paid (or give up after
    // a generous window — the cron backstop still pays it out either way).
    auto *attempts = new int(0);
    auto *timer = new QTimer(this);
    timer->setInterval(8000);
    connect(timer, &QTimer::timeout, this,
            [this, repo, number, amount, isPr, attempts, timer] {
        if (!m_networkAccess || ++(*attempts) > 75) { // ~10 minutes
            timer->stop();
            timer->deleteLater();
            delete attempts;
            return;
        }
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
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, repo, number, amount, isPr, attempts, timer] {
            const QByteArray body = reply->readAll();
            reply->deleteLater();
            const QJsonObject obj = QJsonDocument::fromJson(body).object();
            if (obj.value("status").toString() != QLatin1String("paid"))
                return;
            timer->stop();
            timer->deleteLater();
            delete attempts;
            const QString subject = isPr ? QStringLiteral("pull request #%1")
                                         : QStringLiteral("issue #%1");
            if (!isPr) {
                IssueStore writeStore = issueStoreForCurrentRepo();
                QString error;
                writeStore.setBounty(number, amount,
                                     obj.value("payee").toString(),
                                     QStringLiteral("paid"), &error);
                if (m_repoDetailIndex == issuesRepoIndex())
                    reloadIssues();
            }
            logSystem(QStringLiteral("Bounty for %1 funded and split to the "
                                     "author + treasury (tx %2).")
                          .arg(subject.arg(number))
                          .arg(obj.value("payoutSig").toString().left(12)));
            flashMessage(QStringLiteral("Bounty for %1 paid out to the author "
                                        "+ treasury.")
                             .arg(subject.arg(number)));
        });
    });
    timer->start();
}


void MainWindow::closeCurrentPull()
{
    if (m_currentPullNumber < 0)
        return;
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (!store.setStatus(m_currentPullNumber, "closed", &error))
        QMessageBox::warning(this, "Close pull request", error);
    reloadPulls();
}

void MainWindow::reopenCurrentPull()
{
    if (m_currentPullNumber < 0)
        return;
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (!store.setStatus(m_currentPullNumber, "open", &error))
        QMessageBox::warning(this, "Reopen pull request", error);
    reloadPulls();
}

void MainWindow::sendCurrentPullToSource()
{
    if (m_currentPullNumber < 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const PullRequest *pr = nullptr;
    for (const PullRequest &candidate : std::as_const(m_currentPulls)) {
        if (candidate.number == m_currentPullNumber) {
            pr = &candidate;
            break;
        }
    }
    if (!pr)
        return;
    // The PR was synced from the mirror with its original author/signature intact;
    // deliver it as-authored so the owner's inbox can verify it. Without a
    // signature there's nothing the source of truth would accept.
    if (pr->sig.isEmpty() || pr->author.isEmpty()) {
        QMessageBox::warning(
            this, "Send to source of truth",
            "This pull request is missing its signature, so it can't be delivered "
            "to the source of truth.");
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (QMessageBox::question(
            this, "Send to source of truth",
            QStringLiteral(
                "Deliver pull request #%1 to %2/%3's inbox?\n\n"
                "The relay queues it, so it reaches the source of truth even if "
                "that node is currently offline.")
                .arg(m_currentPullNumber)
                .arg(repo.owner, repo.name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return;
    submitPullToInbox(*pr, repo);
}

// Toggle the pull-delete buttons together so none can launch a second history
// rewrite while one worker thread is running. The proper enable state is
// restored by updatePullActionState() (via reloadPulls) once the worker settles.
void MainWindow::setPullDeleteButtonsEnabled(bool enabled)
{
    if (m_pullDeleteButton)
        m_pullDeleteButton->setEnabled(enabled);
    if (m_pullDeleteBranchButton)
        m_pullDeleteBranchButton->setEnabled(enabled);
    if (m_pullMergeDeleteButton)
        m_pullMergeDeleteButton->setEnabled(enabled);
}

bool MainWindow::confirmPullDeletion(const QString &prompt, bool *rewriteHistory)
{
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Delete pull request"),
                    prompt, QMessageBox::Ok | QMessageBox::Cancel, this);
    // Opt-in, off by default: the plain delete just removes the PR record (and is
    // fast); ticking this also runs the slow filter-branch scrub of the diff text.
    auto *purge = new QCheckBox(
        QStringLiteral("Also scrub the PR's diff from git history (slow)"));
    purge->setChecked(false);
    box.setCheckBox(purge); // QMessageBox takes ownership
    const bool confirmed = box.exec() == QMessageBox::Ok;
    if (rewriteHistory)
        *rewriteHistory = confirmed && purge->isChecked();
    return confirmed;
}

void MainWindow::deleteCurrentPull()
{
    if (m_currentPullNumber < 0)
        return;
    if (m_pullDeleteInProgress) {
        setRepoDetailNotice(
            QStringLiteral("A pull request deletion is already running."));
        return;
    }
    bool rewriteHistory = false;
    if (!m_pullDeleteConfirmPending) {
        m_pullDeleteConfirmPending = true;
        // Show a message box confirmation instead of inline notice (the pull
        // detail panel has no equivalent inline notice widget).
        const QString prompt =
            QStringLiteral("Permanently delete pull request #%1? This cannot be undone.")
                .arg(m_currentPullNumber);
        const bool confirmed = confirmPullDeletion(prompt, &rewriteHistory);
        m_pullDeleteConfirmPending = false;
        if (!confirmed)
            return;
    }

    // The plain delete just drops the PR folder at the tip and is fast. Only the
    // opt-in history rewrite (filter-branch + gc + reflog expire) is slow enough to
    // freeze the UI — either way, run it on a worker thread (deletePull only touches
    // git, no event signing, so a copied store is safe) and report back on the main
    // thread.
    const int deleted = m_currentPullNumber;
    m_pullDeleteInProgress = true;
    setPullDeleteButtonsEnabled(false);
    setRepoDetailNotice(
        rewriteHistory
            ? QStringLiteral("Deleting pull request #%1 and rewriting history… this "
                             "can take a while.")
                  .arg(deleted)
            : QStringLiteral("Deleting pull request #%1…").arg(deleted));
    QApplication::setOverrideCursor(Qt::BusyCursor);

    PullStore store = pullStoreForCurrentRepo();
    auto ok = std::make_shared<bool>(false);
    auto error = std::make_shared<QString>();
    QThread *worker =
        QThread::create([store, deleted, rewriteHistory, ok, error]() mutable {
            QString err;
            *ok = store.deletePull(deleted, rewriteHistory, &err);
            *error = err;
        });
    connect(worker, &QThread::finished, this,
            [this, worker, ok, error, deleted]() {
                m_pullDeleteInProgress = false;
                QApplication::restoreOverrideCursor();
                setPullDeleteButtonsEnabled(true);
                if (!*ok) {
                    QMessageBox::warning(
                        this, "Delete pull request",
                        error->isEmpty()
                            ? QStringLiteral("Could not delete the pull request.")
                            : *error);
                    worker->deleteLater();
                    return;
                }
                if (m_currentPullNumber == deleted)
                    m_currentPullNumber = -1;
                reloadPulls();
                setRepoDetailNotice(
                    QStringLiteral("Deleted pull request #%1.").arg(deleted));
                worker->deleteLater();
            });
    worker->start();
}

// Delete the pull request and the local head branch it was opened from, in one
// confirmed step. The branch only exists on the node that authored the PR, so
// removing it is best-effort: a PR imported from another node (no local branch)
// still deletes cleanly.
void MainWindow::deleteCurrentPullAndBranch()
{
    if (m_currentPullNumber < 0)
        return;
    if (m_pullDeleteInProgress) {
        setRepoDetailNotice(
            QStringLiteral("A pull request deletion is already running."));
        return;
    }
    // Resolve the PR's head branch before anything is removed.
    QString head, base;
    for (const PullRequest &p : std::as_const(m_currentPulls)) {
        if (p.number == m_currentPullNumber) {
            head = p.head;
            base = p.base;
            break;
        }
    }
    // Never touch the base branch (or the branch currently checked out): only a
    // distinct feature branch is a safe target.
    const bool haveBranch =
        !head.isEmpty() && head != base && head != currentRef();

    const QString prompt =
        haveBranch
            ? QStringLiteral("Permanently delete pull request #%1 and its branch "
                             "\"%2\"? This cannot be undone.")
                  .arg(m_currentPullNumber)
                  .arg(head)
            : QStringLiteral("Permanently delete pull request #%1? This cannot be "
                             "undone.")
                  .arg(m_currentPullNumber);
    bool rewriteHistory = false;
    if (!confirmPullDeletion(prompt, &rewriteHistory))
        return;

    deletePullAndBranchAsync(m_currentPullNumber, head, haveBranch, rewriteHistory,
                             /*propagate=*/false);
}

// Merge the pull request, then delete it and its head branch in one confirmed
// step — the "merge, delete PR + branch" workflow (issue #261). The merge runs
// first and synchronously; only if it succeeds do we drop the PR record and its
// branch. The deletion (and best-effort branch removal) reuses the same worker
// flow as deleteCurrentPullAndBranch.
void MainWindow::mergeAndDeleteCurrentPull()
{
    if (m_currentPullNumber < 0)
        return;
    if (m_pullDeleteInProgress) {
        setRepoDetailNotice(
            QStringLiteral("A pull request deletion is already running."));
        return;
    }
    PullRequest current;
    bool found = false;
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (pr.number == m_currentPullNumber) {
            current = pr;
            found = true;
            break;
        }
    }
    if (!found)
        return;
    if (current.reviewSummary() == QLatin1String("changes_requested")) {
        QMessageBox::warning(
            this, "Merge pull request",
            QStringLiteral("Pull request #%1 has an unresolved \"request changes\" "
                           "review. Resolve the review (approve it, or clear the "
                           "request) before merging.")
                .arg(m_currentPullNumber));
        return;
    }

    // Never touch the base branch (or the branch currently checked out): only a
    // distinct feature branch is a safe target.
    const QString head = current.head;
    const bool haveBranch =
        !head.isEmpty() && head != current.base && head != currentRef();

    const QString prompt =
        haveBranch
            ? QStringLiteral("Merge pull request #%1, then permanently delete it and "
                             "its branch \"%2\"? This cannot be undone.")
                  .arg(m_currentPullNumber)
                  .arg(head)
            : QStringLiteral("Merge pull request #%1, then permanently delete it? "
                             "This cannot be undone.")
                  .arg(m_currentPullNumber);
    bool rewriteHistory = false;
    if (!confirmPullDeletion(prompt, &rewriteHistory))
        return;

    // Merge first, synchronously. If it fails (e.g. fresh conflicts) bail out
    // before touching the PR record or its branch.
    PullStore store = pullStoreForCurrentRepo();
    QString error;
    if (!store.mergePull(m_currentPullNumber, &error)) {
        QMessageBox::warning(this, "Merge pull request", error);
        return;
    }
    logSystem(QStringLiteral("Merged pull request #%1.").arg(m_currentPullNumber));
    closeIssuesLinkedFromPull(current);
    fundBountiesForMergedPull(current);
    autoBountyForMergedPull(current);
    // Issue #291: flag the agent session behind this PR before its branch/record
    // are deleted below (after which it can no longer be detected on reload).
    markAgentSessionsMerged(m_currentPullNumber, head);

    // Now delete the merged PR and its branch. propagate=true so the merge (and
    // the PR's removal) reaches peers via the mirror.
    deletePullAndBranchAsync(m_currentPullNumber, head, haveBranch, rewriteHistory,
                             /*propagate=*/true);
}

// Shared worker: delete the PR record (optionally scrubbing its diff from
// history) on a background thread, then best-effort remove its local head branch
// and reload the lists on the main thread. The plain delete just drops the PR
// folder (and the branch ref) and is fast; only the opt-in history rewrite
// (filter-branch + gc + reflog expire) is slow enough to need a worker — either
// way deletePull only touches git (no event signing) so a copied store is safe.
void MainWindow::deletePullAndBranchAsync(int number, const QString &head,
                                          bool haveBranch, bool rewriteHistory,
                                          bool propagate)
{
    const int deleted = number;
    const QString dir = repoGitDir();
    const bool haveWorkTree = repoHasWorkingTree();
    m_pullDeleteInProgress = true;
    setRepoDetailNotice(
        rewriteHistory
            ? QStringLiteral("Deleting pull request #%1 and rewriting history… this "
                             "can take a while.")
                  .arg(deleted)
            : QStringLiteral("Deleting pull request #%1…").arg(deleted));
    setPullDeleteButtonsEnabled(false);
    QApplication::setOverrideCursor(Qt::BusyCursor);

    PullStore store = pullStoreForCurrentRepo();
    auto ok = std::make_shared<bool>(false);
    auto error = std::make_shared<QString>();
    QThread *worker =
        QThread::create([store, deleted, rewriteHistory, ok, error]() mutable {
            QString err;
            *ok = store.deletePull(deleted, rewriteHistory, &err);
            *error = err;
        });
    connect(worker, &QThread::finished, this,
            [this, worker, ok, error, deleted, head, haveBranch, dir, haveWorkTree,
             propagate]() {
                m_pullDeleteInProgress = false;
                QApplication::restoreOverrideCursor();
                setPullDeleteButtonsEnabled(true);
                if (!*ok) {
                    QMessageBox::warning(
                        this, "Delete pull request",
                        error->isEmpty()
                            ? QStringLiteral("Could not delete the pull request.")
                            : *error);
                    worker->deleteLater();
                    return;
                }
                if (m_currentPullNumber == deleted)
                    m_currentPullNumber = -1;

                // Best-effort branch removal; -D force-deletes since the user
                // confirmed and the PR carrying the work is gone. A missing branch
                // is not an error here.
                QString branchErrorNotice;
                if (haveBranch && haveWorkTree && !dir.isEmpty()) {
                    QString branchErr;
                    if (runGitCapture(dir, {"branch", "-D", head}, nullptr,
                                      &branchErr)) {
                        logSystem(QStringLiteral("Git: deleted branch %1 for PR #%2.")
                                      .arg(head)
                                      .arg(deleted));
                        loadBranchesAndTags();
                    } else if (!branchErr.trimmed().isEmpty()) {
                        branchErrorNotice =
                            QStringLiteral("Deleted PR #%1, but its branch could not "
                                           "be removed: %2")
                                .arg(deleted)
                                .arg(branchErr.trimmed());
                    }
                }
                reloadPulls();
                if (branchErrorNotice.isEmpty())
                    setRepoDetailNotice(
                        QStringLiteral("Deleted pull request #%1.").arg(deleted));
                else
                    setRepoDetailNotice(branchErrorNotice, true);
                if (propagate)
                    propagateRepoUpdate(m_repoDetailIndex);
                worker->deleteLater();
            });
    worker->start();
}

QUrl MainWindow::pullsApiUrl(const RepositoryRecord &repo) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
                repoSegment(repo.name, QStringLiteral("repository")) + "/pulls");
    return url;
}

void MainWindow::submitPullToInbox(const PullRequest &pr)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    submitPullToInbox(pr, repo);
}

void MainWindow::submitPullToInbox(const PullRequest &pr,
                                   const RepositoryRecord &targetRepo, bool quiet)
{
    const QJsonObject payload{{"owner", targetRepo.owner},
                              {"repo", targetRepo.name},
                              {"pull", pr.toJson()}};
    QNetworkRequest request(pullsApiUrl(targetRepo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, targetRepo, quiet] {
        reply->deleteLater();
        const QString slug = targetRepo.owner + "/" + targetRepo.name;
        if (reply->error() == QNetworkReply::NoError) {
            if (quiet)
                logSystem("Pull request delivered to " + slug + " (source of truth).");
            else
                QMessageBox::information(
                    this, "Pull request sent",
                    "Your signed pull request was delivered to " + slug + ".");
        } else if (quiet) {
            flashMessage("Could not send the pull request to " + slug + ": " +
                             reply->errorString(),
                         true);
        } else {
            QMessageBox::warning(this, "Pull request",
                                 "Could not send the pull request: " +
                                     reply->errorString());
        }
    });
}

void MainWindow::submitPullEventToInbox(int number, const PullEvent &ev)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", number},
                              {"event", ev.toJson()}};
    QNetworkRequest request(pullsApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, repo] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            flashMessage("Your review was delivered to " + repo.owner + "/" +
                         repo.name + ".");
        else
            QMessageBox::warning(this, "Review",
                                 "Could not send your review: " + reply->errorString());
    });
}

QUrl MainWindow::commitsApiUrl(const RepositoryRecord &repo) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) + "/" +
                repoSegment(repo.name, QStringLiteral("repository")) + "/commits");
    return url;
}

void MainWindow::submitCommitCommentToInbox(const QString &sha, const CommitComment &c)
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"sha", sha},
                              {"comment", c.toJson()}};
    QNetworkRequest request(commitsApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, repo] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            flashMessage("Your commit comment was delivered to " + repo.owner + "/" +
                         repo.name + ".");
        else
            QMessageBox::warning(this, "Commit comment",
                                 "Could not send your comment: " + reply->errorString());
    });
}

void MainWindow::drainCommitInboxFor(RepositoryRecord repo, bool interactive)
{
    const RepositoryRecord writable = writableRecordFor(repo);
    {
        CommitCommentStore probe(writable.localPath, writable.mirrorPath,
                                 &m_profileIdentity, m_userName);
        if (!probe.canWrite())
            return;
    }
    QUrl url = commitsApiUrl(repo);
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
                QMessageBox::warning(this, "Sync inbox",
                                     "Could not reach the inbox: " +
                                         reply->errorString());
            return;
        }
        m_pollBackoff.noteSuccess(backoffKey);
        const QJsonArray pending = QJsonDocument::fromJson(reply->readAll())
                                       .object()
                                       .value("pending")
                                       .toArray();
        if (pending.isEmpty()) {
            if (interactive)
                QMessageBox::information(this, "Sync inbox",
                                         "No pending commit comments.");
            return;
        }
        CommitCommentStore store(writable.localPath, writable.mirrorPath,
                                 &m_profileIdentity, m_userName);
        int merged = 0;
        for (const QJsonValue &value : pending) {
            const QJsonObject obj = value.toObject();
            const QString sha = obj.value("sha").toString();
            const CommitComment c =
                CommitComment::fromJson(obj.value("comment").toObject());
            if (store.applyRemoteComment(sha, c))
                ++merged;
        }
        m_networkAccess->deleteResource(QNetworkRequest(url)); // ack/clear
        const bool onThisRepo =
            m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size() &&
            m_repositories.at(m_repoDetailIndex).owner == repo.owner &&
            m_repositories.at(m_repoDetailIndex).name == repo.name;
        if (onThisRepo && !m_currentCommitHash.isEmpty())
            renderCommitThread(m_currentCommitHash);
        if (interactive)
            QMessageBox::information(
                this, "Sync inbox",
                QStringLiteral("Merged %1 commit comment(s).").arg(merged));
    });
}

void MainWindow::syncPullsInbox()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    drainPullsInboxFor(m_repositories.at(m_repoDetailIndex), /*interactive=*/true);
}

void MainWindow::drainPullsInboxFor(RepositoryRecord repo, bool interactive)
{
    const RepositoryRecord writable = writableRecordFor(repo);
    {
        PullStore probe(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                        m_userName);
        if (!probe.canWrite())
            return;
    }

    QUrl url = pullsApiUrl(repo);
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
                QMessageBox::warning(this, "Sync inbox",
                                     "Could not reach the inbox: " +
                                         reply->errorString());
            return;
        }
        m_pollBackoff.noteSuccess(backoffKey);
        const QJsonArray pending = QJsonDocument::fromJson(reply->readAll())
                                       .object()
                                       .value("pending")
                                       .toArray();
        if (pending.isEmpty()) {
            if (interactive)
                QMessageBox::information(this, "Sync inbox",
                                         "No pending pull requests.");
            return;
        }
        PullStore store(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                        m_userName);
        int merged = 0;
        QString lastAuthor;
        QString lastTitle;
        for (const QJsonValue &value : pending) {
            const QJsonObject obj = value.toObject();
            // A submission is either a whole new PR ("pull") or a conversation
            // event on an existing PR ("event" + "number").
            if (obj.contains("event")) {
                const int number = obj.value("number").toInt();
                const PullEvent ev =
                    PullEvent::fromJson(obj.value("event").toObject());
                if (store.applyRemoteEvent(number, ev)) {
                    ++merged;
                    lastAuthor = ev.authorName.isEmpty() ? ev.author.left(8)
                                                         : ev.authorName;
                    lastTitle = QStringLiteral("review on #%1").arg(number);
                }
                continue;
            }
            const PullRequest pr =
                PullRequest::fromJson(obj.value("pull").toObject());
            if (store.applyRemotePull(pr)) {
                ++merged;
                lastAuthor = pr.authorName.isEmpty() ? pr.author.left(8)
                                                     : pr.authorName;
                lastTitle = pr.title;
            }
        }
        m_networkAccess->deleteResource(QNetworkRequest(url)); // ack/clear
        const bool onThisRepo =
            m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size() &&
            m_repositories.at(m_repoDetailIndex).owner == repo.owner &&
            m_repositories.at(m_repoDetailIndex).name == repo.name;
        if (onThisRepo)
            reloadPulls();
        // Incoming PRs just landed in the working copy: push them to the mirror
        // and notify peers now so every node's count converges promptly.
        if (merged > 0) {
            propagateRepoUpdate(repoIndexFor(repo.owner, repo.name));
            // An inbound PR or review may @mention the owner running this node.
            scanRepoMentionsFor(writable);
        }
        if (interactive) {
            QMessageBox::information(
                this, "Sync inbox",
                QStringLiteral("Merged %1 pull request(s) into pulls/.").arg(merged));
        } else if (merged > 0) {
            const QString body =
                merged == 1
                    ? QStringLiteral("%1 opened a pull request: %2")
                          .arg(lastAuthor, lastTitle)
                    : QStringLiteral("%1 new pull requests on %2/%3")
                          .arg(merged)
                          .arg(repo.owner, repo.name);
            flashMessage(body);
            if (notifyEnabled(kPullAlertSetting))
                notifyIfInactive(
                    QString::fromUtf8("ForkMesh \xE2\x80\x94 new pull request"), body);
            if (notifyEnabled(kPullAlertSetting) && m_trayIcon &&
                QSystemTrayIcon::supportsMessages())
                m_trayIcon->showMessage("ForkMesh — new pull request", body,
                                        QSystemTrayIcon::Information, 6000);
        }
    });
}

void MainWindow::pollOwnedInboxes()
{
    if (!m_networkAccess)
        return;
    // Drain each owned repo's inboxes once. Dedup by owner/name so a preview and
    // its owned copy don't both poll the same inbox.
    QSet<QString> seen;
    for (const RepositoryRecord &repo : m_repositories) {
        if (repo.previewOnly)
            continue;
        const QString key = repo.owner + "/" + repo.name;
        if (seen.contains(key))
            continue;
        const RepositoryRecord writable = writableRecordFor(repo);
        IssueStore probe(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                         m_userName);
        if (!probe.canWrite())
            continue; // not the owner of this repo; nothing to drain
        seen.insert(key);
        // Auto-sync incoming issues only when the option is on (Settings →
        // Repositories) and the working tree is clean, so issue commits never
        // land on top of in-progress edits. Otherwise leave them in the inbox
        // for a manual "Sync inbox" (issue #193).
        const bool autoSyncIssues =
            QSettings().value(kAutoSyncIssuesSetting, true).toBool();
        if (autoSyncIssues && worktreeTrackedClean(writable.localPath))
            drainIssuesInboxFor(repo, /*interactive=*/false);
        drainPullsInboxFor(repo, /*interactive=*/false);
        drainDiscussionsInboxFor(repo, /*interactive=*/false);
        drainCommitInboxFor(repo, /*interactive=*/false);
    }
}

