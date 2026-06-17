#include "MainWindow.h"

#include "MessageRow.h"
#include "RepoHost.h"
#include "ServerNode.h"
#include "Theme.h"

#include <QApplication>
#include <QBuffer>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
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
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

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
const QString kDefaultServerUrl =
    QStringLiteral("wss://forkmesh.com/api/repo/mainnode/forkmesh/rooms/general/ws");
const QString kRoomNameSetting = QStringLiteral("server/room");
const QString kPassphraseSetting = QStringLiteral("server/passphrase");
const QString kServersArray = QStringLiteral("servers/items");
const QString kActiveServerSetting = QStringLiteral("servers/active");
const QString kDefaultRoomName = QStringLiteral("general");
const QString kDefaultPassphrase = QStringLiteral("forkmesh-public-room");
const QString kRepositoriesArray = QStringLiteral("repositories/items");
const QString kMirrorRootSetting = QStringLiteral("repositories/mirrorRoot");
const QString kConnectionTotalSetting = QStringLiteral("stats/connectionTotalMs");
constexpr int kNetworkLogLimit = 2000;
constexpr int kHomeGraphSampleLimit = 18;

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

QString formatDuration(qint64 ms)
{
    const qint64 totalSeconds = std::max<qint64>(0, ms / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes, 2, 10, QChar('0'));
    if (minutes > 0)
        return QStringLiteral("%1m %2s").arg(minutes).arg(seconds, 2, 10, QChar('0'));
    return QStringLiteral("%1s").arg(seconds);
}

QString compactAddress(QString address)
{
    address = address.trimmed();
    if (address.size() <= 30)
        return address;
    return address.left(18) + QStringLiteral("...") + address.right(8);
}

QString sponsorUrlFor(QString address)
{
    address = address.trimmed();
    if (address.isEmpty())
        return {};
    if (address.startsWith(QStringLiteral("bitcoincash:"), Qt::CaseInsensitive))
        return address;
    return QStringLiteral("bitcoincash:") + address;
}

QString connectionGraphText(const QList<int> &samples)
{
    if (samples.isEmpty())
        return QStringLiteral("00m [.       ] 0");

    QStringList lines;
    const int first = std::max(0, int(samples.size()) - kHomeGraphSampleLimit);
    for (int i = first; i < samples.size(); ++i) {
        const int count = std::max(0, samples.at(i));
        const int marks = count == 0 ? 1 : std::min(8, count);
        QString bar(marks, count == 0 ? QChar('.') : QChar('#'));
        bar = bar.leftJustified(8, QChar('.'));
        lines << QStringLiteral("%1m [%2] %3")
                     .arg(i, 2, 10, QChar('0'))
                     .arg(bar)
                     .arg(count);
    }
    return lines.join('\n');
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

QString serverHost(const QString &serverUrl)
{
    return QUrl(serverUrl).host();
}

// A http(s) favicon URL derived from a ws(s) mainnode URL.
QUrl faviconUrl(const QString &serverUrl)
{
    const QUrl url(serverUrl);
    if (url.host().isEmpty())
        return {};
    QUrl out;
    out.setScheme(url.scheme() == QStringLiteral("ws") ? QStringLiteral("http")
                                                       : QStringLiteral("https"));
    out.setHost(url.host());
    if (url.port() > 0)
        out.setPort(url.port());
    out.setPath(QStringLiteral("/favicon.ico"));
    return out;
}

QString faviconCacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/favicons";
}

QString faviconCachePath(const QString &host)
{
    QString safe = host;
    safe.replace(QRegularExpression("[^a-zA-Z0-9._-]"), "_");
    return faviconCacheDir() + "/" + safe + ".png";
}

// A circular fallback badge showing the first letter of the host, used until a
// real favicon is fetched (or when the server has none).
QPixmap letterFavicon(const QString &host)
{
    constexpr int side = 36;
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const uint hash = qHash(host);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Theme::kSenderPalette[hash % Theme::kSenderPaletteSize]));
    painter.drawEllipse(0, 0, side, side);
    const QChar letter = host.isEmpty() ? QChar('?') : host.at(0).toUpper();
    QFont font = painter.font();
    font.setPixelSize(18);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor("#0f172a"));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(letter));
    return pixmap;
}

#if defined(Q_OS_WIN)
const QString kWinRunKey =
    QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
#endif

QString autostartDesktopPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           "/autostart/forkmesh.desktop";
}

#if defined(Q_OS_MACOS)
QString launchAgentPath()
{
    return QDir::homePath() + "/Library/LaunchAgents/com.forkmesh.app.plist";
}
#endif

bool isAutostartEnabled()
{
#if defined(Q_OS_WIN)
    QSettings run(kWinRunKey, QSettings::NativeFormat);
    return run.contains("ForkMesh");
#elif defined(Q_OS_MACOS)
    return QFileInfo::exists(launchAgentPath());
#else
    return QFileInfo::exists(autostartDesktopPath());
#endif
}

void setAutostartEnabled(bool enabled)
{
    const QString exe = QCoreApplication::applicationFilePath();
#if defined(Q_OS_WIN)
    QSettings run(kWinRunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue("ForkMesh", QDir::toNativeSeparators(exe));
    else
        run.remove("ForkMesh");
#elif defined(Q_OS_MACOS)
    const QString path = launchAgentPath();
    if (!enabled) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString plist = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\"><dict>\n"
            "  <key>Label</key><string>com.forkmesh.app</string>\n"
            "  <key>ProgramArguments</key><array><string>%1</string></array>\n"
            "  <key>RunAtLoad</key><true/>\n"
            "</dict></plist>\n").arg(exe);
        file.write(plist.toUtf8());
    }
#else
    const QString path = autostartDesktopPath();
    if (!enabled) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString desktop = QStringLiteral(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=ForkMesh\n"
            "Exec=%1\n"
            "Terminal=false\n"
            "X-GNOME-Autostart-enabled=true\n").arg(exe);
        file.write(desktop.toUtf8());
    }
#endif
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
    m_totalConnectionMs = QSettings().value(kConnectionTotalSetting).toLongLong();

    loadServers();
    loadCachedFavicons();

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildSetupPage());
    m_stack->addWidget(buildChatPage());
    setCentralWidget(m_stack);
    loadRepositories();
    refreshRepositoryList();
    loadActiveServerIntoEdits();
    refreshServerRail();
    for (int i = 0; i < m_servers.size(); ++i)
        fetchFavicon(i);

    m_typingStopTimer = new QTimer(this);
    m_typingStopTimer->setSingleShot(true);
    connect(m_typingStopTimer, &QTimer::timeout, this, [this] {
        sendTypingState(false);
    });
    m_homeStatsTimer = new QTimer(this);
    connect(m_homeStatsTimer, &QTimer::timeout, this, &MainWindow::updateHomeStats);
    m_homeStatsTimer->start(60000);

    // Keep mirrors fresh: periodically fetch each repo so a mirror tracks the
    // owner's repo as it updates. A first pass runs shortly after startup.
    m_mirrorSyncTimer = new QTimer(this);
    connect(m_mirrorSyncTimer, &QTimer::timeout, this, &MainWindow::autoSyncMirrors);
    m_mirrorSyncTimer->start(5 * 60 * 1000);
    QTimer::singleShot(15000, this, &MainWindow::autoSyncMirrors);

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
    const bool legacyWorkersDevUrl = QUrl(savedServerUrl).host().endsWith(
        QStringLiteral(".workers.dev"));
    m_serverUrlEdit->setText(
        savedServerUrl.isEmpty() || savedServerUrl == kLocalServerUrl ||
                legacyWorkersDevUrl
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
    connect(m_bchEdit, &QLineEdit::textEdited, this, [this](const QString &address) {
        QSettings().setValue(kBchSetting, address.trimmed());
        updateBchNotice();
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

    // Seed the Settings section's profile controls.
    if (m_settingsNameEdit)
        m_settingsNameEdit->setText(m_userName);
    setSettingsAvatar(m_userAvatar);

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
    persistEditsToActiveServer();
    const QUrl url(m_serverUrlEdit->text().trimmed());
    auto *server = new ServerNode(name, url, m_roomNameEdit->text().trimmed(),
                                  m_passphraseEdit->text(),
                                  m_bchEdit->text().trimmed(), this);
    attachBackend(server);
    if (!server->start())
        return;

    if (m_backend) {
        if (m_connectedAtMs <= 0)
            m_connectedAtMs = QDateTime::currentMSecsSinceEpoch();
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
        refreshServerRail();
        updateBchNotice();
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
    QLabel *label = m_buildStatusLabel ? m_buildStatusLabel : m_updateStatus;
    if (!label)
        return;
    label->setStyleSheet(isError ? "color:#ff6b6b; background:transparent;"
                                  : "color:#9ca3af; background:transparent;");
    label->setText(status);
    label->show();
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
                    if (m_buildButton)
                        m_buildButton->setEnabled(true);
                    return;
                }
                onSuccess();
            });
    connect(process, &QProcess::errorOccurred, this, [this, process] {
        setUpdateStatus("Update failed: could not run " + process->program(), true);
        process->deleteLater();
        if (m_buildButton)
            m_buildButton->setEnabled(true);
    });
    process->start(program, arguments);
}

void MainWindow::runQuickUpdate()
{
    saveDisplayName(m_nameEdit->text());
    m_buildButton = m_updateButton;
    m_buildStatusLabel = m_updateStatus;
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
            QProcess::startDetached(appPath, {});
            QCoreApplication::quit();
        });
    });
}

// -------------------------------------------------------------- server rail

