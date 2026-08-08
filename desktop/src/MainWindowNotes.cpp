
#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "MarkdownEditor.h"
#include "NoteListEntry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QInputDialog>
#include <QAbstractItemView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStandardPaths>
#include <QSpinBox>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

using namespace forkmesh::ui;

namespace {
constexpr int kNoteIdRole = Qt::UserRole;
constexpr int kNoteStorageRole = Qt::UserRole + 1;

QString notesFilePath()
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/notes");
    QDir().mkpath(directory);
    return directory + QStringLiteral("/notes.json");
}

QJsonArray readLocalNotes()
{
    QFile file(notesFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isArray() ? document.array() : QJsonArray();
}

bool writeLocalNotes(const QJsonArray &notes)
{
    QSaveFile file(notesFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(notes).toJson(QJsonDocument::Indented));
    return file.commit();
}

QString newNoteId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces)
        .remove(QLatin1Char('-')).toLower();
}

QString noteError(const QJsonObject &payload, const QString &fallback)
{
    QString value = payload.value(QStringLiteral("error")).toString();
    if (value.isEmpty())
        return fallback;
    value.replace(QLatin1Char('_'), QLatin1Char(' '));
    return value;
}

int localNoteIndex(const QJsonArray &notes, const QString &id)
{
    for (int i = 0; i < notes.size(); ++i) {
        const QJsonObject note = notes.at(i).toObject();
        if (note.value(QStringLiteral("id")).toString() == id ||
            note.value(QStringLiteral("cloudId")).toString() == id)
            return i;
    }
    return -1;
}

QString noteTimestampLabel(const QJsonObject &note)
{
    const qint64 stamp = static_cast<qint64>(
        note.value(QStringLiteral("updatedAt")).toDouble());
    if (stamp <= 0)
        return QString();
    return QStringLiteral("edited %1").arg(
        QDateTime::fromMSecsSinceEpoch(stamp).toLocalTime()
            .toString(QStringLiteral("MMM d, HH:mm")));
}

QListWidgetItem *noteListItem(const QJsonObject &note,
                              const QJsonObject &cloud, const QString &mode,
                              bool synced)
{
    const QString text = NoteListEntry::lines(
        note, cloud, mode, synced,
        noteTimestampLabel(cloud.isEmpty() ? note : cloud))
            .join(QLatin1Char('\n'));
    auto *item = new QListWidgetItem(text);
    item->setToolTip(text);
    item->setTextAlignment(Qt::AlignTop | Qt::AlignLeft);
    return item;
}

QJsonObject localSnapshot(QJsonObject note, const QString &title,
                          const QString &markdown, const QString &mode)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int version = qMax(0, note.value(QStringLiteral("version")).toInt()) + 1;
    QJsonArray versions = note.value(QStringLiteral("versions")).toArray();
    versions.prepend(QJsonObject{
        {QStringLiteral("version"), version},
        {QStringLiteral("createdAt"), static_cast<double>(now)},
        {QStringLiteral("title"), title},
        {QStringLiteral("markdown"), markdown},
    });
    while (versions.size() > 100)
        versions.removeLast();
    note.insert(QStringLiteral("id"),
                note.value(QStringLiteral("id")).toString(newNoteId()));
    note.insert(QStringLiteral("title"), title);
    note.insert(QStringLiteral("markdown"), markdown);
    note.insert(QStringLiteral("storage"), mode);
    note.insert(QStringLiteral("role"), QStringLiteral("owner"));
    note.insert(QStringLiteral("version"), version);
    note.insert(QStringLiteral("updatedAt"), static_cast<double>(now));
    note.insert(QStringLiteral("versions"), versions);
    return note;
}
} // namespace

