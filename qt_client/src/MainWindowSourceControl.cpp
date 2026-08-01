// MainWindowSourceControl: MainWindow feature methods, split out of MainWindow.cpp.
// Source Control panel: working-tree changes, staging, and commits.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"
#include "PacmanProgress.h"

#include <QAbstractTextDocumentLayout>
#include <QCheckBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QLayout>
#include <QFutureWatcher>
#include <QGraphicsOpacityEffect>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPromise>
#include <QProcess>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTimer>
#include <QUrl>
#include <QWidgetAction>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <memory>

using namespace forkmesh::ui;

// A left-to-right layout that wraps its items onto the next line when the row
// runs out of width (the classic Qt FlowLayout). The commit toolbar uses it so
// every button/action stays visible and wraps down instead of scrolling
// horizontally off the panel (issue #52).
namespace {
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent, int margin = 0, int hSpacing = 6,
                        int vSpacing = 6)
        : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
    {
        setContentsMargins(margin, margin, margin, margin);
    }
    ~FlowLayout() override
    {
        QLayoutItem *item;
        while ((item = takeAt(0)))
            delete item;
    }
    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int i) const override { return m_items.value(i); }
    QLayoutItem *takeAt(int i) override
    {
        return (i >= 0 && i < m_items.size()) ? m_items.takeAt(i) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        return doLayout(QRect(0, 0, width, 0), true);
    }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize size;
        for (QLayoutItem *item : m_items)
            size = size.expandedTo(item->minimumSize());
        const QMargins m = contentsMargins();
        return size + QSize(m.left() + m.right(), m.top() + m.bottom());
    }

private:
    int doLayout(const QRect &rect, bool testOnly) const
    {
        const QMargins m = contentsMargins();
        const QRect eff =
            rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
        int x = eff.x();
        int y = eff.y();
        int lineHeight = 0;
        for (QLayoutItem *item : m_items) {
            const QSize hint = item->sizeHint();
            int nextX = x + hint.width() + m_hSpace;
            if (nextX - m_hSpace > eff.right() + 1 && lineHeight > 0) {
                x = eff.x();
                y = y + lineHeight + m_vSpace;
                nextX = x + hint.width() + m_hSpace;
                lineHeight = 0;
            }
            if (!testOnly)
                item->setGeometry(QRect(QPoint(x, y), hint));
            x = nextX;
            lineHeight = qMax(lineHeight, hint.height());
        }
        return y + lineHeight - rect.y() + m.bottom();
    }
    QList<QLayoutItem *> m_items;
    int m_hSpace;
    int m_vSpace;
};

// Compact source-control row: the filename stays prominent, its directory is a
// muted suffix, and potentially destructive actions only appear while the row
// is under the pointer. No hover highlight is drawn; only the selected (current)
// row gets a thin green outline.
class ScmFileRow : public QWidget
{
public:
    explicit ScmFileRow(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("scmFileRow"));
        setAttribute(Qt::WA_Hover);
        applyStyle();
    }

    void setActionsWidget(QWidget *actions)
    {
        m_actions = actions;
        if (m_actions)
            m_actions->hide();
    }

    void setSelected(bool selected)
    {
        if (m_selected == selected)
            return;
        m_selected = selected;
        applyStyle();
    }

    std::function<void()> onClicked;

protected:
    void enterEvent(QEnterEvent *event) override
    {
        QWidget::enterEvent(event);
        m_hovered = true;
        applyStyle();
    }

    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        // Moving from the row into one of its tool buttons can briefly produce
        // a leave event. Recheck after Qt settles the mouse target.
        QTimer::singleShot(0, this, [this] {
            if (!underMouse()) {
                m_hovered = false;
                applyStyle();
            }
        });
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onClicked)
            onClicked();
        QWidget::mousePressEvent(event);
    }

private:
    void applyStyle()
    {
        // Current file: a thin green outline only, no fill and no hover tint
        // (issue #252 style, but without the background wash other lists use).
        const QString border =
            m_selected ? QStringLiteral("#2da44e") : QStringLiteral("transparent");
        setStyleSheet(
            QStringLiteral("QWidget#scmFileRow{background:transparent;"
                           "border:1px solid %1;border-radius:4px;}")
                .arg(border));
        if (m_actions)
            m_actions->setVisible(m_hovered);
    }

    QWidget *m_actions = nullptr;
    bool m_hovered = false;
    bool m_selected = false;
};
} // namespace

// ---- Source Control panel (working-tree changes) ---------------------------

// Human-readable name for a `git status --porcelain` status letter.
static QString scmStatusTip(QChar status)
{
    switch (status.toLatin1()) {
    case 'M': return QStringLiteral("Modified");
    case 'A': return QStringLiteral("Added");
    case 'D': return QStringLiteral("Deleted");
    case 'R': return QStringLiteral("Renamed");
    case 'C': return QStringLiteral("Copied");
    case 'U': return QStringLiteral("Unmerged (conflict)");
    case 'T': return QStringLiteral("Type changed");
    case '?': return QStringLiteral("Untracked");
    default: return QStringLiteral("Changed");
    }
}

QWidget *MainWindow::buildSourceControlPanel()
{
    auto *panel = new QWidget;
    auto *root = new QVBoxLayout(panel);
    root->setContentsMargins(16, 10, 16, 6);
    root->setSpacing(6);

    // A compact two-line compose field stays visible. Less-common generation
    // settings live behind the adjacent ellipsis menu so they do not consume
    // most of this narrow source-control pane.
    m_scmMessage = new QPlainTextEdit;
    m_scmMessage->setObjectName("messageInput");
    m_scmMessage->setPlaceholderText("Message (Ctrl+Enter to commit)");
    m_scmMessage->setTabChangesFocus(true);
    m_scmMessage->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scmMessage->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scmMessage->setFixedHeight(
        m_scmMessage->fontMetrics().lineSpacing() * 2 + 14);
    m_scmMessage->setStyleSheet(
        "QPlainTextEdit#messageInput{font-size:11px;padding:4px 6px;}");
    auto *commitShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_scmMessage);
    commitShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(commitShortcut, &QShortcut::activated, this, &MainWindow::scmCommit);

    m_scmGenerateButton = new QPushButton(QString::fromUtf8("\xE2\x9C\xA8 Generate"));
    m_scmGenerateButton->setCursor(Qt::PointingHandCursor);
    m_scmGenerateButton->setToolTip(
        "Draft the message from the changes using Claude or OpenAI");
    // A filled accent so the AI action clearly stands out from the ghost buttons.
    m_scmGenerateButton->setStyleSheet(
        "QPushButton{background:#8957e5;color:#ffffff;border:1px solid #8957e5;"
        "border-radius:6px;padding:4px 12px;font-weight:600;}"
        "QPushButton:hover{background:#9a6ff0;border-color:#9a6ff0;}"
        "QPushButton:disabled{background:#6e6a86;border-color:#6e6a86;"
        "color:#d9d9e3;}");
    connect(m_scmGenerateButton, &QPushButton::clicked, this,
            &MainWindow::generateScmMessage);

    m_scmGenModel = new QComboBox;
    for (int i = 0; i < kScmAiModelCount; ++i)
        m_scmGenModel->addItem(QString::fromLatin1(kScmAiModels[i].label), i);
    // On-device option: no model, no API key, no cost — drafts the message
    // locally from the diff's structure. The sentinel data -1 routes
    // generateScmMessage() to the heuristic path. It's the default: free, instant
    // and private, and it auto-fills as the changes change (see autoFillScmMessage).
    m_scmGenModel->addItem(QStringLiteral("On-device (no AI)"), -1);
    m_scmGenModel->setCurrentIndex(m_scmGenModel->findData(-1));
    m_scmGenModel->setToolTip(
        "How to draft the message: on-device (no AI), or a Claude/OpenAI model");
    // Switching engine: auto-fill immediately when on-device is chosen (no-op for
    // the paid models, which only run on an explicit Generate click).
    connect(m_scmGenModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { autoFillScmMessage(); });

    m_scmGenKind = new QComboBox;
    m_scmGenKind->addItem("Commit message");
    m_scmGenKind->addItem("X post");
    m_scmGenKind->setToolTip("What to generate from the changes");

    // How far back to look for the changes being described: just the current
    // uncommitted edits, everything since an hour ago, or everything since
    // midnight (each window also includes the current uncommitted edits).
    m_scmGenDuration = new QComboBox;
    m_scmGenDuration->addItem("Current changes");
    m_scmGenDuration->addItem("Past hour");
    m_scmGenDuration->addItem("All day");
    m_scmGenDuration->setToolTip("Which changes to summarise");
    // When the user switches to "Past hour" or "All day", enable the Generate
    // button immediately — commits exist in those windows even with a clean tree.
    connect(m_scmGenDuration, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                // Re-evaluate the Generate button enable state now that the
                // duration scope has changed.
                refreshSourceControl();
            });

    m_scmCopyButton = new QPushButton("Copy");
    m_scmCopyButton->setToolTip("Copy the message to the clipboard");
    setOcticon(m_scmCopyButton, "copy", 14);
    connect(m_scmCopyButton, &QPushButton::clicked, this, [this] {
        const QString text =
            m_scmMessage ? m_scmMessage->toPlainText().trimmed() : QString();
        if (text.isEmpty()) {
            if (m_scmGenStatus)
                m_scmGenStatus->setText("Nothing to copy.");
            return;
        }
        QApplication::clipboard()->setText(text);
        if (m_scmGenStatus)
            m_scmGenStatus->setText("Copied.");
    });

    // Live character count (and last generation cost). For an X post it shows the
    // 280-char budget; otherwise a plain count.
    m_scmGenStatus = new QLabel;
    m_scmGenStatus->setObjectName("statusLine");
    m_scmGenStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto updateCharCount = [this] {
        if (!m_scmGenStatus || !m_scmMessage)
            return;
        const int n = m_scmMessage->toPlainText().size();
        const bool tweet = m_scmGenKind && m_scmGenKind->currentIndex() == 1;
        m_scmGenStatus->setText(tweet ? QStringLiteral("%1/280").arg(n)
                                      : QStringLiteral("%1 chars").arg(n));
    };
    connect(m_scmMessage, &QPlainTextEdit::textChanged, this, updateCharCount);
    connect(m_scmGenKind, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [updateCharCount](int) { updateCharCount(); });
    updateCharCount();

    m_scmStageAllButton = new QPushButton("Stage all");
    connect(m_scmStageAllButton, &QPushButton::clicked, this, &MainWindow::scmStageAll);
    m_scmUnstageAllButton = new QPushButton("Unstage all");
    connect(m_scmUnstageAllButton, &QPushButton::clicked, this, &MainWindow::scmUnstageAll);
    m_scmDiscardAllButton = new QPushButton("Discard all");
    connect(m_scmDiscardAllButton, &QPushButton::clicked, this, &MainWindow::scmDiscardAll);
    m_scmCommitButton = new QPushButton("Commit");
    m_scmCommitButton->setObjectName("primaryButton");
    connect(m_scmCommitButton, &QPushButton::clicked, this, &MainWindow::scmCommit);
    m_scmCommitPushButton = new QPushButton("Commit & push");
    m_scmCommitPushButton->setObjectName("primaryButton");
    m_scmCommitPushButton->setToolTip(
        "Commit the staged changes, then publish them to the network mirror "
        "(or push to the upstream branch).");
    connect(m_scmCommitPushButton, &QPushButton::clicked, this,
            &MainWindow::scmCommitAndPush);
    m_scmStageCommitPushButton = new QPushButton("Stage all, commit & push");
    m_scmStageCommitPushButton->setObjectName("primaryButton");
    m_scmStageCommitPushButton->setToolTip(
        "Stage every change, commit them, then publish to the network mirror "
        "(or push to the upstream branch) — in one click.");
    connect(m_scmStageCommitPushButton, &QPushButton::clicked, this,
            &MainWindow::scmStageAllCommitAndPush);
    for (QPushButton *b : {m_scmCopyButton, m_scmStageAllButton,
                           m_scmUnstageAllButton, m_scmDiscardAllButton,
                           m_scmCommitButton, m_scmCommitPushButton,
                           m_scmStageCommitPushButton}) {
        if (b != m_scmCommitButton && b != m_scmCommitPushButton &&
            b != m_scmStageCommitPushButton)
            b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }

    auto *generationMenu = new QMenu(panel);
    generationMenu->setObjectName(QStringLiteral("scmGenerationMenu"));
    auto *generationAction = new QWidgetAction(generationMenu);
    auto *generationPanel = new QWidget(generationMenu);
    generationPanel->setMinimumWidth(260);
    auto *generationLayout = new QVBoxLayout(generationPanel);
    generationLayout->setContentsMargins(10, 10, 10, 10);
    generationLayout->setSpacing(6);
    auto addGenerationField = [&](const QString &label, QWidget *field) {
        auto *caption = new QLabel(label, generationPanel);
        caption->setObjectName(QStringLiteral("statusLine"));
        generationLayout->addWidget(caption);
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        generationLayout->addWidget(field);
    };
    generationLayout->addWidget(m_scmGenerateButton);
    addGenerationField(QStringLiteral("Draft with"), m_scmGenModel);
    addGenerationField(QStringLiteral("Create"), m_scmGenKind);
    addGenerationField(QStringLiteral("Include"), m_scmGenDuration);
    auto *copyStatusRow = new QHBoxLayout;
    copyStatusRow->setContentsMargins(0, 2, 0, 0);
    copyStatusRow->addWidget(m_scmCopyButton);
    copyStatusRow->addWidget(m_scmGenStatus, 1);
    generationLayout->addLayout(copyStatusRow);
    generationAction->setDefaultWidget(generationPanel);
    generationMenu->addAction(generationAction);

    auto *generationMenuButton = new QPushButton(panel);
    generationMenuButton->setObjectName(QStringLiteral("ghostButton"));
    generationMenuButton->setText(QString::fromUtf8("\xE2\x8B\xAF"));
    generationMenuButton->setToolTip(
        QStringLiteral("Message generation and copy options"));
    generationMenuButton->setMenu(generationMenu);
    generationMenuButton->setCursor(Qt::PointingHandCursor);
    generationMenuButton->setFixedWidth(30);

    auto *composeRow = new QHBoxLayout;
    composeRow->setContentsMargins(0, 0, 0, 0);
    composeRow->setSpacing(4);
    composeRow->addWidget(m_scmMessage, 1);
    composeRow->addWidget(generationMenuButton, 0, Qt::AlignTop);
    root->addLayout(composeRow);

    m_scmControlsPanel = new QWidget;
    auto *controlsRow = new FlowLayout(m_scmControlsPanel, 0, 6, 6);
    controlsRow->addWidget(m_scmStageAllButton);
    controlsRow->addWidget(m_scmUnstageAllButton);
    controlsRow->addWidget(m_scmDiscardAllButton);
    controlsRow->addWidget(m_scmCommitButton);
    controlsRow->addWidget(m_scmCommitPushButton);
    controlsRow->addWidget(m_scmStageCommitPushButton);

    QSizePolicy controlsPolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    controlsPolicy.setHeightForWidth(true);
    m_scmControlsPanel->setSizePolicy(controlsPolicy);
    root->addWidget(m_scmControlsPanel);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel("CHANGES");
    title->setObjectName("sectionLabel");
    m_scmCountLabel = new QLabel;
    m_scmCountLabel->setObjectName("statusLine");
    // Read-through tally for the combined diff on the right (adhoc #399): every
    // change lives in one scrollable view, and a file checks itself off once
    // you've scrolled past its end, so this is the "how far in am I" counter.
    m_scmViewedLabel = new QLabel;
    m_scmViewedLabel->setObjectName("statusLine");
    m_scmViewedLabel->setToolTip("Files you have scrolled all the way through");

    // Up/down step through the changed files without touching the tree directly,
    // so you can review each diff in turn from the keyboard or mouse.
    m_scmPrevButton = new QPushButton;
    m_scmPrevButton->setToolTip("Previous change");
    setOcticon(m_scmPrevButton, "chevron-up", 14);
    connect(m_scmPrevButton, &QPushButton::clicked, this,
            [this] { scmSelectAdjacentChange(-1); });
    m_scmNextButton = new QPushButton;
    m_scmNextButton->setToolTip("Next change");
    setOcticon(m_scmNextButton, "chevron-down", 14);
    connect(m_scmNextButton, &QPushButton::clicked, this,
            [this] { scmSelectAdjacentChange(1); });

    m_scmRefreshButton = new QPushButton;
    setOcticon(m_scmRefreshButton, "sync", 14);
    m_scmRefreshButton->setToolTip("Rescan the working tree for changes");
    connect(m_scmRefreshButton, &QPushButton::clicked, this,
            [this] { refreshSourceControl(true); });
    addRefreshSpin(m_scmRefreshButton);

    // Auto-mark-viewed toggle: while checked, a file whose end has scrolled into
    // the diff viewport is checked off "Viewed" (and collapsed) on its own.
    // Shares the PR review page's setting so the behaviour matches everywhere.
    m_scmAutoViewedButton = new QPushButton;
    m_scmAutoViewedButton->setCheckable(true);
    m_scmAutoViewedButton->setChecked(autoMarkViewedOnScrollPref());
    setOcticon(m_scmAutoViewedButton, "eye", 14);
    m_scmAutoViewedButton->setToolTip(
        "Automatically mark files as viewed while scrolling");
    connect(m_scmAutoViewedButton, &QPushButton::clicked, this, [this](bool on) {
        setAutoMarkViewedOnScrollPref(on);
        if (on)
            applyScmAutoMarkViewedOnScroll(); // catch up on where we already are
    });

    for (QPushButton *b : {m_scmPrevButton, m_scmNextButton, m_scmRefreshButton,
                           m_scmAutoViewedButton}) {
        b->setObjectName("ghostButton");
        b->setProperty("buttonSize", "sm");
        b->setCursor(Qt::PointingHandCursor);
    }

    header->addWidget(title);
    header->addWidget(m_scmCountLabel);
    header->addWidget(m_scmViewedLabel);
    header->addStretch();
    header->addWidget(m_scmAutoViewedButton);
    header->addWidget(m_scmPrevButton);
    header->addWidget(m_scmNextButton);
    header->addWidget(m_scmRefreshButton);
    root->addLayout(header);

    m_scmTree = new QTreeWidget;
    m_scmTree->setObjectName("fileTree");
    m_scmTree->setColumnCount(1);
    m_scmTree->setHeaderHidden(true);
    // Low enough that the workspace splitter, not this tree, decides how narrow
    // the left column may get (adhoc #74); rows elide, so they stay readable.
    m_scmTree->setMinimumWidth(160);
    m_scmTree->setRootIsDecorated(true);
    m_scmTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    // ScmFileRow (via setItemWidget) draws its own thin green selection
    // outline; blank the app-wide #fileTree::item:selected solid fill so it
    // doesn't paint underneath the row widget (issue #252 pattern).
    blankSelectionBand(m_scmTree);
    connect(m_scmTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *previous) {
                if (previous) {
                    if (auto *row = dynamic_cast<ScmFileRow *>(
                            m_scmTree->itemWidget(previous, 0)))
                        row->setSelected(false);
                }
                if (!item)
                    return;
                if (auto *row = dynamic_cast<ScmFileRow *>(
                        m_scmTree->itemWidget(item, 0)))
                    row->setSelected(true);
                const QString path = item->data(0, Qt::UserRole).toString();
                if (path.isEmpty())
                    return; // group header
                showScmDiff(path, item->data(0, Qt::UserRole + 1).toBool(),
                            item->data(0, Qt::UserRole + 2).toBool());
            });

    root->addWidget(m_scmTree, 1);

    m_scmEmptyNote =
        new QLabel("No working tree on this node \xE2\x80\x94 changes are read-only here.");
    m_scmEmptyNote->setObjectName("statusLine");
    m_scmEmptyNote->setAlignment(Qt::AlignCenter);
    m_scmEmptyNote->hide();
    root->addWidget(m_scmEmptyNote);

    return panel;
}

// Rows the changes panel shows for a `git status --porcelain=v1 -z` dump: one
// per staged entry plus one per unstaged/untracked entry, so a file that is
// both staged and further modified counts twice — exactly as it appears twice
// in the tree. Must stay in step with the parse in refreshSourceControl()
// below; this is the same total without building any rows, for the activity
// rail's badge.
static int scmChangeCount(const QByteArray &porcelain)
{
    int count = 0;
    const QList<QByteArray> fields = porcelain.split('\0');
    for (int i = 0; i < fields.size(); ++i) {
        const QByteArray f = fields.at(i);
        if (f.size() < 3)
            continue; // "XY <path>"
        const char x = f.at(0), y = f.at(1);
        // Renames/copies carry the original path in the following NUL field.
        if (x == 'R' || x == 'C')
            ++i;
        if (x == '?' && y == '?') {
            ++count;
            continue;
        }
        if (x != ' ')
            ++count;
        if (y != ' ')
            ++count;
    }
    return count;
}

