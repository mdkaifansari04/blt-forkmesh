// Files: the app's one editor, on its own activity-rail destination directly
// under Network / Users. Both explorers live here (adhoc #1590) — the real
// filesystem, which shows hidden entries by default because .git, .forkmesh and
// dotfiles are the usual reason for opening it, and the open repository's
// tracked tree, which is the only side that works on a mirror with no working
// tree. They share one column of editor tabs, the per-file commit / pull-request
// actions, and a working-tree commit + push. The Code page has no Explorer of
// its own any more; opening a file from anywhere lands here.

#include "MainWindow.h"
#include "CurrentPageStack.h"
#include "FileTreeSupport.h"
#include "MainWindowInternal.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace forkmesh::ui;

namespace {
// Rows carry the shared file-tree roles (path + isDir, see FileTreeSupport.h);
// only "children already read" is specific to this lazily-filled explorer.
constexpr int kFileLoadedRole = Qt::UserRole + 2;

const char *kFilesRootSetting = "files/explorerRoot";
const char *kFilesHiddenSetting = "files/showHidden";

// Preview cap. Enough to read a config file or a script; short of the point
// where dropping a multi-megabyte log into a QPlainTextEdit stalls the UI.
constexpr qint64 kPreviewByteLimit = 256 * 1024;

// The preview opens with "<path>\n<size> · <modified>\n\n" before the file's
// own first line, so line N of the file is block kFilePreviewHeaderLines+N-1.
constexpr int kFilePreviewHeaderLines = 3;

QString modifiedLabel(const QFileInfo &info)
{
    const QDateTime when = info.lastModified();
    return when.isValid() ? when.toLocalTime().toString(
                                QStringLiteral("yyyy-MM-dd HH:mm"))
                          : QString();
}
} // namespace

