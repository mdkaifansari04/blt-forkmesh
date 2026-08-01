






#include "ControlNode.h"
#include "ForkMeshVersion.h"
#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "OfficeChannelMirror.h"
#include "PrivateMirrorStore.h"

#include <QFutureWatcher>
#include <QNetworkInformation>
#include <QtConcurrent/QtConcurrentRun>

using namespace forkmesh::ui;



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

    const QString file = QString::fromLatin1(
        QCryptographicHash::hash(peerId.toUtf8(), QCryptographicHash::Sha256)
            .toHex());
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/avatars/" + file + ".png";
}

void MainWindow::loadCachedAvatars()
{




    const QString dir = QStandardPaths::writableLocation(
                            QStandardPaths::AppDataLocation) +
                        "/avatars";
    if (!QDir(dir).exists())
        return;








    auto *watcher = new QFutureWatcher<QList<QPair<QString, QImage>>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QList<QPair<QString, QImage>> decoded = watcher->result();
        for (const auto &entry : decoded) {

            if (!m_avatars.contains(entry.first))
                m_avatars.insert(entry.first, QPixmap::fromImage(entry.second));
        }


        for (auto it = m_visibleRows.constBegin(); it != m_visibleRows.constEnd();
             ++it) {
            const QPixmap avatar = m_avatars.value(it.value()->senderId());
            if (!avatar.isNull())
                it.value()->setAvatar(avatar);
        }
        refreshChatMembers();
    });
    watcher->setFuture(QtConcurrent::run([dir] {
        const forkmesh::BackgroundScope activity(
            QStringLiteral("avatars"),
            QStringLiteral("Decoding cached avatar image(s)"));
        QList<QPair<QString, QImage>> decoded;

        const QStringList names =
            QDir(dir).entryList({QStringLiteral("*.png")}, QDir::Files);
        for (const QString &name : names) {
            QFile f(QDir(dir).filePath(name));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QByteArray data = f.readAll();

            const int nl = data.indexOf('\n');
            if (nl <= 0)
                continue;
            const QString peerId = QString::fromUtf8(data.left(nl));
            QImage image;
            if (peerId.isEmpty() || !image.loadFromData(data.mid(nl + 1)))
                continue;
            decoded.append({peerId, image});
        }
        return decoded;
    }));
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

        const int first = std::max(0, int(msgs.size()) - 1000);
        for (int i = first; i < msgs.size(); ++i) {
            const ChatMessage &m = msgs.at(i);
            QJsonObject obj{{"id", m.id},
                            {"threadRootId", m.threadRootId},
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
            m.threadRootId = obj.value("threadRootId").toString().left(96);
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


    const QJsonObject dmNames = root.value("dmNames").toObject();
    for (auto it = dmNames.constBegin(); it != dmNames.constEnd(); ++it)
        m_dmNames.insert(it.key(), it.value().toString());
    for (const QJsonValue &v : root.value("openDms").toArray()) {
        const QString peer = v.toString();
        if (!peer.isEmpty() && !m_openDms.contains(peer))
            m_openDms.append(peer);
    }
    refreshDmList();




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


    const QString current = root.value("current").toString();
    if (!current.isEmpty() && current != m_currentConversation &&
        m_history.contains(current)) {
        switchConversation(current);
    } else if (!m_currentConversation.isEmpty()) {





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


    m_nameEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[A-Za-z0-9-]*")), m_nameEdit));
    auto *nameHint = new QLabel(
        "Your username on the mesh \xE2\x80\x94 letters, numbers and hyphens, no spaces.");
    nameHint->setObjectName("modeHint");
    nameHint->setWordWrap(true);




    m_solanaEdit = new QLineEdit(card);
    m_solanaEdit->setMaxLength(64);
    const QString storedSolana = savedSolanaAddress();
    if (!storedSolana.isEmpty() &&
        !forkmesh::control::isValidSolanaPublicAddress(storedSolana)) {



        saveSolanaAddress(QString());
        m_solanaEdit->clear();
    } else {
        m_solanaEdit->setText(storedSolana);
    }
    m_solanaEdit->hide();



    m_pubkeyLabel = new QLabel("Ed25519 public key: generating...");
    m_pubkeyLabel->setObjectName("modeHint");
    m_pubkeyLabel->setWordWrap(true);
    m_pubkeyLabel->hide();






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








    auto *cardRow = new QHBoxLayout;
    cardRow->addStretch();
    cardRow->addWidget(card);
    cardRow->addStretch();
    setupContentLayout->addLayout(cardRow);
    setupContentLayout->addStretch();

    auto *setupScroll = new QScrollArea;
    setupScroll->setWidgetResizable(true);
    setupScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setupScroll->setFrameShape(QFrame::NoFrame);

    setupScroll->setMinimumHeight(0);
    setupScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    setupScroll->setWidget(setupContent);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *chrome = new WindowChromeBar;
    auto *chromeRow = new QHBoxLayout(chrome);
    chromeRow->setContentsMargins(14, 0, 8, 0);
    chromeRow->setSpacing(8);
    auto *chromeTitle = new QLabel(QStringLiteral("ForkMesh v" FORKMESH_VERSION));
    chromeTitle->setObjectName("appVersionLabel");
    chromeRow->addWidget(chromeTitle);
    chromeRow->addStretch();
    auto makeWindowButton = [this](QStyle::StandardPixmap icon, const QString &tip) {
        auto *button = new QPushButton;
        button->setObjectName(QStringLiteral("windowChromeButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(32, 30);
        button->setToolTip(tip);
        button->setIcon(style()->standardIcon(icon));
        button->setIconSize(QSize(14, 14));
        return button;
    };
    auto *minimizeButton = makeWindowButton(QStyle::SP_TitleBarMinButton,
                                            QStringLiteral("Minimize"));
    connect(minimizeButton, &QPushButton::clicked, this, &MainWindow::showMinimized);
    auto *maximizeButton = makeWindowButton(QStyle::SP_TitleBarMaxButton,
                                            QStringLiteral("Maximize / restore"));
    connect(maximizeButton, &QPushButton::clicked, this, [this] {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
    });
    auto *closeButton = makeWindowButton(QStyle::SP_TitleBarCloseButton,
                                         QStringLiteral("Close"));
    closeButton->setObjectName(QStringLiteral("windowChromeCloseButton"));
    connect(closeButton, &QPushButton::clicked, this, &MainWindow::close);
    chromeRow->addWidget(minimizeButton);
    chromeRow->addWidget(maximizeButton);
    chromeRow->addWidget(closeButton);
    layout->addWidget(chrome);
    layout->addWidget(setupScroll);

    connect(startButton, &QPushButton::clicked, this, &MainWindow::startSession);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, &MainWindow::startSession);
    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &name) {


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

bool MainWindow::testSignInButtonVisible() const
{
    return m_navSignInButton && !m_navSignInButton->isHidden();
}

QString MainWindow::testSavedSolanaAddress() const
{
    return QSettings().value(kSolanaSetting).toString().trimmed();
}

bool MainWindow::testColumnsBecomeResizable()
{


    QTableWidget table(0, 4);
    QHeaderView *header = table.horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::Fixed);
    makeColumnsResizable(&table);


    if (header->sectionResizeMode(1) != QHeaderView::ResizeToContents ||
        header->sectionResizeMode(2) != QHeaderView::ResizeToContents)
        return false;

    table.insertRow(0);
    table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("flex")));
    table.setItem(0, 1, new QTableWidgetItem(QStringLiteral("short")));
    table.setItem(0, 2,
                  new QTableWidgetItem(QStringLiteral("a much wider cell value")));
    table.setItem(0, 3, new QTableWidgetItem(QStringLiteral("x")));

    QApplication::processEvents();
    QApplication::processEvents();



    const bool flexDraggable = header->sectionResizeMode(0) == QHeaderView::Interactive;
    const bool fixedUntouched = header->sectionResizeMode(3) == QHeaderView::Fixed;
    const bool col1Draggable = header->sectionResizeMode(1) == QHeaderView::Interactive;
    const bool col2Draggable = header->sectionResizeMode(2) == QHeaderView::Interactive;

    const bool widthsPreserved = header->sectionSize(2) > header->sectionSize(1);
    return flexDraggable && fixedUntouched && col1Draggable && col2Draggable &&
           widthsPreserved;
}

bool MainWindow::testSpreadsheetResize()
{



    QTableWidget table(0, 5);
    QHeaderView *header = table.horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
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

    QApplication::processEvents();
    QApplication::processEvents();


    const bool flexDraggable =
        header->sectionResizeMode(1) == QHeaderView::Interactive;




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

    QApplication::processEvents();
    QApplication::processEvents();


    header->moveSection(header->visualIndex(0), 2);



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

bool MainWindow::testAgentListChromeHidden() const
{
    return m_agentTable && m_agentTable->horizontalHeader()->isHidden() &&
           m_agentTable->frameShape() == QFrame::NoFrame;
}





QString MainWindow::testAgentColumnLayout() const
{
    if (!m_agentTable)
        return QString();
    QHeaderView *header = m_agentTable->horizontalHeader();
    QStringList labels;
    for (int c = 0; c < m_agentTable->columnCount(); ++c)
        labels << m_agentTable->horizontalHeaderItem(c)->text();


    return QStringLiteral("%1|%2/%3")
        .arg(labels.join(QLatin1Char(',')))
        .arg(header->length())
        .arg(m_agentTable->viewport()->width());
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

void MainWindow::testSetQuickAddAgentProvider(const QString &provider)
{
    if (!m_quickAddAgentProvider)
        return;
    const int index = m_quickAddAgentProvider->findData(provider);
    m_quickAddAgentProvider->setCurrentIndex(index >= 0 ? index : 0);
}

QStringList MainWindow::testQuickAddModelLabels() const
{
    QStringList labels;
    if (!m_quickAddClaudeModel)
        return labels;
    for (int i = 0; i < m_quickAddClaudeModel->count(); ++i)
        labels << m_quickAddClaudeModel->itemText(i);
    return labels;
}

bool MainWindow::testQuickAddModelVisible() const
{
    return m_quickAddClaudeModel && m_quickAddClaudeModel->isVisible();
}

bool MainWindow::testQuickAddModelEditable() const
{
    return m_quickAddClaudeModel && m_quickAddClaudeModel->isEditable();
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

QStringList MainWindow::testWorktreeBranches() const
{
    QStringList branches;
    if (!m_worktreesTable)
        return branches;
    for (int row = 0; row < m_worktreesTable->rowCount(); ++row) {
        if (QTableWidgetItem *item = m_worktreesTable->item(row, 0))
            branches << item->data(Qt::UserRole).toString();
    }
    return branches;
}

QString MainWindow::testWorktreeBranchLabel() const
{
    return m_worktreeBranchLabel ? m_worktreeBranchLabel->text() : QString();
}

int MainWindow::testOverviewBodyPage() const
{
    return m_overviewBodyStack ? m_overviewBodyStack->currentIndex() : -1;
}

QStringList MainWindow::testSourceControlPaths() const
{
    QStringList paths;
    if (!m_scmTree)
        return paths;
    for (int g = 0; g < m_scmTree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_scmTree->topLevelItem(g);
        for (int i = 0; group && i < group->childCount(); ++i) {
            const QString path =
                group->child(i)->data(0, Qt::UserRole).toString();
            if (!path.isEmpty())
                paths.append(path);
        }
    }
    return paths;
}

bool MainWindow::testClickSourceControlPath(const QString &path)
{
    if (!m_scmTree)
        return false;
    m_lastSourceControlDiffPath.clear();
    for (int g = 0; g < m_scmTree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_scmTree->topLevelItem(g);
        for (int i = 0; group && i < group->childCount(); ++i) {
            QTreeWidgetItem *item = group->child(i);
            if (item->data(0, Qt::UserRole).toString() != path)
                continue;
            // Clear first so currentItemChanged fires even when the requested
            // row was already selected by scroll-following logic.
            m_scmTree->setCurrentItem(nullptr);
            m_scmTree->setCurrentItem(item);
            return m_lastSourceControlDiffPath == path;
        }
    }
    return false;
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
        // Commit history has one entry point: drive the Git activity-rail
        // destination exactly as a real click does.
        if (m_railGitButton)
            m_railGitButton->click();
        return;
    }
    if (id == m_worktreesTabIndex) {


        showOverviewWorktrees();
        loadWorktreesPanel();
        focusRepoDetailTable(id);
        return;
    }
    if (id == m_branchesTabIndex) {
        // Branches also lives inside Code overview rather than in the top tab
        // row; drive its toolbar destination directly.
        showOverviewBranches();
        loadBranchesPanel();
        focusRepoDetailTable(id);
        return;
    }
    if (!m_repoDetailTabs)
        return;
    if (QAbstractButton *b = m_repoDetailTabs->button(id))
        b->click();
}

bool MainWindow::testOpenMostRecentCommit()
{
    if (!m_commitsTable)
        return false;
    if (m_commitsTable->rowCount() == 0)
        loadCommits();
    if (m_commitsTable->rowCount() == 0)
        return false;
    openMostRecentCommit();
    return true;
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
        QTableWidgetItem *name =
            m_branchesTable->item(row, kBranchesNameColumn);
        if (name && name->text() == branch) {
            if (QTableWidgetItem *wt =
                    m_branchesTable->item(row, kBranchesWorktreeColumn))
                return wt->text();
        }
    }
    return QString();
}

bool MainWindow::testBranchAttachmentHasIcon(const QString &branch) const
{
    if (!m_branchesTable)
        return false;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        QTableWidgetItem *name =
            m_branchesTable->item(row, kBranchesNameColumn);
        if (name && name->text() == branch)
            return !name->icon().isNull();
    }
    return false;
}

QStringList MainWindow::testBranchRowOrder() const
{
    QStringList names;
    if (!m_branchesTable)
        return names;
    for (int row = 0; row < m_branchesTable->rowCount(); ++row) {
        if (QTableWidgetItem *name =
                m_branchesTable->item(row, kBranchesNameColumn))
            names << name->text();
    }
    return names;
}

void MainWindow::testSetDefaultAgentProvider(const QString &provider)
{
    if (!m_defaultAgentProviderCombo)
        return;


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




    m_selectedNode = m_repositories.at(index).owner;
    openRepoDetail(index);
    return true;
}

QString MainWindow::testRepoDefaultBranch() const
{
    return repoDefaultBranch(repoBranches());
}

QString MainWindow::testRepoActionsTabText() const
{
    // The count rides the icon as a corner badge now (adhoc #6); recompose the
    // old "Actions (N)" text so callers keep asserting caption and count as one.
    if (!m_repoActionsTab)
        return QString();
    const auto *badged = dynamic_cast<VerticalIconButton *>(m_repoActionsTab);
    return QStringLiteral("%1 (%2)")
        .arg(m_repoActionsTab->text(),
             formatCount(badged ? badged->badgeCount() : 0));
}

QString MainWindow::testRepoBranchesButtonText() const
{
    // Same recomposition as testRepoActionsTabText: the visible "N" is a
    // corner badge on a fixed "Branches" caption since adhoc #6.
    if (!m_branchesButton)
        return QString();
    const auto *badged = dynamic_cast<VerticalIconButton *>(m_branchesButton);
    const qint64 count = badged ? badged->badgeCount() : 0;
    return QStringLiteral("%1 %2")
        .arg(formatCount(count),
             count == 1 ? QStringLiteral("branch") : QStringLiteral("branches"));
}

bool MainWindow::testShowRepoIssuesTab()
{
    if (!m_repoDetailStack || m_repoDetailStack->count() <= 2 ||
        !m_repoDetailTabs)
        return false;
    QAbstractButton *button = m_repoDetailTabs->button(2);
    if (!button)
        return false;




    button->click();
    return m_repoDetailStack->currentIndex() == 2;
}

int MainWindow::testRepoTabContentTop()
{
    return m_repoDetailStack ? m_repoDetailStack->mapTo(this, QPoint(0, 0)).y() : -1;
}

int MainWindow::testRepoTabGapAroundIssues() const
{
    if (!m_repoCodeTab || !m_repoIssuesTab || !m_repoPullsTab)
        return -1;
    const QRect codeRect(m_repoCodeTab->mapTo(const_cast<MainWindow *>(this),
                                              QPoint(0, 0)),
                         m_repoCodeTab->size());
    const QRect issuesRect(m_repoIssuesTab->mapTo(const_cast<MainWindow *>(this),
                                                  QPoint(0, 0)),
                           m_repoIssuesTab->size());
    const QRect pullsRect(m_repoPullsTab->mapTo(const_cast<MainWindow *>(this),
                                                QPoint(0, 0)),
                          m_repoPullsTab->size());
    return qMin(issuesRect.left() - codeRect.right() - 1,
                pullsRect.left() - issuesRect.right() - 1);
}




int MainWindow::testIssueLooperGapFromNewIssueButton() const
{
    if (!m_looperToggle || !m_issueListNewButton ||
        !m_looperToggle->isVisible())
        return -100000;
    const QRect looperRect(m_looperToggle->mapTo(const_cast<MainWindow *>(this),
                                                 QPoint(0, 0)),
                           m_looperToggle->size());
    const QRect newIssueRect(
        m_issueListNewButton->mapTo(const_cast<MainWindow *>(this), QPoint(0, 0)),
        m_issueListNewButton->size());
    return looperRect.left() - newIssueRect.right() - 1;
}

bool MainWindow::testIssueLooperRowAligned() const
{
    if (!m_looperToggle || !m_issueListNewButton ||
        !m_looperToggle->isVisible())
        return false;
    const QRect looperRect(m_looperToggle->mapTo(const_cast<MainWindow *>(this),
                                                 QPoint(0, 0)),
                           m_looperToggle->size());
    const QRect newIssueRect(
        m_issueListNewButton->mapTo(const_cast<MainWindow *>(this), QPoint(0, 0)),
        m_issueListNewButton->size());
    return qAbs(looperRect.center().y() - newIssueRect.center().y()) <= 8;
}

int MainWindow::testTopNavTrailingGap() const
{




    if (QWidget *rail =
            findChild<QWidget *>(QStringLiteral("appNavigationRail"))) {
        const QRect rect(rail->mapTo(const_cast<MainWindow *>(this), QPoint(0, 0)),
                         rail->size());
        return rect.left();
    }
    int rightEdge = -1;
    for (QWidget *widget :
         {static_cast<QWidget *>(m_navScreenshotButton),
          static_cast<QWidget *>(m_navResizeButton),
          static_cast<QWidget *>(m_navRebuildButton)}) {
        if (!widget || !widget->isVisibleTo(const_cast<MainWindow *>(this)))
            continue;
        const QRect rect(widget->mapTo(const_cast<MainWindow *>(this), QPoint(0, 0)),
                         widget->size());
        rightEdge = qMax(rightEdge, rect.right());
    }
    return rightEdge >= 0 ? width() - rightEdge - 1 : -1;
}

void MainWindow::testSetDirectoryUserNodes(const QString &user,
                                           const QStringList &nodes)
{
    MemberInfo member;
    member.id = QStringLiteral("user:") + user.toLower();
    member.name = user;
    member.ownerUser = user;
    member.accountKind = QStringLiteral("user");
    member.nodeName = nodes.join(QStringLiteral(", "));
    m_chatDirectoryUsers.insert(user.toLower(), member);
    m_chatDirectoryLoaded = true;
}

void MainWindow::testShowNodesSection()
{
    showSection(kNodesSectionIndex);
    refreshNodesTable();
}

QStringList MainWindow::testNodeDirectoryNames() const
{
    QStringList names;
    if (!m_nodesTable)
        return names;
    for (int row = 0; row < m_nodesTable->rowCount(); ++row) {
        if (QTableWidgetItem *item = m_nodesTable->item(row, 0))
            names.append(item->data(Qt::UserRole).toString());
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList MainWindow::testChatMemberNames(const QString &conversation)
{
    const QString saved = m_currentConversation;
    m_currentConversation = conversation;
    refreshChatMembers();
    QStringList names;
    if (m_chatMembersLayout) {
        static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
        for (int i = 0; i < m_chatMembersLayout->count(); ++i) {
            QWidget *card = m_chatMembersLayout->itemAt(i)->widget();
            if (!card)
                continue;

            for (QLabel *label : card->findChildren<QLabel *>()) {
                if (!label->text().contains(QString::fromUtf8("\xE2\x97\x8F")))
                    continue;
                names.append(label->text()
                                 .remove(tag)
                                 .remove(QString::fromUtf8("\xE2\x97\x8F"))
                                 .remove(QStringLiteral("(you)"))
                                 .trimmed());
                break;
            }
        }
    }
    m_currentConversation = saved;
    refreshChatMembers();
    names.sort(Qt::CaseInsensitive);
    return names;
}

void MainWindow::testRenderNetworkRepos(const QJsonArray &repos)
{
    showSection(kNetworkReposSectionIndex);


    ++m_networkReposLoadGen;
    renderNetworkRepos(repos);
}

QStringList MainWindow::testNetworkRepoNames() const
{
    QStringList names;
    if (!m_networkReposTable)
        return names;
    for (int row = 0; row < m_networkReposTable->rowCount(); ++row) {
        if (QTableWidgetItem *item = m_networkReposTable->item(row, 0))
            names.append(item->data(Qt::UserRole).toString());
    }
    return names;
}

QString MainWindow::testNetworkRepoActionText(int row) const
{
    if (!m_networkReposTable || row < 0 ||
        row >= m_networkReposTable->rowCount())
        return QString();
    QWidget *cell = m_networkReposTable->cellWidget(row, 3);
    if (!cell)
        return QString();
    const QList<QPushButton *> buttons =
        cell->findChildren<QPushButton *>(QString(), Qt::FindDirectChildrenOnly);
    return buttons.isEmpty() ? QString() : buttons.first()->text();
}

QString MainWindow::testNetworkRepoMirrorHeader() const
{
    if (!m_networkReposTable ||
        !m_networkReposTable->horizontalHeaderItem(2))
        return QString();
    return m_networkReposTable->horizontalHeaderItem(2)->text();
}

QStringList MainWindow::testNetworkRepoColumns() const
{
    QStringList labels;
    if (!m_networkReposTable)
        return labels;
    for (int column = 0; column < m_networkReposTable->columnCount(); ++column) {
        if (QTableWidgetItem *item =
                m_networkReposTable->horizontalHeaderItem(column))
            labels.append(item->text());
    }
    return labels;
}

QString MainWindow::testNetworkRepoCellText(int row,
                                            const QString &header) const
{
    if (!m_networkReposTable || row < 0 ||
        row >= m_networkReposTable->rowCount())
        return QString();
    for (int column = 0; column < m_networkReposTable->columnCount(); ++column) {
        QTableWidgetItem *label =
            m_networkReposTable->horizontalHeaderItem(column);
        if (!label || label->text() != header)
            continue;
        QTableWidgetItem *item = m_networkReposTable->item(row, column);
        return item ? item->text() : QString();
    }
    return QString();
}

int MainWindow::testReposNavBadgeCount() const
{
    auto *railButton = dynamic_cast<ActivityRailButton *>(m_reposNavButton);
    return railButton ? railButton->badgeCount() : -1;
}
#endif

void MainWindow::setHeadlessMode(bool headless)
{
    m_headless = headless;











    if (headless && m_nodeOffline) {
        m_nodeOffline = false;
        QSettings().setValue(kNodeOfflineSetting, false);
    }
}

void MainWindow::startSession()
{




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
    const QString name = raw;
    m_nameEdit->setText(name);
    saveProfileName(name);
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        m_setupError->setText(m_profileIdentity.errorString());
        m_setupError->show();
        return;
    }
    if (m_accountAuthenticated &&
        AccountCapability::normalizedAccount(m_accountName) != name) {
        setDesktopCapability(m_accountName, false);
        m_accountAuthenticated = false;
        m_accountTier = QStringLiteral("free");
        m_accountSolanaVerified = false;
        m_isAdmin = false;
        QSettings().remove(kAuthedAccountSetting);
    }







#ifdef FORKMESH_WINDOW_TESTS


    if (!m_testBypassServerStart)
        authenticateSilently(name);
#else
    authenticateSilently(name);
#endif












    const bool publishesMirror = [this] {
        for (const RepositoryRecord &r : std::as_const(m_repositories)) {
            if (!r.previewOnly && r.publishToNetwork && !r.mirrorPath.isEmpty() &&
                QDir(r.mirrorPath).exists())
                return true;
        }
        return false;
    }();
    const QString installerLinkCode =
        qEnvironmentVariable("FORKMESH_LINK_CODE").trimmed();
    static const QRegularExpression installerLinkCodeRe(
        QStringLiteral("^[0-9]{6}$"));
    const bool installerLinkPending =
        m_headless &&
        installerLinkCodeRe.match(installerLinkCode).hasMatch() &&
        m_nodeOwnerUser.trimmed().isEmpty();
    const bool shouldHost =
        (m_headless || publishesMirror) && isValidNodeName(name);
    bool hostingReady = hasOwnerSigningCapability(name);
    if (shouldHost && (!hostingReady || installerLinkPending)) {
#ifdef FORKMESH_WINDOW_TESTS
        if (!m_testBypassServerStart)
            hostingReady = registerNodeAccountSilently(name);
#else
        hostingReady = registerNodeAccountSilently(name);
#endif
    }




    if (shouldHost && hostingReady) {
        if (m_headless)
            ensureFlagshipRepo();
        for (int i = 0; i < m_repositories.size(); ++i) {
            const RepositoryRecord &r = m_repositories.at(i);
            if (!r.previewOnly && r.publishToNetwork &&
                !r.mirrorPath.isEmpty() && QDir(r.mirrorPath).exists())
                publishRepository(i, false);
        }
    } else if (shouldHost) {




        scheduleHeadlessRegisterRetry(name);
    }

    if (m_serverUrlEdit->text().trimmed().isEmpty())
        m_serverUrlEdit->setText(serverHostDisplay(kDefaultServerUrl));

    QString payoutAddress = m_solanaEdit->text().trimmed();
    if (!payoutAddress.isEmpty() &&
        !forkmesh::control::isValidSolanaPublicAddress(payoutAddress)) {
        payoutAddress.clear();
        m_solanaEdit->clear();
    }
    saveSolanaAddress(payoutAddress);
    if (m_accountAuthenticated &&
        AccountCapability::normalizedAccount(m_accountName) !=
            AccountCapability::normalizedAccount(name)) {
        setDesktopCapability(m_accountName, false);
        m_accountAuthenticated = false;
        m_accountTier = QStringLiteral("free");
        m_accountSolanaVerified = false;
        m_isAdmin = false;
        QSettings().remove(kAuthedAccountSetting);
    }
    m_accountName = name;
    QSettings().setValue(kAccountNameSetting, name);
    refreshSettingsEmailVerifiedBadge();

    m_roomNameEdit->setText(kDefaultRoomName);
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



    if (m_settingsNameEdit)
        m_settingsNameEdit->setText(m_userName);
    if (m_settingsMachineNodeEdit)
        m_settingsMachineNodeEdit->setText(machineNodeName());
    setSettingsAvatar(m_userAvatar);
    updateUserSwitcher();
    updateAvatarButton();


    m_channels.clear();
    m_privateChannels.clear();
    m_welcomeAnnounced = false;
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
    auto *server = new ServerNode(chatDisplayName(), machineNodeName(),
                                  nodeOwnerDisplayName(),
                                  m_profileIdentity.publicKey(), url,
                                  kDefaultRoomName,
                                  payoutAddress,
                                  m_roomPassphrase, this);
    server->setConnectionAuthorizer([this](const QUrl &endpoint) {
        return authorizeFirewallConnection(QStringLiteral("WebSocket"), endpoint);
    });
    if (const auto *networkInfo = QNetworkInformation::instance();
        networkInfo &&
        networkInfo->reachability() ==
            QNetworkInformation::Reachability::Disconnected)
        server->setNetworkAvailable(false);




    QList<QUrl> endpoints{url};
    for (const ServerConfig &cfg : std::as_const(m_servers)) {
        const QUrl endpoint(canonicalServerUrl(cfg.url));
        if (endpoint.isValid() && !endpoints.contains(endpoint))
            endpoints.append(endpoint);
    }
    server->setEndpoints(endpoints);
    attachBackend(server);




    server->setAccountKind(
        (m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty())
            ? QStringLiteral("user")
            : chatIdentityIsGuest() ? QStringLiteral("guest")
                                    : QStringLiteral("node"));
    if (!server->start())
        return;

    if (m_backend) {
        if (m_connectedAtMs <= 0)
            m_connectedAtMs = QDateTime::currentMSecsSinceEpoch();


        m_nodeAlertGraceUntilMs =
            QDateTime::currentMSecsSinceEpoch() + 12000;


        updateChatIdentity();


        m_backend->addChannel(QStringLiteral("general"));
        m_backend->addChannel(kWelcomeChannel);
        m_backend->addChannel(QStringLiteral("random"));


        restorePrivateChannels();
        logSystem("Encryption: client-side AES-256-GCM mainnode room encryption.");
        const QJsonObject signedProfile =
            m_profileIdentity.signedProfile(m_userName,
                                            m_userName,
                                            payoutAddress);
        const QString profileBytes = QString::fromUtf8(
            QJsonDocument(signedProfile).toJson(QJsonDocument::Compact));
        logSystem("Identity: signed profile for " +
                  m_profileIdentity.shortPublicKey() + " (" +
                  QString::number(profileBytes.toUtf8().size()) + " bytes).");
        m_stack->setCurrentIndex(1);
        showSection(0);
        updateBreadcrumb();
        updateSolanaNotice();


        loadCachedAvatars();
        loadChatHistory();



        if (m_nodeOffline) {

            if (m_connectedAtMs > 0) {
                m_totalConnectionMs +=
                    QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs;
                m_connectedAtMs = 0;
                QSettings().setValue(kConnectionTotalSetting, m_totalConnectionMs);
            }
        } else if (hasOwnerSigningCapability(name)) {

            startRepoHosts();


            if (!m_heartbeatTimer) {
                m_heartbeatTimer = new QTimer(this);
                m_heartbeatTimer->setInterval(60000);
                connect(m_heartbeatTimer, &QTimer::timeout, this,
                        &MainWindow::sendNodeHeartbeat);
            }
            m_heartbeatTimer->start();
            sendNodeHeartbeat();
            if (m_headless)
                QTimer::singleShot(0, this, &MainWindow::ensureFlagshipRepo);
        }
        updateNodeOnlineControls();
    }
}



QJsonObject MainWindow::emailNotificationPreferencesPayload() const
{
    const QSettings settings;
    auto enabled = [&settings](const QString &key, bool defaultOn) {
        return settings.value(key, defaultOn).toBool();
    };
    QJsonObject prefs;
    prefs.insert(QStringLiteral("mention"),
                 enabled(kEmailNotifyMentionSetting, true));
    prefs.insert(QStringLiteral("subscribed"),
                 enabled(kEmailNotifySubscribedSetting, true));
    prefs.insert(QStringLiteral("pull_submitted"),
                 enabled(kEmailNotifyPullSubmittedSetting, true));
    prefs.insert(QStringLiteral("issue_assigned"),
                 enabled(kEmailNotifyIssueAssignedSetting, true));
    prefs.insert(QStringLiteral("repo_shared"),
                 enabled(kEmailNotifyRepoSharedSetting, true));
    prefs.insert(QStringLiteral("bounty_funded"),
                 enabled(kEmailNotifyBountyFundedSetting, true));
    prefs.insert(QStringLiteral("bounty_paid"),
                 enabled(kEmailNotifyBountyPaidSetting, true));
    prefs.insert(QStringLiteral("release_published"),
                 enabled(kEmailNotifyReleasePublishedSetting, true));
    prefs.insert(QStringLiteral("pending_inbox"),
                 enabled(kEmailNotifyPendingInboxSetting, true));
    prefs.insert(QStringLiteral("credits_refilled"),
                 enabled(kEmailNotifyCreditsRefilledSetting, true));
    prefs.insert(QStringLiteral("general_chat"),
                 enabled(kEmailNotifyGeneralChatSetting, true));
    prefs.insert(QStringLiteral("host_online"),
                 enabled(kEmailNotifyHostOnlineSetting, false));
    prefs.insert(QStringLiteral("host_offline"),
                 enabled(kEmailNotifyHostOfflineSetting, false));
    return prefs;
}

void MainWindow::sendNodeHeartbeat()
{
    const QString name = m_accountName.isEmpty()
                             ? QSettings().value(kAccountNameSetting).toString().trimmed()
                             : m_accountName;
    if (name.isEmpty() || !hasOwnerSigningCapability(name) ||
        !m_profileIdentity.isValid())
        return;


    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_pollBackoff.ready(QStringLiteral("heartbeat"), nowMs))
        return;
    const QString ts = QString::number(nowMs);
    const QByteArray canonical =
        ("forkmesh-heartbeat-v1\n" + name + "\n" + ts).toUtf8();
    const QString solana = savedSolanaAddress();



    const QString creditsRefilled = m_pendingCreditsRefilled5h
        ? QStringLiteral("5h")
        : (m_pendingCreditsRefilledWeekly ? QStringLiteral("weekly") : QString());
    QJsonObject body{{"nodeName", name}, {"solana", solana}, {"ts", ts},
                     {"sig", m_profileIdentity.signData(canonical)}};
    body.insert(QStringLiteral("notificationPreferences"),
                emailNotificationPreferencesPayload());
    if (!creditsRefilled.isEmpty())
        body.insert(QStringLiteral("creditsRefilled"), creditsRefilled);



    if (!m_pendingMirrorRequestAcks.isEmpty()) {
        QJsonArray acks;
        for (const QString &id : m_pendingMirrorRequestAcks)
            acks.append(id);
        body.insert(QStringLiteral("mirrorRequestsAck"), acks);



        m_pendingMirrorRequestAcks.clear();
    }
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


        const bool wasAdmin = m_isAdmin;
        m_isAdmin = resp.value("isAdmin").toBool();


        if (m_isAdmin != wasAdmin)
            updateAdminCrownBadge();



        setPendingRelayJoins(
            m_isAdmin ? resp.value(QStringLiteral("pendingRelays")).toInt()
                      : 0);


        fetchRoomPassphrase();


        startOfficeChannelMirror();



        refreshWebAlerts();
        if (m_isAdmin) {
            if (!m_adminPollTimer) {
                m_adminPollTimer = new QTimer(this);




                m_adminPollTimer->setInterval(300000);
                connect(m_adminPollTimer, &QTimer::timeout, this,
                        &MainWindow::pollPendingUsers);
            }
            if (!m_adminPollTimer->isActive()) {
                m_adminPollTimer->start();
                pollPendingUsers();
            }
        }









        if (resp.value(QStringLiteral("balanceIncreased")).toBool()) {
            updateNavSolanaBalance();
            refreshNavSolanaBalance(true);
        }




        const QJsonObject claim = resp.value(QStringLiteral("claim")).toObject();
        const QString claimCode = claim.value(QStringLiteral("code")).toString();
        if (!claimCode.isEmpty() && claimCode != m_lastClaimCodeShown) {
            m_lastClaimCodeShown = claimCode;
            showNodeClaimCode(claim.value(QStringLiteral("user")).toString(),
                              claimCode);
        }



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




        const QJsonArray mirrorRequests =
            resp.value(QStringLiteral("mirrorRequests")).toArray();
        for (const QJsonValue &value : mirrorRequests) {
            const QJsonObject req = value.toObject();
            const QString id = req.value(QStringLiteral("id")).toString().trimmed();
            const QString owner = req.value(QStringLiteral("owner")).toString().trimmed();
            const QString repo = req.value(QStringLiteral("repo")).toString().trimmed();
            if (id.isEmpty() || owner.isEmpty() || repo.isEmpty())
                continue;
            if (!m_pendingMirrorRequestAcks.contains(id))
                m_pendingMirrorRequestAcks.append(id);
            if (m_handledMirrorRequests.contains(id))
                continue;
            m_handledMirrorRequests.insert(id);
            logSystem("Accepted mirror request: mirroring " + owner + "/" + repo +
                      " for a peer.");
            mirrorNetworkRepo(owner, repo, QString(), false);
        }
    });
}