QWidget *MainWindow::buildNotesSection()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("notesSection"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(20, 18, 20, 20);
    outer->setSpacing(10);

    auto *heading = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Notes"));
    title->setObjectName(QStringLiteral("pageTitle"));
    heading->addWidget(title);
    m_notesStatus = new QLabel(QStringLiteral(
        "Local notes never leave this computer. Cloud notes use your account, "
        "and publishing one puts it on the web."));
    m_notesStatus->setObjectName(QStringLiteral("mutedLabel"));
    heading->addWidget(m_notesStatus);
    heading->addStretch();
    auto *versions = new QPushButton(QStringLiteral("Versions"));
    versions->setObjectName(QStringLiteral("ghostButton"));
    setOcticon(versions, "history", 14);
    connect(versions, &QPushButton::clicked, this,
            &MainWindow::showNoteVersions);
    heading->addWidget(versions);
    auto *share = new QPushButton(QStringLiteral("Share"));
    share->setObjectName(QStringLiteral("ghostButton"));
    setOcticon(share, "people", 14);
    connect(share, &QPushButton::clicked, this, &MainWindow::shareNote);
    heading->addWidget(share);
    auto *attach = new QPushButton(QStringLiteral("Attach"));
    attach->setObjectName(QStringLiteral("ghostButton"));
    setOcticon(attach, "link", 14);
    connect(attach, &QPushButton::clicked, this,
            &MainWindow::attachNoteConversation);
    heading->addWidget(attach);
    auto *remove = new QPushButton(QStringLiteral("Delete"));
    remove->setObjectName(QStringLiteral("dangerButton"));
    setOcticon(remove, "trash", 14);
    connect(remove, &QPushButton::clicked, this, &MainWindow::deleteNote);
    heading->addWidget(remove);
    auto *add = new QPushButton(QStringLiteral("New note"));
    add->setObjectName(QStringLiteral("primaryButton"));
    setOcticon(add, "plus", 14);
    connect(add, &QPushButton::clicked, this, &MainWindow::createNote);
    heading->addWidget(add);
    outer->addLayout(heading);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    m_notesList = new QListWidget;
    m_notesList->setObjectName(QStringLiteral("notesList"));
    m_notesList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_notesList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_notesList->setSpacing(2);
    m_notesList->setAlternatingRowColors(false);
    m_notesList->setUniformItemSizes(false);
    m_notesList->setMinimumWidth(220);
    connect(m_notesList, &QListWidget::itemSelectionChanged, this, [this] {
        QListWidgetItem *item = m_notesList->currentItem();
        if (item)
            selectNote(item->data(kNoteIdRole).toString(),
                       item->data(kNoteStorageRole).toString());
    });
    splitter->addWidget(m_notesList);

    auto *editorHost = new QWidget;
    auto *editorLayout = new QVBoxLayout(editorHost);
    editorLayout->setContentsMargins(12, 0, 0, 0);
    auto *toolbar = new QHBoxLayout;
    m_noteTitle = new QLineEdit;
    m_noteTitle->setPlaceholderText(QStringLiteral("Untitled note"));
    m_noteTitle->setMaxLength(200);
    toolbar->addWidget(m_noteTitle, 1);
    m_noteStorageMode = new QComboBox;
    m_noteStorageMode->addItem(QStringLiteral("Local only"),
                               QStringLiteral("local"));
    m_noteStorageMode->addItem(QStringLiteral("Cloud only"),
                               QStringLiteral("cloud"));
    m_noteStorageMode->addItem(QStringLiteral("Local + cloud"),
                               QStringLiteral("both"));
    m_noteStorageMode->setToolTip(QStringLiteral(
        "Choose exactly where this note is saved. Local-only notes never contact the relay."));
    toolbar->addWidget(m_noteStorageMode);
    m_notePublic = new QCheckBox(QStringLiteral("Publish publicly"));
    m_notePublic->setToolTip(QStringLiteral(
        "Publish this note at a public web address. Needs a cloud storage "
        "mode, because a local-only note never reaches the web."));
    toolbar->addWidget(m_notePublic);
    m_notePublicLink = new QLabel;
    m_notePublicLink->setObjectName(QStringLiteral("mutedLabel"));
    m_notePublicLink->setTextFormat(Qt::RichText);
    m_notePublicLink->setOpenExternalLinks(true);
    m_notePublicLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_notePublicLink->hide();
    toolbar->addWidget(m_notePublicLink);
    auto *save = new QPushButton(QStringLiteral("Save"));
    save->setObjectName(QStringLiteral("primaryButton"));
    setOcticon(save, "check", 14);
    connect(save, &QPushButton::clicked, this, &MainWindow::saveNote);
    toolbar->addWidget(save);
    editorLayout->addLayout(toolbar);
    m_noteEditor = new MarkdownEditor;
    m_noteEditor->setPlaceholderText(QStringLiteral(
        "Write Markdown. Preview it with the Preview tab."));
    editorLayout->addWidget(m_noteEditor, 1);
    splitter->addWidget(editorHost);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    outer->addWidget(splitter, 1);

    connect(m_noteTitle, &QLineEdit::textChanged, this,
            [this] { m_noteDirty = true; });
    connect(m_noteEditor->sourceEdit(), &QPlainTextEdit::textChanged, this,
            [this] { m_noteDirty = true; });
    connect(m_noteStorageMode, &QComboBox::currentIndexChanged, this, [this] {
        m_noteDirty = true;
        updateNotePublishState();
    });
    connect(m_notePublic, &QCheckBox::toggled, this, [this] {
        m_noteDirty = true;
        updateNotePublishState();
    });

    m_noteRefreshTimer = new QTimer(page);
    m_noteRefreshTimer->setInterval(3000);
    connect(m_noteRefreshTimer, &QTimer::timeout, this, [this] {
        if (m_noteDirty || m_selectedNote.isEmpty() ||
            m_selectedNoteStorage == QLatin1String("local") ||
            activeAccountSessionToken().isEmpty())
            return;
        const QString id = m_selectedNote.value(QStringLiteral("cloudId"))
                               .toString(m_selectedNote.value(
                                   QStringLiteral("id")).toString());
        const int known = m_selectedNote.value(QStringLiteral("cloudVersion"))
                              .toInt(m_selectedNote.value(
                                  QStringLiteral("version")).toInt());
        requestNotes(QByteArrayLiteral("GET"),
                     QStringLiteral("/api/notes/%1").arg(id), {},
                     [this, known](bool ok, const QJsonObject &payload,
                                   const QString &) {
                         const QJsonObject note = payload.value(
                             QStringLiteral("note")).toObject();
                         if (ok && note.value(QStringLiteral("version")).toInt() > known) {
                             m_cloudNotes = QJsonArray();
                             m_notesStatus->setText(QStringLiteral(
                                 "A collaborator saved a newer version; refreshing."));
                             refreshNotes();
                         }
                     });
    });
    m_noteRefreshTimer->start();
    return page;
}

