// MainWindowData: the Settings -> Data tab.
//
// Shows every on-disk location ForkMesh owns (with a per-directory breakdown of
// how many folders/files it holds and how big it is), lets you open a folder in
// the file manager to explore it, delete any location individually or wipe them
// all at once, and export/import the configuration as a portable .tar.gz backup.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h.

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"

using namespace forkmesh::ui;

namespace {

// One managed on-disk location shown in the Data tab.
struct DataDir {
    QString label; // friendly name
    QString path;  // absolute path
    QString hint;  // what lives here (tooltip)
    bool inBackup; // included in export/import (true = configuration, not bulk cache)
};

// Recursive folder/file/byte tally for a directory. `prune` holds absolute paths
// whose subtrees should not be descended into (used so the "Application data"
// row doesn't double-count the mirror/browse-cache dirs nested inside it).
struct DirStat {
    int folders = 0;
    int files = 0;
    qint64 bytes = 0;
};

void scanInto(const QString &path, const QSet<QString> &prune, DirStat &s)
{
    const QFileInfoList entries =
        QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                 QDir::Hidden | QDir::System);
    for (const QFileInfo &fi : entries) {
        // Never follow symlinks: they can loop and they point outside the tree.
        if (fi.isSymLink()) {
            s.files++;
            continue;
        }
        if (fi.isDir()) {
            s.folders++;
            const QString abs = fi.absoluteFilePath();
            if (!prune.contains(abs))
                scanInto(abs, prune, s);
        } else {
            s.files++;
            s.bytes += fi.size();
        }
    }
}

// The full set of directories ForkMesh writes to. `mirrorRoot`/`previewRoot` are
// resolved by the caller (they can be relocated in Settings), the rest come from
// the platform's standard locations and the QSettings config file.
QVector<DataDir> resolveDataDirs(const QString &mirrorRoot,
                                 const QString &previewRoot)
{
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString localData =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString cache =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString configDir = QFileInfo(QSettings().fileName()).absolutePath();

    QVector<DataDir> dirs;
    auto add = [&dirs](const QString &label, const QString &path,
                       const QString &hint, bool backup) {
        if (path.isEmpty())
            return;
        const QString abs = QDir(path).absolutePath();
        for (const DataDir &d : dirs)
            if (d.path == abs) // don't list the same folder twice
                return;
        dirs.append({label, abs, hint, backup});
    };
    add("Application data", appData,
        "Your identity key, account, chat history, agents, issues, pull "
        "requests and every other local store.",
        true);
    add("Preferences", configDir,
        "Your settings file: theme, access tokens, notification toggles and "
        "shared action variables.",
        true);
    add("Repository mirrors", mirrorRoot,
        "Local copies of every repository you mirror or fork. Re-downloadable "
        "from the network.",
        false);
    add("Browse cache", previewRoot,
        "Temporary browse-only clones created while exploring the network.",
        false);
    add("Local data", localData,
        "Platform-specific local application data.", false);
    add("Cache", cache, "Regenerated automatically; always safe to delete.",
        false);
    return dirs;
}

} // namespace

// -------------------------------------------------------------------- Data tab