// Keep the activity rail's Git badge honest whichever repo tab is on screen.
// refreshSourceControl() only runs when the changes panel is opened or the
// window is re-activated with it already visible, so sitting on the Code tab —
// or simply opening a repo — left the badge blank (or still showing the repo
// we came from) while files sat uncommitted. This probe's only job is that
// count, so it runs detached and is cheap enough for the poll in the
// constructor to drive it while an agent edits the working tree underneath us.
void MainWindow::refreshRepoChangeBadge()
{
    if (!m_railGitButton)
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty() || !repoHasWorkingTree()) {
        m_railGitButton->setBadgeCount(0);
        return;
    }
    const int forIndex = m_repoDetailIndex;
    runGitDetached(dir,
                   {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
                    QStringLiteral("-z")},
                   [this, forIndex, dir](bool ok, const QByteArray &out) {
                       // The user can switch repos while git runs; a late reply
                       // must not stamp the wrong repo's count on the badge.
                       if (!ok || !m_railGitButton ||
                           m_repoDetailIndex != forIndex)
                           return;
                       m_railGitButton->setBadgeCount(scmChangeCount(out));
                       // Rebuild the panel only when it's on screen *and* the
                       // tree actually moved: the rebuild drops the open diff
                       // and the selection, so it must never run speculatively.
                       // Same repo-keyed scan key refreshSourceControl() caches.
                       const QByteArray scanKey = dir.toUtf8() + '\0' + out;
                       if (scanKey != m_scmStatusCache && m_scmTree &&
                           m_scmTree->isVisible())
                           refreshSourceControl();
                   });
}

void MainWindow::refreshSourceControl()
{
    refreshSourceControl(/*force=*/false);
}

void MainWindow::refreshSourceControl(bool force)
{
    if (!m_scmTree)
        return;
    // A manual refresh should also recheck commits pending sync (the top "Sync"
    // button's count), not just the working-tree file changes, since staging/
    // committing/pulling elsewhere can leave that button stale.
    if (force)
        refreshRepoSyncIndicators();
    const QString dir = repoGitDir();
    const bool canWrite = !dir.isEmpty() && repoHasWorkingTree();
    if (m_scmEmptyNote)
        m_scmEmptyNote->setVisible(!canWrite);
    for (QWidget *w : {static_cast<QWidget *>(m_scmMessage),
                       static_cast<QWidget *>(m_scmTree),
                       static_cast<QWidget *>(m_scmStageAllButton),
                       static_cast<QWidget *>(m_scmUnstageAllButton),
                       static_cast<QWidget *>(m_scmDiscardAllButton)})
        if (w)
            w->setEnabled(canWrite);

    if (!canWrite) {
        m_scmStatusCache.clear();
        m_scmPatchValid = false;
        m_scmDiffRenderKey.clear();
        m_scmSectionKeys.clear();
        m_scmSectionAnchors.clear();
        m_scmSectionPaths.clear();
        m_scmStickyLabelHtml.clear();
        m_scmFileTops.clear();
        m_scmStickySection.clear();
        if (m_scmStickyHeader)
            m_scmStickyHeader->hide();
        m_scmTree->clear();
        if (m_scmDiff)
            m_scmDiff->clear();
        if (m_scmCountLabel)
            m_scmCountLabel->clear();
        if (m_scmViewedLabel)
            m_scmViewedLabel->clear();
        if (m_railGitButton)
            m_railGitButton->setBadgeCount(0);
        if (m_scmCommitButton)
            m_scmCommitButton->setEnabled(false);
        if (m_scmCommitPushButton)
            m_scmCommitPushButton->setEnabled(false);
        if (m_scmStageCommitPushButton)
            m_scmStageCommitPushButton->setEnabled(false);
        if (m_scmGenerateButton)
            m_scmGenerateButton->setEnabled(false);
        return;
    }

    QByteArray out;
    runGitCapture(dir, {"status", "--porcelain=v1", "-z"}, &out, nullptr);

    // Skip the full rebuild when the working tree is unchanged since the last
    // scan. This matters now that we rescan on tab focus / window activation:
    // without it, every rescan would clear the tree (losing the open diff and the
    // selection) and flicker even when nothing moved. Manual refresh skips this
    // short-circuit via force=true.
    // Keyed by repo as well as status output: two repos can produce byte-identical
    // `git status`, and the short-circuit would then leave the previous repo's
    // tree — and now its whole rendered diff — on screen.
    const QByteArray scanKey = dir.toUtf8() + '\0' + out;
    if (!force && scanKey == m_scmStatusCache && m_scmTree->topLevelItemCount() > 0)
        return;
    m_scmStatusCache = scanKey;
    m_scmPatchValid = false; // the tree changed, so the combined patch is stale

    // Remember which file's diff is showing so the rebuild can restore it instead
    // of dropping the user back to a blank diff view.
    QString prevPath;
    bool prevStaged = false;
    if (QTreeWidgetItem *cur = m_scmTree->currentItem()) {
        prevPath = cur->data(0, Qt::UserRole).toString();
        prevStaged = cur->data(0, Qt::UserRole + 1).toBool();
    }

    m_scmTree->clear();

    struct Row {
        QString path;
        QChar status;
        bool staged;
        bool untracked;
    };
    QList<Row> staged, changes;
    const QList<QByteArray> fields = out.split('\0');
    for (int i = 0; i < fields.size(); ++i) {
        const QByteArray f = fields.at(i);
        if (f.size() < 3)
            continue; // "XY <path>"
        const char x = f.at(0), y = f.at(1);
        const QString path = QString::fromUtf8(f.mid(3));
        // Renames/copies carry the original path in the following NUL field.
        if (x == 'R' || x == 'C')
            ++i;
        if (x == '?' && y == '?') {
            changes.append({path, QChar('?'), false, true});
            continue;
        }
        if (x != ' ')
            staged.append({path, QChar(x), true, false});
        if (y != ' ')
            changes.append({path, QChar(y), false, false});
    }

    auto addGroup = [this](const QString &name, const QList<Row> &rows,
                           bool stagedGroup) {
        if (rows.isEmpty())
            return;
        auto *group = new QTreeWidgetItem(m_scmTree);
        group->setFirstColumnSpanned(true);

        // VS Code-style group header: a bold "Name (N)" label with hover actions
        // floated to the right — Open Changes (a combined diff of the whole group)
        // plus Stage/Unstage all, and Discard all for unstaged changes. The label
        // lives in the row widget (not setText) so the buttons can sit at the far
        // right of the full-width header row, the way the per-file actions do.
        auto *gw = new QWidget;
        auto *gh = new QHBoxLayout(gw);
        gh->setContentsMargins(0, 0, 6, 0);
        gh->setSpacing(0);
        auto *gl =
            new QLabel(QStringLiteral("%1 (%2)").arg(name).arg(rows.size()), gw);
        QFont gf = gl->font();
        gf.setBold(true);
        gl->setFont(gf);
        gh->addWidget(gl);
        gh->addStretch();
        auto groupBtn = [&](const QString &glyph, const QString &tip) {
            auto *b = new QToolButton(gw);
            b->setText(glyph);
            b->setToolTip(tip);
            b->setAutoRaise(true);
            b->setCursor(Qt::PointingHandCursor);
            return b;
        };
        auto *openAll = new QToolButton(gw);
        openAll->setIcon(themedOcticon("diff", QColor("#8b949e"), 14));
        openAll->setToolTip("Open all changes");
        openAll->setAutoRaise(true);
        openAll->setCursor(Qt::PointingHandCursor);
        connect(openAll, &QToolButton::clicked, this,
                [this, stagedGroup] { showScmDiffAll(stagedGroup); });
        gh->addWidget(openAll);
        if (stagedGroup) {
            auto *u =
                groupBtn(QString::fromUtf8("\xE2\x88\x92"), "Unstage all changes");
            connect(u, &QToolButton::clicked, this, &MainWindow::scmUnstageAll);
            gh->addWidget(u);
        } else {
            auto *d =
                groupBtn(QString::fromUtf8("\xE2\x86\xBA"), "Discard all changes");
            connect(d, &QToolButton::clicked, this, &MainWindow::scmDiscardAll);
            auto *s = groupBtn(QStringLiteral("+"), "Stage all changes");
            connect(s, &QToolButton::clicked, this, &MainWindow::scmStageAll);
            gh->addWidget(d);
            gh->addWidget(s);
        }
        m_scmTree->setItemWidget(group, 0, gw);

        for (const Row &r : rows) {
            auto *item = new QTreeWidgetItem(group);
            item->setFirstColumnSpanned(true);
            item->setToolTip(0, r.path);
            item->setData(0, Qt::UserRole, r.path);
            item->setData(0, Qt::UserRole + 1, r.staged);
            item->setData(0, Qt::UserRole + 2, r.untracked);

            auto *w = new ScmFileRow;
            auto *h = new QHBoxLayout(w);
            h->setContentsMargins(4, 1, 6, 1);
            h->setSpacing(5);

            auto *fileIcon = new QLabel(w);
            fileIcon->setPixmap(
                iconForFile(r.path.section('/', -1)).pixmap(14, 14));
            fileIcon->setFixedSize(14, 14);
            fileIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
            h->addWidget(fileIcon);

            const QString fileName = r.path.section('/', -1);
            QString directory = r.path.left(r.path.size() - fileName.size());
            if (directory.endsWith('/'))
                directory.chop(1);
            auto *fileLabel = new QLabel(fileName, w);
            fileLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            h->addWidget(fileLabel);
            if (!directory.isEmpty()) {
                auto *directoryLabel = new QLabel(directory, w);
                directoryLabel->setObjectName("statusLine");
                directoryLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
                h->addWidget(directoryLabel);
            }
            h->addStretch();

            auto *statusLabel = new QLabel(QString(r.status), w);
            statusLabel->setToolTip(scmStatusTip(r.status));
            QFont sf = statusLabel->font();
            sf.setBold(true);
            statusLabel->setFont(sf);
            h->addWidget(statusLabel);

            auto *actions = new QWidget(w);
            auto *actionsLayout = new QHBoxLayout(actions);
            actionsLayout->setContentsMargins(0, 0, 0, 0);
            actionsLayout->setSpacing(0);
            auto makeBtn = [&](const QString &glyph, const QString &tip) {
                auto *b = new QToolButton(actions);
                b->setText(glyph);
                b->setToolTip(tip);
                b->setAutoRaise(true);
                b->setCursor(Qt::PointingHandCursor);
                return b;
            };
            const QString path = r.path;
            const bool untracked = r.untracked;
            if (r.staged) {
                auto *u = makeBtn(QString::fromUtf8("\xE2\x88\x92"), "Unstage");
                connect(u, &QToolButton::clicked, this,
                        [this, path] { scmUnstagePath(path); });
                actionsLayout->addWidget(u);
            } else {
                auto *d = makeBtn(QString::fromUtf8("\xE2\x86\xBA"), "Discard changes");
                connect(d, &QToolButton::clicked, this,
                        [this, path, untracked] { scmDiscardPath(path, untracked); });
                auto *s = makeBtn(QStringLiteral("+"), "Stage");
                connect(s, &QToolButton::clicked, this,
                        [this, path] { scmStagePath(path); });
                actionsLayout->addWidget(d);
                actionsLayout->addWidget(s);
            }
            auto *open = new QToolButton(actions);
            open->setIcon(themedOcticon("file", QColor("#8b949e"), 14));
            open->setToolTip("Open file");
            open->setAutoRaise(true);
            open->setCursor(Qt::PointingHandCursor);
            connect(open, &QToolButton::clicked, this,
                    [this, path] { openRepoFile(path); });
            actionsLayout->addWidget(open);
            h->addWidget(actions);
            w->setActionsWidget(actions);
            // setCurrentItem() alone only shows the diff via currentItemChanged,
            // which Qt does not emit when this row is already the current item
            // (e.g. the user switched the right pane to a branch/PR view, then
            // clicked back on the still-selected file). Show the diff directly
            // so a click always does, regardless of prior selection state.
            const bool staged = r.staged;
            w->onClicked = [this, item, path, staged, untracked] {
                m_scmTree->setCurrentItem(item);
                showScmDiff(path, staged, untracked);
            };
            m_scmTree->setItemWidget(item, 0, w);
        }
        group->setExpanded(true);
    };
    addGroup("Staged Changes", staged, /*stagedGroup=*/true);
    addGroup("Changes", changes, /*stagedGroup=*/false);
    // Widen the tree so the full relative paths are always visible (capped so the
    // diff still has room); the user can drag the splitter wider from there.
    fitTreeToWidestEntry(m_scmTree);

    const int total = staged.size() + changes.size();
    if (m_scmCountLabel)
        m_scmCountLabel->setText(total ? QString::number(total) : QString());
    // Mirror the count onto the activity rail's Git badge (adhoc #357).
    if (m_railGitButton)
        m_railGitButton->setBadgeCount(total);
    const bool anything = total > 0;
    if (m_scmCommitButton)
        m_scmCommitButton->setEnabled(anything);
    if (m_scmCommitPushButton)
        m_scmCommitPushButton->setEnabled(anything);
    if (m_scmStageCommitPushButton)
        m_scmStageCommitPushButton->setEnabled(anything);
    if (m_scmGenerateButton) {
        // Enable Generate when there are current changes, OR when the
        // duration dropdown covers committed history (past hour / all day)
        // — in those modes, commits exist even with a clean working tree.
        const bool durCoversHistory =
            m_scmGenDuration && m_scmGenDuration->currentIndex() > 0;
        m_scmGenerateButton->setEnabled(anything || durCoversHistory);
    }
    if (m_scmUnstageAllButton)
        m_scmUnstageAllButton->setEnabled(!staged.isEmpty());
    if (m_scmDiscardAllButton)
        m_scmDiscardAllButton->setEnabled(!changes.isEmpty());

    // On-device drafting is free, local and instant, so when it's the selected
    // engine auto-fill the message from the changes (never the paid AI models).
    autoFillScmMessage();

    // Re-render the whole change set into the diff pane on the right, so the
    // review is one continuous scroll rather than a click per file (adhoc #399).
    renderScmCombinedDiff();

    // Re-select the file that was open before the rebuild (scrolling the combined
    // diff back to it) if it still has changes.
    if (!prevPath.isEmpty()) {
        for (int g = 0; g < m_scmTree->topLevelItemCount(); ++g) {
            QTreeWidgetItem *grp = m_scmTree->topLevelItem(g);
            for (int c = 0; c < grp->childCount(); ++c) {
                QTreeWidgetItem *item = grp->child(c);
                if (item->data(0, Qt::UserRole).toString() == prevPath &&
                    item->data(0, Qt::UserRole + 1).toBool() == prevStaged) {
                    m_scmTree->setCurrentItem(item);
                    return;
                }
            }
        }
    }
}

// QSettings context (see loadDiffViewed) for the working-tree diff. One shared
// context for the whole change set — a path is "viewed" whether its staged or
// its unstaged side is the one you scrolled through.
QString MainWindow::scmViewedContext()
{
    return QStringLiteral("worktree");
}

// Sticky-header overlay and scroll wiring for the combined changes diff, built
// once right after m_scmDiff is constructed (adhoc #399). Mirrors the PR review
// page's header (adhoc #56) and adds a read percentage next to the chart.
void MainWindow::setupScmDiffPane()
{
    if (!m_scmDiff || m_scmStickyHeader)
        return;
    m_scmDiff->setOpenExternalLinks(false);
    m_scmDiff->setOpenLinks(false); // "viewed:" toggles are handled here
    connect(m_scmDiff, &QTextBrowser::anchorClicked, this,
            &MainWindow::onScmDiffAnchorClicked);

    // Parented to the viewport so it floats over the text instead of scrolling
    // away with the document.
    m_scmStickyHeader = new QFrame(m_scmDiff->viewport());
    m_scmStickyHeader->setObjectName("diffStickyHeader");
    const bool dark = qApp->palette().color(QPalette::Base).lightness() < 128;
    m_scmStickyHeader->setStyleSheet(
        QStringLiteral("#diffStickyHeader{background:%1;border-bottom:1px solid %2;}"
                       "#diffStickyHeader QLabel{background:transparent;color:%3;}"
                       "#diffStickyHeader QPushButton{background:transparent;"
                       "border:1px solid %2;border-radius:5px;color:%3;"
                       "font-size:11px;padding:3px 7px;}"
                       "#diffStickyHeader QPushButton:hover{color:#3fb950;"
                       "border-color:#3fb950;}"
                       "#diffStickyHeader QPushButton:checked{background:#238636;"
                       "border-color:#2ea043;color:#ffffff;}")
            .arg(dark ? "#161b22" : "#f6f8fa", dark ? "#30363d" : "#d0d7de",
                 dark ? "#8b949e" : "#57606a"));
    auto *sl = new QHBoxLayout(m_scmStickyHeader);
    sl->setContentsMargins(10, 4, 8, 4);
    sl->setSpacing(6);
    m_scmStickyPath = new QLabel(m_scmStickyHeader);
    m_scmStickyPath->setTextFormat(Qt::RichText);
    m_scmStickyPath->setTextInteractionFlags(Qt::NoTextInteraction);
    sl->addWidget(m_scmStickyPath, 1);
    m_scmStickyPacman = new PacmanProgress(m_scmStickyHeader);
    m_scmStickyPacman->setToolTip(
        QStringLiteral("How much of this file you've scrolled through"));
    sl->addWidget(m_scmStickyPacman, 0);
    m_scmStickyPercent = new QLabel(m_scmStickyHeader);
    m_scmStickyPercent->setToolTip(m_scmStickyPacman->toolTip());
    sl->addWidget(m_scmStickyPercent, 0);
    m_scmStickyViewed = new QPushButton(m_scmStickyHeader);
    m_scmStickyViewed->setCheckable(true);
    m_scmStickyViewed->setCursor(Qt::PointingHandCursor);
    m_scmStickyViewed->setToolTip(QStringLiteral("Mark this file as viewed"));
    connect(m_scmStickyViewed, &QPushButton::clicked, this, [this] {
        const int idx = m_scmSectionKeys.indexOf(m_scmStickySection);
        if (idx < 0)
            return;
        const QString path = m_scmSectionPaths.at(idx);
        const QString ctx = scmViewedContext();
        setDiffViewed(ctx, path, !loadDiffViewed(ctx).contains(path));
        // Give the completed whole-button checkbox a quick, restrained fade-in
        // so the state change is noticeable without shifting the header.
        auto *effect = new QGraphicsOpacityEffect(m_scmStickyViewed);
        effect->setOpacity(0.55);
        m_scmStickyViewed->setGraphicsEffect(effect);
        auto *animation =
            new QPropertyAnimation(effect, "opacity", m_scmStickyViewed);
        animation->setDuration(180);
        animation->setStartValue(0.55);
        animation->setEndValue(1.0);
        connect(animation, &QPropertyAnimation::finished,
                m_scmStickyViewed, [button = m_scmStickyViewed, effect] {
                    button->setGraphicsEffect(nullptr);
                    effect->deleteLater();
                });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
        renderScmCombinedDiff();
        scrollScmDiffToFile(path, m_scmSectionKeys.at(idx).startsWith(
                                      QLatin1String("s|")));
    });
    sl->addWidget(m_scmStickyViewed, 0);
    m_scmStickyHeader->hide();

    // Re-rendering (which collapses newly-viewed files) is far too heavy for
    // every pixel of a fast scroll, so wait for the scroll to settle first.
    m_scmAutoViewedDebounce = new QTimer(this);
    m_scmAutoViewedDebounce->setSingleShot(true);
    m_scmAutoViewedDebounce->setInterval(400);
    connect(m_scmAutoViewedDebounce, &QTimer::timeout, this,
            &MainWindow::applyScmAutoMarkViewedOnScroll);
    connect(m_scmDiff->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] {
                updateScmDiffScrollState(); // cheap, every tick
                if (m_scmAutoViewedButton && m_scmAutoViewedButton->isChecked())
                    m_scmAutoViewedDebounce->start(); // heavy, debounced
            });
}

