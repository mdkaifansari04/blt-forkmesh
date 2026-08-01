






#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "FederatedThreadView.h"
#include "KebabHeaderView.h"
#include "PacmanProgress.h"
#include "PullAiReview.h"
#include "PullBadgeWidget.h"

using namespace forkmesh::ui;

namespace {




constexpr int kCommitShaRole = Qt::UserRole;
constexpr int kCommitMessageRole = Qt::UserRole + 1;
constexpr int kCommitCopyShaRole = Qt::UserRole + 2;


constexpr int kPullFileAgentRole = Qt::UserRole + 1;






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
}



QWidget *MainWindow::buildPullsTab()
{
    auto *page = new QWidget;


    auto *listPane = new QWidget;
    listPane->setMinimumWidth(360);
    auto *heading = new QLabel("Pull requests");
    heading->setObjectName("channelTitle");


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
            m_pullDetail->show();
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
    m_pullDeleteAllMergedButton = new QPushButton("Delete all merged + branches");
    for (QPushButton *b : {m_pullNewButton, m_pullChooseDirButton, m_pullImportButton,
                           m_pullSyncButton, m_pullDeleteAllMergedButton}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    setOcticon(m_pullNewButton, "plus", 16);
    setOcticon(m_pullChooseDirButton, "file-directory", 16);
    setOcticon(m_pullImportButton, "download", 16);
    setOcticon(m_pullSyncButton, "sync", 16);
    setOcticon(m_pullDeleteAllMergedButton, "trash", 16);
    m_pullChooseDirButton->setToolTip("Create a pull request from another local checkout of this repository");
    m_pullImportButton->setToolTip("Open a .patch/.diff file (e.g. a downloaded commit) as a pull request");
    m_pullSyncButton->setToolTip("Pull PR submissions filed by other nodes and merge them");
    m_pullDeleteAllMergedButton->setToolTip(
        "Delete every merged pull request in this repo and its head branch");
    connect(m_pullImportButton, &QPushButton::clicked, this,
            &MainWindow::importPatchAsPull);
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(m_pullNewButton);
    toolbar->addWidget(m_pullChooseDirButton);
    toolbar->addWidget(m_pullImportButton);
    toolbar->addWidget(m_pullSyncButton);
    toolbar->addWidget(m_pullDeleteAllMergedButton);
    toolbar->addStretch();

    m_pullSearch = new QLineEdit;
    m_pullSearch->setObjectName("issueSearch");
    m_pullSearch->setPlaceholderText("Search pull requests\xE2\x80\xA6");
    m_pullSearch->setClearButtonEnabled(true);

    m_pullTable = new QTableWidget(0, 10);
    m_pullTable->setObjectName("issueTable");
    installColumnHeaderMenu(m_pullTable);
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


    m_pullDetail = new QWidget;
    m_pullTitle = new QLabel("Select a pull request");
    m_pullTitle->setObjectName("channelTitle");
    m_pullTitle->setWordWrap(true);
    m_pullUpdateButton = new QPushButton("Update branch");
    m_pullMergeButton = new QPushButton("Merge");
    m_pullResolveButton = new QPushButton("Resolve conflicts\xE2\x80\xA6");
    m_pullFixButton = new QPushButton("Fix");
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
        // Every header action wears the flat rail style (adhoc #7) — the filled
        // green/grey pills that used to single out Merge and the AI actions made
        // an otherwise uniform toolbar read as three unrelated bars.
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }

    m_pullSplitButton->setCheckable(true);
    m_pullSplitButton->setChecked(diffSplitPref());
    setOcticon(m_pullSplitButton, "diff", 16);
    updateDiffSplitButton(m_pullSplitButton);
    connect(m_pullSplitButton, &QPushButton::clicked, this, [this](bool on) {
        setDiffSplitPref(on);
        updateDiffSplitButton(m_pullSplitButton);
        for (QPushButton *b : {m_commitSplitButton, m_branchSplitButton}) {
            if (b) {
                b->setChecked(on);
                updateDiffSplitButton(b);
            }
        }
        if (m_pullFiles && m_pullFiles->count() > 0)
            renderPullDiff();
    });
    setOcticon(m_pullUpdateButton, "sync", 16);
    setOcticon(m_pullMergeButton, "check-circle", 16);
    setOcticon(m_pullResolveButton, "git-pull-request", 16);
    setOcticon(m_pullLinkIssueButton, "link", 16);
    m_pullLinkIssueButton->setToolTip("Link an issue to this pull request");
    connect(m_pullLinkIssueButton, &QPushButton::clicked, this,
            &MainWindow::linkIssueToPullFromPullPage);


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


    auto *pullViewWebsiteButton = new QPushButton("View on website");
    pullViewWebsiteButton->setObjectName("ghostButton");
    pullViewWebsiteButton->setProperty("buttonSize", "sm");
    pullViewWebsiteButton->setCursor(Qt::PointingHandCursor);
    setOcticon(pullViewWebsiteButton, "link", 16);
    pullViewWebsiteButton->setToolTip("Open this pull request on the public website");
    connect(pullViewWebsiteButton, &QPushButton::clicked, this, [this] {
        if (m_currentPullNumber <= 0 || m_repoDetailIndex < 0 ||
            m_repoDetailIndex >= m_repositories.size())
            return;
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        QDesktopServices::openUrl(QUrl(repositoryWebUrl(repo) + "/pulls/" +
                                       QString::number(m_currentPullNumber)));
    });
    setOcticon(m_pullCloseButton, "circle-slash", 16);
    setOcticon(m_pullReopenButton, "issue-reopened", 16);
    m_pullReopenButton->setToolTip("Reopen this pull request");
    m_pullReopenButton->hide();
    setOcticon(m_pullSendToSourceButton, "upload", 16);
    m_pullSendToSourceButton->setToolTip(
        "Deliver this pull request to the repository owner's inbox. The relay "
        "holds it, so it reaches the source of truth even while that node is "
        "offline.");
    m_pullSendToSourceButton->hide();
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
    m_pullPreviewButton->hide();
    connect(m_pullPreviewButton, &QPushButton::clicked, this,
            &MainWindow::buildAndPreviewCurrentPull);
    m_pullUpdateButton->setToolTip("Merge the base branch into this pull request branch");
    m_pullResolveButton->setToolTip(
        "Open a merge editor to resolve this pull request's conflicts and commit "
        "the fix to its branch (the PR stays open, ready to merge)");
    m_pullResolveButton->hide();
    connect(m_pullResolveButton, &QPushButton::clicked, this,
            &MainWindow::resolveCurrentPullConflicts);
    // "Fix" (adhoc #7): the provider dropdown is gone — picking Claude API /
    // OpenAI API / CC from a menu meant choosing a resolver before seeing the
    // task. A plain click now writes a ready-made conflict-resolution prompt
    // into the footer prompt box, where it can be edited and sent to whichever
    // agent the prompt bar is pointed at.
    setOcticon(m_pullFixButton, "rocket", 16);
    m_pullFixButton->setToolTip(
        "Fill the prompt box with a task to resolve this pull request's "
        "conflicts on its own branch \xE2\x80\x94 edit it, then send it to an agent");
    m_pullFixButton->hide(); // only shown when the PR has conflicts
    connect(m_pullFixButton, &QPushButton::clicked, this,
            &MainWindow::fillPromptWithPullConflictFix);
    // Continue the agent session that authored this branch, same as the agent
    // detail view's "Fix conflicts with agent" button: it keeps the run's own
    // context/history and full tool access instead of a fresh, conflict-only
    // rewrite. Only shown when such a session is attached (see
    // updatePullActionState / agentSessionForPull).
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






    m_pullReviewAiButton = new QPushButton("Review with AI");
    m_pullFixAllAiButton = new QPushButton("Fix all with AI");
    for (QPushButton *b : {m_pullReviewAiButton, m_pullFixAllAiButton}) {
        b->setObjectName("ghostButton"); // same flat rail style as the rest (adhoc #7)
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
    m_pullFixAllAiButton->hide();
    connect(m_pullFixAllAiButton, &QPushButton::clicked, this,
            &MainWindow::fixCurrentPullFindingsWithAgent);



    auto *pullHeaderRow = new QHBoxLayout;
    pullHeaderRow->setContentsMargins(0, 0, 0, 0);
    pullHeaderRow->addWidget(m_pullSplitButton, 0, Qt::AlignTop);


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
    pullHeaderRow->addWidget(pullViewWebsiteButton, 0, Qt::AlignTop);
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



    connect(m_pullMeta, &QLabel::linkActivated, this, [this](const QString &href) {
        if (href.startsWith(kAgentLinkScheme))
            switchToAgentsTab(href.mid(kAgentLinkScheme.size()).toInt());
        else if (href.startsWith(kBranchLinkScheme))
            switchToBranch(QUrl::fromPercentEncoding(
                href.mid(kBranchLinkScheme.size()).toUtf8()));
    });


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
                    openPullDiffInGitView(m_currentPullNumber);
                    return;
                }
                if (index < 0 || !m_pullSubStack)
                    return;
                if (button)
                    button->setChecked(true);
                m_pullSubStack->setCurrentIndex(index);
            });

    m_pullFiles = new QListWidget;
    m_pullFiles->setObjectName("overviewList");
    enableHoverRowHighlight(m_pullFiles);
    m_pullFiles->setMinimumWidth(180);
    connect(m_pullFiles, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {



                if (item && !m_pullSuppressFileScroll)
                    scrollPullDiffToFile(item->data(Qt::UserRole).toString());
                if (!item) {
                    if (m_pullEditFileButton)
                        m_pullEditFileButton->setEnabled(false);
                    if (m_pullDeleteFileButton)
                        m_pullDeleteFileButton->setEnabled(false);
                }
            });





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

    m_diffFontPt = qBound(8, QSettings().value(kDiffFontPtSetting, 12).toInt(), 28);
    auto *diffZoomOut = new QPushButton(QString::fromUtf8("\xE2\x88\x92"));
    diffZoomOut->setToolTip("Smaller diff text");
    connect(diffZoomOut, &QPushButton::clicked, this, [this] { adjustDiffFont(-1); });
    auto *diffZoomIn = new QPushButton(QStringLiteral("+"));
    diffZoomIn->setToolTip("Larger diff text");
    connect(diffZoomIn, &QPushButton::clicked, this, [this] { adjustDiffFont(1); });




    m_pullAutoViewedButton = new QPushButton;
    m_pullAutoViewedButton->setCheckable(true);
    m_pullAutoViewedButton->setChecked(true);
    m_pullAutoViewedButton->hide();
    setOcticon(m_pullAutoViewedButton, "eye", 14);
    m_pullAutoViewedButton->setToolTip(
        "Automatically mark files as viewed while scrolling");
    connect(m_pullAutoViewedButton, &QPushButton::clicked, this, [this](bool on) {
        setAutoMarkViewedOnScrollPref(on);
        if (on)
            applyAutoMarkViewedOnScroll();
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
    filesHeader->addWidget(m_pullPrevButton);
    filesHeader->addWidget(m_pullNextButton);



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
    m_pullDiff->setOpenLinks(false);
    connect(m_pullDiff, &QTextBrowser::anchorClicked, this,
            &MainWindow::onPullDiffAnchorClicked);
    registerDiffView(m_pullDiff);






    m_pullStickyHeader = new QFrame(m_pullDiff->viewport());
    m_pullStickyHeader->setObjectName("diffStickyHeader");
    {
        const bool dark = qApp->palette().color(QPalette::Base).lightness() < 128;
        m_pullStickyHeader->setStyleSheet(
            QStringLiteral(
                "#diffStickyHeader{background:%1;border-bottom:1px solid %2;}"
                "#diffStickyHeader QLabel{background:transparent;}"
                "#diffStickyHeader QPushButton{background:transparent;border:none;"
                "color:%3;font-size:11px;padding:2px 4px;}"
                "#diffStickyHeader QPushButton:hover{color:#3fb950;}")
                .arg(dark ? "#161b22" : "#f6f8fa", dark ? "#30363d" : "#d0d7de",
                     dark ? "#8b949e" : "#57606a"));
        auto *sl = new QHBoxLayout(m_pullStickyHeader);
        sl->setContentsMargins(10, 4, 8, 4);
        sl->setSpacing(8);
        m_pullStickyPath = new QLabel(m_pullStickyHeader);
        m_pullStickyPath->setTextFormat(Qt::RichText);
        m_pullStickyPath->setTextInteractionFlags(Qt::NoTextInteraction);
        sl->addWidget(m_pullStickyPath, 1);
        m_pullStickyPacman = new PacmanProgress(m_pullStickyHeader);
        m_pullStickyPacman->setToolTip(
            QStringLiteral("How much of this file you've scrolled through"));
        sl->addWidget(m_pullStickyPacman, 0);
        m_pullStickyPercent = new QLabel(QStringLiteral("0% read"),
                                         m_pullStickyHeader);
        m_pullStickyPercent->setObjectName("hintLabel");
        m_pullStickyPercent->setMinimumWidth(52);
        m_pullStickyPercent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sl->addWidget(m_pullStickyPercent, 0);
        m_pullStickyViewed = new QPushButton(m_pullStickyHeader);
        m_pullStickyViewed->setCursor(Qt::PointingHandCursor);
        m_pullStickyViewed->setToolTip(QStringLiteral("Mark this file as viewed"));
        connect(m_pullStickyViewed, &QPushButton::clicked, this, [this] {
            if (m_pullStickyFile.isEmpty() || m_currentPullNumber < 0)
                return;
            const QString context =
                QStringLiteral("pull/") + QString::number(m_currentPullNumber);
            const QSet<QString> cur = loadDiffViewed(context);
            setDiffViewed(context, m_pullStickyFile,
                          !cur.contains(m_pullStickyFile));
            renderPullDiff();
            scrollPullDiffToFile(m_pullStickyFile);
        });
        sl->addWidget(m_pullStickyViewed, 0);
        m_pullStickyHeader->hide();
    }




    m_pullAutoViewedDebounce = new QTimer(this);
    m_pullAutoViewedDebounce->setSingleShot(true);
    m_pullAutoViewedDebounce->setInterval(400);
    connect(m_pullAutoViewedDebounce, &QTimer::timeout, this,
            &MainWindow::applyAutoMarkViewedOnScroll);
    connect(m_pullDiff->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] {


                updatePullDiffScrollState();

                m_pullAutoViewedDebounce->start();
            });

    auto *diffSplit = new QSplitter(Qt::Horizontal);
    diffSplit->setChildrenCollapsible(false);
    diffSplit->addWidget(filesPane);
    diffSplit->addWidget(m_pullDiff);
    diffSplit->setStretchFactor(0, 0);
    diffSplit->setStretchFactor(1, 1);
    diffSplit->setSizes({240, 600});




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
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this,
            [this] { togglePullDiffSearch(true); });
    auto *closeSearchShortcut = new QShortcut(QKeySequence(Qt::Key_Escape),
                                              m_pullDiffSearchInput);
    closeSearchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeSearchShortcut, &QShortcut::activated, this,
            [this] { togglePullDiffSearch(false); });



    m_pullCommitsList = new QListWidget;
    m_pullCommitsList->setObjectName("overviewList");
    enableHoverRowHighlight(m_pullCommitsList);
    connect(m_pullCommitsList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                const QString sha = item ? item->data(kCommitShaRole).toString()
                                         : QString();
                if (sha.isEmpty())
                    return;
                showOverviewCommits();
                showCommit(sha);
            });


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
    installColumnHeaderMenu(m_pullChecksTable);
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

    composerButtons->addWidget(makeVoiceButton(m_pullComposer), 0, Qt::AlignLeft);
    composerButtons->addStretch();
    composerButtons->addWidget(m_pullRequestChangesButton);
    composerButtons->addWidget(m_pullApproveButton);
    composerButtons->addWidget(m_pullCommentButton);
    auto *composerBlock = new QWidget;
    auto *composerBlockLayout = new QVBoxLayout(composerBlock);
    composerBlockLayout->setContentsMargins(0, 0, 0, 0);
    composerBlockLayout->setSpacing(6);
    composerBlockLayout->addWidget(makeComposerIdentity(nullptr, QStringLiteral("Reviewing")));
    composerBlockLayout->addWidget(m_pullComposer);
    composerBlockLayout->addLayout(composerButtons);





    m_pullConflictDetails = new QLabel;
    m_pullConflictDetails->setObjectName("issueTimelineCard");
    m_pullConflictDetails->setTextFormat(Qt::RichText);
    m_pullConflictDetails->setWordWrap(true);
    m_pullConflictDetails->setContentsMargins(16, 12, 16, 12);
    m_pullConflictDetails->setOpenExternalLinks(false);
    m_pullConflictDetails->hide();
    connect(m_pullConflictDetails, &QLabel::linkActivated, this,
            [this](const QString &) {
                openPullDiffInGitView(m_currentPullNumber);
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
    m_pullAgentRevisionRow->hide();
    conversationInnerLayout->addWidget(m_pullAgentRevisionRow);

    conversationInnerLayout->addStretch();

    m_pullThreadScroll = new QScrollArea;
    m_pullThreadScroll->setWidgetResizable(true);
    m_pullThreadScroll->setWidget(conversationInner);
    m_pullThreadScroll->setObjectName("issuePageScroll");
    m_pullThreadScroll->setFrameShape(QFrame::NoFrame);




    m_pullBadgeWidget = new PullBadgeWidget;
    auto *badgeScroll = new QScrollArea;
    badgeScroll->setWidgetResizable(true);
    badgeScroll->setWidget(m_pullBadgeWidget);
    badgeScroll->setObjectName("issuePageScroll");
    badgeScroll->setFrameShape(QFrame::NoFrame);



    m_pullSubStack = new QStackedWidget;
    m_pullSubStack->addWidget(m_pullThreadScroll);
    m_pullSubStack->addWidget(m_pullCommitsList);
    m_pullSubStack->addWidget(checksPage);
    m_pullSubStack->addWidget(filesPage);
    m_pullSubStack->addWidget(badgeScroll);

    m_pullSubTabs = new QButtonGroup(this);
    m_pullSubTabs->setExclusive(true);
    auto *subTabRow = new QHBoxLayout;
    subTabRow->setContentsMargins(0, 0, 0, 0);
    subTabRow->setSpacing(2);
    const QList<QPair<QString, const char *>> subTabs = {
        {QStringLiteral("Conversation"), "comment"},
        {QStringLiteral("Commits"), "git-branch"},
        {QStringLiteral("Checks"), "workflow"},
        {QStringLiteral("Changes in Git"), "file-diff"},
        {QStringLiteral("Badge"), "graph"}};
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
    m_pullTabBadge = qobject_cast<QPushButton *>(m_pullSubTabs->button(4));
    connect(m_pullSubTabs, &QButtonGroup::idClicked, this, [this](int id) {
        if (id == 3) {
            // The PR page owns conversation/checks/approval state; repository
            // changes have one visual home. Route its Files entry into the Git
            // range pane, which carries the same line threads, Viewed state,
            // find/navigation tools, and PR actions.
            openPullDiffInGitView(m_currentPullNumber);
            return;
        }
        m_pullSubStack->setCurrentIndex(id);
        if (id == 2) {



            PullRequest current;
            for (const PullRequest &pr : std::as_const(m_currentPulls))
                if (pr.number == m_currentPullNumber)
                    current = pr;
            if (current.number > 0)
                renderPullChecks(current);
        }
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
    connect(m_pullDeleteAllMergedButton, &QPushButton::clicked, this,
            &MainWindow::deleteAllMergedPullsAndBranches);
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
    if (m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    ++m_pullLoadGen;
    const PullStore store = pullStoreForCurrentRepo();
    applyLoadedPulls(store, store.loadAll(), store.baseTip());
}

void MainWindow::reloadPullsInBackground()
{
    if (m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const quint64 gen = ++m_pullLoadGen;
    if (m_pullBackgroundLoadInFlight) {
        m_pullBackgroundReloadQueued = true;
        return;
    }
    m_pullBackgroundLoadInFlight = true;
    const PullStore store = pullStoreForCurrentRepo();
    struct LoadedPulls {
        PullStore store;
        QList<PullRequest> pulls;
        QString baseTip;
    };
    runOffThread<LoadedPulls>(
        [store] {
            const forkmesh::BackgroundScope activity(
                QStringLiteral("pulls"), QStringLiteral("load pull metadata"),
                forkmesh::ActionTelemetry::Execution::Worker);
            return LoadedPulls{store, store.loadAll(), store.baseTip()};
        },
        [this, gen](LoadedPulls loaded) {
            m_pullBackgroundLoadInFlight = false;
            if (gen == m_pullLoadGen)
                applyLoadedPulls(loaded.store, std::move(loaded.pulls),
                                 loaded.baseTip);
            if (m_pullBackgroundReloadQueued) {
                m_pullBackgroundReloadQueued = false;
                reloadPullsInBackground();
            }
        });
}

void MainWindow::applyLoadedPulls(const PullStore &store,
                                  QList<PullRequest> pulls,
                                  const QString &baseTip)
{
    m_currentPulls = std::move(pulls);







    const quint64 gen = ++m_pullConflictGen;
    m_pendingPullConflictChecks.clear();
    m_pullConflictByNumber.clear();
    if (store.canWrite()) {






        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        const QString cacheKey = repo.owner + QLatin1Char('/') + repo.name +
                                 QLatin1Char('@') + baseTip;
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

                if (cached->conflict)
                    m_pullConflictByNumber.insert(pr.number, true);
            } else {


                m_pendingPullConflictChecks.append(qMakePair(pr.number, fingerprint));
            }
        }


        for (auto it = m_pullConflictCache.begin();
             it != m_pullConflictCache.end();) {
            if (openNumbers.contains(it.key()))
                ++it;
            else
                it = m_pullConflictCache.erase(it);
        }
    }
    updateRepoPullCount();
    if (m_pullTable) {
        refreshPullList();
        updatePullActionState();
    }


    if (m_repoDetailStack && m_repoDetailStack->currentIndex() == 3)
        refreshAgentTable();



    if (!m_pendingPullConflictChecks.isEmpty())
        QTimer::singleShot(0, this, [this, gen] { processPendingPullConflicts(gen); });
}

void MainWindow::processPendingPullConflicts(quint64 gen)
{

    if (gen != m_pullConflictGen || m_pendingPullConflictChecks.isEmpty())
        return;




    if (m_pullConflictCheckInFlight)
        return;
    const QPair<int, QString> item = m_pendingPullConflictChecks.takeFirst();
    const int number = item.first;
    const QString fingerprint = item.second;
    const PullStore store = pullStoreForCurrentRepo();
    if (!store.canWrite()) {

        if (gen == m_pullConflictGen && !m_pendingPullConflictChecks.isEmpty())
            QTimer::singleShot(0, this, [this, gen] { processPendingPullConflicts(gen); });
        return;
    }







    m_pullConflictCheckInFlight = true;
    auto clean = std::make_shared<bool>(false);
    auto mergeable = std::make_shared<bool>(false);
    auto conflictFiles = std::make_shared<QStringList>();
    QThread *worker = QThread::create([store, number, clean, mergeable,
                                       conflictFiles]() mutable {
        *mergeable = store.checkMergeable(number, clean.get(), conflictFiles.get(),
                                          nullptr,  false);
    });
    connect(worker, &QThread::finished, this,
            [this, worker, gen, number, fingerprint, clean, mergeable,
             conflictFiles]() {
                m_pullConflictCheckInFlight = false;
                worker->deleteLater();
                const bool conflict = *mergeable && !*clean;


                if (gen == m_pullConflictGen) {
                    m_pullConflictCache.insert(
                        number, {fingerprint, conflict, *conflictFiles});
                    if (conflict)
                        m_pullConflictByNumber.insert(number, true);
                    else
                        m_pullConflictByNumber.remove(number);
                    setPullConflictBadge(number, conflict);


                    if (number == m_currentPullNumber)
                        updatePullActionState();
                }




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


    for (const QPair<int, QString> &p : std::as_const(m_pendingPullConflictChecks))
        if (p.first == number)
            return;



    const bool wasIdle = m_pendingPullConflictChecks.isEmpty();
    m_pendingPullConflictChecks.append(qMakePair(number, fingerprint));
    if (wasIdle) {
        const quint64 gen = m_pullConflictGen;
        QTimer::singleShot(0, this, [this, gen] { processPendingPullConflicts(gen); });
    }
}

QString MainWindow::pullConflictBadgeTooltip(int number) const
{
    const QString base = QStringLiteral("This pull request has merge conflicts");
    const auto cached = m_pullConflictCache.constFind(number);
    if (cached == m_pullConflictCache.constEnd() ||
        cached->conflictFiles.isEmpty())
        return base;



    const QStringList &files = cached->conflictFiles;
    constexpr int kMaxListed = 10;
    QStringList lines;
    for (int i = 0; i < files.size() && i < kMaxListed; ++i)
        lines << QStringLiteral("\xE2\x80\xA2 ") + files.at(i);
    if (files.size() > kMaxListed)
        lines << QStringLiteral("\xE2\x80\xA6 and %1 more")
                     .arg(files.size() - kMaxListed);
    return QStringLiteral("%1 in %2 file%3:\n%4")
        .arg(base)
        .arg(files.size())
        .arg(files.size() == 1 ? QString() : QStringLiteral("s"),
             lines.join(QLatin1Char('\n')));
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
            st->setToolTip(pullConflictBadgeTooltip(number));
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
    QList<const PullRequest *> visiblePulls;
    visiblePulls.reserve(m_currentPulls.size());
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (!search.isEmpty()) {
            const QString hay = QStringLiteral("#%1 %2 %3 %4 %5")
                                    .arg(pr.number)
                                    .arg(pr.title, pr.base, pr.head, pr.authorName);
            if (!hay.contains(search, Qt::CaseInsensitive))
                continue;
        }
        visiblePulls.append(&pr);
    }

    m_pullTable->setRowCount(visiblePulls.size());
    for (int row = 0; row < visiblePulls.size(); ++row) {
        const PullRequest &pr = *visiblePulls.at(row);
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


        if (m_pullConflictByNumber.value(pr.number, false)) {
            st->setIcon(themedOcticon("alert", QColor("#f85149"), 13));
            st->setToolTip(pullConflictBadgeTooltip(pr.number));
        }
        m_pullTable->setItem(row, 3, st);
        auto *files = new QTableWidgetItem;
        files->setData(Qt::DisplayRole, pr.filesChanged);
        m_pullTable->setItem(row, 4, files);
        m_pullTable->setItem(row, 5,
                             new QTableWidgetItem(QStringLiteral("+%1 -%2")
                                                      .arg(formatCount(pr.additions))
                                                      .arg(formatCount(pr.deletions))));

        const QString author =
            pr.authorName.trimmed().isEmpty()
                ? (pr.author.isEmpty() ? QString::fromUtf8("\xE2\x80\x94")
                                       : pr.author.left(8))
                : pr.authorName.trimmed();
        auto *authorItem = new QTableWidgetItem(author);
        authorItem->setToolTip(pr.author);
        m_pullTable->setItem(row, 6, authorItem);



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




        auto *created = new QTableWidgetItem(
            pr.ts > 0
                ? QDateTime::fromMSecsSinceEpoch(pr.ts).toString("yyyy-MM-dd HH:mm")
                : QString());
        created->setToolTip(formatIssueRelativeTime(pr.ts));
        m_pullTable->setItem(row, 8, created);



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

void MainWindow::renderPullReviewSummary(PullRequest pr)
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
                 run->status == ActionStatus::Cancelled ||
                 run->status == ActionStatus::Skipped)
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






    PullRequest foundPull;
    bool havePull = false;
    for (const PullRequest &pr : m_currentPulls)
        if (pr.number == number) {
            foundPull = pr;
            havePull = true;
        }
    const PullRequest *found = havePull ? &foundPull : nullptr;
    m_currentPullNumber = found ? number : -1;
    m_pullFiles->clear();
    m_pullFileDiffs.clear();
    togglePullDiffSearch(false);
    m_pullFileAuthorship.clear();
    if (m_pullFileAuthorFilter)
        m_pullFileAuthorFilter->hide();

    if (!found) {
        m_pullTitle->setText("Select a pull request");
        m_pullMeta->clear();
        m_pullDiff->clear();
        m_pullDiffRenderKey.clear();
        if (m_pullCommitsList)
            m_pullCommitsList->clear();
        renderPullThread(PullRequest());
        renderPullChecks(PullRequest());
        renderPullChecksSummary(PullRequest());
        renderPullReviewSummary(PullRequest());
        updatePullSubTabCounts(PullRequest());
        if (m_pullBadgeWidget)
            m_pullBadgeWidget->clearPull();
        if (m_pullComposer)
            m_pullComposer->setEnabled(false);
        for (QPushButton *b : {m_pullCommentButton, m_pullApproveButton,
                               m_pullRequestChangesButton})
            if (b)
                b->setEnabled(false);
        updatePullActionState();
        return;
    }



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
                 found->status, formatCount(found->filesChanged),
                 formatCount(found->additions), formatCount(found->deletions),
                 (found->authorName.isEmpty() ? found->author.left(10)
                                              : found->authorName)
                     .toHtmlEscaped()));



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

            currentFile = line.section(" b/", 1);
        }
        if (!currentFile.isEmpty())
            currentLines << line;
    }
    flush();



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



    if (m_pullBadgeWidget) {
        QList<PullBadgeWidget::FileEntry> badgeFiles;
        badgeFiles.reserve(m_pullFileDiffs.size());
        for (auto it = m_pullFileDiffs.constBegin();
             it != m_pullFileDiffs.constEnd(); ++it) {
            PullBadgeWidget::FileEntry entry;
            entry.path = it.key();
            for (const QString &line : it.value().split('\n')) {
                if (line.startsWith(QLatin1String("+++ ")) ||
                    line.startsWith(QLatin1String("--- ")))
                    continue;
                if (line.startsWith(QLatin1Char('+')))
                    ++entry.adds;
                else if (line.startsWith(QLatin1Char('-')))
                    ++entry.dels;
            }
            entry.icon = iconForFile(it.key().section('/', -1));
            badgeFiles << entry;
        }
        m_pullBadgeWidget->setPull(
            found->title, found->number,
            found->authorName.isEmpty() ? found->author.left(10)
                                        : found->authorName,
            found->additions, found->deletions, badgeFiles);
    }


    if (m_pullFileAuthorFilter) {
        bool anyAgent = false, anyHuman = false;
        for (auto it = m_pullFileAuthorship.constBegin();
             it != m_pullFileAuthorship.constEnd(); ++it)
            (it.value() ? anyAgent : anyHuman) = true;
        const QSignalBlocker block(m_pullFileAuthorFilter);
        m_pullFileAuthorFilter->setCurrentIndex(0);
        m_pullFileAuthorFilter->setVisible(anyAgent && anyHuman);
    }
    fitFileListToWidestEntry(m_pullFiles);
    if (m_pullFiles->count() > 0) {


        renderPullDiff();
        m_pullSuppressFileScroll = true;
        m_pullFiles->setCurrentRow(0);
        m_pullSuppressFileScroll = false;
    } else {
        m_pullDiff->setPlainText("(no changes)");
        m_pullDiff->setProperty("fm_diffSource", QString());
        m_pullDiffRenderKey.clear();
        m_pullFileAnchors.clear();
        m_pullFileOrder.clear();
        m_pullStickyLabelHtml.clear();
        m_pullFileTops.clear();
        m_pullStickyFile.clear();
        if (m_pullStickyHeader)
            m_pullStickyHeader->hide();
    }
    renderPullCommits(*found);
    renderPullThread(*found);
    renderPullChecks(*found);
    renderPullChecksSummary(*found);
    renderPullReviewSummary(*found);
    updatePullSubTabCounts(*found);
    updatePullActionState();
}



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

