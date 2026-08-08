
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
constexpr int kFileLoadedRole = Qt::UserRole + 2;

const char *kFilesRootSetting = "files/explorerRoot";
const char *kFilesHiddenSetting = "files/showHidden";

constexpr qint64 kPreviewByteLimit = 256 * 1024;

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

    m_filesRepoTreeButton = new QPushButton(QStringLiteral("Repo"));
    m_filesRepoTreeButton->setObjectName(QStringLiteral("ghostButton"));
    m_filesRepoTreeButton->setCheckable(true);
    m_filesRepoTreeButton->setCursor(Qt::PointingHandCursor);
    m_filesRepoTreeButton->setToolTip(
        QStringLiteral("Browse the files the open repository tracks"));
    setOcticon(m_filesRepoTreeButton, "repo", 14);
    connect(m_filesRepoTreeButton, &QPushButton::clicked, this, [this] {
        showFilesRepoTree();
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
    connectFileTreeFolderIcons(m_fileExplorerTree,
                               [this](bool opened) { return iconForDir(opened); });
    connect(m_fileExplorerTree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) { populateFileExplorerItem(item); });
    connect(m_fileExplorerTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                previewFileExplorerFile(
                    item ? item->data(0, kFileTreePathRole).toString() : QString());
            });
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

    m_repoFileTree = new QTreeWidget;
    m_repoFileTree->setObjectName(QStringLiteral("fileTree"));
    enableHoverRowHighlight(m_repoFileTree);
    m_repoFileTree->setColumnCount(2);
    m_repoFileTree->setHeaderHidden(true);
    m_repoFileTree->header()->setStretchLastSection(false);
    m_repoFileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_repoFileTree->header()->setSectionResizeMode(1,
                                                   QHeaderView::ResizeToContents);
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
        scheduleNavRecord();
    });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, m_repoFileTabs);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] {
        if (m_repoFileSaveButton && m_repoFileSaveButton->isEnabled())
            saveCurrentRepoFile(false);
        else if (m_repoFileCommitButton && m_repoFileCommitButton->isEnabled())
            saveCurrentRepoFile(false);
        else if (m_repoFilePullButton && m_repoFilePullButton->isEnabled())
            saveCurrentRepoFile(true);
    });

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

QLayout *MainWindow::buildFilesEditorBar()
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

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

    m_repoFileSaveButton = new QPushButton(QStringLiteral("Save"));
    m_repoFileSaveButton->setObjectName(QStringLiteral("primaryButton"));
    m_repoFileSaveButton->setCursor(Qt::PointingHandCursor);
    m_repoFileSaveButton->setToolTip(
        QStringLiteral("Write this file back to the working tree"));
    setOcticon(m_repoFileSaveButton, "check", 16);
    m_repoFileSaveButton->hide();
    connect(m_repoFileSaveButton, &QPushButton::clicked, this,
            [this] { saveCurrentRepoFile(false); });

    row->addWidget(m_repoFilePreviewButton);
    row->addWidget(m_repoFileHistoryButton);
    row->addWidget(m_repoFileCommitButton);
    row->addWidget(m_repoFilePullButton);
    row->addWidget(m_repoFileSaveButton);
    row->addStretch(1);

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

void MainWindow::commitFilesTabChanges()
{
    if (!m_filesCommitMessage)
        return;
    if (commitWorkingTree(m_filesCommitMessage->text()))
        m_filesCommitMessage->clear();
    updateFilesCommitActions();
}

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
    updateFilesCommitActions();
    if (m_filesTreeStack && m_filesTreeStack->currentIndex() == 1)
        return;
    if (QDir(m_fileExplorerRoot).exists()) {
        if (m_fileExplorerTree->topLevelItemCount() == 0)
            setFileExplorerRoot(m_fileExplorerRoot);
        return;
    }
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
    showFilesDiskTree();

    const QString previousRoot = m_fileExplorerRoot;
    m_fileExplorerRoot = target;
    QSettings().setValue(QLatin1String(kFilesRootSetting), target);
    if (m_fileExplorerPath && m_fileExplorerPath->text() != target)
        m_fileExplorerPath->setText(target);

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
    if (m_repoFileTabs)
        m_repoFileTabs->setCurrentWidget(m_fileExplorerPreview);
    const QFileInfo info(path);
    const QString header =
        QStringLiteral("%1\n%2 · %3\n\n")
            .arg(info.absoluteFilePath(),
                 info.isDir() ? QStringLiteral("directory")
                              : formatByteSize(info.size()),
                 modifiedLabel(info));
    if (info.isDir()) {
        m_fileExplorerPreview->setPlainText(header +
                                            QStringLiteral("Expand to browse."));
        return;
    }
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
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    m_fileExplorerPreview->setExtraSelections({selection});
}

void MainWindow::openLocalFileTab(const QString &path)
{
    if (path.isEmpty())
        return;
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
    setFileExplorerRoot(info.absoluteFilePath());
    previewFileExplorerFile(info.absoluteFilePath(), line);
}

void MainWindow::revealLogSourceInExplorer(const QString &relativePath, int line)
{
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