void MainWindow::showNodeClaimCode(const QString &user, const QString &code)
{
    const QString who = user.isEmpty() ? QStringLiteral("A user") : user;


    logSystem("Account: user \"" + who + "\" is claiming this node on the "
              "website. Confirmation code: " + code + " — enter it there to "
              "link this node to that account (expires in 10 minutes).");
    if (m_headless)
        return;
    auto *box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(QStringLiteral("Link this machine?"));
    box->setIcon(QMessageBox::Information);
    box->setText(
        QStringLiteral("<b>%1</b> is claiming this machine's node on "
                       "forkmesh.com.<br><br>"
                       "Confirmation code:"
                       "<div style='font-size:28px;letter-spacing:6px'><b>%2</b></div>"
                       "Enter this code on the website to link this machine to "
                       "that account. If this isn't you, just close this window — "
                       "the code expires in 10 minutes and is never sent anywhere "
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
    if (node.isEmpty() || !hasOwnerSigningCapability(node) ||
        !m_profileIdentity.isValid())
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
    if (target.isEmpty() || node.isEmpty() ||
        !hasOwnerSigningCapability(node) || !m_profileIdentity.isValid())
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
    if (node.isEmpty() || !hasOwnerSigningCapability(node) ||
        !m_profileIdentity.isValid())
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

void MainWindow::fetchRoomPassphrase()
{



    if (!m_roomPassphrase.isEmpty())
        return;
    const QString node = accountOwner();
    if (node.isEmpty() || !hasOwnerSigningCapability(node) ||
        !m_profileIdentity.isValid())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString roomOwner = QStringLiteral("mainnode");
    const QString roomRepo = QStringLiteral("forkmesh");
    const QByteArray canonical =
        ("forkmesh-room-key-v2\n" + node + "\n" + roomOwner + "\n" +
         roomRepo + "\n" + ts).toUtf8();
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/chat/room-key"));
    QUrlQuery query;
    query.addQueryItem("owner", roomOwner);
    query.addQueryItem("repo", roomRepo);
    query.addQueryItem("room", kDefaultRoomName);
    query.addQueryItem("node", node);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", m_profileIdentity.signData(canonical));
    url.setQuery(query);
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        const QString pass = resp.value("passphrase").toString();
        const bool matchingScope =
            resp.value("ok").toBool() &&
            resp.value("scope").toString() ==
                QStringLiteral("mainnode/forkmesh") &&
            resp.value("room").toString() == kDefaultRoomName;
        if (matchingScope && !pass.isEmpty()) {
            m_roomPassphrase = pass;



            if (m_backend)
                m_backend->setRoomPassphrase(pass);
        }
    });
}

void MainWindow::startOfficeChannelMirror()
{


    const QString node = accountOwner();
    if (node.isEmpty() || !hasOwnerSigningCapability(node) ||
        !m_profileIdentity.isValid())
        return;
    if (!m_officeChannelMirror) {
        m_officeChannelMirror = new OfficeChannelMirror(m_networkAccess, this);
        m_officeChannelMirror->setSigner([this](const QByteArray &canonical) {
            return m_profileIdentity.signData(canonical);
        });
        m_officeChannelMirror->setConnectionAuthorizer(
            [this](const QUrl &endpoint) {
                return authorizeFirewallConnection(QStringLiteral("WebSocket"),
                                                   endpoint);
            });
        connect(m_officeChannelMirror, &OfficeChannelMirror::messageArrived,
                this, &MainWindow::onMessage);
        connect(m_officeChannelMirror, &OfficeChannelMirror::sendActivity,
                this, &MainWindow::logSystem);
        connect(m_officeChannelMirror, &OfficeChannelMirror::messageSendFailed,
                this,
                [this](const QString &conversation, const QString &text,
                       const QString &reason) {
                    logSystem(QStringLiteral("Office chat: could not send to %1 "
                                             "(%2).")
                                  .arg(conversation, reason));


                    if (conversation == m_currentConversation &&
                        m_messageInput && m_messageInput->text().isEmpty()) {
                        m_messageInput->setText(text);
                        m_messageInput->setFocus();
                    }
                });
        connect(m_officeChannelMirror, &OfficeChannelMirror::conversationsChanged,
                this, [this](const QStringList &conversations) {
                    m_officeConversations = conversations;


                    setChannels(m_channels);
                    refreshChatMembers();
                });
        connect(m_officeChannelMirror, &OfficeChannelMirror::roomMembersChanged,
                this, [this](const QString &conversation, const QStringList &) {
                    if (conversation == m_currentConversation)
                        refreshChatMembers();
                });
    }
    m_officeChannelMirror->setApiBase(catalogApiUrl());
    m_officeChannelMirror->setIdentity(node, m_profileIdentity.publicKey(),
                                       chatDisplayName());
    m_officeChannelMirror->start();
}

void MainWindow::showAdminVerifyDialog()
{
    const QString node = accountOwner();
    if (node.isEmpty() || !hasOwnerSigningCapability(node) ||
        !m_profileIdentity.isValid())
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
        "New users awaiting manual review because the configured email "
        "verification provider did not confirm delivery:"));
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
    if (node.isEmpty() || target.isEmpty() ||
        !hasOwnerSigningCapability(node) || !m_profileIdentity.isValid())
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