// Open a pull request's diff in the Git view's range pane (adhoc #107): the PR
// counterpart of a branch comparison. The pane renders the PR's review threads
// and comment gutters (see renderBranchDiffPatch) and its "PR #N" button jumps
// on to the full pull request page. A PR whose head branch still exists locally
// diffs against the live refs and browses that branch in the graph, reading as
// the same "<head> -> <base>" comparison a branch does (adhoc #16); one whose
// branch is gone (merged & pruned, or cross-node) shows its stored patch with
// the graph left on the default branch. Either way only the right pane changes:
// the universal source-control composer, working changes, and graph stay put.
void MainWindow::openPullDiffInGitView(int pullNumber)
{



    ensureRepoDetailTabBuilt(4);
    m_currentPullNumber = pullNumber;
    reloadPulls();
    bool rowSelected = false;
    if (m_pullTable) {
        for (int row = 0; row < m_pullTable->rowCount(); ++row) {
            QTableWidgetItem *number = m_pullTable->item(row, 0);
            if (number && number->data(Qt::UserRole).toInt() == pullNumber) {
                m_pullTable->selectRow(row);
                rowSelected = true;
                break;
            }
        }
    }
    if (!rowSelected)
        showPull(pullNumber);
    // The Files button is a route, not a second PR sub-page. Leave the PR detail
    // parked on Conversation so its button state and stack are coherent when
    // the reviewer returns from the Git range pane.
    if (m_pullTabConversation)
        m_pullTabConversation->setChecked(true);
    if (m_pullSubStack)
        m_pullSubStack->setCurrentIndex(0);




    PullRequest pr;
    for (const PullRequest &p : std::as_const(m_currentPulls))
        if (p.number == pullNumber)
            pr = p;
    if (pr.number != pullNumber) {


        switchToPullTab(pullNumber);
        return;
    }

    m_branchDiffPullNumber = pullNumber;
    showOverviewCommits();
    const QString dir = repoGitDir();
    const bool liveBranch =
        !pr.head.isEmpty() && !dir.isEmpty() && localBranchExists(dir, pr.head);
    // The graph browses the PR's head branch when it still exists, so the view
    // reads as one "<head> -> <base>" comparison (adhoc #16). With the branch
    // gone there is nothing to browse, so the graph stays on the default branch.
    const QString graphRef = liveBranch ? pr.head : repoDefaultBranchFast();
    if (!graphRef.isEmpty() && m_repoBranch != graphRef)
        setRepoBranch(graphRef);
    setCommitWorkspacePage(kCommitWorkspaceRangePage);
    if (liveBranch) {
        // Live branch: diff the PR's whole range against the repo's refs.
        showBranchDiff(pr.head);
    } else {


        m_branchDiffBranch = pr.head;
        m_branchDiffWorkDir.clear();
        updateCommitsCompareIndicator(); // "<head> -> <base>" on the branch row
        updateBranchDetailActions(pr.head);
        m_branchDiffLastPatch = pr.patch.toUtf8();
        m_branchDiffLastEmpty = QStringLiteral("This pull request has no changes.");
        m_branchDiffLastValid = true;
        ++m_branchScopeDiffGen;
        renderBranchDiffPatch(pr.patch, m_branchDiffLastEmpty,
                              QStringLiteral("pull/") + QString::number(pullNumber));
    }
    if (m_branchDiffView)
        m_branchDiffView->setFocus();

    QTimer::singleShot(0, this, [this] {
        if (commitsListIsCurrent())
            refreshSourceControl();
        else
            loadCommits();
    });
}