// Render every working-tree change — staged, then unstaged, then untracked — as
// one scrollable diff (adhoc #399), in the same order the tree lists them. The
// raw patch is cached (m_scmPatchValid) so toggling a file's Viewed state
// re-renders without re-running git, and an unchanged render is skipped outright
// (laying out a large diff blocks the GUI thread).
void MainWindow::renderScmCombinedDiff()
{
    if (!m_scmDiff)
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty() || !repoHasWorkingTree())
        return;

    if (!m_scmPatchValid) {
        const QString stagedPatch =
            QString::fromUtf8(gitCaptureStdout(dir, {"diff", "--cached"}));
        QString rest = QString::fromUtf8(gitCaptureStdout(dir, {"diff"}));
        // `git diff` omits untracked files; append each as a /dev/null diff so
        // new files are reviewable in the same scroll.
        QByteArray others;
        runGitCapture(dir, {"ls-files", "--others", "--exclude-standard", "-z"},
                      &others, nullptr);
        for (const QByteArray &p : others.split('\0')) {
            if (p.isEmpty())
                continue;
            rest += QString::fromUtf8(gitCaptureStdout(
                dir, {"diff", "--no-index", "--", "/dev/null", QString::fromUtf8(p)}));
        }
        // The renderer starts a new file at every line beginning "diff --git ",
        // so counting them the same way tells us where the staged half ends
        // (body lines are +/-/space-prefixed, so they can't be miscounted).
        m_scmCombinedStagedFiles =
            stagedPatch.count(QStringLiteral("\ndiff --git ")) +
            (stagedPatch.startsWith(QLatin1String("diff --git ")) ? 1 : 0);
        m_scmCombinedPatch = stagedPatch + rest;
        m_scmPatchValid = true;
    }

    QList<DiffFileEntry> files;
    const QString ctx = scmViewedContext();
    const QSet<QString> viewed = loadDiffViewed(ctx);
    const QString html =
        renderDiffHtml(m_scmCombinedPatch, files, dir, QString(), QString(),
                       QString(), QHash<QString, QString>(), viewed);

    m_scmSectionKeys.clear();
    m_scmSectionAnchors.clear();
    m_scmSectionPaths.clear();
    m_scmStickyLabelHtml.clear();
    m_scmFileTops.clear(); // positions move on re-render; force a recompute
    for (int i = 0; i < files.size(); ++i) {
        const DiffFileEntry &f = files.at(i);
        const QString key = (i < m_scmCombinedStagedFiles ? QStringLiteral("s|")
                                                          : QStringLiteral("u|")) +
                            f.path;
        m_scmSectionKeys.append(key);
        m_scmSectionAnchors.append(f.anchor);
        m_scmSectionPaths.append(f.path);
        m_scmStickyLabelHtml.insert(key, diffStickyLabelHtml(f));
    }

    // Forget Viewed marks for paths that no longer have changes (after a commit,
    // say), so the next change set starts the tally from zero.
    const QSet<QString> live(m_scmSectionPaths.begin(), m_scmSectionPaths.end());
    for (const QString &p : viewed)
        if (!live.contains(p))
            setDiffViewed(ctx, p, false);
    updateScmViewedCount();

    const QString body =
        html.isEmpty()
            ? QStringLiteral("<p style='color:#8b949e'>(no working-tree changes)</p>")
            : html;
    const QString key =
        diffStyleSheet(m_diffFontPt) + QLatin1Char('\x1f') + body;
    if (key == m_scmDiffRenderKey)
        return;
    m_scmDiffRenderKey = key;
    setDiffHtml(m_scmDiff, body);
    // The document (and its layout) was replaced; re-read the file positions once
    // the layout has settled.
    m_scmStickySection.clear();
    QTimer::singleShot(0, this, &MainWindow::updateScmDiffScrollState);
}

void MainWindow::showScmDiff(const QString &path, bool staged, bool untracked)
{
    Q_UNUSED(untracked);
    if (!m_scmDiff || m_scmSuppressFileScroll)
        return; // suppressed: the selection is following the scroll, not driving it
    setCommitWorkspacePage(kCommitWorkspaceChangesPage);
    if (m_scmSectionKeys.isEmpty())
        renderScmCombinedDiff();
    scrollScmDiffToFile(path, staged);
}

// "Open all changes" on a group header: jump to the top of that group's half of
// the combined diff.
void MainWindow::showScmDiffAll(bool staged)
{
    if (!m_scmDiff)
        return;
    setCommitWorkspacePage(kCommitWorkspaceChangesPage);
    if (m_scmSectionKeys.isEmpty())
        renderScmCombinedDiff();
    const QString prefix = staged ? QStringLiteral("s|") : QStringLiteral("u|");
    for (int i = 0; i < m_scmSectionKeys.size(); ++i) {
        if (m_scmSectionKeys.at(i).startsWith(prefix)) {
            m_scmDiff->scrollToAnchor(m_scmSectionAnchors.at(i));
            updateScmDiffScrollState();
            return;
        }
    }
}

// Scroll the combined diff so this file's section sits at the top. A path that is
// both staged and further modified renders twice, so `staged` picks the side.
void MainWindow::scrollScmDiffToFile(const QString &path, bool staged)
{
    if (!m_scmDiff)
        return;
    int idx = m_scmSectionKeys.indexOf(
        (staged ? QStringLiteral("s|") : QStringLiteral("u|")) + path);
    if (idx < 0)
        idx = m_scmSectionPaths.indexOf(path); // only one side has a diff
    if (idx < 0)
        return;
    m_scmDiff->scrollToAnchor(m_scmSectionAnchors.at(idx));
    updateScmDiffScrollState();
}

// The changes-tree row for a path on the staged / unstaged side, or nullptr.
QTreeWidgetItem *MainWindow::scmFindItem(const QString &path, bool staged) const
{
    if (!m_scmTree)
        return nullptr;
    for (int g = 0; g < m_scmTree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *grp = m_scmTree->topLevelItem(g);
        for (int c = 0; c < grp->childCount(); ++c) {
            QTreeWidgetItem *item = grp->child(c);
            if (item->data(0, Qt::UserRole).toString() == path &&
                item->data(0, Qt::UserRole + 1).toBool() == staged)
                return item;
        }
    }
    return nullptr;
}

// Select a file in the changes tree *without* letting currentItemChanged scroll
// the diff back to that file's header — here the selection follows the scroll.
void MainWindow::selectScmFileInTree(const QString &path, bool staged)
{
    QTreeWidgetItem *item = scmFindItem(path, staged);
    if (!item || m_scmTree->currentItem() == item)
        return;
    m_scmSuppressFileScroll = true;
    m_scmTree->setCurrentItem(item);
    m_scmTree->scrollToItem(item);
    m_scmSuppressFileScroll = false;
}

// Pin the sticky header across the top of the changes diff viewport at its
// natural height. Called on every scroll tick and on viewport resize.
void MainWindow::layoutScmStickyHeader()
{
    if (!m_scmStickyHeader || !m_scmDiff)
        return;
    QWidget *vp = m_scmDiff->viewport();
    m_scmStickyHeader->setGeometry(0, 0, vp->width(),
                                   m_scmStickyHeader->sizeHint().height());
}

// Walk the rendered diff once and record each section header's absolute document
// y-position. Locating an anchor scans the document, so doing it per file would
// be O(files x doc); this collects them all in a single pass and the result is
// cached until the next re-render (word-wrap is off, so a resize doesn't move
// them).
void MainWindow::computeScmFileTops()
{
    m_scmFileTops.assign(m_scmSectionAnchors.size(), -1);
    if (!m_scmDiff || m_scmSectionAnchors.isEmpty())
        return;
    QScrollBar *vbar = m_scmDiff->verticalScrollBar();
    const int viewTop = vbar ? vbar->value() : 0;
    QHash<QString, int> anchorIndex;
    for (int i = 0; i < m_scmSectionAnchors.size(); ++i)
        if (!m_scmSectionAnchors.at(i).isEmpty())
            anchorIndex.insert(m_scmSectionAnchors.at(i), i);
    QTextDocument *doc = m_scmDiff->document();
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
                m_scmFileTops[ai.value()] = m_scmDiff->cursorRect(cur).top() + viewTop;
            }
        }
    }
}

// Runs on every scroll tick of the combined diff (cheap; no re-render). Works out
// which file sits at the top of the viewport, mirrors its header into the sticky
// bar, advances the read-progress chart / percentage by how much of that file has
// gone past, and selects the file in the tree so the list follows the scroll.
void MainWindow::updateScmDiffScrollState()
{
    if (!m_scmDiff || !m_scmStickyHeader)
        return;
    if (m_scmSectionKeys.isEmpty()) {
        m_scmStickyHeader->hide();
        return;
    }
    QScrollBar *vbar = m_scmDiff->verticalScrollBar();
    if (!vbar)
        return;
    const int viewTop = vbar->value();
    const int viewBottom = viewTop + m_scmDiff->viewport()->height();
    const int docHeight =
        m_scmDiff->document()->documentLayout()->documentSize().height();
    if (m_scmFileTops.size() != m_scmSectionKeys.size())
        computeScmFileTops();

    // The file at the top of the viewport is the first one whose section still
    // reaches below the top edge.
    int idx = -1, fileTop = 0, fileBottom = 0;
    for (int i = 0; i < m_scmSectionKeys.size(); ++i) {
        if (m_scmFileTops.at(i) < 0)
            continue;
        const int bottom = (i + 1 < m_scmFileTops.size() &&
                            m_scmFileTops.at(i + 1) >= 0)
                               ? m_scmFileTops.at(i + 1)
                               : docHeight;
        if (bottom > viewTop) {
            idx = i;
            fileTop = m_scmFileTops.at(i);
            fileBottom = bottom;
            break;
        }
    }
    if (idx < 0) {
        // Text layout can take one event-loop turn after a large diff is
        // replaced. Never let that transient anchor gap hide the filename:
        // retain the current section, or pin the first file until positions
        // become available on the next scroll/layout tick.
        idx = m_scmSectionKeys.indexOf(m_scmStickySection);
        if (idx < 0)
            idx = 0;
        fileTop = 0;
        fileBottom = qMax(1, docHeight);
    }

    // How much of the file has been read: the fraction of its extent that has
    // passed above the viewport's bottom edge, clamped to [0,1].
    double progress = 1.0;
    if (fileBottom > fileTop)
        progress = double(viewBottom - fileTop) / double(fileBottom - fileTop);
    progress = qBound(0.0, progress, 1.0);

    const QString key = m_scmSectionKeys.at(idx);
    const QString path = m_scmSectionPaths.at(idx);
    const bool isViewed = loadDiffViewed(scmViewedContext()).contains(path);
    if (key != m_scmStickySection) {
        m_scmStickySection = key;
        m_scmStickyPath->setText(m_scmStickyLabelHtml.value(key));
        selectScmFileInTree(path, key.startsWith(QLatin1String("s|")));
    }
    m_scmStickyViewed->setText(isViewed ? QString::fromUtf8("\xE2\x98\x91 Viewed")
                                        : QString::fromUtf8("\xE2\x98\x90 Viewed"));
    m_scmStickyViewed->setChecked(isViewed);
    // A finished / already-viewed file reads as done (full green circle);
    // otherwise the chart tracks the scroll in blue and greens on arrival.
    const double shown = isViewed ? 1.0 : progress;
    m_scmStickyPacman->setColor(shown >= 0.999 ? QColor(0x3f, 0xb9, 0x50)
                                               : QColor(0x58, 0xa6, 0xff));
    m_scmStickyPacman->setProgress(shown);
    m_scmStickyPercent->setText(QStringLiteral("%1%").arg(qRound(shown * 100.0)));

    layoutScmStickyHeader();
    m_scmStickyHeader->show();
    m_scmStickyHeader->raise();
}

// Debounced off the diff scrollbar: check off every file the reviewer has
// scrolled all the way through — its end has reached the viewport bottom — as
// "Viewed", matching the read-progress chart, which fills to 100% on the same
// threshold. Re-renders once for the whole batch (collapsing those files), then
// restores the scroll to whichever file is still on screen, since collapsing
// files above it shifts the document up.
void MainWindow::applyScmAutoMarkViewedOnScroll()
{
    if (!m_scmAutoViewedButton || !m_scmAutoViewedButton->isChecked())
        return;
    if (!m_scmDiff || m_scmSectionKeys.isEmpty())
        return;
    QScrollBar *vbar = m_scmDiff->verticalScrollBar();
    if (!vbar)
        return;
    const int viewBottom = vbar->value() + m_scmDiff->viewport()->height();
    const int docHeight =
        m_scmDiff->document()->documentLayout()->documentSize().height();
    if (m_scmFileTops.size() != m_scmSectionKeys.size())
        computeScmFileTops();

    const QString ctx = scmViewedContext();
    const QSet<QString> viewed = loadDiffViewed(ctx);
    QString currentPath; // first file not yet fully scrolled through
    bool currentStaged = false;
    QStringList newlyViewed;
    for (int i = 0; i < m_scmSectionKeys.size(); ++i) {
        if (m_scmFileTops.at(i) < 0)
            continue;
        const int bottom = (i + 1 < m_scmFileTops.size() &&
                            m_scmFileTops.at(i + 1) >= 0)
                               ? m_scmFileTops.at(i + 1)
                               : docHeight;
        const QString path = m_scmSectionPaths.at(i);
        if (bottom <= viewBottom) {
            if (!viewed.contains(path) && !newlyViewed.contains(path))
                newlyViewed << path;
        } else if (currentPath.isEmpty()) {
            currentPath = path;
            currentStaged = m_scmSectionKeys.at(i).startsWith(QLatin1String("s|"));
        }
    }
    if (newlyViewed.isEmpty())
        return;
    for (const QString &path : std::as_const(newlyViewed))
        setDiffViewed(ctx, path, true);
    renderScmCombinedDiff();
    if (!currentPath.isEmpty())
        scrollScmDiffToFile(currentPath, currentStaged);
}

// "N of M files viewed" above the changes tree.
void MainWindow::updateScmViewedCount()
{
    if (!m_scmViewedLabel)
        return;
    const QSet<QString> paths(m_scmSectionPaths.begin(), m_scmSectionPaths.end());
    if (paths.isEmpty()) {
        m_scmViewedLabel->clear();
        return;
    }
    const QSet<QString> viewed = loadDiffViewed(scmViewedContext());
    int seen = 0;
    for (const QString &p : paths)
        if (viewed.contains(p))
            ++seen;
    m_scmViewedLabel->setText(
        QStringLiteral("%1 of %2 files viewed").arg(seen).arg(paths.size()));
}

// Clicking the "Viewed" checkbox inside the combined diff.
void MainWindow::onScmDiffAnchorClicked(const QUrl &url)
{
    if (url.scheme() != QLatin1String("viewed"))
        return;
    const QString path = url.path();
    if (path.isEmpty())
        return;
    const QString ctx = scmViewedContext();
    setDiffViewed(ctx, path, !loadDiffViewed(ctx).contains(path));
    renderScmCombinedDiff();
    // Keep the file that was toggled in view — collapsing it shifts everything
    // below up, so an untouched scroll position would land somewhere random.
    scrollScmDiffToFile(path, m_scmStickySection.startsWith(QLatin1String("s|")));
}

void MainWindow::scmSelectAdjacentChange(int delta)
{
    // Every change shares one scrollable view, so stepping is just the next /
    // previous hunk anywhere in the working tree — scmScrollToAdjacentHunk
    // crosses file boundaries on its own, and the scroll drags the sticky header
    // and the tree selection along with it.
    scmScrollToAdjacentHunk(delta);
}

bool MainWindow::scmScrollToAdjacentHunk(int delta, bool fromEnd)
{
    if (!m_scmDiff)
        return false;
    if (fromEnd)
        m_scmDiff->moveCursor(QTextCursor::End);
    // Each hunk header renders as "@@ -old +new @@ ..."; the "@@ -" prefix occurs
    // exactly once per hunk, so searching for it walks the diff hunk-by-hunk.
    const QTextDocument::FindFlags flags =
        delta < 0 ? QTextDocument::FindBackward : QTextDocument::FindFlags();
    if (!m_scmDiff->find(QStringLiteral("@@ -"), flags))
        return false;
    // Keep the find's selection as the cursor (so a further step advances past
    // it), but scroll the matched hunk header up near the top of the view.
    const QTextCursor found = m_scmDiff->textCursor();
    QTextCursor lineCur(found);
    lineCur.setPosition(found.selectionStart());
    lineCur.movePosition(QTextCursor::StartOfLine);
    const QRect r = m_scmDiff->cursorRect(lineCur);
    if (QScrollBar *vbar = m_scmDiff->verticalScrollBar())
        vbar->setValue(vbar->value() + r.top() - 4);
    return true;
}

void MainWindow::scmStagePath(const QString &path)
{
    // Staging is usually the last thing you do with a file you've just read, so
    // when the file being staged is the one on screen, move on to the next change
    // instead of leaving the diff parked on a now-staged file (adhoc #399).
    QString nextPath;
    bool nextStaged = false;
    if (m_scmTree) {
        QList<QTreeWidgetItem *> files;
        for (int g = 0; g < m_scmTree->topLevelItemCount(); ++g) {
            QTreeWidgetItem *grp = m_scmTree->topLevelItem(g);
            for (int c = 0; c < grp->childCount(); ++c)
                files.append(grp->child(c));
        }
        QTreeWidgetItem *cur = m_scmTree->currentItem();
        const int idx = cur ? files.indexOf(cur) : -1;
        if (idx >= 0 && cur->data(0, Qt::UserRole).toString() == path &&
            !cur->data(0, Qt::UserRole + 1).toBool()) {
            // Prefer the next still-unstaged change; fall back to the row above
            // when this was the last one in the list.
            for (int i = idx + 1; i < files.size(); ++i) {
                if (!files.at(i)->data(0, Qt::UserRole + 1).toBool()) {
                    nextPath = files.at(i)->data(0, Qt::UserRole).toString();
                    break;
                }
            }
            if (nextPath.isEmpty() && idx > 0) {
                nextPath = files.at(idx - 1)->data(0, Qt::UserRole).toString();
                nextStaged = files.at(idx - 1)->data(0, Qt::UserRole + 1).toBool();
            }
        }
    }

    const QString dir = repoGitDir();
    QString err;
    if (!runGitCapture(dir, {"add", "-A", "--", path}, nullptr, &err))
        QMessageBox::warning(this, "Stage", err.isEmpty() ? "git add failed." : err);
    refreshSourceControl();
    if (!nextPath.isEmpty()) {
        if (QTreeWidgetItem *item = scmFindItem(nextPath, nextStaged)) {
            // Fires currentItemChanged -> showScmDiff, which scrolls the combined
            // diff to that file.
            m_scmTree->setCurrentItem(item);
            m_scmTree->scrollToItem(item);
        }
    }
}

void MainWindow::scmUnstagePath(const QString &path)
{
    const QString dir = repoGitDir();
    QString err;
    if (!runGitCapture(dir, {"restore", "--staged", "--", path}, nullptr, &err) &&
        !runGitCapture(dir, {"reset", "-q", "--", path}, nullptr, nullptr))
        QMessageBox::warning(this, "Unstage", err.isEmpty() ? "git restore failed." : err);
    refreshSourceControl();
}

void MainWindow::scmDiscardPath(const QString &path, bool untracked)
{
    if (QMessageBox::warning(
            this, "Discard changes",
            QStringLiteral("Discard changes to \"%1\"? This cannot be undone.").arg(path),
            QMessageBox::Discard | QMessageBox::Cancel) != QMessageBox::Discard)
        return;
    const QString dir = repoGitDir();
    if (untracked) {
        QFile::remove(QDir(dir).filePath(path));
    } else {
        QString err;
        if (!runGitCapture(dir, {"restore", "--", path}, nullptr, &err) &&
            !runGitCapture(dir, {"checkout", "--", path}, nullptr, nullptr))
            QMessageBox::warning(this, "Discard changes",
                                 err.isEmpty() ? "git restore failed." : err);
    }
    refreshSourceControl();
}

void MainWindow::scmStageAll()
{
    const QString dir = repoGitDir();
    QString err;
    if (!runGitCapture(dir, {"add", "-A"}, nullptr, &err))
        QMessageBox::warning(this, "Stage all", err.isEmpty() ? "git add failed." : err);
    refreshSourceControl();
}

void MainWindow::scmUnstageAll()
{
    const QString dir = repoGitDir();
    runGitCapture(dir, {"reset", "-q"}, nullptr, nullptr);
    refreshSourceControl();
}

void MainWindow::scmDiscardAll()
{
    if (QMessageBox::warning(
            this, "Discard all changes",
            "Discard ALL uncommitted changes, including untracked files? "
            "This cannot be undone.",
            QMessageBox::Discard | QMessageBox::Cancel) != QMessageBox::Discard)
        return;
    const QString dir = repoGitDir();
    runGitCapture(dir, {"restore", "--", "."}, nullptr, nullptr);
    runGitCapture(dir, {"clean", "-fd"}, nullptr, nullptr);
    refreshSourceControl();
}