QWidget *MainWindow::buildFilesSection()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("filesSection"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(20, 18, 20, 20);
    outer->setSpacing(10);

    auto *heading = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Files"));
    title->setObjectName(QStringLiteral("pageTitle"));
    heading->addWidget(title);
    m_fileExplorerStatus = new QLabel;
    m_fileExplorerStatus->setObjectName(QStringLiteral("mutedLabel"));
    m_fileExplorerStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    heading->addWidget(m_fileExplorerStatus);
    heading->addStretch();

    // Hidden entries are on by default and the setting is remembered, so the
    // toggle is a way to quieten a noisy home directory rather than something
    // anyone has to find before the explorer becomes useful.
    m_fileExplorerHidden = new QCheckBox(QStringLiteral("Show hidden"));
    m_fileExplorerHidden->setCursor(Qt::PointingHandCursor);
    m_fileExplorerHidden->setToolTip(
        QStringLiteral("List dotfiles and hidden directories (.git, .forkmesh, "
                       ".config …)"));
    m_fileExplorerHidden->setChecked(
        QSettings().value(QLatin1String(kFilesHiddenSetting), true).toBool());
    connect(m_fileExplorerHidden, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QLatin1String(kFilesHiddenSetting), on);
        setFileExplorerRoot(m_fileExplorerRoot);
    });
    heading->addWidget(m_fileExplorerHidden);

    auto *upButton = new QPushButton(QStringLiteral("Up"));
    upButton->setObjectName(QStringLiteral("ghostButton"));
    upButton->setCursor(Qt::PointingHandCursor);
    upButton->setToolTip(QStringLiteral("Move the root to the parent directory"));
    setOcticon(upButton, "chevron-up", 14);
    connect(upButton, &QPushButton::clicked, this, [this] {
        QDir dir(m_fileExplorerRoot);
        if (dir.cdUp())
            setFileExplorerRoot(dir.absolutePath());
    });
    heading->addWidget(upButton);

    auto *homeButton = new QPushButton(QStringLiteral("Home"));
    homeButton->setObjectName(QStringLiteral("ghostButton"));
    homeButton->setCursor(Qt::PointingHandCursor);
    homeButton->setToolTip(QStringLiteral("Browse this account's home directory"));
    setOcticon(homeButton, "home", 14);
    connect(homeButton, &QPushButton::clicked, this,
            [this] { setFileExplorerRoot(QDir::homePath()); });
    heading->addWidget(homeButton);

    // The repository side of the explorer (adhoc #1590). It lists what git
    // tracks at the current ref rather than what is on disk, so it is also the
    // only side that shows anything on a mirror with no working tree. Every
    // other root control below implies the filesystem side and switches back.
    m_filesRepoTreeButton = new QPushButton(QStringLiteral("Repo"));
    m_filesRepoTreeButton->setObjectName(QStringLiteral("ghostButton"));
    m_filesRepoTreeButton->setCheckable(true);
    m_filesRepoTreeButton->setCursor(Qt::PointingHandCursor);
    m_filesRepoTreeButton->setToolTip(
        QStringLiteral("Browse the files the open repository tracks"));
    setOcticon(m_filesRepoTreeButton, "repo", 14);
    connect(m_filesRepoTreeButton, &QPushButton::clicked, this, [this] {
        showFilesRepoTree();
        // Land on the README rather than an empty editor, the way the Code
        // page's Explorer toggle used to.
        if (m_openFileTabs.isEmpty())
            openRepoReadme();
    });
    heading->addWidget(m_filesRepoTreeButton);

    auto *chooseButton = new QPushButton(QStringLiteral("Choose"));
    chooseButton->setObjectName(QStringLiteral("ghostButton"));
    chooseButton->setCursor(Qt::PointingHandCursor);
    chooseButton->setToolTip(QStringLiteral("Pick a directory to browse"));
    setOcticon(chooseButton, "file-directory", 14);
    connect(chooseButton, &QPushButton::clicked, this, [this] {
        // Shared picker: lists hidden directories, and can actually return "/"
        // (the portal picker cannot — see chooseExistingDirectory).
        const QString picked = chooseExistingDirectory(
            this, QStringLiteral("Choose a directory"), m_fileExplorerRoot);
        if (!picked.isEmpty())
            setFileExplorerRoot(picked);
    });
    heading->addWidget(chooseButton);

    auto *refreshButton = new QPushButton(QStringLiteral("Refresh"));
    refreshButton->setObjectName(QStringLiteral("ghostButton"));
    refreshButton->setCursor(Qt::PointingHandCursor);
    refreshButton->setToolTip(QStringLiteral("Re-read whichever tree is showing"));
    setOcticon(refreshButton, "sync", 14);
    connect(refreshButton, &QPushButton::clicked, this, [this] {
        // Refresh means "re-read what I am looking at", so it follows the tree
        // that is up rather than always re-listing the filesystem root.
        if (m_filesTreeStack && m_filesTreeStack->currentIndex() == 1)
            loadRepoFileTree();
        else
            setFileExplorerRoot(m_fileExplorerRoot);
    });
    heading->addWidget(refreshButton);

    auto *openButton = new QPushButton(QStringLiteral("Open"));
    openButton->setObjectName(QStringLiteral("primaryButton"));
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setToolTip(
        QStringLiteral("Open the current directory in the desktop file manager"));
    setOcticon(openButton, "link", 14);
    connect(openButton, &QPushButton::clicked, this, [this] {
        if (!m_fileExplorerRoot.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_fileExplorerRoot));
    });
    heading->addWidget(openButton);
    outer->addLayout(heading);

    // Typing a path (or pasting one) is the fastest way to jump somewhere far
    // from the current root, so the path bar is editable rather than a label.
    m_fileExplorerPath = new QLineEdit;
    m_fileExplorerPath->setObjectName(QStringLiteral("filesPathBar"));
    m_fileExplorerPath->setPlaceholderText(
        QStringLiteral("Path to browse, e.g. %1").arg(QDir::homePath()));
    m_fileExplorerPath->setClearButtonEnabled(true);
    connect(m_fileExplorerPath, &QLineEdit::returnPressed, this,
            [this] { setFileExplorerRoot(m_fileExplorerPath->text()); });
    outer->addWidget(m_fileExplorerPath);
    outer->addLayout(buildFilesEditorBar());

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName(QStringLiteral("filesExplorerSplitter"));
    splitter->setChildrenCollapsible(false);

    m_fileExplorerTree = new QTreeWidget;
    m_fileExplorerTree->setObjectName(QStringLiteral("fileTree"));
    enableHoverRowHighlight(m_fileExplorerTree);
    m_fileExplorerTree->setColumnCount(3);
    m_fileExplorerTree->setHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Size"),
         QStringLiteral("Modified")});
    m_fileExplorerTree->header()->setStretchLastSection(false);
    m_fileExplorerTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileExplorerTree->header()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_fileExplorerTree->header()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    m_fileExplorerTree->setMinimumWidth(320);
    m_fileExplorerTree->setIndentation(14);
    m_fileExplorerTree->setUniformRowHeights(true);
    m_fileExplorerTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileExplorerTree, &QWidget::customContextMenuRequested, this,
            &MainWindow::showFileExplorerMenu);
    // Open/closed folder glyphs, shared with the repo and Cove explorers.
    connectFileTreeFolderIcons(m_fileExplorerTree,
                               [this](bool opened) { return iconForDir(opened); });
    // Directories fill in the first time they are opened (see
    // populateFileExplorerItem), so browsing a large tree costs one readdir per
    // folder actually visited.
    connect(m_fileExplorerTree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) { populateFileExplorerItem(item); });
    connect(m_fileExplorerTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                previewFileExplorerFile(
                    item ? item->data(0, kFileTreePathRole).toString() : QString());
            });
    // Double-click descends into a directory (it becomes the new root) or opens
    // the file in an editor tab beside the tree — the same tabs the repository
    // side opens into, so one file is never open twice in two places.
    connect(m_fileExplorerTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                const QString path = item->data(0, kFileTreePathRole).toString();
                if (path.isEmpty())
                    return;
                if (fileTreeIsDir(item))
                    setFileExplorerRoot(path);
                else
                    openLocalFileTab(path);
            });

    // The repository's tracked tree, stacked behind the filesystem one: the
    // "Repo" toggle above swaps them, and every other root control swaps back.
    m_repoFileTree = new QTreeWidget;
    m_repoFileTree->setObjectName(QStringLiteral("fileTree"));
    enableHoverRowHighlight(m_repoFileTree);
    // Two columns: name (stretch) + a thin, right-aligned size column.
    m_repoFileTree->setColumnCount(2);
    m_repoFileTree->setHeaderHidden(true);
    m_repoFileTree->header()->setStretchLastSection(false);
    m_repoFileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_repoFileTree->header()->setSectionResizeMode(1,
                                                   QHeaderView::ResizeToContents);
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
    connect(m_repoFileTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (item && !item->data(0, Qt::UserRole + 1).toBool())
                    openRepoFile(item->data(0, Qt::UserRole).toString());
            });
    connectFileTreeFolderIcons(m_repoFileTree,
                               [this](bool opened) { return iconForDir(opened); });

    m_filesTreeStack = new CurrentPageStack;
    m_filesTreeStack->addWidget(m_fileExplorerTree); // 0 filesystem
    m_filesTreeStack->addWidget(m_repoFileTree);     // 1 repository
    splitter->addWidget(m_filesTreeStack);

    m_repoFileTabs = new QTabWidget;
    m_repoFileTabs->setObjectName(QStringLiteral("fileTabs"));
    m_repoFileTabs->setDocumentMode(true);
    m_repoFileTabs->setMovable(true);
    m_repoFileTabs->setTabsClosable(true);
    connect(m_repoFileTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *w = m_repoFileTabs->widget(index);
        if (!w || w == m_fileExplorerPreview)
            return; // the preview tab is permanent and carries no close button
        m_openFileTabs.remove(m_openFileTabs.key(w));
        m_repoFileTabs->removeTab(index);
        w->deleteLater();
        updateRepoFileSaveActions();
    });
    connect(m_repoFileTabs, &QTabWidget::currentChanged, this, [this] {
        updateRepoFileSaveActions();
        // Each open file is a place on the Back/Forward trail, so opening one
        // from the file list (or switching tabs) is a step (adhoc #50).
        scheduleNavRecord();
    });
    // Ctrl+S saves the current tab: commit direct when this node owns a working
    // tree, otherwise fall back to opening a PR (the only save path on a mirror).
    auto *saveShortcut = new QShortcut(QKeySequence::Save, m_repoFileTabs);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] {
        if (m_repoFileCommitButton && m_repoFileCommitButton->isEnabled())
            saveCurrentRepoFile(false);
        else if (m_repoFilePullButton && m_repoFilePullButton->isEnabled())
            saveCurrentRepoFile(true);
    });

    // Whatever the filesystem tree has selected rides in the first tab rather
    // than in a pane of its own, so selecting rows with the arrow keys never
    // piles up tabs and the editor still gets the full width. It is permanent:
    // no close button, and clearing the open files leaves it standing.
    m_fileExplorerPreview = new QPlainTextEdit;
    m_fileExplorerPreview->setObjectName(QStringLiteral("filesPreview"));
    m_fileExplorerPreview->setReadOnly(true);
    m_fileExplorerPreview->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_fileExplorerPreview->setPlaceholderText(
        QStringLiteral("Select a file to preview it here, or double-click one to "
                       "open it in a tab."));
    const int previewTab = m_repoFileTabs->addTab(m_fileExplorerPreview,
                                                  QStringLiteral("Preview"));
    m_repoFileTabs->setTabToolTip(
        previewTab, QStringLiteral("The selected file, read-only"));
    for (QTabBar::ButtonPosition side : {QTabBar::LeftSide, QTabBar::RightSide})
        m_repoFileTabs->tabBar()->setTabButton(previewTab, side, nullptr);
    splitter->addWidget(m_repoFileTabs);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({420, 620});
    outer->addWidget(splitter, 1);

    // First root: whatever was browsed last, else the open repository's working
    // tree, else home.
    QString root =
        QSettings().value(QLatin1String(kFilesRootSetting)).toString();
    if (root.isEmpty() || !QDir(root).exists())
        root = repoGitDir();
    if (root.isEmpty() || !QDir(root).exists())
        root = QDir::homePath();
    setFileExplorerRoot(root);
    updateRepoFileSaveActions();
    updateFilesCommitActions();
    return page;
}