void MainWindow::loadServers()
{
    m_servers.clear();
    const QString json = QSettings().value(kServersArray).toString();
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        const QString url = obj.value("url").toString().trimmed();
        if (url.isEmpty())
            continue;
        ServerConfig server;
        server.url = url;
        server.room = obj.value("room").toString(kDefaultRoomName);
        server.passphrase = obj.value("passphrase").toString(kDefaultPassphrase);
        m_servers.append(server);
    }

    // Migration: seed the list from the legacy single-server keys (or defaults).
    if (m_servers.isEmpty()) {
        const QString savedUrl =
            QSettings().value(kServerUrlSetting).toString().trimmed();
        const bool legacyWorkersDevUrl =
            QUrl(savedUrl).host().endsWith(QStringLiteral(".workers.dev"));
        ServerConfig server;
        server.url = (savedUrl.isEmpty() || savedUrl == kLocalServerUrl ||
                      legacyWorkersDevUrl)
                         ? kDefaultServerUrl
                         : savedUrl;
        server.room =
            QSettings().value(kRoomNameSetting, kDefaultRoomName).toString();
        server.passphrase =
            QSettings().value(kPassphraseSetting, kDefaultPassphrase).toString();
        m_servers.append(server);
    }

    m_activeServer = QSettings().value(kActiveServerSetting, 0).toInt();
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        m_activeServer = 0;
}

void MainWindow::saveServers()
{
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        m_activeServer = qBound(0, m_activeServer, qMax(0, m_servers.size() - 1));

    QJsonArray array;
    for (const ServerConfig &server : std::as_const(m_servers)) {
        array.append(QJsonObject{{"url", server.url},
                                 {"room", server.room},
                                 {"passphrase", server.passphrase}});
    }
    QSettings settings;
    settings.setValue(kServersArray,
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    settings.setValue(kActiveServerSetting, m_activeServer);

    // Mirror the active server into the legacy keys the rest of the app reads.
    if (!m_servers.isEmpty()) {
        const ServerConfig &active = m_servers.at(m_activeServer);
        settings.setValue(kServerUrlSetting, active.url);
        settings.setValue(kRoomNameSetting, active.room);
        settings.setValue(kPassphraseSetting, active.passphrase);
    }
}

void MainWindow::loadActiveServerIntoEdits()
{
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        return;
    const ServerConfig &active = m_servers.at(m_activeServer);
    if (m_serverUrlEdit)
        m_serverUrlEdit->setText(active.url);
    if (m_roomNameEdit)
        m_roomNameEdit->setText(active.room);
    if (m_passphraseEdit)
        m_passphraseEdit->setText(active.passphrase);
}

void MainWindow::persistEditsToActiveServer()
{
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        return;
    ServerConfig &active = m_servers[m_activeServer];
    active.url = m_serverUrlEdit->text().trimmed();
    active.room = m_roomNameEdit->text().trimmed();
    active.passphrase = m_passphraseEdit->text();
    saveServers();
}

QPixmap MainWindow::faviconFor(const ServerConfig &server) const
{
    const QString host = serverHost(server.url);
    if (m_faviconCache.contains(host))
        return m_faviconCache.value(host);
    return letterFavicon(host);
}

QWidget *MainWindow::buildServerRail()
{
    m_serverRail = new QWidget;
    m_serverRail->setObjectName("serverRail");
    m_serverRail->setFixedWidth(56);

    m_serverGroup = new QButtonGroup(this);
    m_serverGroup->setExclusive(true);

    auto *layout = new QVBoxLayout(m_serverRail);
    layout->setContentsMargins(8, 14, 8, 14);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignTop);
    refreshServerRail();
    return m_serverRail;
}

void MainWindow::refreshServerRail()
{
    if (!m_serverRail)
        return;
    auto *layout = qobject_cast<QVBoxLayout *>(m_serverRail->layout());
    if (!layout)
        return;

    // Clear existing buttons.
    for (QAbstractButton *button : m_serverGroup->buttons())
        m_serverGroup->removeButton(button);
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    for (int i = 0; i < m_servers.size(); ++i) {
        const ServerConfig &server = m_servers.at(i);
        auto *button = new QPushButton;
        button->setObjectName("serverButton");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(40, 40);
        button->setIconSize(QSize(28, 28));
        button->setIcon(QIcon(faviconFor(server)));
        button->setToolTip(serverHost(server.url));
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        if (i == m_activeServer)
            button->setChecked(true);
        m_serverGroup->addButton(button, i);
        connect(button, &QWidget::customContextMenuRequested, this,
                [this, i](const QPoint &) { removeServer(i); });
        layout->addWidget(button, 0, Qt::AlignHCenter);
    }

    auto *addButton = new QPushButton("+");
    addButton->setObjectName("serverAddButton");
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setFixedSize(40, 40);
    addButton->setToolTip("Add a mainnode server");
    connect(addButton, &QPushButton::clicked, this, &MainWindow::promptAddServer);
    layout->addWidget(addButton, 0, Qt::AlignHCenter);
    layout->addStretch();

    connect(m_serverGroup, &QButtonGroup::idClicked, this,
            &MainWindow::switchToServer, Qt::UniqueConnection);
}

void MainWindow::switchToServer(int index)
{
    if (index < 0 || index >= m_servers.size())
        return;
    const bool live = m_backend != nullptr;
    if (index == m_activeServer && live)
        return;

    if (live)
        persistEditsToActiveServer(); // capture any edits to the current server
    m_activeServer = index;
    saveServers();
    loadActiveServerIntoEdits();
    refreshServerRail();
    startSession(); // tears down the old backend and connects to the new server
}

void MainWindow::promptAddServer()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Add mainnode server");
    auto *urlEdit = new QLineEdit(&dialog);
    urlEdit->setPlaceholderText(kDefaultServerUrl);
    auto *roomEdit = new QLineEdit(kDefaultRoomName, &dialog);
    auto *passEdit = new QLineEdit(kDefaultPassphrase, &dialog);

    auto *form = new QFormLayout;
    form->addRow("Server URL", urlEdit);
    form->addRow("Room", roomEdit);
    form->addRow("Passphrase", passEdit);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->addLayout(form);
    dialogLayout->addWidget(buttons);
    dialog.resize(460, 200);
    if (dialog.exec() != QDialog::Accepted)
        return;

    ServerConfig server;
    server.url = urlEdit->text().trimmed();
    if (server.url.isEmpty())
        server.url = kDefaultServerUrl;
    server.room = roomEdit->text().trimmed().isEmpty() ? kDefaultRoomName
                                                       : roomEdit->text().trimmed();
    server.passphrase =
        passEdit->text().isEmpty() ? kDefaultPassphrase : passEdit->text();
    m_servers.append(server);
    const int newIndex = m_servers.size() - 1;
    saveServers();
    refreshServerRail();
    fetchFavicon(newIndex);
    switchToServer(newIndex);
}

void MainWindow::removeServer(int index)
{
    if (index < 0 || index >= m_servers.size() || m_servers.size() <= 1)
        return;
    if (QMessageBox::question(
            this, "Remove server",
            QStringLiteral("Stop tracking %1?").arg(serverHost(m_servers.at(index).url))) !=
        QMessageBox::Yes)
        return;

    const bool removingActive = (index == m_activeServer);
    m_servers.removeAt(index);
    if (m_activeServer > index)
        --m_activeServer;
    if (m_activeServer >= m_servers.size())
        m_activeServer = m_servers.size() - 1;
    saveServers();
    loadActiveServerIntoEdits();
    refreshServerRail();
    if (removingActive)
        startSession(); // reconnect to whichever server is now active
}

// ------------------------------------------------------------------ favicons

void MainWindow::loadCachedFavicons()
{
    for (const ServerConfig &server : std::as_const(m_servers)) {
        const QString host = serverHost(server.url);
        const QString path = faviconCachePath(host);
        QPixmap pix;
        if (QFileInfo::exists(path) && pix.load(path) && !pix.isNull())
            m_faviconCache.insert(host, pix);
    }
}

void MainWindow::fetchFavicon(int index)
{
    if (index < 0 || index >= m_servers.size())
        return;
    const QString host = serverHost(m_servers.at(index).url);
    if (host.isEmpty() || m_faviconCache.contains(host))
        return;
    const QUrl url = faviconUrl(m_servers.at(index).url);
    if (!url.isValid())
        return;

    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, host] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll()) || pix.isNull())
            return;
        if (pix.width() > 64)
            pix = pix.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_faviconCache.insert(host, pix);
        QDir().mkpath(faviconCacheDir());
        pix.save(faviconCachePath(host), "PNG");
        refreshServerRail();
    });
}

// ----------------------------------------------------------------- chat page

QWidget *MainWindow::buildChatPage()
{
    auto *page = new QWidget;

    // Section stack switched by the left navigation rail.
    m_sectionStack = new QStackedWidget;
    m_sectionStack->addWidget(buildHomeSection());     // 0 Home
    m_sectionStack->addWidget(buildReposSection());    // 1 Repos
    m_sectionStack->addWidget(buildIssuesSection());   // 2 Issues
    m_sectionStack->addWidget(buildChatSection());     // 3 Chat
    m_sectionStack->addWidget(buildSettingsSection()); // 4 Settings

    // Rails + section content live in a horizontal row.
    auto *content = new QWidget;
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(buildServerRail());
    contentLayout->addWidget(buildNavRail());
    contentLayout->addWidget(m_sectionStack, 1);

    // Global donation nudge: shown across the whole app until this node sets a
    // Bitcoin Cash address, so the network stays open to donations.
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildBchNotice());
    layout->addWidget(content, 1);
    return page;
}