QWidget *MainWindow::buildDataSection()
{
    auto *page = new QWidget;
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(2, 14, 2, 14);
    col->setSpacing(10);

    // --- where the data lives ------------------------------------------------
    auto *storageLabel = new QLabel("CONFIGURATION DATA");
    storageLabel->setObjectName("sectionLabel");
    auto *storageHint = new QLabel(
        "Every folder ForkMesh keeps on this computer. Open one to explore it, "
        "or delete it to reclaim space \xE2\x80\x94 mirrors and caches come back "
        "on their own, but deleting your application data removes the identity "
        "key that is your account.");
    storageHint->setObjectName("statusLine");
    storageHint->setWordWrap(true);

    m_dataDirTable = new QTableWidget(0, 6);
    installColumnHeaderMenu(m_dataDirTable); // 3-dots per-column menu (issue #318)
    m_dataDirTable->setHorizontalHeaderLabels(
        {"Location", "Path", "Folders", "Files", "Size", ""});
    m_dataDirTable->verticalHeader()->setVisible(false);
    m_dataDirTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dataDirTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dataDirTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dataDirTable->setWordWrap(false);
    m_dataDirTable->setTextElideMode(Qt::ElideMiddle);
    QHeaderView *dataHeader = m_dataDirTable->horizontalHeader();
    dataHeader->setStretchLastSection(false);
    for (int c = 0; c < m_dataDirTable->columnCount(); ++c)
        dataHeader->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    dataHeader->setSectionResizeMode(1, QHeaderView::Stretch); // path takes slack

    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    connect(refreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshDataDirTable);

    // --- backup / restore ----------------------------------------------------
    auto *backupLabel = new QLabel("BACKUP & RESTORE");
    backupLabel->setObjectName("sectionLabel");
    auto *backupHint = new QLabel(
        "Export your configuration (identity, account and preferences \xE2\x80\x94 "
        "not the large, re-downloadable mirrors) to a single archive you can "
        "keep safe or move to another computer. Importing replaces the current "
        "configuration and restarts ForkMesh.");
    backupHint->setObjectName("statusLine");
    backupHint->setWordWrap(true);

    auto *exportButton = new QPushButton("Export configuration\xE2\x80\xA6");
    exportButton->setObjectName("primaryButton");
    exportButton->setCursor(Qt::PointingHandCursor);
    setOcticon(exportButton, "upload", 16);
    connect(exportButton, &QPushButton::clicked, this,
            &MainWindow::exportConfigData);

    auto *importButton = new QPushButton("Import configuration\xE2\x80\xA6");
    importButton->setObjectName("ghostButton");
    importButton->setCursor(Qt::PointingHandCursor);
    setOcticon(importButton, "download", 16);
    connect(importButton, &QPushButton::clicked, this,
            &MainWindow::importConfigData);

    auto *backupRow = new QHBoxLayout;
    backupRow->setContentsMargins(0, 0, 0, 0);
    backupRow->addWidget(exportButton);
    backupRow->addWidget(importButton);
    backupRow->addStretch();

    m_dataStatus = new QLabel;
    m_dataStatus->setObjectName("statusLine");
    m_dataStatus->setWordWrap(true);
    m_dataStatus->setVisible(false);

    // --- danger zone ---------------------------------------------------------
    auto *dangerLabel = new QLabel("DELETE EVERYTHING");
    dangerLabel->setObjectName("sectionLabel");
    auto *dangerHint = new QLabel(
        "Erase all ForkMesh data at once and start fresh. This removes your "
        "identity, settings, mirrors and caches, then restarts ForkMesh. The "
        "app itself stays installed. This cannot be undone.");
    dangerHint->setObjectName("statusLine");
    dangerHint->setWordWrap(true);
    auto *deleteAllButton = new QPushButton("Delete all ForkMesh data\xE2\x80\xA6");
    deleteAllButton->setObjectName("dangerButton");
    deleteAllButton->setCursor(Qt::PointingHandCursor);
    connect(deleteAllButton, &QPushButton::clicked, this,
            &MainWindow::deleteAllData);

    col->addWidget(storageLabel);
    col->addWidget(storageHint);
    col->addWidget(m_dataDirTable, 1);
    col->addWidget(refreshButton, 0, Qt::AlignLeft);
    col->addSpacing(6);
    col->addWidget(backupLabel);
    col->addWidget(backupHint);
    col->addLayout(backupRow);
    col->addWidget(m_dataStatus);
    col->addSpacing(6);
    col->addWidget(dangerLabel);
    col->addWidget(dangerHint);
    col->addWidget(deleteAllButton, 0, Qt::AlignLeft);
    col->addStretch();

    refreshDataDirTable();
    return page;
}