// The row between the path bar and the splitter: what the open file can do
// (markdown preview, history, commit direct, save as PR) then what the working
// tree can do (a commit message, Commit, Push). Both used to be a page apart —
// the file actions on the Code page's Explorer, the commit on Source Control.
QLayout *MainWindow::buildFilesEditorBar()
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    // Markdown files get a tiny toggle that flips between the raw source and a
    // rendered preview (README.md, docs, any *.md / *.markdown).
    m_repoFilePreviewButton = new QPushButton(QStringLiteral("Preview"));
    m_repoFilePreviewButton->setObjectName(QStringLiteral("ghostButton"));
    m_repoFilePreviewButton->setCursor(Qt::PointingHandCursor);
    m_repoFilePreviewButton->setCheckable(true);
    m_repoFilePreviewButton->setToolTip(QStringLiteral("Preview rendered Markdown"));
    setOcticon(m_repoFilePreviewButton, "eye", 16);
    connect(m_repoFilePreviewButton, &QPushButton::clicked, this,
            &MainWindow::toggleRepoFileMarkdownPreview);

    m_repoFileHistoryButton = new QPushButton(QStringLiteral("Show history"));
    m_repoFileHistoryButton->setObjectName(QStringLiteral("ghostButton"));
    m_repoFileHistoryButton->setCursor(Qt::PointingHandCursor);
    m_repoFileHistoryButton->setToolTip(
        QStringLiteral("Show the commit history and changes for this file"));
    setOcticon(m_repoFileHistoryButton, "history", 16);
    connect(m_repoFileHistoryButton, &QPushButton::clicked, this, [this] {
        QWidget *w = m_repoFileTabs ? m_repoFileTabs->currentWidget() : nullptr;
        const QString path = w ? w->property("previewPath").toString() : QString();
        if (!path.isEmpty())
            showRepoFileHistory(path);
    });

    m_repoFileCommitButton = new QPushButton(QStringLiteral("Commit direct"));
    m_repoFileCommitButton->setObjectName(QStringLiteral("ghostButton"));
    m_repoFileCommitButton->setCursor(Qt::PointingHandCursor);
    m_repoFileCommitButton->setToolTip(QStringLiteral(
        "Save this file and commit it directly to the default branch"));
    setOcticon(m_repoFileCommitButton, "upload", 16);
    connect(m_repoFileCommitButton, &QPushButton::clicked, this,
            [this] { saveCurrentRepoFile(false); });

    m_repoFilePullButton = new QPushButton(QStringLiteral("Save as PR"));
    m_repoFilePullButton->setObjectName(QStringLiteral("ghostButton"));
    m_repoFilePullButton->setCursor(Qt::PointingHandCursor);
    m_repoFilePullButton->setToolTip(QStringLiteral(
        "Save this file on a new branch and open a pull request"));
    setOcticon(m_repoFilePullButton, "git-pull-request", 16);
    connect(m_repoFilePullButton, &QPushButton::clicked, this,
            [this] { saveCurrentRepoFile(true); });

    row->addWidget(m_repoFilePreviewButton);
    row->addWidget(m_repoFileHistoryButton);
    row->addWidget(m_repoFileCommitButton);
    row->addWidget(m_repoFilePullButton);
    row->addStretch(1);

    // Working-tree commit + push. This stages everything and commits it, which
    // is what Source Control's "Stage & push" does — the difference is only that
    // the message lives here, beside the file being edited.
    m_filesCommitMessage = new QLineEdit;
    m_filesCommitMessage->setObjectName(QStringLiteral("filesCommitMessage"));
    m_filesCommitMessage->setPlaceholderText(
        QStringLiteral("Commit message for all changes\xE2\x80\xA6"));
    m_filesCommitMessage->setClearButtonEnabled(true);
    m_filesCommitMessage->setMinimumWidth(240);
    connect(m_filesCommitMessage, &QLineEdit::returnPressed, this,
            &MainWindow::commitFilesTabChanges);
    row->addWidget(m_filesCommitMessage);

    m_filesCommitButton = new QPushButton(QStringLiteral("Commit"));
    m_filesCommitButton->setObjectName(QStringLiteral("ghostButton"));
    m_filesCommitButton->setCursor(Qt::PointingHandCursor);
    m_filesCommitButton->setToolTip(QStringLiteral(
        "Stage every change in the working tree and commit it with the message "
        "beside this button"));
    setOcticon(m_filesCommitButton, "git-commit", 16);
    connect(m_filesCommitButton, &QPushButton::clicked, this,
            &MainWindow::commitFilesTabChanges);
    row->addWidget(m_filesCommitButton);

    m_filesPushButton = new QPushButton(QStringLiteral("Push"));
    m_filesPushButton->setObjectName(QStringLiteral("primaryButton"));
    m_filesPushButton->setCursor(Qt::PointingHandCursor);
    m_filesPushButton->setToolTip(QStringLiteral(
        "Publish the commits on this branch to the network mirror (or push to "
        "the upstream branch)"));
    setOcticon(m_filesPushButton, "repo-push", 16);
    connect(m_filesPushButton, &QPushButton::clicked, this,
            &MainWindow::pushCurrentRepoUpstream);
    row->addWidget(m_filesPushButton);
    return row;
}