QWidget *MainWindow::buildBchNotice()
{
    m_bchBanner = new QWidget;
    m_bchBanner->setObjectName("bchBanner");
    m_bchBannerLabel = new QLabel(
        "\xF0\x9F\x92\x9A Add a Bitcoin Cash address so others can sponsor this "
        "node \xE2\x80\x94 it keeps the network open to donations and more "
        "sustainable.");
    m_bchBannerLabel->setObjectName("bchBannerLabel");
    m_bchBannerLabel->setWordWrap(true);

    auto *addButton = new QPushButton("Add BCH address");
    addButton->setObjectName("primaryButton");
    addButton->setCursor(Qt::PointingHandCursor);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::promptSetBchAddress);

    auto *dismissButton = new QPushButton("\xE2\x9C\x95");
    dismissButton->setObjectName("ghostButton");
    dismissButton->setCursor(Qt::PointingHandCursor);
    dismissButton->setToolTip("Hide for now");
    connect(dismissButton, &QPushButton::clicked, m_bchBanner, &QWidget::hide);

    auto *layout = new QHBoxLayout(m_bchBanner);
    layout->setContentsMargins(16, 10, 12, 10);
    layout->setSpacing(10);
    layout->addWidget(m_bchBannerLabel, 1);
    layout->addWidget(addButton);
    layout->addWidget(dismissButton);
    m_bchBanner->hide();
    return m_bchBanner;
}

void MainWindow::updateBchNotice()
{
    if (!m_bchBanner)
        return;
    const bool hasAddress =
        !QSettings().value(kBchSetting).toString().trimmed().isEmpty();
    m_bchBanner->setVisible(!hasAddress);
}

void MainWindow::promptSetBchAddress()
{
    bool ok = false;
    const QString current = QSettings().value(kBchSetting).toString().trimmed();
    const QString address = QInputDialog::getText(
        this, "Bitcoin Cash address",
        "Enter a Bitcoin Cash address to receive donations:", QLineEdit::Normal,
        current, &ok);
    if (!ok)
        return;
    const QString trimmed = address.trimmed();
    QSettings().setValue(kBchSetting, trimmed);
    if (m_bchEdit)
        m_bchEdit->setText(trimmed);
    // The address is shared with peers on the next connect; the sponsor button
    // and donation notice pick it up immediately.
    updateBchNotice();
    updateHomeStats();
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
    auto *issuesButton = makeNavButton("\xF0\x9F\x93\x8B", "Issues");
    auto *chatButton = makeNavButton("\xF0\x9F\x92\xAC", "Chat");
    auto *settingsButton = makeNavButton("\xE2\x9A\x99", "Settings");
    auto *versionLabel = new QLabel("v" FORKMESH_VERSION);
    versionLabel->setObjectName("versionLabel");
    versionLabel->setAlignment(Qt::AlignHCenter);
    homeButton->setChecked(true);

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    m_navGroup->addButton(homeButton, 0);
    m_navGroup->addButton(reposButton, 1);
    m_navGroup->addButton(issuesButton, 2);
    m_navGroup->addButton(chatButton, 3);
    m_navGroup->addButton(settingsButton, 4);
    connect(m_navGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_sectionStack->setCurrentIndex(id);
        if (id == 0)
            updateHomeStats();
        else if (id == 2)
            refreshIssuesRepoCombo();
    });

    auto *layout = new QVBoxLayout(rail);
    layout->setContentsMargins(10, 16, 10, 16);
    layout->setSpacing(8);
    layout->addWidget(logo);
    layout->addSpacing(10);
    layout->addWidget(homeButton);
    layout->addWidget(reposButton);
    layout->addWidget(issuesButton);
    layout->addWidget(chatButton);
    layout->addStretch();
    layout->addWidget(settingsButton);

    // Tiny rebuild-and-restart button next to the version label.
    auto *rebuildMini = new QPushButton(QString::fromUtf8("\xE2\x9F\xB3"));
    rebuildMini->setObjectName("ghostButton");
    rebuildMini->setCursor(Qt::PointingHandCursor);
    rebuildMini->setFixedSize(26, 22);
    rebuildMini->setToolTip("Rebuild and restart ForkMesh");
    connect(rebuildMini, &QPushButton::clicked, this,
            &MainWindow::quickRebuildRestart);
    auto *footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->setSpacing(4);
    footerRow->addStretch();
    footerRow->addWidget(rebuildMini);
    footerRow->addWidget(versionLabel);
    footerRow->addStretch();
    layout->addLayout(footerRow);
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
    else if (index == 2)
        refreshIssuesRepoCombo();
}

