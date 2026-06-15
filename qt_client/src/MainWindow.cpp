#include "MainWindow.h"

#include "MessageRow.h"
#include "RepoHost.h"
#include "ServerNode.h"
#include "SettingsDialog.h"
#include "Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifndef FORKMESH_VERSION
#define FORKMESH_VERSION "dev"
#endif
#ifndef FORKMESH_SOURCE_DIR
#define FORKMESH_SOURCE_DIR ""
#endif

namespace {

const QString kRepoUrl = QStringLiteral("https://github.com/forkmesh/forkmesh.git");
const QString kDisplayNameSetting = QStringLiteral("profile/displayName");
const QString kHandleSetting = QStringLiteral("profile/handle");
const QString kBchSetting = QStringLiteral("profile/bch");
const QString kAvatarSetting = QStringLiteral("profile/avatarPng");
const QString kServerUrlSetting = QStringLiteral("server/url");
const QString kLocalServerUrl =
    QStringLiteral("ws://127.0.0.1:8787/api/repo/mainnode/forkmesh/rooms/general/ws");
const QString kWorkersDevServerUrl =
    QStringLiteral("wss://forkmesh-relay.forkmesh.workers.dev/api/repo/mainnode/forkmesh/rooms/general/ws");
const QString kDefaultServerUrl =
    QStringLiteral("wss://forkmesh.com/api/repo/mainnode/forkmesh/rooms/general/ws");
const QString kRoomNameSetting = QStringLiteral("server/room");
const QString kPassphraseSetting = QStringLiteral("server/passphrase");
const QString kDefaultRoomName = QStringLiteral("general");
const QString kDefaultPassphrase = QStringLiteral("forkmesh-public-room");
const QString kRepositoriesArray = QStringLiteral("repositories/items");
constexpr int kNetworkLogLimit = 2000;

// Directory holding client/CMakeLists.txt to update from: the build-time
// checkout when it still exists, otherwise a persistent clone managed by the
// app in its data directory (used when the binary was installed without a
// checkout, e.g. via install.sh).
QString updateClientDir()
{
    const QString baked = QStringLiteral(FORKMESH_SOURCE_DIR);
    if (!baked.isEmpty() && QDir(baked).exists("CMakeLists.txt"))
        return baked;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/src/qt_client";
}

QString builtExecutablePath(const QString &buildDir)
{
#ifdef Q_OS_MACOS
    const QString appExecutable = buildDir + "/ForkMesh.app/Contents/MacOS/ForkMesh";
    if (QFileInfo::exists(appExecutable))
        return appExecutable;
#endif
    return buildDir + "/forkmesh";
}

#ifdef Q_OS_MACOS
QString brewPrefix(const QString &formula)
{
    const QString brew = QStandardPaths::findExecutable("brew");
    if (brew.isEmpty())
        return {};

    QProcess process;
    process.start(brew, {"--prefix", formula});
    if (!process.waitForFinished(3000) || process.exitCode() != 0)
        return {};
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}
#endif

QStringList cmakeConfigureArgs(const QString &clientDir, const QString &buildDir)
{
    QStringList args{"-S", clientDir, "-B", buildDir, "-DCMAKE_BUILD_TYPE=Release",
                     "-DFORKMESH_BUILD_TESTS=OFF"};
#ifdef Q_OS_MACOS
    const QString qtPrefix = brewPrefix("qt");
    if (!qtPrefix.isEmpty())
        args << "-DCMAKE_PREFIX_PATH=" + qtPrefix;
    const QString opensslPrefix = brewPrefix("openssl@3");
    if (!opensslPrefix.isEmpty())
        args << "-DOPENSSL_ROOT_DIR=" + opensslPrefix;
#endif
    return args;
}

const QString kDmPrefix = QStringLiteral("@");

bool isDirectConversation(const QString &conversation)
{
    return conversation.startsWith(kDmPrefix);
}

QString dmKey(const QString &peerId)
{
    return kDmPrefix + peerId;
}

QString dmPeerId(const QString &conversation)
{
    return conversation.mid(1);
}

QString repoSegment(QString value, const QString &fallback)
{
    value = value.trimmed().toLower();
    QString out;
    bool lastWasDash = false;
    for (const QChar ch : value) {
        const bool ok = ch.isLetterOrNumber() || ch == '_' || ch == '-';
        if (ok) {
            out.append(ch);
            lastWasDash = false;
        } else if (!lastWasDash) {
            out.append('-');
            lastWasDash = true;
        }
    }
    while (out.startsWith('-'))
        out.remove(0, 1);
    while (out.endsWith('-'))
        out.chop(1);
    if (out.isEmpty())
        out = fallback;
    return out.left(48);
}

QString repoNameFromUrl(QString url)
{
    url = url.trimmed();
    url.replace('\\', '/');
    QString name = url.section('/', -1);
    if (name.endsWith(".git"))
        name.chop(4);
    return repoSegment(name, QStringLiteral("repository"));
}

QString formatRepoDate(qint64 timestampMs)
{
    if (timestampMs <= 0)
        return QStringLiteral("never");
    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString("yyyy-MM-dd hh:mm");
}

QString defaultDisplayName(const ForkMeshIdentity &identity)
{
    const QString suffix = identity.shortPublicKey().left(8);
    return suffix.isEmpty() ? QStringLiteral("forkmesh-node")
                            : QStringLiteral("node-") + suffix;
}

void saveDisplayName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty())
        QSettings().setValue(kDisplayNameSetting, trimmed);
}

QIcon statusDotIcon(bool online)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(online ? QColor("#22c55e") : QColor("#6b7280"));
    painter.drawEllipse(1, 1, 10, 10);
    return QIcon(pixmap);
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("ForkMesh");
    resize(1060, 700);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    m_trayIcon->setToolTip("ForkMesh");
    if (QSystemTrayIcon::isSystemTrayAvailable())
        m_trayIcon->show();

    m_networkAccess = new QNetworkAccessManager(this);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildSetupPage());
    m_stack->addWidget(buildChatPage());
    setCentralWidget(m_stack);
    loadRepositories();
    refreshRepositoryList();

    m_typingStopTimer = new QTimer(this);
    m_typingStopTimer->setSingleShot(true);
    connect(m_typingStopTimer, &QTimer::timeout, this, [this] {
        sendTypingState(false);
    });

    if (!m_profileIdentity.load()) {
        m_setupError->setText(m_profileIdentity.errorString());
        m_setupError->show();
    } else if (m_pubkeyLabel) {
        m_pubkeyLabel->setText("Ed25519 public key: " +
                               m_profileIdentity.shortPublicKey());
        m_pubkeyLabel->setToolTip(m_profileIdentity.publicKey());
        if (m_nameEdit->text().trimmed().isEmpty())
            m_nameEdit->setText(defaultDisplayName(m_profileIdentity));
        QTimer::singleShot(0, this, &MainWindow::startSession);
    }
    updateHomeStats();
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
        "Preserve code, mirror repositories, and chat through a mainnode");
    subtitle->setObjectName("appSubtitle");
    subtitle->setAlignment(Qt::AlignHCenter);
    auto *versionLabel = new QLabel("v" FORKMESH_VERSION);
    versionLabel->setObjectName("versionLabel");
    versionLabel->setAlignment(Qt::AlignHCenter);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText("Display name");
    m_nameEdit->setMaxLength(32);
    m_nameEdit->setText(QSettings().value(kDisplayNameSetting).toString());
    m_handleEdit = new QLineEdit;
    m_handleEdit->setPlaceholderText("Handle (alice or node.example:alice)");
    m_handleEdit->setMaxLength(80);
    m_handleEdit->setText(QSettings().value(kHandleSetting).toString());
    m_bchEdit = new QLineEdit;
    m_bchEdit->setPlaceholderText("Bitcoin Cash address for donations (optional)");
    m_bchEdit->setMaxLength(160);
    m_bchEdit->setText(QSettings().value(kBchSetting).toString());
    m_pubkeyLabel = new QLabel("Ed25519 public key: generating...");
    m_pubkeyLabel->setObjectName("modeHint");
    m_pubkeyLabel->setWordWrap(true);

    m_serverUrlEdit = new QLineEdit;
    m_serverUrlEdit->setPlaceholderText(kDefaultServerUrl);
    m_serverUrlEdit->setMaxLength(2048);
    const QString savedServerUrl = QSettings().value(kServerUrlSetting).toString().trimmed();
    m_serverUrlEdit->setText(
        savedServerUrl.isEmpty() || savedServerUrl == kLocalServerUrl ||
                savedServerUrl == kWorkersDevServerUrl
            ? kDefaultServerUrl
            : savedServerUrl);
    m_roomNameEdit = new QLineEdit;
    m_roomNameEdit->setPlaceholderText("Default repository room");
    m_roomNameEdit->setMaxLength(80);
    m_roomNameEdit->setText(QSettings().value(kRoomNameSetting, kDefaultRoomName).toString());
    m_passphraseEdit = new QLineEdit;
    m_passphraseEdit->setPlaceholderText("Mainnode room passphrase");
    m_passphraseEdit->setMaxLength(256);
    m_passphraseEdit->setEchoMode(QLineEdit::Password);
    m_passphraseEdit->setText(
        QSettings().value(kPassphraseSetting, kDefaultPassphrase).toString());

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

    m_updateButton = new QPushButton("\xE2\x9F\xB3 Quick update");
    m_updateButton->setObjectName("ghostButton");
    m_updateButton->setCursor(Qt::PointingHandCursor);
    m_updateButton->setToolTip("Pull the latest version, rebuild, and relaunch");
    m_updateStatus = new QLabel;
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
    cardLayout->addWidget(m_handleEdit);
    cardLayout->addWidget(m_bchEdit);
    cardLayout->addWidget(m_pubkeyLabel);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(m_serverUrlEdit);
    cardLayout->addWidget(m_roomNameEdit);
    cardLayout->addWidget(m_passphraseEdit);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(mainnodeHint);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(m_setupError);
    cardLayout->addSpacing(10);
    cardLayout->addWidget(startButton);
    cardLayout->addWidget(m_updateButton, 0, Qt::AlignHCenter);
    cardLayout->addWidget(m_updateStatus);

    auto *layout = new QVBoxLayout(page);
    layout->addStretch();
    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch();

    connect(startButton, &QPushButton::clicked, this, &MainWindow::startSession);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, &MainWindow::startSession);
    connect(m_nameEdit, &QLineEdit::textEdited, this, [](const QString &name) {
        saveDisplayName(name);
    });
    connect(m_handleEdit, &QLineEdit::textEdited, this, [](const QString &handle) {
        QSettings().setValue(kHandleSetting, handle.trimmed());
    });
    connect(m_bchEdit, &QLineEdit::textEdited, this, [](const QString &address) {
        QSettings().setValue(kBchSetting, address.trimmed());
    });
    connect(m_serverUrlEdit, &QLineEdit::textEdited, this, [](const QString &url) {
        QSettings().setValue(kServerUrlSetting, url.trimmed());
    });
    connect(m_roomNameEdit, &QLineEdit::textEdited, this, [](const QString &room) {
        QSettings().setValue(kRoomNameSetting, room.trimmed());
    });
    // Persisted so the room can be rejoined automatically after a restart.
    connect(m_passphraseEdit, &QLineEdit::textEdited, this,
            [](const QString &passphrase) {
                QSettings().setValue(kPassphraseSetting, passphrase);
            });
    connect(m_updateButton, &QPushButton::clicked, this, &MainWindow::runQuickUpdate);

    return page;
}