// Commit everything in the working tree with the message from the Files page's
// own box, then clear it. Push stays a separate click, exactly as it is in
// Source Control, so a commit is never published by accident.
void MainWindow::commitFilesTabChanges()
{
    if (!m_filesCommitMessage)
        return;
    if (commitWorkingTree(m_filesCommitMessage->text()))
        m_filesCommitMessage->clear();
    updateFilesCommitActions();
}

// Commit and push need a working tree this node owns; a mirror has none, and
// the file-level "Save as PR" is the only way to change it.
void MainWindow::updateFilesCommitActions()
{
    const bool owned = !repoGitDir().isEmpty() && repoHasWorkingTree();
    if (m_filesCommitMessage)
        m_filesCommitMessage->setEnabled(owned);
    if (m_filesCommitButton)
        m_filesCommitButton->setEnabled(owned);
    if (m_filesPushButton)
        m_filesPushButton->setEnabled(owned);
}

// Show the repository's tracked tree, building it for the open repo the first
// time. Nothing is loaded until this destination is actually asked for.
void MainWindow::showFilesRepoTree()
{
    if (!m_filesTreeStack)
        return;
    if (m_treeLoadedForIndex != m_repoDetailIndex) {
        loadRepoFileTree();
        m_treeLoadedForIndex = m_repoDetailIndex;
    }
    m_filesTreeStack->setCurrentIndex(1);
    if (m_filesRepoTreeButton)
        m_filesRepoTreeButton->setChecked(true);
    if (m_fileExplorerStatus)
        m_fileExplorerStatus->setText(
            repoGitDir().isEmpty()
                ? QStringLiteral("No local copy of a repository is open.")
                : QStringLiteral("Tracked files at %1").arg(currentRef()));
}