QWidget *MainWindow::buildHomeSection()
{
    auto *page = new QWidget;

    auto *card = new QWidget;
    card->setObjectName("homeCard");
    card->setMaximumWidth(760);
    card->setMinimumWidth(560);

    auto *title = new QLabel("<span style='color:#22c55e'>Fork</span>Mesh");
    title->setObjectName("homeTitle");
    auto *subtitle = new QLabel("Mainnode quest board");
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
    m_homeStats->setObjectName("homeScoreValue");
    m_homeStats->setWordWrap(true);
    m_homeTotals = new QLabel;
    m_homeTotals->setObjectName("homeStat");
    m_homeTotals->setWordWrap(true);
    m_homeGraph = new QLabel;
    m_homeGraph->setObjectName("homeGraph");
    m_homeGraph->setMinimumHeight(96);
    m_homeGraph->setWordWrap(false);

    auto *scoreBoard = new QWidget;
    scoreBoard->setObjectName("homeScoreBoard");
    auto *scoreLayout = new QGridLayout(scoreBoard);
    scoreLayout->setContentsMargins(14, 12, 14, 12);
    scoreLayout->setHorizontalSpacing(14);
    scoreLayout->setVerticalSpacing(6);
    auto *scoreLabel = new QLabel("SCORE");
    scoreLabel->setObjectName("sectionLabel");
    auto *timeLabel = new QLabel("UPTIME");
    timeLabel->setObjectName("sectionLabel");
    scoreLayout->addWidget(scoreLabel, 0, 0);
    scoreLayout->addWidget(timeLabel, 0, 1);
    scoreLayout->addWidget(m_homeStats, 1, 0);
    scoreLayout->addWidget(m_homeTotals, 1, 1);
    scoreLayout->setColumnStretch(0, 1);
    scoreLayout->setColumnStretch(1, 1);

    auto *graphLabel = new QLabel("MINUTE GRAPH");
    graphLabel->setObjectName("sectionLabel");
    auto *nodesLabel = new QLabel("LEADERBOARD");
    nodesLabel->setObjectName("sectionLabel");
    m_homeNodes = new QLabel("No nodes connected yet.");
    m_homeNodes->setObjectName("homeStat");
    m_homeNodeList = new QListWidget;
    m_homeNodeList->setSelectionMode(QAbstractItemView::NoSelection);
    m_homeNodeList->setFocusPolicy(Qt::NoFocus);
    m_homeNodeList->setMinimumHeight(120);
    m_homeNodeList->setToolTip("Live connection status of nodes in this room");

    m_homeSponsorButton = new QPushButton("Sponsor this node");
    m_homeSponsorButton->setObjectName("primaryButton");
    m_homeSponsorButton->setCursor(Qt::PointingHandCursor);
    connect(m_homeSponsorButton, &QPushButton::clicked, this, [this] {
        const QString url = sponsorUrlFor(QSettings().value(kBchSetting).toString());
        if (!url.isEmpty())
            QDesktopServices::openUrl(QUrl(url));
    });
    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->addWidget(m_homeSponsorButton);
    actionRow->addStretch();

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
    cardLayout->addWidget(scoreBoard);
    cardLayout->addSpacing(6);
    cardLayout->addWidget(graphLabel);
    cardLayout->addWidget(m_homeGraph);
    cardLayout->addSpacing(12);
    cardLayout->addWidget(nodesLabel);
    cardLayout->addWidget(m_homeNodes);
    cardLayout->addWidget(m_homeNodeList);
    cardLayout->addSpacing(10);
    cardLayout->addLayout(actionRow);

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
                    showSection(3); // jump to the chat for this repo
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

// ---- Issues section --------------------------------------------------------

QWidget *MainWindow::buildIssuesSection()
{
    auto *page = new QWidget;

    // Left: repo picker, filters, issue list.
    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(320);

    auto *heading = new QLabel("Issues");
    heading->setObjectName("channelTitle");

    m_issuesRepoCombo = new QComboBox;
    m_issuesRepoCombo->setToolTip("Repository whose issues you are viewing");

    m_issueStatusFilter = new QComboBox;
    m_issueStatusFilter->addItems({"Open", "Closed", "All"});
    m_issueLabelFilter = new QComboBox;
    m_issueMilestoneFilter = new QComboBox;
    auto *filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->addWidget(m_issueStatusFilter);
    filterRow->addWidget(m_issueLabelFilter);
    filterRow->addWidget(m_issueMilestoneFilter);

    m_issueNewButton = new QPushButton("+ New issue");
    m_issueNewButton->setObjectName("ghostButton");
    m_issueNewButton->setCursor(Qt::PointingHandCursor);
    m_issueSyncButton = new QPushButton("Sync inbox");
    m_issueSyncButton->setObjectName("ghostButton");
    m_issueSyncButton->setCursor(Qt::PointingHandCursor);
    m_issueSyncButton->setToolTip(
        "Pull issue/comment submissions filed by other nodes and merge them");
    auto *newRow = new QHBoxLayout;
    newRow->setContentsMargins(0, 0, 0, 0);
    newRow->addWidget(m_issueNewButton);
    newRow->addWidget(m_issueSyncButton);

    m_issueList = new QListWidget;
    m_issueList->setToolTip("Issues in this repository");

    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(18, 18, 18, 18);
    sidebarLayout->setSpacing(8);
    sidebarLayout->addWidget(heading);
    sidebarLayout->addWidget(m_issuesRepoCombo);
    sidebarLayout->addLayout(filterRow);
    sidebarLayout->addLayout(newRow);
    sidebarLayout->addWidget(m_issueList, 1);

    // Center: the selected issue's title, status badge, thread and composer.
    m_issueTitle = new QLabel("Select an issue");
    m_issueTitle->setObjectName("channelTitle");
    m_issueTitle->setWordWrap(true);
    m_issueMeta = new QLabel; // status badge
    m_issueMeta->setObjectName("statusLine");
    m_issueMeta->setWordWrap(true);
    m_issueMeta->setTextFormat(Qt::RichText);
    m_issueReadonlyNote = new QLabel;
    m_issueReadonlyNote->setObjectName("statusLine");
    m_issueReadonlyNote->setWordWrap(true);
    m_issueReadonlyNote->hide();

    m_issueThreadContainer = new QWidget;
    m_issueThreadLayout = new QVBoxLayout(m_issueThreadContainer);
    m_issueThreadLayout->setContentsMargins(0, 0, 0, 0);
    m_issueThreadLayout->setSpacing(10);
    m_issueThreadLayout->addStretch();
    m_issueThreadScroll = new QScrollArea;
    m_issueThreadScroll->setWidgetResizable(true);
    m_issueThreadScroll->setWidget(m_issueThreadContainer);
    m_issueThreadScroll->setObjectName("messageScroll");

    m_issueComposer = new QPlainTextEdit;
    m_issueComposer->setPlaceholderText("Write a comment\xE2\x80\xA6");
    m_issueComposer->setFixedHeight(80);
    m_issueAttachButton = new QPushButton("Attach image");
    m_issueCloseButton = new QPushButton("Close issue");
    m_issueCommentButton = new QPushButton("Comment");
    m_issueCommentButton->setObjectName("primaryButton");
    m_issueCommentButton->setCursor(Qt::PointingHandCursor);
    for (QPushButton *b : {m_issueAttachButton, m_issueCloseButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    auto *composerButtons = new QVBoxLayout;
    composerButtons->addWidget(m_issueAttachButton);
    composerButtons->addWidget(m_issueCommentButton);
    composerButtons->addWidget(m_issueCloseButton);
    auto *composerRow = new QHBoxLayout;
    composerRow->setContentsMargins(0, 0, 0, 0);
    composerRow->addWidget(m_issueComposer, 1);
    composerRow->addLayout(composerButtons);

    auto *center = new QWidget;
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(22, 18, 16, 18);
    centerLayout->setSpacing(8);
    centerLayout->addWidget(m_issueTitle);
    centerLayout->addWidget(m_issueMeta);
    centerLayout->addWidget(m_issueReadonlyNote);
    centerLayout->addWidget(m_issueThreadScroll, 1);
    centerLayout->addLayout(composerRow);

    // Right: GitHub-style metadata sidebar (assignees, labels, milestone).
    auto *meta = new QWidget;
    meta->setObjectName("sidebar");
    meta->setFixedWidth(230);
    m_issueAssigneesValue = new QLabel("No one assigned");
    m_issueLabelsValue = new QLabel("None yet");
    m_issueMilestoneValue = new QLabel("No milestone");
    for (QLabel *v : {m_issueAssigneesValue, m_issueLabelsValue, m_issueMilestoneValue}) {
        v->setObjectName("statusLine");
        v->setWordWrap(true);
        v->setTextFormat(Qt::RichText);
    }
    m_issueLabelsButton = new QPushButton("Edit");
    m_issueMilestoneButton = new QPushButton("Edit");
    m_issueAssigneesButton = new QPushButton("Edit");
    m_issueDeleteButton = new QPushButton("Delete issue");
    for (QPushButton *b : {m_issueLabelsButton, m_issueMilestoneButton,
                           m_issueAssigneesButton, m_issueDeleteButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    auto *metaLayout = new QVBoxLayout(meta);
    metaLayout->setContentsMargins(16, 18, 16, 18);
    metaLayout->setSpacing(6);
    auto addMetaSection = [&](const QString &label, QLabel *value, QPushButton *btn) {
        auto *header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        auto *l = new QLabel(label);
        l->setObjectName("sectionLabel");
        header->addWidget(l);
        header->addStretch();
        btn->setMaximumWidth(60);
        header->addWidget(btn);
        metaLayout->addLayout(header);
        metaLayout->addWidget(value);
        metaLayout->addSpacing(10);
    };
    addMetaSection("ASSIGNEES", m_issueAssigneesValue, m_issueAssigneesButton);
    addMetaSection("LABELS", m_issueLabelsValue, m_issueLabelsButton);
    addMetaSection("MILESTONE", m_issueMilestoneValue, m_issueMilestoneButton);
    metaLayout->addStretch();
    metaLayout->addWidget(m_issueDeleteButton);

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sidebar);
    layout->addWidget(center, 1);
    layout->addWidget(meta);

    connect(m_issuesRepoCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { reloadIssues(); });
    connect(m_issueStatusFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshIssueList(); });
    connect(m_issueLabelFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshIssueList(); });
    connect(m_issueMilestoneFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshIssueList(); });
    connect(m_issueList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    showIssue(item->data(Qt::UserRole).toInt());
            });
    connect(m_issueNewButton, &QPushButton::clicked, this, &MainWindow::promptNewIssue);
    connect(m_issueSyncButton, &QPushButton::clicked, this,
            &MainWindow::syncIssuesInbox);
    connect(m_issueCommentButton, &QPushButton::clicked, this,
            &MainWindow::addIssueComment);
    connect(m_issueAttachButton, &QPushButton::clicked, this,
            &MainWindow::attachIssueImage);
    connect(m_issueCloseButton, &QPushButton::clicked, this,
            &MainWindow::toggleIssueStatus);
    connect(m_issueDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteCurrentIssue);
    connect(m_issueLabelsButton, &QPushButton::clicked, this,
            &MainWindow::editIssueLabels);
    connect(m_issueMilestoneButton, &QPushButton::clicked, this,
            &MainWindow::editIssueMilestone);
    connect(m_issueAssigneesButton, &QPushButton::clicked, this,
            &MainWindow::editIssueAssignees);
    return page;
}

int MainWindow::issuesRepoIndex() const
{
    if (!m_issuesRepoCombo || m_issuesRepoCombo->currentIndex() < 0)
        return -1;
    bool ok = false;
    const int idx = m_issuesRepoCombo->currentData().toInt(&ok);
    if (!ok || idx < 0 || idx >= m_repositories.size())
        return -1;
    return idx;
}

IssueStore MainWindow::issueStoreForCurrentRepo() const
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return IssueStore(QString(), QString(), &m_profileIdentity, m_userName);
    const RepositoryRecord &repo = m_repositories.at(idx);
    return IssueStore(repo.localPath, repo.mirrorPath, &m_profileIdentity, m_userName);
}

void MainWindow::refreshIssuesRepoCombo()
{
    if (!m_issuesRepoCombo)
        return;
    const QVariant previous =
        m_issuesRepoCombo->count() ? m_issuesRepoCombo->currentData() : QVariant();
    QSignalBlocker blocker(m_issuesRepoCombo);
    m_issuesRepoCombo->clear();
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        m_issuesRepoCombo->addItem(repo.owner + "/" + repo.name, i);
    }
    if (previous.isValid()) {
        const int restore = m_issuesRepoCombo->findData(previous);
        if (restore >= 0)
            m_issuesRepoCombo->setCurrentIndex(restore);
    }
    blocker.unblock();
    reloadIssues();
}

void MainWindow::reloadIssues()
{
    if (!m_issueList)
        return;
    if (issuesRepoIndex() < 0) {
        m_currentIssues.clear();
        m_currentLabels.clear();
        m_currentMilestones.clear();
        m_issueList->clear();
        m_currentIssueNumber = -1;
        renderIssueThread(Issue());
        updateIssueActionState();
        return;
    }
    const IssueStore store = issueStoreForCurrentRepo();
    m_currentIssues = store.loadAll();
    m_currentLabels = store.loadLabels();
    m_currentMilestones = store.loadMilestones();

    QSignalBlocker labelBlock(m_issueLabelFilter);
    m_issueLabelFilter->clear();
    m_issueLabelFilter->addItem("All labels", QString());
    for (const IssueLabel &label : m_currentLabels)
        m_issueLabelFilter->addItem(label.name, label.name);
    labelBlock.unblock();

    QSignalBlocker msBlock(m_issueMilestoneFilter);
    m_issueMilestoneFilter->clear();
    m_issueMilestoneFilter->addItem("All milestones", QString());
    for (const IssueMilestone &ms : m_currentMilestones)
        m_issueMilestoneFilter->addItem(ms.title, ms.title);
    msBlock.unblock();

    refreshIssueList();
    updateIssueActionState();
}

void MainWindow::refreshIssueList()
{
    if (!m_issueList)
        return;
    const QString statusFilter = m_issueStatusFilter->currentText();
    const QString labelFilter = m_issueLabelFilter->currentData().toString();
    const QString msFilter = m_issueMilestoneFilter->currentData().toString();
    const int keep = m_currentIssueNumber;

    m_issueList->clear();
    int rowToSelect = -1;
    for (const Issue &issue : m_currentIssues) {
        if (statusFilter == "Open" && issue.status != "open")
            continue;
        if (statusFilter == "Closed" && issue.status != "closed")
            continue;
        if (!labelFilter.isEmpty() && !issue.labels.contains(labelFilter))
            continue;
        if (!msFilter.isEmpty() && issue.milestone != msFilter)
            continue;
        QString text = QStringLiteral("#%1  %2").arg(issue.number).arg(issue.title);
        if (issue.status == "closed")
            text += "  \xE2\x9C\x94"; // check mark
        if (!issue.labels.isEmpty())
            text += "\n   " + issue.labels.join(", ");
        auto *item = new QListWidgetItem(text, m_issueList);
        item->setData(Qt::UserRole, issue.number);
        if (issue.number == keep)
            rowToSelect = m_issueList->count() - 1;
    }
    if (rowToSelect >= 0)
        m_issueList->setCurrentRow(rowToSelect);
    else if (m_issueList->count() > 0)
        m_issueList->setCurrentRow(0);
    else {
        m_currentIssueNumber = -1;
        renderIssueThread(Issue());
        updateIssueActionState();
    }
}