bool MainWindow::performScmCommit()
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || !repoHasWorkingTree())
        return false;
    const QString msg =
        m_scmMessage ? m_scmMessage->toPlainText().trimmed() : QString();
    if (msg.isEmpty()) {
        QMessageBox::information(this, "Commit", "Enter a commit message first.");
        return false;
    }
    QByteArray staged;
    runGitCapture(dir, {"diff", "--cached", "--name-only"}, &staged, nullptr);
    if (staged.trimmed().isEmpty()) {
        if (QMessageBox::question(
                this, "Commit",
                "Nothing is staged. Stage all changes and commit?") != QMessageBox::Yes)
            return false;
        QString err;
        if (!runGitCapture(dir, {"add", "-A"}, nullptr, &err)) {
            QMessageBox::warning(this, "Commit", err.isEmpty() ? "git add failed." : err);
            return false;
        }
    }
    QString err;
    if (!runGitCapture(dir, {"commit", "-m", msg}, nullptr, &err)) {
        QMessageBox::warning(this, "Commit",
                             err.isEmpty() ? "git commit failed." : err.left(300));
        return false;
    }
    if (m_scmMessage)
        m_scmMessage->clear();
    logSystem(QStringLiteral("Committed: %1").arg(msg.section('\n', 0, 0)));
    loadCommits(); // refresh history + the working-changes panel
    if (m_repoDetailIndex >= 0)
        propagateRepoUpdate(m_repoDetailIndex);
    return true;
}

void MainWindow::scmCommit()
{
    performScmCommit();
}

void MainWindow::scmCommitAndPush()
{
    // Only push once the commit actually lands; performScmCommit() surfaces any
    // failure (empty message, git error) itself. pushCurrentRepoUpstream() then
    // publishes to the served network mirror or pushes to the upstream branch,
    // matching the "Publish N" button's behaviour.
    if (performScmCommit())
        pushCurrentRepoUpstream();
}

void MainWindow::scmStageAllCommitAndPush()
{
    const QString dir = repoGitDir();
    if (dir.isEmpty() || !repoHasWorkingTree())
        return;
    // Check the message before staging, so a missing one doesn't leave everything
    // staged for nothing (performScmCommit re-checks once the commit runs).
    if (!m_scmMessage || m_scmMessage->toPlainText().trimmed().isEmpty()) {
        QMessageBox::information(this, "Commit", "Enter a commit message first.");
        return;
    }
    QString err;
    if (!runGitCapture(dir, {"add", "-A"}, nullptr, &err)) {
        QMessageBox::warning(this, "Stage all",
                             err.isEmpty() ? "git add failed." : err);
        return;
    }
    refreshSourceControl();
    if (performScmCommit())
        pushCurrentRepoUpstream();
}

QString MainWindow::scmContextDiff() const
{
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return QString();

    // Duration scope: 0 = current uncommitted edits only, 1 = since an hour ago,
    // 2 = since midnight. For a window we diff the working tree against the newest
    // commit older than the cutoff, which captures everything committed within the
    // window plus the current uncommitted edits in one diff.
    const int dur = m_scmGenDuration ? m_scmGenDuration->currentIndex() : 0;
    QByteArray diff;
    if (dur == 1 || dur == 2) {
        const QString cutoff =
            dur == 1 ? QStringLiteral("1 hour ago") : QStringLiteral("midnight");
        QByteArray revOut;
        runGitCapture(dir, {"rev-list", "-1", "--before=" + cutoff, "HEAD"}, &revOut,
                      nullptr);
        const QString rev = QString::fromUtf8(revOut).trimmed();
        if (!rev.isEmpty())
            diff = gitCaptureStdout(dir, {"diff", rev});
    }
    // Current changes, or a fallback when nothing was committed within the window.
    if (diff.trimmed().isEmpty()) {
        QByteArray staged = gitCaptureStdout(dir, {"diff", "--cached"});
        diff = staged.trimmed().isEmpty() ? gitCaptureStdout(dir, {"diff"}) : staged;
    }
    QString text = QString::fromUtf8(diff);
    // Note any untracked files, whose contents don't appear in `git diff`.
    QByteArray untracked;
    runGitCapture(dir, {"ls-files", "--others", "--exclude-standard"}, &untracked,
                  nullptr);
    const QString others = QString::fromUtf8(untracked).trimmed();
    if (!others.isEmpty())
        text += QStringLiteral("\n\n# New (untracked) files:\n") + others;
    // Cap for token cost; the model only needs a representative slice.
    constexpr int kCap = 12000;
    if (text.size() > kCap)
        text = text.left(kCap) + QStringLiteral("\n... [diff truncated]");
    return text;
}

QStringList MainWindow::scmDiffScopeArgs() const
{
    const QString dir = repoGitDir();
    const int dur = m_scmGenDuration ? m_scmGenDuration->currentIndex() : 0;
    if (!dir.isEmpty() && (dur == 1 || dur == 2)) {
        const QString cutoff =
            dur == 1 ? QStringLiteral("1 hour ago") : QStringLiteral("midnight");
        QByteArray revOut;
        runGitCapture(dir, {"rev-list", "-1", "--before=" + cutoff, "HEAD"}, &revOut,
                      nullptr);
        const QString rev = QString::fromUtf8(revOut).trimmed();
        if (!rev.isEmpty())
            return {rev};
    }
    QByteArray staged;
    if (!dir.isEmpty())
        runGitCapture(dir, {"diff", "--cached", "--name-only"}, &staged, nullptr);
    if (!staged.trimmed().isEmpty())
        return {QStringLiteral("--cached")};
    return {};
}

// A local, model-free commit-message drafter. It parses the diff to find the
// symbols that changed — types/functions added or removed (keyword-led definitions
// in any language, plus C++ header declarations) and the functions whose bodies
// changed (from git's hunk-header context) — drops generic/internal names, then
// builds the subject from the single most significant symbol, humanizing its
// camelCase into words ("scmHeuristicCommitMessage" -> "scm heuristic commit
// message"). The conventional type comes from the file kinds, the branch name, the
// change shape and a bug-fix keyword scan. Not as good as the AI models, but
// instant, private and free — and far more specific than "update N files"; the
// result lands in the editable message field. `variant` > 0 rotates the symbol
// choice and verb wording so re-clicking Generate offers alternative drafts.
QString MainWindow::scmHeuristicCommitMessage(int variant) const
{
    const QString dir = repoGitDir();
    if (dir.isEmpty())
        return QString();
    const QStringList scope = scmDiffScopeArgs();

    struct Change {
        QChar status;
        QString path;    // new path (for renames)
        QString oldPath; // only set for renames
    };
    QList<Change> changes;
    const QString ns = QString::fromUtf8(gitCaptureStdout(
        dir, QStringList{"diff"} + scope + QStringList{"--name-status"}));
    for (const QString &row : ns.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = row.split(QLatin1Char('\t'), Qt::SkipEmptyParts);
        if (f.isEmpty())
            continue;
        const QChar st = f.first().isEmpty() ? QChar('M') : f.first().at(0);
        Change c{st, f.last(), QString()};
        if ((st == QLatin1Char('R') || st == QLatin1Char('C')) && f.size() >= 3)
            c.oldPath = f.at(1);
        changes.append(c);
    }
    // Untracked files only surface under the plain working-tree scope.
    if (scope.isEmpty()) {
        const QString u = QString::fromUtf8(gitCaptureStdout(
            dir, {"ls-files", "--others", "--exclude-standard"}));
        for (const QString &p : u.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            changes.append({QChar('A'), p.trimmed(), QString()});
    }
    if (changes.isEmpty())
        return QString();

    // Per-path category, used to pick a conventional type when the whole change
    // set is one flavour.
    auto category = [](const QString &path) -> QString {
        const QString p = path.toLower();
        const QString base = p.section(QLatin1Char('/'), -1);
        if (p.contains(QLatin1String(".github/workflows/")))
            return QStringLiteral("ci");
        if (base.endsWith(QLatin1String(".md")) || base.endsWith(QLatin1String(".rst"))
            || p.contains(QLatin1String("/docs/")) || p.startsWith(QLatin1String("docs/"))
            || base.startsWith(QLatin1String("readme"))
            || base.startsWith(QLatin1String("changelog")))
            return QStringLiteral("docs");
        if (p.contains(QLatin1String("/tests/")) || p.contains(QLatin1String("/test/"))
            || base.startsWith(QLatin1String("test_"))
            || base.contains(QLatin1String("_test."))
            || base.contains(QLatin1String(".test."))
            || base.contains(QLatin1String("spec.")))
            return QStringLiteral("test");
        if (base == QLatin1String("cmakelists.txt") || base.endsWith(QLatin1String(".cmake"))
            || base == QLatin1String("makefile") || base == QLatin1String("dockerfile")
            || base == QLatin1String("package.json") || base.endsWith(QLatin1String(".lock"))
            || base.endsWith(QLatin1String(".yml")) || base.endsWith(QLatin1String(".yaml"))
            || base.endsWith(QLatin1String(".toml")) || base == QLatin1String(".gitignore"))
            return QStringLiteral("build");
        return QStringLiteral("code");
    };

    int added = 0, removed = 0, renamed = 0;
    QString commonCat;
    bool uniformCat = true;
    QStringList commonDir;
    bool firstDir = true;
    for (const Change &c : changes) {
        if (c.status == QLatin1Char('A')) ++added;
        else if (c.status == QLatin1Char('D')) ++removed;
        else if (c.status == QLatin1Char('R')) ++renamed;

        const QString cat = category(c.path);
        if (commonCat.isEmpty()) commonCat = cat;
        else if (commonCat != cat) uniformCat = false;

        const int slash = c.path.lastIndexOf(QLatin1Char('/'));
        const QStringList parts =
            (slash >= 0 ? c.path.left(slash) : QString()).split(QLatin1Char('/'),
                                                                Qt::SkipEmptyParts);
        if (firstDir) { commonDir = parts; firstDir = false; }
        else {
            int k = 0;
            while (k < commonDir.size() && k < parts.size() && commonDir[k] == parts[k])
                ++k;
            commonDir = commonDir.mid(0, k);
        }
    }

    // ---- content analysis: walk the patch and classify which functions/types were
    // added, removed, or had their bodies changed. Naming the actual symbols is what
    // makes the message describe the change instead of just counting files. ----
    QHash<QString, int> addedType, addedFn, removedType, removedFn, touchedFn;
    QString addedText; // added code lines, for the bug-fix keyword scan
    {
        const QString patch = QString::fromUtf8(gitCaptureStdout(
            dir, QStringList{"diff"} + scope + QStringList{"--unified=0"}));
        // Enclosing function from a hunk-header context: the identifier just before
        // the first '(' (after any Class:: qualifier).
        auto funcFromContext = [](const QString &ctx) -> QString {
            const int paren = ctx.indexOf(QLatin1Char('('));
            if (paren < 0)
                return QString();
            static const QRegularExpression tail(
                QStringLiteral("([A-Za-z_][A-Za-z0-9_]*)\\s*$"));
            const auto m = tail.match(ctx.left(paren));
            if (!m.hasMatch())
                return QString();
            static const QSet<QString> kw = {
                QStringLiteral("if"),    QStringLiteral("for"),
                QStringLiteral("while"), QStringLiteral("switch"),
                QStringLiteral("catch"), QStringLiteral("return"),
                QStringLiteral("sizeof")};
            return kw.contains(m.captured(1)) ? QString() : m.captured(1);
        };
        // Definitions introduced/removed on one line. Keyword-led forms work for any
        // language; in a header a "<type> name(args);" declaration counts too.
        // All anchored at the start of the (comment-stripped) line: a real
        // definition begins the statement, whereas prose mentioning "function" or
        // "struct" inside a comment does not.
        static const QRegularExpression typeDef(QStringLiteral(
            "^(?:typedef\\s+)?(?:class|struct|enum)\\s+(?:class\\s+)?([A-Za-z_]\\w*)"));
        static const QRegularExpression langFn(QStringLiteral(
            "^(?:export\\s+|public\\s+|private\\s+|pub\\s+|async\\s+|static\\s+)*"
            "(?:def|func|fn|function|sub)\\s+([A-Za-z_]\\w*)"));
        static const QRegularExpression headerDecl(QStringLiteral(
            "^[A-Za-z_][\\w:<>,*&\\s]*\\s[*&]*([A-Za-z_]\\w*)\\s*\\([^;{]*\\)\\s*"
            "(?:const|override|noexcept|final|=\\s*\\w+|\\s)*;\\s*$"));
        auto scanDefs = [&](const QString &content, bool header,
                            QHash<QString, int> &types, QHash<QString, int> &fns) {
            QString t = content.trimmed();
            // Skip comment and preprocessor lines so their prose can't pose as code.
            if (t.isEmpty() || t.startsWith(QLatin1String("//"))
                || t.startsWith(QLatin1Char('*')) || t.startsWith(QLatin1String("/*"))
                || t.startsWith(QLatin1Char('#')))
                return;
            const int cmt = t.indexOf(QLatin1String("//"));
            if (cmt > 0)
                t = t.left(cmt).trimmed();
            const auto mt = typeDef.match(t);
            if (mt.hasMatch()) { ++types[mt.captured(1)]; return; }
            const auto ml = langFn.match(t);
            if (ml.hasMatch()) { ++fns[ml.captured(1)]; return; }
            if (header) {
                const auto mh = headerDecl.match(t);
                if (mh.hasMatch())
                    ++fns[mh.captured(1)];
            }
        };

        QString file;
        bool header = false;
        int seen = 0;
        const QStringList lines = patch.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (++seen > 40000) // bound the work on a very large diff
                break;
            if (line.startsWith(QLatin1String("+++ "))) {
                file = line.mid(4).trimmed();
                if (file.startsWith(QLatin1String("b/")))
                    file = file.mid(2);
                const QString lower = file.toLower();
                header = lower.endsWith(QLatin1String(".h"))
                         || lower.endsWith(QLatin1String(".hpp"))
                         || lower.endsWith(QLatin1String(".hh"))
                         || lower.endsWith(QLatin1String(".hxx"));
                continue;
            }
            if (line.startsWith(QLatin1String("--- ")))
                continue;
            if (line.startsWith(QLatin1String("@@"))) {
                const int second = line.indexOf(QLatin1String("@@"), 2);
                if (second >= 0) {
                    const QString fn = funcFromContext(line.mid(second + 2));
                    if (!fn.isEmpty())
                        ++touchedFn[fn];
                }
                continue;
            }
            if (line.startsWith(QLatin1Char('+'))) {
                const QString content = line.mid(1);
                scanDefs(content, header, addedType, addedFn);
                addedText += content.toLower();
                addedText += QLatin1Char('\n');
            } else if (line.startsWith(QLatin1Char('-'))) {
                scanDefs(line.mid(1), header, removedType, removedFn);
            }
        }
    }

    // Split camelCase / snake_case / digit runs into lowercase words — a symbol's
    // own name is the developer's description of what it does, so "deletePullAndBranch"
    // reads back as "delete pull and branch".
    auto humanize = [](const QString &name) -> QString {
        QString out;
        for (int i = 0; i < name.size(); ++i) {
            const QChar c = name.at(i);
            if (c == QLatin1Char('_') || c == QLatin1Char('-')) {
                if (!out.isEmpty() && !out.endsWith(QLatin1Char(' ')))
                    out += QLatin1Char(' ');
                continue;
            }
            const bool prevLower = i > 0 && name.at(i - 1).isLower();
            const bool prevUpper = i > 0 && name.at(i - 1).isUpper();
            const bool nextLower = i + 1 < name.size() && name.at(i + 1).isLower();
            const bool boundary = (c.isUpper() && prevLower)              // fooBar
                                  || (c.isUpper() && prevUpper && nextLower) // HTMLParser
                                  || (c.isDigit() && i > 0 && !name.at(i - 1).isDigit());
            if (boundary && !out.isEmpty() && !out.endsWith(QLatin1Char(' ')))
                out += QLatin1Char(' ');
            out += c.toLower();
        }
        return out.simplified();
    };

    // Generic / internal names that make a poor headline.
    static const QSet<QString> kGeneric = {
        QStringLiteral("change"),  QStringLiteral("data"),    QStringLiteral("item"),
        QStringLiteral("info"),    QStringLiteral("helper"),  QStringLiteral("impl"),
        QStringLiteral("result"),  QStringLiteral("options"), QStringLiteral("option"),
        QStringLiteral("config"),  QStringLiteral("context"), QStringLiteral("entry"),
        QStringLiteral("node"),    QStringLiteral("pair"),    QStringLiteral("tmp"),
        QStringLiteral("temp"),    QStringLiteral("foo"),     QStringLiteral("bar"),
        QStringLiteral("base"),    QStringLiteral("value"),   QStringLiteral("object"),
        QStringLiteral("list"),    QStringLiteral("map"),     QStringLiteral("util"),
        QStringLiteral("utils"),   QStringLiteral("main"),    QStringLiteral("init"),
        QStringLiteral("state"),   QStringLiteral("params"),  QStringLiteral("args"),
        QStringLiteral("type"),    QStringLiteral("types"),   QStringLiteral("handler")};

    // Rank one or more (freq, opposite-side, bonus) buckets together by descriptive
    // merit, dropping the opposite side and generic names. Merging into a single
    // sort (rather than concatenating) lets a well-named function outrank a terse
    // internal struct, while `bonus` still tilts ties toward new types.
    struct Bucket {
        const QHash<QString, int> *freq;
        const QHash<QString, int> *exclude;
        int bonus;
    };
    auto rankMerged = [&](std::initializer_list<Bucket> buckets) -> QStringList {
        struct Cand { QString name; int score; };
        QList<Cand> cs;
        for (const Bucket &b : buckets)
            for (auto i = b.freq->cbegin(); i != b.freq->cend(); ++i) {
                const QString &n = i.key();
                if (b.exclude->contains(n) || kGeneric.contains(n.toLower())
                    || n.size() < 4)
                    continue;
                const int words = humanize(n).count(QLatin1Char(' ')) + 1;
                cs.append({n, b.bonus + qMin(words, 4) + qMin(i.value(), 3)});
            }
        std::sort(cs.begin(), cs.end(), [](const Cand &a, const Cand &b) {
            if (a.score != b.score) return a.score > b.score;
            return a.name < b.name;
        });
        QStringList out;
        for (const Cand &c : cs) out << c.name;
        return out;
    };
    auto fileStems = [&](QChar status) -> QStringList {
        QStringList out;
        for (const Change &c : changes)
            if (c.status == status) {
                QString b = c.path.section(QLatin1Char('/'), -1);
                const int dot = b.lastIndexOf(QLatin1Char('.'));
                if (dot > 0) b = b.left(dot);
                if (!kGeneric.contains(b.toLower()))
                    out << b;
            }
        return out;
    };

    const QHash<QString, int> none;
    // New symbols, ranked together (a small type tilt breaks ties toward classes,
    // but descriptiveness wins — so a one-line helper struct can't beat a
    // well-named function). Fall back to whole new file names.
    QStringList newNames =
        rankMerged({{&addedType, &removedType, 2}, {&addedFn, &removedFn, 1}});
    QStringList goneNames =
        rankMerged({{&removedType, &addedType, 2}, {&removedFn, &addedFn, 1}});
    if (newNames.isEmpty() && added > 0)
        newNames = fileStems(QChar('A'));
    if (goneNames.isEmpty() && removed > 0)
        goneNames = fileStems(QChar('D'));
    // Functions whose bodies changed (not net added or removed).
    QHash<QString, int> changedFreq = touchedFn;
    for (const QString &n : addedFn.keys()) changedFreq.remove(n);
    for (const QString &n : addedType.keys()) changedFreq.remove(n);
    for (const QString &n : removedFn.keys()) changedFreq.remove(n);
    for (const QString &n : removedType.keys()) changedFreq.remove(n);
    const QStringList chgNames = rankMerged({{&changedFreq, &none, 0}});

    // fix-signal: words in the added code that strongly suggest a bug fix.
    static const QRegularExpression fixWords(QStringLiteral(
        "\\b(fix|bug|crash|freeze|hang|leak|deadlock|segfault|overflow|regression|"
        "guard|workaround|race)\\b"));
    const bool fixy = fixWords.match(addedText).hasMatch();

    // ---- conventional type ----
    QString type;
    if (uniformCat && commonCat != QLatin1String("code"))
        type = commonCat;
    if (type.isEmpty()) {
        const QString branch = QString::fromUtf8(gitCaptureStdout(
            dir, {"rev-parse", "--abbrev-ref", "HEAD"})).trimmed().toLower();
        if (branch.contains(QLatin1String("fix")) || branch.contains(QLatin1String("bug"))
            || branch.contains(QLatin1String("hotfix")))
            type = QStringLiteral("fix");
        else if (branch.contains(QLatin1String("feat")))
            type = QStringLiteral("feat");
        else if (branch.contains(QLatin1String("refactor")))
            type = QStringLiteral("refactor");
    }
    if (type.isEmpty()) {
        if (!newNames.isEmpty() && goneNames.isEmpty() && removed == 0 && renamed == 0)
            type = QStringLiteral("feat");
        else if (fixy)
            type = QStringLiteral("fix");
        else if (!chgNames.isEmpty() || !goneNames.isEmpty() || !newNames.isEmpty()
                 || removed > 0 || renamed > 0)
            type = QStringLiteral("refactor");
        else
            type = QStringLiteral("chore");
    }

    // ---- subject: name the headline symbol(s), humanized. A second name is added
    // only when it's a clearly different theme (different first word), so we never
    // emit "scm X and scm Y" or a vague "and N more" tail. ----
    // On a re-click (variant > 0) rotate the pool so a different symbol leads —
    // but only among the strongest few, so cycling never digs down to the noisy
    // low-ranked tail (terse internal structs, etc.).
    auto rotate = [&](QStringList names) -> QStringList {
        if (names.size() > 4)
            names = names.mid(0, 4);
        if (variant > 0 && names.size() > 1) {
            const int off = variant % names.size();
            names = names.mid(off) + names.mid(0, off);
        }
        return names;
    };
    auto headline = [&](const QStringList &all) -> QStringList {
        const QStringList names = rotate(all);
        QStringList out;
        for (const QString &n : names) {
            if (out.isEmpty()) { out << n; continue; }
            if (humanize(n).section(QLatin1Char(' '), 0, 0)
                != humanize(out.first()).section(QLatin1Char(' '), 0, 0)) {
                out << n;
                break;
            }
        }
        return out;
    };

    QString subject;
    QString verb;     // "" means let the conventional type carry the verb
    QStringList pick; // the chosen symbol name(s)
    const bool renameOne =
        changes.size() == 1 && renamed == 1 && !changes.first().oldPath.isEmpty();
    if (renameOne) {
        subject = QStringLiteral("rename %1 to %2")
                      .arg(changes.first().oldPath.section(QLatin1Char('/'), -1),
                           changes.first().path.section(QLatin1Char('/'), -1));
    } else if (!newNames.isEmpty()) {
        verb = QStringLiteral("add");
        pick = headline(newNames);
    } else if (!goneNames.isEmpty()) {
        verb = QStringLiteral("remove");
        pick = headline(goneNames);
    } else if (!chgNames.isEmpty()) {
        pick = headline(chgNames);
    }
    // Vary the wording on a re-click: a synonym for add/remove, or a light verb in
    // front of a body-change subject (which normally lets the type carry the verb).
    if (variant > 0 && !renameOne) {
        if (verb == QLatin1String("add")) {
            static const QStringList syn = {QStringLiteral("add"),
                QStringLiteral("introduce"), QStringLiteral("implement"),
                QStringLiteral("create"), QStringLiteral("wire up")};
            verb = syn.at(variant % syn.size());
        } else if (verb == QLatin1String("remove")) {
            static const QStringList syn = {QStringLiteral("remove"),
                QStringLiteral("drop"), QStringLiteral("delete"),
                QStringLiteral("strip out")};
            verb = syn.at(variant % syn.size());
        } else if (verb.isEmpty() && !pick.isEmpty()) {
            static const QStringList syn = {QString(), QStringLiteral("tweak"),
                QStringLiteral("rework"), QStringLiteral("refine"),
                QStringLiteral("revise"), QStringLiteral("adjust")};
            verb = syn.at(variant % syn.size());
        }
    }
    auto compose = [&](int maxNames) -> QString {
        QStringList words;
        for (const QString &n : pick.mid(0, maxNames))
            words << humanize(n);
        QString ph;
        if (words.size() >= 2) {
            // Use a comma when a name already contains "and" to avoid "X and Y and Z".
            const QString sep = (words[0].contains(QStringLiteral(" and "))
                                 || words[1].contains(QStringLiteral(" and ")))
                                    ? QStringLiteral(", ")
                                    : QStringLiteral(" and ");
            ph = words[0] + sep + words[1];
        } else {
            ph = words.value(0);
        }
        if (ph.isEmpty())
            return QString();
        return verb.isEmpty() ? ph : verb + QLatin1Char(' ') + ph;
    };
    if (subject.isEmpty() && !pick.isEmpty())
        subject = compose(2);
    if (subject.isEmpty()) {
        // Nothing nameable (e.g. data/config only): fall back to a file summary.
        const QString fv = added == changes.size()    ? QStringLiteral("add")
                           : removed == changes.size() ? QStringLiteral("remove")
                           : renamed == changes.size() ? QStringLiteral("rename")
                                                       : QStringLiteral("update");
        subject = changes.size() == 1
                      ? QStringLiteral("%1 %2").arg(
                            fv, changes.first().path.section(QLatin1Char('/'), -1))
                      : QStringLiteral("%1 %2 files").arg(fv).arg(changes.size());
    }

    // Optional scope: the common directory, unless it's a generic container.
    static const QSet<QString> kGenericScope = {
        QStringLiteral("src"),     QStringLiteral("lib"),  QStringLiteral("source"),
        QStringLiteral("sources"), QStringLiteral("app"),  QStringLiteral("code"),
        QStringLiteral("include"), QStringLiteral("dist")};
    QString scopeName = commonDir.isEmpty() ? QString() : commonDir.last();
    if (kGenericScope.contains(scopeName.toLower()))
        scopeName.clear();

    auto assemble = [&](const QString &subj) {
        QString m = type;
        if (!scopeName.isEmpty() && scopeName.size() <= 20)
            m += QStringLiteral("(%1)").arg(scopeName);
        return m + QStringLiteral(": ") + subj;
    };
    QString msg = assemble(subject);
    // If naming two themes overran, fall back to just the first.
    if (msg.size() > 72 && pick.size() >= 2)
        msg = assemble(compose(1));
    return msg;
}