void MainWindow::startSession()
{
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        name = defaultDisplayName(m_profileIdentity);
        m_nameEdit->setText(name);
        saveDisplayName(name);
    }
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        m_setupError->setText(m_profileIdentity.errorString());
        m_setupError->show();
        return;
    }
    if (m_serverUrlEdit->text().trimmed().isEmpty())
        m_serverUrlEdit->setText(kDefaultServerUrl);
    if (m_roomNameEdit->text().trimmed().isEmpty())
        m_roomNameEdit->setText(kDefaultRoomName);
    if (m_passphraseEdit->text().isEmpty())
        m_passphraseEdit->setText(kDefaultPassphrase);
    m_setupError->hide();
    m_userName = name;
    persistProfile();
    m_userAvatar = QSettings().value(kAvatarSetting).toByteArray();

    // Reset chat state.
    m_channels.clear();
    m_currentConversation.clear();
    m_history.clear();
    m_visibleRows.clear();
    m_reactions.clear();
    m_avatars.clear();
    m_dmNames.clear();
    m_openDms.clear();
    m_unread.clear();
    m_typing.clear();
    m_typingConversation.clear();
    m_typingStopTimer->stop();
    m_channelList->clear();
    m_dmList->clear();
    m_memberList->clear();
    rebuildConversationView();

    QSettings().setValue(kServerUrlSetting, m_serverUrlEdit->text().trimmed());
    QSettings().setValue(kRoomNameSetting, m_roomNameEdit->text().trimmed());
    QSettings().setValue(kPassphraseSetting, m_passphraseEdit->text());
    const QUrl url(m_serverUrlEdit->text().trimmed());
    auto *server = new ServerNode(name, url, m_roomNameEdit->text().trimmed(),
                                  m_passphraseEdit->text(), this);
    attachBackend(server);
    if (!server->start())
        return;

    if (m_backend) {
        if (!m_userAvatar.isEmpty())
            m_backend->setAvatar(m_userAvatar);
        for (const RepositoryRecord &repo : std::as_const(m_repositories))
            m_backend->addChannel(repositoryChannel(repo));
        m_encryptionLabel->setText("\xF0\x9F\x94\x92 Mainnode encrypted");
        logSystem("Encryption: client-side AES-256-GCM mainnode room encryption.");
        const QJsonObject signedProfile =
            m_profileIdentity.signedProfile(m_nameEdit->text(),
                                            m_handleEdit->text(),
                                            m_bchEdit->text());
        const QString profileBytes = QString::fromUtf8(
            QJsonDocument(signedProfile).toJson(QJsonDocument::Compact));
        logSystem("Identity: signed profile for " +
                  m_profileIdentity.shortPublicKey() + " (" +
                  QString::number(profileBytes.toUtf8().size()) + " bytes).");
        m_stack->setCurrentIndex(1);
        showSection(0); // land on the Home overview after connecting
        // Serve already-mirrored repos live to the web for this session.
        startRepoHosts();
    }
}

void MainWindow::persistProfile()
{
    QString handle = m_handleEdit->text().trimmed();
    if (handle.isEmpty())
        handle = repoSegment(m_nameEdit->text(), QStringLiteral("node"));
    m_handleEdit->setText(handle);

    saveDisplayName(m_nameEdit->text());
    QSettings settings;
    settings.setValue(kHandleSetting, handle);
    settings.setValue(kBchSetting, m_bchEdit->text().trimmed());
}

// --------------------------------------------------------------- quick update

void MainWindow::setUpdateStatus(const QString &status, bool isError)
{
    m_updateStatus->setStyleSheet(isError ? "color:#ff6b6b; background:transparent;"
                                          : "color:#9ca3af; background:transparent;");
    m_updateStatus->setText(status);
    m_updateStatus->show();
}

void MainWindow::runUpdateStep(const QString &program, const QStringList &arguments,
                               const QString &workingDir,
                               std::function<void()> onSuccess)
{
    auto *process = new QProcess(this);
    process->setWorkingDirectory(workingDir);
    connect(process, &QProcess::finished, this,
            [this, process, onSuccess](int exitCode, QProcess::ExitStatus) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                if (exitCode != 0) {
                    setUpdateStatus("Update failed: " + errors.right(300), true);
                    m_updateButton->setEnabled(true);
                    return;
                }
                onSuccess();
            });
    connect(process, &QProcess::errorOccurred, this, [this, process] {
        setUpdateStatus("Update failed: could not run " + process->program(), true);
        process->deleteLater();
        m_updateButton->setEnabled(true);
    });
    process->start(program, arguments);
}