void MainWindow::refreshDataDirTable()
{
    if (!m_dataDirTable)
        return;
    const QVector<DataDir> dirs =
        resolveDataDirs(repositoryMirrorRoot(), repositoryPreviewRoot());
    // Nested mirror/browse-cache subtrees are shown as their own rows, so keep
    // them out of the "Application data" tally to avoid counting them twice.
    QSet<QString> nested;
    nested.insert(QDir(repositoryMirrorRoot()).absolutePath());
    nested.insert(QDir(repositoryPreviewRoot()).absolutePath());

    m_dataDirTable->setRowCount(dirs.size());
    for (int r = 0; r < dirs.size(); ++r) {
        const DataDir &d = dirs.at(r);
        const bool exists = QFileInfo::exists(d.path);

        auto *nameItem = new QTableWidgetItem(d.label);
        nameItem->setToolTip(d.hint);
        m_dataDirTable->setItem(r, 0, nameItem);

        auto *pathItem = new QTableWidgetItem(d.path);
        pathItem->setToolTip(d.path);
        m_dataDirTable->setItem(r, 1, pathItem);

        if (exists) {
            QSet<QString> prune = nested;
            prune.remove(d.path); // always count the row's own subtree in full
            DirStat st;
            scanInto(d.path, prune, st);
            auto num = [](int n) {
                auto *it = new QTableWidgetItem(QLocale().toString(n));
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                return it;
            };
            m_dataDirTable->setItem(r, 2, num(st.folders));
            m_dataDirTable->setItem(r, 3, num(st.files));
            auto *sizeItem =
                new QTableWidgetItem(QLocale().formattedDataSize(st.bytes));
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_dataDirTable->setItem(r, 4, sizeItem);
        } else {
            auto dash = [] {
                auto *it = new QTableWidgetItem(QStringLiteral("\xE2\x80\x94"));
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                it->setForeground(QColor(0x8b, 0x94, 0x9e));
                return it;
            };
            m_dataDirTable->setItem(r, 2, dash());
            m_dataDirTable->setItem(r, 3, dash());
            auto *sizeItem =
                new QTableWidgetItem(QStringLiteral("not created yet"));
            sizeItem->setForeground(QColor(0x8b, 0x94, 0x9e));
            m_dataDirTable->setItem(r, 4, sizeItem);
        }

        // Per-row actions: open the folder to explore it, or delete it.
        auto *actions = new QWidget;
        auto *actionRow = new QHBoxLayout(actions);
        actionRow->setContentsMargins(4, 2, 4, 2);
        actionRow->setSpacing(6);
        auto *openButton = new QPushButton("Open");
        openButton->setObjectName("ghostButton");
        openButton->setCursor(Qt::PointingHandCursor);
        openButton->setEnabled(exists);
        openButton->setToolTip("Open this folder in your file manager");
        const QString path = d.path;
        connect(openButton, &QPushButton::clicked, this, [path] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        auto *deleteButton = new QPushButton("Delete");
        deleteButton->setObjectName("ghostButton");
        deleteButton->setCursor(Qt::PointingHandCursor);
        deleteButton->setEnabled(exists);
        const QString label = d.label;
        const bool critical = d.inBackup; // identity/preferences => restart after
        connect(deleteButton, &QPushButton::clicked, this,
                [this, label, path, critical] {
                    deleteDataDir(label, path, critical);
                });
        actionRow->addWidget(openButton);
        actionRow->addWidget(deleteButton);
        actionRow->addStretch();
        m_dataDirTable->setCellWidget(r, 5, actions);
    }
}

void MainWindow::setDataStatus(const QString &text, bool error)
{
    if (!m_dataStatus)
        return;
    m_dataStatus->setText(text);
    m_dataStatus->setStyleSheet(error ? QStringLiteral("color:#f85149;")
                                       : QString());
    m_dataStatus->setVisible(!text.isEmpty());
}

// ----------------------------------------------------------- export / import

void MainWindow::exportConfigData()
{
    const QString tarExe = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tarExe.isEmpty()) {
        setDataStatus("Can't export: the 'tar' tool was not found on this "
                      "system.",
                      true);
        return;
    }

    const QString suggested =
        QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
            .filePath("forkmesh-config-" +
                      QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") +
                      ".tar.gz");
    QString out = QFileDialog::getSaveFileName(
        this, "Export ForkMesh configuration", suggested,
        "Backup archive (*.tar.gz)");
    if (out.isEmpty())
        return;
    if (!out.endsWith(QStringLiteral(".tar.gz")) &&
        !out.endsWith(QStringLiteral(".tgz")))
        out += QStringLiteral(".tar.gz");

    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString appParent = QFileInfo(appData).absolutePath();
    const QString configFile = QSettings().fileName();

    QStringList args;
    args << QStringLiteral("-czf") << out;
    // Skip the large, re-downloadable repo mirrors / browse cache when they live
    // inside the app-data dir (the default). --exclude must precede the members.
    auto excludeIfNested = [&args, &appParent](const QString &root) {
        const QString rel =
            QDir(appParent).relativeFilePath(QDir(root).absolutePath());
        if (!rel.startsWith(QStringLiteral("..")))
            args << (QStringLiteral("--exclude=") + rel);
    };
    excludeIfNested(repositoryMirrorRoot());
    excludeIfNested(repositoryPreviewRoot());

    bool any = false;
    if (QFileInfo::exists(appData)) {
        args << QStringLiteral("-C") << appParent
             << QFileInfo(appData).fileName();
        any = true;
    }
    if (QFileInfo::exists(configFile)) {
        args << QStringLiteral("-C") << QFileInfo(configFile).absolutePath()
             << QFileInfo(configFile).fileName();
        any = true;
    }
    if (!any) {
        setDataStatus("There's no configuration to export yet.", true);
        return;
    }

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QProcess p;
    p.start(tarExe, args);
    p.waitForFinished(-1);
    QGuiApplication::restoreOverrideCursor();

    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
        setDataStatus("Exported configuration to " + out + " (" +
                      QLocale().formattedDataSize(QFileInfo(out).size()) + ").");
    } else {
        const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
        setDataStatus("Export failed" + (err.isEmpty() ? QString() : ": " + err),
                      true);
    }
}