void MainWindow::updateNotePublishState()
{
    if (!m_notePublic || !m_noteStorageMode)
        return;
    const QString mode = m_noteStorageMode->currentData().toString();
    const QString role = m_selectedNote.value(QStringLiteral("role"))
                             .toString(QStringLiteral("owner"));
    const bool publishable = mode != QLatin1String("local") &&
                             role == QLatin1String("owner");
    m_notePublic->setEnabled(publishable);
    if (!publishable)
        m_notePublic->setChecked(false);
    if (!m_notePublicLink)
        return;
    const QString cloudId = m_selectedNote.value(QStringLiteral("cloudId"))
        .toString(m_selectedNoteStorage == QLatin1String("cloud")
                      ? m_selectedNote.value(QStringLiteral("id")).toString()
                      : QString());
    const bool published =
        m_selectedNote.value(QStringLiteral("visibility")).toString() ==
        QLatin1String("public");
    if (!published || cloudId.isEmpty()) {
        m_notePublicLink->clear();
        m_notePublicLink->hide();
        if (m_notePublic->isChecked() && m_notePublic->isEnabled()) {
            m_notePublicLink->setText(QStringLiteral(
                "<span>Save to publish</span>"));
            m_notePublicLink->show();
        }
        return;
    }
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/notes/%1").arg(cloudId));
    url.setQuery(QString());
    const QString address = url.toString();
    m_notePublicLink->setText(
        QStringLiteral("<a href=\"%1\">Live on the web</a>").arg(address));
    m_notePublicLink->setToolTip(address);
    m_notePublicLink->show();
}