void MainWindow::runQuickUpdate()
{
    saveDisplayName(m_nameEdit->text());
    m_updateButton->setEnabled(false);
    const QString clientDir = updateClientDir();

    if (QDir(clientDir).exists("CMakeLists.txt")) {
        setUpdateStatus("Pulling the latest version...");
        runUpdateStep("git", {"pull", "--ff-only"}, clientDir,
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

void MainWindow::buildAndRelaunch(const QString &clientDir)
{
    const QString buildDir = clientDir + "/build";
    setUpdateStatus("Configuring...");
    runUpdateStep("cmake", cmakeConfigureArgs(clientDir, buildDir),
                  clientDir, [this, buildDir] {
        setUpdateStatus("Rebuilding...");
        runUpdateStep("cmake",
                      {"--build", buildDir, "-j",
                       QString::number(QThread::idealThreadCount())},
                      buildDir, [this, buildDir] {
            // When the running binary lives elsewhere (e.g. ~/.local/bin),
            // install the fresh build over it; the running inode stays valid.
            const QString built = builtExecutablePath(buildDir);
            const QString appPath = QCoreApplication::applicationFilePath();
            if (QFileInfo(built).canonicalFilePath() !=
                QFileInfo(appPath).canonicalFilePath()) {
                QFile::remove(appPath);
                if (!QFile::copy(built, appPath)) {
                    setUpdateStatus("Update failed: could not replace " + appPath, true);
                    m_updateButton->setEnabled(true);
                    return;
                }
                QFile::setPermissions(appPath,
                                      QFile::ReadOwner | QFile::WriteOwner |
                                      QFile::ExeOwner | QFile::ReadGroup |
                                      QFile::ExeGroup | QFile::ReadOther |
                                      QFile::ExeOther);
            }
            setUpdateStatus("Relaunching...");
            QProcess::startDetached(appPath, {});
            QCoreApplication::quit();
        });
    });
}

// ----------------------------------------------------------------- chat page

QWidget *MainWindow::buildChatPage()
{
    auto *page = new QWidget;

    // Section stack switched by the left navigation rail: Home / Repos / Chat.
    m_sectionStack = new QStackedWidget;
    m_sectionStack->addWidget(buildHomeSection());   // 0 Home
    m_sectionStack->addWidget(buildReposSection());  // 1 Repos
    m_sectionStack->addWidget(buildChatSection());   // 2 Chat

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildNavRail());
    layout->addWidget(m_sectionStack, 1);
    return page;
}

QWidget *MainWindow::buildNavRail()
{
    auto *rail = new QWidget;
    rail->setObjectName("navRail");
    rail->setFixedWidth(86);

    auto *logo = new QLabel("<span style='color:#22c55e'>F</span>M");
    logo->setObjectName("navLogo");
    logo->setAlignment(Qt::AlignHCenter);

    auto makeNavButton = [](const QString &glyph, const QString &text) {
        auto *button = new QPushButton(glyph + "\n" + text);
        button->setObjectName("navButton");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(60);
        return button;
    };
    auto *homeButton = makeNavButton("\xF0\x9F\x8F\xA0", "Home");
    auto *reposButton = makeNavButton("\xF0\x9F\x93\xA6", "Repos");
    auto *chatButton = makeNavButton("\xF0\x9F\x92\xAC", "Chat");
    homeButton->setChecked(true);

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    m_navGroup->addButton(homeButton, 0);
    m_navGroup->addButton(reposButton, 1);
    m_navGroup->addButton(chatButton, 2);
    connect(m_navGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_sectionStack->setCurrentIndex(id);
        if (id == 0)
            updateHomeStats();
    });

    auto *layout = new QVBoxLayout(rail);
    layout->setContentsMargins(10, 16, 10, 16);
    layout->setSpacing(8);
    layout->addWidget(logo);
    layout->addSpacing(10);
    layout->addWidget(homeButton);
    layout->addWidget(reposButton);
    layout->addWidget(chatButton);
    layout->addStretch();
    return rail;
}

void MainWindow::showSection(int index)
{
    if (m_navGroup && m_navGroup->button(index))
        m_navGroup->button(index)->setChecked(true);
    if (m_sectionStack)
        m_sectionStack->setCurrentIndex(index);
    if (index == 0)
        updateHomeStats();
}

QWidget *MainWindow::buildHomeSection()
{
    auto *page = new QWidget;

    auto *card = new QWidget;
    card->setObjectName("homeCard");
    card->setFixedWidth(460);

    auto *title = new QLabel("<span style='color:#22c55e'>Fork</span>Mesh");
    title->setObjectName("homeTitle");
    auto *subtitle = new QLabel(
        "Preserve code, mirror repositories, and chat through a mainnode");
    subtitle->setObjectName("homeStat");
    subtitle->setWordWrap(true);

    m_homeName = new QLabel;
    m_homeName->setObjectName("homeName");
    m_homeStatus = new QLabel("\xE2\x97\x8F offline");
    m_homeStatus->setObjectName("homeStat");
    m_homeStatus->setWordWrap(true);
    m_homePubkey = new QLabel;
    m_homePubkey->setObjectName("homeStat");
    m_homePubkey->setWordWrap(true);
    m_homeStats = new QLabel;
    m_homeStats->setObjectName("homeStat");

    auto *nodesLabel = new QLabel("NETWORK NODES");
    nodesLabel->setObjectName("sectionLabel");
    m_homeNodes = new QLabel("No nodes connected yet.");
    m_homeNodes->setObjectName("homeStat");
    m_homeNodeList = new QListWidget;
    m_homeNodeList->setSelectionMode(QAbstractItemView::NoSelection);
    m_homeNodeList->setFocusPolicy(Qt::NoFocus);
    m_homeNodeList->setMinimumHeight(120);
    m_homeNodeList->setToolTip("Live connection status of nodes in this room");

    auto *addRepoButton = new QPushButton("+ Add repository");
    addRepoButton->setObjectName("primaryButton");
    addRepoButton->setCursor(Qt::PointingHandCursor);
    connect(addRepoButton, &QPushButton::clicked, this,
            &MainWindow::promptAddRepository);
    auto *settingsButton = new QPushButton("Settings");
    settingsButton->setCursor(Qt::PointingHandCursor);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->addWidget(addRepoButton);
    actionRow->addWidget(settingsButton);
    actionRow->addStretch();

    auto *versionLabel = new QLabel("v" FORKMESH_VERSION);
    versionLabel->setObjectName("versionLabel");

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 28, 28, 28);
    cardLayout->setSpacing(8);
    cardLayout->addWidget(title);
    cardLayout->addWidget(subtitle);
    cardLayout->addSpacing(10);
    cardLayout->addWidget(m_homeName);
    cardLayout->addWidget(m_homeStatus);
    cardLayout->addWidget(m_homePubkey);
    cardLayout->addSpacing(4);
    cardLayout->addWidget(m_homeStats);
    cardLayout->addSpacing(12);
    cardLayout->addWidget(nodesLabel);
    cardLayout->addWidget(m_homeNodes);
    cardLayout->addWidget(m_homeNodeList);
    cardLayout->addSpacing(16);
    cardLayout->addLayout(actionRow);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(versionLabel);

    auto *layout = new QVBoxLayout(page);
    layout->addStretch();
    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch();
    return page;
}

QWidget *MainWindow::buildReposSection()
{
    auto *page = new QWidget;
    page->setObjectName("sidebar"); // reuse list/label styling

    auto *heading = new QLabel("Repositories");
    heading->setObjectName("channelTitle");
    auto *subtitle = new QLabel(
        "Pick a local Git repository to mirror and publish. Only signed "
        "metadata is shared \xE2\x80\x94 the .git data stays on this machine.");
    subtitle->setObjectName("statusLine");
    subtitle->setWordWrap(true);

    auto *reposLabel = new QLabel("MIRRORED REPOSITORIES");
    reposLabel->setObjectName("sectionLabel");
    m_repoList = new QListWidget;
    m_repoList->setToolTip("Repositories this node is preserving locally");

    // Live web status for the selected repository: a green "online" indicator
    // and a clickable link to browse it on the website once it is published.
    m_repoWebLink = new QLabel("Select a repository to see its web status.");
    m_repoWebLink->setObjectName("statusLine");
    m_repoWebLink->setWordWrap(true);
    m_repoWebLink->setOpenExternalLinks(true);
    m_repoWebLink->setTextInteractionFlags(Qt::TextBrowserInteraction);

    auto *addRepoButton = new QPushButton("+ Add");
    addRepoButton->setObjectName("ghostButton");
    addRepoButton->setCursor(Qt::PointingHandCursor);
    m_syncRepoButton = new QPushButton("Sync");
    m_syncRepoButton->setObjectName("ghostButton");
    m_syncRepoButton->setCursor(Qt::PointingHandCursor);
    m_publishRepoButton = new QPushButton("Publish");
    m_publishRepoButton->setObjectName("ghostButton");
    m_publishRepoButton->setCursor(Qt::PointingHandCursor);
    auto *repoButtonRow = new QHBoxLayout;
    repoButtonRow->setContentsMargins(0, 0, 0, 0);
    repoButtonRow->addWidget(addRepoButton);
    repoButtonRow->addWidget(m_syncRepoButton);
    repoButtonRow->addWidget(m_publishRepoButton);
    repoButtonRow->addStretch();

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(8);
    layout->addWidget(heading);
    layout->addWidget(subtitle);
    layout->addSpacing(8);
    layout->addWidget(reposLabel);
    layout->addWidget(m_repoList, 1);
    layout->addWidget(m_repoWebLink);
    layout->addLayout(repoButtonRow);

    connect(m_repoList, &QListWidget::currentRowChanged, this,
            [this](int) { updateRepoWebLink(); });
    connect(m_repoList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                if (!item)
                    return;
                const int index = item->data(Qt::UserRole).toInt();
                if (index >= 0 && index < m_repositories.size()) {
                    switchConversation(repositoryChannel(m_repositories.at(index)));
                    showSection(2); // jump to the chat for this repo
                }
            });
    connect(addRepoButton, &QPushButton::clicked, this,
            &MainWindow::promptAddRepository);
    connect(m_syncRepoButton, &QPushButton::clicked, this,
            &MainWindow::syncSelectedRepository);
    connect(m_publishRepoButton, &QPushButton::clicked, this,
            &MainWindow::publishSelectedRepository);
    return page;
}

