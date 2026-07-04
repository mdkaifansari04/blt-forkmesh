// MainWindowSetup: MainWindow feature methods, split out of MainWindow.cpp.
// Onboarding & session: setup page, account/node registration, chat-history persistence, and the quick-update flow.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"

using namespace forkmesh::ui;

// ----------------------------------------------------------- chat persistence

QString MainWindow::chatHistoryKey() const
{
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        return {};
    const ServerConfig &s = m_servers.at(m_activeServer);
    const QByteArray seed = (s.url + "\n" + s.room).toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex());
}

QString MainWindow::chatHistoryPath() const
{
    const QString key = chatHistoryKey();
    if (key.isEmpty())
        return {};
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/chat_history/" + key + ".json";
}

QString MainWindow::avatarCachePath(const QString &peerId) const
{
    if (peerId.isEmpty())
        return {};
    // Node ids are base64url, so hash to a filesystem-safe, fixed-length name.
    const QString file = QString::fromLatin1(
        QCryptographicHash::hash(peerId.toUtf8(), QCryptographicHash::Sha256)
            .toHex());
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/avatars/" + file + ".png";
}

void MainWindow::loadCachedAvatars()
{
    // Restore avatars saved on previous sessions so message rows and profiles
    // keep their picture before (or without) the peer re-broadcasting it. The
    // file name is a hash of the node id, so each .png stores the id inside it
    // in a small header line we wrote at save time.
    const QString dir = QStandardPaths::writableLocation(
                            QStandardPaths::AppDataLocation) +
                        "/avatars";
    QDir d(dir);
    if (!d.exists())
        return;
    const QFileInfoList files = d.entryInfoList({"*.png"}, QDir::Files);
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = f.readAll();
        f.close();
        // Each cached avatar is "<peerId>\n" followed by the PNG bytes.
        const int nl = data.indexOf('\n');
        if (nl <= 0)
            continue;
        const QString peerId = QString::fromUtf8(data.left(nl));
        QPixmap pixmap;
        if (peerId.isEmpty() || !pixmap.loadFromData(data.mid(nl + 1)))
            continue;
        if (!m_avatars.contains(peerId))
            m_avatars.insert(peerId, pixmap);
    }
}

void MainWindow::saveChatHistory()
{
    const QString path = chatHistoryPath();
    if (path.isEmpty())
        return;

    QJsonObject conversations;
    for (auto it = m_history.constBegin(); it != m_history.constEnd(); ++it) {
        QJsonArray arr;
        const QList<ChatMessage> &msgs = it.value();
        // Keep the file bounded: only the most recent messages per conversation.
        const int first = std::max(0, int(msgs.size()) - 1000);
        for (int i = first; i < msgs.size(); ++i) {
            const ChatMessage &m = msgs.at(i);
            QJsonObject obj{{"id", m.id},
                            {"senderId", m.senderId},
                            {"senderName", m.senderName},
                            {"text", m.text},
                            {"ts", m.timestampMs},
                            {"self", m.self},
                            {"edited", m.edited},
                            {"deleted", m.deleted}};
            if (m.hasFile()) {
                obj.insert("fileName", m.fileName);
                obj.insert("fileMime", m.fileMime);
                // Inline small attachments so they survive a restart.
                if (m.fileData.size() <= 512 * 1024)
                    obj.insert("fileData",
                               QString::fromLatin1(m.fileData.toBase64()));
            }
            arr.append(obj);
        }
        if (!arr.isEmpty())
            conversations.insert(it.key(), arr);
    }

    QJsonArray openDms;
    for (const QString &peer : std::as_const(m_openDms))
        openDms.append(peer);
    QJsonObject dmNames;
    for (auto it = m_dmNames.constBegin(); it != m_dmNames.constEnd(); ++it)
        dmNames.insert(it.key(), it.value());

    QJsonObject unreadObj;
    for (auto it = m_unreadCounts.constBegin(); it != m_unreadCounts.constEnd(); ++it)
        unreadObj.insert(it.key(), it.value());

    const QJsonObject root{{"current", m_currentConversation},
                           {"openDms", openDms},
                           {"dmNames", dmNames},
                           {"unread", unreadObj},
                           {"conversations", conversations}};
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void MainWindow::loadChatHistory()
{
    const QString path = chatHistoryPath();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    // Messages already past the 7-day retention window (e.g. the app was shut
    // down for a while) are skipped entirely rather than reloaded and pruned
    // later.
    const qint64 expiryCutoff =
        QDateTime::currentMSecsSinceEpoch() - kChatMessageRetentionMs;

    const QJsonObject conversations = root.value("conversations").toObject();
    for (auto it = conversations.constBegin(); it != conversations.constEnd(); ++it) {
        const QString conversation = it.key();
        QList<ChatMessage> &dest = m_history[conversation];
        for (const QJsonValue &v : it.value().toArray()) {
            const QJsonObject obj = v.toObject();
            ChatMessage m;
            m.id = obj.value("id").toString();
            m.conversation = conversation;
            m.senderId = obj.value("senderId").toString();
            m.senderName = obj.value("senderName").toString();
            m.text = obj.value("text").toString();
            m.timestampMs = obj.value("ts").toVariant().toLongLong();
            m.self = obj.value("self").toBool();
            m.edited = obj.value("edited").toBool();
            m.deleted = obj.value("deleted").toBool();
            m.fileName = obj.value("fileName").toString();
            m.fileMime = obj.value("fileMime").toString();
            if (obj.contains("fileData"))
                m.fileData = QByteArray::fromBase64(
                    obj.value("fileData").toString().toLatin1());
            if (m.timestampMs <= expiryCutoff) {
                // Remember the id even though the message is dropped: a peer
                // that hasn't pruned yet may replay it on reconnect, and an
                // unknown id would resurrect it as a fresh unread message.
                if (!m.id.isEmpty())
                    m_historyIds.insert(m.id);
                continue;
            }
            if (!m.id.isEmpty()) {
                if (m_historyIds.contains(m.id))
                    continue;
                m_historyIds.insert(m.id);
            }
            dest.append(m);
        }
    }

    // Restore the open DM tabs and their display names.
    const QJsonObject dmNames = root.value("dmNames").toObject();
    for (auto it = dmNames.constBegin(); it != dmNames.constEnd(); ++it)
        m_dmNames.insert(it.key(), it.value().toString());
    for (const QJsonValue &v : root.value("openDms").toArray()) {
        const QString peer = v.toString();
        if (!peer.isEmpty() && !m_openDms.contains(peer))
            m_openDms.append(peer);
    }
    refreshDmList();

    // Restore the unread state for conversations the user was not actively reading
    // when they closed the app. This prevents notifications from re-appearing for
    // already-read messages when history is replayed from the relay (issue #89).
    const QJsonObject unreadObj = root.value("unread").toObject();
    for (auto it = unreadObj.constBegin(); it != unreadObj.constEnd(); ++it) {
        const QString conv = it.key();
        const int count = it.value().toInt();
        if (!conv.isEmpty() && count > 0) {
            m_unread.insert(conv);
            m_unreadCounts[conv] = count;
        }
    }
    if (!m_unread.isEmpty()) {
        refreshChannelList();
        refreshDmList();
        updateChatButton();
    }

    // Reopen the last conversation so history is visible immediately.
    const QString current = root.value("current").toString();
    if (!current.isEmpty() && current != m_currentConversation &&
        m_history.contains(current)) {
        switchConversation(current);
    } else if (!m_currentConversation.isEmpty()) {
        // History often loads *after* the relay has already selected a channel
        // (e.g. #general). switchConversation() no-ops when the target is the
        // current conversation, which left the view empty until you switched
        // away and back. Re-render the open conversation so the just-loaded
        // history shows up on first load.
        rebuildConversationView();
    }
}

void MainWindow::scheduleChatSave()
{
    if (!m_chatSaveTimer) {
        m_chatSaveTimer = new QTimer(this);
        m_chatSaveTimer->setSingleShot(true);
        connect(m_chatSaveTimer, &QTimer::timeout, this,
                &MainWindow::saveChatHistory);
    }
    m_chatSaveTimer->start(1500);
}

void MainWindow::pruneExpiredChatHistory()
{
    const qint64 cutoff =
        QDateTime::currentMSecsSinceEpoch() - kChatMessageRetentionMs;
    bool changedCurrent = false;
    bool changedAny = false;
    for (auto it = m_history.begin(); it != m_history.end(); ++it) {
        QList<ChatMessage> &messages = it.value();
        // Messages are appended in arrival order, so expired ones are always a
        // prefix; evict from the front instead of scanning the whole list.
        // The evicted ids stay in m_historyIds on purpose: a peer that hasn't
        // pruned yet can replay an expired message on reconnect, and forgetting
        // the id would resurrect it as a fresh unread message with a
        // notification the user already saw. The set is rebuilt from the
        // (bounded) history file on restart, so it can't grow without limit.
        bool changed = false;
        while (!messages.isEmpty() && messages.first().timestampMs <= cutoff) {
            messages.removeFirst();
            changed = true;
        }
        if (changed) {
            changedAny = true;
            if (it.key() == m_currentConversation)
                changedCurrent = true;
        }
    }
    if (!changedAny)
        return;
    if (changedCurrent)
        rebuildConversationView();
    scheduleChatSave();
}

// ---------------------------------------------------------------- setup page

QWidget *MainWindow::buildSetupPage()
{
    auto *page = new QWidget;

    auto *card = new QWidget;
    card->setObjectName("setupCard");
    card->setFixedWidth(420);

    auto *title = new QLabel("<span style='color:#22c55e'>Fork</span>Mesh");
    title->setObjectName("appTitle");
    title->setAlignment(Qt::AlignHCenter);
    auto *subtitle = new QLabel(
        "Pick a name and join the mesh \xE2\x80\x94 preserve code, mirror repos, and chat.");
    subtitle->setObjectName("appSubtitle");
    subtitle->setAlignment(Qt::AlignHCenter);
    subtitle->setWordWrap(true);
    auto *versionLabel = new QLabel("v" FORKMESH_VERSION);
    versionLabel->setObjectName("versionLabel");
    versionLabel->setAlignment(Qt::AlignHCenter);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText("Pick a username (e.g. ada-lovelace)");
    m_nameEdit->setMaxLength(63);
    m_nameEdit->setText(savedProfileName());
    // Only allow characters a username can contain, so spaces and symbols can't
    // be typed or pasted in the first place (validated again on submit).
    m_nameEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[A-Za-z0-9-]*")), m_nameEdit));
    auto *nameHint = new QLabel(
        "Your username on the mesh \xE2\x80\x94 letters, numbers and hyphens, no spaces.");
    nameHint->setObjectName("modeHint");
    nameHint->setWordWrap(true);
    // Payout Solana address is no longer collected on first run — it is set later
    // from Settings once the user is logged in. The widget is kept (hidden) as a
    // data-holder so the profile/settings sync and session start paths that read
    // it keep working unchanged.
    m_solanaEdit = new QLineEdit(card);
    m_solanaEdit->setMaxLength(64);
    m_solanaEdit->setText(savedSolanaAddress());
    m_solanaEdit->hide();
    // The node's public key is no longer surfaced on the welcome screen (it kept
    // the first run feeling technical). The label is kept as a hidden data-holder
    // so the code that fills it after identity load keeps working unchanged.
    m_pubkeyLabel = new QLabel("Ed25519 public key: generating...");
    m_pubkeyLabel->setObjectName("modeHint");
    m_pubkeyLabel->setWordWrap(true);
    m_pubkeyLabel->hide();

    // Relay server: users only ever see the host (e.g. "forkmesh.com"). The full
    // wss:// API/room URL is assembled in code (canonicalServerUrl). The field is
    // pre-filled with the default host and styled muted ("greyed out") to signal
    // that most people can leave it alone — but it stays editable for anyone
    // self-hosting a relay.
    auto *serverLabel = new QLabel("Relay server");
    serverLabel->setObjectName("modeHint");
    m_serverUrlEdit = new QLineEdit;
    m_serverUrlEdit->setPlaceholderText(serverHostDisplay(kDefaultServerUrl));
    m_serverUrlEdit->setMaxLength(2048);
    m_serverUrlEdit->setObjectName("relayHostEdit");
    const QString savedServerUrl = QSettings().value(kServerUrlSetting).toString().trimmed();
    const bool legacyWorkersDevUrl = QUrl(savedServerUrl).host().endsWith(
        QStringLiteral(".workers.dev"));
    const QString effectiveServerUrl =
        savedServerUrl.isEmpty() || savedServerUrl == kLocalServerUrl ||
                legacyWorkersDevUrl
            ? kDefaultServerUrl
            : savedServerUrl;
    m_serverUrlEdit->setText(serverHostDisplay(effectiveServerUrl));

    // The default repository room is fixed so every node converges on the same
    // shared rooms (issue #116). It is no longer shown on the setup screen — the
    // widget is kept as a hidden data-holder so the multi-server config and the
    // session start path keep working unchanged. It always holds the default.
    m_roomNameEdit = new QLineEdit(card);
    m_roomNameEdit->setMaxLength(80);
    m_roomNameEdit->setText(kDefaultRoomName);
    m_roomNameEdit->setReadOnly(true);
    m_roomNameEdit->hide();

    auto *mainnodeHint = new QLabel(
        "Mainnodes relay encrypted repository-room ciphertext only.");
    mainnodeHint->setObjectName("modeHint");

    m_setupError = new QLabel;
    m_setupError->setWordWrap(true);
    m_setupError->setStyleSheet("color:#ff6b6b; background:transparent;");
    m_setupError->hide();

    auto *startButton = new QPushButton("Start ForkMesh node");
    startButton->setObjectName("primaryButton");
    startButton->setMinimumHeight(40);

    // "Join the network (donate)" was removed from first run — joining/donating
    // now happens in-app after starting, mirroring the website flow (browse first,
    // then sign up). The Quick update button is likewise gone from this screen;
    // its widgets are kept (hidden) so runQuickUpdate()/setUpdateStatus() — which
    // are also reachable from the in-app rebuild path — keep functioning.
    m_updateButton = new QPushButton(card);
    m_updateButton->hide();
    m_updateStatus = new QLabel(card);
    m_updateStatus->setObjectName("modeHint");
    m_updateStatus->setWordWrap(true);
    m_updateStatus->setAlignment(Qt::AlignHCenter);
    m_updateStatus->hide();

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 28, 28, 28);
    cardLayout->setSpacing(10);
    cardLayout->addWidget(title);
    cardLayout->addWidget(subtitle);
    cardLayout->addWidget(versionLabel);
    cardLayout->addSpacing(14);
    cardLayout->addWidget(m_nameEdit);
    cardLayout->addWidget(nameHint);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(serverLabel);
    cardLayout->addWidget(m_serverUrlEdit);
    cardLayout->addWidget(mainnodeHint);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(m_setupError);
    cardLayout->addSpacing(10);
    cardLayout->addWidget(startButton);

    auto *setupContent = new QWidget;
    auto *setupContentLayout = new QVBoxLayout(setupContent);
    setupContentLayout->addStretch();
    setupContentLayout->addWidget(card, 0, Qt::AlignHCenter);
    setupContentLayout->addStretch();

    auto *setupScroll = new QScrollArea;
    setupScroll->setWidgetResizable(true);
    setupScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setupScroll->setFrameShape(QFrame::NoFrame);
    // Let the setup card scroll in short windows instead of fixing window height.
    setupScroll->setMinimumHeight(0);
    setupScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    setupScroll->setWidget(setupContent);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(setupScroll);

    connect(startButton, &QPushButton::clicked, this, &MainWindow::startSession);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, &MainWindow::startSession);
    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &name) {
        // Live-lowercase so the name reads exactly as it will register, and clear
        // any stale validation error as soon as the user starts fixing it.
        const QString lower = name.toLower();
        if (lower != name) {
            const int pos = m_nameEdit->cursorPosition();
            QSignalBlocker block(m_nameEdit);
            m_nameEdit->setText(lower);
            m_nameEdit->setCursorPosition(pos);
        }
        if (m_setupError)
            m_setupError->hide();
        saveProfileName(lower);
    });
    // The field holds a bare host; persist the canonical full relay URL so every
    // other consumer (which expects a complete wss:// URL) keeps working.
    connect(m_serverUrlEdit, &QLineEdit::textEdited, this, [](const QString &host) {
        QSettings().setValue(kServerUrlSetting, canonicalServerUrl(host));
    });

    return page;
}