void MainWindow::autoFillScmMessage()
{
    // Only the free, local, instant drafter runs on its own — never the paid AI
    // models, and never over a message the user has started typing.
    if (m_scmGenerating || !m_scmMessage || !m_scmGenModel
        || m_scmGenModel->currentData().toInt() != -1
        || !m_scmMessage->toPlainText().trimmed().isEmpty())
        return;
    const QString msg = scmHeuristicCommitMessage(0); // 0 = the deterministic best
    if (msg.isEmpty())
        return;
    m_scmHeuristicVariant = 0; // a fresh auto-fill restarts the "vary on click" cycle
    m_scmMessage->setPlainText(msg);
    if (m_scmGenStatus)
        m_scmGenStatus->setText(QString::fromUtf8(
            "%1 chars \xc2\xb7 on-device, auto \xc2\xb7 \xE2\x86\xBB click to vary")
                                    .arg(msg.size()));
}

void MainWindow::generateScmMessage()
{
    if (m_scmGenerating)
        return;
    const QString diff = scmContextDiff();
    if (diff.trimmed().isEmpty()) {
        if (m_scmGenStatus)
            m_scmGenStatus->setText("No changes to describe.");
        return;
    }
    const int mi = m_scmGenModel ? m_scmGenModel->currentData().toInt() : 0;
    if (mi < 0) { // On-device (no AI): derive the message locally from the diff.
        // Each explicit click advances the variant so re-clicking "refreshes" to an
        // alternative phrasing/symbol choice (auto-fill always uses variant 0).
        const QString msg = scmHeuristicCommitMessage(++m_scmHeuristicVariant);
        if (msg.isEmpty()) {
            m_scmHeuristicVariant = 0;
            if (m_scmGenStatus)
                m_scmGenStatus->setText("No changes to describe.");
            return;
        }
        if (m_scmMessage)
            m_scmMessage->setPlainText(msg);
        const int n =
            m_scmMessage ? m_scmMessage->toPlainText().size() : msg.size();
        if (m_scmGenStatus)
            m_scmGenStatus->setText(
                QString::fromUtf8("%1 chars \xc2\xb7 on-device, no cost \xc2\xb7 \xE2\x86\xBB click to vary").arg(n));
        return;
    }
    if (mi >= kScmAiModelCount)
        return;
    const ScmAiModel model = kScmAiModels[mi];
    const bool isCommit = !(m_scmGenKind && m_scmGenKind->currentIndex() == 1);
    const int outTok = isCommit ? 120 : 280;

    const QString system =
        isCommit ? QStringLiteral("You write concise, conventional git commit "
                                  "messages.")
                 : QStringLiteral("You write short, engaging X (Twitter) posts "
                                  "for developers.");
    const QString task =
        isCommit
            ? QStringLiteral(
                  "Write a single-line git commit message for the following "
                  "changes: a short imperative subject (max ~70 chars), no body. "
                  "Output ONLY that one line, with no backticks or commentary."
                  "\n\nDiff:\n%1")
                  .arg(diff)
            : QStringLiteral(
                  "Write a single engaging X (Twitter) post, at most 280 "
                  "characters, announcing these code changes to developer "
                  "followers. At most two relevant hashtags. Output ONLY the post "
                  "text.\n\nChanges:\n%1")
                  .arg(diff);

    const bool claude =
        QString::fromLatin1(model.provider) == QLatin1String("claude");
    const QString apiKey =
        claude ? QSettings().value(kClaudeApiKeySetting).toString().trimmed()
               : QSettings().value(kCodexApiKeySetting).toString().trimmed();
    if (apiKey.isEmpty()) {
        if (m_scmGenStatus)
            m_scmGenStatus->setText(claude ? "Add a Claude API key in Settings."
                                           : "Add an OpenAI API key in Settings.");
        return;
    }

    QNetworkReply *reply = nullptr;
    if (claude) {
        QJsonObject payload;
        payload.insert("model", QString::fromLatin1(model.id));
        payload.insert("max_tokens", outTok);
        QJsonArray messages;
        QJsonObject um;
        um.insert("role", "user");
        um.insert("content", system + "\n\n" + task);
        messages.append(um);
        payload.insert("messages", messages);
        QNetworkRequest req(QUrl("https://api.anthropic.com/v1/messages"));
        req.setRawHeader("x-api-key", apiKey.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    } else {
        QJsonObject payload;
        payload.insert("model", QString::fromLatin1(model.id));
        payload.insert("instructions", system);
        payload.insert("input", task);
        payload.insert("max_output_tokens", outTok);
        QNetworkRequest req =
            openAiRequest(QUrl("https://api.openai.com/v1/responses"), apiKey);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_networkAccess->post(
            req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }

    m_scmGenerating = true;
    if (m_scmGenerateButton) {
        m_scmGenerateButton->setEnabled(false);
        m_scmGenerateButton->setText(QString::fromUtf8("Generating\xE2\x80\xA6"));
    }
    if (m_scmGenStatus)
        m_scmGenStatus->setText(QString::fromUtf8("Asking %1\xE2\x80\xA6")
                                    .arg(QString::fromLatin1(model.label)));

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, claude, model, isCommit] {
                const QByteArray body = reply->readAll();
                reply->deleteLater();
                m_scmGenerating = false;
                if (m_scmGenerateButton) {
                    m_scmGenerateButton->setEnabled(true);
                    m_scmGenerateButton->setText(
                        QString::fromUtf8("\xE2\x9C\xA8 Generate"));
                }
                if (reply->error() != QNetworkReply::NoError) {
                    if (m_scmGenStatus)
                        m_scmGenStatus->setText("Failed: " +
                                                apiErrorSummary(reply, body));
                    return;
                }
                const QJsonObject obj = QJsonDocument::fromJson(body).object();
                QString text;
                double cost = 0.0;
                qint64 inTok = 0, outTokActual = 0;
                if (claude) {
                    for (const QJsonValue &v : obj.value("content").toArray()) {
                        const QJsonObject o = v.toObject();
                        if (o.value("type").toString() == QLatin1String("text"))
                            text += o.value("text").toString();
                    }
                    const QJsonObject usage = obj.value("usage").toObject();
                    inTok = usage.value("input_tokens").toInt();
                    outTokActual = usage.value("output_tokens").toInt();
                    cost = inTok / 1e6 * model.inPerM +
                           outTokActual / 1e6 * model.outPerM;
                } else {
                    text = openAiResponseText(obj);
                    cost = openAiAskCostUsd(obj, &inTok, &outTokActual);
                }
                text = text.trimmed();
                if (text.isEmpty()) {
                    if (m_scmGenStatus)
                        m_scmGenStatus->setText("Empty response.");
                    return;
                }
                if (m_scmMessage) {
                    if (isCommit) {
                        QString oneLine = text;
                        oneLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
                        m_scmMessage->setPlainText(oneLine.simplified());
                    } else {
                        m_scmMessage->setPlainText(text);
                    }
                }
                // Char count first (what the user asked to see), then the cost.
                const int n = m_scmMessage
                                  ? m_scmMessage->toPlainText().size()
                                           : text.size();
                QString line = isCommit ? QStringLiteral("%1 chars").arg(n)
                                        : QStringLiteral("%1/280").arg(n);
                line += QStringLiteral(" \xC2\xB7 $%1 \xC2\xB7 %2 in / %3 out")
                            .arg(QString::number(cost, 'f', 4))
                            .arg(inTok)
                            .arg(outTokActual);
                // An X post also goes to the clipboard so it's one paste from being
                // posted; keep the original (possibly multi-line) text there.
                if (!isCommit) {
                    QApplication::clipboard()->setText(text);
                    line += QStringLiteral(", copied");
                }
                if (m_scmGenStatus)
                    m_scmGenStatus->setText(line);
            });
}

QWidget *MainWindow::buildRepoSecurityTab()
{
    auto *page = new QWidget;
    page->setObjectName("mainContent");

    auto *scroll = new QScrollArea;
    scroll->setObjectName("mainContent");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    content->setObjectName("insightsPage");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    auto *heading = new QLabel("Security");
    heading->setObjectName("securityQualityTitle");
    heading->setProperty("class", "channelTitle");
    heading->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;"));
    auto *subtitle = new QLabel(
        "Local security evidence from this node. External advisory matching is "
        "not part of the MVP; quality metrics live on the Quality tab.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    m_securityRefreshButton = new QPushButton("Refresh");
    m_securityRefreshButton->setObjectName("ghostButton");
    m_securityRefreshButton->setProperty("buttonSize", "sm");
    m_securityRefreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_securityRefreshButton, "sync", 16);
    connect(m_securityRefreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshRepoSecurity);

    auto *headingCol = new QVBoxLayout;
    headingCol->setContentsMargins(0, 0, 0, 0);
    headingCol->setSpacing(3);
    headingCol->addWidget(heading);
    headingCol->addWidget(subtitle);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    headerRow->addLayout(headingCol, 1);
    headerRow->addWidget(m_securityRefreshButton, 0, Qt::AlignTop);
    layout->addLayout(headerRow);

    m_securitySummary = new QLabel;
    m_securitySummary->setObjectName("insightsCard");
    m_securitySummary->setTextFormat(Qt::RichText);
    m_securitySummary->setWordWrap(true);
    m_securitySummary->setMinimumHeight(92);
    layout->addWidget(m_securitySummary);

    auto *signalsLabel = new QLabel("SIGNALS");
    signalsLabel->setObjectName("sectionLabel");
    layout->addWidget(signalsLabel);

    m_securitySignalsPanel = new QWidget;
    m_securitySignalsGrid = new QGridLayout(m_securitySignalsPanel);
    m_securitySignalsGrid->setContentsMargins(0, 0, 0, 0);
    m_securitySignalsGrid->setSpacing(10);
    layout->addWidget(m_securitySignalsPanel);

    auto *findingsLabel = new QLabel("FINDINGS");
    findingsLabel->setObjectName("sectionLabel");
    layout->addWidget(findingsLabel);

    m_securityFindingsTable = new QTableWidget(0, 4);
    installColumnHeaderMenu(m_securityFindingsTable); // 3-dots per-column menu (issue #318)
    m_securityFindingsTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_securityFindingsTable);
    m_securityFindingsTable->setHorizontalHeaderLabels(
        {"Severity", "Category", "Finding", "Action"});
    m_securityFindingsTable->verticalHeader()->setVisible(false);
    m_securityFindingsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_securityFindingsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_securityFindingsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_securityFindingsTable->setShowGrid(false);
    m_securityFindingsTable->setWordWrap(false);
    QHeaderView *header = m_securityFindingsTable->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_securityFindingsTable);
    m_securityFindingsTable->setMinimumHeight(220);
    layout->addWidget(m_securityFindingsTable);
    layout->addStretch();
    connect(m_securityFindingsTable, &QTableWidget::cellClicked,
            this, [this](int row, int /*col*/) {
        auto *item = m_securityFindingsTable->item(row, 2);
        if (!item)
            return;
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        // Switch to the Code tab (index 0) so the highlighted line is visible;
        // openRepoFileAtLine alone only touches the (currently hidden) files panel.
        if (m_repoDetailTabs && m_repoDetailTabs->button(0))
            m_repoDetailTabs->button(0)->click();
        openRepoFileAtLine(path, item->data(Qt::UserRole + 1).toInt());
    });

    scroll->setWidget(content);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);
    refreshRepoSecurity();
    return page;
}

// Severity -> badge/text colour for the Security and Quality tabs. Mirrors the
// palette used elsewhere (green pass, blue info, amber warning, orange/red severe).
static QString repoSecuritySeverityColor(RepoSecuritySeverity severity)
{
    switch (severity) {
    case RepoSecuritySeverity::Pass:
        return QStringLiteral("#3fb950");
    case RepoSecuritySeverity::Info:
        return QStringLiteral("#58a6ff");
    case RepoSecuritySeverity::Warning:
        return QStringLiteral("#d29922");
    case RepoSecuritySeverity::High:
        return QStringLiteral("#f0883e");
    case RepoSecuritySeverity::Critical:
        return QStringLiteral("#f85149");
    }
    return QStringLiteral("#8b949e");
}

// Render one security signal as an "insightsCard" HTML body: severity-tinted
// title, the summary, and (when present) the longer detail.
static QString repoSecuritySignalHtml(const RepoSecuritySignal &signal)
{
    const QString color = repoSecuritySeverityColor(signal.severity);
    QString html = QStringLiteral(
                       "<div style='font-weight:700; font-size:13px; color:%1'>%2</div>"
                       "<div style='color:#8b949e; font-size:11px; font-weight:600; "
                       "text-transform:uppercase; margin-top:2px'>%3</div>")
                       .arg(color, signal.title.toHtmlEscaped(),
                            RepoSecurity::severityText(signal.severity).toHtmlEscaped());
    if (!signal.summary.isEmpty())
        html += QStringLiteral(
                    "<div style='color:#c9d1d9; font-size:12px; margin-top:6px'>%1</div>")
                    .arg(signal.summary.toHtmlEscaped());
    if (!signal.detail.isEmpty())
        html += QStringLiteral(
                    "<div style='color:#8b949e; font-size:11px; margin-top:4px'>%1</div>")
                    .arg(signal.detail.toHtmlEscaped());
    // Itemised entries (e.g. dependency manifests) render as a list where each
    // path is a "check" link; the card's linkActivated handler opens the file.
    for (const QString &item : signal.items) {
        const QString href = QString::fromUtf8(QUrl::toPercentEncoding(item));
        html += QStringLiteral(
                    "<div style='color:#c9d1d9; font-size:12px; margin-top:4px'>"
                    "&#8226;&nbsp;<a style='color:#58a6ff; text-decoration:none' "
                    "href='manifest:%1'>%2</a></div>")
                    .arg(href, item.toHtmlEscaped());
    }
    return html;
}

// Fill a Security/Quality findings table: one row per finding (or a single
// "Pass" row when there are none). Column 2 carries the path/line payload the
// tables' cellClicked handlers use to open the file.
static void fillRepoFindingsTable(QTableWidget *table,
                                  const QList<RepoSecurityFinding> &findings,
                                  const QString &emptyText)
{
    if (findings.isEmpty()) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto *severity = new QTableWidgetItem("Pass");
        severity->setForeground(QColor(repoSecuritySeverityColor(
            RepoSecuritySeverity::Pass)));
        table->setItem(row, 0, severity);
        table->setItem(row, 1, new QTableWidgetItem("Local scan"));
        table->setItem(row, 2, new QTableWidgetItem(emptyText));
        table->setItem(row, 3, new QTableWidgetItem("-"));
        return;
    }
    for (const RepoSecurityFinding &finding : findings) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto *severity =
            new QTableWidgetItem(RepoSecurity::severityText(finding.severity));
        severity->setForeground(QColor(repoSecuritySeverityColor(finding.severity)));
        auto *category = new QTableWidgetItem(finding.category);
        const QString location =
            finding.path.isEmpty()
                ? QString()
                : QStringLiteral(" (%1%2)")
                      .arg(finding.path,
                           finding.line > 0
                               ? QStringLiteral(":%1").arg(finding.line)
                               : QString());
        auto *detail =
            new QTableWidgetItem(finding.title + QStringLiteral(": ") +
                                 finding.detail + location);
        if (!finding.path.isEmpty()) {
            detail->setData(Qt::UserRole, finding.path);
            detail->setData(Qt::UserRole + 1, finding.line);
            detail->setToolTip(
                QStringLiteral("Click to open %1:%2")
                    .arg(finding.path)
                    .arg(finding.line > 0 ? QString::number(finding.line)
                                          : QStringLiteral("?")));
        }
        auto *action = new QTableWidgetItem(finding.recommendedAction);
        for (QTableWidgetItem *item : {severity, category, action})
            item->setToolTip(item->text());
        table->setItem(row, 0, severity);
        table->setItem(row, 1, category);
        table->setItem(row, 2, detail);
        table->setItem(row, 3, action);
    }
}

