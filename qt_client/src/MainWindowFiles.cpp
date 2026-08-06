// Files: a plain local file explorer on its own activity-rail destination,
// directly under Network / Users. The repo Explorer only ever shows paths git
// tracks in the open repository; this one browses the real filesystem, and it
// shows hidden entries by default because .git, .forkmesh and dotfiles are the
// usual reason for opening it at all.

#include "MainWindow.h"
#include "MainWindowInternal.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace forkmesh::ui;

namespace {
// Item data roles on every row of the explorer tree.
constexpr int kFilePathRole = Qt::UserRole;      // absolute path
constexpr int kFileIsDirRole = Qt::UserRole + 1; // directory rows expand
constexpr int kFileLoadedRole = Qt::UserRole + 2; // children already read

const char *kFilesRootSetting = "files/explorerRoot";
const char *kFilesHiddenSetting = "files/showHidden";

// Preview cap. Enough to read a config file or a script; short of the point
// where dropping a multi-megabyte log into a QPlainTextEdit stalls the UI.
constexpr qint64 kPreviewByteLimit = 256 * 1024;

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

    auto *repoButton = new QPushButton(QStringLiteral("Repo"));
    repoButton->setObjectName(QStringLiteral("ghostButton"));
    repoButton->setCursor(Qt::PointingHandCursor);
    repoButton->setToolTip(
        QStringLiteral("Browse the working tree of the open repository"));
    setOcticon(repoButton, "repo", 14);
    connect(repoButton, &QPushButton::clicked, this, [this] {
        const QString dir = repoGitDir();
        if (dir.isEmpty())
            flashMessage(QStringLiteral("No local copy of a repository is open."),
                         true);
        else
            setFileExplorerRoot(dir);
    });
    heading->addWidget(repoButton);

    auto *chooseButton = new QPushButton(QStringLiteral("Choose"));
    chooseButton->setObjectName(QStringLiteral("ghostButton"));
    chooseButton->setCursor(Qt::PointingHandCursor);
    chooseButton->setToolTip(QStringLiteral("Pick a directory to browse"));
    setOcticon(chooseButton, "file-directory", 14);
    connect(chooseButton, &QPushButton::clicked, this, [this] {
        // The picker lists hidden directories too, so it can reach the same
        // places the tree below it shows.
        QFileDialog dialog(this, QStringLiteral("Choose a directory"),
                           m_fileExplorerRoot);
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::ShowDirsOnly, true);
        dialog.setFilter(QDir::AllDirs | QDir::Drives | QDir::NoDotAndDotDot |
                         QDir::Hidden);
        if (dialog.exec() == QDialog::Accepted &&
            !dialog.selectedFiles().isEmpty())
            setFileExplorerRoot(dialog.selectedFiles().first());
    });
    heading->addWidget(chooseButton);

    auto *refreshButton = new QPushButton(QStringLiteral("Refresh"));
    refreshButton->setObjectName(QStringLiteral("ghostButton"));
    refreshButton->setCursor(Qt::PointingHandCursor);
    refreshButton->setToolTip(QStringLiteral("Re-read the current directory"));
    setOcticon(refreshButton, "sync", 14);
    connect(refreshButton, &QPushButton::clicked, this,
            [this] { setFileExplorerRoot(m_fileExplorerRoot); });
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
    // Directories fill in the first time they are opened (see
    // populateFileExplorerItem), so browsing a large tree costs one readdir per
    // folder actually visited.
    connect(m_fileExplorerTree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) {
                populateFileExplorerItem(item);
                if (item && item->data(0, kFileIsDirRole).toBool())
                    item->setIcon(0, iconForDir(true));
            });
    connect(m_fileExplorerTree, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) {
                if (item && item->data(0, kFileIsDirRole).toBool())
                    item->setIcon(0, iconForDir(false));
            });
    connect(m_fileExplorerTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                previewFileExplorerFile(
                    item ? item->data(0, kFilePathRole).toString() : QString());
            });
    // Double-click descends into a directory (it becomes the new root) or hands
    // a file to whatever the desktop opens it with.
    connect(m_fileExplorerTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                const QString path = item->data(0, kFilePathRole).toString();
                if (path.isEmpty())
                    return;
                if (item->data(0, kFileIsDirRole).toBool())
                    setFileExplorerRoot(path);
                else
                    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            });
    splitter->addWidget(m_fileExplorerTree);

    m_fileExplorerPreview = new QPlainTextEdit;
    m_fileExplorerPreview->setObjectName(QStringLiteral("filesPreview"));
    m_fileExplorerPreview->setReadOnly(true);
    m_fileExplorerPreview->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_fileExplorerPreview->setPlaceholderText(
        QStringLiteral("Select a file to preview it here."));
    splitter->addWidget(m_fileExplorerPreview);
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
    return page;
}