#ifdef FORKMESH_WINDOW_TESTS
void MainWindow::testSetSetupInputs(const QString &name, const QString &solana)
{
    if (m_nameEdit)
        m_nameEdit->setText(name);
    if (m_solanaEdit)
        m_solanaEdit->setText(solana);
    QSettings().setValue(kSolanaSetting, solana.trimmed());
}

int MainWindow::testStackIndex() const
{
    return m_stack ? m_stack->currentIndex() : -1;
}

QString MainWindow::testSavedSolanaAddress() const
{
    return QSettings().value(kSolanaSetting).toString().trimmed();
}

bool MainWindow::testColumnsBecomeResizable()
{
    // Mirror a real data table's header: a Stretch flex column, two
    // content-fitted columns, and a Fixed button column.
    QTableWidget table(0, 4);
    QHeaderView *header = table.horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::Fixed);
    makeColumnsResizable(&table);

    // Nothing changes until real rows populate the table.
    if (header->sectionResizeMode(1) != QHeaderView::ResizeToContents ||
        header->sectionResizeMode(2) != QHeaderView::ResizeToContents)
        return false;

    table.insertRow(0);
    table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("flex")));
    table.setItem(0, 1, new QTableWidgetItem(QStringLiteral("short")));
    table.setItem(0, 2,
                  new QTableWidgetItem(QStringLiteral("a much wider cell value")));
    table.setItem(0, 3, new QTableWidgetItem(QStringLiteral("x")));
    // The snapshot/switch is deferred to the next event-loop turn.
    QApplication::processEvents();
    QApplication::processEvents();

    // Spreadsheet semantics: the flex/Stretch column also becomes draggable
    // (fitted to the width of its widest data); only Fixed button columns stay.
    const bool flexDraggable = header->sectionResizeMode(0) == QHeaderView::Interactive;
    const bool fixedUntouched = header->sectionResizeMode(3) == QHeaderView::Fixed;
    const bool col1Draggable = header->sectionResizeMode(1) == QHeaderView::Interactive;
    const bool col2Draggable = header->sectionResizeMode(2) == QHeaderView::Interactive;
    // The fitted widths survive the switch, so the wider column stays wider.
    const bool widthsPreserved = header->sectionSize(2) > header->sectionSize(1);
    return flexDraggable && fixedUntouched && col1Draggable && col2Draggable &&
           widthsPreserved;
}

bool MainWindow::testSpreadsheetResize()
{
    // Mirror the issue table: a leading content column, a Stretch flex column
    // (Title), then several content-fitted columns. After makeColumnsResizable
    // every column is independently draggable, like a spreadsheet.
    QTableWidget table(0, 5);
    QHeaderView *header = table.horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents); // #
    header->setSectionResizeMode(1, QHeaderView::Stretch);          // Title (flex)
    for (int i = 2; i < 5; ++i)
        header->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    makeColumnsResizable(&table);
    table.resize(900, 200);

    table.insertRow(0);
    table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("1")));
    table.setItem(0, 1, new QTableWidgetItem(QStringLiteral("a flexible title")));
    table.setItem(0, 2, new QTableWidgetItem(QStringLiteral("status value")));
    table.setItem(0, 3,
                  new QTableWidgetItem(QStringLiteral("a wider neighbour cell")));
    table.setItem(0, 4, new QTableWidgetItem(QStringLiteral("tail value")));
    // The snapshot/switch is deferred to the next event-loop turn.
    QApplication::processEvents();
    QApplication::processEvents();

    // Every column, including the former Stretch flex column, is now draggable.
    const bool flexDraggable =
        header->sectionResizeMode(1) == QHeaderView::Interactive;

    // Drag column 2's divider wider. Like a spreadsheet, only column 2 grows; the
    // columns to its right keep their widths and simply shift over (no neighbour
    // silently donates width), so the table gets wider overall.
    const int before2 = header->sectionSize(2);
    const int before3 = header->sectionSize(3);
    const int before4 = header->sectionSize(4);
    const int delta = 24;
    header->resizeSection(2, before2 + delta);
    QApplication::processEvents();

    const bool draggedGrew = header->sectionSize(2) == before2 + delta;
    const bool neighborUntouched = header->sectionSize(3) == before3;
    const bool tailUntouched = header->sectionSize(4) == before4;
    return flexDraggable && draggedGrew && neighborUntouched && tailUntouched;
}

bool MainWindow::testSpreadsheetResizeAfterMove()
{
    // Three draggable content columns. Reordering them must not change the
    // spreadsheet rule: resizing one column never disturbs the others' widths.
    QTableWidget table(0, 3);
    QHeaderView *header = table.horizontalHeader();
    header->setSectionsMovable(true);
    for (int i = 0; i < 3; ++i)
        header->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    makeColumnsResizable(&table);
    table.resize(600, 200);

    table.insertRow(0);
    table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("alpha value")));
    table.setItem(0, 1, new QTableWidgetItem(QStringLiteral("beta value")));
    table.setItem(0, 2, new QTableWidgetItem(QStringLiteral("gamma value")));
    // The snapshot/switch to Interactive is deferred to the next event-loop turn.
    QApplication::processEvents();
    QApplication::processEvents();

    // Move logical column 0 to the far right: visual order becomes 1, 2, 0.
    header->moveSection(header->visualIndex(0), 2);

    // Dragging logical column 1 wider grows only column 1; both other columns
    // (its visual neighbour and the moved column) keep their widths and shift.
    const int before1 = header->sectionSize(1);
    const int before2 = header->sectionSize(2);
    const int before0 = header->sectionSize(0);
    const int delta = 20;
    header->resizeSection(1, before1 + delta);
    QApplication::processEvents();

    const bool draggedGrew = header->sectionSize(1) == before1 + delta;
    const bool neighborUntouched = header->sectionSize(2) == before2;
    const bool movedColumnUntouched = header->sectionSize(0) == before0;
    return draggedGrew && neighborUntouched && movedColumnUntouched;
}

bool MainWindow::testAgentColumnsMovable() const
{
    return m_agentTable && m_agentTable->horizontalHeader()->sectionsMovable();
}

QString MainWindow::testQuickAddAgentProvider() const
{
    return m_quickAddAgentProvider ? m_quickAddAgentProvider->currentData().toString()
                                   : QString();
}

QString MainWindow::testIssueAgentProvider() const
{
    return m_issueAgentProvider ? m_issueAgentProvider->currentData().toString()
                                : QString();
}

QString MainWindow::testWorktreeAheadBehindText(const QString &branch) const
{
    if (!m_worktreesTable)
        return QString();
    for (int row = 0; row < m_worktreesTable->rowCount(); ++row) {
        QTableWidgetItem *b = m_worktreesTable->item(row, 0);
        if (b && b->data(Qt::UserRole).toString() == branch) {
            if (QTableWidgetItem *ab = m_worktreesTable->item(row, 3))
                return ab->text();
        }
    }
    return QString();
}

QString MainWindow::testWorktreeBranchLabel() const
{
    return m_worktreeBranchLabel ? m_worktreeBranchLabel->text() : QString();
}

QString MainWindow::testArrowOnWorktrees(bool down)
{
    if (!m_worktreesTable)
        return QString();
    m_worktreesTable->setFocus();
    const Qt::Key key = down ? Qt::Key_Down : Qt::Key_Up;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(m_worktreesTable, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(m_worktreesTable, &release);
    QApplication::processEvents();
    const int row = m_worktreesTable->currentRow();
    QTableWidgetItem *b = row >= 0 ? m_worktreesTable->item(row, 0) : nullptr;
    return b ? b->data(Qt::UserRole).toString() : QString();
}

void MainWindow::testClickRepoDetailTab(int id)
{
    if (id == 1) {
        // Commits has no top-bar tab anymore: drive the commit strip's
        // "N Commits" toggle instead, the same path a real click takes
        // (no-op when the panel is already showing — click would hide it).
        if (m_historyButton && !m_historyButton->isChecked())
            m_historyButton->click();
        return;
    }
    if (!m_repoDetailTabs)
        return;
    if (QAbstractButton *b = m_repoDetailTabs->button(id))
        b->click(); // emits idClicked(id) -> the same path a real click takes
}

bool MainWindow::testWorktreesTableHasKeyboardFocus() const
{
    return m_worktreesTable && m_worktreesTable->window() &&
           m_worktreesTable->window()->focusWidget() == m_worktreesTable;
}

bool MainWindow::testReleasesTableHasKeyboardFocus() const
{
    return m_releasesTable && m_releasesTable->window() &&
           m_releasesTable->window()->focusWidget() == m_releasesTable;
}

bool MainWindow::testMirrorNodesTableHasKeyboardFocus() const
{
    return m_mirrorNodesTable && m_mirrorNodesTable->window() &&
           m_mirrorNodesTable->window()->focusWidget() == m_mirrorNodesTable;
}

QString MainWindow::testBranchWorktreePath(const QString &branch) const
{
    if (!m_branchesTable)
        return QString();
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        QTableWidgetItem *name = m_branchesTable->item(row, 0);
        if (name && name->text() == branch) {
            if (QTableWidgetItem *wt = m_branchesTable->item(row, 3))
                return wt->text();
        }
    }
    return QString();
}

QString MainWindow::testBranchAttachmentText(const QString &branch) const
{
    if (!m_branchesTable)
        return QString();
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        QTableWidgetItem *name = m_branchesTable->item(row, 0);
        if (name && name->text() == branch) {
            if (QTableWidgetItem *attach = m_branchesTable->item(row, 4))
                return attach->text();
        }
    }
    return QString();
}

bool MainWindow::testBranchAttachmentHasIcon(const QString &branch) const
{
    if (!m_branchesTable)
        return false;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        QTableWidgetItem *name = m_branchesTable->item(row, 0);
        if (name && name->text() == branch) {
            if (QTableWidgetItem *attach = m_branchesTable->item(row, 4))
                return !attach->icon().isNull();
        }
    }
    return false;
}