void MainWindow::setPendingRelayJoins(int count)
{
    count = qMax(0, count);
    const int previous = m_pendingRelayJoins;
    m_pendingRelayJoins = count;
    if (m_relayJoinDot && m_relayMenuButton) {
        if (count > 0) {


            m_relayJoinDot->move(
                qMax(0, m_relayMenuButton->width() - m_relayJoinDot->width() -
                            2),
                2);
            m_relayJoinDot->show();
            m_relayJoinDot->raise();
        } else {
            m_relayJoinDot->hide();
        }
    }
    if (m_relayJoinApproveButton) {
        m_relayJoinApproveButton->setText(
            count > 1 ? QStringLiteral("Approve (%1)").arg(count)
                      : QStringLiteral("Approve"));
        m_relayJoinApproveButton->setToolTip(
            count > 1
                ? QStringLiteral(
                      "%1 newly launched ForkMesh instances pinged this relay "
                      "asking to join. Approve to link them — each joins the "
                      "Worlds and starts the firework show.")
                      .arg(count)
                : QStringLiteral(
                      "A newly launched ForkMesh instance pinged this relay "
                      "asking to join. Approve to link it — it joins the "
                      "Worlds and the firework show starts."));
        m_relayJoinApproveButton->setVisible(count > 0);
    }
    if (count > previous)
        logSystem(QStringLiteral(
            "A new ForkMesh instance is ready to be linked — click Approve "
            "beside the relay favicon to let it join."));
}