static const char *kDiffSourceProp = "fm_diffSource";



void MainWindow::registerDiffView(QTextEdit *view)
{
    if (!view || m_diffViews.contains(view))
        return;
    m_diffViews.append(view);
    if (view->toolTip().isEmpty())
        view->setToolTip(QStringLiteral("Ctrl+scroll to change the text size"));




    view->setLineWrapMode(QTextEdit::WidgetWidth);
    view->viewport()->installEventFilter(this);
    if (auto *browser = qobject_cast<QTextBrowser *>(view))
        browser->setOpenLinks(false);



    addDiffStreamFinishedHook(view, [this, view] { onDiffStreamFinished(view); });
    connect(view, &QObject::destroyed, this, [this](QObject *o) {
        auto *dead = static_cast<QTextEdit *>(o);
        m_diffViews.removeAll(dead);
        m_diffRestoreScroll.remove(dead);
    });
}






void MainWindow::setDiffHtml(QTextEdit *view, const QString &html)
{
    if (!view)
        return;
    view->setProperty(kDiffSourceProp, html);
    renderDiffStreamed(view, html, diffStyleSheet(m_diffFontPt));
}



void MainWindow::onDiffStreamFinished(QTextEdit *view)
{
    if (!view)
        return;

    const auto scroll = m_diffRestoreScroll.find(view);
    if (scroll != m_diffRestoreScroll.end()) {
        if (QScrollBar *vbar = view->verticalScrollBar())
            vbar->setValue(qMin(*scroll, vbar->maximum()));
        m_diffRestoreScroll.erase(scroll);
    }
    if (view == m_pullDiff) {
        m_pullFileTops.clear();
        m_pullStickyFile.clear();
        if (m_pullDiffSearchBar && m_pullDiffSearchBar->isVisible())
            pullDiffSearchRecompute();
        updatePullDiffScrollState();
    } else if (view == m_branchDiffView) {
        m_branchFileTops.clear();
        m_branchStickyFile.clear();
        rebuildBranchDiffSpans();
        if (m_branchDiffSearchBar && m_branchDiffSearchBar->isVisible())
            branchDiffSearchRecompute();
    }
}