int MainWindow::testClickBranchAgentCell(const QString &branch)
{
    if (!m_branchesTable)
        return -1;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        QTableWidgetItem *name = m_branchesTable->item(row, 0);
        if (name && name->text() == branch) {
            // Fire the same signal a real click on the Issue / Agent cell would,
            // so the production cellClicked handler runs (adhoc #258).
            emit m_branchesTable->cellClicked(row, 4);
            break;
        }
    }
    return m_selectedAgentSessionId;
}

QStringList MainWindow::testBranchRowOrder() const
{
    QStringList names;
    if (!m_branchesTable)
        return names;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        if (QTableWidgetItem *name = m_branchesTable->item(row, 0))
            names << name->text();
    }
    return names;
}

void MainWindow::testSetDefaultAgentProvider(const QString &provider)
{
    if (!m_defaultAgentProviderCombo)
        return;
    // Drive it like a user picking the value so the connected slot persists the
    // setting and re-seeds the live pickers.
    const int index = m_defaultAgentProviderCombo->findData(provider);
    m_defaultAgentProviderCombo->setCurrentIndex(index >= 0 ? index : 0);
}

int MainWindow::testAddPublishedRepository(const QString &owner, const QString &name,
                                           const QString &mirrorPath)
{
    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.mirrorPath = mirrorPath;
    repo.publishToNetwork = true;
    m_repositories.append(repo);
    return m_repositories.size() - 1;
}

int MainWindow::testAddLocalRepository(const QString &owner, const QString &name,
                                       const QString &localPath)
{
    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.localPath = localPath;
    m_repositories.append(repo);
    return m_repositories.size() - 1;
}

bool MainWindow::testOpenRepository(int index)
{
    if (index < 0 || index >= m_repositories.size())
        return false;
    openRepoDetail(index);
    return true;
}

QString MainWindow::testRepoDefaultBranch() const
{
    return repoDefaultBranch(repoBranches());
}

bool MainWindow::testShowRepoIssuesTab()
{
    if (!m_repoDetailStack || m_repoDetailStack->count() <= 2)
        return false;
    m_repoDetailStack->setCurrentIndex(2); // Issues
    if (m_repoDetailTabs) {
        if (QAbstractButton *b = m_repoDetailTabs->button(2))
            b->setChecked(true);
    }
    return true;
}

void MainWindow::testShowPublishBar(bool on)
{
    if (m_repoPushButton) {
        if (on) {
            m_repoPushButton->setText(QStringLiteral("Sync"));
            m_repoPushButton->setEnabled(true);
            positionRepoPushButton(); // floats it above the Code tab
        }
        m_repoPushButton->setVisible(on);
    }
    if (m_repoPublishBar)
        m_repoPublishBar->setVisible(on);
}

int MainWindow::testRepoTabContentTop()
{
    return m_repoDetailStack ? m_repoDetailStack->mapTo(this, QPoint(0, 0)).y() : -1;
}
#endif

void MainWindow::setHeadlessMode(bool headless)
{
    m_headless = headless;
    // A headless mirror exists to serve its repos, and it has no GUI toggle to
    // bring itself back online. So a persisted parked-offline flag — inherited
    // from a prior desktop session on this box, or left over from before the
    // machine was converted to a headless daemon — would silently strand it:
    // still connected to the room and syncing its mirror (so it publishes a fresh
    // catalog record and shows up in the Mirror nodes list), yet never starting a
    // host tunnel or sending the online heartbeat. The node then appears offline
    // on the Network page and serves nothing, with no way for a headless operator
    // to fix it (adhoc #216: "mirror1" was connected and syncing but never online
    // or serving). Force such a node online here, before startSession reads the
    // flag, so a headless daemon always serves.
    if (headless && m_nodeOffline) {
        m_nodeOffline = false;
        QSettings().setValue(kNodeOfflineSetting, false);
    }
}

void MainWindow::startSession()
{
    // Picking a name is the very first thing on first run — we don't quietly
    // assign a generated one and drop the user into an anonymous "browse" mode.
    // Validate the name explicitly (rather than silently rewriting it) so spaces
    // or an odd first/last character are surfaced instead of mangled.
    const QString raw = m_nameEdit->text().trimmed().toLower();
    if (raw.isEmpty()) {
        m_setupError->setText("Pick a username to get started.");
        m_setupError->show();
        m_nameEdit->setFocus();
        return;
    }
    if (!isValidNodeName(raw)) {
        m_setupError->setText(
            "That username won't work — use lowercase letters, numbers and hyphens "
            "(no spaces), starting with a letter.");
        m_setupError->show();
        m_nameEdit->setFocus();
        return;
    }
    const QString name = raw; // validated; no rewriting needed
    m_nameEdit->setText(name);
    saveProfileName(name);
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        m_setupError->setText(m_profileIdentity.errorString());
        m_setupError->show();
        return;
    }

    // No wallet, no signup: the core flow (clone, mirror, issues, PRs, chat)
    // needs only a node name. Crypto is strictly opt-in and lives behind the
    // "Get paid to mirror" button on the node profile — we never gate entry on
    // an account or a Solana address here. We still attempt a silent, dialog-free
    // auth so a returning node that already owns an active account keeps its
    // hosting/payout privileges; a brand-new node simply starts unauthenticated.
#ifdef FORKMESH_WINDOW_TESTS
    // Tests pin the auth state directly and bypass the server start, so skip the
    // real silent-auth network round-trip here.
    if (!m_testBypassServerStart)
        authenticateSilently(name);
#else
    authenticateSilently(name);
#endif

    // A node that mirrors a public repo needs a key-bound account or the relay
    // rejects its catalog writes and host tokens — so it hosts/chats and shows up
    // online in every peer's roster, yet never appears on the website (adhoc #207:
    // "threaded-byte" was online and mirroring but had no account at all). A
    // headless VM has no GUI to click "Join ForkMesh"; a desktop node that already
    // opted into publishing a mirror has effectively made the same choice. So
    // auto-register (free, key-bound, no dialog) for a headless node OR any node
    // that is already publishing a mirror to the network, mirroring what a desktop
    // user does by hand. Guarded on hasActiveAccountSession() so a returning node
    // whose silent auth already succeeded never re-registers, and skipped under the
    // test server-bypass.
    const bool publishesMirror = [this] {
        for (const RepositoryRecord &r : std::as_const(m_repositories)) {
            if (!r.previewOnly && r.publishToNetwork && !r.mirrorPath.isEmpty() &&
                QDir(r.mirrorPath).exists())
                return true;
        }
        return false;
    }();
    if ((m_headless || publishesMirror) && !hasActiveAccountSession() &&
        isValidNodeName(name)) {
        bool registered = false;
#ifdef FORKMESH_WINDOW_TESTS
        if (!m_testBypassServerStart)
            registered = registerNodeAccountSilently(name);
#else
        registered = registerNodeAccountSilently(name);
#endif
        // A just-registered node has no catalog record yet, and an already-synced
        // mirror won't re-publish on the next (quiet, unchanged) auto-sync — so
        // seed the catalog now for every mirror we already hold. The repo page
        // builds its mirror rows from catalog records, so without this the node
        // would host (host_presence) yet never appear in the list.
        if (registered) {
            for (int i = 0; i < m_repositories.size(); ++i) {
                const RepositoryRecord &r = m_repositories.at(i);
                if (!r.previewOnly && r.publishToNetwork &&
                    !r.mirrorPath.isEmpty() && QDir(r.mirrorPath).exists())
                    publishRepository(i, false);
            }
        } else if (m_headless || publishesMirror) {
            // Nothing else in an unattended run ever calls
            // registerNodeAccountSilently() again — so a transient failure here
            // (relay unreachable right at boot is the common case on a fresh
            // VPS) would otherwise strand the node unregistered forever, even
            // though it keeps mirroring/chatting fine: no account means no host
            // token, so it never marks host_presence and never appears on the
            // website. This covers both a headless VM and a node that only
            // qualified via publishesMirror — the same set registered above.
            // Retry with backoff.
            scheduleHeadlessRegisterRetry(name);
        }
    }

    if (m_serverUrlEdit->text().trimmed().isEmpty())
        m_serverUrlEdit->setText(serverHostDisplay(kDefaultServerUrl));

    saveSolanaAddress(m_solanaEdit->text().trimmed());
    if (m_accountAuthenticated && m_accountName != name) {
        m_accountAuthenticated = false;
        m_accountTier = QStringLiteral("free");
        m_accountSolanaVerified = false;
        m_isAdmin = false;
    }
    m_accountName = name;
    QSettings().setValue(kAccountNameSetting, name);
    refreshSettingsEmailVerifiedBadge();

    m_roomNameEdit->setText(kDefaultRoomName); // fixed shared room (read-only)
    m_setupError->hide();
    m_userName = name;
#ifdef FORKMESH_WINDOW_TESTS
    if (m_testBypassServerStart) {
        m_stack->setCurrentIndex(1);
        return;
    }
#endif
    persistProfile();
    m_userAvatar = QSettings().value(kAvatarSetting).toByteArray();

    // Seed the Settings section's profile controls and the avatar nav button
    // (which now stands in for the old settings gear).
    if (m_settingsNameEdit)
        m_settingsNameEdit->setText(m_userName);
    setSettingsAvatar(m_userAvatar);
    updateAvatarButton();

    // Reset chat state.
    m_channels.clear();
    m_privateChannels.clear(); // repopulated from QSettings by restorePrivateChannels()
    m_welcomeAnnounced = false; // re-evaluate the one-time greeting for this identity
    m_currentConversation.clear();
    m_history.clear();
    m_historyIds.clear();
    m_visibleRows.clear();
    m_reactions.clear();
    m_avatars.clear();
    m_dmNames.clear();
    m_openDms.clear();
    m_unread.clear();
    m_unreadCounts.clear();
    m_typing.clear();
    m_typingConversation.clear();
    m_typingStopTimer->stop();
    m_channelList->clear();
    m_dmList->clear();
    rebuildConversationView();

    const QString fullServerUrl = canonicalServerUrl(m_serverUrlEdit->text());
    QSettings().setValue(kServerUrlSetting, fullServerUrl);
    QSettings().setValue(kRoomNameSetting, kDefaultRoomName);
    persistEditsToActiveServer();
    const QUrl url(fullServerUrl);
    auto *server = new ServerNode(name, m_profileIdentity.publicKey(), url,
                                  kDefaultRoomName,
                                  m_solanaEdit->text().trimmed(), this);
    // Fail over across every configured mainnode (issue #364): the active server
    // is tried first, then the rest in order, so a dead or quota-limited mainnode
    // no longer strands the client. ServerNode rotates through the list on each
    // failed reconnect and heartbeats to whichever mainnode answers.
    QList<QUrl> endpoints{url};
    for (const ServerConfig &cfg : std::as_const(m_servers)) {
        const QUrl endpoint(canonicalServerUrl(cfg.url));
        if (endpoint.isValid() && !endpoints.contains(endpoint))
            endpoints.append(endpoint);
    }
    server->setEndpoints(endpoints);
    attachBackend(server);
    if (!server->start())
        return;

    if (m_backend) {
        if (m_connectedAtMs <= 0)
            m_connectedAtMs = QDateTime::currentMSecsSinceEpoch();
        // Stay quiet about node connections while the initial roster settles, so
        // a restart doesn't fire an alert for every node that is already online.
        m_nodeAlertGraceUntilMs =
            QDateTime::currentMSecsSinceEpoch() + 12000;
        // Broadcast the generated face avatar when no custom avatar is set, so
        // peers always see a unique, identifiable face for this node.
        m_backend->setAvatar(effectiveAvatar());
        // Fixed shared channels for the whole network — no per-repo rooms. Every
        // node joins the same #general, #welcome and #random over the one
        // encrypted room (#welcome carries new-node join greetings, issue #192).
        m_backend->addChannel(QStringLiteral("general"));
        m_backend->addChannel(QStringLiteral("welcome"));
        m_backend->addChannel(QStringLiteral("random"));
        // Re-create any invite-only rooms this node owned or was invited to; the
        // backend clears its channel set each session, so they'd vanish otherwise.
        restorePrivateChannels();
        logSystem("Encryption: client-side AES-256-GCM mainnode room encryption.");
        const QJsonObject signedProfile =
            m_profileIdentity.signedProfile(m_userName,
                                            m_userName,
                                            m_solanaEdit->text());
        const QString profileBytes = QString::fromUtf8(
            QJsonDocument(signedProfile).toJson(QJsonDocument::Compact));
        logSystem("Identity: signed profile for " +
                  m_profileIdentity.shortPublicKey() + " (" +
                  QString::number(profileBytes.toUtf8().size()) + " bytes).");
        m_stack->setCurrentIndex(1);
        showSection(0); // land on the Home overview after connecting
        updateBreadcrumb();
        updateSolanaNotice();
        // Restore locally-saved chat history for this server/room so past
        // conversations are visible right away (deduped against any replay).
        loadCachedAvatars();
        loadChatHistory();
        // Honour a node the user previously parked offline: stay connected for
        // chat, but don't serve repos or send the reward heartbeat until they
        // flip the top-bar toggle back on.
        if (m_nodeOffline) {
            // The uptime clock shouldn't run while offline.
            if (m_connectedAtMs > 0) {
                m_totalConnectionMs +=
                    QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs;
                m_connectedAtMs = 0;
                QSettings().setValue(kConnectionTotalSetting, m_totalConnectionMs);
            }
        } else {
            // Serve already-mirrored repos live to the web for this session.
            startRepoHosts();
            // Heartbeat so an active, online node stays eligible for the reward
            // split. The server ignores it unless the account is active.
            if (!m_heartbeatTimer) {
                m_heartbeatTimer = new QTimer(this);
                m_heartbeatTimer->setInterval(60000);
                connect(m_heartbeatTimer, &QTimer::timeout, this,
                        &MainWindow::sendNodeHeartbeat);
            }
            m_heartbeatTimer->start();
            sendNodeHeartbeat();
        }
        updateNodeOnlineControls();
    }
}