void MainWindow::showRelayJoinApprovalDialog()
{
    const QString node = accountOwner();
    if (node.isEmpty() || !hasOwnerSigningCapability(node) ||
        !m_profileIdentity.isValid())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-admin-relays-v1\n" + node + "\n" + ts).toUtf8();
    QUrl url = accountsApiUrl("admin-relays");
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

    QDialog dialog(this);
    dialog.setWindowTitle("Link new instances");
    dialog.resize(560, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(
        "Newly launched ForkMesh instances that pinged this relay asking to "
        "join the federation. Approving links an instance into the Worlds — "
        "and the firework show starts.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    auto *rows = new QVBoxLayout(inner);
    int pendingShown = 0;
    for (const QJsonValue &v : resp.value("relays").toArray()) {
        const QJsonObject obj = v.toObject();
        if (obj.value("status").toString() != QStringLiteral("pending"))
            continue;
        const QString pubkey = obj.value("pubkey").toString();
        if (pubkey.isEmpty())
            continue;
        ++pendingShown;
        const QString label = obj.value("label").toString();
        const QString baseUrl = obj.value("baseUrl").toString();
        auto *row = new QHBoxLayout;
        auto *info = new QLabel(
            QStringLiteral("<b>%1</b><br><span style='color:#8b949e'>%2</span>")
                .arg((label.isEmpty() ? QStringLiteral("Unnamed instance")
                                      : label)
                         .toHtmlEscaped(),
                     baseUrl.toHtmlEscaped()));
        info->setTextFormat(Qt::RichText);
        row->addWidget(info, 1);
        auto *blockBtn = new QPushButton("Block");
        blockBtn->setObjectName("ghostButton");
        blockBtn->setCursor(Qt::PointingHandCursor);
        row->addWidget(blockBtn);
        auto *approveBtn = new QPushButton("Approve && link");
        approveBtn->setObjectName("primaryButton");
        approveBtn->setCursor(Qt::PointingHandCursor);
        row->addWidget(approveBtn);
        rows->addLayout(row);
        connect(approveBtn, &QPushButton::clicked, &dialog,
                [this, pubkey, approveBtn, blockBtn]() {
                    approveBtn->setEnabled(false);
                    blockBtn->setEnabled(false);
                    if (adminRelayApprove(pubkey, QStringLiteral("approve"))) {
                        approveBtn->setText("Linked \xE2\x9C\x93");
                    } else {
                        approveBtn->setText("Failed");
                        approveBtn->setEnabled(true);
                        blockBtn->setEnabled(true);
                    }
                });
        connect(blockBtn, &QPushButton::clicked, &dialog,
                [this, pubkey, approveBtn, blockBtn]() {
                    approveBtn->setEnabled(false);
                    blockBtn->setEnabled(false);
                    if (adminRelayApprove(pubkey, QStringLiteral("block"))) {
                        blockBtn->setText("Blocked");
                    } else {
                        blockBtn->setText("Failed");
                        approveBtn->setEnabled(true);
                        blockBtn->setEnabled(true);
                    }
                });
    }
    if (!pendingShown)
        rows->addWidget(
            new QLabel("<i>No instances waiting to be linked.</i>"));
    rows->addStretch();
    scroll->setWidget(inner);
    layout->addWidget(scroll, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    dialog.exec();
}

bool MainWindow::adminRelayApprove(const QString &pubkey, const QString &action)
{
    const QString node = accountOwner();
    if (node.isEmpty() || pubkey.isEmpty() ||
        !hasOwnerSigningCapability(node) || !m_profileIdentity.isValid())
        return false;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-admin-relay-approve-v1\n" + node + "\n" + pubkey + "\n" +
         action + "\n" + ts)
            .toUtf8();
    int status = 0;
    const QJsonObject resp = postAccountSync(
        "admin-relay-approve",
        QJsonObject{{"node", node}, {"ts", ts},
                    {"sig", m_profileIdentity.signData(canonical)},
                    {"pubkey", pubkey}, {"action", action}},
        &status);
    if (status != 200 || !resp.value("ok").toBool())
        return false;
    setPendingRelayJoins(m_pendingRelayJoins - 1);
    logSystem(action == QStringLiteral("approve")
                  ? QStringLiteral(
                        "\xF0\x9F\x8E\x86 Instance linked: it joined the "
                        "federation and the World is running its firework "
                        "show.")
                  : QStringLiteral("Instance join request blocked."));
    return true;
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

QString MainWindow::hostLinkUserName()
{
    if (!hasOwnerSigningCapability(accountOwner()))
        return QString();
    const QString linkedOwner = m_nodeOwnerUser.trimmed().toLower();
    if (!linkedOwner.isEmpty())
        return linkedOwner;

    const QString signer = accountOwner().trimmed().toLower();
    if (signer.isEmpty())
        return QString();

    if (m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty())
        return signer;

    if (!m_networkAccess)
        return QString();

    int status = 0;
    const QJsonObject lookup = getAccountSync(signer, &status);
    if (status != 200 || !lookup.value(QStringLiteral("exists")).toBool())
        return QString();

    m_nodeOwnerUser = lookup.value(QStringLiteral("owner")).toString().trimmed().toLower();
    m_profileIsUserAccount =
        lookup.value(QStringLiteral("kind")).toString() == QStringLiteral("user");
    if (!m_nodeOwnerUser.isEmpty())
        return m_nodeOwnerUser;
    return m_profileIsUserAccount ? signer : QString();
}

QString MainWindow::hostLinkSigningAccountName(QString *userName)
{
    if (userName)
        userName->clear();
    const QString signer = accountOwner().trimmed().toLower();
    if (signer.isEmpty() || !hasOwnerSigningCapability(signer))
        return QString();
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load())
        return QString();
    const QString user = hostLinkUserName();
    if (user.isEmpty())
        return QString();
    if (userName)
        *userName = user;
    return signer;
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

QString MainWindow::topBarUserName() const
{
    const QString linkedOwner = m_nodeOwnerUser.trimmed().toLower();
    if (!linkedOwner.isEmpty())
        return linkedOwner;

    const QString account = settingsAccountName();
    if (!account.isEmpty())
        return account;

    return accountNameFromInput(m_userName, QString());
}

QString MainWindow::nodeOwnerDisplayName() const
{
    const QString linkedOwner = m_nodeOwnerUser.trimmed().toLower();
    if (!linkedOwner.isEmpty())
        return linkedOwner;

    const QString account = accountOwner().trimmed().toLower();
    if (account.isEmpty())
        return QString();



    if (m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty())
        return account;

    return QString();
}

QString MainWindow::chatDisplayName() const
{




    if (chatIdentityIsGuest())
        return guestChatName();
    const QString user = topBarUserName().trimmed();
    if (!user.isEmpty())
        return user.left(80);
    return accountNameFromInput(m_userName, QString()).left(80);
}

bool MainWindow::chatIdentityIsGuest() const
{


    if (m_headless)
        return false;

    if (m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty() ||
        !m_nodeOwnerUser.trimmed().isEmpty())
        return false;



    const QString generated = accountNameFromInput(
        QSettings().value(kGeneratedNodeNameSetting).toString(), QString());
    return !generated.isEmpty() &&
           savedProfileName().compare(generated, Qt::CaseInsensitive) == 0;
}

QString MainWindow::guestChatName() const
{




    const QString generated =
        QSettings().value(kGeneratedNodeNameSetting).toString();
    static const QRegularExpression tailDigits(QStringLiteral("(\\d{4})$"));
    const QRegularExpressionMatch match = tailDigits.match(generated);
    QString digits = match.hasMatch() ? match.captured(1) : QString();
    if (digits.isEmpty()) {
        const QByteArray key = m_profileIdentity.publicKey().toUtf8();
        if (!key.isEmpty()) {
            const QByteArray digest =
                QCryptographicHash::hash(key, QCryptographicHash::Sha256);
            quint32 value = 0;
            for (int i = 0; i < 4; ++i)
                value = (value << 8) | quint8(digest.at(i));
            digits = QString::number(1000 + value % 9000);
        }
    }
    return digits.isEmpty() ? QStringLiteral("Guest")
                            : QStringLiteral("Guest %1").arg(digits);
}










QString MainWindow::machineNodeName() const
{
    const QString saved = accountNameFromInput(
        QSettings().value(kMachineNodeNameSetting).toString(), QString());
    if (!saved.isEmpty())
        return saved;
    if (m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty()) {
        const QString host =
            accountNameFromInput(QSysInfo::machineHostName(), QString());
        if (!host.isEmpty())
            return host;
    }
    return accountOwner();
}




void MainWindow::saveMachineNodeName(const QString &name)
{
    const QString clean = accountNameFromInput(name, QString());
    if (clean.isEmpty())
        QSettings().remove(kMachineNodeNameSetting);
    else
        QSettings().setValue(kMachineNodeNameSetting, clean);
    updateChatIdentity();
    updateNavSolanaBalance();
    if (m_profileIsSelf)
        renderProfileAccountStatus();
    logSystem("This machine's node name is now \"" + machineNodeName() + "\".");
}

void MainWindow::updateChatIdentity()
{
    if (!m_backend)
        return;
    const QString name = chatDisplayName();
    if (!name.isEmpty() && name != m_lastChatDisplayName) {
        m_backend->setUserName(name);
        m_lastChatDisplayName = name;
    }



    m_backend->setNodeIdentity(machineNodeName(), nodeOwnerDisplayName());
    const QByteArray avatar = effectiveUserAvatar();
    if (avatar != m_lastChatAvatar) {
        m_backend->setAvatar(avatar);
        m_lastChatAvatar = avatar;
    }
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



    if (verified && !normalized.isEmpty() &&
        normalized == settingsAccountName())
        maybeAnnounceWelcome();
}

void MainWindow::refreshSettingsEmailVerifiedBadge()
{
    updateUserSwitcher();
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

bool MainWindow::hasOwnerSigningCapability(
    const QString &signerAccount) const
{
    if (!AccountCapability::ownerSigningAllowed(
            hasActiveAccountSession(), m_accountDesktopCapable,
            m_accountName, signerAccount)) {
        return false;
    }
    const QSettings settings;
    return AccountCapability::persistedMarkerMatches(
        settings.value(kDesktopCapableAccountSetting).toString(),
        settings.value(kDesktopCapablePublicKeySetting).toString(),
        m_accountName, m_profileIdentity.publicKey());
}

void MainWindow::setDesktopCapability(const QString &accountName,
                                      bool capable)
{
    const QString normalized =
        AccountCapability::normalizedAccount(accountName);
    const QString publicKey = m_profileIdentity.publicKey().trimmed();
    m_accountDesktopCapable =
        capable && !normalized.isEmpty() && !publicKey.isEmpty();
    QSettings settings;
    if (m_accountDesktopCapable) {
        settings.setValue(kDesktopCapableAccountSetting, normalized);
        settings.setValue(kDesktopCapablePublicKeySetting, publicKey);
        return;
    }
    settings.remove(kDesktopCapableAccountSetting);
    settings.remove(kDesktopCapablePublicKeySetting);
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    stopRepoHosts();
}

bool MainWindow::restoreDesktopCapability(const QString &accountName)
{
    const QString stored =
        QSettings().value(kDesktopCapableAccountSetting).toString();
    const QString storedPublicKey =
        QSettings().value(kDesktopCapablePublicKeySetting).toString();
    m_accountDesktopCapable =
        AccountCapability::persistedMarkerMatches(
            stored, storedPublicKey, accountName,
            m_profileIdentity.publicKey());
    return m_accountDesktopCapable;
}

QString MainWindow::catalogOwner(const RepositoryRecord &repo) const
{
    const QString account = accountOwner();
    return account.isEmpty() ? repoSegment(repo.owner, QStringLiteral("owner"))
                             : account;
}

QUrl MainWindow::accountsApiUrl(const QString &leaf) const
{
    QUrl url = catalogApiUrl();
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



    const QString viewer = accountOwner();
    if (m_profileIdentity.isValid() && !viewer.isEmpty() &&
        hasOwnerSigningCapability(viewer)) {
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
        return;
    }
    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.cloneUrl = cloneUrl;
    repo.publishToNetwork = true;



    repo.isPrivate = isPrivate;
    if (isPrivate) {
        const QString accessId =
            m_privateCatalogAccessIds.value(owner + "/" + name);
        if (PrivateMirrorStore::isOpaqueId(accessId))
            repo.privateReplicaId = accessId;
    }


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



    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/") + safeOwner + QLatin1Char('/') + safeName);
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString();
}

bool MainWindow::serviceManagedCheckout(const QString &localPath) const
{
    const QString canonical =
        QFileInfo(localPath.trimmed()).canonicalFilePath();
    const QString serviceHome =
        QFileInfo(QDir::homePath()).canonicalFilePath();
    return !canonical.isEmpty() && !serviceHome.isEmpty() &&
           (canonical == serviceHome ||
            canonical.startsWith(serviceHome + QLatin1Char('/'))) &&
           QFileInfo(QDir(canonical).filePath(QStringLiteral(".git")))
               .exists();
}

void MainWindow::ensureFlagshipRepo()
{
    if (!m_networkAccess)
        return;
    const QString canonicalOwner = QStringLiteral("forkmesh");
    const QString canonicalName = QStringLiteral("forkmesh");
    const QString canonicalClone = hostedCloneUrl(canonicalOwner, canonicalName);
    const QString canonicalMirrorPath =
        repositoryMirrorRoot() + "/" +
        repoSegment(canonicalOwner, QStringLiteral("owner")) + "-" +
        repoSegment(canonicalName, QStringLiteral("repository")) + ".git";





    for (int i = 0; i < m_repositories.size(); ++i) {
        RepositoryRecord &repo = m_repositories[i];
        if (repo.previewOnly ||
            repo.name.compare(canonicalName, Qt::CaseInsensitive) != 0)
            continue;
        const QUrl clone(repo.cloneUrl.trimmed());
        const QString relayHost = catalogApiUrl().host();
        const bool relayClone =
            !clone.host().isEmpty() &&
            clone.host().compare(relayHost, Qt::CaseInsensitive) == 0;
        const bool managedFlagship =
            m_headless ||
            relayClone ||
            repo.owner.compare(canonicalOwner, Qt::CaseInsensitive) == 0 ||
            repo.owner.compare(QStringLiteral("newnewnode"), Qt::CaseInsensitive) == 0;
        if (managedFlagship) {
            bool changed = false;
            if (repo.owner.compare(canonicalOwner, Qt::CaseInsensitive) != 0) {
                repo.owner = canonicalOwner;
                changed = true;
            }
            if (!canonicalClone.isEmpty() &&
                repo.cloneUrl.compare(canonicalClone, Qt::CaseInsensitive) != 0) {
                repo.cloneUrl = canonicalClone;
                changed = true;
            }
            if (m_headless && !repo.localPath.trimmed().isEmpty()) {








                if (!serviceManagedCheckout(repo.localPath)) {
                    repo.localPath.clear();
                    changed = true;
                }
            }
            if (repo.mirrorPath.trimmed().isEmpty() ||
                (m_headless &&
                 QDir::cleanPath(repo.mirrorPath) != QDir::cleanPath(canonicalMirrorPath))) {
                repo.mirrorPath = canonicalMirrorPath;
                changed = true;
            }
            if (!repo.publishToNetwork) {
                repo.publishToNetwork = true;
                changed = true;
            }
            if (repo.hostedSinceMs <= 0) {
                repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
                changed = true;
            }
            const bool hasMirror =
                !repo.mirrorPath.trimmed().isEmpty() &&
                QDir(repo.mirrorPath).exists();
            const QString servedBranch =
                hasMirror ? mirrorHeadBranch(repo.mirrorPath) : QString();
            const bool needsSync =
                !hasMirror || servedBranch.isEmpty() || repo.lastSyncMs <= 0;
            if (changed) {
                saveRepositories();
                refreshRepositoryList();
            }
            if ((changed || needsSync) && !m_syncingRepos.contains(i)) {
                logSystem("Mirroring forkmesh/forkmesh from " + repo.cloneUrl);
                syncRepository(i,  true);
            } else if (repo.publishToNetwork && hasMirror) {
                publishRepository(i, false);
                startRepoHosts();
            }
        }


        return;
    }

    const QJsonArray repos = fetchCatalogRepos();
    QString owner = canonicalOwner;
    QString cloneUrl = canonicalClone;
    bool ownerLive = false;
    for (const QJsonValue &v : repos) {
        const QJsonObject r = v.toObject();
        if (r.value("name").toString().compare(QStringLiteral("forkmesh"),
                                               Qt::CaseInsensitive) != 0)
            continue;
        const QString candidateOwner = r.value("owner").toString();
        if (candidateOwner.compare(canonicalOwner, Qt::CaseInsensitive) != 0)
            continue;
        QString candidateUrl = r.value("cloneUrl").toString().trimmed();
        if (candidateUrl.isEmpty())
            candidateUrl = canonicalClone;
        if (candidateUrl.isEmpty())
            continue;
        owner = canonicalOwner;
        cloneUrl = candidateUrl;
        ownerLive = r.value("liveHost").toBool();
        break;
    }
    if (cloneUrl.isEmpty())
        return;
    if (!ownerLive)
        logSystem("No live ForkMesh host right now; mirroring forkmesh/forkmesh "
                  "through the relay so it appears once a host comes online.");



    if (m_freshInstall) {
        m_pendingAutoOpenRepoKey = canonicalOwner + "/forkmesh";
        flashMessage(QStringLiteral("Syncing the ForkMesh project repo\xE2\x80\xA6"));
    }
    mirrorCatalogRepo(canonicalOwner, QStringLiteral("forkmesh"), cloneUrl);
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
            setDesktopCapability(accountName,
                                 m_testAccountFlowDesktopCapable);
        }
        Q_UNUSED(solana);
        return m_testAccountFlowResult;
    }
#endif
    Q_UNUSED(solana);
    if (hasOwnerSigningCapability(accountName))
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


    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == "active")
        return runLoginFlow(accountName);



    return runSignupFlow(accountName, solana);
}





bool MainWindow::authenticateSilently(const QString &accountName)
{
    if (hasOwnerSigningCapability(accountName))
        return true;
    if (!isValidNodeName(accountName))
        return false;
    if (m_accountAuthenticated &&
        AccountCapability::normalizedAccount(m_accountName) !=
            AccountCapability::normalizedAccount(accountName)) {
        setDesktopCapability(m_accountName, false);
        m_accountAuthenticated = false;
        m_accountTier = QStringLiteral("free");
        m_accountSolanaVerified = false;
        m_isAdmin = false;
        QSettings().remove(kAuthedAccountSetting);
    }
    int status = 0;
    const QJsonObject lookup = getAccountSync(accountName, &status);
    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == "active" &&
        lookup.value("pubkey").toString() == m_profileIdentity.publicKey()) {
        m_accountAuthenticated = true;
        m_accountName = accountName;
        m_accountTier = QStringLiteral("active");
        m_accountSolanaVerified = true;
        setDesktopCapability(accountName, true);
        m_nodeOwnerUser = lookup.value("owner").toString();




        m_profileIsUserAccount =
            lookup.value(QStringLiteral("kind")).toString() ==
            QStringLiteral("user");
        cacheWebUserSolanaProfile(accountName, lookup);
        QSettings().setValue(kAuthedAccountSetting, accountName);
        applyAccountEmailVerified(accountName,
                                  lookup.value("emailVerified").toBool());




        updateChatIdentity();
        if (m_settingsNameEdit && m_settingsNameEdit->text().trimmed().isEmpty())
            m_settingsNameEdit->setText(m_accountName);
        return true;
    }





    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == "active") {
        m_accountSolanaVerified = true;
        setDesktopCapability(accountName, false);
    }







    const bool cachedHere =
        AccountCapability::normalizedAccount(
            QSettings().value(kAuthedAccountSetting).toString()) ==
        AccountCapability::normalizedAccount(accountName);
    if (cachedHere && status != 200 &&
        restoreDesktopCapability(accountName)) {
        m_accountAuthenticated = true;
        m_accountName = accountName;
        m_accountTier = QStringLiteral("active");
        m_accountSolanaVerified = true;
        refreshSettingsEmailVerifiedBadge();
        return true;
    }
    if (status == 200)
        setDesktopCapability(accountName, false);
    return false;
}








