












#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"
#include "LocalBackupStore.h"

using namespace forkmesh::ui;

namespace {


struct DataDir {
    QString label;
    QString path;
    QString hint;
    bool inBackup;
};




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




QVector<DataDir> resolveDataDirs(const QString &mirrorRoot,
                                 const QString &previewRoot,
                                 const QString &backupRoot)
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
            if (d.path == abs)
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
    add("Backups", backupRoot,
        "Hourly snapshots of your configuration data. Never archived into an "
        "export or another snapshot \xE2\x80\x94 no backups of backups.",
        false);
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

}



QWidget *MainWindow::buildDataSection()
{
    auto *page = new QWidget;
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(2, 14, 2, 14);
    col->setSpacing(10);


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
    installColumnHeaderMenu(m_dataDirTable);
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
    dataHeader->setSectionResizeMode(1, QHeaderView::Stretch);

    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    connect(refreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshDataDirTable);






    auto *logsLabel = new QLabel("LOG FILES");
    logsLabel->setObjectName("sectionLabel");
    auto *logsHint = new QLabel(
        "Where ForkMesh writes its diagnostics on this computer. Both are plain "
        "text \xE2\x80\x94 open them, or hand the paths to a coding agent when "
        "reporting a problem.");
    logsHint->setObjectName("statusLine");
    logsHint->setWordWrap(true);

    auto *logsGrid = new QGridLayout;
    logsGrid->setContentsMargins(0, 0, 0, 0);
    logsGrid->setHorizontalSpacing(10);
    logsGrid->setColumnStretch(1, 1);
    int logRow = 0;
    const auto addLogRow = [&](const QString &name, const QString &path,
                               const QString &hint) {
        if (path.isEmpty())
            return;
        auto *nameLabel = new QLabel(name);
        nameLabel->setToolTip(hint);
        auto *pathLabel = new QLabel(QDir::toNativeSeparators(path));
        pathLabel->setObjectName("statusLine");
        pathLabel->setToolTip(hint);
        pathLabel->setTextFormat(Qt::PlainText);
        pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto *openButton = new QPushButton("Open");
        openButton->setObjectName("ghostButton");
        openButton->setCursor(Qt::PointingHandCursor);
        openButton->setEnabled(QFileInfo::exists(path));
        openButton->setToolTip(openButton->isEnabled()
                                   ? QStringLiteral("Open this log in your text editor")
                                   : QStringLiteral("Nothing written to this log yet"));
        connect(openButton, &QPushButton::clicked, this, [path] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        auto *copyButton = new QPushButton("Copy path");
        copyButton->setObjectName("ghostButton");
        copyButton->setCursor(Qt::PointingHandCursor);
        connect(copyButton, &QPushButton::clicked, this, [this, path] {
            QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(path));
            setDataStatus(
                QStringLiteral("Copied %1").arg(QDir::toNativeSeparators(path)));
        });
        logsGrid->addWidget(nameLabel, logRow, 0);
        logsGrid->addWidget(pathLabel, logRow, 1);
        logsGrid->addWidget(openButton, logRow, 2);
        logsGrid->addWidget(copyButton, logRow, 3);
        ++logRow;
    };
    addLogRow(QStringLiteral("App log"), networkLogPath(),
              QStringLiteral("Everything the Log view shows: network calls, sync, "
                             "agents and system messages."));
    addLogRow(QStringLiteral("UI-stall log"), stallLogPath(),
              QStringLiteral("Backtraces for every GUI-thread freeze the watchdog "
                             "records; the footer's stall badge drafts a fix-it "
                             "prompt from these."));


    auto *autoLabel = new QLabel("AUTOMATIC BACKUPS");
    autoLabel->setObjectName("sectionLabel");
    auto *autoHint = new QLabel(
        "With this on, every hour ForkMesh writes a snapshot of the live "
        "database \xE2\x80\x94 your identity key, account, chat history, agents, "
        "issues and pull requests \xE2\x80\x94 to this computer's drive. It "
        "starts on only for control nodes (the ones holding a Cloudflare API "
        "token), since a day of snapshots can run to gigabytes; tick the box to "
        "turn it on here. Restore any snapshot below to roll the whole database "
        "back to that moment. Each one is an ordinary .tar.gz, so it can also "
        "be recovered by hand with \"tar xzf\".");
    autoHint->setObjectName("statusLine");
    autoHint->setWordWrap(true);

    m_backupEnabledCheck = new QCheckBox("Back up hourly to this computer");
    m_backupEnabledCheck->setChecked(autoBackupEnabled());
    m_backupEnabledCheck->setCursor(Qt::PointingHandCursor);
    connect(m_backupEnabledCheck, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(kAutoBackupEnabledSetting, on);
        startAutoBackups();
        setBackupStatus(on ? QStringLiteral("Hourly backups are on.")
                           : QStringLiteral("Hourly backups are off \xE2\x80\x94 "
                                            "existing snapshots are kept."));
    });

    m_backupKeepSpin = new QSpinBox;
    m_backupKeepSpin->setRange(forkmesh::kBackupKeepMin, forkmesh::kBackupKeepMax);
    m_backupKeepSpin->setValue(backupKeepCount());
    m_backupKeepSpin->setSuffix(" kept");
    m_backupKeepSpin->setToolTip(
        "How many snapshots to keep before the oldest is deleted. 24 hourly "
        "snapshots cover a full day.");
    connect(m_backupKeepSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int keep) {
                QSettings().setValue(kAutoBackupKeepSetting, keep);
                pruneOldBackups();
                refreshBackupTable();
            });

    m_backupNowButton = new QPushButton("Back up now");
    m_backupNowButton->setObjectName("primaryButton");
    m_backupNowButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_backupNowButton, "history", 16);
    connect(m_backupNowButton, &QPushButton::clicked, this,
            [this] { takeBackupNow(false); });

    auto *openBackupsButton = new QPushButton("Open backup folder");
    openBackupsButton->setObjectName("ghostButton");
    openBackupsButton->setCursor(Qt::PointingHandCursor);
    setOcticon(openBackupsButton, "file-directory", 16);
    connect(openBackupsButton, &QPushButton::clicked, this, [this] {
        QDir().mkpath(backupRoot());
        QDesktopServices::openUrl(QUrl::fromLocalFile(backupRoot()));
    });

    auto *autoRow = new QHBoxLayout;
    autoRow->setContentsMargins(0, 0, 0, 0);
    autoRow->addWidget(m_backupEnabledCheck);
    autoRow->addSpacing(12);
    autoRow->addWidget(m_backupKeepSpin);
    autoRow->addSpacing(12);
    autoRow->addWidget(m_backupNowButton);
    autoRow->addWidget(openBackupsButton);
    autoRow->addStretch();

    m_backupTable = new QTableWidget(0, 5);
    installColumnHeaderMenu(m_backupTable);
    m_backupTable->setHorizontalHeaderLabels(
        {"Taken", "Age", "Size", "File", ""});
    m_backupTable->verticalHeader()->setVisible(false);
    m_backupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_backupTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_backupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_backupTable->setWordWrap(false);
    m_backupTable->setTextElideMode(Qt::ElideMiddle);
    QHeaderView *backupHeader = m_backupTable->horizontalHeader();
    backupHeader->setStretchLastSection(false);
    for (int c = 0; c < m_backupTable->columnCount(); ++c)
        backupHeader->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    backupHeader->setSectionResizeMode(3, QHeaderView::Stretch);

    m_backupStatus = new QLabel;
    m_backupStatus->setObjectName("statusLine");
    m_backupStatus->setWordWrap(true);
    m_backupStatus->setVisible(false);


    auto *backupLabel = new QLabel("EXPORT & IMPORT");
    backupLabel->setObjectName("sectionLabel");
    auto *backupHint = new QLabel(
        "Same archive as an hourly snapshot, saved wherever you choose \xE2\x80\x94 "
        "for moving your configuration to another computer or keeping a copy "
        "off this drive. Importing replaces the current configuration and "
        "restarts ForkMesh.");
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
    col->addWidget(logsLabel);
    col->addWidget(logsHint);
    col->addLayout(logsGrid);
    col->addSpacing(6);
    col->addWidget(autoLabel);
    col->addWidget(autoHint);
    col->addLayout(autoRow);
    col->addWidget(m_backupTable, 1);
    col->addWidget(m_backupStatus);
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
    refreshBackupTable();
    return page;
}