// ---- Account / node registration (staged join + reward heartbeat) ----------

void MainWindow::sendNodeHeartbeat()
{
    const QString name = m_accountName.isEmpty()
                             ? QSettings().value(kAccountNameSetting).toString().trimmed()
                             : m_accountName;
    if (name.isEmpty() || !m_profileIdentity.isValid())
        return;
    // Back off exponentially while the relay is failing (offline / HTTP 429) so
    // a rate-limited node stops beating every single minute into the flood.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_pollBackoff.ready(QStringLiteral("heartbeat"), nowMs))
        return;
    const QString ts = QString::number(nowMs);
    const QByteArray canonical =
        ("forkmesh-heartbeat-v1\n" + name + "\n" + ts).toUtf8();
    const QString solana = savedSolanaAddress();
    // Issue #346: an opt-in refill notification piggybacks this already-signed
    // channel instead of a dedicated endpoint. 5h takes priority when both
    // flags are set in the same tick; the other stays pending for the next beat.
    const QString creditsRefilled = m_pendingCreditsRefilled5h
        ? QStringLiteral("5h")
        : (m_pendingCreditsRefilledWeekly ? QStringLiteral("weekly") : QString());
    QJsonObject body{{"nodeName", name}, {"solana", solana}, {"ts", ts},
                     {"sig", m_profileIdentity.signData(canonical)}};
    if (!creditsRefilled.isEmpty())
        body.insert(QStringLiteral("creditsRefilled"), creditsRefilled);
    QNetworkRequest request(accountsApiUrl("heartbeat"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, creditsRefilled]() {
        if (reply->error() != QNetworkReply::NoError) {
            m_pollBackoff.noteFailure(QStringLiteral("heartbeat"),
                                      QDateTime::currentMSecsSinceEpoch());
            reply->deleteLater();
            return;
        }
        if (creditsRefilled == QLatin1String("5h"))
            m_pendingCreditsRefilled5h = false;
        else if (creditsRefilled == QLatin1String("weekly"))
            m_pendingCreditsRefilledWeekly = false;
        m_pollBackoff.noteSuccess(QStringLiteral("heartbeat"));
        const QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        // The server tells us whether this node is an admin; if so, start
        // watching for newly-joined users that need email verification.
        const bool wasAdmin = m_isAdmin;
        m_isAdmin = resp.value("isAdmin").toBool();
        // Admin status is learned after the initial nav render, so refresh the
        // top-bar name once when it flips to show/hide the crown — without the
        // balance re-query the heartbeat path otherwise avoids.
        if (m_isAdmin != wasAdmin && m_navNodeName) {
            const QString name = accountNameFromInput(m_userName, QString());
            const QString crown = QString::fromUtf8(" \xF0\x9F\x91\x91");
            m_navNodeName->setText(m_isAdmin && !name.isEmpty() ? name + crown : name);
            m_navNodeName->setToolTip(
                m_isAdmin && !name.isEmpty() ? name + " (admin)" : name);
        }
        if (m_isAdmin) {
            if (!m_adminPollTimer) {
                m_adminPollTimer = new QTimer(this);
                m_adminPollTimer->setInterval(90000);
                connect(m_adminPollTimer, &QTimer::timeout, this,
                        &MainWindow::pollPendingUsers);
            }
            if (!m_adminPollTimer->isActive()) {
                m_adminPollTimer->start();
                pollPendingUsers();
            }
        }
        // The nav balance is deliberately NOT refreshed on every heartbeat:
        // hitting Solana RPC + the price API each minute is wasteful. Instead the
        // server reports this node's wallet balance in the heartbeat reply and
        // flags when it grew since the last beat (a donation). Only then do we
        // refresh the displayed balance (which also fires the disbursement
        // notification). Other refreshes happen on startup / address changes.
        if (resp.value(QStringLiteral("donationReceived")).toBool())
            updateNavSolanaBalance();
        // A user on forkmesh.com is claiming this node (adhoc #53): the reply
        // carries the confirmation code, which is shown on this machine only.
        // Typing it into the website completes the link. Guard on the code so
        // the per-minute heartbeat doesn't reopen the popup for one claim.
        const QJsonObject claim = resp.value(QStringLiteral("claim")).toObject();
        const QString claimCode = claim.value(QStringLiteral("code")).toString();
        if (!claimCode.isEmpty() && claimCode != m_lastClaimCodeShown) {
            m_lastClaimCodeShown = claimCode;
            showNodeClaimCode(claim.value(QStringLiteral("user")).toString(),
                              claimCode);
        }
        // An admin requested ownership of THIS node (adhoc #141): the reply
        // carries who's asking, and only this node's own signed decision (made
        // by whoever is currently logged into it) can approve or deny it.
        const QJsonObject transfer =
            resp.value(QStringLiteral("ownershipTransfer")).toObject();
        const QString transferAdmin = transfer.value(QStringLiteral("admin")).toString();
        if (!transferAdmin.isEmpty() &&
            transferAdmin != m_lastOwnershipTransferAdminShown) {
            m_lastOwnershipTransferAdminShown = transferAdmin;
            showOwnershipTransferPrompt(transferAdmin);
        } else if (transferAdmin.isEmpty()) {
            m_lastOwnershipTransferAdminShown.clear();
        }
    });
}

void MainWindow::showNodeClaimCode(const QString &user, const QString &code)
{
    const QString who = user.isEmpty() ? QStringLiteral("A user") : user;
    // Headless nodes (the usual claim target) have no screen: put the code on
    // the console/log, which is what an SSH'd operator is looking at.
    logSystem("Account: user \"" + who + "\" is claiming this node on the "
              "website. Confirmation code: " + code + " — enter it there to "
              "link this node to that account (expires in 10 minutes).");
    if (m_headless)
        return;
    auto *box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(QStringLiteral("Link this node?"));
    box->setIcon(QMessageBox::Information);
    box->setText(
        QStringLiteral("<b>%1</b> is claiming this node on forkmesh.com.<br><br>"
                       "Confirmation code:"
                       "<div style='font-size:28px;letter-spacing:6px'><b>%2</b></div>"
                       "Enter this code on the website to link this node to that "
                       "account. If this isn't you, just close this window — the "
                       "code expires in 10 minutes and is never sent anywhere "
                       "else.")
            .arg(who.toHtmlEscaped(), code.toHtmlEscaped()));
    box->setStandardButtons(QMessageBox::Close);
    box->setModal(false);
    box->show();
    box->raise();
    box->activateWindow();
}

void MainWindow::showOwnershipTransferPrompt(const QString &admin)
{
    const QString who = admin.isEmpty() ? QStringLiteral("An admin") : admin;
    logSystem("Account: admin \"" + who + "\" has requested ownership of this "
              "node. Approve or deny from the node's profile" +
              (m_headless ? QStringLiteral(" (headless — leave pending or "
                                           "resolve from another client).")
                          : QStringLiteral(".")));
    if (m_headless)
        return;
    auto *box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(QStringLiteral("Ownership transfer request"));
    box->setIcon(QMessageBox::Warning);
    box->setText(
        QStringLiteral("<b>%1</b> (admin) is requesting ownership of this "
                       "node.<br><br>Approving hands this node over to that "
                       "account and removes it from its current owner's "
                       "fleet. If you didn't expect this, choose Deny.")
            .arg(who.toHtmlEscaped()));
    QPushButton *approve = box->addButton("Approve", QMessageBox::AcceptRole);
    QPushButton *deny = box->addButton("Deny", QMessageBox::RejectRole);
    box->setDefaultButton(deny);
    box->setModal(false);
    connect(box, &QMessageBox::buttonClicked, this,
            [this, box, approve](QAbstractButton *clicked) {
                submitOwnershipTransferDecision(clicked == approve);
            });
    box->show();
    box->raise();
    box->activateWindow();
}

void MainWindow::submitOwnershipTransferDecision(bool approve)
{
    const QString node = accountOwner();
    if (node.isEmpty() || !m_profileIdentity.isValid())
        return;
    const QString action = approve ? QStringLiteral("approve")
                                   : QStringLiteral("deny");
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-ownership-transfer-confirm-v1\n" + node + "\n" + action +
         "\n" + ts)
            .toUtf8();
    int status = 0;
    const QJsonObject resp = postAccountSync(
        "ownership-transfer-confirm",
        QJsonObject{{"nodeName", node}, {"action", action}, {"ts", ts},
                    {"sig", m_profileIdentity.signData(canonical)}},
        &status);
    m_lastOwnershipTransferAdminShown.clear();
    if (status == 200 && resp.value("ok").toBool()) {
        logSystem(approve ? "Account: ownership transfer approved."
                          : "Account: ownership transfer denied.");
        if (approve)
            refreshProfileAccountStatus();
    } else {
        logSystem("Account: ownership transfer decision failed to submit.");
    }
}

void MainWindow::requestNodeOwnership()
{
    if (!m_isAdmin)
        return;
    const QString target = m_profileNodeName;
    const QString node = accountOwner();
    if (target.isEmpty() || node.isEmpty() || !m_profileIdentity.isValid())
        return;
    if (QMessageBox::question(
            this, "Take ownership",
            QStringLiteral("Request ownership of <b>%1</b>?<br><br>This only "
                           "sends a request — it takes effect once that "
                           "node's current owner approves the confirmation "
                           "prompt it receives.")
                .arg(target.toHtmlEscaped()),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-admin-request-ownership-v1\n" + node + "\n" + target +
         "\n" + ts)
            .toUtf8();
    int status = 0;
    const QJsonObject resp = postAccountSync(
        "admin-request-ownership",
        QJsonObject{{"node", node}, {"target", target}, {"ts", ts},
                    {"sig", m_profileIdentity.signData(canonical)}},
        &status);
    if ((status == 200 || status == 201) && resp.value("ok").toBool()) {
        logSystem("Admin: requested ownership of " + target +
                  " — awaiting that node's approval.");
        QMessageBox::information(
            this, "Request sent",
            QStringLiteral("Ownership request sent. It completes once %1's "
                           "current owner approves it.")
                .arg(target.toHtmlEscaped()));
    } else {
        QMessageBox::warning(
            this, "Request failed",
            QStringLiteral("Could not request ownership: %1")
                .arg(resp.value("error").toString(
                    QStringLiteral("unknown error"))));
    }
}

void MainWindow::pollPendingUsers()
{
    if (!m_isAdmin)
        return;
    const QString node = accountOwner();
    if (node.isEmpty() || !m_profileIdentity.isValid())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-admin-pending-v1\n" + node + "\n" + ts).toUtf8();
    QUrl url = accountsApiUrl("admin-pending");
    QUrlQuery query;
    query.addQueryItem("node", node);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", m_profileIdentity.signData(canonical));
    url.setQuery(query);
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        if (!resp.value("ok").toBool())
            return;
        QStringList current, fresh;
        for (const QJsonValue &v : resp.value("pending").toArray()) {
            const QString name = v.toObject().value("name").toString();
            if (name.isEmpty())
                continue;
            current.append(name);
            if (!m_seenPendingUsers.contains(name))
                fresh.append(name);
        }
        m_seenPendingUsers = current;
        if (fresh.isEmpty())
            return;
        const QString msg =
            QStringLiteral("%1 new user(s) joined and need email verification:\n\n%2")
                .arg(fresh.size())
                .arg(fresh.join(", "));
        if (notifyEnabled(kNewUserAlertSetting) && m_trayIcon &&
            QSystemTrayIcon::supportsMessages())
            m_trayIcon->showMessage("ForkMesh — new user", msg,
                                    QSystemTrayIcon::Information, 8000);
        if (QMessageBox::information(this, "New user joined",
                                     msg + "\n\nReview and verify now?",
                                     QMessageBox::Yes | QMessageBox::No) ==
            QMessageBox::Yes)
            showAdminVerifyDialog();
    });
}