void MainWindow::importConfigData()
{
    const QString tarExe = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tarExe.isEmpty()) {
        setDataStatus("Can't import: the 'tar' tool was not found on this "
                      "system.",
                      true);
        return;
    }

    const QString in = QFileDialog::getOpenFileName(
        this, "Import ForkMesh configuration",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        "Backup archive (*.tar.gz *.tgz)");
    if (in.isEmpty())
        return;

    QMessageBox box(
        QMessageBox::Warning, QStringLiteral("Import configuration"),
        "Importing replaces ForkMesh's current identity, account and "
        "preferences with the contents of:\n\n" +
            in +
            "\n\nYour current configuration is moved aside to a timestamped "
            "backup first. ForkMesh will restart to finish.",
        QMessageBox::Cancel, this);
    QPushButton *go = box.addButton(QStringLiteral("Import & restart\xE2\x80\xA6"),
                                    QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != go)
        return;

    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString appParent = QFileInfo(appData).absolutePath();
    const QString appBase = QFileInfo(appData).fileName();
    const QString configFile = QSettings().fileName();
    const QString configDir = QFileInfo(configFile).absolutePath();
    const QString configBase = QFileInfo(configFile).fileName();
    const QString stamp =
        QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");

    // Extract next to the app-data dir so moving the unpacked tree into place is
    // a same-filesystem rename rather than a slow cross-device copy.
    const QString workRoot =
        appParent + QStringLiteral("/.forkmesh-import-") + stamp;
    QDir().mkpath(workRoot);

    QProcess p;
    p.start(tarExe, {QStringLiteral("-xzf"), in, QStringLiteral("-C"), workRoot});
    p.waitForFinished(-1);
    if (!(p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)) {
        const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
        QDir(workRoot).removeRecursively();
        setDataStatus("Import failed to unpack the archive" +
                          (err.isEmpty() ? QString() : ": " + err),
                      true);
        return;
    }

    const QString importedApp = QDir(workRoot).filePath(appBase);
    const QString importedConfig = QDir(workRoot).filePath(configBase);
    if (!QFileInfo::exists(importedApp) && !QFileInfo::exists(importedConfig)) {
        QDir(workRoot).removeRecursively();
        setDataStatus("That archive doesn't look like a ForkMesh "
                      "configuration backup.",
                      true);
        return;
    }

    // Stop everything that writes to disk before swapping the folders out.
    stopLiveServicesForDataOp();

    if (QFileInfo::exists(importedApp)) {
        QDir().mkpath(appParent);
        if (QFileInfo::exists(appData))
            QDir().rename(appData, appData + ".bak-" + stamp);
        QDir().rename(importedApp, appData);
    }
    if (QFileInfo::exists(importedConfig)) {
        QDir().mkpath(configDir);
        if (QFileInfo::exists(configFile))
            QFile::rename(configFile, configFile + ".bak-" + stamp);
        // ~/.config and ~/.local/share can be on different filesystems; fall
        // back to a copy when a rename can't cross the boundary.
        if (!QFile::rename(importedConfig, configFile))
            QFile::copy(importedConfig, configFile);
    }
    QDir(workRoot).removeRecursively();

    relaunchForkMesh();
}