bool MainWindow::registerNodeAccountSilently(const QString &accountName)
{
    if (!isValidNodeName(accountName))
        return false;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load())
        return false;

    const QString linkCode =
        qEnvironmentVariable("FORKMESH_LINK_CODE").trimmed();
    static const QRegularExpression linkCodeRe(QStringLiteral("^[0-9]{6}$"));
    const bool installerLinkPending =
        linkCodeRe.match(linkCode).hasMatch() &&
        m_nodeOwnerUser.trimmed().isEmpty();



    if (hasOwnerSigningCapability(accountName) && !installerLinkPending)
        return true;

    auto activateSession = [&](const QString &owner, bool emailVerified) {
        m_accountAuthenticated = true;
        m_accountName = accountName;
        m_accountTier = QStringLiteral("active");
        m_accountSolanaVerified = true;
        setDesktopCapability(accountName, true);
        m_nodeOwnerUser = owner;
        QSettings().setValue(kAuthedAccountSetting, accountName);
        applyAccountEmailVerified(accountName, emailVerified);


        updateChatIdentity();
    };

    auto reclaimWithInstallerLinkCode = [&]() -> bool {
        if (!linkCodeRe.match(linkCode).hasMatch())
            return false;
        const QString pubkey = m_profileIdentity.publicKey();
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        const QByteArray canonical =
            ("forkmesh-reclaim-node-v1\n" + accountName + "\n" + pubkey +
             "\n" + linkCode + "\n" + ts).toUtf8();
        int status = 0;
        const QJsonObject resp = postAccountSync(
            "reclaim-node",
            QJsonObject{{"nodeName", accountName},
                        {"pubkey", pubkey},
                        {"linkCode", linkCode},
                        {"ts", ts},
                        {"sig", m_profileIdentity.signData(canonical)}},
            &status);
        if (status == 200 && resp.value("ok").toBool() &&
            resp.value("linked").toBool()) {
            activateSession(resp.value("user").toString(),
                            resp.value("emailVerified").toBool());
            logSystem("Account: reclaimed headless node \"" + accountName +
                      "\" for owner \"" + m_nodeOwnerUser +
                      "\"; its mirrors will now publish and host.");
            return true;
        }
        if (status == 202 && resp.value("pending").toBool()) {
            logSystem("Account: link code accepted for \"" + accountName +
                      "\"; waiting for the desktop link offer before hosting.");
        } else if (status) {
            logSystem("Account: could not reclaim \"" + accountName +
                      "\" with the installer link code.");
        }
        return false;
    };





    int lookupStatus = 0;
    const QJsonObject lookup = getAccountSync(accountName, &lookupStatus);
    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == QStringLiteral("active") &&
        lookup.value("pubkey").toString() != m_profileIdentity.publicKey()) {
        if (reclaimWithInstallerLinkCode())
            return true;
        logSystem("Account: \"" + accountName + "\" is registered to another "
                  "node; this headless node will keep mirroring without hosting "
                  "under that name.");
        return false;
    }


    if (lookupStatus == 0)
        return false;







    if (lookup.value("exists").toBool() &&
        lookup.value("status").toString() == QStringLiteral("active") &&
        lookup.value("pubkey").toString() == m_profileIdentity.publicKey()) {
        if (reclaimWithInstallerLinkCode())
            return true;




        if (installerLinkPending &&
            lookup.value("owner").toString().trimmed().isEmpty())
            return false;
        activateSession(lookup.value("owner").toString(),
                        lookup.value("emailVerified").toBool());
        return true;
    }


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
    m_accountSolanaVerified = true;
    setDesktopCapability(accountName, true);
    m_nodeOwnerUser = fresp.value("owner").toString();
    QSettings().setValue(kAuthedAccountSetting, accountName);
    QSettings().setValue(kAccountNameSetting, accountName);
    applyAccountEmailVerified(accountName, fresp.value("emailVerified").toBool());
    logSystem("Account: registered headless node \"" + accountName +
              "\" (free, key-bound); its mirrors will now publish and host.");



    if (!m_nodeOwnerUser.isEmpty())
        logSystem("Account: node \"" + accountName + "\" attached to owner \"" +
                  m_nodeOwnerUser + "\".");
    return true;
}