void MainWindow::showAdminVerifyDialog()
{
    const QString node = accountOwner();
    if (node.isEmpty() || !m_profileIdentity.isValid())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-admin-pending-v1\n" + node + "\n" + ts).toUtf8();
    QUrl url = accountsApiUrl("admin-pending");
    QUrlQuery query;
    query.addQueryItem("node", node);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", m_profileIdentity.signData(canonical));
    url.setQuery(query);
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    const QJsonArray pending = resp.value("pending").toArray();

    QDialog dialog(this);
    dialog.setWindowTitle("Verify new users");
    dialog.resize(480, 420);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        "New users awaiting manual email verification (placeholder until an "
        "email service such as Amazon SES is connected):"));
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    auto *rows = new QVBoxLayout(inner);
    if (pending.isEmpty())
        rows->addWidget(new QLabel("<i>No users awaiting verification.</i>"));
    for (const QJsonValue &v : pending) {
        const QJsonObject obj = v.toObject();
        const QString name = obj.value("name").toString();
        const QString email = obj.value("email").toString();
        if (name.isEmpty())
            continue;
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(
            QStringLiteral("<b>%1</b><br><span style='color:#8b949e'>%2</span>")
                .arg(name.toHtmlEscaped(), email.toHtmlEscaped()));
        label->setTextFormat(Qt::RichText);
        row->addWidget(label, 1);
        auto *btn = new QPushButton("Verify email");
        btn->setObjectName("ghostButton");
        btn->setCursor(Qt::PointingHandCursor);
        row->addWidget(btn);
        rows->addLayout(row);
        connect(btn, &QPushButton::clicked, &dialog, [this, name, btn]() {
            btn->setEnabled(false);
            btn->setText(adminVerifyEmail(name) ? "Verified \xE2\x9C\x93" : "Failed");
        });
    }
    rows->addStretch();
    scroll->setWidget(inner);
    layout->addWidget(scroll, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    dialog.exec();
}

bool MainWindow::adminVerifyEmail(const QString &target)
{
    const QString node = accountOwner();
    if (node.isEmpty() || target.isEmpty() || !m_profileIdentity.isValid())
        return false;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-admin-verify-email-v1\n" + node + "\n" + target + "\n" + ts)
            .toUtf8();
    int status = 0;
    const QJsonObject resp = postAccountSync(
        "admin-verify-email",
        QJsonObject{{"node", node}, {"target", target}, {"ts", ts},
                    {"sig", m_profileIdentity.signData(canonical)}},
        &status);
    if (status == 200 && resp.value("ok").toBool()) {
        m_seenPendingUsers.removeAll(target);
        logSystem("Admin: verified email for " + target);
        return true;
    }
    return false;
}

QString MainWindow::accountOwner() const
{
    if (!m_accountName.isEmpty())
        return m_accountName;
    const QString stored = QSettings().value(kAccountNameSetting).toString();
    if (!stored.isEmpty())
        return stored;
    return accountNameFromInput(m_userName, QStringLiteral("owner"));
}

QString MainWindow::settingsAccountName() const
{
    if (!m_accountName.trimmed().isEmpty())
        return m_accountName.trimmed().toLower();
    const QString authed =
        QSettings().value(kAuthedAccountSetting).toString().trimmed().toLower();
    if (!authed.isEmpty())
        return authed;
    return QSettings().value(kAccountNameSetting).toString().trimmed().toLower();
}

bool MainWindow::accountEmailVerified(const QString &accountName) const
{
    const QString normalized = accountName.trimmed().toLower();
    return !normalized.isEmpty() &&
           QSettings().value(emailVerifiedSettingKey(normalized), false).toBool();
}

void MainWindow::applyAccountEmailVerified(const QString &accountName, bool verified)
{
    const QString normalized = accountName.trimmed().toLower();
    if (!normalized.isEmpty() && verified)
        QSettings().setValue(emailVerifiedSettingKey(normalized), true);
    refreshSettingsEmailVerifiedBadge();
}

void MainWindow::refreshSettingsEmailVerifiedBadge()
{
    if (!m_settingsEmailVerifiedBadge)
        return;
    const bool verified = accountEmailVerified(settingsAccountName());
    m_settingsEmailVerifiedBadge->setText(QStringLiteral("Email is verified"));
    m_settingsEmailVerifiedBadge->setToolTip(
        QStringLiteral("This account's email has been verified."));
    m_settingsEmailVerifiedBadge->setVisible(verified);
    if (m_settingsEmailLabel)
        m_settingsEmailLabel->setVisible(verified);
}

bool MainWindow::hasActiveAccountSession() const
{
    return m_accountAuthenticated && m_accountTier == QStringLiteral("active");
}

QString MainWindow::catalogOwner(const RepositoryRecord &repo) const
{
    const QString account = accountOwner();
    return account.isEmpty() ? repoSegment(repo.owner, QStringLiteral("owner"))
                             : account;
}

QUrl MainWindow::accountsApiUrl(const QString &leaf) const
{
    QUrl url = catalogApiUrl(); // same host, http(s) scheme
    url.setPath(QStringLiteral("/api/accounts/") + leaf);
    url.setQuery(QString());
    return url;
}

QJsonObject MainWindow::postAccountSync(const QString &leaf,
                                        const QJsonObject &body, int *status)
{
    QNetworkRequest request(accountsApiUrl(leaf));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (status)
        *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    return QJsonDocument::fromJson(data).object();
}

QJsonObject MainWindow::getAccountSync(const QString &leaf, int *status)
{
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(accountsApiUrl(leaf)));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (status)
        *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    return QJsonDocument::fromJson(data).object();
}

QUrl MainWindow::catalogListUrl()
{
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/repositories"));
    url.setQuery(QString());
    // When this node has a registered account identity, sign a short-lived listing
    // token so the relay also returns our own private repos (hidden from the public
    // catalog). Anonymous callers still receive the public-only list.
    const QString viewer = accountOwner();
    if (m_profileIdentity.isValid() && !viewer.isEmpty()) {
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        const QByteArray canonical =
            ("forkmesh-catalog-view-v1\n" + viewer + "\n" + ts).toUtf8();
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("viewer"), viewer);
        query.addQueryItem(QStringLiteral("ts"), ts);
        query.addQueryItem(QStringLiteral("sig"),
                           m_profileIdentity.signData(canonical));
        url.setQuery(query);
    }
    return url;
}

QJsonArray MainWindow::fetchCatalogRepos()
{
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(catalogListUrl()));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    return obj.value("repositories").toArray();
}

int MainWindow::fetchNodesOnline()
{
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/network/stats"));
    url.setQuery(QString());
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    return obj.value("hosts").toInt();
}

void MainWindow::mirrorCatalogRepo(const QString &owner, const QString &name,
                                   const QString &cloneUrl, bool isPrivate)
{
    if (owner.isEmpty() || name.isEmpty() || cloneUrl.isEmpty())
        return;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &r = m_repositories.at(i);
        if (r.owner != owner || r.name != name)
            continue;
        if (r.previewOnly) {
            mirrorPreviewRepository(i);
            return;
        }
        return; // already mirroring this repo
    }
    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.cloneUrl = cloneUrl;
    repo.publishToNetwork = true;
    // A private repo discovered via the authenticated catalog — one we own, or
    // one shared with us (issue #9) — must keep its private flag so clone/fetch
    // attaches a view token (the owner's, or our grantee share token).
    repo.isPrivate = isPrivate;
    // Mirrored repos (cloned from another node) start with actions off; the user
    // can opt in per repo on the Settings/Actions tab.
    repo.actionsEnabled = false;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(owner, QStringLiteral("owner")) + "-" +
                      repoSegment(name, QStringLiteral("repository")) + ".git";
    m_repositories.append(repo);
    saveRepositories();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));
    refreshRepositoryList();
    logSystem("Mirroring " + owner + "/" + name + " from " + cloneUrl);
    syncRepository(m_repositories.size() - 1);
}

QString MainWindow::hostedCloneUrl(const QString &owner, const QString &name) const
{
    const QString safeOwner = repoSegment(owner, QStringLiteral("owner"));
    const QString safeName = repoSegment(name, QStringLiteral("repository"));
    if (safeOwner.isEmpty() || safeName.isEmpty())
        return QString();
    // Catalog records published by local nodes omit cloneUrl; the repo is still
    // reachable through the mainnode's git smart-HTTP route, which forwards to
    // whichever client is currently hosting it.
    QUrl url = catalogApiUrl(); // http(s) on the mainnode host
    url.setPath(QStringLiteral("/") + safeOwner + QLatin1Char('/') + safeName);
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString();
}

void MainWindow::ensureFlagshipRepo()
{
    if (!m_networkAccess)
        return;
    // Already mirroring the ForkMesh project repo — nothing to bootstrap.
    for (const RepositoryRecord &repo : std::as_const(m_repositories))
        if (!repo.previewOnly &&
            repo.name.compare(QStringLiteral("forkmesh"), Qt::CaseInsensitive) == 0)
            return;

    const QJsonArray repos = fetchCatalogRepos();
    QString owner, cloneUrl;
    bool ownerLive = false;
    for (const QJsonValue &v : repos) {
        const QJsonObject r = v.toObject();
        if (r.value("name").toString().compare(QStringLiteral("forkmesh"),
                                               Qt::CaseInsensitive) != 0)
            continue;
        const QString candidateOwner = r.value("owner").toString();
        if (candidateOwner.isEmpty())
            continue;
        QString candidateUrl = r.value("cloneUrl").toString().trimmed();
        if (candidateUrl.isEmpty())
            candidateUrl = hostedCloneUrl(candidateOwner, QStringLiteral("forkmesh"));
        if (candidateUrl.isEmpty())
            continue;
        const bool live = r.value("liveHost").toBool();
        // Prefer a live host; otherwise keep the first usable entry as a fallback.
        if (live || owner.isEmpty()) {
            owner = candidateOwner;
            cloneUrl = candidateUrl;
            ownerLive = live;
        }
        if (live)
            break;
    }
    if (owner.isEmpty() || cloneUrl.isEmpty())
        return;
    if (!ownerLive)
        logSystem("No live ForkMesh host right now; mirroring " + owner +
                  "/forkmesh anyway so it appears once a host comes online.");
    // On a fresh install, tell the user we're pulling down the project repo and
    // jump them straight into it once the initial clone finishes (adhoc #113),
    // instead of leaving them on an empty repo list wondering what happened.
    if (m_freshInstall) {
        m_pendingAutoOpenRepoKey = owner + "/forkmesh";
        flashMessage(QStringLiteral("Syncing the ForkMesh project repo\xE2\x80\xA6"));
    }
    mirrorCatalogRepo(owner, QStringLiteral("forkmesh"), cloneUrl);
}

bool MainWindow::ensureNodeAccount(const QString &accountName, const QString &solana)
{
#ifdef FORKMESH_WINDOW_TESTS
    if (m_testUseAccountFlowResult) {
        ++m_testEnsureNodeAccountCalls;
        if (m_testAccountFlowResult) {
            m_accountAuthenticated = true;
            m_accountName = accountName;
            m_accountTier = QStringLiteral("active");
            m_accountSolanaVerified = true;
        }
        Q_UNUSED(solana);
        return m_testAccountFlowResult;
    }
#endif
    Q_UNUSED(solana);
    if (m_accountAuthenticated && m_accountName == accountName)
        return true;
    if (!isValidNodeName(accountName)) {
        QMessageBox::warning(this, "Join the network",
                             "Choose a valid username first (lowercase letters, "
                             "numbers and hyphens; start with a letter).");
        return false;
    }

    if (authenticateSilently(accountName))
        return true;

    int status = 0;
    const QJsonObject lookup = getAccountSync(accountName, &status);
    // An active account owned by a different key — fall back to password login
    // (universal cross-device access).
    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == "active")
        return runLoginFlow(accountName);

    // New (or unfinished) account: run the staged join (reserve → donate →
    // set credentials).
    return runSignupFlow(accountName, solana);
}

// Silent (no dialogs, no signup) authentication: the app is authenticated when
// this node already holds the key registered to an active account. The node key —
// not a password — is what the relay verifies for hosting/publishing, so this can
// run on launch without prompting. Returns false (quietly) when it can't confirm.
bool MainWindow::authenticateSilently(const QString &accountName)
{
    if (m_accountAuthenticated && m_accountName == accountName)
        return true;
    if (!isValidNodeName(accountName))
        return false;
    int status = 0;
    const QJsonObject lookup = getAccountSync(accountName, &status);
    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == "active" &&
        lookup.value("pubkey").toString() == m_profileIdentity.publicKey()) {
        m_accountAuthenticated = true;
        m_accountName = accountName;
        m_accountTier = QStringLiteral("active");
        m_accountSolanaVerified = true;
        m_nodeOwnerUser = lookup.value("owner").toString();
        QSettings().setValue(kAuthedAccountSetting, accountName);
        applyAccountEmailVerified(accountName,
                                  lookup.value("emailVerified").toBool());
        return true;
    }
    // The relay says the account is active but bound to another desktop key
    // (e.g. a password login from a second device). That mismatch only blocks
    // signed hosting auth from here — the account and its payout wallet are
    // already verified network members, so reflect that instead of nagging
    // "verify your payout wallet" on every launch.
    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == "active")
        m_accountSolanaVerified = true;
    // Trust a previously authenticated marker whenever the relay gave no
    // authoritative answer — unreachable (status 0), rate-limited or erroring
    // (429/5xx) — so a transient lookup failure doesn't demote a returning user
    // to unverified for the whole session. When the lookup DID answer, do not
    // treat a password-login cache as signed hosting auth unless the server
    // pubkey matched above. Publishing and heartbeat require this desktop's
    // Ed25519 key, not just an email/password session.
    const bool cachedHere =
        QSettings().value(kAuthedAccountSetting).toString() == accountName;
    if (cachedHere && status != 200) {
        m_accountAuthenticated = true;
        m_accountName = accountName;
        m_accountTier = QStringLiteral("active");
        m_accountSolanaVerified = true;
        refreshSettingsEmailVerifiedBadge();
        return true;
    }
    return false;
}