void MainWindow::showFilesDiskTree()
{
    if (m_filesTreeStack)
        m_filesTreeStack->setCurrentIndex(0);
    if (m_filesRepoTreeButton)
        m_filesRepoTreeButton->setChecked(false);
}

void MainWindow::refreshFilesPage()
{
    if (!m_fileExplorerTree)
        return;
    // Whether the working tree can be committed to depends on the repository
    // that is open, which can have changed while the page was away.
    updateFilesCommitActions();
    // The repository tree keeps its place: it is rebuilt when the open repo
    // changes (showFilesRepoTree) and by the Refresh button, not on every
    // return to the page.
    if (m_filesTreeStack && m_filesTreeStack->currentIndex() == 1)
        return;
    // Navigating back to the page keeps whatever was expanded; the Refresh
    // button is there for an explicit re-read.
    if (QDir(m_fileExplorerRoot).exists()) {
        if (m_fileExplorerTree->topLevelItemCount() == 0)
            setFileExplorerRoot(m_fileExplorerRoot);
        return;
    }
    // The root has since disappeared (an unmounted volume, a deleted worktree).
    // Re-listing it would only hit setFileExplorerRoot's own exists() guard and
    // leave the vanished directory's rows on screen, so fall back to somewhere
    // that does exist rather than showing phantom entries.
    QString fallback = repoGitDir();
    if (fallback.isEmpty() || !QDir(fallback).exists())
        fallback = QDir::homePath();
    setFileExplorerRoot(fallback);
}

QFileInfoList MainWindow::fileExplorerEntries(const QString &dir) const
{
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (!m_fileExplorerHidden || m_fileExplorerHidden->isChecked())
        filters |= QDir::Hidden | QDir::System;
    return QDir(dir).entryInfoList(filters,
                                   QDir::DirsFirst | QDir::Name |
                                       QDir::IgnoreCase | QDir::LocaleAware);
}

void MainWindow::setFileExplorerRoot(const QString &path)
{
    if (!m_fileExplorerTree)
        return;

    QString target = QDir::cleanPath(path.trimmed());
    if (target.startsWith(QLatin1Char('~')))
        target = QDir::homePath() + target.mid(1);
    if (target.isEmpty())
        target = QDir::homePath();
    const QFileInfo info(target);
    // Pointing the path bar at a file is a reasonable thing to do by accident
    // (or by paste); browse its directory and select it rather than refusing.
    QString selectPath;
    if (info.isFile()) {
        selectPath = info.absoluteFilePath();
        target = info.absolutePath();
    }
    if (!QDir(target).exists()) {
        if (m_fileExplorerStatus)
            m_fileExplorerStatus->setText(
                QStringLiteral("No such directory: %1").arg(target));
        if (m_fileExplorerPath)
            m_fileExplorerPath->setText(m_fileExplorerRoot);
        return;
    }
    target = QDir(target).absolutePath();
    // Re-rooting is a filesystem operation, so it is also what brings the
    // filesystem tree back to the front when the repository one is showing.
    showFilesDiskTree();

    const QString previousRoot = m_fileExplorerRoot;
    m_fileExplorerRoot = target;
    QSettings().setValue(QLatin1String(kFilesRootSetting), target);
    if (m_fileExplorerPath && m_fileExplorerPath->text() != target)
        m_fileExplorerPath->setText(target);

    // Re-listing the same directory (Refresh, or the hidden toggle) keeps the
    // folders the user had open and the scroll offset, the same way the repo
    // Explorer does — see restoreFileTreeExpandedPaths.
    const bool sameRoot = (previousRoot == target);
    const QSet<QString> expanded =
        sameRoot ? fileTreeExpandedPaths(m_fileExplorerTree) : QSet<QString>();
    const int scrollOffset =
        sameRoot ? fileTreeScrollOffset(m_fileExplorerTree) : 0;

    m_fileExplorerTree->clear();
    const QFileInfoList entries = fileExplorerEntries(target);
    int hidden = 0;
    for (const QFileInfo &entry : entries) {
        if (entry.isHidden())
            ++hidden;
        addFileExplorerRow(nullptr, entry);
    }

    if (entries.isEmpty()) {
        auto *empty = new QTreeWidgetItem(m_fileExplorerTree,
                                          {QStringLiteral("(empty directory)")});
        empty->setFlags(Qt::NoItemFlags);
    }

    if (m_fileExplorerStatus) {
        // "12 entries · 3 hidden", or "12 entries · hidden not shown" when the
        // toggle is off — the count and the word have to move together.
        const QString hiddenText =
            m_fileExplorerHidden && !m_fileExplorerHidden->isChecked()
                ? QStringLiteral("hidden not shown")
                : QStringLiteral("%1 hidden").arg(hidden);
        m_fileExplorerStatus->setText(
            QStringLiteral("%1 %2 · %3")
                .arg(entries.size())
                .arg(entries.size() == 1 ? QStringLiteral("entry")
                                         : QStringLiteral("entries"),
                     hiddenText));
    }
    previewFileExplorerFile(QString());
    // Children load on expand, so re-opening a folder has to re-read it.
    restoreFileTreeExpandedPaths(
        m_fileExplorerTree, expanded, scrollOffset,
        [this](QTreeWidgetItem *item) { populateFileExplorerItem(item); });

    if (selectPath.isEmpty())
        return;
    for (int i = 0; i < m_fileExplorerTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_fileExplorerTree->topLevelItem(i);
        if (item->data(0, kFileTreePathRole).toString() != selectPath)
            continue;
        m_fileExplorerTree->setCurrentItem(item);
        m_fileExplorerTree->scrollToItem(item);
        break;
    }
}