void MainWindow::refreshDataDirTable()
{
    if (!m_dataDirTable)
        return;
    const QVector<DataDir> dirs = resolveDataDirs(
        repositoryMirrorRoot(), repositoryPreviewRoot(), backupRoot());


    QSet<QString> nested;
    nested.insert(QDir(repositoryMirrorRoot()).absolutePath());
    nested.insert(QDir(repositoryPreviewRoot()).absolutePath());
    nested.insert(QDir(backupRoot()).absolutePath());

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
            prune.remove(d.path);
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
        const bool critical = d.inBackup;
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




    const QStringList args = forkmesh::configArchiveTarArgs(
        out, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
        QSettings().fileName(),
        {repositoryMirrorRoot(), repositoryPreviewRoot(), backupRoot()});
    if (args.isEmpty()) {
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
    const QString in = QFileDialog::getOpenFileName(
        this, "Import ForkMesh configuration",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        "Backup archive (*.tar.gz *.tgz)");
    if (in.isEmpty())
        return;
    restoreConfigArchive(in);
}




void MainWindow::restoreConfigArchive(const QString &in)
{
    const QString tarExe = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tarExe.isEmpty()) {
        setDataStatus("Can't restore: the 'tar' tool was not found on this "
                      "system.",
                      true);
        return;
    }
    if (!QFileInfo::exists(in)) {
        setDataStatus("That backup is no longer on disk: " + in, true);
        refreshBackupTable();
        return;
    }

    QMessageBox box(
        QMessageBox::Warning, QStringLiteral("Restore configuration"),
        "Restoring replaces ForkMesh's current identity, account and "
        "preferences with the contents of:\n\n" +
            in +
            "\n\nYour current configuration is moved aside to a timestamped "
            "backup first. ForkMesh will restart to finish.",
        QMessageBox::Cancel, this);
    QPushButton *go = box.addButton(QStringLiteral("Restore & restart\xE2\x80\xA6"),
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



    const QString workRoot =
        appParent + QStringLiteral("/.forkmesh-import-") + stamp;
    QDir().mkpath(workRoot);

    QProcess p;
    p.start(tarExe, {QStringLiteral("-xzf"), in, QStringLiteral("-C"), workRoot});
    p.waitForFinished(-1);
    if (!(p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)) {
        const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
        QDir(workRoot).removeRecursively();
        setDataStatus("Restore failed to unpack the archive" +
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


    stopLiveServicesForDataOp();

    if (QFileInfo::exists(importedApp)) {
        QDir().mkpath(appParent);
        const QString displaced = appData + ".bak-" + stamp;
        const QString liveBackups = QDir(backupRoot()).absolutePath();





        const QString rel = QDir(appData).relativeFilePath(liveBackups);
        const bool backupsNested = !rel.startsWith(QStringLiteral(".."));
        if (QFileInfo::exists(appData))
            QDir().rename(appData, displaced);
        QDir().rename(importedApp, appData);
        if (backupsNested && !QFileInfo::exists(liveBackups) &&
            QFileInfo::exists(QDir(displaced).filePath(rel)))
            QDir().rename(QDir(displaced).filePath(rel), liveBackups);
    }
    if (QFileInfo::exists(importedConfig)) {
        QDir().mkpath(configDir);
        if (QFileInfo::exists(configFile))
            QFile::rename(configFile, configFile + ".bak-" + stamp);


        if (!QFile::rename(importedConfig, configFile))
            QFile::copy(importedConfig, configFile);
    }
    QDir(workRoot).removeRecursively();

    relaunchForkMesh();
}



QString MainWindow::backupRoot() const
{
    return forkmesh::defaultBackupRoot();
}

bool MainWindow::autoBackupEnabled() const
{


    return QSettings()
        .value(kAutoBackupEnabledSetting,
               forkmesh::autoBackupDefault(resolvedCloudflareApiToken()))
        .toBool();
}

int MainWindow::backupKeepCount() const
{
    return qBound(forkmesh::kBackupKeepMin,
                  QSettings()
                      .value(kAutoBackupKeepSetting, forkmesh::kBackupKeepDefault)
                      .toInt(),
                  forkmesh::kBackupKeepMax);
}

void MainWindow::setBackupStatus(const QString &text, bool error)
{
    if (!m_backupStatus)
        return;
    m_backupStatus->setText(text);
    m_backupStatus->setStyleSheet(error ? QStringLiteral("color:#f85149;")
                                        : QString());
    m_backupStatus->setVisible(!text.isEmpty());
}



void MainWindow::startAutoBackups()
{
    if (!m_backupTimer) {
        m_backupTimer = new QTimer(this);
        m_backupTimer->setInterval(
            static_cast<int>(forkmesh::kBackupIntervalMs));
        connect(m_backupTimer, &QTimer::timeout, this,
                [this] { takeBackupNow(true); });
    }
    if (!autoBackupEnabled()) {
        m_backupTimer->stop();
        return;
    }
    m_backupTimer->start();






    const QList<forkmesh::BackupSnapshot> snapshots =
        forkmesh::listBackups(backupRoot());
    const QDateTime last =
        snapshots.isEmpty() ? QDateTime() : snapshots.first().taken;
    if (forkmesh::backupIsDue(last, QDateTime::currentDateTime()))
        QTimer::singleShot(15000, this, [this] { takeBackupNow(true); });
}




void MainWindow::takeBackupNow(bool automatic)
{
    if (automatic && !autoBackupEnabled())
        return;
    if (m_backupProcess) {
        if (!automatic)
            setBackupStatus("A backup is already running\xE2\x80\xA6");
        return;
    }

    const QString tarExe = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tarExe.isEmpty()) {


        setBackupStatus("Can't back up: the 'tar' tool was not found on this "
                        "system.",
                        true);
        logSystem(QStringLiteral(
            "Backup: skipped \xE2\x80\x94 'tar' was not found on this system."));
        return;
    }

    QDir().mkpath(backupRoot());
    const QString finalPath = QDir(backupRoot())
                                  .filePath(forkmesh::backupFileName(
                                      QDateTime::currentDateTime()));



    const QString partPath = finalPath + QStringLiteral(".part");
    const QStringList args = forkmesh::configArchiveTarArgs(
        partPath,
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
        QSettings().fileName(),
        {repositoryMirrorRoot(), repositoryPreviewRoot(), backupRoot()});
    if (args.isEmpty()) {
        setBackupStatus("There's nothing to back up yet.", true);
        return;
    }

    auto *tar = new QProcess(this);
    m_backupProcess = tar;
    if (m_backupNowButton)
        m_backupNowButton->setEnabled(false);
    if (!automatic)
        setBackupStatus("Backing up\xE2\x80\xA6");
    connect(tar, &QProcess::finished, this,
            [this, tar, partPath, finalPath, automatic](
                int code, QProcess::ExitStatus status) {
                m_backupProcess = nullptr;
                if (m_backupNowButton)
                    m_backupNowButton->setEnabled(true);



                const bool ok = status == QProcess::NormalExit &&
                                (code == 0 || code == 1);
                if (ok && QFile::rename(partPath, finalPath)) {
                    pruneOldBackups();
                    refreshBackupTable();
                    const QString name = QFileInfo(finalPath).fileName();
                    const QString size = QLocale().formattedDataSize(
                        QFileInfo(finalPath).size());
                    setBackupStatus((automatic ? QStringLiteral("Hourly backup ")
                                               : QStringLiteral("Backup ")) +
                                    "saved: " + name + " (" + size + ").");
                    logSystem(QStringLiteral("Backup: wrote %1 (%2).")
                                  .arg(name, size));
                } else {
                    QFile::remove(partPath);
                    const QString err =
                        QString::fromUtf8(tar->readAllStandardError())
                            .trimmed()
                            .left(300);
                    const QString why =
                        err.isEmpty()
                            ? QStringLiteral("tar exited with %1").arg(code)
                            : err;
                    setBackupStatus("Backup failed: " + why, true);
                    logSystem(QStringLiteral("Backup: failed (%1).").arg(why));
                }
                tar->deleteLater();
            });
    tar->start(tarExe, args);
}



void MainWindow::pruneOldBackups()
{
    const QList<forkmesh::BackupSnapshot> stale = forkmesh::backupsToPrune(
        forkmesh::listBackups(backupRoot()), backupKeepCount());
    for (const forkmesh::BackupSnapshot &snapshot : stale)
        QFile::remove(snapshot.path);
    if (m_backupProcess)
        return;
    const QFileInfoList partials =
        QDir(backupRoot())
            .entryInfoList({QStringLiteral("forkmesh-backup-*.tar.gz.part")},
                           QDir::Files);
    for (const QFileInfo &fi : partials)
        QFile::remove(fi.absoluteFilePath());
}

void MainWindow::refreshBackupTable()
{
    if (!m_backupTable)
        return;
    const QList<forkmesh::BackupSnapshot> snapshots =
        forkmesh::listBackups(backupRoot());
    m_backupTable->setRowCount(snapshots.size());
    for (int r = 0; r < snapshots.size(); ++r) {
        const forkmesh::BackupSnapshot &snapshot = snapshots.at(r);

        auto *takenItem = new QTableWidgetItem(
            snapshot.taken.toString(QStringLiteral("yyyy-MM-dd hh:mm")));
        m_backupTable->setItem(r, 0, takenItem);

        m_backupTable->setItem(
            r, 1,
            new QTableWidgetItem(formatIssueRelativeTime(
                snapshot.taken.toMSecsSinceEpoch())));

        auto *sizeItem = new QTableWidgetItem(
            QLocale().formattedDataSize(snapshot.bytes));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_backupTable->setItem(r, 2, sizeItem);

        auto *fileItem = new QTableWidgetItem(snapshot.fileName);
        fileItem->setToolTip(snapshot.path);
        m_backupTable->setItem(r, 3, fileItem);

        auto *actions = new QWidget;
        auto *actionRow = new QHBoxLayout(actions);
        actionRow->setContentsMargins(4, 2, 4, 2);
        actionRow->setSpacing(6);
        const QString path = snapshot.path;
        const QString name = snapshot.fileName;
        auto *restoreButton = new QPushButton("Restore");
        restoreButton->setObjectName("ghostButton");
        restoreButton->setCursor(Qt::PointingHandCursor);
        restoreButton->setToolTip(
            "Replace the live database with this snapshot and restart");
        connect(restoreButton, &QPushButton::clicked, this,
                [this, path] { restoreConfigArchive(path); });
        auto *deleteButton = new QPushButton("Delete");
        deleteButton->setObjectName("ghostButton");
        deleteButton->setCursor(Qt::PointingHandCursor);
        connect(deleteButton, &QPushButton::clicked, this, [this, path, name] {
            QMessageBox box(QMessageBox::Warning,
                            QStringLiteral("Delete backup"),
                            "Permanently delete this snapshot?\n\n" + name,
                            QMessageBox::Cancel, this);
            QPushButton *go = box.addButton(QStringLiteral("Delete"),
                                            QMessageBox::DestructiveRole);
            box.setDefaultButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() != go)
                return;
            QFile::remove(path);
            refreshBackupTable();
            setBackupStatus("Deleted " + name + ".");
        });
        actionRow->addWidget(restoreButton);
        actionRow->addWidget(deleteButton);
        actionRow->addStretch();
        m_backupTable->setCellWidget(r, 4, actions);
    }

    if (snapshots.isEmpty())
        setBackupStatus(
            autoBackupEnabled()
                ? QStringLiteral("No snapshots yet \xE2\x80\x94 the first one is "
                                 "taken shortly after startup, then every hour.")
                : QStringLiteral("Hourly backups are off. \"Back up now\" still "
                                 "takes one on demand."));
}



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
    refreshBackupTable();
}

void MainWindow::deleteAllData()
{
    const QVector<DataDir> dirs = resolveDataDirs(
        repositoryMirrorRoot(), repositoryPreviewRoot(), backupRoot());
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



void MainWindow::stopLiveServicesForDataOp()
{
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    if (m_adminPollTimer)
        m_adminPollTimer->stop();


    if (m_backupTimer)
        m_backupTimer->stop();
    if (m_backupProcess) {
        m_backupProcess->disconnect(this);
        m_backupProcess->kill();
        m_backupProcess->waitForFinished(2000);
        m_backupProcess->deleteLater();
        m_backupProcess = nullptr;
    }
    stopRepoHosts();
    if (m_backend) {
        m_backend->disconnect(this);
        m_backend->shutdown();
        m_backend->deleteLater();
        m_backend = nullptr;
    }
}



void MainWindow::relaunchForkMesh()
{
    QDir::setCurrent(QDir::homePath());



    forkmesh::releaseSingleInstance();
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                            QCoreApplication::arguments().mid(1));
    QCoreApplication::exit(0);
}