void MainWindow::requestNotes(const QByteArray &method, const QString &path,
                              const QJsonObject &body,
                              NoteReplyHandler handler)
{
    QUrl url = catalogApiUrl();
    url.setPath(path);
    url.setQuery(QString());
    const QString token = accountSessionTokenForUrl(url);
    if (!m_networkAccess || token.isEmpty()) {
        handler(false, {}, QStringLiteral(
            "Cloud notes need a password sign-in; local notes remain available."));
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") +
                             token.toUtf8());
    const QByteArray encoded = body.isEmpty()
        ? QByteArray()
        : QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (!encoded.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
    QNetworkReply *reply = method == QByteArrayLiteral("GET")
        ? m_networkAccess->get(request)
        : method == QByteArrayLiteral("POST")
        ? m_networkAccess->post(request, encoded)
        : m_networkAccess->sendCustomRequest(request, method, encoded);
    connect(reply, &QNetworkReply::finished, this,
            [reply, handler = std::move(handler)]() mutable {
        const QJsonObject payload =
            QJsonDocument::fromJson(reply->readAll()).object();
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        status >= 200 && status < 300;
        const QString error = ok ? QString() : noteError(
            payload, status ? QStringLiteral("HTTP %1").arg(status)
                            : reply->errorString());
        reply->deleteLater();
        handler(ok, payload, error);
    });
}

void MainWindow::refreshNotes()
{
    if (!m_notesList || m_notesLoading)
        return;
    m_localNotes = readLocalNotes();
    m_notesLoading = true;
    requestNotes(QByteArrayLiteral("GET"), QStringLiteral("/api/notes"), {},
                 [this](bool ok, const QJsonObject &payload,
                        const QString &error) {
        m_notesLoading = false;
        m_cloudNotes = ok
            ? payload.value(QStringLiteral("notes")).toArray()
            : QJsonArray();
        m_notesStatus->setText(ok
            ? QStringLiteral("Local and cloud notes refreshed.")
            : QStringLiteral("Local notes loaded. %1").arg(error));
        renderNotesList(m_selectedNote.value(QStringLiteral("id")).toString());
    });
}

QJsonObject MainWindow::cloudNoteFor(const QJsonObject &note) const
{
    const QString cloudId = note.value(QStringLiteral("cloudId")).toString();
    if (cloudId.isEmpty())
        return {};
    for (const QJsonValue &value : m_cloudNotes) {
        const QJsonObject cloud = value.toObject();
        if (cloud.value(QStringLiteral("id")).toString() == cloudId)
            return cloud;
    }
    // Signed out, or the listing call failed: fall back to the metadata the
    // last successful cloud save mirrored onto the local record.
    return note.contains(QStringLiteral("shares")) ? note : QJsonObject();
}

void MainWindow::renderNotesList(const QString &selectId)
{
    if (!m_notesList)
        return;
    m_notesList->blockSignals(true);
    m_notesList->clear();
    QSet<QString> cloudIds;
    for (const QJsonValue &value : m_localNotes) {
        const QJsonObject note = value.toObject();
        const QString cloudId = note.value(QStringLiteral("cloudId")).toString();
        if (!cloudId.isEmpty())
            cloudIds.insert(cloudId);
        QListWidgetItem *item = noteListItem(
            note, cloudNoteFor(note),
            note.value(QStringLiteral("storage")).toString(
                QStringLiteral("local")),
            !cloudId.isEmpty());
        item->setData(kNoteIdRole,
                      note.value(QStringLiteral("id")).toString());
        item->setData(kNoteStorageRole, QStringLiteral("local"));
        m_notesList->addItem(item);
        if (item->data(kNoteIdRole).toString() == selectId)
            m_notesList->setCurrentItem(item);
    }
    for (const QJsonValue &value : m_cloudNotes) {
        const QJsonObject note = value.toObject();
        const QString id = note.value(QStringLiteral("id")).toString();
        if (cloudIds.contains(id))
            continue;
        QListWidgetItem *item =
            noteListItem(note, note, QStringLiteral("cloud"), true);
        item->setData(kNoteIdRole, id);
        item->setData(kNoteStorageRole, QStringLiteral("cloud"));
        m_notesList->addItem(item);
        if (id == selectId)
            m_notesList->setCurrentItem(item);
    }
    m_notesList->blockSignals(false);
    if (!m_notesList->currentItem() && m_notesList->count())
        m_notesList->setCurrentRow(0);
    if (m_notesList->currentItem())
        selectNote(m_notesList->currentItem()->data(kNoteIdRole).toString(),
                   m_notesList->currentItem()->data(kNoteStorageRole).toString());
}

void MainWindow::selectNote(const QString &id, const QString &storage)
{
    if (m_noteDirty && !m_selectedNote.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Unsaved note"),
            QStringLiteral("Discard the unsaved changes to this note?"));
        if (answer != QMessageBox::Yes)
            return;
    }
    if (storage == QLatin1String("local")) {
        const int index = localNoteIndex(m_localNotes, id);
        if (index < 0)
            return;
        m_selectedNote = m_localNotes.at(index).toObject();
        m_selectedNoteStorage = QStringLiteral("local");
        m_noteTitle->setText(m_selectedNote.value(QStringLiteral("title")).toString());
        m_noteEditor->setMarkdown(m_selectedNote.value(QStringLiteral("markdown")).toString());
        const QString mode = m_selectedNote.value(QStringLiteral("storage"))
                                 .toString(QStringLiteral("local"));
        m_noteStorageMode->setCurrentIndex(
            qMax(0, m_noteStorageMode->findData(mode)));
        m_notePublic->setChecked(
            m_selectedNote.value(QStringLiteral("visibility")).toString() ==
            QLatin1String("public"));
        updateNotePublishState();
        m_noteDirty = false;
        return;
    }
    requestNotes(QByteArrayLiteral("GET"),
                 QStringLiteral("/api/notes/%1").arg(id), {},
                 [this](bool ok, const QJsonObject &payload,
                        const QString &error) {
        if (!ok) {
            m_notesStatus->setText(QStringLiteral("Note unavailable: %1").arg(error));
            return;
        }
        m_selectedNote = payload.value(QStringLiteral("note")).toObject();
        m_selectedNoteStorage = QStringLiteral("cloud");
        m_noteTitle->setText(m_selectedNote.value(QStringLiteral("title")).toString());
        m_noteEditor->setMarkdown(m_selectedNote.value(QStringLiteral("markdown")).toString());
        m_noteStorageMode->setCurrentIndex(
            m_noteStorageMode->findData(QStringLiteral("cloud")));
        m_notePublic->setChecked(
            m_selectedNote.value(QStringLiteral("visibility")).toString() ==
            QLatin1String("public"));
        updateNotePublishState();
        m_noteDirty = false;
    });
}