// Renders the dependency-scan signal as a rich table widget: one row per
// manifest with dependency-count, outdated-count, vulnerable-count, and a
// per-row "Run scan" button.  Placed full-width (spanning all 4 grid columns)
// below the other signal cards.
static QWidget *buildDepScanCard(const RepoSecuritySignal &signal,
                                  const QList<RepoSecurityFinding> &findings,
                                  const QHash<QString, int> &dependencyCounts,
                                  qint64 generatedAtMs,
                                  const QString &localBase,
                                  QObject *context,
                                  bool scanning,
                                  const QString &scanningPath,
                                  std::function<void(const QString &)> runScan)
{
    auto *card = new QFrame;
    card->setObjectName("insightsCard");
    auto *vlay = new QVBoxLayout(card);
    vlay->setContentsMargins(12, 12, 12, 12);
    vlay->setSpacing(6);

    // Title + severity + last-scanned time in one row
    const QString color = repoSecuritySeverityColor(signal.severity);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    auto *titleLbl = new QLabel(
        QStringLiteral("<span style='font-weight:700;font-size:13px;color:%1'>%2</span>"
                       "&nbsp;<span style='color:#8b949e;font-size:11px;font-weight:600;"
                       "text-transform:uppercase'>%3</span>")
            .arg(color, signal.title.toHtmlEscaped(),
                 RepoSecurity::severityText(signal.severity).toHtmlEscaped()));
    titleLbl->setTextFormat(Qt::RichText);
    titleRow->addWidget(titleLbl, 1);
    if (scanning) {
        auto *scanningLbl = new QLabel(
            QStringLiteral("<span style='color:#388bfd;font-size:11px;font-weight:600'>"
                           "Scanning&#8230;</span>"));
        scanningLbl->setTextFormat(Qt::RichText);
        titleRow->addWidget(scanningLbl, 0);
    } else if (generatedAtMs > 0) {
        const QString ts = QDateTime::fromMSecsSinceEpoch(generatedAtMs)
                               .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        auto *timeLbl = new QLabel(
            QStringLiteral("<span style='color:#8b949e;font-size:11px'>Last scanned: %1</span>")
                .arg(ts));
        timeLbl->setTextFormat(Qt::RichText);
        titleRow->addWidget(timeLbl, 0);
    }
    vlay->addLayout(titleRow);

    if (scanning) {
        // Indeterminate (range 0,0) progress bar: Qt animates a marquee chunk on
        // its own timer, giving a genuine moving "scan in progress" indicator
        // rather than a static icon (the scan itself runs off the GUI thread via
        // MainWindow::runRepoDependencyScan, so this can actually animate).
        auto *bar = new QProgressBar;
        bar->setRange(0, 0);
        bar->setTextVisible(false);
        bar->setFixedHeight(4);
        bar->setStyleSheet(
            QStringLiteral("QProgressBar{border:none;border-radius:2px;"
                           "background-color:#30363d;}"
                           "QProgressBar::chunk{border-radius:2px;"
                           "background-color:#388bfd;}"));
        vlay->addWidget(bar);
    }

    if (!signal.summary.isEmpty()) {
        auto *summaryLbl = new QLabel(
            QStringLiteral("<span style='color:#c9d1d9;font-size:12px'>%1</span>")
                .arg(signal.summary.toHtmlEscaped()));
        summaryLbl->setTextFormat(Qt::RichText);
        vlay->addWidget(summaryLbl);
    }

    if (signal.items.isEmpty()) {
        if (!signal.detail.isEmpty()) {
            auto *detailLbl = new QLabel(
                QStringLiteral("<span style='color:#8b949e;font-size:11px'>%1</span>")
                    .arg(signal.detail.toHtmlEscaped()));
            detailLbl->setTextFormat(Qt::RichText);
            vlay->addWidget(detailLbl);
        }
        return card;
    }

    // Count outdated (loose-specifier) alerts per manifest from findings
    QHash<QString, int> outdatedByPath;
    for (const RepoSecurityFinding &f : findings) {
        if (f.category == QLatin1String("Dependency"))
            outdatedByPath[f.path]++;
    }

    // Manifest table: header row + one row per manifest
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 8, 0, 0);
    grid->setSpacing(4);
    grid->setColumnStretch(0, 1);

    auto makeHdr = [](const QString &text) {
        auto *lbl = new QLabel(
            QStringLiteral("<span style='color:#8b949e;font-size:11px;font-weight:600'>"
                           "%1</span>")
                .arg(text.toHtmlEscaped()));
        lbl->setTextFormat(Qt::RichText);
        return lbl;
    };
    grid->addWidget(makeHdr(QStringLiteral("Manifest")), 0, 0);
    grid->addWidget(makeHdr(QStringLiteral("Dependencies")), 0, 1, Qt::AlignRight);
    grid->addWidget(makeHdr(QStringLiteral("Outdated")), 0, 2, Qt::AlignRight);
    grid->addWidget(makeHdr(QStringLiteral("Vulnerable")), 0, 3, Qt::AlignRight);
    // column 4 reserved for buttons (no header needed)

    int row = 1;
    for (const QString &manifest : signal.items) {
        const QString href = QString::fromUtf8(QUrl::toPercentEncoding(manifest));
        auto *pathLbl = new QLabel(
            QStringLiteral("<a style='color:#58a6ff;text-decoration:none' "
                           "href='manifest:%1'>%2</a>")
                .arg(href, manifest.toHtmlEscaped()));
        pathLbl->setTextFormat(Qt::RichText);
        pathLbl->setTextInteractionFlags(Qt::TextBrowserInteraction);
        if (!localBase.trimmed().isEmpty()) {
            const QString abs = QDir(localBase).filePath(manifest);
            QObject::connect(pathLbl, &QLabel::linkActivated, context,
                             [abs](const QString &) {
                                 if (QFileInfo::exists(abs))
                                     QDesktopServices::openUrl(
                                         QUrl::fromLocalFile(abs));
                             });
        }

        const int depCount = dependencyCounts.value(manifest, 0);
        const QString depHtml =
            QStringLiteral("<span style='color:#c9d1d9;font-size:12px'>%1</span>")
                .arg(depCount);
        auto *depLbl = new QLabel(depHtml);
        depLbl->setTextFormat(Qt::RichText);
        depLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        const int outdated = outdatedByPath.value(manifest, 0);
        const QString outdatedHtml =
            outdated > 0
                ? QStringLiteral("<span style='color:#d29922;font-size:12px'>%1</span>")
                      .arg(outdated)
                : QStringLiteral("<span style='color:#3fb950;font-size:12px'>0</span>");
        auto *outdatedLbl = new QLabel(outdatedHtml);
        outdatedLbl->setTextFormat(Qt::RichText);
        outdatedLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // Vulnerable count: not yet implemented (requires external advisory DB)
        auto *vulnLbl = new QLabel(
            QStringLiteral("<span style='color:#8b949e;font-size:12px'>&ndash;</span>"));
        vulnLbl->setTextFormat(Qt::RichText);
        vulnLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        const bool isScanning = scanning && (scanningPath == manifest);
        auto *scanBtn = new QPushButton(isScanning ? QStringLiteral("Scanning\xE2\x80\xA6")
                                                   : QStringLiteral("Run scan"));
        scanBtn->setObjectName("ghostButton");
        scanBtn->setProperty("buttonSize", "sm");
        scanBtn->setEnabled(!scanning);
        scanBtn->setCursor(scanning ? Qt::BusyCursor : Qt::PointingHandCursor);
        QObject::connect(scanBtn, &QPushButton::clicked, context,
                         [runScan, manifest]() { runScan(manifest); });

        grid->addWidget(pathLbl, row, 0);
        grid->addWidget(depLbl, row, 1, Qt::AlignRight);
        grid->addWidget(outdatedLbl, row, 2, Qt::AlignRight);
        grid->addWidget(vulnLbl, row, 3, Qt::AlignRight);
        grid->addWidget(scanBtn, row, 4, Qt::AlignRight);
        ++row;
    }

    vlay->addLayout(grid);
    return card;
}

RepoSecurityInput MainWindow::buildRepoSecurityInput(const RepositoryRecord &selected,
                                                     const RepositoryRecord &writable) const
{
    RepoSecurityInput input;
    input.owner = selected.owner;
    input.name = selected.name;
    input.localPath = writable.localPath;
    input.mirrorPath = writable.mirrorPath.isEmpty() ? selected.mirrorPath
                                                     : writable.mirrorPath;
    input.publishToNetwork = selected.publishToNetwork;
    input.isPrivate = selected.isPrivate;
    input.previewOnly = selected.previewOnly;
    input.actionsEnabled = selected.actionsEnabled;
    input.integrityWarning = m_repoPinMismatch;
    input.issues =
        IssueStore(writable.localPath, input.mirrorPath, &m_profileIdentity, m_userName)
            .loadAll();
    input.workflows = availableWorkflowsForRepo(writable);
    for (const ActionRun &run : std::as_const(m_actionRuns))
        if (run.owner == selected.owner && run.name == selected.name)
            input.actionRuns.append(run);
    return input;
}

void MainWindow::applyRepoSecuritySnapshot(const RepoSecuritySnapshot &snapshot,
                                           const QString &localBase)
{
    m_lastRepoSecuritySnapshot = snapshot;

    while (QLayoutItem *item = m_securitySignalsGrid->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    TableRepaintGuard repaintGuard(m_securityFindingsTable);
    m_securityFindingsTable->setSortingEnabled(false);
    m_securityFindingsTable->setRowCount(0);

    const RepoSecuritySeverity highest = RepoSecurity::highestSeverity(snapshot);
    int warnings = 0;
    int severe = 0;
    for (const RepoSecuritySignal &signal : snapshot.signalList) {
        if (signal.severity == RepoSecuritySeverity::Warning)
            ++warnings;
        else if (signal.severity == RepoSecuritySeverity::High ||
                 signal.severity == RepoSecuritySeverity::Critical)
            ++severe;
    }
    for (const RepoSecurityFinding &finding : snapshot.findings) {
        if (finding.severity == RepoSecuritySeverity::Warning)
            ++warnings;
        else if (finding.severity == RepoSecuritySeverity::High ||
                 finding.severity == RepoSecuritySeverity::Critical)
            ++severe;
    }

    m_securitySummary->setText(
        QStringLiteral(
            "<div style='font-size:21px; font-weight:800; color:%1'>%2</div>"
            "<div style='color:#8b949e; font-size:12px; font-weight:600'>"
            "%3 at %4. %5 finding%6, %7 warning%8, %9 severe.</div>")
            .arg(repoSecuritySeverityColor(highest),
                 RepoSecurity::severityText(highest).toHtmlEscaped(),
                 snapshot.repoKey.toHtmlEscaped(), snapshot.ref.toHtmlEscaped())
            .arg(snapshot.findings.size())
            .arg(snapshot.findings.size() == 1 ? QString() : QStringLiteral("s"))
            .arg(warnings)
            .arg(warnings == 1 ? QString() : QStringLiteral("s"))
            .arg(severe));

    int index = 0;
    const RepoSecuritySignal *depSignal = nullptr;
    for (const RepoSecuritySignal &signal : snapshot.signalList) {
        // Dependency scan gets a dedicated full-width table card (added below)
        if (signal.key == QLatin1String("dependencies")) {
            depSignal = &signal;
            continue;
        }
        auto *card = new QLabel(repoSecuritySignalHtml(signal));
        card->setObjectName("insightsCard");
        card->setTextFormat(Qt::RichText);
        card->setWordWrap(true);
        card->setMinimumHeight(92);
        const int row = index / 3;
        const int col = index % 3;
        m_securitySignalsGrid->addWidget(card, row, col);
        ++index;
    }
    // Place the dependency-scan card in a dedicated full-width row
    if (depSignal) {
        const int depRow = (index + 2) / 3;
        auto *depCard = buildDepScanCard(
            *depSignal, snapshot.findings, snapshot.dependencyCounts, snapshot.generatedAtMs,
            localBase, this, m_repoSecurityScanRunning, m_repoSecurityScanningPath,
            [this](const QString &manifestPath) { runRepoDependencyScan(manifestPath); });
        m_securitySignalsGrid->addWidget(depCard, depRow, 0, 1, 4);
    }

    fillRepoFindingsTable(m_securityFindingsTable, snapshot.findings,
                          QStringLiteral("No local findings at this ref."));

    m_securityFindingsTable->setSortingEnabled(true);
}

void MainWindow::refreshRepoSecurity()
{
    if (!m_securitySummary || !m_securitySignalsGrid || !m_securityFindingsTable)
        return;

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        while (QLayoutItem *item = m_securitySignalsGrid->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        TableRepaintGuard repaintGuard(m_securityFindingsTable);
        m_securityFindingsTable->setSortingEnabled(false);
        m_securityFindingsTable->setRowCount(0);
        m_securitySummary->setText(
            "<b>Security</b><br><span style='color:#8b949e'>"
            "Select a repository to scan local evidence.</span>");
        m_securityFindingsTable->setSortingEnabled(true);
        return;
    }

    const RepositoryRecord &selected = m_repositories.at(m_repoDetailIndex);
    const RepositoryRecord &writable = writableRecordFor(selected);
    m_repoSecurityScanRunning = false;
    m_repoSecurityScanningPath.clear();
    applyRepoSecuritySnapshot(
        RepoSecurity::scan(buildRepoSecurityInput(selected, writable)),
        writable.localPath);
}

void MainWindow::runRepoDependencyScan(const QString &manifestPath)
{
    if (m_repoSecurityScanRunning)
        return;
    if (!m_securitySummary || !m_securitySignalsGrid || !m_securityFindingsTable)
        return;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;

    const RepositoryRecord &selected = m_repositories.at(m_repoDetailIndex);
    const RepositoryRecord &writable = writableRecordFor(selected);
    const RepoSecurityInput input = buildRepoSecurityInput(selected, writable);
    const QString startOwner = selected.owner;
    const QString startName = selected.name;

    m_repoSecurityScanRunning = true;
    m_repoSecurityScanningPath = manifestPath;
    // Re-render immediately using the last-known snapshot so the button/progress
    // bar flip to their busy state right away, before the rescan itself (which
    // runs off-thread below) has produced anything new.
    applyRepoSecuritySnapshot(m_lastRepoSecuritySnapshot, writable.localPath);

    if (manifestPath.isEmpty()) {
        // Full repo scan
        runOffThread<RepoSecuritySnapshot>(
            [input]() { return RepoSecurity::scan(input); },
            [this, startOwner, startName](RepoSecuritySnapshot snapshot) {
                m_repoSecurityScanRunning = false;
                m_repoSecurityScanningPath.clear();
                if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
                    return;
                const RepositoryRecord &current = m_repositories.at(m_repoDetailIndex);
                if (current.owner != startOwner || current.name != startName)
                    return; // user navigated to a different repo while the scan ran
                applyRepoSecuritySnapshot(snapshot, writableRecordFor(current).localPath);
            });
    } else {
        // Single-manifest scan: update just that manifest's findings and dependency count
        runOffThread<RepoSecurityManifestScan>(
            [input, manifestPath]() { return RepoSecurity::scanManifest(input, manifestPath); },
            [this, startOwner, startName, manifestPath](RepoSecurityManifestScan result) {
                m_repoSecurityScanRunning = false;
                m_repoSecurityScanningPath.clear();
                if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
                    return;
                const RepositoryRecord &current = m_repositories.at(m_repoDetailIndex);
                if (current.owner != startOwner || current.name != startName)
                    return;

                // Update the snapshot with new findings for this manifest
                RepoSecuritySnapshot &snapshot = m_lastRepoSecuritySnapshot;
                snapshot.dependencyCounts[manifestPath] = result.dependencyCount;
                // Replace old findings for this manifest with new ones
                snapshot.findings.erase(
                    std::remove_if(snapshot.findings.begin(), snapshot.findings.end(),
                                   [&manifestPath](const RepoSecurityFinding &f) {
                                       return f.path == manifestPath && f.category == QLatin1String("Dependency");
                                   }),
                    snapshot.findings.end());
                snapshot.findings.append(result.findings);

                applyRepoSecuritySnapshot(snapshot, writableRecordFor(current).localPath);
            });
    }
}

void MainWindow::openRepoFileAtLine(const QString &path, int line)
{
    openRepoFile(path);
    if (line <= 0)
        return;
    QTimer::singleShot(0, this, [this, path, line] {
        auto *edit = qobject_cast<QPlainTextEdit *>(m_openFileTabs.value(path));
        if (!edit)
            return;
        QTextCursor lc(edit->document());
        lc.movePosition(QTextCursor::Start);
        if (line > 1)
            lc.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line - 1);
        QTextEdit::ExtraSelection lineSel;
        lineSel.cursor = lc;
        lineSel.format.setBackground(QColor(31, 111, 235, 60));
        lineSel.format.setProperty(QTextFormat::FullWidthSelection, true);
        // Move the cursor (and focus) first: CodePreviewEditor's own
        // cursorPositionChanged handler calls highlightCurrentLine(), which
        // replaces the extra-selection list with its neutral current-line
        // background. Applying our highlight after that keeps the distinct
        // blue finding highlight instead of it being silently overwritten.
        edit->setTextCursor(lc);
        edit->setFocus();
        edit->centerCursor();
        edit->setExtraSelections({lineSel});
    });
}

QWidget *MainWindow::buildRepoQualityTab()
{
    auto *page = new QWidget;
    page->setObjectName("mainContent");

    auto *scroll = new QScrollArea;
    scroll->setObjectName("mainContent");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    content->setObjectName("insightsPage");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    auto *heading = new QLabel("Quality");
    heading->setObjectName("qualityTitle");
    heading->setProperty("class", "channelTitle");
    heading->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;"));
    auto *subtitle = new QLabel(
        "Local quality metrics for this repository: check runs, code volume, "
        "documentation, tests, maintenance markers, file sizes, commit "
        "activity and issue hygiene.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    m_qualityRefreshButton = new QPushButton("Refresh");
    m_qualityRefreshButton->setObjectName("ghostButton");
    m_qualityRefreshButton->setProperty("buttonSize", "sm");
    m_qualityRefreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_qualityRefreshButton, "sync", 16);
    connect(m_qualityRefreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshRepoQuality);

    auto *headingCol = new QVBoxLayout;
    headingCol->setContentsMargins(0, 0, 0, 0);
    headingCol->setSpacing(3);
    headingCol->addWidget(heading);
    headingCol->addWidget(subtitle);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    headerRow->addLayout(headingCol, 1);
    headerRow->addWidget(m_qualityRefreshButton, 0, Qt::AlignTop);
    layout->addLayout(headerRow);

    m_qualitySummary = new QLabel;
    m_qualitySummary->setObjectName("insightsCard");
    m_qualitySummary->setTextFormat(Qt::RichText);
    m_qualitySummary->setWordWrap(true);
    m_qualitySummary->setMinimumHeight(92);
    layout->addWidget(m_qualitySummary);

    auto *metricsLabel = new QLabel("METRICS");
    metricsLabel->setObjectName("sectionLabel");
    layout->addWidget(metricsLabel);

    m_qualitySignalsPanel = new QWidget;
    m_qualitySignalsGrid = new QGridLayout(m_qualitySignalsPanel);
    m_qualitySignalsGrid->setContentsMargins(0, 0, 0, 0);
    m_qualitySignalsGrid->setSpacing(10);
    layout->addWidget(m_qualitySignalsPanel);

    auto *findingsLabel = new QLabel("FINDINGS");
    findingsLabel->setObjectName("sectionLabel");
    layout->addWidget(findingsLabel);

    m_qualityFindingsTable = new QTableWidget(0, 4);
    installColumnHeaderMenu(m_qualityFindingsTable); // 3-dots per-column menu (issue #318)
    m_qualityFindingsTable->setObjectName("issueTable");
    enableHoverRowHighlight(m_qualityFindingsTable);
    m_qualityFindingsTable->setHorizontalHeaderLabels(
        {"Severity", "Category", "Finding", "Suggestion"});
    m_qualityFindingsTable->verticalHeader()->setVisible(false);
    m_qualityFindingsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_qualityFindingsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_qualityFindingsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_qualityFindingsTable->setShowGrid(false);
    m_qualityFindingsTable->setWordWrap(false);
    QHeaderView *header = m_qualityFindingsTable->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_qualityFindingsTable);
    m_qualityFindingsTable->setMinimumHeight(220);
    layout->addWidget(m_qualityFindingsTable);
    layout->addStretch();
    connect(m_qualityFindingsTable, &QTableWidget::cellClicked,
            this, [this](int row, int /*col*/) {
        auto *item = m_qualityFindingsTable->item(row, 2);
        if (!item)
            return;
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        // Switch to the Code tab (index 0) so the highlighted line is visible;
        // openRepoFileAtLine alone only touches the (currently hidden) files panel.
        if (m_repoDetailTabs && m_repoDetailTabs->button(0))
            m_repoDetailTabs->button(0)->click();
        openRepoFileAtLine(path, item->data(Qt::UserRole + 1).toInt());
    });

    scroll->setWidget(content);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);
    refreshRepoQuality();
    return page;
}