// One row of the explorer, used for both the top level (`parent` null) and for
// lazily-loaded children, so the two paths cannot drift in icon, columns or
// placeholder handling.
QTreeWidgetItem *MainWindow::addFileExplorerRow(QTreeWidgetItem *parent,
                                                const QFileInfo &entry)
{
    auto *item = parent ? new QTreeWidgetItem(parent)
                        : new QTreeWidgetItem(m_fileExplorerTree);
    const bool isDir = entry.isDir();
    item->setText(0, entry.fileName());
    item->setData(0, kFileTreePathRole, entry.absoluteFilePath());
    item->setData(0, kFileTreeIsDirRole, isDir);
    item->setIcon(0, isDir ? iconForDir(false) : iconForFile(entry.fileName()));
    if (!isDir)
        setFileTreeSizeCell(item, 1, formatByteSize(entry.size()));
    item->setText(2, modifiedLabel(entry));
    item->setToolTip(0, entry.absoluteFilePath());
    // The placeholder child is what draws the expand arrow before the directory
    // has been read. Symlinked directories never get one, so a link pointing at
    // an ancestor cannot loop the tree; an unreadable one does, and says so in
    // place when it is opened rather than silently collapsing again.
    if (isDir && !entry.isSymLink())
        item->addChild(new QTreeWidgetItem);
    return item;
}

void MainWindow::populateFileExplorerItem(QTreeWidgetItem *item)
{
    if (!fileTreeIsDir(item) || item->data(0, kFileLoadedRole).toBool())
        return;
    item->setData(0, kFileLoadedRole, true);
    qDeleteAll(item->takeChildren()); // drop the expand-arrow placeholder

    const QString dir = fileTreePath(item);
    const QFileInfoList entries = fileExplorerEntries(dir);
    if (entries.isEmpty()) {
        const bool readable = QFileInfo(dir).isReadable();
        auto *note = new QTreeWidgetItem(
            item, {readable ? QStringLiteral("(empty)")
                            : QStringLiteral("(not readable)")});
        note->setFlags(Qt::NoItemFlags);
        return;
    }
    for (const QFileInfo &entry : entries)
        addFileExplorerRow(item, entry);
}

void MainWindow::previewFileExplorerFile(const QString &path, int line)
{
    if (!m_fileExplorerPreview)
        return;
    m_fileExplorerPreview->setExtraSelections({});
    if (path.isEmpty()) {
        m_fileExplorerPreview->clear();
        return;
    }
    // Raise the preview tab, but only for a real file: clearing it (a re-root)
    // must not pull the user off the file they have open in another tab.
    if (m_repoFileTabs)
        m_repoFileTabs->setCurrentWidget(m_fileExplorerPreview);
    const QFileInfo info(path);
    const QString header =
        QStringLiteral("%1\n%2 · %3\n\n")
            .arg(info.absoluteFilePath(),
                 info.isDir() ? QStringLiteral("directory")
                              : formatByteSize(info.size()),
                 modifiedLabel(info));
    // Selection follows the arrow keys, so this runs on the GUI thread for
    // every row the cursor passes over. Counting a directory's entries here
    // would mean a readdir + stat per keypress, which stalls on a large one —
    // expanding the folder is the way to see what is in it.
    if (info.isDir()) {
        m_fileExplorerPreview->setPlainText(header +
                                            QStringLiteral("Expand to browse."));
        return;
    }
    // Only regular files are opened. The listing includes QDir::System, so a
    // selection can land on a FIFO or a socket — and opening one of those
    // blocks in the kernel until a writer appears, freezing the whole window.
    // Qt reports them as neither file nor directory.
    if (!info.isFile()) {
        m_fileExplorerPreview->setPlainText(
            header + QStringLiteral("Not a regular file — no preview."));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_fileExplorerPreview->setPlainText(
            header + QStringLiteral("Could not read this file: %1")
                         .arg(file.errorString()));
        return;
    }
    const QByteArray head = file.read(kPreviewByteLimit);
    if (fileContentIsBinary(head)) {
        m_fileExplorerPreview->setPlainText(
            header + QStringLiteral("Binary file — no preview."));
        return;
    }
    QString text = QString::fromUtf8(head);
    if (info.size() > kPreviewByteLimit) {
        text += QStringLiteral("\n\n… truncated at %1 of %2.")
                    .arg(formatByteSize(kPreviewByteLimit),
                         formatByteSize(info.size()));
    }
    m_fileExplorerPreview->setPlainText(header + text);
    if (line > 0)
        highlightFileExplorerPreviewLine(line);
}