void MainWindow::createNote()
{
    m_selectedNote = QJsonObject{
        {QStringLiteral("id"), newNoteId()},
        {QStringLiteral("title"), QStringLiteral("Untitled note")},
        {QStringLiteral("markdown"), QString()},
        {QStringLiteral("storage"), QStringLiteral("local")},
        {QStringLiteral("version"), 0},
    };
    m_selectedNoteStorage = QStringLiteral("local");
    m_noteTitle->setText(QStringLiteral("Untitled note"));
    m_noteEditor->setMarkdown(QString());
    m_noteStorageMode->setCurrentIndex(0);
    m_notePublic->setChecked(false);
    updateNotePublishState();
    m_noteDirty = true;
    m_noteTitle->selectAll();
    m_noteTitle->setFocus();
}

void MainWindow::saveNote()
{
    if (m_selectedNote.isEmpty())
        return createNote();
    const QString title = m_noteTitle->text().trimmed().isEmpty()
        ? QStringLiteral("Untitled note") : m_noteTitle->text().trimmed();
    const QString body = m_noteEditor->markdown();
    const QString mode = m_noteStorageMode->currentData().toString();
    const bool keepLocal = mode != QLatin1String("cloud");
    const bool keepCloud = mode != QLatin1String("local");
    auto saveLocal = [this, title, body, mode](QJsonObject note) {
        note = localSnapshot(note, title, body, mode);
        const int index = localNoteIndex(
            m_localNotes, note.value(QStringLiteral("id")).toString());
        if (index >= 0)
            m_localNotes.replace(index, note);
        else
            m_localNotes.prepend(note);
        if (!writeLocalNotes(m_localNotes)) {
            m_notesStatus->setText(QStringLiteral("Could not save the local note."));
            return;
        }
        m_selectedNote = note;
        m_selectedNoteStorage = QStringLiteral("local");
        m_noteDirty = false;
        renderNotesList(note.value(QStringLiteral("id")).toString());
        m_notesStatus->setText(QStringLiteral("Saved locally."));
    };
    if (!keepCloud) {
        saveLocal(m_selectedNote);
        return;
    }
    const QString cloudId = m_selectedNote.value(QStringLiteral("cloudId"))
                                .toString(m_selectedNoteStorage == QLatin1String("cloud")
                                    ? m_selectedNote.value(QStringLiteral("id")).toString()
                                    : QString());
    QJsonObject requestBody{
        {QStringLiteral("title"), title},
        {QStringLiteral("markdown"), body},
        {QStringLiteral("visibility"),
         m_notePublic->isChecked() ? QStringLiteral("public")
                                   : QStringLiteral("private")},
    };
    QByteArray method = QByteArrayLiteral("POST");
    QString path = QStringLiteral("/api/notes");
    if (!cloudId.isEmpty()) {
        method = QByteArrayLiteral("PATCH");
        path += QStringLiteral("/%1").arg(cloudId);
        requestBody.insert(QStringLiteral("baseVersion"),
            m_selectedNote.value(QStringLiteral("cloudVersion"))
                .toInt(m_selectedNote.value(QStringLiteral("version")).toInt()));
    }
    QJsonObject pending = m_selectedNote;
    if (!cloudId.isEmpty())
        pending.insert(QStringLiteral("cloudId"), cloudId);
    requestNotes(method, path, requestBody,
                 [this, keepLocal, saveLocal, pending](
                     bool ok, const QJsonObject &payload,
                     const QString &error) mutable {
        if (!ok) {
            saveLocal(pending);
            m_notesStatus->setText(QStringLiteral(
                "Cloud save failed: %1 Kept on this computer; save again "
                "once you are signed in.").arg(error));
            return;
        }
        QJsonObject cloud = payload.value(QStringLiteral("note")).toObject();
        const QString id = cloud.value(QStringLiteral("id")).toString();
        int cloudIndex = -1;
        for (int i = 0; i < m_cloudNotes.size(); ++i) {
            if (m_cloudNotes.at(i).toObject().value(QStringLiteral("id"))
                    .toString() == id)
                cloudIndex = i;
        }
        if (cloudIndex >= 0)
            m_cloudNotes.replace(cloudIndex, cloud);
        else
            m_cloudNotes.prepend(cloud);
        if (keepLocal) {
            QJsonObject local = pending;
            local.insert(QStringLiteral("cloudId"),
                         cloud.value(QStringLiteral("id")));
            local.insert(QStringLiteral("cloudVersion"),
                         cloud.value(QStringLiteral("version")));
            local.insert(QStringLiteral("visibility"),
                         cloud.value(QStringLiteral("visibility")));
            // Mirror the sidebar metadata so the row keeps saying who the
            // note is shared with and how often it has been read while the
            // cloud listing is out of reach (offline, or signed out).
            local.insert(QStringLiteral("shares"),
                         cloud.value(QStringLiteral("shares")));
            local.insert(QStringLiteral("views"),
                         cloud.value(QStringLiteral("views")));
            local.insert(QStringLiteral("readers"),
                         cloud.value(QStringLiteral("readers")));
            saveLocal(local);
            updateNotePublishState();
            m_notesStatus->setText(
                cloud.value(QStringLiteral("visibility")).toString() ==
                        QLatin1String("public")
                    ? QStringLiteral("Saved locally and published to the web.")
                    : QStringLiteral("Saved locally and to the cloud."));
        } else {
            const int localIndex = localNoteIndex(
                m_localNotes, pending.value(QStringLiteral("id")).toString());
            if (localIndex >= 0) {
                m_localNotes.removeAt(localIndex);
                writeLocalNotes(m_localNotes);
            }
            m_selectedNote = cloud;
            m_selectedNoteStorage = QStringLiteral("cloud");
            m_noteDirty = false;
            updateNotePublishState();
            renderNotesList(cloud.value(QStringLiteral("id")).toString());
            m_notesStatus->setText(
                cloud.value(QStringLiteral("visibility")).toString() ==
                        QLatin1String("public")
                    ? QStringLiteral("Saved to the cloud and published to the web.")
                    : QStringLiteral("Saved to the cloud."));
        }
    });
}

