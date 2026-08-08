#pragma once

// Shared building blocks for every file explorer in the client. The repo
// Explorer (git-tracked paths), the Cove Explorer (encrypted cove documents)
// and the Files section (the real filesystem) each list a different source, but
// the tree they render it into behaves identically: same item-data roles, same
// folder-icon-follows-expansion rule, same size cell, same "reveal this in the
// desktop file manager", same expand-state preservation across a rebuild.
// Those pieces live here so the three explorers cannot drift apart — before
// this they were three hand-copied versions that had already diverged (the
// muted size colour existed in one of them, the directory picker's
// non-native-dialog workaround in another).
// Deliberately standalone: Qt only, no MainWindow*.h. MainWindowInternal.h
// includes it, so it must not include MainWindowInternal.h back.

#include <QBrush>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QScrollBar>
#include <QSet>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>

#include <functional>

namespace forkmesh::ui {

constexpr int kFileTreePathRole = Qt::UserRole;
constexpr int kFileTreeIsDirRole = Qt::UserRole + 1;

inline QString fileTreePath(const QTreeWidgetItem *item)
{
    return item ? item->data(0, kFileTreePathRole).toString() : QString();
}

inline bool fileTreeIsDir(const QTreeWidgetItem *item)
{
    return item && item->data(0, kFileTreeIsDirRole).toBool();
}

inline void setFileTreeSizeCell(QTreeWidgetItem *item, int column,
                                const QString &text)
{
    if (!item)
        return;
    item->setText(column, text);
    item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
    item->setForeground(column, QBrush(QColor("#8b949e")));
}

inline void connectFileTreeFolderIcons(QTreeWidget *tree,
                                       std::function<QIcon(bool)> icon)
{
    if (!tree || !icon)
        return;
    QObject::connect(tree, &QTreeWidget::itemExpanded, tree,
                     [icon](QTreeWidgetItem *item) {
                         if (fileTreeIsDir(item))
                             item->setIcon(0, icon(true));
                     });
    QObject::connect(tree, &QTreeWidget::itemCollapsed, tree,
                     [icon](QTreeWidgetItem *item) {
                         if (fileTreeIsDir(item))
                             item->setIcon(0, icon(false));
                     });
}

inline QSet<QString> fileTreeExpandedPaths(QTreeWidget *tree)
{
    QSet<QString> expanded;
    if (!tree)
        return expanded;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *parent) {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem *child = parent->child(i);
            if (child->isExpanded()) {
                const QString path = fileTreePath(child);
                if (!path.isEmpty())
                    expanded.insert(path);
            }
            walk(child);
        }
    };
    walk(tree->invisibleRootItem());
    return expanded;
}

inline int fileTreeScrollOffset(QTreeWidget *tree)
{
    return tree && tree->verticalScrollBar() ? tree->verticalScrollBar()->value()
                                             : 0;
}

inline void restoreFileTreeExpandedPaths(
    QTreeWidget *tree, const QSet<QString> &expanded, int scrollOffset = 0,
    const std::function<void(QTreeWidgetItem *)> &onExpand = {})
{
    if (!tree)
        return;
    if (!expanded.isEmpty()) {
        std::function<void(QTreeWidgetItem *)> walk =
            [&](QTreeWidgetItem *parent) {
                for (int i = 0; i < parent->childCount(); ++i) {
                    QTreeWidgetItem *child = parent->child(i);
                    if (fileTreeIsDir(child) &&
                        expanded.contains(fileTreePath(child))) {
                        if (onExpand)
                            onExpand(child);
                        child->setExpanded(true);
                    }
                    walk(child);
                }
            };
        walk(tree->invisibleRootItem());
    }
    if (tree->verticalScrollBar())
        tree->verticalScrollBar()->setValue(scrollOffset);
}

inline void revealInDesktopFileManager(const QString &path)
{
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    const QString target =
        info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!target.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

inline QString chooseExistingDirectory(QWidget *parent, const QString &title,
                                       const QString &start,
                                       const QList<QUrl> &extraSidebar = {})
{
    QString from = start;
    if (from.isEmpty() || !QDir(from).exists())
        from = QDir::homePath();
    QFileDialog dialog(parent, title, from);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFilter(QDir::AllDirs | QDir::Drives | QDir::NoDotAndDotDot |
                     QDir::Hidden);
    QList<QUrl> sidebar{QUrl::fromLocalFile(QDir::rootPath()),
                        QUrl::fromLocalFile(QDir::homePath())};
    for (const QUrl &url : extraSidebar) {
        if (!sidebar.contains(url))
            sidebar.append(url);
    }
    dialog.setSidebarUrls(sidebar);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    const QStringList chosen = dialog.selectedFiles();
    return chosen.isEmpty() ? QString() : chosen.first();
}

inline bool fileContentIsBinary(const QByteArray &head)
{
    return head.contains('\0');
}

} // namespace forkmesh::ui