void MainWindow::refreshRepoQuality()
{
    if (!m_qualitySummary || !m_qualitySignalsGrid || !m_qualityFindingsTable)
        return;

    while (QLayoutItem *item = m_qualitySignalsGrid->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    TableRepaintGuard repaintGuard(m_qualityFindingsTable);
    m_qualityFindingsTable->setSortingEnabled(false);
    m_qualityFindingsTable->setRowCount(0);

    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        m_qualitySummary->setText(
            "<b>Quality</b><br><span style='color:#8b949e'>"
            "Select a repository to compute quality metrics.</span>");
        m_qualityFindingsTable->setSortingEnabled(true);
        return;
    }

    const RepositoryRecord &selected = m_repositories.at(m_repoDetailIndex);
    const RepositoryRecord &writable = writableRecordFor(selected);

    RepoSecurityInput input;
    input.owner = selected.owner;
    input.name = selected.name;
    input.localPath = writable.localPath;
    input.mirrorPath = writable.mirrorPath.isEmpty() ? selected.mirrorPath
                                                     : writable.mirrorPath;
    input.publishToNetwork = selected.publishToNetwork;
    input.isPrivate = selected.isPrivate;
    input.previewOnly = selected.previewOnly;
    input.actionsEnabled = selected.actionsEnabled;
    input.issues =
        IssueStore(writable.localPath, input.mirrorPath, &m_profileIdentity, m_userName)
            .loadAll();
    input.workflows = availableWorkflowsForRepo(writable);
    for (const ActionRun &run : std::as_const(m_actionRuns))
        if (run.owner == selected.owner && run.name == selected.name)
            input.actionRuns.append(run);

    const RepoSecuritySnapshot snapshot = RepoQuality::scan(input);
    const RepoSecuritySeverity highest = RepoSecurity::highestSeverity(snapshot);
    int warnings = 0;
    int severe = 0;
    for (const RepoSecuritySignal &signal : snapshot.signalList) {
        if (signal.severity == RepoSecuritySeverity::Warning)
            ++warnings;
        else if (signal.severity == RepoSecuritySeverity::High ||
                 signal.severity == RepoSecuritySeverity::Critical)
            ++severe;
    }
    for (const RepoSecurityFinding &finding : snapshot.findings) {
        if (finding.severity == RepoSecuritySeverity::Warning)
            ++warnings;
        else if (finding.severity == RepoSecuritySeverity::High ||
                 finding.severity == RepoSecuritySeverity::Critical)
            ++severe;
    }

    m_qualitySummary->setText(
        QStringLiteral(
            "<div style='font-size:21px; font-weight:800; color:%1'>%2</div>"
            "<div style='color:#8b949e; font-size:12px; font-weight:600'>"
            "%3 at %4. %5 finding%6, %7 warning%8, %9 severe.</div>")
            .arg(repoSecuritySeverityColor(highest),
                 RepoSecurity::severityText(highest).toHtmlEscaped(),
                 snapshot.repoKey.toHtmlEscaped(), snapshot.ref.toHtmlEscaped())
            .arg(snapshot.findings.size())
            .arg(snapshot.findings.size() == 1 ? QString() : QStringLiteral("s"))
            .arg(warnings)
            .arg(warnings == 1 ? QString() : QStringLiteral("s"))
            .arg(severe));

    int index = 0;
    for (const RepoSecuritySignal &signal : snapshot.signalList) {
        auto *card = new QLabel(repoSecuritySignalHtml(signal));
        card->setObjectName("insightsCard");
        card->setTextFormat(Qt::RichText);
        card->setWordWrap(true);
        card->setMinimumHeight(92);
        const int row = index / 3;
        const int col = index % 3;
        m_qualitySignalsGrid->addWidget(card, row, col);
        ++index;
    }

    fillRepoFindingsTable(m_qualityFindingsTable, snapshot.findings,
                          QStringLiteral("No quality findings at this ref."));

    m_qualityFindingsTable->setSortingEnabled(true);
}

namespace {
// The scan itself lives in DirectorySizeScan.cpp so the elevated helper
// process can run the identical walk (adhoc #76).
using forkmesh::DirectorySizeScanCancel;
using forkmesh::DirectorySizeScanOptions;
using forkmesh::DirectorySizeScanResult;

// Mount points offered as size-map shortcuts (adhoc #21) — everything `df`
// lists, minus the read-only squashfs images snap piles up by the dozen,
// biggest filesystem first so the real disks lead the column.
QList<QStorageInfo> sizeMapVolumes()
{
    QList<QStorageInfo> volumes;
    QSet<QString> seen;
    for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady() || volume.bytesTotal() <= 0)
            continue;
        if (volume.fileSystemType() == "squashfs")
            continue;
        const QString mount = volume.rootPath();
        if (mount.isEmpty() || seen.contains(mount))
            continue;
        seen.insert(mount);
        volumes.append(volume);
    }
    std::sort(volumes.begin(), volumes.end(),
              [](const QStorageInfo &a, const QStorageInfo &b) {
                  return a.bytesTotal() > b.bytesTotal();
              });
    return volumes;
}
} // namespace

QWidget *MainWindow::buildSizeMapTab()
{
    auto *page = new QWidget;
    page->setObjectName("mainContent");

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);

    auto *heading = new QLabel("Size map");
    heading->setObjectName("channelTitle");
    auto *subtitle = new QLabel(
        "How a folder's bytes spread across directories and files "
        "(.git excluded). Click a directory to zoom in, the centre to zoom "
        "back out; slices with no further subdivision are individual files. "
        "It starts on this repository's working copy — pick any other folder "
        "on disk, or a filesystem on the right, to size that instead. Folders "
        "holding directories this user cannot read — \"/\" above all — ask for "
        "the root password first, so the map covers everything.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    auto *refresh = new QPushButton("Rescan");
    refresh->setObjectName("ghostButton");
    refresh->setProperty("buttonSize", "sm");
    refresh->setCursor(Qt::PointingHandCursor);
    setOcticon(refresh, "sync", 16);
    connect(refresh, &QPushButton::clicked, this,
            [this] { refreshSizeMapTab(true, true); });
    addRefreshSpin(refresh);

    // Only visible while a scan (worker thread or elevated helper) is running.
    auto *stop = new QPushButton("Stop");
    stop->setObjectName("ghostButton");
    stop->setProperty("buttonSize", "sm");
    stop->setCursor(Qt::PointingHandCursor);
    setOcticon(stop, "x", 16);
    stop->setToolTip("Stop the scan in progress.");
    stop->setVisible(false);
    m_sizeMapStop = stop;
    connect(stop, &QPushButton::clicked, this, [this] { stopSizeMapScan(); });

    auto *headingCol = new QVBoxLayout;
    headingCol->setContentsMargins(0, 0, 0, 0);
    headingCol->setSpacing(3);
    headingCol->addWidget(heading);
    headingCol->addWidget(subtitle);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    headerRow->addLayout(headingCol, 1);
    headerRow->addWidget(refresh, 0, Qt::AlignTop);
    headerRow->addWidget(stop, 0, Qt::AlignTop);
    layout->addLayout(headerRow);

    // Folder switcher: the map defaults to the repository's working copy but
    // can size any directory on disk.
    auto *choose = new QPushButton("Choose folder…");
    choose->setObjectName("ghostButton");
    choose->setProperty("buttonSize", "sm");
    choose->setCursor(Qt::PointingHandCursor);
    setOcticon(choose, "file-directory", 16);
    choose->setToolTip("Scan any folder on this machine instead of the "
                       "repository's working copy.");
    connect(choose, &QPushButton::clicked, this,
            [this] { chooseSizeMapFolder(); });

    auto *resetRoot = new QPushButton("Back to repository");
    resetRoot->setObjectName("ghostButton");
    resetRoot->setProperty("buttonSize", "sm");
    resetRoot->setCursor(Qt::PointingHandCursor);
    resetRoot->setToolTip("Size this repository's working copy again.");
    resetRoot->setVisible(false);
    m_sizeMapResetRoot = resetRoot;
    connect(resetRoot, &QPushButton::clicked, this,
            [this] { setSizeMapRootOverride(QString()); });

    m_sizeMapRootLabel = new QLabel;
    m_sizeMapRootLabel->setObjectName("statusLine");
    m_sizeMapRootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *folderRow = new QHBoxLayout;
    folderRow->setContentsMargins(0, 0, 0, 0);
    folderRow->setSpacing(8);
    folderRow->addWidget(choose);
    folderRow->addWidget(resetRoot);
    folderRow->addWidget(m_sizeMapRootLabel, 1);
    layout->addLayout(folderRow);

    auto *hideIgnored = new QCheckBox("Hide .gitignored files");
    hideIgnored->setCursor(Qt::PointingHandCursor);
    hideIgnored->setToolTip(
        "Drop everything git ignores (build output, node_modules, …) from the "
        "map, so only tracked and un-ignored files count toward the sizes.");
    m_sizeMapHideIgnored = hideIgnored;
    connect(hideIgnored, &QCheckBox::toggled, this,
            [this] { refreshSizeMapTab(true, true); });
    layout->addWidget(hideIgnored);

    m_sizeMapStatus = new QLabel;
    m_sizeMapStatus->setObjectName("statusLine");
    m_sizeMapStatus->setWordWrap(true);

    // Only appears once a scan actually hit directories this user cannot list
    // (adhoc #76) — sizing "/" as a normal user misses /root, /var/lib and the
    // rest, so the map looks empty next to whatever pseudo-file survived.
    auto *elevate = new QPushButton("Scan as administrator");
    elevate->setObjectName("ghostButton");
    elevate->setProperty("buttonSize", "sm");
    elevate->setCursor(Qt::PointingHandCursor);
    setOcticon(elevate, "lock", 16);
    elevate->setToolTip(
        "Ask for the administrator (root) password and measure the folders this "
        "user cannot read.");
    elevate->setVisible(false);
    m_sizeMapElevate = elevate;
    connect(elevate, &QPushButton::clicked, this,
            [this] { rescanSizeMapElevated(); });

    auto *statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(8);
    statusRow->addWidget(m_sizeMapStatus, 1);
    statusRow->addWidget(elevate, 0, Qt::AlignTop);
    layout->addLayout(statusRow);

    auto *chart = new RepoSunburstChart;
    m_sizeMapChart = chart;

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(14);
    body->addWidget(chart, 1);
    body->addWidget(buildSizeMapVolumesPanel(), 0);
    layout->addLayout(body, 1);
    refreshSizeMapVolumes();
    return page;
}

// Right-hand column of filesystem shortcuts: one small used/free map per mount
// point, each a click away from becoming the big map (adhoc #21).
QWidget *MainWindow::buildSizeMapVolumesPanel()
{
    auto *panel = new QWidget;
    panel->setFixedWidth(268);
    auto *col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(6);

    auto *title = new QLabel("Filesystems");
    title->setObjectName("sectionTitle");
    col->addWidget(title);

    auto *hint = new QLabel(
        "Every mounted filesystem, sized from the mount itself. Click one to "
        "expand it into the full map — nested mounts are left to their own "
        "card, so the totals match df.");
    hint->setObjectName("statusLine");
    hint->setWordWrap(true);
    col->addWidget(hint);

    auto *scroll = new QScrollArea;
    scroll->setObjectName("mainContent");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *inner = new QWidget;
    m_sizeMapVolumesBox = inner;
    auto *innerCol = new QVBoxLayout(inner);
    innerCol->setContentsMargins(0, 0, 0, 0);
    innerCol->setSpacing(2);
    innerCol->addStretch(1);
    scroll->setWidget(inner);
    col->addWidget(scroll, 1);
    return panel;
}