void MainWindow::refreshFilesPage()
{
    if (!m_fileExplorerTree)
        return;
    // Navigating back to the page keeps whatever was expanded; only a root that
    // has since disappeared (an unmounted volume, a deleted worktree) forces a
    // rebuild. The Refresh button is there for an explicit re-read.
    if (m_fileExplorerTree->topLevelItemCount() == 0 ||
        !QDir(m_fileExplorerRoot).exists())
        setFileExplorerRoot(m_fileExplorerRoot);
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

    m_fileExplorerRoot = target;
    QSettings().setValue(QLatin1String(kFilesRootSetting), target);
    if (m_fileExplorerPath && m_fileExplorerPath->text() != target)
        m_fileExplorerPath->setText(target);

    m_fileExplorerTree->clear();
    const QFileInfoList entries = fileExplorerEntries(target);
    int hidden = 0;
    for (const QFileInfo &entry : entries) {
        if (entry.isHidden())
            ++hidden;
        auto *item = new QTreeWidgetItem(m_fileExplorerTree);
        item->setText(0, entry.fileName());
        item->setData(0, kFilePathRole, entry.absoluteFilePath());
        const bool isDir = entry.isDir();
        item->setData(0, kFileIsDirRole, isDir);
        item->setIcon(0, isDir ? iconForDir(false) : iconForFile(entry.fileName()));
        if (!isDir) {
            item->setText(1, formatByteSize(entry.size()));
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        }
        item->setText(2, modifiedLabel(entry));
        item->setToolTip(0, entry.absoluteFilePath());
        // An unreadable directory still gets its arrow; opening it reports the
        // failure in place instead of silently collapsing again.
        if (isDir)
            item->addChild(new QTreeWidgetItem);
    }

    if (entries.isEmpty()) {
        auto *empty = new QTreeWidgetItem(m_fileExplorerTree,
                                          {QStringLiteral("(empty directory)")});
        empty->setFlags(Qt::NoItemFlags);
    }

    if (m_fileExplorerStatus) {
        m_fileExplorerStatus->setText(
            QStringLiteral("%1 %2 · %3 hidden")
                .arg(entries.size())
                .arg(entries.size() == 1 ? QStringLiteral("entry")
                                         : QStringLiteral("entries"))
                .arg(m_fileExplorerHidden && !m_fileExplorerHidden->isChecked()
                         ? QStringLiteral("not shown")
                         : QString::number(hidden)));
    }
    previewFileExplorerFile(QString());

    if (selectPath.isEmpty())
        return;
    for (int i = 0; i < m_fileExplorerTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_fileExplorerTree->topLevelItem(i);
        if (item->data(0, kFilePathRole).toString() != selectPath)
            continue;
        m_fileExplorerTree->setCurrentItem(item);
        m_fileExplorerTree->scrollToItem(item);
        break;
    }
}

void MainWindow::populateFileExplorerItem(QTreeWidgetItem *item)
{
    if (!item || !item->data(0, kFileIsDirRole).toBool() ||
        item->data(0, kFileLoadedRole).toBool())
        return;
    item->setData(0, kFileLoadedRole, true);
    qDeleteAll(item->takeChildren()); // drop the expand-arrow placeholder

    const QString dir = item->data(0, kFilePathRole).toString();
    const QFileInfoList entries = fileExplorerEntries(dir);
    if (entries.isEmpty()) {
        const bool readable = QFileInfo(dir).isReadable();
        auto *note = new QTreeWidgetItem(
            item, {readable ? QStringLiteral("(empty)")
                            : QStringLiteral("(not readable)")});
        note->setFlags(Qt::NoItemFlags);
        return;
    }
    for (const QFileInfo &entry : entries) {
        auto *child = new QTreeWidgetItem(item);
        child->setText(0, entry.fileName());
        child->setData(0, kFilePathRole, entry.absoluteFilePath());
        const bool isDir = entry.isDir();
        child->setData(0, kFileIsDirRole, isDir);
        child->setIcon(0,
                       isDir ? iconForDir(false) : iconForFile(entry.fileName()));
        if (!isDir) {
            child->setText(1, formatByteSize(entry.size()));
            child->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        }
        child->setText(2, modifiedLabel(entry));
        child->setToolTip(0, entry.absoluteFilePath());
        // Symlinked directories are listed but never auto-descended into, so a
        // link pointing at an ancestor cannot loop the tree.
        if (isDir && !entry.isSymLink())
            child->addChild(new QTreeWidgetItem);
    }
}

void MainWindow::previewFileExplorerFile(const QString &path)
{
    if (!m_fileExplorerPreview)
        return;
    if (path.isEmpty()) {
        m_fileExplorerPreview->clear();
        return;
    }
    const QFileInfo info(path);
    const QString header =
        QStringLiteral("%1\n%2 · %3\n\n")
            .arg(info.absoluteFilePath(),
                 info.isDir() ? QStringLiteral("directory")
                              : formatByteSize(info.size()),
                 modifiedLabel(info));
    if (info.isDir()) {
        m_fileExplorerPreview->setPlainText(
            header + QStringLiteral("%1 entries").arg(fileExplorerEntries(path).size()));
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
    // A NUL byte in the head is the cheap, dependable binary test; showing the
    // raw bytes of an executable or an image helps nobody.
    if (head.contains('\0')) {
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
    const QString path = item ? item->data(0, kFilePathRole).toString()
                              : m_fileExplorerRoot;
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    const bool isDir = info.isDir();

    QMenu menu(this);
    QAction *open = menu.addAction(
        isDir ? QStringLiteral("Browse here") : QStringLiteral("Open"));
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
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else if (chosen == reveal) {
        // A file has no folder to open, so reveal its parent directory.
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(isDir ? path : info.absolutePath()));
    } else if (chosen == copyPath) {
        QGuiApplication::clipboard()->setText(info.absoluteFilePath());
        flashMessage(QStringLiteral("Path copied."));
    } else if (chosen == copyName) {
        QGuiApplication::clipboard()->setText(info.fileName());
        flashMessage(QStringLiteral("Name copied."));
    }
}
