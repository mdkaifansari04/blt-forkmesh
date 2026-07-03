// MainWindowShortcuts: MainWindow feature methods, split out of MainWindow.cpp.
// Per-repo Shortcuts tab (adhoc #118).
//
// Shortcuts are plain files in the checkout's .forkmesh/shortcuts/ folder —
// shell scripts today, with prompts/skills riding along as editable text — so
// they version and sync with the repo like workflows do. The tab lists each
// file as a clickable card: a script click runs it through bash with stdout/
// stderr streamed live into the page's output pane; other kinds open in the
// editor. New / edit / delete round out the CRUD.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"

using namespace forkmesh::ui;

namespace {

// A shortcut is "runnable" when it looks like a shell script; everything else
// (prompt .md/.txt, skill files, …) is treated as text a card click opens for
// editing instead of executing.
bool shortcutIsScript(const QString &filePath)
{
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("sh") || suffix == QLatin1String("bash"))
        return true;
    return suffix.isEmpty() && info.isExecutable();
}

QString shortcutKind(const QString &filePath)
{
    if (shortcutIsScript(filePath))
        return QStringLiteral("script");
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QLatin1String("skill"))
        return QStringLiteral("skill");
    if (suffix == QLatin1String("md") || suffix == QLatin1String("txt") ||
        suffix == QLatin1String("prompt"))
        return QStringLiteral("prompt");
    return QStringLiteral("file");
}

// Card title/subtitle from the file's header: a "# name:" / "# description:"
// comment when present, else the file name and its first comment line.
void shortcutMeta(const QString &filePath, QString *name, QString *description)
{
    *name = QFileInfo(filePath).fileName();
    description->clear();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    for (int i = 0; i < 12 && !file.atEnd(); ++i) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1String("#!")))
            continue;
        if (line.startsWith(QLatin1String("# name:")))
            *name = line.mid(7).trimmed();
        else if (line.startsWith(QLatin1String("# description:")))
            *description = line.mid(14).trimmed();
        else if (description->isEmpty() && line.startsWith(QLatin1Char('#')))
            *description = line.mid(1).trimmed();
    }
}

} // namespace

QString MainWindow::shortcutsDirPath() const
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return {};
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    if (repo.previewOnly || repo.localPath.isEmpty())
        return {};
    return repo.localPath + QStringLiteral("/.forkmesh/shortcuts");
}

QWidget *MainWindow::buildShortcutsTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel("Shortcuts");
    heading->setObjectName("channelTitle");
    m_shortcutsSummary = new QLabel;
    m_shortcutsSummary->setObjectName("statusLine");
    auto *newButton = new QPushButton("New shortcut");
    newButton->setObjectName("primaryButton");
    newButton->setCursor(Qt::PointingHandCursor);
    setOcticon(newButton, "plus", 16);
    connect(newButton, &QPushButton::clicked, this,
            [this] { openShortcutEditor(QString()); });
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("ghostButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 16);
    connect(refreshButton, &QPushButton::clicked, this,
            &MainWindow::loadShortcutsPanel);
    addRefreshSpin(refreshButton);
    headerRow->addWidget(heading);
    headerRow->addWidget(m_shortcutsSummary);
    headerRow->addStretch();
    headerRow->addWidget(newButton);
    headerRow->addWidget(refreshButton);
    layout->addLayout(headerRow);

    auto *hint = new QLabel(
        "Scripts, prompts and skills stored in this repo's .forkmesh/shortcuts/ "
        "folder, so they version and sync with the code. Click a script card to "
        "run it (bash, from the repo root) — its output streams below. Other "
        "files open in the editor.");
    hint->setObjectName("statusLine");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // Cards scroll in their own area so a long list never squeezes the output
    // pane; loadShortcutsPanel rebuilds the host's rows.
    m_shortcutCardsHost = new QWidget;
    auto *cardsLayout = new QVBoxLayout(m_shortcutCardsHost);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(8);
    cardsLayout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_shortcutCardsHost);
    layout->addWidget(scroll, 2);

    auto *outputRow = new QHBoxLayout;
    outputRow->setContentsMargins(0, 0, 0, 0);
    auto *outputHeading = new QLabel("Output");
    outputHeading->setStyleSheet("font-weight:600;background:transparent;");
    m_shortcutRunStatus = new QLabel;
    m_shortcutRunStatus->setObjectName("statusLine");
    m_shortcutStopButton = new QPushButton("Stop");
    m_shortcutStopButton->setObjectName("ghostButton");
    m_shortcutStopButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_shortcutStopButton, "stop", 14);
    m_shortcutStopButton->hide();
    connect(m_shortcutStopButton, &QPushButton::clicked, this,
            &MainWindow::stopShortcut);
    outputRow->addWidget(outputHeading);
    outputRow->addWidget(m_shortcutRunStatus);
    outputRow->addStretch();
    outputRow->addWidget(m_shortcutStopButton);
    layout->addLayout(outputRow);

    m_shortcutOutput = new QPlainTextEdit;
    m_shortcutOutput->setObjectName("networkLog");
    m_shortcutOutput->setReadOnly(true);
    m_shortcutOutput->setMaximumBlockCount(5000);
    m_shortcutOutput->setPlaceholderText("Run a shortcut to see its output here.");
    layout->addWidget(m_shortcutOutput, 1);

    return page;
}