void MainWindow::deleteNote()
{
    if (m_selectedNote.isEmpty() || QMessageBox::question(
            this, QStringLiteral("Delete note"),
            QStringLiteral("Delete this note from its selected storage?")) !=
            QMessageBox::Yes)
        return;
    const QString localId = m_selectedNote.value(QStringLiteral("id")).toString();
    const QString cloudId = m_selectedNote.value(QStringLiteral("cloudId"))
                                .toString(m_selectedNoteStorage == QLatin1String("cloud")
                                    ? localId : QString());
    const int index = localNoteIndex(m_localNotes, localId);
    if (index >= 0) {
        m_localNotes.removeAt(index);
        writeLocalNotes(m_localNotes);
    }
    auto finish = [this] {
        m_selectedNote = {};
        m_selectedNoteStorage.clear();
        m_noteDirty = false;
        refreshNotes();
    };
    if (cloudId.isEmpty())
        return finish();
    requestNotes(QByteArrayLiteral("DELETE"),
                 QStringLiteral("/api/notes/%1").arg(cloudId), {},
                 [this, finish](bool ok, const QJsonObject &,
                                const QString &error) {
        if (!ok)
            m_notesStatus->setText(QStringLiteral("Cloud delete failed: %1").arg(error));
        else
            finish();
    });
}