// Non-interactive account registration for a headless mirror node. The desktop
// opens the runSignupFlow dialog and a human clicks "Join ForkMesh"; a headless
// VM has no GUI, so its auto-start path calls this to reserve + finalize its
// node name (binding this VM's Ed25519 key) the same free, no-donation way. Once
// the account is key-bound, the relay accepts this node's catalog writes and
// host-auth tokens, so its mirrors finally register in the database and appear on
// the repository page. Returns true when the node ends up active + key-bound.
bool MainWindow::registerNodeAccountSilently(const QString &accountName)
{
    if (!isValidNodeName(accountName))
        return false;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load())
        return false;
    if (m_accountAuthenticated && m_accountName == accountName)
        return true;

    // Don't try to claim a name that already belongs to another node's key: an
    // active account bound to a different pubkey isn't ours to register. The node
    // keeps mirroring + chatting; it just won't host under a name it can't sign
    // for. (A stale/abandoned reservation on another key is reclaimable — the
    // relay's reserve step decides — so only an ACTIVE mismatch bails here.)
    int lookupStatus = 0;
    const QJsonObject lookup = getAccountSync(accountName, &lookupStatus);
    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == QStringLiteral("active") &&
        lookup.value("pubkey").toString() != m_profileIdentity.publicKey()) {
        logSystem("Account: \"" + accountName + "\" is registered to another "
                  "node; this headless node will keep mirroring without hosting "
                  "under that name.");
        return false;
    }
    // Relay unreachable (status 0): nothing to register against right now. A later
    // auto-sync/startSession retries once it's reachable.
    if (lookupStatus == 0)
        return false;

    // Already an active, key-bound account under this node's own key — e.g. a
    // prior run already reserved + finalized it (an installer link code, adhoc
    // #53, may have attached an owner to it along the way). It's already
    // registered, linked or not; re-running reserve/finalize would only hit the
    // relay's "node_name_taken" guard for active accounts, so just adopt the
    // existing session instead of re-registering.
    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == QStringLiteral("active") &&
        lookup.value("pubkey").toString() == m_profileIdentity.publicKey()) {
        m_accountAuthenticated = true;
        m_accountName = accountName;
        m_accountTier = QStringLiteral("active");
        m_accountSolanaVerified = true;
        m_nodeOwnerUser = lookup.value("owner").toString();
        QSettings().setValue(kAuthedAccountSetting, accountName);
        applyAccountEmailVerified(accountName,
                                  lookup.value("emailVerified").toBool());
        return true;
    }

    // Step 1: reserve the name, binding it to this node's key.
    const QString rts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray rcanon =
        ("forkmesh-reserve-v1\n" + accountName + "\n" + rts).toUtf8();
    int rstatus = 0;
    const QJsonObject rresp = postAccountSync(
        "reserve",
        QJsonObject{{"nodeName", accountName},
                    {"pubkey", m_profileIdentity.publicKey()},
                    {"ts", rts},
                    {"sig", m_profileIdentity.signData(rcanon)}},
        &rstatus);
    if (!rresp.value("ok").toBool()) {
        logSystem("Account: could not reserve \"" + accountName +
                  "\" for headless registration (it may be taken).");
        return false;
    }

    // Step 2: finalize for free with no email/password — a key-bound join. The
    // finalize signature covers an empty email segment, matching the relay's
    // canonical string for a key-bound, credential-less join (see runSignupFlow).
    const QString fts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray fcanon =
        ("forkmesh-finalize-v1\n" + accountName + "\n\n" + fts).toUtf8();
    int fstatus = 0;
    QJsonObject finalizeBody{{"nodeName", accountName},
                             {"email", QString()},
                             {"password", QString()},
                             {"pubkey", m_profileIdentity.publicKey()},
                             {"ts", fts},
                             {"sig", m_profileIdentity.signData(fcanon)}};
    // Installer link code (adhoc #53): install.sh printed a code on this
    // machine and handed it to the daemon it launched. Presenting it at
    // registration lets the relay attach this fresh node to the user whose
    // desktop app drove the install (which offers the same code, key-signed).
    const QString linkCode =
        qEnvironmentVariable("FORKMESH_LINK_CODE").trimmed();
    static const QRegularExpression linkCodeRe(QStringLiteral("^[0-9]{6}$"));
    if (linkCodeRe.match(linkCode).hasMatch())
        finalizeBody.insert(QStringLiteral("linkCode"), linkCode);
    const QJsonObject fresp = postAccountSync("finalize", finalizeBody, &fstatus);
    if (fstatus != 201 || !fresp.value("ok").toBool()) {
        logSystem("Account: could not finalize headless registration for \"" +
                  accountName + "\".");
        return false;
    }

    m_accountAuthenticated = true;
    m_accountName = accountName;
    m_accountTier = QStringLiteral("active");
    m_accountSolanaVerified = true; // registered = active network member
    m_nodeOwnerUser = fresp.value("owner").toString(); // set if a link code linked it
    QSettings().setValue(kAuthedAccountSetting, accountName);
    QSettings().setValue(kAccountNameSetting, accountName);
    applyAccountEmailVerified(accountName, fresp.value("emailVerified").toBool());
    logSystem("Account: registered headless node \"" + accountName +
              "\" (free, key-bound); its mirrors will now publish and host.");
    // Echo the owner the link code attached this node to (adhoc #258) so an SSH
    // installer streaming this log can confirm the attachment landed under the
    // right account rather than the node registering as an orphan.
    if (!m_nodeOwnerUser.isEmpty())
        logSystem("Account: node \"" + accountName + "\" attached to owner \"" +
                  m_nodeOwnerUser + "\".");
    return true;
}

// Backoff retry for a headless node whose first registerNodeAccountSilently()
// call (from startSession(), the only other call site) failed — most commonly
// because the relay wasn't reachable yet moments after the box booted. Nothing
// else in a headless run ever retries this, so without it the node would keep
// mirroring/chatting fine but simply never show up on the website (adhoc #219).
// Capped at a 5-minute steady-state interval and kept idempotent: once
// registerNodeAccountSilently() succeeds (or an active session already exists)
// the timer stops rescheduling itself.
void MainWindow::scheduleHeadlessRegisterRetry(const QString &accountName)
{
    if (!m_headlessRegisterRetryTimer) {
        m_headlessRegisterRetryTimer = new QTimer(this);
        m_headlessRegisterRetryTimer->setSingleShot(true);
        connect(m_headlessRegisterRetryTimer, &QTimer::timeout, this,
                [this, accountName] {
                    if (hasActiveAccountSession())
                        return;
                    if (registerNodeAccountSilently(accountName)) {
                        m_headlessRegisterAttempt = 0;
                        // Bring the live /host tunnels up now. startSession() ran
                        // startRepoHosts() back when this node had no session — a
                        // no-op then (hasActiveAccountSession() was false) — and
                        // nothing else in a headless run retries it. So a node that
                        // only registered on this retry would publish its catalog
                        // record (below) yet never open a host tunnel: it marks no
                        // host_presence and so shows offline ("not ready to serve")
                        // on the website even though it is fully mirroring. Honour a
                        // node the user parked offline, matching startSession's guard.
                        if (!m_nodeOffline)
                            startRepoHosts();
                        for (int i = 0; i < m_repositories.size(); ++i) {
                            const RepositoryRecord &r = m_repositories.at(i);
                            if (!r.previewOnly && r.publishToNetwork &&
                                !r.mirrorPath.isEmpty() &&
                                QDir(r.mirrorPath).exists())
                                publishRepository(i, false);
                        }
                        return;
                    }
                    scheduleHeadlessRegisterRetry(accountName);
                });
    }
    static const int delaysSec[] = {15, 30, 60, 120, 300};
    const int idx = qMin(m_headlessRegisterAttempt,
                         int(sizeof(delaysSec) / sizeof(delaysSec[0])) - 1);
    ++m_headlessRegisterAttempt;
    m_headlessRegisterRetryTimer->start(delaysSec[idx] * 1000);
}

// POST /api/accounts/login — log in by email (+ optional TOTP).
bool MainWindow::verifyTotpLogin(const QString &email,
                                 const QString &password, const QString &totp,
                                 const QString &accountName, bool *fatal)
{
    if (fatal)
        *fatal = false;
    int status = 0;
    const QJsonObject resp = postAccountSync(
        "login",
        QJsonObject{{"email", email}, {"password", password},
                    {"totp", totp},
                    {"pubkey", m_profileIdentity.publicKey()}},
        &status);
    auto acceptLogin = [&](const QJsonObject &payload, bool ownsDesktopKey) {
        m_accountAuthenticated = true;
        m_accountName = payload.value("nodeName").toString(accountName);
        m_accountTier = QStringLiteral("active");
        // True means this local Qt identity is the account's registered desktop
        // key and can publish/host/sign owner-only actions. False is a safe
        // password-only session for viewing/using the app when the account is
        // bound to another desktop key.
        m_accountSolanaVerified = ownsDesktopKey;
        // Remember that this machine successfully authenticated this account so
        // the next launch opens straight onto the app shell. Without this a
        // password (cross-device) login — where the node key does NOT own the
        // account — fails silent auth on every restart and is sent back to the
        // login screen even with correct credentials.
        QSettings().setValue(kAuthedAccountSetting, m_accountName);
        applyAccountEmailVerified(m_accountName,
                                  payload.value("emailVerified").toBool());
    };

    if (status == 200 && resp.value("ok").toBool()) {
        acceptLogin(resp, true);
        return true;
    }
    const QString err = resp.value("error").toString();

    // If the password is correct but this account is already bound to another
    // desktop key, do a second web-style login without pubkey. That matches the
    // web/mobile architecture: a client may use the central Worker with
    // email/password, but only the bound desktop key can publish/host. Do not
    // silently rotate or replace the account key here.
    if (err == "pubkey_mismatch") {
        int webStatus = 0;
        const QJsonObject webResp = postAccountSync(
            "login",
            QJsonObject{{"email", email}, {"password", password}, {"totp", totp}},
            &webStatus);
        if (webStatus == 200 && webResp.value("ok").toBool()) {
            acceptLogin(webResp, false);
            QMessageBox::information(
                this, "Logged in",
                "You are signed in with email/password, but this account is "
                "already bound to a different desktop key. Browsing and account "
                "features will work from this device; publishing, hosting, and "
                "owner-signed actions require the original desktop key or an "
                "explicit account-key rotation/import.");
            return true;
        }
    }

    if (fatal)
        *fatal = false;
    QMessageBox::warning(this, "Log in",
                         err == "bad_totp"
                             ? "Incorrect authenticator code."
                             // The relay returns one generic code for a bad
                             // email/password/unknown account so attackers can't
                             // tell which accounts exist.
                             : err == "invalid_credentials"
                                   ? "Incorrect email or password."
                             : err == "too_many_attempts"
                                   ? "Too many failed attempts. Wait a few minutes "
                                     "and try again."
                             : err == "pubkey_mismatch"
                                   ? "This account is already bound to another "
                                     "desktop key, and password-only fallback also "
                                     "failed. Use the original device, import its "
                                     "identity backup, or rotate the account key."
                                         : "Login failed" +
                                               (err.isEmpty() ? QString() : ": " + err) +
                                               ".");
    return false;
}

bool MainWindow::runLoginFlow(const QString &accountName)
{
    QDialog dialog(this);
    dialog.setWindowTitle("Log in to " + accountName);
    auto *form = new QFormLayout(&dialog);
    form->addRow(new QLabel("Log in with your email and password to join the network."));
    auto *emailEdit = new QLineEdit;
    emailEdit->setPlaceholderText("you@example.com");
    auto *passEdit = new QLineEdit;
    passEdit->setEchoMode(QLineEdit::Password);
    auto *totpEdit = new QLineEdit;
    totpEdit->setPlaceholderText("6-digit code (only if you enabled 2FA)");
    totpEdit->setMaxLength(6);
    form->addRow("Email", emailEdit);
    form->addRow("Password", passEdit);
    form->addRow("2FA code", totpEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Log in");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    while (dialog.exec() == QDialog::Accepted) {
        const QString email = emailEdit->text().trimmed();
        if (!email.contains('@')) {
            QMessageBox::warning(this, "Log in", "Enter a valid email.");
            continue;
        }
        bool fatal = false;
        if (verifyTotpLogin(email, passEdit->text(), totpEdit->text().trimmed(),
                            accountName, &fatal))
            return true;
        // Unrecoverable failure (e.g. this account is bound to another device key):
        // re-prompting can't help, so stop here instead of re-opening the dialog.
        if (fatal)
            break;
    }
    if (m_setupError) {
        m_setupError->setText("Account login is required to join the network.");
        m_setupError->show();
    }
    return false;
}