void MainWindow::adjustDiffFont(int delta)
{
    const int next = qBound(8, m_diffFontPt + delta, 28);
    if (next == m_diffFontPt)
        return;
    m_diffFontPt = next;
    QSettings().setValue(kDiffFontPtSetting, m_diffFontPt);
    for (QTextEdit *view : m_diffViews) {
        if (!view || view->document()->isEmpty())
            continue;
        const QString src = view->property(kDiffSourceProp).toString();
        if (src.isEmpty())
            continue;
        QScrollBar *vbar = view->verticalScrollBar();
        const int scroll = vbar ? vbar->value() : 0;



        if (scroll > 0)
            m_diffRestoreScroll.insert(view, scroll);
        setDiffHtml(view, src);
        if (vbar)
            vbar->setValue(qMin(scroll, vbar->maximum()));
    }
    m_pullDiffRenderKey.clear();
    m_scmDiffRenderKey.clear();


    if (m_pullDiffSearchBar && m_pullDiffSearchBar->isVisible())
        pullDiffSearchRecompute();
    if (m_branchDiffSearchBar && m_branchDiffSearchBar->isVisible())
        branchDiffSearchRecompute();
}





QHash<QString, QString> MainWindow::buildPullLineNotes(const PullRequest &pr)
{
    QHash<QString, QString> notes;
    {
        const auto htmlBody = [](QString text) {
            text = text.toHtmlEscaped();
            text.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
            return text;
        };



        const bool canApplyFixes = pr.status == QLatin1String("open") &&
                                   pullStoreForCurrentRepo().canWrite();
        const PullReviewSnapshot snapshot = buildPullReviewSnapshot(pr);
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
    return notes;
}

void MainWindow::renderPullDiff()
{
    if (!m_pullDiff)
        return;

    QHash<QString, QString> notes;
    const PullRequest *pr = nullptr;
    for (const PullRequest &p : m_currentPulls)
        if (p.number == m_currentPullNumber)
            pr = &p;
    if (pr)
        notes = buildPullLineNotes(*pr);





    QList<DiffFileEntry> files;
    const QSet<QString> viewed =
        loadDiffViewed(QStringLiteral("pull/") + QString::number(m_currentPullNumber));
    const QString fullPatch = pr ? pr->patch : QString();
    const QString html = renderDiffHtml(fullPatch, files, QString(), QString(),
                                        QString(), QStringLiteral("*"), notes, viewed);





    m_pullFileAnchors.clear();
    m_pullFileOrder.clear();
    m_pullStickyLabelHtml.clear();
    m_pullFileTops.clear();
    for (const DiffFileEntry &f : files) {
        m_pullFileAnchors.insert(f.path, f.anchor);
        m_pullFileOrder.append(f.path);
        m_pullStickyLabelHtml.insert(f.path, diffStickyLabelHtml(f));
    }

    const QString styleSheet = diffStyleSheet(m_diffFontPt);
    const QString body =
        html.isEmpty() ? QStringLiteral("<p style='color:#8b949e'>(no changes)</p>") : html;







    const QString key = QString::number(m_currentPullNumber) +
                        QLatin1Char('\x1f') + styleSheet + QLatin1Char('\x1f') +
                        body;
    if (key == m_pullDiffRenderKey)
        return;
    m_pullDiffRenderKey = key;

    setDiffHtml(m_pullDiff, body);


    if (m_pullDiffSearchBar && m_pullDiffSearchBar->isVisible())
        pullDiffSearchRecompute();



    m_pullStickyFile.clear();
    QTimer::singleShot(0, this, &MainWindow::updatePullDiffScrollState);
}


void MainWindow::scrollPullDiffToFile(const QString &filePath)
{
    if (!m_pullDiff)
        return;
    const QString anchor = m_pullFileAnchors.value(filePath);
    if (anchor.isEmpty())
        return;


    flushDiffStream(m_pullDiff);
    m_pullDiff->scrollToAnchor(anchor);
}




void MainWindow::selectPullFileInList(const QString &filePath)
{
    if (!m_pullFiles)
        return;
    for (int row = 0; row < m_pullFiles->count(); ++row) {
        QListWidgetItem *item = m_pullFiles->item(row);
        if (item && item->data(Qt::UserRole).toString() == filePath) {
            if (m_pullFiles->currentItem() == item)
                return;
            m_pullSuppressFileScroll = true;
            m_pullFiles->setCurrentItem(item);
            m_pullFiles->scrollToItem(item);
            m_pullSuppressFileScroll = false;
            return;
        }
    }
}







void MainWindow::computePullFileTops()
{
    m_pullFileTops.assign(m_pullFileOrder.size(), -1);
    if (!m_pullDiff || m_pullFileOrder.isEmpty())
        return;
    QScrollBar *vbar = m_pullDiff->verticalScrollBar();
    const int viewTop = vbar ? vbar->value() : 0;
    QHash<QString, int> anchorIndex;
    for (int i = 0; i < m_pullFileOrder.size(); ++i) {
        const QString a = m_pullFileAnchors.value(m_pullFileOrder.at(i));
        if (!a.isEmpty())
            anchorIndex.insert(a, i);
    }
    QTextDocument *doc = m_pullDiff->document();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid() || !frag.charFormat().isAnchor())
                continue;
            for (const QString &name : frag.charFormat().anchorNames()) {
                const auto ai = anchorIndex.constFind(name);
                if (ai == anchorIndex.constEnd())
                    continue;
                QTextCursor cur(doc);
                cur.setPosition(frag.position());
                m_pullFileTops[ai.value()] =
                    m_pullDiff->cursorRect(cur).top() + viewTop;
            }
        }
    }
}



void MainWindow::layoutPullStickyHeader()
{
    if (!m_pullStickyHeader || !m_pullDiff)
        return;
    QWidget *vp = m_pullDiff->viewport();
    m_pullStickyHeader->setGeometry(0, 0, vp->width(),
                                    m_pullStickyHeader->sizeHint().height());
}







void MainWindow::updatePullDiffScrollState()
{
    if (!m_pullDiff || !m_pullStickyHeader)
        return;
    if (m_currentPullNumber < 0 || m_pullFileOrder.isEmpty()) {
        m_pullStickyHeader->hide();
        return;
    }
    QScrollBar *vbar = m_pullDiff->verticalScrollBar();
    if (!vbar)
        return;
    const int viewTop = vbar->value();
    const int viewBottom = viewTop + m_pullDiff->viewport()->height();
    QTextDocument *doc = m_pullDiff->document();
    const int docHeight = doc->documentLayout()->documentSize().height();




    if (m_pullFileTops.size() != m_pullFileOrder.size())
        computePullFileTops();
    const QList<int> &tops = m_pullFileTops;



    int idx = -1, fileTop = 0, fileBottom = 0;
    for (int i = 0; i < m_pullFileOrder.size(); ++i) {
        if (tops[i] < 0)
            continue;
        const int bottom =
            (i + 1 < tops.size() && tops[i + 1] >= 0) ? tops[i + 1] : docHeight;
        if (bottom > viewTop) {
            idx = i;
            fileTop = tops[i];
            fileBottom = bottom;
            break;
        }
    }
    if (idx < 0) {
        m_pullStickyHeader->hide();
        return;
    }
    const QString path = m_pullFileOrder.at(idx);




    double progress = 1.0;
    if (fileBottom > fileTop)
        progress = double(viewBottom - fileTop) / double(fileBottom - fileTop);
    progress = qBound(0.0, progress, 1.0);

    const QSet<QString> viewed =
        loadDiffViewed(QStringLiteral("pull/") + QString::number(m_currentPullNumber));
    const bool isViewed = viewed.contains(path);
    if (path != m_pullStickyFile) {
        m_pullStickyFile = path;
        m_pullStickyPath->setText(m_pullStickyLabelHtml.value(path));

        selectPullFileInList(path);
    }
    m_pullStickyViewed->setText(isViewed
                                    ? QString::fromUtf8("\xE2\x98\x91 Viewed")
                                    : QString::fromUtf8("\xE2\x98\x90 Viewed"));


    m_pullStickyPacman->setColor(progress >= 0.999 || isViewed
                                     ? QColor(0x3f, 0xb9, 0x50)
                                     : QColor(0x58, 0xa6, 0xff));
    m_pullStickyPacman->setProgress(isViewed ? 1.0 : progress);
    if (m_pullStickyPercent) {
        const int percent =
            isViewed ? 100 : qBound(0, qRound(progress * 100.0), 100);
        m_pullStickyPercent->setText(
            QStringLiteral("%1% read").arg(percent));
    }

    layoutPullStickyHeader();




    if (viewTop <= fileTop + m_pullStickyHeader->sizeHint().height()) {
        m_pullStickyHeader->hide();
        return;
    }
    m_pullStickyHeader->show();
    m_pullStickyHeader->raise();
}