void MainWindow::refreshSizeMapVolumes()
{
    if (!m_sizeMapVolumesBox)
        return;
    auto *col = qobject_cast<QVBoxLayout *>(m_sizeMapVolumesBox->layout());
    if (!col)
        return;
    // Mounts come and go (removable disks, containers), so the column is
    // rebuilt from scratch on every rescan rather than patched in place.
    while (QLayoutItem *item = col->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    const QString current = QDir::cleanPath(sizeMapRoot());
    for (const QStorageInfo &volume : sizeMapVolumes()) {
        auto *card = new StorageMiniMap(volume);
        const QString mount = card->mountPoint();
        card->setSelected(!current.isEmpty() && QDir::cleanPath(mount) == current);
        card->setOnClicked([this, mount] { setSizeMapRootOverride(mount); });
        col->addWidget(card);
    }
    col->addStretch(1);
}

QString MainWindow::sizeMapRoot() const
{
    if (!m_sizeMapRootOverride.isEmpty())
        return m_sizeMapRootOverride;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return QString();
    return writableRecordFor(m_repositories.at(m_repoDetailIndex)).localPath;
}

void MainWindow::chooseSizeMapFolder()
{
    // Start the browser where the map currently sits, so picking a sibling
    // folder is one step away.
    QString start = sizeMapRoot();
    if (start.isEmpty() || !QDir(start).exists())
        start = QDir::homePath();
    // Qt's own dialog rather than the platform one: the portal/GTK pickers
    // bury the filesystem root behind "Other Locations" and refuse to hand
    // back "/" itself, which is exactly the folder people want to size
    // (adhoc #21). This one returns whatever directory is open when Choose is
    // pressed, so "/" — and every mount — is selectable.
    QFileDialog dialog(this, "Choose a folder to size", start);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFilter(QDir::AllDirs | QDir::Drives | QDir::NoDotAndDotDot |
                     QDir::Hidden);
    QList<QUrl> sidebar{QUrl::fromLocalFile(QDir::rootPath()),
                        QUrl::fromLocalFile(QDir::homePath())};
    for (const QStorageInfo &volume : sizeMapVolumes()) {
        const QUrl url = QUrl::fromLocalFile(volume.rootPath());
        if (!sidebar.contains(url))
            sidebar.append(url);
    }
    dialog.setSidebarUrls(sidebar);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QStringList chosen = dialog.selectedFiles();
    if (chosen.isEmpty() || chosen.first().isEmpty())
        return;
    setSizeMapRootOverride(chosen.first());
}

void MainWindow::setSizeMapRootOverride(const QString &path)
{
    if (m_sizeMapRootOverride == path)
        return;
    m_sizeMapRootOverride = path;
    // Picking a folder is a click, so this is allowed to ask for the root
    // password when that folder needs it — clicking "/" is the whole reason the
    // prompt moved to the front (adhoc #112).
    refreshSizeMapTab(true, true);
}

void MainWindow::refreshSizeMapTab(bool force, bool allowElevation)
{
    auto *chart = static_cast<RepoSunburstChart *>(m_sizeMapChart);
    if (!chart || !m_sizeMapStatus)
        return;
    const bool overridden = !m_sizeMapRootOverride.isEmpty();
    if (!overridden &&
        (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()))
        return;
    const QString path = sizeMapRoot();
    refreshSizeMapVolumes();
    if (m_sizeMapResetRoot)
        m_sizeMapResetRoot->setVisible(overridden);
    if (m_sizeMapRootLabel) {
        m_sizeMapRootLabel->setText(
            path.isEmpty()
                ? QStringLiteral("No folder selected.")
                : QStringLiteral("%1%2").arg(
                      QDir::toNativeSeparators(path),
                      overridden ? QString()
                                 : QStringLiteral("  ·  working copy")));
    }
    if (path.isEmpty() || !QDir(path).exists()) {
        chart->clear();
        m_sizeMapScannedPath.clear();
        if (m_sizeMapElevate)
            m_sizeMapElevate->setVisible(false);
        m_sizeMapStatus->setText(
            overridden ? "That folder no longer exists — choose another one."
                       : "No local working copy to scan for this repository.");
        return;
    }
    if (!force && m_sizeMapScannedPath == path)
        return; // the chart already shows this folder
    if (m_sizeMapScanning)
        return; // its finish handler notices the root changed and rescans
    // Every plain rescan starts unprivileged again; the button comes back if
    // this folder still holds directories the user cannot list.
    if (m_sizeMapElevate)
        m_sizeMapElevate->setVisible(false);
    const bool hideIgnored =
        m_sizeMapHideIgnored && m_sizeMapHideIgnored->isChecked();
    DirectorySizeScanOptions options;
    options.pruned = sizeMapPrunedPaths(path);
    // Folders this user cannot fully read — "/" above all — used to be scanned
    // twice: once unprivileged into a map missing /root, /var/lib and the rest,
    // then again after clicking the button. Ask for the password on the click
    // that chose the folder instead (adhoc #112).
    if (allowElevation && forkmesh::scanNeedsElevation(path, options.pruned)) {
        // Off the click's own stack: where pkexec is missing the prompt is a
        // modal dialog, and pumping the event loop from inside a filesystem
        // card's click handler would run the deleteLater() refreshSizeMapVolumes
        // just queued for that very card. The epoch also collapses a second
        // click into one prompt.
        const int pending = ++m_sizeMapScanEpoch;
        m_sizeMapStatus->setText(
            QStringLiteral("Asking for administrator access to size %1 …")
                .arg(QDir::toNativeSeparators(path)));
        QTimer::singleShot(0, this, [this, pending] {
            if (pending == m_sizeMapScanEpoch)
                rescanSizeMapElevated(true);
        });
        return;
    }
    m_sizeMapScanning = true;
    if (m_sizeMapStop)
        m_sizeMapStop->setVisible(true);
    const int epoch = ++m_sizeMapScanEpoch;
    m_sizeMapStatus->setText(
        QStringLiteral("Scanning %1 …").arg(QDir::toNativeSeparators(path)));
    // The walk reports the folder it is in from the worker thread; the hop
    // through invokeMethod() is what keeps the label on the GUI thread. The
    // QPointer matters because the pool thread outlives a window closed
    // mid-scan.
    const QPointer<MainWindow> guard(this);
    const forkmesh::DirectorySizeScanProgress progress =
        [this, guard, epoch](const QString &current, qint64 bytes, int files) {
            if (!guard)
                return;
            QMetaObject::invokeMethod(
                this,
                [this, epoch, current, bytes, files] {
                    if (epoch != m_sizeMapScanEpoch)
                        return; // a newer scan owns the label now
                    showSizeMapScanProgress(current, bytes, files, false);
                },
                Qt::QueuedConnection);
        };
    auto *watcher = new QFutureWatcher<DirectorySizeScanResult>(this);
    m_sizeMapWatcher = watcher;
    connect(watcher, &QFutureWatcher<DirectorySizeScanResult>::finished, this,
            [this, watcher, path, epoch, hideIgnored] {
                watcher->deleteLater();
                if (m_sizeMapWatcher == watcher)
                    m_sizeMapWatcher = nullptr;
                m_sizeMapScanning = false;
                if (m_sizeMapStop)
                    m_sizeMapStop->setVisible(false);
                if (epoch != m_sizeMapScanEpoch)
                    return; // a newer scan superseded this one
                DirectorySizeScanResult result = watcher->result();
                // The user may have opened another repo (or picked another
                // folder) while the scan ran — a stale tree would mislabel the
                // chart, so rescan instead.
                if (sizeMapRoot() != path) {
                    refreshSizeMapTab(false);
                    return;
                }
                applySizeMapResult(path, std::move(result), hideIgnored, false);
            });
    // The QPromise parameter is how Stop reaches into a running walk: the
    // future's cancel() only flips promise.isCanceled(), so scanDirectorySizes
    // takes a canceled() poll wired to it and checks it between directories
    // (adhoc #189's Stop button).
    watcher->setFuture(QtConcurrent::run(
        [path, options, progress](QPromise<DirectorySizeScanResult> &promise) {
            const forkmesh::BackgroundScope activity(
                QStringLiteral("scan"), QStringLiteral("Sizing %1").arg(path));
            const DirectorySizeScanCancel canceled = [&promise] {
                return promise.isCanceled();
            };
            promise.addResult(
                forkmesh::scanDirectorySizes(path, options, progress, canceled));
        }));
}

// Live status line while a scan runs: the folder being walked right now, with
// the totals counted so far (adhoc #112). The path is elided rather than
// wrapped, so a deep tree cannot rewrap the row on every update.
void MainWindow::showSizeMapScanProgress(const QString &current, qint64 bytes,
                                         int files, bool elevated)
{
    if (!m_sizeMapStatus)
        return;
    const QString suffix =
        QStringLiteral("  ·  %1 files · %2 so far")
            .arg(QLocale().toString(files),
                 QLocale().formattedDataSize(bytes));
    const QString prefix = elevated
                               ? QStringLiteral("Scanning as administrator: ")
                               : QStringLiteral("Scanning: ");
    const QFontMetrics metrics(m_sizeMapStatus->font());
    const int budget = qMax(220, m_sizeMapStatus->width() -
                                    metrics.horizontalAdvance(prefix + suffix) -
                                    16);
    m_sizeMapStatus->setText(
        prefix +
        metrics.elidedText(QDir::toNativeSeparators(current), Qt::ElideMiddle,
                           budget) +
        suffix);
}

QSet<QString> MainWindow::sizeMapPrunedPaths(const QString &path) const
{
    QSet<QString> pruned;
    // Resolve the .gitignore prune set on the GUI thread (git via QProcess is
    // awkward from a QtConcurrent worker), then hand it to the scan. Using
    // --directory keeps wholly-ignored trees to a single entry instead of every
    // file inside them.
    if (m_sizeMapHideIgnored && m_sizeMapHideIgnored->isChecked()) {
        QByteArray out;
        if (runGitCapture(path,
                          {"ls-files", "--others", "--ignored",
                           "--exclude-standard", "--directory", "-z"},
                          &out, nullptr)) {
            const QDir root(path);
            for (const QByteArray &raw : out.split('\0')) {
                if (raw.isEmpty())
                    continue;
                QString rel = QString::fromUtf8(raw);
                if (rel.endsWith(QLatin1Char('/')))
                    rel.chop(1);
                pruned.insert(root.absoluteFilePath(rel));
            }
        }
    }
    // Stay on one filesystem, like `du -x`: pseudo mounts under the scanned
    // root report fiction and real mounts would be counted twice, once here and
    // once from their own card. QStorageInfo alone is not enough — it hides the
    // pseudo filesystems, which is how /proc/kcore's 128 TiB used to swallow a
    // scan of "/" whole (adhoc #76) — so the kernel's mount table is read too.
    const QString ownMount = QDir::cleanPath(QStorageInfo(path).rootPath());
    QSet<QString> mounts = forkmesh::systemMountPoints();
    for (const QStorageInfo &volume : QStorageInfo::mountedVolumes())
        mounts.insert(QDir::cleanPath(volume.rootPath()));
    for (const QString &mount : std::as_const(mounts)) {
        if (mount.isEmpty() || mount == ownMount)
            continue;
        pruned.insert(mount); // only matches if it sits inside the scan
    }
    return pruned;
}

// Shared tail of both scans: label the chart, then decide whether to offer the
// elevated rescan (adhoc #76).
void MainWindow::applySizeMapResult(const QString &path,
                                    DirectorySizeScanResult result,
                                    bool hideIgnored, bool elevated)
{
    if (!m_sizeMapStatus)
        return;
    m_sizeMapScannedPath = path;
    QString status =
        QStringLiteral("%1 files · %2 on disk (%3)")
            .arg(QLocale().toString(result.root.fileCount),
                 QLocale().formattedDataSize(result.root.size),
                 hideIgnored ? QStringLiteral(".git & .gitignored excluded")
                             : QStringLiteral(".git excluded"));
    const bool blocked = result.unreadableDirs > 0;
    if (blocked) {
        status += QStringLiteral(" · %1 folder%2 could not be read (%3%4)")
                      .arg(QLocale().toString(result.unreadableDirs),
                           result.unreadableDirs == 1 ? QString()
                                                      : QStringLiteral("s"),
                           result.unreadableSample.join(QStringLiteral(", ")),
                           result.unreadableSample.size() < result.unreadableDirs
                               ? QStringLiteral(", …")
                               : QString());
        status += elevated || forkmesh::runningAsRoot()
                      ? QStringLiteral(" — their contents are not counted.")
                      : QStringLiteral(
                            " — scan as administrator to include them.");
    } else if (elevated) {
        status += QStringLiteral(" · measured with administrator access");
    }
    m_sizeMapStatus->setText(status);
    if (m_sizeMapElevate)
        m_sizeMapElevate->setVisible(blocked && !elevated &&
                                     !forkmesh::runningAsRoot());
    if (auto *liveChart = static_cast<RepoSunburstChart *>(m_sizeMapChart)) {
        liveChart->setBasePath(path);
        liveChart->setRoot(std::move(result.root));
    }
}

void MainWindow::rescanSizeMapElevated(bool upfront)
{
    if (m_sizeMapScanning || !m_sizeMapStatus)
        return;
    const QString path = sizeMapRoot();
    if (path.isEmpty() || !QDir(path).exists())
        return;
    const bool hideIgnored =
        m_sizeMapHideIgnored && m_sizeMapHideIgnored->isChecked();

    // The helper is this very binary in its --size-map-scan mode, so root runs
    // the identical walk and streams the tree back. The request goes through a
    // file rather than stdin, which stays free for `sudo -S`'s password.
    DirectorySizeScanOptions options;
    options.pruned = sizeMapPrunedPaths(path);
    auto *request = new QTemporaryFile(
        QDir::temp().filePath(QStringLiteral("forkmesh-sizemap-XXXXXX.json")));
    if (!request->open()) {
        delete request;
        m_sizeMapStatus->setText(
            QStringLiteral("Could not prepare the elevated scan (no writable "
                           "temporary directory)."));
        if (upfront)
            refreshSizeMapTab(true); // still size what this user can read
        return;
    }
    request->write(forkmesh::encodeScanRequest(path, options));
    request->flush();
    const QString requestPath = request->fileName();

    const QString helper = QCoreApplication::applicationFilePath();
    const QString pkexec =
        QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    QString program;
    QStringList arguments;
    QByteArray stdinPayload;
    if (!pkexec.isEmpty()) {
        // pkexec raises the desktop's own password dialog; nothing to type here.
        program = pkexec;
        arguments = {helper, QStringLiteral("--size-map-scan"), requestPath};
    } else {
        bool accepted = false;
        const QString password = QInputDialog::getText(
            this, QStringLiteral("Administrator password"),
            QStringLiteral("Enter the root password for sudo, so %1 can be "
                           "measured in full — including the folders this user "
                           "cannot read:")
                .arg(QDir::toNativeSeparators(path)),
            QLineEdit::Password, QString(), &accepted);
        if (!accepted) {
            delete request;
            // Dismissed before anything was drawn: size what this user can read
            // rather than leaving the tab empty. That scan's own status ends in
            // "scan as administrator to include them", so the decline is not
            // silent.
            if (upfront)
                refreshSizeMapTab(true);
            return;
        }
        program = QStringLiteral("sudo");
        arguments = {QStringLiteral("-S"), QStringLiteral("-p"), QString(),
                     QStringLiteral("--"), helper,
                     QStringLiteral("--size-map-scan"), requestPath};
        stdinPayload = password.toUtf8() + '\n';
    }

    m_sizeMapScanning = true;
    if (m_sizeMapStop)
        m_sizeMapStop->setVisible(true);
    const int epoch = ++m_sizeMapScanEpoch;
    if (m_sizeMapElevate)
        m_sizeMapElevate->setVisible(false);
    m_sizeMapStatus->setText(
        QStringLiteral("Scanning %1 with administrator access …")
            .arg(QDir::toNativeSeparators(path)));

    auto *process = new QProcess(this);
    m_sizeMapElevatedProcess = process;
    request->setParent(process); // the temp file dies with the process
    process->setProgram(program);
    process->setArguments(arguments);
    // Root's walk reports itself on stderr, one line per update, so the same
    // live folder name appears whether the scan runs here or in the helper
    // (adhoc #112). Whole lines only: a read can land mid-line.
    auto pending = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardError, this,
            [this, process, pending, epoch] {
                pending->append(process->readAllStandardError());
                for (int cut = pending->indexOf('\n'); cut >= 0;
                     cut = pending->indexOf('\n')) {
                    const QByteArray line = pending->left(cut);
                    pending->remove(0, cut + 1);
                    QString current;
                    qint64 bytes = 0;
                    int files = 0;
                    // Anything else on stderr is pkexec's or sudo's own chatter.
                    if (!forkmesh::decodeScanProgress(line, &current, &bytes,
                                                      &files))
                        continue;
                    if (epoch != m_sizeMapScanEpoch)
                        continue;
                    showSizeMapScanProgress(current, bytes, files, true);
                }
                // A helper that never emits a newline must not grow the buffer
                // for the length of a scan of "/".
                if (pending->size() > 64 * 1024)
                    pending->clear();
            });
    connect(process, &QProcess::finished, this,
            [this, process, path, epoch, hideIgnored, upfront](
                int exitCode, QProcess::ExitStatus) {
                process->deleteLater();
                if (m_sizeMapElevatedProcess == process)
                    m_sizeMapElevatedProcess = nullptr;
                m_sizeMapScanning = false;
                if (m_sizeMapStop)
                    m_sizeMapStop->setVisible(false);
                if (epoch != m_sizeMapScanEpoch)
                    return; // a newer scan superseded this one
                const QByteArray payload = process->readAllStandardOutput();
                DirectorySizeScanResult result;
                if (exitCode != 0 ||
                    !forkmesh::decodeScanResult(payload, &result)) {
                    // Nothing has been drawn yet when the prompt came first, so
                    // fall back to the unprivileged walk rather than leaving an
                    // empty tab behind a dismissed password box. Its status
                    // offers the rescan again.
                    if (upfront) {
                        refreshSizeMapTab(true);
                        return;
                    }
                    // 126/127 is pkexec's "dismissed / not authorised".
                    m_sizeMapStatus->setText(
                        exitCode == 126 || exitCode == 127
                            ? QStringLiteral(
                                  "Administrator access was declined — the map "
                                  "still shows only the readable folders.")
                            : QStringLiteral(
                                  "The elevated scan failed — the map still "
                                  "shows only the readable folders."));
                    if (m_sizeMapElevate)
                        m_sizeMapElevate->setVisible(true);
                    return;
                }
                if (sizeMapRoot() != path) {
                    refreshSizeMapTab(false);
                    return;
                }
                applySizeMapResult(path, std::move(result), hideIgnored, true);
            });
    // A missing pkexec/sudo never emits finished(), so the scanning flag would
    // stay set and block every later rescan.
    connect(process, &QProcess::errorOccurred, this,
            [this, process, epoch, upfront](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                process->deleteLater();
                if (m_sizeMapElevatedProcess == process)
                    m_sizeMapElevatedProcess = nullptr;
                m_sizeMapScanning = false;
                if (m_sizeMapStop)
                    m_sizeMapStop->setVisible(false);
                if (epoch != m_sizeMapScanEpoch || !m_sizeMapStatus)
                    return;
                // Same fallback as a declined prompt: a map of the readable
                // folders beats an empty tab.
                if (upfront) {
                    refreshSizeMapTab(true);
                    return;
                }
                m_sizeMapStatus->setText(QStringLiteral(
                    "No way to ask for administrator access on this machine "
                    "(neither pkexec nor sudo could be started)."));
            });
    process->start();
    if (!stdinPayload.isEmpty()) {
        process->write(stdinPayload);
        stdinPayload.fill('\0');
    }
    process->closeWriteChannel();
}

void MainWindow::stopSizeMapScan()
{
    // Disconnect before cancel/kill, same as stopSearch(): once the finished
    // handler can never fire, there is no race between its own bookkeeping and
    // whatever the very next Rescan does, so this can freely reuse
    // m_sizeMapScanning/m_sizeMapScanEpoch instead of tracking a third flag.
    if (m_sizeMapWatcher) {
        disconnect(m_sizeMapWatcher, nullptr, this, nullptr);
        m_sizeMapWatcher->cancel();
        m_sizeMapWatcher->deleteLater();
        m_sizeMapWatcher = nullptr;
    }
    if (m_sizeMapElevatedProcess) {
        disconnect(m_sizeMapElevatedProcess, nullptr, this, nullptr);
        m_sizeMapElevatedProcess->kill();
        m_sizeMapElevatedProcess->deleteLater();
        m_sizeMapElevatedProcess = nullptr;
    }
    m_sizeMapScanning = false;
    ++m_sizeMapScanEpoch; // discards any progress update already queued
    if (m_sizeMapStop)
        m_sizeMapStop->setVisible(false);
    if (m_sizeMapStatus)
        m_sizeMapStatus->setText(QStringLiteral("Scan stopped."));
}

QWidget *MainWindow::buildInsightsTab()
{
    auto *page = new QWidget;
    page->setObjectName("mainContent");

    auto *scroll = new QScrollArea;
    scroll->setObjectName("mainContent");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    content->setObjectName("insightsPage");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    auto *heading = new QLabel("Insights");
    heading->setObjectName("channelTitle");
    auto *subtitle = new QLabel(
        "Local repository and ForkMesh traffic metrics. No data leaves this node.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    m_insightsRefreshButton = new QPushButton("Refresh");
    m_insightsRefreshButton->setObjectName("ghostButton");
    m_insightsRefreshButton->setProperty("buttonSize", "sm");
    m_insightsRefreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_insightsRefreshButton, "sync", 16);
    connect(m_insightsRefreshButton, &QPushButton::clicked, this,
            &MainWindow::loadRepoInsights);
    addRefreshSpin(m_insightsRefreshButton);

    auto *headingCol = new QVBoxLayout;
    headingCol->setContentsMargins(0, 0, 0, 0);
    headingCol->setSpacing(3);
    headingCol->addWidget(heading);
    headingCol->addWidget(subtitle);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    headerRow->addLayout(headingCol, 1);
    headerRow->addWidget(m_insightsRefreshButton, 0, Qt::AlignTop);
    layout->addLayout(headerRow);

    m_insightsSummary = new QLabel;
    m_insightsSummary->setObjectName("insightsCard");
    m_insightsSummary->setTextFormat(Qt::RichText);
    m_insightsSummary->setWordWrap(true);
    m_insightsSummary->setMinimumHeight(110);

    m_insightsTraffic = new QLabel;
    m_insightsTraffic->setObjectName("insightsCard");
    m_insightsTraffic->setTextFormat(Qt::RichText);
    m_insightsTraffic->setWordWrap(true);
    m_insightsTraffic->setMinimumHeight(110);

    auto *summaryRow = new QHBoxLayout;
    summaryRow->setContentsMargins(0, 0, 0, 0);
    summaryRow->setSpacing(10);
    summaryRow->addWidget(m_insightsSummary, 2);
    summaryRow->addWidget(m_insightsTraffic, 1);
    layout->addLayout(summaryRow);

    auto *languageLabel = new QLabel("LANGUAGES");
    languageLabel->setObjectName("sectionLabel");
    m_insightsLanguageBar = new QLabel;
    m_insightsLanguageBar->setObjectName("langBar");
    m_insightsLanguageBar->setFixedHeight(8);
    m_insightsLanguageBar->setTextFormat(Qt::RichText);
    m_insightsLanguageLegend = new QLabel;
    m_insightsLanguageLegend->setObjectName("statusLine");
    m_insightsLanguageLegend->setTextFormat(Qt::RichText);
    m_insightsLanguageLegend->setWordWrap(true);
    layout->addWidget(languageLabel);
    layout->addWidget(m_insightsLanguageBar);
    layout->addWidget(m_insightsLanguageLegend);

    auto configureTable = [](QTableWidget *table) {
        table->setObjectName("issueTable");
        enableHoverRowHighlight(table);
        table->verticalHeader()->setVisible(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setShowGrid(false);
        table->setWordWrap(false);
        table->setSortingEnabled(false);
        table->horizontalHeader()->setHighlightSections(false);
    };

    // Merged "Contributors & activity": one row per contributor carrying the
    // commit count, share, and a commits-over-time bar chart (formerly two
    // separate sections). A time-range selector rescopes every column.
    auto *contributorsLabel = new QLabel("CONTRIBUTORS & ACTIVITY");
    contributorsLabel->setObjectName("sectionLabel");

    m_insightsRangeCombo = new QComboBox;
    m_insightsRangeCombo->setObjectName("ghostCombo");
    m_insightsRangeCombo->setCursor(Qt::PointingHandCursor);
    // userData = window in days; 0 means all time.
    m_insightsRangeCombo->addItem("All time", 0);
    m_insightsRangeCombo->addItem("Last 12 months", 365);
    m_insightsRangeCombo->addItem("Last 90 days", 90);
    m_insightsRangeCombo->addItem("Last 30 days", 30);
    m_insightsRangeCombo->addItem("Last 7 days", 7);
    connect(m_insightsRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this] { loadRepoInsights(); });

    auto *contributorsHeader = new QHBoxLayout;
    contributorsHeader->setContentsMargins(0, 0, 0, 0);
    contributorsHeader->setSpacing(8);
    contributorsHeader->addWidget(contributorsLabel, 1, Qt::AlignBottom);
    contributorsHeader->addWidget(new QLabel("Activity range:"), 0, Qt::AlignVCenter);
    contributorsHeader->addWidget(m_insightsRangeCombo, 0, Qt::AlignVCenter);

    m_insightsContributors = new QTableWidget(0, 4);
    installColumnHeaderMenu(m_insightsContributors); // 3-dots per-column menu (issue #318)
    m_insightsContributors->setHorizontalHeaderLabels(
        {"Contributor", "Commits", "Share", "Activity"});
    configureTable(m_insightsContributors);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    m_insightsContributors->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::Stretch);
    makeColumnsResizable(m_insightsContributors);
    m_insightsContributors->verticalHeader()->setDefaultSectionSize(34);
    m_insightsContributors->setMinimumHeight(260);
    // Right-click a contributor to reassign their commits to another identity.
    m_insightsContributors->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_insightsContributors, &QWidget::customContextMenuRequested, this,
            &MainWindow::showInsightsContributorMenu);
    // Left-click a contributor's name or commit count to open the Commits tab
    // filtered to that author.
    connect(m_insightsContributors, &QTableWidget::cellClicked, this,
            [this](int row, int column) {
                if (column != 0 && column != 1)
                    return;
                QTableWidgetItem *nameItem = m_insightsContributors->item(row, 0);
                if (!nameItem)
                    return;
                const QString name = nameItem->data(Qt::UserRole).toString().isEmpty()
                                         ? nameItem->text()
                                         : nameItem->data(Qt::UserRole).toString();
                openCommitsForContributor(name);
            });

    // Caption under the table: the span the activity bars cover, oldest to newest.
    m_insightsActivityAxis = new QLabel;
    m_insightsActivityAxis->setObjectName("statusLine");
    m_insightsActivityAxis->setTextFormat(Qt::RichText);
    m_insightsActivityAxis->setWordWrap(true);

    layout->addLayout(contributorsHeader);
    layout->addWidget(m_insightsContributors);
    layout->addWidget(m_insightsActivityAxis);

    m_insightsActivity = new QLabel;
    m_insightsActivity->setObjectName("statusLine");
    m_insightsActivity->setTextFormat(Qt::RichText);
    m_insightsActivity->setWordWrap(true);
    layout->addWidget(m_insightsActivity);
    layout->addStretch();

    scroll->setWidget(content);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);
    return page;
}

QWidget *MainWindow::buildPlaceholderTab(const QString &name)
{
    auto *page = new QWidget;
    auto *label = new QLabel(
        QStringLiteral("<b>%1</b><br><span style='color:#8b949e'>Not available in "
                       "ForkMesh yet.</span>")
            .arg(name));
    label->setObjectName("placeholderPanel");
    label->setAlignment(Qt::AlignCenter);
    label->setTextFormat(Qt::RichText);
    auto *layout = new QVBoxLayout(page);
    layout->addStretch();
    layout->addWidget(label, 0, Qt::AlignCenter);
    layout->addStretch();
    return page;
}