void MainWindow::scheduleHeadlessRegisterRetry(const QString &accountName)
{
    if (!m_headlessRegisterRetryTimer) {
        m_headlessRegisterRetryTimer = new QTimer(this);
        m_headlessRegisterRetryTimer->setSingleShot(true);
        connect(m_headlessRegisterRetryTimer, &QTimer::timeout, this,
                [this, accountName] {
                    const QString linkCode =
                        qEnvironmentVariable("FORKMESH_LINK_CODE").trimmed();
                    static const QRegularExpression linkCodeRe(
                        QStringLiteral("^[0-9]{6}$"));
                    const bool installerLinkPending =
                        linkCodeRe.match(linkCode).hasMatch() &&
                        m_nodeOwnerUser.trimmed().isEmpty();
                    if (hasOwnerSigningCapability(accountName) &&
                        !installerLinkPending)
                        return;
                    if (registerNodeAccountSilently(accountName)) {
                        m_headlessRegisterAttempt = 0;
                        if (m_headless)
                            ensureFlagshipRepo();










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


bool MainWindow::verifyTotpLogin(const QString &email,
                                 const QString &password, const QString &totp,
                                 const QString &accountName, bool *fatal)
{
    if (fatal)
        *fatal = false;
    QJsonObject loginRequest{{"email", email}, {"password", password},
                             {"totp", totp}};
    const QString publicKey = m_profileIdentity.publicKey();
    if (!publicKey.isEmpty()) {
        const QString deviceTs =
            QString::number(QDateTime::currentMSecsSinceEpoch());
        const QByteArray deviceCanonical =
            ForkMeshIdentity::deviceBindCanonical(
                accountName, publicKey, deviceTs);
        const QString deviceSig =
            deviceCanonical.isEmpty()
                ? QString()
                : m_profileIdentity.signData(deviceCanonical);
        if (!deviceSig.isEmpty()) {
            loginRequest.insert(QStringLiteral("pubkey"), publicKey);
            loginRequest.insert(QStringLiteral("deviceTs"), deviceTs);
            loginRequest.insert(QStringLiteral("deviceSig"), deviceSig);



            loginRequest.insert(QStringLiteral("deviceLabel"),
                                machineNodeName());
        }
    }
    int status = 0;
    const QJsonObject resp =
        postAccountSync("login", loginRequest, &status);
    auto acceptLogin = [&](const QJsonObject &payload, bool ownsDesktopKey) {
        m_accountAuthenticated = true;
        m_accountName = payload.value("nodeName").toString(accountName);
        m_accountTier = QStringLiteral("active");




        m_accountSolanaVerified = true;
        setDesktopCapability(m_accountName, ownsDesktopKey);



        QSettings().setValue(kAuthedAccountSetting, m_accountName);
        applyAccountEmailVerified(m_accountName,
                                  payload.value("emailVerified").toBool());



        m_accountSessionToken = payload.value("sessionToken").toString();
        m_profileIsUserAccount =
            payload.value(QStringLiteral("kind")).toString() ==
            QStringLiteral("user");
        cacheWebUserSolanaProfile(m_accountName, payload);



        if (ownsDesktopKey)
            ensurePrivateMirrorRecipientIdentityRegistered();
        const QString serverAvatar = payload.value("avatarPng").toString();
        if (!serverAvatar.isEmpty()) {
            const QByteArray png =
                QByteArray::fromBase64(serverAvatar.toLatin1());
            if (!png.isEmpty() && png != m_userAvatar) {
                m_userAvatar = png;
                QSettings().setValue(kAvatarSetting, png);
                updateAvatarButton();
                updateUserAvatarButton();
                updateChatIdentity();
            }
        } else {
            pushAccountAvatar();
        }
        updateUserSwitcher();
    };

    if (status == 200 && resp.value("ok").toBool()) {
        const bool ownsDesktopKey =
            resp.value(QStringLiteral("deviceKeyMatched")).toBool(false) &&
            resp.value(QStringLiteral("desktopCapable")).toBool(false);
        acceptLogin(resp, ownsDesktopKey);
        return true;
    }
    const QString err = resp.value("error").toString();






    if (AccountCapability::allowsPasswordOnlyFallback(err)) {
        int webStatus = 0;
        const QJsonObject webResp = postAccountSync(
            "login",
            QJsonObject{{"email", email}, {"password", password}, {"totp", totp}},
            &webStatus);
        if (webStatus == 200 && webResp.value("ok").toBool()) {
            acceptLogin(webResp, false);
            QMessageBox::information(
                this, "Logged in",
                err == QLatin1String("device_proof_required")
                    ? "You are signed in with email/password for browsing and "
                      "account features. This device's identity proof was not "
                      "accepted, so publishing, hosting, and owner-signed "
                      "actions stay disabled. Check this computer's clock and "
                      "identity, then sign in again to restore them."
                : err == QLatin1String("device_key_conflict")
                    ? "You are signed in with email/password for browsing and "
                      "account features. This desktop key belongs to another "
                      "account, so publishing, hosting, and owner-signed actions "
                      "stay disabled until you import or create the correct key."
                    : "You are signed in with email/password, but this account "
                      "is already bound to a different desktop key. Browsing and "
                      "account features will work from this device; publishing, "
                      "hosting, and owner-signed actions require the original "
                      "desktop key or an explicit account-key rotation/import.");
            return true;
        }
    }

    if (fatal)
        *fatal = false;
    QMessageBox::warning(this, "Log in",
                         err == "bad_totp"
                             ? "Incorrect authenticator code."



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
                             : err == "device_proof_required"
                                   ? "This desktop could not prove possession of "
                                     "its identity key. Reload the identity or "
                                     "restart ForkMesh, then try again."
                             : err == "device_key_conflict"
                                   ? "This desktop identity is already registered "
                                     "to another account. Import the identity for "
                                     "this account or use a separate desktop key."
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


        if (fatal)
            break;
    }
    if (m_setupError) {
        m_setupError->setText("Account login is required to join the network.");
        m_setupError->show();
    }
    return false;
}







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
        if (status == 0) {
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
        m_accountSolanaVerified = true;
        setDesktopCapability(name, true);
        QSettings().setValue(kAuthedAccountSetting, name);
        joined = true;
        dialog.accept();
    };
    connect(joinBtn, &QPushButton::clicked, this, doJoin);
    connect(nameEdit, &QLineEdit::returnPressed, this, [&]() {
        if (joinBtn->isEnabled())
            doJoin();
    });



    joinBtn->setEnabled(isValidNodeName(accountName));

    dialog.exec();
    if (joined) {



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



    const QString address = savedSolanaAddress().trimmed();
    if (address.isEmpty() ||
        !forkmesh::control::isValidSolanaPublicAddress(address)) {
        QMessageBox::information(
            this, QStringLiteral("Reward settings"),
            QStringLiteral(
                "Add one valid public self-custodial Solana payout address. "
                "Never enter a private key, seed, mnemonic, or recovery phrase."));
        return;
    }
    const QString name = accountOwner();
    if (name.isEmpty() || !hasOwnerSigningCapability(name)) {
        QMessageBox::information(
            this, QStringLiteral("Reward settings"),
            QStringLiteral(
                "Register or sign in to this node from Account settings first. "
                "Checking reward settings does not create a wallet or request a deposit, "
                "and it does not register an account."));
        return;
    }
    sendNodeHeartbeat();
    if (m_profileEligibility)
        m_profileEligibility->setText(
            QString::fromUtf8(
                "<span style='color:#3fb950'>Public payout address configured "
                "\xC2\xB7 may be eligible</span>"));
    updateSolanaNotice();
    flashMessage(QStringLiteral(
        "Reward settings checked. Selection and payment are not guaranteed."));
}

void MainWindow::persistProfile()
{
    const QString name = accountNameFromInput(m_nameEdit->text(), m_userName);
    m_nameEdit->setText(name);
    saveProfileName(name);
    const QString address = m_solanaEdit->text().trimmed();
    if (address.isEmpty() ||
        forkmesh::control::isValidSolanaPublicAddress(address)) {
        saveSolanaAddress(address);
    } else {
        m_solanaEdit->clear();
        saveSolanaAddress(QString());
        flashMessage(
            QStringLiteral(
                "Invalid payout-address text was removed. ForkMesh accepts only "
                "a public Solana address, never a private key or recovery phrase."),
            true);
    }
    updateNavSolanaBalance();
}



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




    appendUpdateLog(QStringLiteral("==> ForkMesh update started\n"));
}

void MainWindow::appendUpdateLog(const QString &text)
{
    if (!m_updateLog || text.isEmpty())
        return;
    m_updateLog->moveCursor(QTextCursor::End);
    m_updateLog->insertPlainText(text);
    m_updateLog->moveCursor(QTextCursor::End);



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













    m_footerUpdateLog->setStyleSheet(
        QStringLiteral("QTextEdit#footerUpdateLog{color:#1f2328;border:none;"
                       "background:#ffffff;font-family:monospace;font-size:11px;"
                       "padding:3px 1px 3px 12px;}"
                       "QTextEdit#footerUpdateLog QScrollBar:vertical{"
                       "background:#ffffff;width:9px;margin:0;}"
                       "QTextEdit#footerUpdateLog QScrollBar::handle:vertical{"
                       "background:#d0d7de;border-radius:4px;min-height:24px;}"
                       "QTextEdit#footerUpdateLog QScrollBar::handle:vertical:"
                       "hover{background:#afb8c1;}"
                       "QTextEdit#footerUpdateLog QScrollBar::add-line:vertical,"
                       "QTextEdit#footerUpdateLog QScrollBar::sub-line:vertical{"
                       "height:0;}"
                       "QTextEdit#footerUpdateLog QScrollBar::add-page:vertical,"
                       "QTextEdit#footerUpdateLog QScrollBar::sub-page:vertical{"
                       "background:#ffffff;}"));
}






QString MainWindow::footerLogLineHtml(const QString &clean)
{
    QString time, message = clean;
    if (clean.size() >= 21 && clean.at(10) == QLatin1Char(' ')) {
        time = clean.mid(11, 8);
        message = clean.mid(21);
    }
    const QString badge = logBadgeFor(clean);
    const QString accent = logAccentFor(clean);
    QString html;




    html += logPromptIconTag(m_footerUpdateLog, clean);
    html += logFaviconTag(message, m_footerUpdateLog);
    if (!time.isEmpty())
        html += QStringLiteral("<span style='color:#656d76'>%1</span>&nbsp;&nbsp;")
                    .arg(time);
    html += QStringLiteral(
                "<span style='color:%1; font-weight:700'>%2</span>&nbsp;&nbsp;"
                "<span style='color:#1f2328'>%3</span>")
                .arg(accent, badge.leftJustified(7).toHtmlEscaped(),
                     forkmesh::colorizeBackgroundMarker(message.toHtmlEscaped()));
    return html;
}

void MainWindow::setFooterUpdateLine(const QString &line)
{
    if (!m_footerUpdateLog)
        return;
    const QString clean = line.trimmed();
    if (clean.isEmpty())
        return;
    const QString html = footerLogLineHtml(clean);




    QScrollBar *bar = m_footerUpdateLog->verticalScrollBar();
    const bool follow = !m_footerLogScrollPaused;
    m_footerUpdateLog->append(html);










    QTextDocument *doc = m_footerUpdateLog->document();
    if (const int excess = doc->blockCount() - kFooterLogSeedLines; excess > 0) {
        QTextCursor trim(doc);
        trim.beginEditBlock();
        trim.movePosition(QTextCursor::Start);
        trim.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, excess);
        trim.removeSelectedText();
        trim.endEditBlock();
    }



    if (QTextBlock last = m_footerUpdateLog->document()->lastBlock(); last.isValid())
        last.setUserData(new FooterLogLineData(clean));
    if (follow && bar)
        bar->setValue(bar->maximum());
}

void MainWindow::setUpdateStatus(const QString &status, bool isError)
{
    logRestart(isError ? QStringLiteral("ERROR: %1").arg(status) : status);


    appendUpdateLog((isError ? QStringLiteral("!! ") : QStringLiteral("==> ")) + status +
                    QStringLiteral("\n"));


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
                          haveWorktree,  4))
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


    appendUpdateLog(QStringLiteral("\n$ %1\n  (in %2)\n").arg(commandLine, workingDir));
    QElapsedTimer stepTimer;
    stepTimer.start();
    auto *process = new QProcess(this);
    process->setWorkingDirectory(workingDir);
    process->setProcessChannelMode(QProcess::MergedChannels);


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


        const QString repoDir = QFileInfo(clientDir).absolutePath();
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
                           QString::number(ramCappedBuildJobs())},
                          buildDir, [this, buildDir, appPath] {
            const QString built = builtExecutablePath(buildDir);
            installAndRelaunch(built, appPath);
        });
    });
}