void MainWindow::loadShortcutsPanel()
{
    if (!m_shortcutCardsHost)
        return;
    auto *cardsLayout = static_cast<QVBoxLayout *>(m_shortcutCardsHost->layout());
    while (cardsLayout->count() > 1) { // keep the trailing stretch
        QLayoutItem *item = cardsLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    if (m_shortcutsSummary)
        m_shortcutsSummary->clear();

    const QString dirPath = shortcutsDirPath();
    if (dirPath.isEmpty()) {
        if (m_shortcutsSummary)
            m_shortcutsSummary->setText(
                "read-only mirror — shortcuts need a working tree");
        return;
    }

    const QFileInfoList files =
        QDir(dirPath).entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    if (m_shortcutsSummary)
        m_shortcutsSummary->setText(
            files.isEmpty() ? QStringLiteral("none yet")
                            : QStringLiteral("%1 shortcut%2")
                                  .arg(files.size())
                                  .arg(files.size() == 1 ? "" : "s"));

    int insertAt = 0;
    for (const QFileInfo &info : files) {
        const QString path = info.absoluteFilePath();
        const bool script = shortcutIsScript(path);
        QString name, description;
        shortcutMeta(path, &name, &description);

        // The card is itself a button — clicking anywhere runs a script or
        // opens anything else in the editor. Its labels are made transparent
        // to the mouse so clicks land on the card; the edit/delete buttons
        // stay clickable on top of it.
        auto *card = new QPushButton;
        card->setObjectName("shortcutCard");
        card->setCursor(Qt::PointingHandCursor);
        card->setToolTip(script
                             ? QStringLiteral("Run %1").arg(info.fileName())
                             : QStringLiteral("Edit %1").arg(info.fileName()));
        connect(card, &QPushButton::clicked, this, [this, path, script] {
            if (script)
                runShortcut(path);
            else
                openShortcutEditor(path);
        });

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(3);

        auto *titleRow = new QHBoxLayout;
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(8);
        auto *icon = new QLabel;
        icon->setPixmap(themedOcticon(script ? "rocket" : "file",
                                      QColor("#2ea043"), 16)
                            .pixmap(16, 16));
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *title = new QLabel(name);
        title->setStyleSheet("font-weight:600;font-size:14px;background:transparent;");
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *kind = new QLabel(shortcutKind(path));
        kind->setObjectName("statusLine");
        kind->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *editButton = new QPushButton;
        editButton->setObjectName("ghostButton");
        editButton->setProperty("buttonSize", "sm");
        editButton->setCursor(Qt::PointingHandCursor);
        setOcticon(editButton, "pencil", 14);
        editButton->setToolTip("Edit this shortcut");
        connect(editButton, &QPushButton::clicked, this,
                [this, path] { openShortcutEditor(path); });
        auto *deleteButton = new QPushButton;
        deleteButton->setObjectName("ghostButton");
        deleteButton->setProperty("buttonSize", "sm");
        deleteButton->setCursor(Qt::PointingHandCursor);
        setOcticon(deleteButton, "trash", 14);
        deleteButton->setToolTip("Delete this shortcut");
        connect(deleteButton, &QPushButton::clicked, this,
                [this, path] { deleteShortcut(path); });
        titleRow->addWidget(icon);
        titleRow->addWidget(title);
        titleRow->addWidget(kind);
        titleRow->addStretch();
        titleRow->addWidget(editButton);
        titleRow->addWidget(deleteButton);
        cardLayout->addLayout(titleRow);

        if (!description.isEmpty()) {
            auto *desc = new QLabel(description);
            desc->setObjectName("statusLine");
            desc->setWordWrap(true);
            desc->setAttribute(Qt::WA_TransparentForMouseEvents);
            cardLayout->addWidget(desc);
        }

        cardsLayout->insertWidget(insertAt++, card);
    }
}

void MainWindow::runShortcut(const QString &filePath)
{
    if (m_shortcutProcess) {
        setRepoDetailNotice(
            "A shortcut is already running — stop it before starting another.",
            true);
        return;
    }
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const QString workDir = m_repositories.at(m_repoDetailIndex).localPath;

    if (m_shortcutOutput) {
        m_shortcutOutput->clear();
        m_shortcutOutput->appendPlainText(QStringLiteral("$ bash %1").arg(filePath));
    }
    const QString name = QFileInfo(filePath).fileName();
    if (m_shortcutRunStatus)
        m_shortcutRunStatus->setText(QStringLiteral("running %1…").arg(name));
    if (m_shortcutStopButton)
        m_shortcutStopButton->show();

    auto *process = new QProcess(this);
    m_shortcutProcess = process;
    process->setWorkingDirectory(workDir);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (m_shortcutOutput)
            m_shortcutOutput->appendPlainText(
                QString::fromUtf8(process->readAllStandardOutput()).trimmed());
    });
    auto finish = [this, process, name](const QString &message) {
        if (m_shortcutOutput)
            m_shortcutOutput->appendPlainText(message);
        if (m_shortcutRunStatus)
            m_shortcutRunStatus->setText(QStringLiteral("%1 — %2").arg(name, message));
        if (m_shortcutStopButton)
            m_shortcutStopButton->hide();
        m_shortcutProcess = nullptr;
        process->deleteLater();
    };
    connect(process, &QProcess::finished, this,
            [finish](int code, QProcess::ExitStatus st) {
                finish(st == QProcess::NormalExit && code == 0
                           ? QStringLiteral("done (exit 0)")
                           : QStringLiteral("failed (exit %1)").arg(code));
            });
    connect(process, &QProcess::errorOccurred, this,
            [finish, process](QProcess::ProcessError) {
                // finished() still follows a Crashed error; only a failed start
                // never reaches it, so complete here just for that case.
                if (process->state() == QProcess::NotRunning &&
                    process->error() == QProcess::FailedToStart)
                    finish(QStringLiteral("failed to start bash"));
            });
    process->start(QStringLiteral("bash"), {filePath});
}