void MainWindow::shareNote()
{
    const QString cloudId = m_selectedNote.value(QStringLiteral("cloudId"))
        .toString(m_selectedNoteStorage == QLatin1String("cloud")
                      ? m_selectedNote.value(QStringLiteral("id")).toString()
                      : QString());
    if (cloudId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Share note"),
            QStringLiteral("Choose Cloud only or Local + cloud and save before sharing."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Share note"));
    auto *form = new QFormLayout(&dialog);
    QComboBox type; type.addItems({QStringLiteral("user"), QStringLiteral("organization")});
    QLineEdit name;
    QComboBox role; role.addItems({QStringLiteral("editor"), QStringLiteral("viewer")});
    form->addRow(QStringLiteral("Share with"), &type);
    form->addRow(QStringLiteral("Name"), &name);
    form->addRow(QStringLiteral("Permission"), &role);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    requestNotes(QByteArrayLiteral("POST"),
                 QStringLiteral("/api/notes/%1/shares").arg(cloudId),
                 QJsonObject{{QStringLiteral("type"), type.currentText()},
                             {QStringLiteral("name"), name.text().trimmed()},
                             {QStringLiteral("role"), role.currentText()}},
                 [this, cloudId](bool ok, const QJsonObject &payload,
                                 const QString &error) {
        if (!ok) {
            m_notesStatus->setText(
                QStringLiteral("Share failed: %1").arg(error));
            return;
        }
        const QJsonArray shares = payload.value(QStringLiteral("shares")).toArray();
        m_selectedNote.insert(QStringLiteral("shares"), shares);
        for (int i = 0; i < m_cloudNotes.size(); ++i) {
            QJsonObject cloud = m_cloudNotes.at(i).toObject();
            if (cloud.value(QStringLiteral("id")).toString() != cloudId)
                continue;
            cloud.insert(QStringLiteral("shares"), shares);
            m_cloudNotes.replace(i, cloud);
            break;
        }
        renderNotesList(m_selectedNote.value(QStringLiteral("id")).toString());
        m_notesStatus->setText(QStringLiteral("Note shared."));
    });
}

void MainWindow::attachNoteConversation()
{
    if (m_selectedNote.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Attach PR, issue, or discussion"));
    auto *form = new QFormLayout(&dialog);
    QLineEdit owner, repo;
    QComboBox kind; kind.addItems({QStringLiteral("issue"), QStringLiteral("pull"), QStringLiteral("discussion")});
    QSpinBox number; number.setRange(1, 999999999);
    form->addRow(QStringLiteral("Owner"), &owner);
    form->addRow(QStringLiteral("Repository"), &repo);
    form->addRow(QStringLiteral("Kind"), &kind);
    form->addRow(QStringLiteral("Number"), &number);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QJsonObject link{{QStringLiteral("owner"), owner.text().trimmed()},
                           {QStringLiteral("repo"), repo.text().trimmed()},
                           {QStringLiteral("kind"), kind.currentText()},
                           {QStringLiteral("number"), number.value()}};
    const QString cloudId = m_selectedNote.value(QStringLiteral("cloudId"))
        .toString(m_selectedNoteStorage == QLatin1String("cloud")
                      ? m_selectedNote.value(QStringLiteral("id")).toString()
                      : QString());
    if (!cloudId.isEmpty()) {
        requestNotes(QByteArrayLiteral("POST"),
                     QStringLiteral("/api/notes/%1/links").arg(cloudId), link,
                     [this](bool ok, const QJsonObject &, const QString &error) {
            m_notesStatus->setText(ok ? QStringLiteral("Conversation attached.")
                                      : QStringLiteral("Attach failed: %1").arg(error));
        });
    }
    QJsonArray links = m_selectedNote.value(QStringLiteral("links")).toArray();
    links.append(link);
    m_selectedNote.insert(QStringLiteral("links"), links);
    m_noteDirty = true;
    if (cloudId.isEmpty())
        saveNote();
}

void MainWindow::showNoteVersions()
{
    if (m_selectedNote.isEmpty())
        return;
    const QString cloudId = m_selectedNote.value(QStringLiteral("cloudId"))
        .toString(m_selectedNoteStorage == QLatin1String("cloud")
                      ? m_selectedNote.value(QStringLiteral("id")).toString()
                      : QString());
    if (cloudId.isEmpty()) {
        const QJsonArray versions = m_selectedNote.value(QStringLiteral("versions")).toArray();
        QStringList labels;
        for (const QJsonValue &value : versions) {
            const QJsonObject version = value.toObject();
            labels << QStringLiteral("Version %1 — %2")
                .arg(version.value(QStringLiteral("version")).toInt())
                .arg(QDateTime::fromMSecsSinceEpoch(
                    static_cast<qint64>(version.value(QStringLiteral("createdAt")).toDouble()))
                    .toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        }
        bool ok = false;
        const QString chosen = QInputDialog::getItem(
            this, QStringLiteral("Local note versions"),
            QStringLiteral("Restore snapshot"), labels, 0, false, &ok);
        if (!ok || chosen.isEmpty())
            return;
        const int index = labels.indexOf(chosen);
        const QJsonObject version = versions.at(index).toObject();
        m_noteTitle->setText(version.value(QStringLiteral("title")).toString());
        m_noteEditor->setMarkdown(version.value(QStringLiteral("markdown")).toString());
        m_noteDirty = true;
        return;
    }
    requestNotes(QByteArrayLiteral("GET"),
                 QStringLiteral("/api/notes/%1/versions").arg(cloudId), {},
                 [this, cloudId](bool ok, const QJsonObject &payload,
                                 const QString &error) {
        if (!ok) {
            m_notesStatus->setText(QStringLiteral("Versions unavailable: %1").arg(error));
            return;
        }
        QStringList labels;
        const QJsonArray versions = payload.value(QStringLiteral("versions")).toArray();
        for (const QJsonValue &value : versions)
            labels << QStringLiteral("Version %1").arg(
                value.toObject().value(QStringLiteral("version")).toInt());
        bool accepted = false;
        const QString chosen = QInputDialog::getItem(
            this, QStringLiteral("Cloud note versions"),
            QStringLiteral("Restore as a new version"), labels, 0, false,
            &accepted);
        if (!accepted || chosen.isEmpty())
            return;
        const int version = chosen.section(QLatin1Char(' '), -1).toInt();
        requestNotes(QByteArrayLiteral("POST"),
                     QStringLiteral("/api/notes/%1/versions").arg(cloudId),
                     QJsonObject{{QStringLiteral("version"), version},
                                 {QStringLiteral("baseVersion"),
                                  m_selectedNote.value(QStringLiteral("version"))}},
                     [this](bool restored, const QJsonObject &result,
                            const QString &restoreError) {
            if (!restored) {
                m_notesStatus->setText(QStringLiteral("Restore failed: %1").arg(restoreError));
                return;
            }
            m_selectedNote = result.value(QStringLiteral("note")).toObject();
            m_noteTitle->setText(m_selectedNote.value(QStringLiteral("title")).toString());
            m_noteEditor->setMarkdown(m_selectedNote.value(QStringLiteral("markdown")).toString());
            m_noteDirty = false;
            m_notesStatus->setText(QStringLiteral("Snapshot restored as a new version."));
        });
    });
}