void MainWindow::applyAutoMarkViewedOnScroll()
{
    if (!m_pullDiff || m_currentPullNumber < 0 || m_pullFileOrder.isEmpty())
        return;
    QScrollBar *vbar = m_pullDiff->verticalScrollBar();
    if (!vbar)
        return;

    const int viewTop = vbar->value();
    const int viewBottom = viewTop + m_pullDiff->viewport()->height();
    QTextDocument *doc = m_pullDiff->document();
    const int docHeight = doc->documentLayout()->documentSize().height();




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
    QString currentFile;
    QStringList newlyViewed;
    for (int i = 0; i < m_pullFileOrder.size(); ++i) {
        if (tops[i] < 0)
            continue;


        const int bottom = (i + 1 < tops.size() && tops[i + 1] >= 0) ? tops[i + 1]
                                                                     : docHeight;

        if (bottom <= viewBottom) {
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



    pullScrollToAdjacentHunk(delta);
}

bool MainWindow::pullScrollToAdjacentHunk(int delta)
{
    if (!m_pullDiff)
        return false;
    QScrollBar *vbar = m_pullDiff->verticalScrollBar();
    if (!vbar)
        return false;

    flushDiffStream(m_pullDiff);





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
        return false;
    vbar->setValue(std::clamp(target - 4, vbar->minimum(), vbar->maximum()));
    return true;
}



void MainWindow::togglePullDiffSearch(bool show)
{
    if (!m_pullDiffSearchBar || !m_pullDiffSearchInput)
        return;
    m_pullDiffSearchBar->setVisible(show);
    if (show) {
        m_pullDiffSearchInput->setFocus();
        m_pullDiffSearchInput->selectAll();
    } else {
        m_pullDiffSearchInput->clear();
        if (m_pullDiff)
            m_pullDiff->setFocus();
    }
}




void MainWindow::pullDiffSearchRecompute()
{
    if (!m_pullDiff)
        return;
    m_pullDiffSearchMatches.clear();
    m_pullDiffSearchIndex = -1;

    const QString term =
        m_pullDiffSearchInput ? m_pullDiffSearchInput->text() : QString();
    if (!term.isEmpty()) {

        flushDiffStream(m_pullDiff);
        QTextCursor cur = m_pullDiff->document()->find(term);
        while (!cur.isNull()) {
            m_pullDiffSearchMatches.append(cur);
            if (m_pullDiffSearchMatches.size() >= 5000)
                break;
            cur = m_pullDiff->document()->find(term, cur);
        }
        if (!m_pullDiffSearchMatches.isEmpty())
            m_pullDiffSearchIndex = 0;
    }

    applyDiffSearchHighlights(m_pullDiff, m_pullDiffSearchMatches,
                                  m_pullDiffSearchIndex, m_pullDiffSearchCount,
                                  term.isEmpty());
    if (m_pullDiffSearchIndex >= 0)
        pullDiffSearchGoTo(0);
}




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
    applyDiffSearchHighlights(m_pullDiff, m_pullDiffSearchMatches,
                                  m_pullDiffSearchIndex, m_pullDiffSearchCount,
                                  false);




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
                                     const QString &authorId,
                                     const std::function<void()> &onDelete)
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








    QPixmap authorAvatar;
    if (!authorId.isEmpty()) {
        const QPixmap cached = m_avatars.value(authorId);
        if (!cached.isNull())
            authorAvatar = roundedRectPixmap(cached, 36, 36 * 0.28);
        else if (authorId == m_profileIdentity.publicKey())


            authorAvatar = roundedAvatar(effectiveUserAvatar(), 36);
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
    if (!copyLink.isEmpty() || !body.trimmed().isEmpty() || bool(onDelete)) {
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
        if (onDelete) {
            menu->addSeparator();
            QAction *deleteAction = menu->addAction("Delete comment");
            connect(deleteAction, &QAction::triggered, this,
                    [onDelete]() { onDelete(); });
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


        bodyLabel->setOpenExternalLinks(false);
        connect(bodyLabel, &QLabel::linkActivated, this,
                [this](const QString &href) {


                    if (href.startsWith(QLatin1String("applyfix:")))
                        applyPullSuggestionFix(href.mid(9));
                    else
                        openBodyReference(href);
                });
        bodyLabel->setContentsMargins(16, 12, 16, 14);
        cardLayout->addWidget(bodyLabel);
    }
    rowLayout->addWidget(card, 1);

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


    m_pullThreadLayout->addStretch();
    if (pr.number == 0) {
        if (m_pullLinksValue)
            m_pullLinksValue->hide();
        return;
    }


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


        addConversationCard(
            m_pullThreadLayout, who,
            QStringLiteral("<b>%1</b> %2 <span style='color:#8b949e'>%3</span>")
                .arg(who.toHtmlEscaped(), verb, when),
            body, accent,
            pullLink + QStringLiteral("#%1")
                           .arg(ev.id.isEmpty() ? QString::number(ev.ts) : ev.id),
            ev.author);
    }
    if (m_repoDetailIndex >= 0 &&
        m_repoDetailIndex < m_repositories.size() && m_networkAccess) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        auto *remoteThread =
            new FederatedThreadView(m_networkAccess, m_pullThreadContainer);
        const QUrl server(canonicalServerUrl(
            m_activeServer >= 0 && m_activeServer < m_servers.size()
                ? m_servers.at(m_activeServer).url
                : QString()));
        remoteThread->load(server, repo.owner, repo.name,
                           QStringLiteral("pull"), pr.number);
        m_pullThreadLayout->insertWidget(
            qMax(0, m_pullThreadLayout->count() - 1), remoteThread);
    }
}

void MainWindow::renderPullCommits(PullRequest pr)
{
    if (!m_pullCommitsList)
        return;






    m_pullCommitsList->clear();
    const QString dir = repoGitDir();


    bool listed = false;


    QString repoOwner, repoName;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        repoOwner = m_repositories.at(m_repoDetailIndex).owner;
        repoName = m_repositories.at(m_repoDetailIndex).name;
    }



    const auto checkStatusFor = [&](const QString &sha) -> QString {
        if (sha.isEmpty() || repoOwner.isEmpty())
            return QString();
        const auto rank = [](const QString &s) {
            if (s == ActionStatus::Failed || s == ActionStatus::Rejected ||
                s == ActionStatus::Cancelled || s == ActionStatus::Skipped)
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

    const auto checkGlyph = [](const QString &status) -> QString {
        if (status == ActionStatus::Success)
            return QString::fromUtf8("\xE2\x9C\x93 ");
        if (status == ActionStatus::Failed || status == ActionStatus::Rejected ||
            status == ActionStatus::Cancelled || status == ActionStatus::Skipped)
            return QString::fromUtf8("\xE2\x9C\x97 ");
        if (status == ActionStatus::Running)
            return QString::fromUtf8("\xE2\x97\x8F ");
        if (status == ActionStatus::Queued || status == ActionStatus::AwaitingApproval)
            return QString::fromUtf8("\xE2\x97\x8B ");
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

                QString tip = QString::fromUtf8("%1\n%2 committed %3")
                                  .arg(sha, f.at(3), f.at(4));
                if (!rel.isEmpty())
                    tip += QString::fromUtf8(" (%1 ago)").arg(rel);
                if (!status.isEmpty())
                    tip += QString::fromUtf8("\nChecks: %1").arg(actionStatusText(status));

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

                if (line.startsWith(QLatin1String("ForkMesh-Agent:")))
                    agentTrailer = line.mid(15).trimmed();
                continue;
            }
            if (line.isEmpty()) {
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




QList<int> MainWindow::runIdsForPull(PullRequest pr) const
{
    QList<int> ids;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return ids;







    const QString repoOwner = m_repositories.at(m_repoDetailIndex).owner;
    const QString repoName = m_repositories.at(m_repoDetailIndex).name;
    const QStringList commitShas = pullCommitShas(pr);
    QSet<QString> shas(commitShas.cbegin(), commitShas.cend());

    const QString dir = repoGitDir();
    if (!dir.isEmpty() && !pr.head.isEmpty()) {
        QByteArray tip;
        if (runGitCapture(dir, {"rev-parse", pr.head}, &tip, nullptr))
            shas.insert(QString::fromUtf8(tip).trimmed());
    }
    if (shas.isEmpty())
        return ids;
    for (const ActionRun &run : std::as_const(m_actionRuns)) {
        if (run.owner == repoOwner && run.name == repoName &&
            shas.contains(run.commit))
            ids.append(run.id);
    }
    return ids;
}

void MainWindow::renderPullChecks(PullRequest pr)
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
        m_pullChecksLog->setPlainText(displaySafePlainLog(
            "No checks have run for this pull request yet. Use \"Run checks against "
            "this PR\" to queue this repository's push workflows."));
}



void MainWindow::renderPullChecksSummary(PullRequest pr)
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
                 run->status == ActionStatus::Cancelled ||
                 run->status == ActionStatus::Skipped)
            ++failed;
        else if (run->status == ActionStatus::Running)
            ++running;
        else
            ++pending;
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
    m_pullChecksLog->setPlainText(displaySafePlainLog(
        m_actionStore ? m_actionStore->readLog(*run) : QString()));
    m_pullChecksLog->moveCursor(QTextCursor::End);
}