// Park the preview on one line and tint it, for the callers that arrive with a
// line in hand — a log entry's origin, so far (adhoc #1587). Line 1 of the file
// is the block after the three-line header previewFileExplorerFile() writes.
void MainWindow::highlightFileExplorerPreviewLine(int line)
{
    if (!m_fileExplorerPreview || line <= 0)
        return;
    QTextDocument *doc = m_fileExplorerPreview->document();
    const int block = kFilePreviewHeaderLines + line - 1;
    if (block >= doc->blockCount())
        return; // past the end (a truncated preview, or a stale line number)

    QTextCursor cursor(doc->findBlockByNumber(block));
    m_fileExplorerPreview->setTextCursor(cursor);
    m_fileExplorerPreview->centerCursor();

    QTextEdit::ExtraSelection selection;
    selection.cursor = cursor;
    selection.format.setBackground(
        currentThemeIsDark() ? QColor(56, 47, 12) : QColor(255, 244, 191));
    // Full-width, so the line reads as picked out rather than as a text
    // selection the next keypress would replace.
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    m_fileExplorerPreview->setExtraSelections({selection});
}

// Open an absolute path from the filesystem tree in an editor tab. A file the
// open repository tracks goes through openRepoFile, so it is editable and the
// commit / pull-request actions apply to it; anything else — an untracked file,
// a build output, something in another directory entirely — opens read-only,
// because there is no branch to commit it onto.
void MainWindow::openLocalFileTab(const QString &path)
{
    if (path.isEmpty())
        return;
    // The tabs live on a lazily-built section, and openRepoFile routes absolute
    // paths here from anywhere in the app.
    ensureSectionBuilt(kFilesSectionIndex);
    if (!m_repoFileTabs)
        return;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const QString dir = repoGitDir();
    if (!dir.isEmpty()) {
        const QString rel = QDir(dir).relativeFilePath(absolute);
        if (!rel.startsWith(QLatin1String("../")) &&
            !QDir::isAbsolutePath(rel) &&
            runGitCapture(dir, {"ls-files", "--error-unmatch", "--", rel}, nullptr,
                          nullptr)) {
            openRepoFile(rel);
            return;
        }
    }

    if (m_openFileTabs.contains(absolute)) {
        m_repoFileTabs->setCurrentWidget(m_openFileTabs.value(absolute));
        showSection(kFilesSectionIndex);
        return;
    }
    const QFileInfo info(absolute);
    QString content;
    QFile file(absolute);
    // Only regular files are read: the listing includes QDir::System, and
    // opening a FIFO blocks in the kernel until a writer appears.
    if (!info.isFile()) {
        content = QStringLiteral("Not a regular file — nothing to open.");
    } else if (!file.open(QIODevice::ReadOnly)) {
        content = QStringLiteral("Could not read this file: %1")
                      .arg(file.errorString());
    } else {
        const QByteArray head = file.read(kPreviewByteLimit);
        if (fileContentIsBinary(head)) {
            content = QStringLiteral("Binary file — not shown.");
        } else {
            content = QString::fromUtf8(head);
            if (info.size() > kPreviewByteLimit)
                content += QStringLiteral("\n\n… truncated at %1 of %2.")
                               .arg(formatByteSize(kPreviewByteLimit),
                                    formatByteSize(info.size()));
        }
    }

    auto *editor = new CodePreviewEditor(absolute);
    editor->setReadOnly(true);
    editor->setPlainText(content);
    editor->document()->setModified(false);
    new CodePreviewHighlighter(editor->document(), absolute);

    const QString name = info.fileName();
    const int index = m_repoFileTabs->addTab(editor, iconForFile(name), name);
    m_repoFileTabs->setTabToolTip(index, absolute);
    m_repoFileTabs->setCurrentIndex(index);
    m_openFileTabs.insert(absolute, editor);
    updateRepoFileSaveActions();
    showSection(kFilesSectionIndex);
}

// Drop every open file, leaving the permanent preview tab in place. Opening a
// different repository calls this: the old repo's tabs are meaningless once the
// paths they name belong to something else.
void MainWindow::clearRepoFileTabs()
{
    if (!m_repoFileTabs)
        return;
    for (int i = m_repoFileTabs->count() - 1; i >= 0; --i) {
        QWidget *w = m_repoFileTabs->widget(i);
        if (w == m_fileExplorerPreview)
            continue;
        m_repoFileTabs->removeTab(i);
        w->deleteLater();
    }
    m_openFileTabs.clear();
    updateRepoFileSaveActions();
}