QWidget *MainWindow::buildChatSection()
{
    auto *page = new QWidget;

    // Sidebar
    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(280);

    auto *workspace = new QLabel("<span style='color:#22c55e'>Fork</span>Mesh");
    workspace->setObjectName("workspaceName");
    m_statusLine = new QLabel;
    m_statusLine->setObjectName("statusLine");
    m_statusLine->setWordWrap(true);

    auto *channelsLabel = new QLabel("REPOSITORY CHATS");
    channelsLabel->setObjectName("sectionLabel");
    m_channelList = new QListWidget;
    auto *addChannelButton = new QPushButton("+ Add chat");
    addChannelButton->setObjectName("ghostButton");
    addChannelButton->setCursor(Qt::PointingHandCursor);

    auto *dmsLabel = new QLabel("DIRECT MESSAGES");
    dmsLabel->setObjectName("sectionLabel");
    m_dmList = new QListWidget;

    auto *membersLabel = new QLabel("MEMBERS");
    membersLabel->setObjectName("sectionLabel");
    membersLabel->setToolTip("Click a member to start a direct chat");
    m_memberList = new QListWidget;
    m_memberList->setSelectionMode(QAbstractItemView::NoSelection);
    m_memberList->setFocusPolicy(Qt::NoFocus);
    m_memberList->setCursor(Qt::PointingHandCursor);
    m_memberList->setToolTip("Click a member to start a direct chat");

    auto *chatVersionLabel = new QLabel("v" FORKMESH_VERSION);
    chatVersionLabel->setObjectName("versionLabel");

    auto *badgeRow = new QHBoxLayout;
    badgeRow->setContentsMargins(0, 0, 0, 0);
    badgeRow->addWidget(workspace);
    badgeRow->addStretch();

    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(14, 16, 14, 12);
    sidebarLayout->setSpacing(6);
    sidebarLayout->addLayout(badgeRow);
    sidebarLayout->addWidget(m_statusLine);
    sidebarLayout->addWidget(channelsLabel);
    sidebarLayout->addWidget(m_channelList, 2);
    sidebarLayout->addWidget(addChannelButton);
    sidebarLayout->addWidget(dmsLabel);
    sidebarLayout->addWidget(m_dmList, 1);
    sidebarLayout->addWidget(membersLabel);
    sidebarLayout->addWidget(m_memberList, 2);
    auto *footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->addStretch();
    footerRow->addWidget(chatVersionLabel);
    sidebarLayout->addLayout(footerRow);

    // Main column
    auto *header = new QWidget;
    header->setObjectName("chatHeader");
    m_channelTitle = new QLabel("#general");
    m_channelTitle->setObjectName("channelTitle");
    m_encryptionLabel = new QLabel;
    m_encryptionLabel->setObjectName("encryptionLabel");
    auto *settingsButton = new QPushButton("\xE2\x9A\x99");
    settingsButton->setObjectName("iconButton");
    settingsButton->setCursor(Qt::PointingHandCursor);
    settingsButton->setToolTip("Settings & network log");
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 12, 18, 12);
    headerLayout->addWidget(m_channelTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(m_encryptionLabel);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(settingsButton);

    // Firewall banner: hidden until the backend reports the host firewall is
    // blocking ForkMesh, then offers a one-click "Allow through firewall".
    m_firewallBanner = new QWidget;
    m_firewallBanner->setObjectName("firewallBanner");
    m_firewallBannerLabel = new QLabel;
    m_firewallBannerLabel->setObjectName("firewallBannerLabel");
    m_firewallBannerLabel->setWordWrap(true);
    m_firewallAllowButton = new QPushButton("Allow through firewall");
    m_firewallAllowButton->setObjectName("primaryButton");
    m_firewallAllowButton->setCursor(Qt::PointingHandCursor);
    auto *firewallDismiss = new QPushButton("\xE2\x9C\x95");
    firewallDismiss->setObjectName("ghostButton");
    firewallDismiss->setCursor(Qt::PointingHandCursor);
    firewallDismiss->setToolTip("Dismiss");
    auto *firewallLayout = new QHBoxLayout(m_firewallBanner);
    firewallLayout->setContentsMargins(16, 10, 12, 10);
    firewallLayout->setSpacing(10);
    firewallLayout->addWidget(m_firewallBannerLabel, 1);
    firewallLayout->addWidget(m_firewallAllowButton);
    firewallLayout->addWidget(firewallDismiss);
    m_firewallBanner->hide();
    connect(m_firewallAllowButton, &QPushButton::clicked, this,
            &MainWindow::allowFirewall);
    connect(firewallDismiss, &QPushButton::clicked, m_firewallBanner,
            &QWidget::hide);

    // Scrollable column of message-row widgets (supports avatars, inline
    // images, animated GIFs, file chips, and reaction bars).
    m_messageScroll = new QScrollArea;
    m_messageScroll->setObjectName("messageView");
    m_messageScroll->setWidgetResizable(true);
    m_messageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageContainer = new QWidget;
    m_messageContainer->setObjectName("messageContainer");
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(4, 8, 4, 8);
    m_messageLayout->setSpacing(0);
    m_messageLayout->addStretch();
    m_messageScroll->setWidget(m_messageContainer);

    // Keep the newest message visible. A row added to the layout grows the
    // scroll range asynchronously, so we can't reliably scroll the instant we
    // insert it; instead, whenever the range grows while we're pinned to the
    // bottom, jump to the new maximum. The user scrolling up clears the pin so
    // we don't drag them back down while they read history.
    QScrollBar *vbar = m_messageScroll->verticalScrollBar();
    connect(vbar, &QScrollBar::rangeChanged, this, [this](int, int max) {
        if (m_stickToBottom)
            m_messageScroll->verticalScrollBar()->setValue(max);
    });
    connect(vbar, &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar *bar = m_messageScroll->verticalScrollBar();
        m_stickToBottom = value >= bar->maximum() - 4;
    });

    auto *composer = new QWidget;
    composer->setObjectName("composerBar");
    auto *attachButton = new QPushButton("\xF0\x9F\x93\x8E");
    attachButton->setObjectName("iconButton");
    attachButton->setCursor(Qt::PointingHandCursor);
    attachButton->setToolTip("Share a file (any type, including GIFs)");
    connect(attachButton, &QPushButton::clicked, this, &MainWindow::attachFile);
    m_messageInput = new QLineEdit;
    m_messageInput->setObjectName("messageInput");
    m_messageInput->setPlaceholderText("Message #general");
    m_messageInput->setMaxLength(16000);
    auto *sendButton = new QPushButton("Send");
    sendButton->setObjectName("primaryButton");
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(14, 10, 14, 12);
    composerLayout->setSpacing(8);
    composerLayout->addWidget(attachButton);
    composerLayout->addWidget(m_messageInput);
    composerLayout->addWidget(sendButton);

    m_typingLabel = new QLabel;
    m_typingLabel->setObjectName("typingLabel");
    m_typingLabel->setFixedHeight(20);
    m_typingLabel->setText(QString());

    auto *mainColumn = new QVBoxLayout;
    mainColumn->setContentsMargins(0, 0, 0, 0);
    mainColumn->setSpacing(0);
    mainColumn->addWidget(header);
    mainColumn->addWidget(m_firewallBanner);
    mainColumn->addWidget(m_messageScroll, 1);
    mainColumn->addWidget(m_typingLabel);
    mainColumn->addWidget(composer);

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sidebar);
    layout->addLayout(mainColumn, 1);

    connect(m_channelList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    switchConversation(item->data(Qt::UserRole).toString());
            });
    connect(m_dmList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    switchConversation(item->data(Qt::UserRole).toString());
            });
    connect(m_memberList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                const QString id = item->data(Qt::UserRole).toString();
                const bool self = item->data(Qt::UserRole + 2).toBool();
                if (!id.isEmpty() && !self)
                    openDirectChat(id, item->data(Qt::UserRole + 1).toString());
            });
    connect(addChannelButton, &QPushButton::clicked, this, &MainWindow::promptAddChannel);
    connect(m_messageInput, &QLineEdit::textEdited, this, &MainWindow::onComposerEdited);
    connect(m_messageInput, &QLineEdit::returnPressed, this, &MainWindow::sendCurrentMessage);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendCurrentMessage);

    return page;
}

void MainWindow::updateHomeStats()
{
    if (!m_homeName)
        return;
    const QString name = m_userName.isEmpty()
                             ? defaultDisplayName(m_profileIdentity)
                             : m_userName;
    m_homeName->setText(name);
    const QString key = m_profileIdentity.shortPublicKey();
    m_homePubkey->setText(key.isEmpty()
                              ? QStringLiteral("Ed25519 key: generating\xE2\x80\xA6")
                              : "Ed25519 key: " + key);
    const int repoCount = m_repositories.size();
    const int chatCount = m_channels.size();
    const int dmCount = m_openDms.size();
    m_homeStats->setText(
        QString::number(repoCount) +
        (repoCount == 1 ? " repository mirrored" : " repositories mirrored") +
        "  \xC2\xB7  " + QString::number(chatCount) +
        (chatCount == 1 ? " chat" : " chats") + "  \xC2\xB7  " +
        QString::number(dmCount) +
        (dmCount == 1 ? " direct message" : " direct messages"));
}

void MainWindow::attachBackend(ChatBackend *backend)
{
    if (m_backend)
        leaveSession();
    m_backend = backend;

    connect(backend, &ChatBackend::messageArrived, this, &MainWindow::onMessage);
    connect(backend, &ChatBackend::reactionChanged, this, &MainWindow::onReaction);
    connect(backend, &ChatBackend::messageEdited, this, &MainWindow::onMessageEdited);
    connect(backend, &ChatBackend::messageDeleted, this, &MainWindow::onMessageDeleted);
    connect(backend, &ChatBackend::avatarChanged, this, &MainWindow::onAvatar);
    connect(backend, &ChatBackend::typingChanged, this, &MainWindow::onTypingChanged);
    connect(backend, &ChatBackend::systemMessage, this, &MainWindow::logSystem);
    connect(backend, &ChatBackend::channelsChanged, this, &MainWindow::setChannels);
    connect(backend, &ChatBackend::rosterChanged, this, &MainWindow::setRoster);
    connect(backend, &ChatBackend::statusChanged, this, [this](const QString &status) {
        const QString summary = status.section(" · ", 0, 0);
        m_statusLine->setText(summary);
        if (m_homeStatus)
            m_homeStatus->setText("\xE2\x97\x8F " + summary);
        logSystem("Status: " + status);
    });
    connect(backend, &ChatBackend::firewallBlocking, this, &MainWindow::showFirewallBanner);
    connect(backend, &ChatBackend::firewallHealthy, this, &MainWindow::hideFirewallBanner);
    connect(backend, &ChatBackend::fatalError, this, [this](const QString &message) {
        leaveSession();
        m_setupError->setText(message);
        m_setupError->show();
    });
}

void MainWindow::leaveSession(const QString &)
{
    stopRepoHosts();
    if (m_backend) {
        m_backend->disconnect(this);
        m_backend->disconnect(m_statusLine);
        m_backend->shutdown();
        m_backend->deleteLater();
        m_backend = nullptr;
    }
    m_stack->setCurrentIndex(0);
    m_userName.clear();

    // Reset the Home overview's live status back to disconnected.
    if (m_homeStatus)
        m_homeStatus->setText("\xE2\x97\x8F offline");
    if (m_homeNodeList)
        m_homeNodeList->clear();
    if (m_homeNodes)
        m_homeNodes->setText("No nodes connected yet.");
}