// In-app join — joining the network is free. The only thing required is a
// username: it's reserved against this device's Ed25519 key and immediately
// activated (no donation, no email/password). The username identifies the user,
// not this machine — a user can attach many nodes to it later. Cross-device
// email/password login can be added later. Returns true once the account is
// active.
bool MainWindow::runSignupFlow(const QString &accountName, const QString &solana)
{
    Q_UNUSED(solana);
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        QMessageBox::warning(this, "Join the network", m_profileIdentity.errorString());
        return false;
    }

    QDialog dialog(this);
    dialog.setObjectName("signupWizard");
    dialog.setWindowTitle("Join ForkMesh");
    dialog.setModal(true);
    dialog.setMinimumWidth(440);

    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(8);

    auto *titleLabel = new QLabel("Choose your username");
    titleLabel->setObjectName("wizardTitle");
    titleLabel->setWordWrap(true);
    outer->addWidget(titleLabel);
    outer->addSpacing(4);

    auto *nameEdit = new QLineEdit;
    nameEdit->setMaxLength(63);
    nameEdit->setPlaceholderText("ada-lovelace");
    nameEdit->setText(accountName);
    auto *hint = new QLabel;
    hint->setObjectName("modeHint");
    hint->setWordWrap(true);
    auto *joinBtn = new QPushButton("Join ForkMesh");
    joinBtn->setObjectName("primaryButton");
    joinBtn->setMinimumHeight(38);
    outer->addWidget(nameEdit);
    outer->addWidget(hint);
    outer->addSpacing(4);
    outer->addWidget(joinBtn);

    bool joined = false;

    auto styleHint = [](QLabel *h, const QString &text, const char *color) {
        h->setText(text);
        h->setStyleSheet(color ? QStringLiteral("color:%1; background:transparent;")
                                     .arg(QString::fromUtf8(color))
                               : QStringLiteral("background:transparent;"));
    };
    styleHint(hint, "Lowercase letters, numbers and hyphens. Start with a letter. "
                    "Your username is public, and joining is free.", nullptr);

    // Debounced availability check so the Join button only lights up for a free
    // name (the reserve below is still the authority, but this avoids a round-trip
    // failure for obviously-taken names).
    auto *availTimer = new QTimer(&dialog);
    availTimer->setSingleShot(true);
    availTimer->setInterval(350);
    connect(nameEdit, &QLineEdit::textChanged, this, [=]() {
        const QString v = nameEdit->text().trimmed().toLower();
        joinBtn->setEnabled(false);
        if (v.isEmpty()) {
            styleHint(hint, "Your username is public, and joining is free.", nullptr);
            return;
        }
        if (!isValidNodeName(v)) {
            styleHint(hint, "Use lowercase letters, numbers and hyphens; start "
                            "with a letter.", "#f85149");
            return;
        }
        styleHint(hint, "Checking availability…", nullptr);
        availTimer->start();
    });
    connect(availTimer, &QTimer::timeout, this, [=]() {
        const QString v = nameEdit->text().trimmed().toLower();
        if (!isValidNodeName(v))
            return;
        int status = 0;
        const QJsonObject look = getAccountSync(v, &status);
        if (status == 0) { // relay unreachable: allow continuing
            styleHint(hint, "Couldn't check availability — you can still continue.",
                      nullptr);
            joinBtn->setEnabled(true);
            return;
        }
        if (look.value("exists").toBool() && !look.value("available").toBool()) {
            styleHint(hint, "That username is already taken — try another.", "#f85149");
            joinBtn->setEnabled(false);
        } else {
            styleHint(hint, QString::fromUtf8("\xE2\x80\x9C%1\xE2\x80\x9D is available.")
                                .arg(v), "#3fb950");
            joinBtn->setEnabled(true);
        }
    });

    // Reserve the name (binding it to this device key), then activate it for free
    // by finalizing with no email/password. Both calls are signed with the local
    // identity; the finalize signature covers an empty email segment, matching the
    // relay's canonical string for a key-bound, credential-less join.
    auto doJoin = [&]() {
        const QString name = nameEdit->text().trimmed().toLower();
        if (!isValidNodeName(name))
            return;
        joinBtn->setEnabled(false);
        joinBtn->setText("Joining…");

        const QString rts = QString::number(QDateTime::currentMSecsSinceEpoch());
        const QByteArray rcanon =
            ("forkmesh-reserve-v1\n" + name + "\n" + rts).toUtf8();
        int rstatus = 0;
        const QJsonObject rresp = postAccountSync(
            "reserve",
            QJsonObject{{"nodeName", name},
                        {"pubkey", m_profileIdentity.publicKey()},
                        {"ts", rts},
                        {"sig", m_profileIdentity.signData(rcanon)}},
            &rstatus);
        if (!rresp.value("ok").toBool()) {
            joinBtn->setText("Join ForkMesh");
            joinBtn->setEnabled(true);
            styleHint(hint, "Could not reserve that username — it may be taken. "
                            "Try another.", "#f85149");
            return;
        }

        const QString fts = QString::number(QDateTime::currentMSecsSinceEpoch());
        const QByteArray fcanon =
            ("forkmesh-finalize-v1\n" + name + "\n\n" + fts).toUtf8();
        int fstatus = 0;
        const QJsonObject fresp = postAccountSync(
            "finalize",
            QJsonObject{{"nodeName", name},
                        {"email", QString()},
                        {"password", QString()},
                        {"pubkey", m_profileIdentity.publicKey()},
                        {"ts", fts},
                        {"sig", m_profileIdentity.signData(fcanon)}},
            &fstatus);
        joinBtn->setText("Join ForkMesh");
        joinBtn->setEnabled(true);
        if (fstatus != 201 || !fresp.value("ok").toBool()) {
            styleHint(hint, "Could not join right now. Please try again.", "#f85149");
            return;
        }
        m_accountAuthenticated = true;
        m_accountName = name;
        m_accountTier = QStringLiteral("active");
        m_accountSolanaVerified = true; // joined = active network member
        QSettings().setValue(kAuthedAccountSetting, name);
        joined = true;
        dialog.accept();
    };
    connect(joinBtn, &QPushButton::clicked, this, doJoin);
    connect(nameEdit, &QLineEdit::returnPressed, this, [&]() {
        if (joinBtn->isEnabled())
            doJoin();
    });

    // A valid name supplied on the previous screen can join straight away, so let
    // the button start enabled instead of forcing the user to retype.
    joinBtn->setEnabled(isValidNodeName(accountName));

    dialog.exec();
    if (joined) {
        // Don't block the launch behind a modal "OK" — return immediately so the
        // caller can open the app, then flash the confirmation as a non-blocking
        // toast on the next event-loop tick.
        QTimer::singleShot(0, this, [this] {
            flashMessage(QString::fromUtf8(
                "You\xE2\x80\x99re in \xE2\x80\x94 your username is registered."));
        });
        return true;
    }
    if (m_setupError) {
        m_setupError->setText("Joining was cancelled. You can keep using ForkMesh "
                              "for free, or join again any time.");
        m_setupError->show();
    }
    return false;
}

void MainWindow::verifyWallet()
{
    // Repurposed: the "join / verify" button now drives the staged join flow,
    // which both registers the account and marks it an active network member.
    const QString name = accountOwner();
    if (name.isEmpty()) {
        QMessageBox::information(this, "Join the network",
                                 "Set your username on the setup screen first.");
        return;
    }
    if (ensureNodeAccount(name, m_solanaEdit ? m_solanaEdit->text().trimmed() : QString())) {
        if (m_profileEligibility)
            m_profileEligibility->setText(
                QString::fromUtf8("<span style='color:#3fb950'>Active "
                               "\xC2\xB7 network member</span>"));
        // Verified now: hide the "verify your wallet" banner.
        updateSolanaNotice();
    }
}

void MainWindow::persistProfile()
{
    const QString name = accountNameFromInput(m_nameEdit->text(), m_userName);
    m_nameEdit->setText(name);
    saveProfileName(name);
    saveSolanaAddress(m_solanaEdit->text().trimmed());
    updateNavSolanaBalance();
}

// --------------------------------------------------------------- quick update

void MainWindow::showUpdateLog()
{
    if (!m_updateLogDialog) {
        m_updateLogDialog = new QDialog(this);
        m_updateLogDialog->setWindowTitle(QStringLiteral("Updating ForkMesh"));
        m_updateLogDialog->resize(780, 480);
        auto *intro = new QLabel(QStringLiteral(
            "Live update log. The app relaunches automatically once the rebuild "
            "finishes; if a step fails this window stays open so you can read "
            "exactly what went wrong."));
        intro->setObjectName("statusLine");
        intro->setWordWrap(true);
        m_updateLog = new QPlainTextEdit;
        m_updateLog->setReadOnly(true);
        m_updateLog->setObjectName("actionLog");
        m_updateLog->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_updateLog->setMaximumBlockCount(50000);
        applyLogFont(m_updateLog);
        new AgentLogHighlighter(m_updateLog->document());
        auto *copyBtn = new QPushButton(QStringLiteral("Copy log"));
        copyBtn->setObjectName("ghostButton");
        copyBtn->setCursor(Qt::PointingHandCursor);
        connect(copyBtn, &QPushButton::clicked, this, [this] {
            if (!m_updateLog)
                return;
            QApplication::clipboard()->setText(m_updateLog->toPlainText());
            flashMessage(QStringLiteral("Update log copied to clipboard."));
        });
        auto *closeBtn = new QPushButton(QStringLiteral("Close"));
        closeBtn->setObjectName("ghostButton");
        closeBtn->setCursor(Qt::PointingHandCursor);
        connect(closeBtn, &QPushButton::clicked, m_updateLogDialog, &QDialog::hide);
        auto *row = new QHBoxLayout;
        row->addStretch();
        row->addWidget(copyBtn);
        row->addWidget(closeBtn);
        auto *lay = new QVBoxLayout(m_updateLogDialog);
        lay->addWidget(intro);
        lay->addWidget(m_updateLog, 1);
        lay->addLayout(row);
    }
    m_updateLog->clear();
    // The full window no longer pops up on its own: progress streams onto the
    // footer one-liner (appendUpdateLog mirrors each line there). The user can
    // click that line to open this window, and a failure re-opens it
    // automatically (see setUpdateStatus) so the error is never missed.
    appendUpdateLog(QStringLiteral("==> ForkMesh update started\n"));
}

void MainWindow::appendUpdateLog(const QString &text)
{
    if (!m_updateLog || text.isEmpty())
        return;
    m_updateLog->moveCursor(QTextCursor::End);
    m_updateLog->insertPlainText(text);
    m_updateLog->moveCursor(QTextCursor::End);
    // Mirror the newest meaningful line onto the footer one-liner. Walk back from
    // the last block so partial chunks (which can end mid-line) still surface a
    // complete, non-empty line of live output.
    for (QTextBlock b = m_updateLog->document()->lastBlock(); b.isValid();
         b = b.previous()) {
        const QString line = b.text().trimmed();
        if (!line.isEmpty()) {
            setFooterUpdateLine(line);
            break;
        }
    }
}

void MainWindow::styleFooterUpdateLog()
{
    if (!m_footerUpdateLog)
        return;
    // Theme-aware so the strip reads on either canvas (it carries its own inline
    // sheet, not the global one).
    const bool dark = currentThemeIsDark();
    const QString colour = dark ? QStringLiteral("#8b949e") : QStringLiteral("#656d76");
    const QString border = dark ? QStringLiteral("#21262d") : QStringLiteral("#d0d7de");
    const QString canvas = dark ? QStringLiteral("#0d1117") : QStringLiteral("#f6f8fa");
    m_footerUpdateLog->setStyleSheet(
        QStringLiteral("QPlainTextEdit#footerUpdateLog{color:%1;border:none;"
                       "border-right:1px solid %2;background:%3;"
                       "font-family:monospace;font-size:11px;padding:3px 12px;}")
            .arg(colour, border, canvas));
}

void MainWindow::setFooterUpdateLine(const QString &line)
{
    if (!m_footerUpdateLog)
        return;
    const QString clean = line.trimmed();
    if (clean.isEmpty())
        return;
    // Only auto-scroll to the new line if the view was already at (or very near)
    // the bottom — otherwise a user who scrolled up to search back through
    // history would get yanked back down by every new event.
    QScrollBar *bar = m_footerUpdateLog->verticalScrollBar();
    const bool wasAtBottom = !bar || bar->value() >= bar->maximum() - 2;
    m_footerUpdateLog->appendPlainText(clean);
    if (wasAtBottom && bar)
        bar->setValue(bar->maximum());
}

void MainWindow::setUpdateStatus(const QString &status, bool isError)
{
    logRestart(isError ? QStringLiteral("ERROR: %1").arg(status) : status);
    // Mirror the phase into the live log as a narrative header ("==>" / "!!"),
    // which AgentLogHighlighter colourises.
    appendUpdateLog((isError ? QStringLiteral("!! ") : QStringLiteral("==> ")) + status +
                    QStringLiteral("\n"));
    // A failure is the one case the footer one-liner isn't enough for: re-open the
    // full window so the user can read exactly what went wrong.
    if (isError && m_updateLogDialog) {
        m_updateLogDialog->show();
        m_updateLogDialog->raise();
        m_updateLogDialog->activateWindow();
    }
    QLabel *label = m_buildStatusLabel ? m_buildStatusLabel : m_updateStatus;
    if (!label)
        return;
    label->setStyleSheet(isError ? "color:#ff6b6b; background:transparent;"
                                  : "color:#9ca3af; background:transparent;");
    label->setText(status);
    label->show();
}

#ifdef FORKMESH_WINDOW_TESTS
QStringList MainWindow::testQuickUpdatePullArguments(const QString &clientDir) const
{
    return quickUpdatePullArguments(clientDir);
}