void MainWindow::showIssue(int number)
{
    for (const Issue &issue : m_currentIssues) {
        if (issue.number == number) {
            m_currentIssueNumber = number;
            renderIssueThread(issue);
            updateIssueActionState();
            return;
        }
    }
}

void MainWindow::renderIssueThread(const Issue &issue)
{
    // Clear all cards (keep the trailing stretch rebuilt at the end).
    while (QLayoutItem *item = m_issueThreadLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    if (issue.number == 0) {
        m_issueTitle->setText("Select an issue");
        m_issueMeta->clear();
        if (m_issueAssigneesValue)
            m_issueAssigneesValue->setText("No one assigned");
        if (m_issueLabelsValue)
            m_issueLabelsValue->setText("None yet");
        if (m_issueMilestoneValue)
            m_issueMilestoneValue->setText("No milestone");
        m_issueThreadLayout->addStretch();
        return;
    }

    m_issueTitle->setText(QStringLiteral("#%1  %2").arg(issue.number).arg(issue.title));

    // Status badge stays next to the title; labels/milestone/assignees live in
    // the GitHub-style right sidebar.
    m_issueMeta->setText(issue.status == "closed"
                             ? QStringLiteral("<b style='color:#ef4444'>\xE2\x97\x8F closed</b>")
                             : QStringLiteral("<b style='color:#22c55e'>\xE2\x97\x8F open</b>"));

    auto colorFor = [this](const QString &name) -> QString {
        for (const IssueLabel &l : m_currentLabels)
            if (l.name == name && !l.color.isEmpty())
                return l.color;
        return QStringLiteral("#94a3b8");
    };
    if (issue.assignees.isEmpty()) {
        m_issueAssigneesValue->setText("No one assigned");
    } else {
        QStringList shown;
        for (const QString &a : issue.assignees)
            shown << a.left(16).toHtmlEscaped();
        m_issueAssigneesValue->setText(shown.join("<br>"));
    }
    if (issue.labels.isEmpty()) {
        m_issueLabelsValue->setText("None yet");
    } else {
        QStringList chips;
        for (const QString &name : issue.labels)
            chips << QStringLiteral("<span style='color:%1'>\xE2\x97\x8F %2</span>")
                         .arg(colorFor(name), name.toHtmlEscaped());
        m_issueLabelsValue->setText(chips.join("<br>"));
    }
    m_issueMilestoneValue->setText(
        issue.milestone.isEmpty()
            ? QStringLiteral("No milestone")
            : QStringLiteral("<b>%1</b>").arg(issue.milestone.toHtmlEscaped()));

    // Pre-compute edits (target -> latest edit) and deletions.
    QHash<QString, IssueEvent> edits;
    QSet<QString> deleted;
    for (const IssueEvent &ev : issue.events) {
        if (ev.type == "edit" && !ev.target.isEmpty())
            edits.insert(ev.target, ev); // later edits overwrite
        else if (ev.type == "delete" && !ev.target.isEmpty() && ev.target != "self")
            deleted.insert(ev.target);
    }

    const int idx = issuesRepoIndex();
    const QString imageBase =
        idx >= 0 ? m_repositories.at(idx).localPath + "/issues/" +
                       QString::number(issue.number) + "/"
                 : QString();
    const bool haveLocalFiles = !imageBase.isEmpty() &&
                                QFileInfo::exists(imageBase + "issue.md");

    auto addCard = [&](const IssueEvent &ev, bool isOpen) {
        IssueEvent shown = ev;
        if (edits.contains(ev.id)) {
            shown.body = edits.value(ev.id).body;
            shown.attachments = edits.value(ev.id).attachments;
        }
        auto *card = new QWidget;
        card->setObjectName("homeCard");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 10, 14, 12);
        cardLayout->setSpacing(6);
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        const QString when =
            QDateTime::fromMSecsSinceEpoch(ev.ts).toString("yyyy-MM-dd HH:mm");
        auto *header = new QLabel(
            QStringLiteral("<b style='color:%1'>%2</b> <span style='color:#94a3b8'>%3%4</span>")
                .arg(senderColor(who), who.toHtmlEscaped(), when,
                     isOpen ? QStringLiteral(" \xC2\xB7 opened") : QString()));
        header->setTextFormat(Qt::RichText);
        cardLayout->addWidget(header);
        auto *body = new QLabel;
        body->setTextFormat(Qt::MarkdownText);
        body->setText(shown.body);
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextBrowserInteraction);
        body->setOpenExternalLinks(true);
        cardLayout->addWidget(body);
        for (const QString &rel : shown.attachments) {
            if (haveLocalFiles) {
                QPixmap pix(imageBase + rel);
                if (!pix.isNull()) {
                    auto *img = new QLabel;
                    img->setPixmap(pix.width() > 420
                                       ? pix.scaledToWidth(420, Qt::SmoothTransformation)
                                       : pix);
                    cardLayout->addWidget(img);
                    continue;
                }
            }
            auto *placeholder = new QLabel(QStringLiteral("\xF0\x9F\x96\xBC %1").arg(rel));
            placeholder->setObjectName("statusLine");
            cardLayout->addWidget(placeholder);
        }
        m_issueThreadLayout->addWidget(card);
    };

    auto addActivity = [&](const QString &text, qint64 ts, const QString &who) {
        const QString when = QDateTime::fromMSecsSinceEpoch(ts).toString("HH:mm");
        auto *line = new QLabel(QStringLiteral("\xC2\xB7 %1 %2 (%3)")
                                    .arg(who.toHtmlEscaped(), text, when));
        line->setObjectName("statusLine");
        line->setWordWrap(true);
        m_issueThreadLayout->addWidget(line);
    };

    for (const IssueEvent &ev : issue.events) {
        const QString who = ev.authorName.isEmpty() ? ev.author.left(10) : ev.authorName;
        if (ev.type == "open")
            addCard(ev, true);
        else if (ev.type == "comment") {
            if (!deleted.contains(ev.id))
                addCard(ev, false);
        } else if (ev.type == "status")
            addActivity(ev.status == "closed" ? "closed this" : "reopened this", ev.ts, who);
        else if (ev.type == "labels")
            addActivity("set labels: " + ev.labels.join(", "), ev.ts, who);
        else if (ev.type == "milestone")
            addActivity(ev.milestone.isEmpty() ? "cleared the milestone"
                                               : "set milestone: " + ev.milestone,
                        ev.ts, who);
        else if (ev.type == "assignees")
            addActivity("set assignees: " + ev.assignees.join(", "), ev.ts, who);
    }
    m_issueThreadLayout->addStretch();
}

void MainWindow::updateIssueActionState()
{
    const IssueStore store = issueStoreForCurrentRepo();
    const bool writable = store.canWrite();
    const bool haveIssue = m_currentIssueNumber >= 0;

    if (m_issueNewButton)
        m_issueNewButton->setEnabled(writable);
    if (m_issueSyncButton)
        m_issueSyncButton->setEnabled(writable);
    // Owner-only structural edits.
    for (QPushButton *b : {m_issueCloseButton, m_issueLabelsButton,
                           m_issueMilestoneButton, m_issueAssigneesButton,
                           m_issueDeleteButton, m_issueAttachButton}) {
        if (b)
            b->setEnabled(writable && haveIssue);
    }
    // Comments work for everyone with an issue selected: owners write locally,
    // others submit a signed comment to the relay inbox.
    if (m_issueCommentButton)
        m_issueCommentButton->setEnabled(haveIssue);
    if (m_issueComposer)
        m_issueComposer->setEnabled(haveIssue);

    // Reflect current status on the close/reopen button.
    if (m_issueCloseButton && haveIssue) {
        for (const Issue &issue : m_currentIssues) {
            if (issue.number == m_currentIssueNumber) {
                m_issueCloseButton->setText(issue.status == "closed" ? "Reopen"
                                                                     : "Close issue");
                break;
            }
        }
    }
    if (m_issueReadonlyNote) {
        m_issueReadonlyNote->setVisible(!writable && issuesRepoIndex() >= 0);
        m_issueReadonlyNote->setText(
            "You don't host this repository \xE2\x80\x94 comments are sent to the "
            "maintainer's inbox (text only). New issues and edits are owner-only.");
    }
    if (m_issueCommentButton)
        m_issueCommentButton->setText(writable ? "Comment" : "Send to maintainer");
}

void MainWindow::promptNewIssue()
{
    const IssueStore probe = issueStoreForCurrentRepo();
    if (!probe.canWrite())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle("New issue");
    auto *titleEdit = new QLineEdit(&dialog);
    titleEdit->setPlaceholderText("Title");
    auto *bodyEdit = new QPlainTextEdit(&dialog);
    bodyEdit->setPlaceholderText("Describe the issue (markdown supported)\xE2\x80\xA6");
    auto *labelsEdit = new QLineEdit(&dialog);
    labelsEdit->setPlaceholderText("labels (comma separated)");
    auto *milestoneCombo = new QComboBox(&dialog);
    milestoneCombo->addItem("(no milestone)", QString());
    for (const IssueMilestone &ms : m_currentMilestones)
        milestoneCombo->addItem(ms.title, ms.title);

    auto *form = new QFormLayout;
    form->addRow("Title", titleEdit);
    form->addRow("Body", bodyEdit);
    form->addRow("Labels", labelsEdit);
    form->addRow("Milestone", milestoneCombo);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->addLayout(form);
    dialogLayout->addWidget(buttons);
    dialog.resize(520, 420);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString title = titleEdit->text().trimmed();
    if (title.isEmpty()) {
        QMessageBox::warning(this, "New issue", "A title is required.");
        return;
    }
    QStringList labels;
    for (const QString &part : labelsEdit->text().split(',', Qt::SkipEmptyParts))
        labels << part.trimmed();

    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    const int number = store.createIssue(title, bodyEdit->toPlainText(), labels,
                                         milestoneCombo->currentData().toString(),
                                         {}, {}, &error);
    if (number < 0) {
        QMessageBox::warning(this, "New issue", error);
        return;
    }
    m_currentIssueNumber = number;
    reloadIssues();
}