// ------------------------------------------------------------------ firewall

void MainWindow::showFirewallBanner(const QString &displayCommand,
                                    const QString &privilegedCommand)
{
    m_firewallPrivilegedCommand = privilegedCommand;
    m_firewallBannerLabel->setText(
        "\xE2\x9A\xA0 A firewall on this computer may be blocking peers from "
        "connecting. Click to open ForkMesh's ports (asks for your password).");
    m_firewallBannerLabel->setToolTip(displayCommand);
    m_firewallAllowButton->setEnabled(true);
    m_firewallAllowButton->setText("Allow through firewall");
    m_firewallBanner->show();
}

void MainWindow::hideFirewallBanner()
{
    if (m_firewallBanner)
        m_firewallBanner->hide();
    if (m_firewallAllowButton)
        m_firewallAllowButton->show();
}

void MainWindow::allowFirewall()
{
    if (m_firewallPrivilegedCommand.isEmpty())
        return;
    m_firewallAllowButton->setEnabled(false);
    m_firewallAllowButton->setText("Allowing…");

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                if (exitCode == 0) {
                    // Ports are open immediately; existing sockets start
                    // receiving, so discovery recovers within a few seconds.
                    m_firewallBannerLabel->setText(
                        "\xE2\x9C\x94 Firewall opened. Peers should connect "
                        "within a few seconds.");
                    m_firewallAllowButton->hide();
                    logSystem("Firewall opened for ForkMesh's ports.");
                } else {
                    m_firewallBannerLabel->setText(
                        "Could not open the firewall" +
                        (errors.isEmpty() ? QString() : ": " + errors.right(200)) +
                        ". Run the command shown in the chat manually.");
                    m_firewallAllowButton->setEnabled(true);
                    m_firewallAllowButton->setText("Try again");
                }
            });
    connect(process, &QProcess::errorOccurred, this, [this, process] {
        process->deleteLater();
        m_firewallBannerLabel->setText(
            "Could not launch the privilege helper (pkexec). Run the command "
            "shown in the chat manually in a terminal.");
        m_firewallAllowButton->setEnabled(true);
        m_firewallAllowButton->setText("Try again");
    });
    // pkexec shows a graphical password prompt and runs the command as root.
    process->start("pkexec", {"sh", "-c", m_firewallPrivilegedCommand});
}

// -------------------------------------------------------------- diagnostics

void MainWindow::logSystem(const QString &text)
{
    const QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
    const QString line = time + "  " + text;
    m_networkLog.append(line);
    while (m_networkLog.size() > kNetworkLogLimit)
        m_networkLog.removeFirst();
    if (m_settingsDialog) {
        m_settingsDialog->appendLog(line);
    }
}

void MainWindow::notifyIfInactive(const QString &title, const QString &body)
{
    if (isActiveWindow())
        return;

    QApplication::alert(this, 0);
    if (!m_trayIcon || !QSystemTrayIcon::isSystemTrayAvailable())
        return;

    QString cleanBody = body.simplified();
    if (cleanBody.size() > 180)
        cleanBody = cleanBody.left(177) + "...";
    m_trayIcon->showMessage(title, cleanBody, QSystemTrayIcon::Information, 6000);
}

// ------------------------------------------------------------------ messages

QString MainWindow::senderColor(const QString &sender) const
{
    const uint hash = qHash(sender);
    return Theme::kSenderPalette[hash % Theme::kSenderPaletteSize];
}

MessageRow *MainWindow::addMessageRow(const ChatMessage &message)
{
    auto *row = new MessageRow(message, senderColor(message.senderName));
    if (m_avatars.contains(message.senderId))
        row->setAvatar(m_avatars.value(message.senderId));
    if (m_reactions.contains(message.id))
        row->setReactions(m_reactions.value(message.id));
    connect(row, &MessageRow::reactionToggled, this,
            [this](const QString &messageId, const QString &emoji) {
                if (m_backend)
                    m_backend->sendReaction(m_currentConversation, messageId, emoji);
            });
    connect(row, &MessageRow::editRequested, this, &MainWindow::promptEditMessage);
    connect(row, &MessageRow::deleteRequested, this, &MainWindow::confirmDeleteMessage);
    connect(row, &MessageRow::saveFileRequested, this,
            &MainWindow::saveIncomingFile);
    // Insert before the trailing stretch.
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, row);
    m_visibleRows.insert(message.id, row);
    return row;
}