static bool binaryPassesStartCheck(const QString &binary, const QString &asUser)
{
    QProcess probe;
    if (asUser.isEmpty()) {
        probe.start(binary, {QStringLiteral("--version")});
    } else {
        probe.start(QStringLiteral("sudo"),
                    {QStringLiteral("-u"), asUser, QStringLiteral("-H"), binary,
                     QStringLiteral("--version")});
    }
    if (!probe.waitForStarted(5000))
        return false;
    if (!probe.waitForFinished(15000)) {
        probe.kill();
        probe.waitForFinished(2000);
        return false;
    }
    return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
}

void MainWindow::installAndRelaunch(const QString &built, const QString &appPath)
{






    const QStringList relaunchArgs = QCoreApplication::arguments().mid(1);

    if (!m_updateAsUser.isEmpty()) {



        const QString binDir = QFileInfo(appPath).absolutePath();
        const QString script =
            QStringLiteral("mkdir -p %1 && { [ ! -e %3 ] || cp -f %3 %3.bak-update; }"
                           " && cp -f %2 %3 && chmod 0755 %3")
                .arg(shellSingleQuote(binDir), shellSingleQuote(built),
                     shellSingleQuote(appPath));
        setUpdateStatus("Installing for " + m_updateAsUser + "...");
        const QString probeUser = m_updateAsUser;
        if (!binaryPassesStartCheck(built, probeUser)) {
            setUpdateStatus("Update failed: the new build did not pass its "
                            "start check; keeping the current version.",
                            true);
            stopRestartSpin();
            if (m_buildButton)
                m_buildButton->setEnabled(true);
            return;
        }
        runUpdateStep("sudo", {"-u", m_updateAsUser, "-H", "sh", "-c", script},
                      QDir::tempPath(), [this, appPath, relaunchArgs] {
            setUpdateStatus("Relaunching...");
            const QString user = m_updateAsUser;



            forkmesh::releaseSingleInstance();
            QStringList args{QStringLiteral("-u"), user, QStringLiteral("-H"),
                             appPath};
            args += relaunchArgs;
            QProcess::startDetached("sudo", args);
            logRestart(QStringLiteral("relaunched %1; quitting").arg(appPath));
            logSystem(QStringLiteral("=== Restarting now (rebuild & restart) ==="));
            QCoreApplication::quit();
        });
        return;
    }




    if (!binaryPassesStartCheck(built, QString())) {
        setUpdateStatus("Update failed: the new build did not pass its start "
                        "check; keeping the current version.",
                        true);
        stopRestartSpin();
        if (m_buildButton)
            m_buildButton->setEnabled(true);
        return;
    }
    if (QFileInfo(built).canonicalFilePath() !=
        QFileInfo(appPath).canonicalFilePath()) {
        const QString bak = appPath + QStringLiteral(".bak-update");
        QFile::remove(bak);
        if (!QFile::rename(appPath, bak)) {
            setUpdateStatus("Update failed: could not move the current binary "
                            "aside at " + appPath, true);
            stopRestartSpin();
            if (m_buildButton)
                m_buildButton->setEnabled(true);
            return;
        }
        if (!QFile::copy(built, appPath)) {
            QFile::rename(bak, appPath);
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



    forkmesh::releaseSingleInstance();
    if (!QProcess::startDetached(appPath, relaunchArgs)) {
        setUpdateStatus("Update installed but the relaunch failed; still "
                        "running the previous version in memory.",
                        true);
        stopRestartSpin();
        if (m_buildButton)
            m_buildButton->setEnabled(true);
        return;
    }
    logRestart(QStringLiteral("relaunched %1; quitting").arg(appPath));
    logSystem(QStringLiteral("=== Restarting now (rebuild & restart) ==="));
    QCoreApplication::quit();
}

QString MainWindow::resolveInstallCloneUrl()
{
    if (!m_networkAccess)
        return QString();


    QUrl url = catalogApiUrl();
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










static bool fileSha256Matches(const QString &path, const QString &expected)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    if (!hasher.addData(&file))
        return false;
    return QString::fromLatin1(hasher.result().toHex()) ==
           expected.toLower();
}

bool MainWindow::tryPrebuiltAutoUpdate(const QString &clientDir,
                                       const QString &tag,
                                       const QString &tagCommit)
{


    if (!invokingNonRootUser().isEmpty())
        return false;

#if defined(Q_OS_LINUX)
    const QString wantOs = QStringLiteral("linux");
#elif defined(Q_OS_MACOS)
    const QString wantOs = QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    const QString wantOs = QStringLiteral("windows");
#else
    return false;
#endif
    const QString wantArch = QSysInfo::currentCpuArchitecture();



    QByteArray manifestOut;
    if (!runGitCapture(clientDir,
                       {QStringLiteral("show"),
                        tagCommit +
                            QStringLiteral(":.forkmesh/releases/latest/release.json")},
                       &manifestOut, nullptr)) {
        logSystem(QStringLiteral("Auto-update: release %1 publishes no "
                                 "artifact manifest; building from source.")
                      .arg(tag));
        return false;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(manifestOut).object();
    static const QRegularExpression sha256Re(
        QStringLiteral("\\A[0-9a-f]{64}\\z"));
    QString assetHash;
    QString assetName;
    const QJsonArray assets =
        manifest.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        if (asset.value(QStringLiteral("os")).toString().trimmed() != wantOs)
            continue;
        if (asset.value(QStringLiteral("arch")).toString().trimmed() !=
            wantArch)
            continue;
        const QString hash = asset.value(QStringLiteral("blob_sha256"))
                                 .toString()
                                 .trimmed()
                                 .toLower();
        if (!sha256Re.match(hash).hasMatch())
            continue;
        assetHash = hash;
        assetName = asset.value(QStringLiteral("name")).toString();
        break;
    }
    const QString manifestRepo =
        manifest.value(QStringLiteral("repo")).toString().trimmed();
    const int slash = manifestRepo.indexOf(QLatin1Char('/'));
    if (assetHash.isEmpty() || slash <= 0) {
        logSystem(QStringLiteral("Auto-update: release %1 has no prebuilt "
                                 "artifact for %2/%3; building from source.")
                      .arg(tag, wantOs, wantArch));
        return false;
    }




    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly || repo.mirrorPath.trimmed().isEmpty())
            continue;
        const QString identity =
            repoSegment(repo.owner, QStringLiteral("owner")) +
            QLatin1Char('/') +
            repoSegment(repo.name, QStringLiteral("repository"));
        if (identity.compare(manifestRepo, Qt::CaseInsensitive) != 0)
            continue;
        const QString blob = mirrorReleaseBlobPath(repo.mirrorPath, assetHash);
        if (QFile::exists(blob) && fileSha256Matches(blob, assetHash)) {
            logSystem(QStringLiteral("Auto-update: installing release %1 from "
                                     "this node's own artifact store (%2).")
                          .arg(tag, assetName));
            installPrebuiltAndRelaunch(blob, tag);
            return true;
        }
    }



    if (!m_networkAccess)
        return false;
    const QString owner = manifestRepo.left(slash);
    const QString name = manifestRepo.mid(slash + 1);
    const QString staging =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("forkmesh-update-") + assetHash.left(12));
    auto tmp = std::make_shared<QFile>(staging + QStringLiteral(".part"));
    if (!tmp->open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    auto hasher =
        std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/repo/%1/%2/releases/blob/sha256/%3")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(owner)),
                         QString::fromUtf8(QUrl::toPercentEncoding(name)),
                         assetHash));
    logSystem(QStringLiteral(
                  "Auto-update: downloading the release %1 artifact (%2)...")
                  .arg(tag, assetName));
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::readyRead, this, [reply, tmp, hasher] {
        const QByteArray chunk = reply->readAll();
        tmp->write(chunk);
        hasher->addData(chunk);
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, tmp, hasher, staging, assetHash, tag] {
                const QByteArray rest = reply->readAll();
                tmp->write(rest);
                hasher->addData(rest);
                const bool ok = reply->error() == QNetworkReply::NoError;
                const QString netError = reply->errorString();
                reply->deleteLater();
                tmp->close();
                const QString actual =
                    QString::fromLatin1(hasher->result().toHex());
                if (!ok || actual != assetHash) {
                    QFile::remove(tmp->fileName());
                    logSystem(
                        QStringLiteral(
                            "Auto-update: artifact download for %1 failed "
                            "(%2); falling back to a source build.")
                            .arg(tag, ok ? QStringLiteral("checksum mismatch")
                                         : netError));
                    updateRebuildRestart();
                    return;
                }
                QFile::remove(staging);
                if (!QFile::rename(tmp->fileName(), staging)) {
                    QFile::remove(tmp->fileName());
                    updateRebuildRestart();
                    return;
                }
                installPrebuiltAndRelaunch(staging, tag);
            });
    return true;
}