void MainWindow::addIssueComment()
{
    if (m_currentIssueNumber < 0)
        return;
    const QString body = m_issueComposer ? m_issueComposer->toPlainText() : QString();
    if (body.trimmed().isEmpty() && m_pendingIssueAttachments.isEmpty())
        return;
    IssueStore store = issueStoreForCurrentRepo();
    if (!store.canWrite()) {
        // Not the host: send a signed comment to the maintainer's relay inbox.
        submitIssueCommentToInbox(body);
        return;
    }
    QString error;
    if (!store.addComment(m_currentIssueNumber, body, m_pendingIssueAttachments, &error)) {
        QMessageBox::warning(this, "Comment", error);
        return;
    }
    m_issueComposer->clear();
    m_pendingIssueAttachments.clear();
    if (m_issueAttachButton)
        m_issueAttachButton->setText("Attach image");
    reloadIssues();
}

void MainWindow::attachIssueImage()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Attach images", QString(),
        "Images (*.png *.jpg *.jpeg *.gif *.webp);;All files (*)");
    if (files.isEmpty())
        return;
    m_pendingIssueAttachments += files;
    if (m_issueAttachButton)
        m_issueAttachButton->setText(
            QStringLiteral("Attached: %1").arg(m_pendingIssueAttachments.size()));
}

void MainWindow::toggleIssueStatus()
{
    if (m_currentIssueNumber < 0)
        return;
    QString status = "open";
    for (const Issue &issue : m_currentIssues)
        if (issue.number == m_currentIssueNumber)
            status = issue.status;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setStatus(m_currentIssueNumber, status == "open" ? "closed" : "open",
                         &error)) {
        QMessageBox::warning(this, "Issue", error);
        return;
    }
    reloadIssues();
}

void MainWindow::deleteCurrentIssue()
{
    if (m_currentIssueNumber < 0)
        return;
    if (QMessageBox::question(
            this, "Delete issue",
            QStringLiteral("Delete issue #%1? This removes its folder and commits.")
                .arg(m_currentIssueNumber)) != QMessageBox::Yes)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.deleteIssue(m_currentIssueNumber, &error)) {
        QMessageBox::warning(this, "Delete issue", error);
        return;
    }
    m_currentIssueNumber = -1;
    reloadIssues();
}

void MainWindow::editIssueLabels()
{
    if (m_currentIssueNumber < 0)
        return;
    QStringList current;
    for (const Issue &issue : m_currentIssues)
        if (issue.number == m_currentIssueNumber)
            current = issue.labels;

    QDialog dialog(this);
    dialog.setWindowTitle("Labels");
    auto *list = new QListWidget(&dialog);
    for (const IssueLabel &label : m_currentLabels) {
        auto *item = new QListWidgetItem(label.name, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(current.contains(label.name) ? Qt::Checked : Qt::Unchecked);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->addWidget(new QLabel("Select labels for this issue:"));
    dialogLayout->addWidget(list);
    dialogLayout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QStringList chosen;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            chosen << list->item(i)->text();
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setLabels(m_currentIssueNumber, chosen, &error))
        QMessageBox::warning(this, "Labels", error);
    reloadIssues();
}

void MainWindow::editIssueMilestone()
{
    if (m_currentIssueNumber < 0)
        return;
    QStringList options{"(no milestone)"};
    for (const IssueMilestone &ms : m_currentMilestones)
        options << ms.title;
    QString current;
    for (const Issue &issue : m_currentIssues)
        if (issue.number == m_currentIssueNumber)
            current = issue.milestone;
    int currentIndex = current.isEmpty() ? 0 : options.indexOf(current);
    if (currentIndex < 0)
        currentIndex = 0;
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, "Milestone", "Milestone:",
                                                 options, currentIndex, false, &ok);
    if (!ok)
        return;
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setMilestone(m_currentIssueNumber,
                            choice == "(no milestone)" ? QString() : choice, &error))
        QMessageBox::warning(this, "Milestone", error);
    reloadIssues();
}

void MainWindow::editIssueAssignees()
{
    if (m_currentIssueNumber < 0)
        return;
    QString current;
    for (const Issue &issue : m_currentIssues)
        if (issue.number == m_currentIssueNumber)
            current = issue.assignees.join(", ");
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, "Assignees", "Assignees (comma separated names or pubkeys):",
        QLineEdit::Normal, current, &ok);
    if (!ok)
        return;
    QStringList assignees;
    for (const QString &part : text.split(',', Qt::SkipEmptyParts))
        assignees << part.trimmed();
    IssueStore store = issueStoreForCurrentRepo();
    QString error;
    if (!store.setAssignees(m_currentIssueNumber, assignees, &error))
        QMessageBox::warning(this, "Assignees", error);
    reloadIssues();
}

QUrl MainWindow::issuesApiUrl(const RepositoryRecord &repo) const
{
    QUrl url = catalogApiUrl();
    url.setPath("/api/repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                "/" + repoSegment(repo.name, QStringLiteral("repository")) +
                "/issues");
    return url;
}

void MainWindow::submitIssueCommentToInbox(const QString &body)
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);

    QString text = body;
    while (text.endsWith('\n') || text.endsWith('\r'))
        text.chop(1);

    IssueStore store = issueStoreForCurrentRepo();
    IssueEvent ev;
    ev.type = "comment";
    ev.body = text;
    ev = store.makeSignedEvent(m_currentIssueNumber, ev);
    // bodyFile isn't part of the signature; name it after the (now-assigned) id
    // so the maintainer's node stores it predictably.
    ev.bodyFile = "comments/" + ev.id + ".md";

    QJsonObject eventJson = ev.toJson();
    eventJson.insert("body", ev.body); // worker needs the text to verify the sig
    const QJsonObject payload{{"owner", repo.owner},
                              {"repo", repo.name},
                              {"number", m_currentIssueNumber},
                              {"event", eventJson}};

    QNetworkRequest request(issuesApiUrl(repo));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            if (m_issueComposer)
                m_issueComposer->clear();
            QMessageBox::information(
                this, "Comment sent",
                "Your signed comment was delivered to the maintainer's inbox.");
        } else {
            QMessageBox::warning(this, "Comment",
                                 "Could not send the comment: " + reply->errorString());
        }
    });
}

void MainWindow::syncIssuesInbox()
{
    const int idx = issuesRepoIndex();
    if (idx < 0)
        return;
    const RepositoryRecord &repo = m_repositories.at(idx);
    if (!issueStoreForCurrentRepo().canWrite())
        return;

    const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-issues-pull-v1\n" + owner + "\n" + ts).toUtf8();
    const QString sig = m_profileIdentity.signData(canonical);

    QUrl url = issuesApiUrl(repo);
    QUrlQuery query;
    query.addQueryItem("owner", owner);
    query.addQueryItem("ts", ts);
    query.addQueryItem("sig", sig);
    url.setQuery(query);

    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, repo, url] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "Sync inbox",
                                 "Could not reach the inbox: " + reply->errorString());
            return;
        }
        const QJsonObject root =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray pending = root.value("pending").toArray();
        if (pending.isEmpty()) {
            QMessageBox::information(this, "Sync inbox", "No pending submissions.");
            return;
        }
        IssueStore store = issueStoreForCurrentRepo();
        int merged = 0;
        for (const QJsonValue &value : pending) {
            const QJsonObject item = value.toObject();
            const int number = item.value("number").toInt();
            const QJsonObject eventObj = item.value("event").toObject();
            IssueEvent ev = IssueEvent::fromJson(eventObj);
            ev.body = eventObj.value("body").toString();
            if (store.applyRemoteEvent(number, ev,
                                       item.value("titleIfNew").toString()))
                ++merged;
        }
        // Acknowledge so the inbox clears the merged submissions.
        m_networkAccess->deleteResource(QNetworkRequest(url));
        reloadIssues();
        QMessageBox::information(
            this, "Sync inbox",
            QStringLiteral("Merged %1 submission(s) into issues/.").arg(merged));
    });
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
    sidebarLayout->addStretch();

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
    connect(settingsButton, &QPushButton::clicked, this, [this] { showSection(4); });
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
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 sessionMs = m_connectedAtMs > 0 ? now - m_connectedAtMs : 0;
    const qint64 totalMs = m_totalConnectionMs + sessionMs;
    if (m_connectedAtMs > 0)
        QSettings().setValue(kConnectionTotalSetting, totalMs);

    const QString name = m_userName.isEmpty()
                             ? defaultDisplayName(m_profileIdentity)
                             : m_userName;
    m_homeName->setText(name);
    const QString key = m_profileIdentity.shortPublicKey();
    m_homePubkey->setText(key.isEmpty()
                              ? QStringLiteral("Ed25519 key: generating\xE2\x80\xA6")
                              : "Ed25519 key: " + key);
    int mirroredRepos = 0;
    int onlineRepos = 0;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.lastSyncMs > 0 || (!repo.mirrorPath.isEmpty() &&
                                    QDir(repo.mirrorPath).exists()))
            ++mirroredRepos;
        if (repo.publishedAtMs > 0 || repo.publishToNetwork)
            ++onlineRepos;
    }
    const int repoCount = m_repositories.size();
    const int chatCount = m_channels.size();
    const int dmCount = m_openDms.size();
    int onlineNodes = 0;
    for (const MemberInfo &member : std::as_const(m_homeRoster)) {
        if (member.online)
            ++onlineNodes;
    }
    if (m_connectedAtMs > 0) {
        const int minute = int(sessionMs / 60000);
        while (m_connectionMinuteSamples.size() <= minute)
            m_connectionMinuteSamples.append(onlineNodes);
        m_connectionMinuteSamples[minute] = onlineNodes;
    }

    m_homeStats->setText(
        QString::number(repoCount) + " repos\n" +
        QString::number(mirroredRepos) + " mirrored  |  " +
        QString::number(onlineRepos) + " online\n" +
        QString::number(chatCount) + " chats  |  " +
        QString::number(dmCount) + " DMs");
    if (m_homeTotals) {
        m_homeTotals->setText(
            "Session " + formatDuration(sessionMs) + "\nTotal " +
            formatDuration(totalMs) + "\n" +
            QString::number(onlineNodes) + " / " +
            QString::number(m_homeRoster.size()) + " nodes online");
    }
    if (m_homeGraph)
        m_homeGraph->setText(connectionGraphText(m_connectionMinuteSamples));
    if (m_homeNodes) {
        m_homeNodes->setText(
            m_homeRoster.isEmpty()
                ? QStringLiteral("No nodes connected yet.")
                : QString::number(onlineNodes) + " of " +
                      QString::number(m_homeRoster.size()) +
                      (m_homeRoster.size() == 1 ? " node online"
                                                : " nodes online"));
    }
    if (m_homeSponsorButton) {
        const QString bch = QSettings().value(kBchSetting).toString().trimmed();
        m_homeSponsorButton->setEnabled(!bch.isEmpty());
        m_homeSponsorButton->setText(bch.isEmpty() ? "No BCH address yet"
                                                   : "Sponsor this node");
        m_homeSponsorButton->setToolTip(bch.isEmpty()
                                            ? "Add a BCH address on the start screen."
                                            : sponsorUrlFor(bch));
    }
}