QStringList MainWindow::testBuildAndPreviewSteps(const QString &gitDir,
                                                 const QString &previewDir,
                                                 const QString &clientDir,
                                                 const QString &buildDir,
                                                 const QString &commit,
                                                 bool haveWorktree) const
{
    QStringList lines;
    for (const PullPreviewStep &step :
         pullPreviewSteps(gitDir, previewDir, clientDir, buildDir, commit,
                          haveWorktree, /*jobs=*/4))
        lines << (QStringList{step.program} + step.args).join(QLatin1Char(' '));
    return lines;
}
#endif

void MainWindow::runUpdateStep(const QString &program, const QStringList &arguments,
                               const QString &workingDir,
                               std::function<void()> onSuccess,
                               std::function<void()> onFailure)
{
    const QString commandLine = (QStringList{program} + arguments).join(QLatin1Char(' '));
    logRestart(QStringLiteral("run: %1").arg(commandLine));
    // Echo the exact command and its working directory into the live log, then
    // stream the process's merged stdout+stderr as it runs.
    appendUpdateLog(QStringLiteral("\n$ %1\n  (in %2)\n").arg(commandLine, workingDir));
    QElapsedTimer stepTimer;
    stepTimer.start();
    auto *process = new QProcess(this);
    process->setWorkingDirectory(workingDir);
    process->setProcessChannelMode(QProcess::MergedChannels);
    // Keep a bounded copy of the output so a failure can surface its tail in the
    // status label even though the full detail is already in the log window.
    auto output = std::make_shared<QString>();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, output] {
        const QString chunk = QString::fromUtf8(process->readAllStandardOutput());
        output->append(chunk);
        if (output->size() > 200000)
            *output = output->right(200000);
        appendUpdateLog(chunk);
    });
    connect(process, &QProcess::finished, this,
            [this, process, stepTimer, commandLine, onSuccess, onFailure, output](
                int exitCode, QProcess::ExitStatus) {
                const QString tail = QString::fromUtf8(process->readAllStandardOutput());
                if (!tail.isEmpty()) {
                    output->append(tail);
                    appendUpdateLog(tail);
                }
                process->deleteLater();
                logRestart(QStringLiteral("done in %1ms (exit %2): %3")
                               .arg(stepTimer.elapsed())
                               .arg(exitCode)
                               .arg(commandLine));
                appendUpdateLog(QString::fromUtf8("\xE2\x80\x94 finished in %1ms (exit %2)\n")
                                    .arg(stepTimer.elapsed())
                                    .arg(exitCode));
                if (exitCode != 0) {
                    if (onFailure) {
                        onFailure();
                        return;
                    }
                    stopRefreshSpin();
                    stopRestartSpin();
                    setUpdateStatus("Update failed: " + output->trimmed().right(300), true);
                    if (m_buildButton)
                        m_buildButton->setEnabled(true);
                    return;
                }
                onSuccess();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, stepTimer, commandLine] {
        stopRefreshSpin();
        stopRestartSpin();
        logRestart(QStringLiteral("failed to start after %1ms: %2")
                       .arg(stepTimer.elapsed())
                       .arg(commandLine));
        setUpdateStatus("Update failed: could not run " + process->program(), true);
        process->deleteLater();
        if (m_buildButton)
            m_buildButton->setEnabled(true);
    });
    process->start(program, arguments);
}

void MainWindow::runQuickUpdate()
{
    beginRestartLog();
    showUpdateLog();
    logRestart(QStringLiteral("quick update started"));
    logSystem(QStringLiteral("=== Quick update started (will rebuild & restart) ==="));
    saveProfileName(m_nameEdit->text());
    m_buildButton = m_updateButton;
    m_buildStatusLabel = m_updateStatus;
    m_updateButton->setEnabled(false);
    const QString clientDir = updateClientDir();

    if (QDir(clientDir).exists("CMakeLists.txt")) {
        setUpdateStatus("Pulling the latest version...");
        runUpdateStep("git", quickUpdatePullArguments(clientDir), clientDir,
                      [this, clientDir] { buildAndRelaunch(clientDir); });
    } else {
        // No checkout anywhere (binary installed without one): clone a fresh
        // copy into the app data directory and update from there from now on.
        const QString repoDir = QFileInfo(clientDir).absolutePath(); // .../src
        QDir().mkpath(QFileInfo(repoDir).absolutePath());
        setUpdateStatus("Downloading the latest version...");
        runUpdateStep("git", {"clone", "--depth", "1", kRepoUrl, repoDir},
                      QFileInfo(repoDir).absolutePath(),
                      [this, clientDir] { buildAndRelaunch(clientDir); });
    }
}

void MainWindow::runUpdateStepUser(const QString &program,
                                   const QStringList &arguments,
                                   const QString &workingDir,
                                   std::function<void()> onSuccess,
                                   std::function<void()> onFailure)
{
    if (m_updateAsUser.isEmpty()) {
        runUpdateStep(program, arguments, workingDir, std::move(onSuccess),
                      std::move(onFailure));
        return;
    }
    // Run the command as the invoking non-root user so files it writes are owned
    // by them and land under their home, never under /root.
    QStringList wrapped{"-u", m_updateAsUser, "-H", program};
    wrapped += arguments;
    runUpdateStep("sudo", wrapped, workingDir, std::move(onSuccess),
                  std::move(onFailure));
}

void MainWindow::buildAndRelaunch(const QString &clientDir, const QString &asUser,
                                  const QString &relaunchPath, const QString &buildType)
{
    m_updateAsUser = asUser;
    const QString appPath = relaunchPath.isEmpty()
                                ? QCoreApplication::applicationFilePath()
                                : relaunchPath;
    const QString buildDir = clientDir + "/build";
    setUpdateStatus("Configuring...");
    runUpdateStepUser("cmake", cmakeConfigureArgs(clientDir, buildDir, buildType),
                      clientDir, [this, buildDir, appPath] {
        setUpdateStatus("Rebuilding...");
        runUpdateStepUser("cmake",
                          {"--build", buildDir, "-j",
                           QString::number(QThread::idealThreadCount())},
                          buildDir, [this, buildDir, appPath] {
            const QString built = builtExecutablePath(buildDir);
            installAndRelaunch(built, appPath);
        });
    });
}

void MainWindow::installAndRelaunch(const QString &built, const QString &appPath)
{
    if (!m_updateAsUser.isEmpty()) {
        // Install and relaunch as the user so the binary is theirs, not root's.
        const QString binDir = QFileInfo(appPath).absolutePath();
        const QString script =
            QStringLiteral("mkdir -p %1 && cp -f %2 %3 && chmod 0755 %3")
                .arg(shellSingleQuote(binDir), shellSingleQuote(built),
                     shellSingleQuote(appPath));
        setUpdateStatus("Installing for " + m_updateAsUser + "...");
        runUpdateStep("sudo", {"-u", m_updateAsUser, "-H", "sh", "-c", script},
                      QDir::tempPath(), [this, appPath] {
            setUpdateStatus("Relaunching...");
            const QString user = m_updateAsUser;
            // Release the instance lock first so the replacement process (which
            // runs as a different user here, but may still share this user's
            // data on a single-user box) doesn't bounce off it before we quit.
            forkmesh::releaseSingleInstance();
            QProcess::startDetached("sudo", {"-u", user, "-H", appPath});
            logRestart(QStringLiteral("relaunched %1; quitting").arg(appPath));
            logSystem(QStringLiteral("=== Restarting now (rebuild & restart) ==="));
            QCoreApplication::quit();
        });
        return;
    }

    // In-process update: replace the running binary over its own path (the
    // running inode stays valid) and relaunch directly.
    if (QFileInfo(built).canonicalFilePath() !=
        QFileInfo(appPath).canonicalFilePath()) {
        QFile::remove(appPath);
        if (!QFile::copy(built, appPath)) {
            setUpdateStatus("Update failed: could not replace " + appPath, true);
            stopRestartSpin();
            if (m_buildButton)
                m_buildButton->setEnabled(true);
            return;
        }
        QFile::setPermissions(appPath,
                              QFile::ReadOwner | QFile::WriteOwner |
                              QFile::ExeOwner | QFile::ReadGroup |
                              QFile::ExeGroup | QFile::ReadOther |
                              QFile::ExeOther);
    }
    setUpdateStatus("Relaunching...");
    // Release the instance lock before spawning the replacement process, or it
    // bounces off the still-held lock (this process hasn't unwound yet) and
    // exits into nothing instead of taking over.
    forkmesh::releaseSingleInstance();
    QProcess::startDetached(appPath, {});
    logRestart(QStringLiteral("relaunched %1; quitting").arg(appPath));
    logSystem(QStringLiteral("=== Restarting now (rebuild & restart) ==="));
    QCoreApplication::quit();
}

QString MainWindow::resolveInstallCloneUrl()
{
    if (!m_networkAccess)
        return QString();
    // Ask the mainnode which node is currently hosting a live forkmesh mirror,
    // then build the hosted git URL the installer would clone from.
    QUrl url = catalogApiUrl(); // http(s) on the mainnode host
    url.setPath(QStringLiteral("/api/install-source"));
    url.setQuery(QString());
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    const QString node = obj.value("node").toString().trimmed();
    const QString repo = obj.value("repo").toString(QStringLiteral("forkmesh")).trimmed();
    if (node.isEmpty() || repo.isEmpty())
        return QString();
    QUrl clone = catalogApiUrl();
    clone.setPath(QStringLiteral("/") + node + QLatin1Char('/') + repo);
    clone.setQuery(QString());
    clone.setFragment(QString());
    return clone.toString();
}

void MainWindow::updateRebuildRestart()
{
    beginRestartLog();
    showUpdateLog();
    logRestart(QStringLiteral("update, rebuild & restart started"));
    logSystem(QStringLiteral("=== Update, rebuild & restart started ==="));
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;

    // Under sudo, target the invoking user's home so nothing is written to /root.
    const QString user = invokingNonRootUser();
    const QString home = user.isEmpty() ? QString() : homeForUser(user);
    const QString clientDir =
        user.isEmpty() ? updateClientDir() : clientDirUnderHome(home);
    const QString relaunchPath =
        user.isEmpty() ? QCoreApplication::applicationFilePath()
                       : (home + QStringLiteral("/.local/bin/forkmesh"));
    // Build steps and the relaunch run as the user when we are root under sudo.
    m_updateAsUser = user;

    if (m_rebuildButton)
        m_rebuildButton->setEnabled(false);
    flashMessage(QStringLiteral("Updating ForkMesh from the install mirror..."));
    setUpdateStatus(user.isEmpty()
                        ? QStringLiteral("Finding an online ForkMesh mirror...")
                        : QStringLiteral("Finding an online ForkMesh mirror "
                                         "(installing for %1)...").arg(user));

    const QString installUrl = resolveInstallCloneUrl();
    if (installUrl.isEmpty()) {
        m_updateAsUser.clear();
        setUpdateStatus("No online ForkMesh mirror is available right now. "
                        "Try again shortly.",
                        true);
        flashMessage("No online ForkMesh mirror is available right now.", true);
        stopRestartSpin();
        if (m_rebuildButton)
            m_rebuildButton->setEnabled(true);
        return;
    }

    const QString repoDir = QFileInfo(clientDir).absolutePath(); // .../src
    const bool haveCheckout = QDir(repoDir).exists(QStringLiteral(".git"));

    // Clean re-clone from the live mirror, used both when there is no existing
    // checkout and as the fallback when a fast-forward pull fails (the local
    // checkout has diverged from the mirror, or the old origin is unreachable).
    auto recloneFromMirror = [this, installUrl, repoDir, clientDir, user,
                              relaunchPath] {
        const QString parent = QFileInfo(repoDir).absolutePath();
        runUpdateStepUser("mkdir", {"-p", parent}, QDir::tempPath(),
                          [this, installUrl, repoDir, parent, clientDir, user,
                           relaunchPath] {
            // Drop the stale checkout so `git clone` can recreate it in place.
            runUpdateStepUser("rm", {"-rf", repoDir}, parent,
                              [this, installUrl, repoDir, parent, clientDir, user,
                               relaunchPath] {
                runUpdateStepUser("git",
                                  {"clone", "--depth", "1", installUrl, repoDir},
                                  parent, [this, clientDir, user, relaunchPath] {
                    buildAndRelaunch(clientDir, user, relaunchPath);
                });
            });
        });
    };

    if (haveCheckout) {
        setUpdateStatus("Pulling a fresh copy from " + installUrl + "...");
        // Repoint origin at the freshly resolved live mirror, then fast-forward.
        runUpdateStepUser("git", {"-C", repoDir, "remote", "set-url", "origin",
                                  installUrl},
                          repoDir, [this, repoDir, clientDir, user, relaunchPath,
                                    installUrl, recloneFromMirror] {
            runUpdateStepUser("git", {"-C", repoDir, "pull", "--ff-only"}, repoDir,
                              [this, clientDir, user, relaunchPath] {
                buildAndRelaunch(clientDir, user, relaunchPath);
            }, [this, installUrl, recloneFromMirror] {
                // A diverged or unreachable mirror cannot be fast-forwarded; fall
                // back to a clean re-clone from the current live mirror.
                setUpdateStatus("Could not fast-forward; re-cloning a fresh copy "
                                "from " + installUrl + "...");
                recloneFromMirror();
            });
        });
    } else {
        setUpdateStatus("Downloading a fresh copy from " + installUrl + "...");
        recloneFromMirror();
    }
}