void MainWindow::installPrebuiltAndRelaunch(const QString &artifactPath,
                                            const QString &tag)
{


    const QString staged =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("forkmesh-update-staged"));
    if (staged != artifactPath) {
        QFile::remove(staged);
        if (!QFile::copy(artifactPath, staged)) {
            logSystem(QStringLiteral("Auto-update: could not stage the %1 "
                                     "artifact; falling back to a source "
                                     "build.")
                          .arg(tag));
            updateRebuildRestart();
            return;
        }
    }
    QFile::setPermissions(staged, QFile::ReadOwner | QFile::WriteOwner |
                                      QFile::ExeOwner | QFile::ReadGroup |
                                      QFile::ExeGroup | QFile::ReadOther |
                                      QFile::ExeOther);
    logRestart(QStringLiteral("prebuilt update to %1 staged").arg(tag));



    m_updateAsUser.clear();
    installAndRelaunch(staged, QCoreApplication::applicationFilePath());
}

void MainWindow::updateRebuildRestart()
{
    beginRestartLog();
    showUpdateLog();
    logRestart(QStringLiteral("update, rebuild & restart started"));
    logSystem(QStringLiteral("=== Update, rebuild & restart started ==="));
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;


    const QString user = invokingNonRootUser();
    const QString home = user.isEmpty() ? QString() : homeForUser(user);
    const QString clientDir =
        user.isEmpty() ? updateClientDir() : clientDirUnderHome(home);
    const QString relaunchPath =
        user.isEmpty() ? QCoreApplication::applicationFilePath()
                       : (home + QStringLiteral("/.local/bin/forkmesh"));

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

    const QString repoDir = QFileInfo(clientDir).absolutePath();
    const bool haveCheckout = QDir(repoDir).exists(QStringLiteral(".git"));




    auto recloneFromMirror = [this, installUrl, repoDir, clientDir, user,
                              relaunchPath] {
        const QString parent = QFileInfo(repoDir).absolutePath();
        runUpdateStepUser("mkdir", {"-p", parent}, QDir::tempPath(),
                          [this, installUrl, repoDir, parent, clientDir, user,
                           relaunchPath] {

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

        runUpdateStepUser("git", {"-C", repoDir, "remote", "set-url", "origin",
                                  installUrl},
                          repoDir, [this, repoDir, clientDir, user, relaunchPath,
                                    installUrl, recloneFromMirror] {
            runUpdateStepUser("git", {"-C", repoDir, "pull", "--ff-only"}, repoDir,
                              [this, clientDir, user, relaunchPath] {
                buildAndRelaunch(clientDir, user, relaunchPath);
            }, [this, installUrl, recloneFromMirror] {


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