// Show `path` in the Files section: the explorer rooted on its directory with
// the file selected, and the preview open (at `line` when there is one).
void MainWindow::revealFileInExplorer(const QString &path, int line)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        flashMessage(QStringLiteral("No such file: %1").arg(path), true);
        return;
    }
    showSection(kFilesSectionIndex);
    if (m_filesNavButton)
        m_filesNavButton->setChecked(true);
    // Re-roots on the containing directory and selects the row (see
    // setFileExplorerRoot); the preview follows that selection, but is set
    // explicitly here because only this path knows the line.
    setFileExplorerRoot(info.absoluteFilePath());
    previewFileExplorerFile(info.absoluteFilePath(), line);
}

void MainWindow::revealLogSourceInExplorer(const QString &relativePath, int line)
{
    // The path stored in the log is relative to the checkout the binary was
    // built from. That checkout is the first place to look; the open repository
    // is the second, which is what makes this work for a build running against
    // a clone somewhere else.
    QStringList roots;
    const QString repo = repoGitDir();
    if (!repo.isEmpty())
        roots << repo;
    const QString resolved = forkmesh::resolveLogSourcePath(relativePath, roots);
    if (resolved.isEmpty()) {
        flashMessage(QStringLiteral("%1 isn't on this machine — the log entry "
                                    "was written by a build from another "
                                    "checkout.")
                         .arg(relativePath),
                     true);
        return;
    }
    revealFileInExplorer(resolved, line);
}

#ifdef FORKMESH_WINDOW_TESTS
bool MainWindow::testFilesNavButtonVisible() const
{
    return m_filesNavButton && !m_filesNavButton->isHidden();
}

void MainWindow::testSetFileExplorerShowHidden(bool on)
{
    if (m_fileExplorerHidden)
        m_fileExplorerHidden->setChecked(on);
}

QStringList MainWindow::testFileExplorerNames() const
{
    QStringList names;
    if (!m_fileExplorerTree)
        return names;
    for (int i = 0; i < m_fileExplorerTree->topLevelItemCount(); ++i)
        names.append(m_fileExplorerTree->topLevelItem(i)->text(0));
    return names;
}

QString MainWindow::testFileExplorerPreview() const
{
    return m_fileExplorerPreview ? m_fileExplorerPreview->toPlainText() : QString();
}

int MainWindow::testFileExplorerPreviewLine() const
{
    if (!m_fileExplorerPreview || m_fileExplorerPreview->extraSelections().isEmpty())
        return 0;
    const int block =
        m_fileExplorerPreview->extraSelections().first().cursor.blockNumber();
    return block < kFilePreviewHeaderLines
               ? 0
               : block - kFilePreviewHeaderLines + 1;
}

QStringList MainWindow::testExpandedFileExplorerPaths() const
{
    QStringList paths(fileTreeExpandedPaths(m_fileExplorerTree).values());
    paths.sort();
    return paths;
}

QStringList MainWindow::testExpandFileExplorerEntry(const QString &name)
{
    QStringList names;
    if (!m_fileExplorerTree)
        return names;
    for (int i = 0; i < m_fileExplorerTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_fileExplorerTree->topLevelItem(i);
        if (item->text(0) != name)
            continue;
        item->setExpanded(true);
        populateFileExplorerItem(item);
        for (int child = 0; child < item->childCount(); ++child)
            names.append(item->child(child)->text(0));
        break;
    }
    return names;
}
#endif

void MainWindow::showFileExplorerMenu(const QPoint &pos)
{
    if (!m_fileExplorerTree)
        return;
    QTreeWidgetItem *item = m_fileExplorerTree->itemAt(pos);
    // Placeholder rows ("(empty directory)", "(not readable)") carry no path;
    // fall back to the directory being listed, as the repo tree menu does.
    QString path = fileTreePath(item);
    if (path.isEmpty())
        path = m_fileExplorerRoot;
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    const bool isDir = info.isDir();

    QMenu menu(this);
    QAction *open = menu.addAction(
        isDir ? QStringLiteral("Browse here") : QStringLiteral("Open in a tab"));
    QAction *desktop =
        isDir ? nullptr
              : menu.addAction(QStringLiteral("Open with the desktop app"));
    QAction *reveal = menu.addAction(
        QStringLiteral("Show in desktop file manager"));
    menu.addSeparator();
    QAction *copyPath = menu.addAction(QStringLiteral("Copy path"));
    QAction *copyName = menu.addAction(QStringLiteral("Copy name"));

    QAction *chosen = menu.exec(m_fileExplorerTree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == open) {
        if (isDir)
            setFileExplorerRoot(path);
        else
            openLocalFileTab(path);
    } else if (chosen == desktop) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else if (chosen == reveal) {
        revealInDesktopFileManager(path);
    } else if (chosen == copyPath) {
        QGuiApplication::clipboard()->setText(info.absoluteFilePath());
        flashMessage(QStringLiteral("Path copied."));
    } else if (chosen == copyName) {
        QGuiApplication::clipboard()->setText(info.fileName());
        flashMessage(QStringLiteral("Name copied."));
    }
}