// ----------------------------------------------------------------- settings

QWidget *MainWindow::buildSettingsSection()
{
    auto *page = new QWidget;

    auto *title = new QLabel("Settings");
    title->setObjectName("settingsTitle");

    auto *profileLabel = new QLabel("PROFILE");
    profileLabel->setObjectName("sectionLabel");

    m_settingsNameEdit = new QLineEdit;
    m_settingsNameEdit->setMaxLength(32);
    m_settingsNameEdit->setPlaceholderText("Display name");
    connect(m_settingsNameEdit, &QLineEdit::editingFinished, this,
            [this] { onDisplayNameChanged(m_settingsNameEdit->text()); });

    m_settingsAvatarPreview = new QLabel("No\navatar");
    m_settingsAvatarPreview->setObjectName("avatarPreview");
    m_settingsAvatarPreview->setFixedSize(64, 64);
    m_settingsAvatarPreview->setAlignment(Qt::AlignCenter);
    auto *uploadButton = new QPushButton("Upload avatar…");
    uploadButton->setObjectName("ghostButton");
    uploadButton->setCursor(Qt::PointingHandCursor);
    connect(uploadButton, &QPushButton::clicked, this, &MainWindow::chooseAvatar);
    auto *avatarRow = new QHBoxLayout;
    avatarRow->setSpacing(12);
    avatarRow->addWidget(m_settingsAvatarPreview);
    avatarRow->addWidget(uploadButton);
    avatarRow->addStretch();

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setSpacing(8);
    form->addRow("Display name", m_settingsNameEdit);
    form->addRow("Avatar", avatarRow);

    auto *startupLabel = new QLabel("STARTUP");
    startupLabel->setObjectName("sectionLabel");
    m_autostartCheck = new QCheckBox("Launch ForkMesh at login");
    m_autostartCheck->setChecked(isAutostartEnabled());
    m_autostartCheck->setToolTip(
        "Start ForkMesh automatically when you log in to this computer.");
    connect(m_autostartCheck, &QCheckBox::toggled, this, [](bool enabled) {
        setAutostartEnabled(enabled);
    });

    auto *maintLabel = new QLabel("MAINTENANCE");
    maintLabel->setObjectName("sectionLabel");
    m_rebuildButton = new QPushButton("\xE2\x9F\xB3 Clear cache & rebuild");
    m_rebuildButton->setObjectName("ghostButton");
    m_rebuildButton->setCursor(Qt::PointingHandCursor);
    m_rebuildButton->setToolTip(
        "Delete the build cache, rebuild from scratch, and relaunch");
    connect(m_rebuildButton, &QPushButton::clicked, this,
            &MainWindow::rebuildAndRelaunch);
    m_rebuildStatus = new QLabel;
    m_rebuildStatus->setObjectName("modeHint");
    m_rebuildStatus->setWordWrap(true);
    m_rebuildStatus->hide();

    // Mirror storage location: where bare mirrors of repos are kept. Mirrors act
    // as the local "remote" a fork pushes to (see issue: fork from the client).
    auto *storageLabel = new QLabel("MIRROR STORAGE");
    storageLabel->setObjectName("sectionLabel");
    m_mirrorRootEdit = new QLineEdit(repositoryMirrorRoot());
    m_mirrorRootEdit->setReadOnly(true);
    m_mirrorRootEdit->setToolTip(
        "Folder where mirrored repositories are stored. New mirrors are created "
        "here; a local fork pushes into its mirror.");
    auto *mirrorChangeButton = new QPushButton("Change\xE2\x80\xA6");
    mirrorChangeButton->setObjectName("ghostButton");
    mirrorChangeButton->setCursor(Qt::PointingHandCursor);
    connect(mirrorChangeButton, &QPushButton::clicked, this,
            &MainWindow::changeMirrorLocation);
    auto *mirrorRow = new QHBoxLayout;
    mirrorRow->setContentsMargins(0, 0, 0, 0);
    mirrorRow->addWidget(m_mirrorRootEdit, 1);
    mirrorRow->addWidget(mirrorChangeButton);

    auto *logLabel = new QLabel("NETWORK LOG");
    logLabel->setObjectName("sectionLabel");
    m_settingsLog = new QPlainTextEdit;
    m_settingsLog->setReadOnly(true);
    m_settingsLog->setObjectName("networkLog");
    m_settingsLog->setMaximumBlockCount(kNetworkLogLimit);
    for (const QString &line : std::as_const(m_networkLog))
        m_settingsLog->appendPlainText(line);

    auto *leaveButton = new QPushButton("\xE2\x86\x90 Leave node");
    leaveButton->setObjectName("dangerButton");
    leaveButton->setCursor(Qt::PointingHandCursor);
    connect(leaveButton, &QPushButton::clicked, this, [this] { leaveSession(); });
    auto *footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->addWidget(leaveButton);
    footerRow->addStretch();

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(profileLabel);
    layout->addLayout(form);
    layout->addSpacing(6);
    layout->addWidget(storageLabel);
    layout->addLayout(mirrorRow);
    layout->addSpacing(6);
    layout->addWidget(startupLabel);
    layout->addWidget(m_autostartCheck);
    layout->addSpacing(6);
    layout->addWidget(maintLabel);
    layout->addWidget(m_rebuildButton, 0, Qt::AlignLeft);
    layout->addWidget(m_rebuildStatus);
    layout->addSpacing(6);
    layout->addWidget(logLabel);
    layout->addWidget(m_settingsLog, 1);
    layout->addLayout(footerRow);
    return page;
}

void MainWindow::setSettingsAvatar(const QByteArray &pngData)
{
    if (!m_settingsAvatarPreview || pngData.isEmpty())
        return;
    QPixmap pixmap;
    if (!pixmap.loadFromData(pngData))
        return;
    constexpr int side = 64;
    QPixmap rounded(side, side);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(0, 0, side, side, 14, 14);
    painter.setClipPath(clip);
    painter.drawPixmap(0, 0,
                       pixmap.scaled(side, side, Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation));
    m_settingsAvatarPreview->setPixmap(rounded);
}

void MainWindow::chooseAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Choose avatar image", QString(),
        "Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)");
    if (path.isEmpty())
        return;
    QImage image(path);
    if (image.isNull())
        return;
    // Center-crop to a square, scale down, and re-encode as a small PNG.
    const int squareSide = qMin(image.width(), image.height());
    image = image.copy((image.width() - squareSide) / 2,
                       (image.height() - squareSide) / 2, squareSide, squareSide)
                .scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    setSettingsAvatar(png);
    onAvatarChosen(png);
}

void MainWindow::quickRebuildRestart()
{
    // Incremental rebuild + relaunch (no cache wipe) for fast iteration. Reuses
    // the Settings rebuild button/status as the progress target.
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;
    const QString clientDir = updateClientDir();
    if (!QDir(clientDir).exists("CMakeLists.txt")) {
        QMessageBox::information(
            this, "Rebuild & restart",
            "No local source checkout to rebuild from. Use Quick update on the "
            "start screen instead.");
        return;
    }
    if (m_rebuildButton)
        m_rebuildButton->setEnabled(false);
    buildAndRelaunch(clientDir);
}