void MainWindow::runChecksForCurrentPull()
{
    if (m_currentPullNumber < 0 || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;





    PullRequest pr;
    for (const PullRequest &p : std::as_const(m_currentPulls))
        if (p.number == m_currentPullNumber)
            pr = p;
    if (pr.number <= 0 || pr.head.isEmpty())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);

    const QString dir = repoGitDir();
    QByteArray tip;
    if (dir.isEmpty() ||
        !runGitCapture(dir, {"rev-parse", pr.head}, &tip, nullptr) ||
        tip.trimmed().isEmpty()) {
        setRepoDetailNotice(
            "Could not resolve the pull request's head commit to run checks.", true);
        return;
    }
    queueWorkflowsForCommit(m_repoDetailIndex, repo.owner, repo.name,
                            QString::fromUtf8(tip).trimmed(),
                            QStringLiteral("refs/heads/") + pr.head);
    renderPullChecks(pr);
    renderPullChecksSummary(pr);
    renderPullReviewSummary(pr);
    updatePullSubTabCounts(pr);
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



    const QString head = pr->head;
    const int number = pr->number;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    const QString gitDir = repoGitDir();
    if (gitDir.isEmpty()) {
        setRepoDetailNotice("No local copy of this repository to build from.", true);
        return;
    }


    QByteArray tip;
    if (!runGitCapture(gitDir, {QStringLiteral("rev-parse"), head}, &tip,
                       nullptr) ||
        tip.trimmed().isEmpty()) {
        setRepoDetailNotice(
            "Could not resolve this pull request's head commit to build it.", true);
        return;
    }
    const QString commit = QString::fromUtf8(tip).trimmed();




    QString slug = repo.owner + QLatin1Char('-') + repo.name;
    slug.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                 QStringLiteral("_"));
    const QString previewDir =
        QDir::tempPath() + QStringLiteral("/forkmesh-pr-preview/%1-pr%2")
                               .arg(slug).arg(number);
    const QString clientDir = previewDir + QStringLiteral("/qt_client");
    const QString buildDir = clientDir + QStringLiteral("/build");
    const bool haveWorktree = QFileInfo::exists(previewDir + QStringLiteral("/.git"));


    if (m_pullPreviewDialog) {
        m_pullPreviewDialog->deleteLater();
        m_pullPreviewDialog = nullptr;
    }
    auto *dialog = new QDialog(this);
    m_pullPreviewDialog = dialog;


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



    if (!haveWorktree) {
        QDir(previewDir).removeRecursively();
        QDir().mkpath(QFileInfo(previewDir).absolutePath());
    }


    auto steps = std::make_shared<QList<PullPreviewStep>>(
        pullPreviewSteps(gitDir, previewDir, clientDir, buildDir, commit,
                         haveWorktree, ramCappedBuildJobs()));

    auto runNext = std::make_shared<std::function<void(int)>>();
    *runNext = [this, steps, runNext, dlg, statusPtr, appendLog,
                launchPreview](int index) {
        if (!dlg)
            return;
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

void MainWindow::updatePullSubTabCounts(PullRequest pr)
{
    const auto label = [](QPushButton *b, const QString &name, int n) {
        if (b)
            b->setText(n > 0 ? QStringLiteral("%1 %2").arg(name).arg(n) : name);
    };
    if (pr.number <= 0) {
        label(m_pullTabConversation, QStringLiteral("Conversation"), 0);
        label(m_pullTabCommits, QStringLiteral("Commits"), 0);
        label(m_pullTabChecks, QStringLiteral("Checks"), 0);
        label(m_pullTabFiles, QStringLiteral("Changes in Git"), 0);
        return;
    }
    const PullReviewSnapshot snapshot = buildPullReviewSnapshot(pr);
    label(m_pullTabConversation, QStringLiteral("Conversation"),
          snapshot.topLevelItems + snapshot.totalThreads);
    label(m_pullTabCommits, QStringLiteral("Commits"), pullCommitShas(pr).size());
    label(m_pullTabChecks, QStringLiteral("Checks"), runIdsForPull(pr).size());
    label(m_pullTabFiles, QStringLiteral("Changes in Git"), pr.filesChanged);
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




    QString head;
    for (const PullRequest &pr : m_currentPulls)
        if (pr.number == m_currentPullNumber) {
            head = pr.head;
            break;
        }
    const AgentSession *linked = agentSessionForPull(m_currentPullNumber, head);
    const int linkedId = linked ? linked->id : -1;
    if (linkedId < 0 || !findAgentSession(linkedId)) {
        flashMessage(QStringLiteral("No agent session found for this pull request."),
                     true);
        return;
    }


    PullStore store = pullStoreForCurrentRepo();
    if (store.canWrite()) {
        QString error;
        store.addComment(m_currentPullNumber, feedback, &error);
    }





    AgentSession *session = findAgentSession(linkedId);
    if (!session) {
        flashMessage(QStringLiteral("No agent session found for this pull request."),
                     true);
        return;
    }


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
    int independentApprovals = 0;
    bool independentChangesRequested = false;
    bool independentReviewReady = false;
    for (const PullRequest &pr : m_currentPulls) {
        if (pr.number == m_currentPullNumber) {
            open   = pr.status == "open";
            closed = pr.status == "closed";
            merged = pr.status == "merged";
            head   = pr.head;
            base   = pr.base;
            patch  = pr.patch;
            independentApprovals = pr.independentApprovalCount();
            independentChangesRequested =
                pr.hasIndependentChangesRequested();
            independentReviewReady = pr.independentReviewGateSatisfied();
        }
    }
    const bool mergeable = writable && have && open;



    const bool reviewBlocks = !independentReviewReady;
    bool behind = false;
    if (mergeable)
        store.isBranchBehindBase(m_currentPullNumber, &behind);







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
        } else if (independentChangesRequested) {
            m_pullMergeStatus->setText(QString::fromUtf8(
                "<span style='color:#f85149'>\xE2\x9A\xA0 Changes requested "
                "\xE2\x80\x94 a reviewer is blocking this merge. Resolve their "
                "review (approve, or clear the request) to merge.</span>"));
            m_pullMergeStatus->show();
        } else if (!independentReviewReady) {
            m_pullMergeStatus->setText(QString::fromUtf8(
                "<span style='color:#d29922'>Peer approval required "
                "\xE2\x80\x94 at least one reviewer other than the pull-request "
                "author must approve before merge.</span>"));
            m_pullMergeStatus->show();
        } else if (mergeClean) {
            m_pullMergeStatus->setText(QString::fromUtf8(
                "<span style='color:#3fb950'>\xE2\x9C\x93 No conflicts \xE2\x80\x94 "
                "%1 independent approval%2; ready to merge. More peer approvals "
                "strengthen the review signal.</span>")
                .arg(independentApprovals)
                .arg(independentApprovals == 1 ? QString() : QStringLiteral("s")));
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
    if (m_pullDeleteAllMergedButton) {
        bool anyMerged = false;
        for (const PullRequest &pr : m_currentPulls) {
            if (pr.status == QLatin1String("merged")) {
                anyMerged = true;
                break;
            }
        }
        m_pullDeleteAllMergedButton->setEnabled(writable && anyMerged &&
                                                !m_pullDeleteInProgress);
    }
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
                      ? QStringLiteral("At least one independent peer approval is "
                                       "required, with no unresolved request for "
                                       "changes.")
                      : mergeable && !mergeClean
                            ? QStringLiteral("This pull request has conflicts — use "
                                             "\"Resolve conflicts\" to commit a fix to "
                                             "its branch, then merge.")
                            : QStringLiteral("Apply and merge this pull request"));
    }


    if (m_pullMergeDeleteButton)
        m_pullMergeDeleteButton->setEnabled(mergeable && mergeClean &&
                                            !conflictPending && !reviewBlocks &&
                                            !m_pullDeleteInProgress);



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


    const bool aiFixBusy = m_aiFix && m_aiFix->number == m_currentPullNumber;
    if (m_pullResolveButton) {
        m_pullResolveButton->setVisible(conflicted);
        m_pullResolveButton->setEnabled(conflicted && !aiFixBusy);
    }
    if (m_pullFixButton) {
        // Shown whenever the PR conflicts. It only writes a prompt, so it needs
        // no API key of its own — but while an AI fix already holds the working
        // tree there is nothing useful to queue up, hence the same busy gate as
        // the other conflict actions.
        m_pullFixButton->setVisible(conflicted);
        m_pullFixButton->setEnabled(conflicted && !aiFixBusy);
    }
    if (m_pullFixConflictsButton) {




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



    if (m_pullSendToSourceButton) {
        const bool offerSend = !writable && have && open;
        m_pullSendToSourceButton->setVisible(offerSend);
        m_pullSendToSourceButton->setEnabled(offerSend);
    }
    if (m_pullDeleteButton)
        m_pullDeleteButton->setEnabled(writable && have);
    if (m_pullDeleteBranchButton)
        m_pullDeleteBranchButton->setEnabled(writable && have);


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



    QString title;
    for (const QString &line : patch.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("Subject:"))) {
            title = line.mid(8).trimmed();
            title.remove(QRegularExpression(QStringLiteral("^\\[PATCH[^\\]]*\\]\\s*")));
            break;
        }
        if (line.startsWith(QLatin1String("diff --git ")))
            break;
    }
    if (title.isEmpty())
        title = QFileInfo(path).completeBaseName();

    const QStringList branches = repoBranches();
    const QString base = repoDefaultBranch(branches);


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


    QString head = QStringLiteral("imported/") +
                   title.toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                                           QStringLiteral("-"));
    head = head.left(60);
    if (head.endsWith(QLatin1Char('-')))
        head.chop(1);

    QString error;
    const int number = store.createPull(
        title, QStringLiteral("Imported from patch file `%1`.").arg(QFileInfo(path).fileName()),
        base, head, patch, QString(),  false, &error);
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




    RepositoryRecord currentRepo = m_repositories.at(m_repoDetailIndex);
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
            if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
                m_repositories[m_repoDetailIndex].localPath = dir;
            saveRepositories();
            refreshRepositoryList();
        }
    }

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
                                              fromRange, &error);
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
    if (!current.independentReviewGateSatisfied()) {
        QMessageBox::warning(
            this, "Merge pull request",
            current.hasIndependentChangesRequested()
                ? QStringLiteral("Pull request #%1 has an unresolved peer "
                                 "\"request changes\" review. Resolve it before "
                                 "merging.")
                      .arg(m_currentPullNumber)
                : QStringLiteral("Pull request #%1 needs at least one approval "
                                 "from a peer other than its author before "
                                 "merging.")
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



    refreshSourceControl(true);
    reloadPulls();


    markAgentSessionsMerged(current.number, current.head);





    if (QSettings().value(kAutoSyncOnMergeSetting, false).toBool()) {
        propagateRepoUpdate(m_repoDetailIndex);
    } else {
        logSystem(QStringLiteral("Merge landed locally — click \"Sync\" to publish "
                                 "it to main (auto-sync-on-merge is off)."));
    }
}







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
    enableHoverRowHighlight(fileList);
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




    const auto takeAllTheirs = [](const QString &text) {
        QStringList lines = text.split('\n');
        const QList<ConflictRegion> regions = findConflicts(lines);

        for (int i = regions.size() - 1; i >= 0; --i) {
            const ConflictRegion r = regions.at(i);
            const QStringList theirs =
                lines.mid(r.sepLine + 1, r.endLine - r.sepLine - 1);
            lines = lines.mid(0, r.startLine) + theirs + lines.mid(r.endLine + 1);
        }
        return lines.join('\n');
    };
    connect(allTheirsBtn, &QPushButton::clicked, &dlg, [=] {
        saveCurrent();
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








void MainWindow::aiFixLog(const QString &text)
{
    if (!m_aiFix || !m_agentStore)
        return;
    if (AgentSession *s = findAgentSession(m_aiFix->sessionId))
        m_agentStore->appendLog(*s, text);
    onAgentLog(m_aiFix->sessionId, text);
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


    reloadAgents();

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
        if (!m_aiReview)
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
            m_aiReview->costUsd += in / 1e6 * 5.0 + out / 1e6 * 25.0;
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
    trackProcessActivity(process, QStringLiteral("review"),
                         QStringLiteral("AI review of the pull request diff"));
#ifdef Q_OS_WIN
    process->start(QStringLiteral("cmd"), {QStringLiteral("/c"), command});
#else
    const QString shell = QFile::exists(QStringLiteral("/bin/bash"))
                              ? QStringLiteral("/bin/bash")
                              : QStringLiteral("/bin/sh");
    process->start(shell, {QStringLiteral("-lc"), command});
#endif
}




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
    onAgentLog(m_aiReview->sessionId, text);
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






    auto *store = new PullStore(pullStoreForCurrentRepo());
    QString error;
    if (!store->startPullAgentEdit(number, &error)) {
        delete store;
        QMessageBox::warning(this, "Fix all with AI", error);
        return;
    }
    const QString editTree = store->agentEditWorkTree();

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
    m_aiFix->workTree = editTree.isEmpty() ? workTree : editTree;
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

// The PR header's "Fix" button (adhoc #7). Rather than starting a resolver
// behind the user's back, it composes the task and drops it into the footer
// prompt box — the same target "Send to Prompt" and the log views' plus glyph
// use — so it can be read, edited and aimed at any agent before it runs.
void MainWindow::fillPromptWithPullConflictFix()
{
    if (m_currentPullNumber <= 0)
        return;
    PullRequest current;
    for (const PullRequest &pr : std::as_const(m_currentPulls))
        if (pr.number == m_currentPullNumber)
            current = pr;

    QStringList lines;
    lines << QStringLiteral("Resolve the merge conflicts on pull request #%1%2.")
                 .arg(m_currentPullNumber)
                 .arg(current.title.isEmpty()
                          ? QString()
                          : QStringLiteral(" (\"%1\")").arg(current.title));
    if (!current.head.isEmpty() && !current.base.isEmpty())
        lines << QStringLiteral("Its branch %1 no longer applies cleanly to %2.")
                     .arg(current.head, current.base);
    // reloadPulls()'s dry-run already cached which files conflict, so name them
    // instead of making the agent rediscover them.
    const auto cached = m_pullConflictCache.constFind(m_currentPullNumber);
    if (cached != m_pullConflictCache.constEnd() && !cached->conflictFiles.isEmpty())
        lines << QStringLiteral("Conflicting files: %1.")
                     .arg(cached->conflictFiles.join(QStringLiteral(", ")));
    lines << QStringLiteral(
        "Merge the base branch in, resolve every conflict keeping both sides' "
        "intent, and commit the fix to the pull request's own branch so it "
        "merges cleanly. Leave the pull request open.");
    appendTextToActivePrompt(lines.join(QLatin1Char(' ')));
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




    auto *store = new PullStore(pullStoreForCurrentRepo());
    QStringList conflicted;
    bool resolvedClean = false;
    QString error;
    if (!store->startConflictAgentEdit(
            number, &conflicted, &resolvedClean, &error)) {
        delete store;
        QMessageBox::warning(this, "Fix conflicts", error);
        return;
    }



    AgentSession session;
    session.owner = repo.owner;
    session.name = repo.name;
    session.issueNumber = 0;
    session.issueTitle = QStringLiteral("Resolve conflicts on PR #%1").arg(number);
    session.provider = provider;
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
    m_aiFix->workTree = store->agentEditWorkTree();
    m_aiFix->files = conflicted;
    m_aiFix->claudeCode = claudeCode;



    if (m_pullMergeStatus) {
        m_pullMergeStatus->setText(QString::fromUtf8(
            "<span style='color:#58a6ff'>\xF0\x9F\xA4\x96 %1 is resolving conflicts\xE2\x80\xA6 "
            "watch it on the Agents tab.</span>").arg(agentProviderName(provider)));
        m_pullMergeStatus->show();
    }
    updatePullActionState();
    switchToAgentsTab(session.id);

    if (resolvedClean) {


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
        if (!m_aiFix)
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
            m_aiFix->costUsd += in / 1e6 * 1.0 + out / 1e6 * 5.0;
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




void MainWindow::aiFixRunClaudeCode()
{
    if (!m_aiFix)
        return;






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


    QString promptQuoted = promptPath;
    promptQuoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    promptQuoted = QLatin1Char('\'') + promptQuoted + QLatin1Char('\'');
    QString command = claudeCodeCommandSetting();




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
    trackProcessActivity(process, QStringLiteral("agent"),
                         QStringLiteral("Claude Code is editing the branch"));
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
    m_aiFix->store->abortConflictMerge();

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


        finalizeResolved();
        return;
    }


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


    PullStore store = pullStoreForCurrentRepo();
    QString content;
    QString error;
    if (!store.startPullFileEdit(number, relPath, &content, &error)) {
        QMessageBox::warning(this, "Edit file", error);
        return;
    }


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


    if (const AgentSession *session = agentSessionForPull(pr.number, pr.head))
        if (session->issueNumber > 0)
            linked.insert(session->issueNumber);

    static const QRegularExpression issueRefRe(
        QStringLiteral("\\bissue[-\\s]+#?(\\d+)\\b|"
                       "\\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\\b\\s*:?\\s*#(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);


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

    for (const PullRequest &pr : m_currentPulls)
        if (issuesLinkedFromPull(pr).contains(issueNumber))
            linked.insert(pr.number);


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

    const int idx = issuesRepoIndex();
    if (idx < 0 || !m_networkAccess)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);
    IssueEvent ev;
    ev.type = QStringLiteral("comment");
    ev.body = body;
    ev = store.makeSignedEvent(issueNumber, ev);
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



    Q_UNUSED(pr);
    return;

#if 0





































































































#endif
}

void MainWindow::autoBountyForMergedPull(const PullRequest &pr)
{



    Q_UNUSED(pr);
    QSettings legacySettings;
    const bool wasEnabled =
        legacySettings.value(kAutoPrBountyEnabledSetting, false).toBool();
    const bool usedWallet =
        legacySettings.value(kAutoPrBountyModeSetting).toString() ==
        QLatin1String("wallet");
    legacySettings.setValue(kAutoPrBountyEnabledSetting, false);
    legacySettings.setValue(kAutoPrBountyModeSetting,
                            QStringLiteral("perPr"));
    if (wasEnabled || usedWallet)
        logSystem(QStringLiteral(
            "Legacy automatic PR bounty funding was disabled; no Worker-held "
            "wallet or escrow request was sent."));
    return;

#if 0

















































































































#endif
}

void MainWindow::pollBountyPayout(const RepositoryRecord &repo, int number,
                                  double amount, const QString &kind)
{


    Q_UNUSED(repo);
    Q_UNUSED(number);
    Q_UNUSED(amount);
    Q_UNUSED(kind);
    return;

#if 0


























































#endif
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



    if (pr->sig.isEmpty() || pr->author.isEmpty()) {
        QMessageBox::warning(
            this, "Send to source of truth",
            "This pull request is missing its signature, so it can't be delivered "
            "to the source of truth.");
        return;
    }



    const PullRequest pull = *pr;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
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
    submitPullToInbox(pull, repo);
}




void MainWindow::setPullDeleteButtonsEnabled(bool enabled)
{
    if (m_pullDeleteButton)
        m_pullDeleteButton->setEnabled(enabled);
    if (m_pullDeleteBranchButton)
        m_pullDeleteBranchButton->setEnabled(enabled);
    if (m_pullMergeDeleteButton)
        m_pullMergeDeleteButton->setEnabled(enabled);
    if (m_pullDeleteAllMergedButton)
        m_pullDeleteAllMergedButton->setEnabled(enabled);
}

bool MainWindow::confirmPullDeletion(const QString &prompt, bool *rewriteHistory)
{
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Delete pull request"),
                    prompt, QMessageBox::Ok | QMessageBox::Cancel, this);


    auto *purge = new QCheckBox(
        QStringLiteral("Also scrub the PR's diff from git history (slow)"));
    purge->setChecked(false);
    box.setCheckBox(purge);
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


        const QString prompt =
            QStringLiteral("Permanently delete pull request #%1? This cannot be undone.")
                .arg(m_currentPullNumber);
        const bool confirmed = confirmPullDeletion(prompt, &rewriteHistory);
        m_pullDeleteConfirmPending = false;
        if (!confirmed)
            return;
    }






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





void MainWindow::deleteCurrentPullAndBranch()
{
    if (m_currentPullNumber < 0)
        return;
    if (m_pullDeleteInProgress) {
        setRepoDetailNotice(
            QStringLiteral("A pull request deletion is already running."));
        return;
    }

    QString head, base;
    for (const PullRequest &p : std::as_const(m_currentPulls)) {
        if (p.number == m_currentPullNumber) {
            head = p.head;
            base = p.base;
            break;
        }
    }


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
                              false);
}





void MainWindow::deleteAllMergedPullsAndBranches()
{
    if (m_pullDeleteInProgress) {
        setRepoDetailNotice(
            QStringLiteral("A pull request deletion is already running."));
        return;
    }
    QList<PullRequest> merged;
    for (const PullRequest &pr : std::as_const(m_currentPulls)) {
        if (pr.status == QLatin1String("merged"))
            merged.append(pr);
    }
    if (merged.isEmpty()) {
        setRepoDetailNotice(QStringLiteral("No merged pull requests to delete."));
        return;
    }

    const QString prompt =
        QStringLiteral("Permanently delete %1 merged pull request(s) and their "
                       "branches? This cannot be undone.")
            .arg(merged.size());
    bool rewriteHistory = false;
    if (!confirmPullDeletion(prompt, &rewriteHistory))
        return;

    auto queue = std::make_shared<QList<PullRequest>>(std::move(merged));
    auto deletedCount = std::make_shared<int>(0);
    auto step = std::make_shared<std::function<void()>>();
    *step = [this, queue, deletedCount, rewriteHistory, step]() {
        if (queue->isEmpty()) {
            setRepoDetailNotice(
                QStringLiteral("Deleted %1 merged pull request(s).").arg(*deletedCount));
            return;
        }
        const PullRequest pr = queue->takeFirst();


        const bool haveBranch =
            !pr.head.isEmpty() && pr.head != pr.base && pr.head != currentRef();
        ++*deletedCount;
        deletePullAndBranchAsync(pr.number, pr.head, haveBranch, rewriteHistory,
                                  false, [step] { (*step)(); });
    };
    (*step)();
}






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
    if (!current.independentReviewGateSatisfied()) {
        QMessageBox::warning(
            this, "Merge pull request",
            current.hasIndependentChangesRequested()
                ? QStringLiteral("Pull request #%1 has an unresolved peer "
                                 "\"request changes\" review. Resolve it before "
                                 "merging.")
                      .arg(m_currentPullNumber)
                : QStringLiteral("Pull request #%1 needs at least one approval "
                                 "from a peer other than its author before "
                                 "merging.")
                .arg(m_currentPullNumber));
        return;
    }



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



    refreshSourceControl(true);


    markAgentSessionsMerged(m_currentPullNumber, head);





    const bool autoSyncOnMerge =
        QSettings().value(kAutoSyncOnMergeSetting, false).toBool();
    if (!autoSyncOnMerge)
        logSystem(QStringLiteral("Merge landed locally — click \"Sync\" to publish "
                                 "it to main (auto-sync-on-merge is off)."));
    deletePullAndBranchAsync(m_currentPullNumber, head, haveBranch, rewriteHistory,
                              autoSyncOnMerge);
}







void MainWindow::deletePullAndBranchAsync(int number, const QString &head,
                                          bool haveBranch, bool rewriteHistory,
                                          bool propagate,
                                          std::function<void()> onDone)
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
             propagate, onDone]() {
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
                    if (onDone)
                        onDone();
                    return;
                }
                if (m_currentPullNumber == deleted)
                    m_currentPullNumber = -1;




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
                if (onDone)
                    onDone();
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

void MainWindow::syncPullsInbox()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    drainPullsInboxFor(m_repositories.at(m_repoDetailIndex),  true);
}