void MainWindow::rebuildConversationView()
{
    // Drop existing rows (keep the trailing stretch at the end).
    m_visibleRows.clear();
    while (m_messageLayout->count() > 1) {
        QLayoutItem *item = m_messageLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    for (const ChatMessage &message : m_history.value(m_currentConversation))
        addMessageRow(message);
    // Switching into a conversation always lands at the newest message.
    scrollToBottom();
}

void MainWindow::scrollToBottom()
{
    m_stickToBottom = true;
    // The rangeChanged handler scrolls once the new rows expand the range, but
    // when the range is unchanged (e.g. it already fit) no signal fires, so
    // pin to the current maximum after layout settles too.
    QTimer::singleShot(0, this, [this] {
        QScrollBar *bar = m_messageScroll->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
}

void MainWindow::onMessage(const ChatMessage &message)
{
    const QString conversation = message.conversation;
    if (conversation.isEmpty())
        return;

    // Open a DM tab on first contact. For an incoming DM the author *is* the
    // other party (senderId == peerId), so that names the conversation; our
    // own echoed messages (senderId != peerId) must not rename it.
    if (isDirectConversation(conversation)) {
        const QString peerId = dmPeerId(conversation);
        if (message.senderId == peerId && !message.senderName.isEmpty())
            m_dmNames.insert(peerId, message.senderName);
        if (!m_openDms.contains(peerId)) {
            m_openDms.append(peerId);
            refreshDmList();
        }
    }

    m_history[conversation].append(message);

    if (conversation == m_currentConversation) {
        const bool wasAtBottom = m_stickToBottom;
        addMessageRow(message);
        // Follow new arrivals only when already reading the latest; the
        // rangeChanged handler does the actual scrolling once the row lays out.
        if (wasAtBottom)
            scrollToBottom();
    } else {
        m_unread.insert(conversation);
        refreshChannelList();
        refreshDmList();
    }

    if (!message.self) {
        const QString where = isDirectConversation(conversation)
                                  ? "sent you a message"
                                  : "in " + conversation;
        const QString preview =
            message.hasFile() ? "\xF0\x9F\x93\x8E " + message.fileName : message.text;
        notifyIfInactive(message.senderName + " " + where, preview);
    }
}

void MainWindow::onReaction(const QString &conversation, const QString &messageId,
                            const QString &emoji, const QString &reactorName,
                            bool added)
{
    Q_UNUSED(conversation);
    QStringList &reactors = m_reactions[messageId][emoji];
    if (added) {
        if (!reactors.contains(reactorName))
            reactors.append(reactorName);
    } else {
        reactors.removeAll(reactorName);
        if (reactors.isEmpty())
            m_reactions[messageId].remove(emoji);
    }
    if (auto *row = m_visibleRows.value(messageId))
        row->setReactions(m_reactions.value(messageId));
}

void MainWindow::onMessageEdited(const QString &conversation, const QString &messageId,
                                 const QString &newText)
{
    QList<ChatMessage> &messages = m_history[conversation];
    for (ChatMessage &message : messages) {
        if (message.id == messageId) {
            message.text = newText.left(16000);
            message.edited = true;
            break;
        }
    }
    if (conversation == m_currentConversation)
        rebuildConversationView();
}

void MainWindow::onMessageDeleted(const QString &conversation, const QString &messageId)
{
    QList<ChatMessage> &messages = m_history[conversation];
    for (ChatMessage &message : messages) {
        if (message.id == messageId) {
            message.text.clear();
            message.fileName.clear();
            message.fileMime.clear();
            message.fileData.clear();
            message.deleted = true;
            break;
        }
    }
    m_reactions.remove(messageId);
    if (conversation == m_currentConversation)
        rebuildConversationView();
}

void MainWindow::promptEditMessage(const QString &messageId, const QString &currentText)
{
    if (!m_backend || messageId.isEmpty())
        return;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, "Edit message", "Message:", currentText, &ok);
    const QString trimmed = text.trimmed();
    if (!ok || trimmed.isEmpty() || trimmed == currentText)
        return;
    m_backend->editMessage(m_currentConversation, messageId, trimmed);
}

void MainWindow::confirmDeleteMessage(const QString &messageId)
{
    if (!m_backend || messageId.isEmpty())
        return;
    const int result = QMessageBox::question(
        this, "Delete message", "Delete this message for everyone?");
    if (result == QMessageBox::Yes)
        m_backend->deleteMessage(m_currentConversation, messageId);
}

void MainWindow::onAvatar(const QString &peerId, const QByteArray &pngData)
{
    QPixmap pixmap;
    if (!pixmap.loadFromData(pngData))
        return;
    m_avatars.insert(peerId, pixmap);
    // Update any visible rows authored by this peer.
    for (auto it = m_visibleRows.constBegin(); it != m_visibleRows.constEnd(); ++it) {
        if (it.value()->senderId() == peerId)
            it.value()->setAvatar(pixmap);
    }
}

void MainWindow::onTypingChanged(const QString &conversation, const QString &peerId,
                                 const QString &peerName, bool active)
{
    if (conversation.isEmpty() || peerId.isEmpty())
        return;
    if (active)
        m_typing[conversation].insert(peerId, peerName);
    else if (m_typing.contains(conversation))
        m_typing[conversation].remove(peerId);
    refreshTypingLabel();
}

void MainWindow::setChannels(const QStringList &channels)
{
    m_channels = channels;
    if (m_currentConversation.startsWith('#') &&
        !m_channels.contains(m_currentConversation))
        m_currentConversation.clear();
    refreshChannelList();
    updateHomeStats();
    if (m_currentConversation.isEmpty() && !m_channels.isEmpty())
        m_channelList->setCurrentRow(0); // triggers switchConversation
}

void MainWindow::setRoster(const QList<MemberInfo> &members)
{
    m_memberList->clear();
    QHash<QString, int> nameCounts;
    for (const MemberInfo &member : members)
        ++nameCounts[member.name];
    for (const MemberInfo &member : members) {
        QString label = member.name;
        if (nameCounts.value(member.name) > 1 && !member.id.isEmpty())
            label += " [" + member.id.left(6) + "]";
        if (!member.note.isEmpty())
            label += " " + member.note;
        if (member.self)
            label += " (you)";
        auto *item = new QListWidgetItem(label);
        item->setIcon(statusDotIcon(member.online));
        item->setData(Qt::UserRole, member.id);
        item->setData(Qt::UserRole + 1, member.name);
        item->setData(Qt::UserRole + 2, member.self);
        m_memberList->addItem(item);
        // Keep DM tab titles in sync with renamed/rediscovered members.
        if (m_dmNames.contains(member.id) && m_dmNames.value(member.id) != member.name) {
            m_dmNames.insert(member.id, member.name);
            refreshDmList();
            if (m_currentConversation == dmKey(member.id))
                m_channelTitle->setText(kDmPrefix + member.name);
        }
    }

    // Mirror the live connection status of every node onto the Home overview.
    if (m_homeNodeList) {
        m_homeNodeList->clear();
        int online = 0;
        for (const MemberInfo &member : members) {
            QString label = member.name;
            if (member.self)
                label += " (you)";
            else if (!member.note.isEmpty())
                label += " " + member.note;
            auto *item = new QListWidgetItem(statusDotIcon(member.online), label);
            item->setData(Qt::UserRole, member.online ? "online" : "offline");
            m_homeNodeList->addItem(item);
            if (member.online)
                ++online;
        }
        if (m_homeNodes) {
            m_homeNodes->setText(
                members.isEmpty()
                    ? QStringLiteral("No nodes connected yet.")
                    : QString::number(online) + " of " +
                          QString::number(members.size()) +
                          (members.size() == 1 ? " node online" : " nodes online"));
        }
    }
}

void MainWindow::refreshChannelList()
{
    QSignalBlocker blocker(m_channelList);
    m_channelList->clear();
    for (const QString &channel : std::as_const(m_channels)) {
        auto *item = new QListWidgetItem(
            (m_unread.contains(channel) ? "\xE2\x97\x8F " : "") + channel);
        item->setData(Qt::UserRole, channel);
        m_channelList->addItem(item);
        if (channel == m_currentConversation)
            m_channelList->setCurrentItem(item);
    }
}

void MainWindow::refreshDmList()
{
    QSignalBlocker blocker(m_dmList);
    m_dmList->clear();
    for (const QString &peerId : std::as_const(m_openDms)) {
        const QString key = dmKey(peerId);
        auto *item = new QListWidgetItem(
            (m_unread.contains(key) ? "\xE2\x97\x8F " : "") + kDmPrefix +
            m_dmNames.value(peerId, QStringLiteral("unknown")));
        item->setData(Qt::UserRole, key);
        m_dmList->addItem(item);
        if (key == m_currentConversation)
            m_dmList->setCurrentItem(item);
    }
    updateHomeStats();
}

void MainWindow::switchConversation(const QString &conversation)
{
    if (conversation.isEmpty() || conversation == m_currentConversation)
        return;
    sendTypingState(false);
    m_currentConversation = conversation;
    m_unread.remove(conversation);

    QString title = conversation;
    if (isDirectConversation(conversation))
        title = kDmPrefix + m_dmNames.value(dmPeerId(conversation),
                                            QStringLiteral("unknown"));
    m_channelTitle->setText(title);
    m_messageInput->setPlaceholderText("Message " + title);
    rebuildConversationView();
    refreshTypingLabel();

    // Selection lives in exactly one sidebar list at a time.
    if (isDirectConversation(conversation)) {
        QSignalBlocker blocker(m_channelList);
        m_channelList->clearSelection();
        m_channelList->setCurrentItem(nullptr);
    } else {
        QSignalBlocker blocker(m_dmList);
        m_dmList->clearSelection();
        m_dmList->setCurrentItem(nullptr);
    }
    refreshChannelList();
    refreshDmList();
}

void MainWindow::openDirectChat(const QString &peerId, const QString &peerName)
{
    m_dmNames.insert(peerId, peerName);
    if (!m_openDms.contains(peerId)) {
        m_openDms.append(peerId);
        refreshDmList();
    }
    switchConversation(dmKey(peerId));
    m_messageInput->setFocus();
}

void MainWindow::promptAddChannel()
{
    if (!m_backend)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Add channel", "Channel name:", QLineEdit::Normal, "#", &ok);
    if (ok && !name.trimmed().isEmpty() && name.trimmed() != "#")
        m_backend->addChannel(name);
}

void MainWindow::sendCurrentMessage()
{
    const QString text = m_messageInput->text().trimmed();
    if (text.isEmpty() || !m_backend || m_currentConversation.isEmpty())
        return;
    sendTypingState(false);
    if (isDirectConversation(m_currentConversation))
        m_backend->sendDirect(dmPeerId(m_currentConversation), text);
    else
        m_backend->sendChat(m_currentConversation, text);
    m_messageInput->clear();
}

void MainWindow::onComposerEdited(const QString &text)
{
    if (!m_backend || m_currentConversation.isEmpty())
        return;
    if (text.trimmed().isEmpty()) {
        sendTypingState(false);
        return;
    }
    sendTypingState(true);
    m_typingStopTimer->start(2500);
}

void MainWindow::sendTypingState(bool active)
{
    if (!m_backend)
        return;
    if (active) {
        if (m_currentConversation.isEmpty())
            return;
        if (m_typingConversation == m_currentConversation &&
            m_typingStopTimer->isActive())
            return;
        m_typingConversation = m_currentConversation;
        m_backend->sendTyping(m_currentConversation, true);
        return;
    }
    if (!m_typingConversation.isEmpty()) {
        m_backend->sendTyping(m_typingConversation, false);
        m_typingConversation.clear();
    }
    m_typingStopTimer->stop();
}

void MainWindow::refreshTypingLabel()
{
    QStringList names;
    const auto active = m_typing.value(m_currentConversation);
    for (const QString &name : active)
        names.append(name);
    names.removeDuplicates();

    if (names.isEmpty()) {
        m_typingLabel->clear();
    } else if (names.size() == 1) {
        m_typingLabel->setText(names.first() + " is typing...");
    } else if (names.size() == 2) {
        m_typingLabel->setText(names.at(0) + " and " + names.at(1) +
                               " are typing...");
    } else {
        m_typingLabel->setText(QString::number(names.size()) + " people are typing...");
    }
}

// ------------------------------------------------------------------- files

void MainWindow::attachFile()
{
    if (!m_backend || m_currentConversation.isEmpty())
        return;
    const QString path =
        QFileDialog::getOpenFileName(this, "Share a file", QString(), "All files (*)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Share a file", "Could not read " + path);
        return;
    }
    // Keep LAN transfers reasonable; the wire frame caps at ~96 MB.
    const qint64 maxBytes = 64ll * 1024 * 1024;
    if (file.size() > maxBytes) {
        QMessageBox::warning(this, "Share a file",
                             "That file is larger than 64 MB. Please share a "
                             "smaller file.");
        return;
    }
    const QByteArray data = file.readAll();
    const QFileInfo info(path);
    const QString mime = QMimeDatabase().mimeTypeForFileNameAndData(path, data).name();
    m_backend->sendFile(m_currentConversation, info.fileName(), mime, data);
}

void MainWindow::saveIncomingFile(const QString &fileName, const QByteArray &data)
{
    const QString safeName = QFileInfo(fileName).fileName().left(180);
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString suggested =
        (dir.isEmpty() ? QDir::homePath() : dir) + "/" +
        (safeName.isEmpty() ? QStringLiteral("file") : safeName);
    const QString path =
        QFileDialog::getSaveFileName(this, "Save file", suggested);
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        QMessageBox::warning(this, "Save file", "Could not save to " + path);
        return;
    }
}

// ------------------------------------------------------------- repositories

QString MainWindow::repositoryMirrorRoot() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/mirrors";
}

QString MainWindow::repositoryChannel(const RepositoryRecord &repo) const
{
    return "#" + repoSegment(repo.owner, QStringLiteral("owner")) + "-" +
           repoSegment(repo.name, QStringLiteral("repository"));
}

QString MainWindow::repositorySource(const RepositoryRecord &repo) const
{
    return repo.localPath.trimmed().isEmpty() ? repo.cloneUrl.trimmed()
                                             : repo.localPath.trimmed();
}