void MainWindow::rebuildAndRelaunch()
{
    if (m_settingsNameEdit)
        saveDisplayName(m_settingsNameEdit->text());
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;
    m_rebuildButton->setEnabled(false);

    const QString clientDir = updateClientDir();
    if (!QDir(clientDir).exists("CMakeLists.txt")) {
        setUpdateStatus("No local source checkout to rebuild from. Use Quick "
                        "update on the start screen instead.",
                        true);
        m_rebuildButton->setEnabled(true);
        return;
    }
    // Clear the build cache for a clean from-scratch rebuild, then relaunch.
    setUpdateStatus("Clearing build cache...");
    QDir(clientDir + "/build").removeRecursively();
    buildAndRelaunch(clientDir);
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
    if (m_connectedAtMs > 0) {
        m_totalConnectionMs += QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs;
        m_connectedAtMs = 0;
        QSettings().setValue(kConnectionTotalSetting, m_totalConnectionMs);
    }
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
    m_homeRoster.clear();
    m_connectionMinuteSamples.clear();
    if (m_homeNodeList)
        m_homeNodeList->clear();
    if (m_homeNodes)
        m_homeNodes->setText("No nodes connected yet.");
    updateHomeStats();
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
    if (m_settingsLog)
        m_settingsLog->appendPlainText(line);
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
    m_homeRoster = members;
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
        QList<MemberInfo> ranked = members;
        std::sort(ranked.begin(), ranked.end(), [](const MemberInfo &a,
                                                   const MemberInfo &b) {
            if (a.self != b.self)
                return a.self;
            if (a.online != b.online)
                return a.online;
            return a.name.localeAwareCompare(b.name) < 0;
        });
        int rank = 1;
        for (const MemberInfo &member : std::as_const(ranked)) {
            QString label = member.name;
            if (member.self)
                label += " (you)";
            else if (!member.note.isEmpty())
                label += " " + member.note;
            QString bch = member.bchAddress.trimmed();
            if (member.self && bch.isEmpty())
                bch = QSettings().value(kBchSetting).toString().trimmed();
            const QString balance = member.bchBalance.trimmed().isEmpty()
                                        ? QStringLiteral("balance pending")
                                        : member.bchBalance.trimmed();
            label = QString::number(rank) + ". " + label + "\nBCH " +
                    (bch.isEmpty() ? QStringLiteral("no address")
                                   : compactAddress(bch)) +
                    " | " + balance;
            auto *item = new QListWidgetItem(statusDotIcon(member.online), label);
            item->setData(Qt::UserRole, member.online ? "online" : "offline");
            item->setToolTip(bch.isEmpty() ? QStringLiteral("No BCH address published")
                                           : sponsorUrlFor(bch));
            m_homeNodeList->addItem(item);
            ++rank;
        }
    }
    updateHomeStats();
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
    const QString configured =
        QSettings().value(kMirrorRootSetting).toString().trimmed();
    if (!configured.isEmpty())
        return configured;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/mirrors";
}

void MainWindow::changeMirrorLocation()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Choose where to store mirrored repositories",
        repositoryMirrorRoot());
    if (chosen.isEmpty())
        return;
    QSettings().setValue(kMirrorRootSetting, chosen);
    if (m_mirrorRootEdit)
        m_mirrorRootEdit->setText(chosen);
    logSystem("Mirror storage folder set to " + chosen +
              " (applies to newly added repositories).");
    QMessageBox::information(
        this, "Mirror storage",
        "New mirrors will be stored in:\n" + chosen +
            "\n\nExisting mirrors stay where they are. A local fork will push "
            "into its repository's mirror here.");
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
    loadRepoStats();
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
        const QPair<int, int> stats =
            m_repoStats.value(repo.owner + "/" + repo.name);
        if (stats.first > 0)
            label += "\n   served " + QString::number(stats.first) +
                     "\xC3\x97 through the mainnode \xC2\xB7 " +
                     QString::number(stats.second) + " clone" +
                     (stats.second == 1 ? "" : "s");
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
    const QString ownerSetting = QSettings().value(kHandleSetting).toString().trimmed();
    repo.owner = ownerSetting.isEmpty()
                     ? repoSegment(m_userName, QStringLiteral("owner"))
                     : repoSegment(ownerSetting, QStringLiteral("owner"));
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
    url.setFragment("repo/" + repoSegment(repo.owner, QStringLiteral("owner")) +
                    "/" + repoSegment(repo.name, QStringLiteral("repository")));
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
        connect(host, &RepoHost::requestServed, this, &MainWindow::onRequestServed);
        host->start();
        m_repoHosts.append(host);
    }
}

void MainWindow::onRequestServed(const QString &owner, const QString &name, bool clone)
{
    QPair<int, int> &stats = m_repoStats[owner + "/" + name];
    stats.first += 1; // served through the mainnode
    if (clone)
        stats.second += 1; // git clone
    saveRepoStats();
    refreshRepositoryList();
}

void MainWindow::loadRepoStats()
{
    m_repoStats.clear();
    const QJsonObject obj =
        QJsonDocument::fromJson(
            QSettings().value(QStringLiteral("repositories/stats")).toByteArray())
            .object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        m_repoStats.insert(it.key(),
                           {entry.value("served").toInt(),
                            entry.value("clones").toInt()});
    }
}

void MainWindow::saveRepoStats() const
{
    QJsonObject obj;
    for (auto it = m_repoStats.constBegin(); it != m_repoStats.constEnd(); ++it) {
        obj.insert(it.key(), QJsonObject{{"served", it.value().first},
                                         {"clones", it.value().second}});
    }
    QSettings().setValue(QStringLiteral("repositories/stats"),
                         QJsonDocument(obj).toJson(QJsonDocument::Compact));
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
                const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
                const QString name = repoSegment(repo.name, QStringLiteral("repository"));
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
                    {"owner", owner},
                    {"name", name},
                    {"updatedAt", updatedAt},
                    {"count", files.size()},
                    {"maintainer", m_profileIdentity.publicKey()}};
                QJsonObject payload{
                    {"owner", owner},
                    {"name", name},
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
    const QString owner = repoSegment(repo.owner, QStringLiteral("owner"));
    const QString name = repoSegment(repo.name, QStringLiteral("repository"));
    QJsonObject metadata{{"owner", owner},
                         {"name", name},
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

namespace {

// A cheap digest of all refs in a bare mirror, so an automatic fetch can tell
// whether the owner's repo actually changed before announcing/republishing.
QString mirrorRefsDigest(const QString &mirrorPath)
{
    if (!QDir(mirrorPath).exists())
        return QString();
    QProcess p;
    p.start("git", {"-C", mirrorPath, "for-each-ref",
                    "--format=%(objectname) %(refname)"});
    if (!p.waitForFinished(5000))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}

} // namespace

void MainWindow::autoSyncMirrors()
{
    // Quietly refresh every repo's mirror so it tracks the owner's repo.
    for (int i = 0; i < m_repositories.size(); ++i) {
        if (!m_syncingRepos.contains(i) &&
            !repositorySource(m_repositories.at(i)).isEmpty())
            syncRepository(i, /*quiet=*/true);
    }
}

void MainWindow::syncRepository(int index, bool quiet)
{
    if (index < 0 || index >= m_repositories.size() ||
        m_syncingRepos.contains(index))
        return;

    RepositoryRecord &repo = m_repositories[index];
    if (!QDir().mkpath(QFileInfo(repo.mirrorPath).absolutePath())) {
        if (!quiet)
            QMessageBox::warning(this, "Sync repository",
                                 "Could not create " +
                                     QFileInfo(repo.mirrorPath).absolutePath());
        return;
    }

    const bool hasMirror = QDir(repo.mirrorPath).exists();
    const QString source = repositorySource(repo);
    const QString beforeDigest = mirrorRefsDigest(repo.mirrorPath);
    const QStringList args = hasMirror
                                 ? QStringList{"-C", repo.mirrorPath,
                                               "fetch", "--prune"}
                                 : QStringList{"clone", "--mirror",
                                               source, repo.mirrorPath};

    m_syncingRepos.insert(index);
    refreshRepositoryList();
    if (!quiet)
        logSystem(QStringLiteral("Mirror: ") +
                  (hasMirror ? QStringLiteral("fetching ") : QStringLiteral("cloning ")) +
                  repo.owner + "/" + repo.name + " from " + source + ".");

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index, quiet, beforeDigest, hasMirror](
                int exitCode, QProcess::ExitStatus) {
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
                    // Did the owner's repo actually change?
                    const bool changed =
                        !hasMirror ||
                        mirrorRefsDigest(repo.mirrorPath) != beforeDigest;
                    repo.lastSyncMs = QDateTime::currentMSecsSinceEpoch();
                    saveRepositories();
                    refreshRepositoryList();
                    // Quiet auto-syncs only speak up when something changed.
                    if (!quiet || changed)
                        logSystem("Mirror: synced " + repo.owner + "/" +
                                  repo.name + " into " + repo.mirrorPath + ".");
                    if (changed && m_backend) {
                        m_backend->sendChat(
                            repositoryChannel(repo),
                            "Mirror synced by " + m_userName + " at " +
                                formatRepoDate(repo.lastSyncMs));
                    }
                    if (repo.publishToNetwork && (changed || !quiet)) {
                        publishRepository(index, false);
                        // Serve this repo's files live to the web now that a
                        // mirror exists (pure live tunnel, nothing uploaded).
                        startRepoHosts();
                    }
                } else {
                    refreshRepositoryList();
                    logSystem("Mirror: sync failed for " + repo.owner + "/" +
                              repo.name + ": " + errors.right(300));
                    if (!quiet)
                        QMessageBox::warning(
                            this, "Sync repository",
                            "Git mirror sync failed" +
                                (errors.isEmpty() ? QString() :
                                                    ": " + errors.right(500)));
                }
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, index, quiet] {
                process->deleteLater();
                m_syncingRepos.remove(index);
                refreshRepositoryList();
                if (!quiet)
                    QMessageBox::warning(
                        this, "Sync repository",
                        "Could not run git. Install Git and try again.");
            });
    process->start("git", args);
}

// ------------------------------------------------------------------ settings

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