void MainWindow::drainPullsInboxFor(RepositoryRecord repo, bool interactive)
{
    const RepositoryRecord writable = writableRecordFor(repo);
    bool ownerIntake = false;
    {
        PullStore probe(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                        m_userName);
        ownerIntake = probe.canWrite();
    }
    const bool mirrorIntake =
        !ownerIntake && !repo.previewOnly && !repo.isPrivate &&
        repo.publishToNetwork && !repo.mirrorPath.trimmed().isEmpty() &&
        QDir(repo.mirrorPath).exists();
    if (!ownerIntake && !mirrorIntake)
        return;





    if (!hasOwnerSigningCapability())
        return;

    QUrl url = pullsApiUrl(repo);


    const QString signer =
        mirrorIntake ? accountOwner().trimmed().toLower()
                     : repoSegment(repo.owner, QStringLiteral("owner"));
    if (signer.isEmpty() || !hasOwnerSigningCapability(signer))
        return;
    const QString intakeKey =
        QStringLiteral("pulls:") + repo.owner.trimmed().toLower() +
        QLatin1Char('/') + repo.name.trimmed().toLower();
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
                QMessageBox::warning(this, "Sync inbox",
                                     "Could not reach the inbox: " +
                                         reply->errorString());
            return;
        }
        m_pollBackoff.noteSuccess(backoffKey);
        const QJsonArray pending =
            QJsonDocument::fromJson(reply->readAll())
                .object()
                .value("pending")
                .toArray();
        if (!mirrorIntake) {
            applyPullsInboxPayload(repo, pending, interactive);
            return;
        }
        if (pending.isEmpty())
            return;

        QTemporaryDir worktree(
            QDir::tempPath() + QStringLiteral(
                "/forkmesh-mirror-pulls-XXXXXX"));
        QByteArray pullTip;
        const bool pullBranchExists = runGitCapture(
            repo.mirrorPath,
            {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
             QStringLiteral("refs/heads/forkmesh/pulls^{commit}")},
            &pullTip, nullptr);
        QStringList addArgs{
            QStringLiteral("worktree"), QStringLiteral("add"),
            QStringLiteral("--force")};
        if (!pullBranchExists) {
            const QString base = mirrorHeadBranch(repo.mirrorPath);
            if (base.isEmpty())
                return;
            addArgs << QStringLiteral("-b") << QStringLiteral("forkmesh/pulls")
                    << worktree.path() << base;
        } else {
            addArgs << worktree.path() << QStringLiteral("forkmesh/pulls");
        }
        QString gitError;
        if (!worktree.isValid() ||
            !runGitCapture(repo.mirrorPath, addArgs, nullptr, &gitError)) {
            m_pollBackoff.noteFailure(
                backoffKey, QDateTime::currentMSecsSinceEpoch());
            logSystem(
                QStringLiteral("Mirror pull intake could not open %1/%2: %3")
                    .arg(repo.owner, repo.name,
                         gitError.trimmed().right(240)));
            return;
        }
        runGitCapture(worktree.path(),
                      {QStringLiteral("config"), QStringLiteral("user.name"),
                       signer.left(80)},
                      nullptr, nullptr);
        runGitCapture(
            worktree.path(),
            {QStringLiteral("config"), QStringLiteral("user.email"),
             signer.left(63) +
                 QStringLiteral("@users.noreply.forkmesh.com")},
            nullptr, nullptr);
        RepositoryRecord materialized = repo;
        materialized.localPath = worktree.path();
        applyPullsInboxPayload(
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




void MainWindow::applyPullsInboxPayload(const RepositoryRecord &repo,
                                        const QJsonArray &pending,
                                        bool interactive,
                                        bool mirrorIntake)
{
    if (!hasOwnerSigningCapability())
        return;
    if (pending.isEmpty()) {
        if (interactive)
            QMessageBox::information(this, "Sync inbox",
                                     "No pending pull requests.");
        return;
    }
    const RepositoryRecord writable = writableRecordFor(repo);
    PullStore store(writable.localPath, writable.mirrorPath, &m_profileIdentity,
                    m_userName);
    if (!store.canWrite())
        return;
    int merged = 0;
    QStringList drainedIds;
    QString lastAuthor;
    QString lastTitle;
    for (const QJsonValue &value : pending) {
        const QJsonObject obj = value.toObject();
        const QString inboxId =
            obj.value("id").isDouble()
                ? QString::number(
                      static_cast<qint64>(obj.value("id").toDouble()))
                : QString();


        if (obj.contains("event")) {
            const int number = obj.value("number").toInt();
            const PullEvent ev =
                PullEvent::fromJson(obj.value("event").toObject());
            if (store.applyRemoteEvent(number, ev)) {
                if (!inboxId.isEmpty())
                    drainedIds << inboxId;
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
            if (!inboxId.isEmpty())
                drainedIds << inboxId;
            ++merged;
            lastAuthor = pr.authorName.isEmpty() ? pr.author.left(8)
                                                 : pr.authorName;
            lastTitle = pr.title;
        }
    }
    if (!drainedIds.isEmpty()) {
        QUrl ackUrl = pullsApiUrl(repo);
        const QString ackSigner =
            mirrorIntake ? accountOwner().trimmed().toLower()
                         : repoSegment(repo.owner, QStringLiteral("owner"));
        QUrlQuery query = signedInboxQuery(ackSigner);
        if (mirrorIntake) {
            query.addQueryItem(
                QStringLiteral("mirror"), QStringLiteral("1"));
            appendMirrorStateAttestation(&query, repo, ackSigner);
        }
        query.addQueryItem(QStringLiteral("ids"),
                           drainedIds.join(QStringLiteral(",")));
        ackUrl.setQuery(query);
        m_networkAccess->deleteResource(QNetworkRequest(ackUrl));
    }
    const bool onThisRepo =
        m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size() &&
        m_repositories.at(m_repoDetailIndex).owner == repo.owner &&
        m_repositories.at(m_repoDetailIndex).name == repo.name;
    if (onThisRepo)
        reloadPulls();


    if (!mirrorIntake && merged > 0) {
        propagateRepoUpdate(repoIndexFor(repo.owner, repo.name));

        scanRepoMentionsFor(writable);
    } else if (mirrorIntake && merged > 0) {
        m_mirrorAdvertSig.clear();
        refreshMirrorAdverts();
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
}

void MainWindow::pollOwnedInboxes()
{
    if (!m_networkAccess || !hasOwnerSigningCapability())
        return;


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
            continue;
        seen.insert(key);




        const bool autoSyncIssues =
            QSettings().value(kAutoSyncIssuesSetting, true).toBool();
        if (autoSyncIssues && worktreeTrackedClean(writable.localPath))
            drainIssuesInboxFor(repo,  false);
        drainPullsInboxFor(repo,  false);
        drainDiscussionsInboxFor(repo,  false);
    }
}



QUrlQuery MainWindow::signedInboxQuery(const QString &owner) const
{




    if (!hasOwnerSigningCapability())
        return QUrlQuery();
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-issues-pull-v1\n" + owner + "\n" + ts).toUtf8();
    QUrlQuery query;
    query.addQueryItem("owner", owner);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", m_profileIdentity.signData(canonical));
    return query;
}







void MainWindow::appendMirrorStateAttestation(QUrlQuery *query,
                                              const RepositoryRecord &repo,
                                              const QString &signer) const
{
    if (!query || signer.isEmpty() || !hasOwnerSigningCapability(signer) ||
        !m_profileIdentity.isValid())
        return;
    const QString stateHash = mirrorStateHash(repo.mirrorPath);
    if (stateHash.isEmpty())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-repostate-v1\n" + signer + "\n" +
         repoSegment(repo.name, QStringLiteral("repository")) + "\n" +
         stateHash + "\n" + ts)
            .toUtf8();
    query->addQueryItem(QStringLiteral("state"), stateHash);
    query->addQueryItem(QStringLiteral("stateTs"), ts);
    query->addQueryItem(QStringLiteral("stateSig"),
                        m_profileIdentity.signData(canonical));
}




void MainWindow::scheduleRelaySync()
{
    if (!hasOwnerSigningCapability())
        return;
    if (!m_relaySyncDebounce) {
        m_relaySyncDebounce = new QTimer(this);
        m_relaySyncDebounce->setSingleShot(true);
        m_relaySyncDebounce->setInterval(2000);
        connect(m_relaySyncDebounce, &QTimer::timeout, this,
                &MainWindow::performRelaySync);
    }
    if (!m_relaySyncDebounce->isActive())
        m_relaySyncDebounce->start();
}







void MainWindow::performRelaySync()
{
    if (!m_networkAccess)
        return;



    pollMirrorIssueInboxes();
    const QString account = m_accountName.isEmpty()
        ? QSettings().value(kAccountNameSetting).toString().trimmed()
        : m_accountName;
    if (!hasOwnerSigningCapability(account) || !m_profileIdentity.isValid())
        return;
    if (!m_relaySyncSupported) {

        pollOwnedInboxes();
        drainAgentPrompts();
        return;
    }
    if (m_relaySyncInFlight)
        return;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_pollBackoff.ready(QStringLiteral("relaySync"), nowMs))
        return;
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/sync"));
    url.setQuery(signedInboxQuery(repoSegment(account, QStringLiteral("owner"))));
    m_relaySyncInFlight = true;
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, account] {
        m_relaySyncInFlight = false;
        reply->deleteLater();
        if (!hasOwnerSigningCapability(account))
            return;
        if (reply->error() != QNetworkReply::NoError) {
            const int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 404 ||
                reply->error() == QNetworkReply::ContentNotFoundError) {


                m_relaySyncSupported = false;
                pollOwnedInboxes();
                drainAgentPrompts();
                return;
            }
            if (status == 401 || status == 403) {






                static bool s_relaySyncAuthWarned = false;
                if (!s_relaySyncAuthWarned) {
                    s_relaySyncAuthWarned = true;
                    logSystem(QStringLiteral(
                                  "The relay rejected this node's sign-in (HTTP "
                                  "%1), so new issues, chats and other updates "
                                  "can't sync down. Re-link this node to your "
                                  "account from Settings to reconnect.")
                                  .arg(status));
                }
            }
            m_pollBackoff.noteFailure(QStringLiteral("relaySync"),
                                      QDateTime::currentMSecsSinceEpoch());
            return;
        }
        m_pollBackoff.noteSuccess(QStringLiteral("relaySync"));
        const QJsonArray repos = QJsonDocument::fromJson(reply->readAll())
                                     .object()
                                     .value("repos")
                                     .toArray();
        const bool autoSyncIssues =
            QSettings().value(kAutoSyncIssuesSetting, true).toBool();
        for (const QJsonValue &value : repos) {
            const QJsonObject entry = value.toObject();
            const QString entryOwner = entry.value("owner").toString();
            const QString entryName = entry.value("name").toString();
            int idx = -1;
            for (int i = 0; i < m_repositories.size(); ++i) {
                const RepositoryRecord &r = m_repositories.at(i);
                if (!r.previewOnly &&
                    r.owner.compare(entryOwner, Qt::CaseInsensitive) == 0 &&
                    r.name.compare(entryName, Qt::CaseInsensitive) == 0) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0)
                continue;
            const RepositoryRecord repo = m_repositories.at(idx);
            const RepositoryRecord writable = writableRecordFor(repo);



            if (autoSyncIssues && worktreeTrackedClean(writable.localPath))
                applyIssuesInboxPayload(repo, entry.value("issues").toArray(),
                                         false);
            applyPullsInboxPayload(repo, entry.value("pulls").toArray(),
                                    false);
            applyDiscussionsInboxPayload(repo,
                                         entry.value("discussions").toArray(),
                                          false);
            applyAgentPromptsPayload(repo, entry.value("agentPrompts").toArray());



            const QJsonObject aboutUpdate = entry.value("aboutUpdate").toObject();
            if (!aboutUpdate.isEmpty()) {
                QString aboutError;
                if (applyRepoAboutMetadataAt(
                        idx, aboutUpdate.value("about").toString(),
                        aboutUpdate.value("website").toString(), &aboutError)) {
                    logSystem(QStringLiteral(
                                  "Applied About details edited on the website "
                                  "for %1/%2.")
                                  .arg(entryOwner, entryName));
                } else {
                    logSystem(QStringLiteral(
                                  "Could not apply the website About edit for "
                                  "%1/%2: %3")
                                  .arg(entryOwner, entryName, aboutError));
                }
            }
        }
    });
}