// ---------------------------------------------------------------- deletion

void MainWindow::deleteDataDir(const QString &label, const QString &path,
                               bool critical)
{
    if (!QFileInfo::exists(path))
        return;

    QString detail = "Permanently delete " + label + "?\n\n" + path;
    if (critical)
        detail += "\n\nThis removes your identity and account \xE2\x80\x94 it "
                  "cannot be undone. ForkMesh will restart afterwards.";
    else
        detail += "\n\nThis cannot be undone.";

    QMessageBox box(QMessageBox::Warning, QStringLiteral("Delete data"), detail,
                    QMessageBox::Cancel, this);
    QPushButton *go = box.addButton(QStringLiteral("Delete\xE2\x80\xA6"),
                                    QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != go)
        return;

    if (critical) {
        stopLiveServicesForDataOp();
        QDir(path).removeRecursively();
        relaunchForkMesh();
        return;
    }

    if (QDir(path).removeRecursively())
        setDataStatus("Deleted " + label + ".");
    else
        setDataStatus("Couldn't fully delete " + label + " \xE2\x80\x94 some "
                      "files may be in use.",
                      true);
    refreshDataDirTable();
}

void MainWindow::deleteAllData()
{
    const QVector<DataDir> dirs =
        resolveDataDirs(repositoryMirrorRoot(), repositoryPreviewRoot());
    QStringList present;
    for (const DataDir &d : dirs)
        if (QFileInfo::exists(d.path))
            present << d.path;
    if (present.isEmpty()) {
        setDataStatus("There's no ForkMesh data on this computer to delete.");
        return;
    }

    QString detail = QStringLiteral(
        "This permanently erases every ForkMesh data folder on this computer, "
        "including your identity key (your account cannot be recovered), all "
        "settings, mirrored repositories and caches.\n\nFolders removed:\n");
    for (const QString &p : std::as_const(present))
        detail += "    " + p + "/\n";
    detail += QStringLiteral(
        "\nThe ForkMesh app stays installed and will restart fresh.");

    QMessageBox box(QMessageBox::Warning,
                    QStringLiteral("Delete all ForkMesh data"), detail,
                    QMessageBox::Cancel, this);
    QPushButton *go = box.addButton(QStringLiteral("Delete everything\xE2\x80\xA6"),
                                    QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != go)
        return;

    bool ok = false;
    const QString typed = QInputDialog::getText(
        this, QStringLiteral("Confirm delete"),
        QStringLiteral("Type DELETE to erase all ForkMesh data:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || typed.trimmed().compare(QStringLiteral("DELETE"),
                                       Qt::CaseInsensitive) != 0)
        return;

    stopLiveServicesForDataOp();
    for (const QString &p : std::as_const(present))
        QDir(p).removeRecursively();
    relaunchForkMesh();
}

// Quiesce every background service that writes to disk so a wipe/swap/restore
// isn't racing live file writes. Mirrors the teardown used by uninstall.
void MainWindow::stopLiveServicesForDataOp()
{
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    if (m_adminPollTimer)
        m_adminPollTimer->stop();
    stopRepoHosts();
    if (m_backend) {
        m_backend->disconnect(this);
        m_backend->shutdown();
        m_backend->deleteLater();
        m_backend = nullptr;
    }
}

// Restart the currently-running binary so it comes back up on the freshly
// imported/wiped data, then quit this instance.
void MainWindow::relaunchForkMesh()
{
    QDir::setCurrent(QDir::homePath());
    // Release the instance lock before spawning the replacement process, or it
    // bounces off the still-held lock (this process hasn't unwound yet) and
    // exits into nothing instead of taking over.
    forkmesh::releaseSingleInstance();
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                            QCoreApplication::arguments().mid(1));
    QCoreApplication::exit(0);
}