void MainWindow::loadRepositories()
{
    m_repositories.clear();
    QSettings settings;
    const int count = settings.beginReadArray(kRepositoriesArray);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        RepositoryRecord repo;
        repo.owner = settings.value("owner").toString();
        repo.name = settings.value("name").toString();
        repo.description = settings.value("description").toString();
        repo.cloneUrl = settings.value("cloneUrl").toString();
        repo.localPath = settings.value("localPath").toString();
        repo.bchAddress = settings.value("bchAddress").toString();
        repo.mirrorPath = settings.value("mirrorPath").toString();
        repo.publishToNetwork = settings.value("publishToNetwork").toBool();
        repo.hostedSinceMs = settings.value("hostedSinceMs").toLongLong();
        repo.lastSyncMs = settings.value("lastSyncMs").toLongLong();
        repo.publishedAtMs = settings.value("publishedAtMs").toLongLong();
        if (!repo.name.isEmpty() && !repositorySource(repo).isEmpty())
            m_repositories.append(repo);
    }
    settings.endArray();
}

void MainWindow::saveRepositories() const
{
    QSettings settings;
    settings.beginWriteArray(kRepositoriesArray);
    for (int i = 0; i < m_repositories.size(); ++i) {
        settings.setArrayIndex(i);
        const RepositoryRecord &repo = m_repositories.at(i);
        settings.setValue("owner", repo.owner);
        settings.setValue("name", repo.name);
        settings.setValue("description", repo.description);
        settings.setValue("cloneUrl", repo.cloneUrl);
        settings.setValue("localPath", repo.localPath);
        settings.setValue("bchAddress", repo.bchAddress);
        settings.setValue("mirrorPath", repo.mirrorPath);
        settings.setValue("publishToNetwork", repo.publishToNetwork);
        settings.setValue("hostedSinceMs", repo.hostedSinceMs);
        settings.setValue("lastSyncMs", repo.lastSyncMs);
        settings.setValue("publishedAtMs", repo.publishedAtMs);
    }
    settings.endArray();
}

void MainWindow::refreshRepositoryList()
{
    if (!m_repoList)
        return;

    QSignalBlocker blocker(m_repoList);
    const int previousRow = m_repoList->currentRow();
    m_repoList->clear();
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        const bool online = repo.publishedAtMs > 0;
        QString label = repo.owner + "/" + repo.name;
        if (m_syncingRepos.contains(i))
            label += "  \xC2\xB7 syncing";
        else if (repo.lastSyncMs > 0)
            label += "  \xC2\xB7 mirrored";
        if (online)
            label += "  \xC2\xB7 online";
        else if (repo.publishToNetwork)
            label += "  \xC2\xB7 publishing\xE2\x80\xA6";
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, i);
        // Green dot = published and browsable on the web; grey = local/pending.
        if (repo.publishToNetwork)
            item->setIcon(statusDotIcon(online));
        item->setToolTip(
            "Source: " + repositorySource(repo) +
            "\nMirror path: " + repo.mirrorPath +
            "\nHosted since: " + formatRepoDate(repo.hostedSinceMs) +
            "\nLast sync: " + formatRepoDate(repo.lastSyncMs) +
            "\nWeb: " +
            (online ? "online at " + repositoryWebUrl(repo)
                    : (repo.publishToNetwork ? "publishing\xE2\x80\xA6"
                                             : "local only")) +
            (repo.bchAddress.isEmpty() ? QString() :
                                       "\nDonations: " + repo.bchAddress));
        m_repoList->addItem(item);
    }
    if (previousRow >= 0 && previousRow < m_repoList->count())
        m_repoList->setCurrentRow(previousRow);
    updateRepoWebLink();
    updateHomeStats();
}

void MainWindow::promptAddRepository()
{
    // Simple flow: pick a local Git repository. Everything else is derived.
    // The folder is mirrored locally and only signed metadata is published to
    // the website; the .git data never leaves this machine.
    const QString path = QFileDialog::getExistingDirectory(
        this, "Choose a local Git repository to mirror and publish");
    if (path.isEmpty())
        return;

    const bool looksLikeGit =
        QDir(path).exists(".git") || QDir(path).exists("HEAD");
    if (!looksLikeGit) {
        QMessageBox::warning(
            this, "Add repository",
            "That folder is not a Git repository. Choose a folder created by "
            "\"git init\" or \"git clone\".");
        return;
    }

    RepositoryRecord repo;
    repo.localPath = path;
    repo.name = repoNameFromUrl(path);
    repo.owner = QSettings().value(kHandleSetting).toString().trimmed();
    if (repo.owner.isEmpty())
        repo.owner = repoSegment(m_userName, QStringLiteral("owner"));
    repo.bchAddress = QSettings().value(kBchSetting).toString().trimmed();
    // Selecting a local repo publishes it to the website so it shows up online
    // and others can discover and mirror it. No public clone URL is sent.
    repo.publishToNetwork = true;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryMirrorRoot() + "/" +
                      repoSegment(repo.owner, QStringLiteral("owner")) + "-" +
                      repoSegment(repo.name, QStringLiteral("repository")) + ".git";

    m_repositories.append(repo);
    saveRepositories();
    refreshRepositoryList();
    if (m_backend)
        m_backend->addChannel(repositoryChannel(repo));

    const QJsonObject metadata{{"owner", repo.owner},
                               {"name", repo.name},
                               {"channel", repositoryChannel(repo)},
                               {"mirrorPath", repo.mirrorPath},
                               {"hostedSince", QString::number(repo.hostedSinceMs)},
                               {"maintainer", m_profileIdentity.publicKey()}};
    logSystem("Repository: signed mirror metadata for " + repo.owner + "/" +
              repo.name + " with signature " +
              m_profileIdentity.signJson(metadata).left(16) + "...");
    publishRepository(m_repositories.size() - 1, false);
    syncRepository(m_repositories.size() - 1);
}

QString MainWindow::repositoryWebUrl(const RepositoryRecord &repo) const
{
    // Deep link to the repository's card on the public website, derived from
    // the same host that serves the catalog API.
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/"));
    url.setFragment("repo-" + repoSegment(repo.owner, QStringLiteral("owner")) +
                    "-" + repoSegment(repo.name, QStringLiteral("repository")));
    return url.toString();
}

void MainWindow::updateRepoWebLink()
{
    if (!m_repoWebLink)
        return;
    QListWidgetItem *item = m_repoList ? m_repoList->currentItem() : nullptr;
    if (!item) {
        m_repoWebLink->setText("Select a repository to see its web status.");
        return;
    }
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(index);
    if (repo.publishedAtMs > 0) {
        const QString url = repositoryWebUrl(repo);
        m_repoWebLink->setText(
            "\xF0\x9F\x9F\xA2 <b>Online</b> \xC2\xB7 browsable at "
            "<a style='color:#4ade80' href=\"" + url + "\">" + url + "</a>");
    } else if (repo.publishToNetwork) {
        m_repoWebLink->setText(
            "\xF0\x9F\x95\x92 Publishing to the network\xE2\x80\xA6");
    } else {
        m_repoWebLink->setText("Local only \xC2\xB7 not published.");
    }
}

void MainWindow::syncSelectedRepository()
{
    if (!m_repoList)
        return;
    QListWidgetItem *item = m_repoList->currentItem();
    if (!item) {
        QMessageBox::information(this, "Sync repository",
                                 "Select a mirrored repository first.");
        return;
    }
    syncRepository(item->data(Qt::UserRole).toInt());
}