void MainWindow::stopShortcut()
{
    if (!m_shortcutProcess)
        return;
    if (m_shortcutOutput)
        m_shortcutOutput->appendPlainText(QStringLiteral("^C stopping…"));
    m_shortcutProcess->terminate();
    // Escalate if the process ignores SIGTERM; the finished() handler resets
    // the UI whichever signal lands.
    QPointer<QProcess> process = m_shortcutProcess;
    QTimer::singleShot(3000, this, [process] {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void MainWindow::openShortcutEditor(const QString &filePath)
{
    const QString dirPath = shortcutsDirPath();
    if (dirPath.isEmpty()) {
        setRepoDetailNotice(
            "This is a read-only mirror; shortcuts can't be edited here.", true);
        return;
    }
    const bool creating = filePath.isEmpty();

    QDialog dialog(this);
    dialog.setWindowTitle(creating ? "New shortcut" : "Edit shortcut");
    dialog.resize(640, 480);
    auto *form = new QVBoxLayout(&dialog);
    auto *nameEdit = new QLineEdit;
    nameEdit->setPlaceholderText("file name, e.g. run-tests.sh (.md for a prompt)");
    if (creating) {
        form->addWidget(nameEdit);
    } else {
        nameEdit->setText(QFileInfo(filePath).fileName());
        auto *nameLabel = new QLabel(QFileInfo(filePath).fileName());
        nameLabel->setObjectName("statusLine");
        form->addWidget(nameLabel);
    }
    auto *contentEdit = new QPlainTextEdit;
    contentEdit->setObjectName("networkLog");
    if (creating) {
        contentEdit->setPlainText(QStringLiteral(
            "#!/usr/bin/env bash\n"
            "# name: My shortcut\n"
            "# description: What this shortcut does.\n"
            "set -euo pipefail\n\n"));
    } else {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            contentEdit->setPlainText(QString::fromUtf8(file.readAll()));
    }
    form->addWidget(contentEdit, 1);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString targetPath = filePath;
    if (creating) {
        // Keep the name a plain file inside the shortcuts folder.
        const QString name =
            QFileInfo(nameEdit->text().trimmed()).fileName();
        if (name.isEmpty() || name.startsWith(QLatin1Char('.'))) {
            setRepoDetailNotice("Shortcut needs a plain file name.", true);
            return;
        }
        targetPath = dirPath + QLatin1Char('/') + name;
        if (QFile::exists(targetPath)) {
            setRepoDetailNotice(
                QStringLiteral("%1 already exists.").arg(name), true);
            return;
        }
        QDir().mkpath(dirPath);
    }
    QFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setRepoDetailNotice(
            QStringLiteral("Couldn't write %1.").arg(targetPath), true);
        return;
    }
    out.write(contentEdit->toPlainText().toUtf8());
    out.close();
    if (shortcutIsScript(targetPath))
        QFile::setPermissions(targetPath, QFile::permissions(targetPath) |
                                              QFileDevice::ExeOwner |
                                              QFileDevice::ExeGroup);
    setRepoDetailNotice(
        QStringLiteral("Saved %1.").arg(QFileInfo(targetPath).fileName()), false);
    loadShortcutsPanel();
}

void MainWindow::deleteShortcut(const QString &filePath)
{
    const QString name = QFileInfo(filePath).fileName();
    if (QMessageBox::question(
            this, "Delete shortcut",
            QStringLiteral("Delete %1? The file is removed from "
                           ".forkmesh/shortcuts/ in the working tree.")
                .arg(name)) != QMessageBox::Yes)
        return;
    if (!QFile::remove(filePath)) {
        setRepoDetailNotice(QStringLiteral("Couldn't delete %1.").arg(name), true);
        return;
    }
    setRepoDetailNotice(QStringLiteral("Deleted %1.").arg(name), false);
    loadShortcutsPanel();
}