QUrl MainWindow::catalogApiUrl() const
{
    QUrl url(m_serverUrlEdit ? m_serverUrlEdit->text().trimmed() : kDefaultServerUrl);
    if (!url.isValid() || url.host().isEmpty())
        url = QUrl(kDefaultServerUrl);
    if (url.scheme() == "ws")
        url.setScheme(QStringLiteral("http"));
    else if (url.scheme() == "wss")
        url.setScheme(QStringLiteral("https"));
    url.setPath(QStringLiteral("/api/repositories"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

QUrl MainWindow::filesApiUrl(const RepositoryRecord &repo) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/files");
    return url;
}

QUrl MainWindow::hostWsUrl(const RepositoryRecord &repo) const
{
    QUrl url(m_serverUrlEdit ? m_serverUrlEdit->text().trimmed() : kDefaultServerUrl);
    if (!url.isValid() || url.host().isEmpty())
        url = QUrl(kDefaultServerUrl);
    if (url.scheme() == "http")
        url.setScheme(QStringLiteral("ws"));
    else if (url.scheme() == "https")
        url.setScheme(QStringLiteral("wss"));
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/host");
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void MainWindow::stopRepoHosts()
{
    for (RepoHost *host : std::as_const(m_repoHosts)) {
        host->stop();
        host->deleteLater();
    }
    m_repoHosts.clear();
}

void MainWindow::startRepoHosts()
{
    // One live host per published repository that already has a local mirror.
    // Rebuilt from scratch so adding/removing repos stays simple.
    stopRepoHosts();
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (!repo.publishToNetwork || repo.mirrorPath.isEmpty() ||
            !QDir(repo.mirrorPath).exists())
            continue;
        auto *host = new RepoHost(repo.owner, repo.name, repo.mirrorPath,
                                  hostWsUrl(repo), this);
        connect(host, &RepoHost::log, this, &MainWindow::logSystem);
        host->start();
        m_repoHosts.append(host);
    }
}

void MainWindow::publishRepositoryFiles(int index)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(index);
    if (!repo.publishToNetwork || repo.mirrorPath.isEmpty())
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load())
        return;

    // List the files in the mirror's default branch (paths + sizes only). The
    // file contents never leave this machine; only the listing is published.
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index](int exitCode, QProcess::ExitStatus) {
                const QByteArray out = process->readAllStandardOutput();
                process->deleteLater();
                if (exitCode != 0 || index < 0 || index >= m_repositories.size())
                    return;

                const RepositoryRecord &repo = m_repositories.at(index);
                QJsonArray files;
                const QList<QByteArray> lines = out.split('\n');
                for (const QByteArray &line : lines) {
                    // "<mode> <type> <oid> <size>\t<path>"
                    const int tab = line.indexOf('\t');
                    if (tab < 0)
                        continue;
                    const QList<QByteArray> meta =
                        line.left(tab).simplified().split(' ');
                    if (meta.size() < 4 || meta.at(1) != "blob")
                        continue;
                    bool ok = false;
                    const qlonglong size = meta.at(3).toLongLong(&ok);
                    files.append(QJsonObject{
                        {"path", QString::fromUtf8(line.mid(tab + 1))},
                        {"size", double(ok ? size : 0)}});
                    if (files.size() >= 5000)
                        break;
                }

                const QString updatedAt =
                    QString::number(QDateTime::currentMSecsSinceEpoch());
                const QJsonObject signedMeta{
                    {"owner", repo.owner},
                    {"name", repo.name},
                    {"updatedAt", updatedAt},
                    {"count", files.size()},
                    {"maintainer", m_profileIdentity.publicKey()}};
                QJsonObject payload{
                    {"owner", repo.owner},
                    {"name", repo.name},
                    {"updatedAt", updatedAt},
                    {"maintainer", m_profileIdentity.publicKey()},
                    {"signature", m_profileIdentity.signJson(signedMeta)},
                    {"files", files}};

                QNetworkRequest request(filesApiUrl(repo));
                request.setHeader(QNetworkRequest::ContentTypeHeader,
                                  QStringLiteral("application/json"));
                QNetworkReply *reply = m_networkAccess->post(
                    request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
                logSystem("Files: publishing " + QString::number(files.size()) +
                          " file paths for " + repo.owner + "/" + repo.name + ".");
                connect(reply, &QNetworkReply::finished, this, [this, reply, repo] {
                    const int status =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                            .toInt();
                    const QNetworkReply::NetworkError error = reply->error();
                    reply->deleteLater();
                    if (error == QNetworkReply::NoError && status >= 200 &&
                        status < 300)
                        logSystem("Files: published file list for " + repo.owner +
                                  "/" + repo.name + ".");
                    else
                        logSystem("Files: could not publish file list for " +
                                  repo.owner + "/" + repo.name + " (HTTP " +
                                  QString::number(status) + ").");
                });
            });
    connect(process, &QProcess::errorOccurred, this, [process] { process->deleteLater(); });
    process->start("git", {"-C", repo.mirrorPath, "ls-tree", "-r", "-l",
                           "--full-tree", "HEAD"});
}

void MainWindow::publishSelectedRepository()
{
    if (!m_repoList)
        return;
    QListWidgetItem *item = m_repoList->currentItem();
    if (!item) {
        QMessageBox::information(this, "Publish repository",
                                 "Select a mirrored repository first.");
        return;
    }
    const int index = item->data(Qt::UserRole).toInt();
    if (index >= 0 && index < m_repositories.size()) {
        m_repositories[index].publishToNetwork = true;
        saveRepositories();
        refreshRepositoryList();
    }
    publishRepository(index);
}

void MainWindow::publishRepository(int index, bool showDialogOnError)
{
    if (index < 0 || index >= m_repositories.size())
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        logSystem("Catalog: could not load identity for repository publishing.");
        return;
    }

    RepositoryRecord &repo = m_repositories[index];
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QJsonObject metadata{{"owner", repo.owner},
                         {"name", repo.name},
                         {"description", repo.description},
                         {"cloneUrl", repo.cloneUrl},
                         {"bch", repo.bchAddress},
                         {"channel", repositoryChannel(repo)},
                         {"hostedSince", QString::number(repo.hostedSinceMs)},
                         {"lastSync", QString::number(repo.lastSyncMs)},
                         {"updatedAt", QString::number(now)},
                         {"source", repo.localPath.trimmed().isEmpty()
                                        ? QStringLiteral("remote-clone")
                                        : QStringLiteral("local-node")},
                         {"maintainer", m_profileIdentity.publicKey()}};
    metadata.insert("signature", m_profileIdentity.signJson(metadata));

    QNetworkRequest request(catalogApiUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply =
        m_networkAccess->post(request, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    logSystem("Catalog: publishing " + repo.owner + "/" + repo.name + " to " +
              request.url().toString() + ".");

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, index, showDialogOnError] {
                const QByteArray body = reply->readAll();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QNetworkReply::NetworkError error = reply->error();
                reply->deleteLater();

                if (index < 0 || index >= m_repositories.size())
                    return;

                RepositoryRecord &repo = m_repositories[index];
                if (error == QNetworkReply::NoError && status >= 200 && status < 300) {
                    repo.publishToNetwork = true;
                    repo.publishedAtMs = QDateTime::currentMSecsSinceEpoch();
                    saveRepositories();
                    refreshRepositoryList();
                    logSystem("Catalog: published " + repo.owner + "/" +
                              repo.name + " to forkmesh.com.");
                    return;
                }

                const QString detail =
                    QString::fromUtf8(body).trimmed().left(500);
                const QString message =
                    "Catalog publish failed for " + repo.owner + "/" + repo.name +
                    (status > 0 ? " (HTTP " + QString::number(status) + ")" :
                                  QString()) +
                    (detail.isEmpty() ? QString() : ": " + detail);
                logSystem(message);
                if (showDialogOnError)
                    QMessageBox::warning(this, "Publish repository", message);
            });
}

void MainWindow::syncRepository(int index)
{
    if (index < 0 || index >= m_repositories.size() ||
        m_syncingRepos.contains(index))
        return;

    RepositoryRecord &repo = m_repositories[index];
    if (!QDir().mkpath(QFileInfo(repo.mirrorPath).absolutePath())) {
        QMessageBox::warning(this, "Sync repository",
                             "Could not create " +
                                 QFileInfo(repo.mirrorPath).absolutePath());
        return;
    }

    const bool hasMirror = QDir(repo.mirrorPath).exists();
    const QString source = repositorySource(repo);
    const QStringList args = hasMirror
                                 ? QStringList{"-C", repo.mirrorPath,
                                               "fetch", "--prune"}
                                 : QStringList{"clone", "--mirror",
                                               source, repo.mirrorPath};

    m_syncingRepos.insert(index);
    refreshRepositoryList();
    logSystem(QStringLiteral("Mirror: ") +
              (hasMirror ? QStringLiteral("fetching ") : QStringLiteral("cloning ")) +
              repo.owner + "/" + repo.name + " from " + source + ".");

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index](int exitCode, QProcess::ExitStatus) {
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                m_syncingRepos.remove(index);

                if (index < 0 || index >= m_repositories.size()) {
                    refreshRepositoryList();
                    return;
                }

                RepositoryRecord &repo = m_repositories[index];
                if (exitCode == 0) {
                    repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                    saveRepositories();
                    refreshRepositoryList();
                    logSystem("Mirror: synced " + repo.owner + "/" + repo.name +
                              " into " + repo.mirrorPath + ".");
                    if (m_backend) {
                        m_backend->sendChat(
                            repositoryChannel(repo),
                            "Mirror synced by " + m_userName + " at " +
                                formatRepoDate(repo.lastSyncMs));
                    }
                    if (repo.publishToNetwork) {
                        publishRepository(index, false);
                        // Serve this repo's files live to the web now that a
                        // mirror exists (pure live tunnel, nothing uploaded).
                        startRepoHosts();
                    }
                } else {
                    refreshRepositoryList();
                    logSystem("Mirror: sync failed for " + repo.owner + "/" +
                              repo.name + ": " + errors.right(300));
                    QMessageBox::warning(
                        this, "Sync repository",
                        "Git mirror sync failed" +
                            (errors.isEmpty() ? QString() :
                                                ": " + errors.right(500)));
                }
            });
    connect(process, &QProcess::errorOccurred, this, [this, process, index] {
        process->deleteLater();
        m_syncingRepos.remove(index);
        refreshRepositoryList();
        QMessageBox::warning(this, "Sync repository",
                             "Could not run git. Install Git and try again.");
    });
    process->start("git", args);
}

// ------------------------------------------------------------------ settings

void MainWindow::openSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(FORKMESH_VERSION, this);
        m_settingsDialog->setDisplayName(m_userName);
        if (!m_userAvatar.isEmpty())
            m_settingsDialog->setAvatar(m_userAvatar);
        for (const QString &line : std::as_const(m_networkLog))
            m_settingsDialog->appendLog(line);
        connect(m_settingsDialog, &SettingsDialog::displayNameChanged, this,
                &MainWindow::onDisplayNameChanged);
        connect(m_settingsDialog, &SettingsDialog::avatarChosen, this,
                &MainWindow::onAvatarChosen);
        connect(m_settingsDialog, &SettingsDialog::leaveRequested, this,
                [this] { leaveSession(); });
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void MainWindow::onDisplayNameChanged(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == m_userName)
        return;
    m_userName = trimmed;
    saveDisplayName(trimmed);
    // The display name is announced with each peer link; it takes effect for
    // new messages immediately and for peers on their next reconnect.
    logSystem("Display name changed to " + trimmed +
              " (applies to new messages).");
}

void MainWindow::onAvatarChosen(const QByteArray &pngData)
{
    m_userAvatar = pngData;
    QSettings().setValue(kAvatarSetting, pngData);
    if (m_backend)
        m_backend->setAvatar(pngData);
}
