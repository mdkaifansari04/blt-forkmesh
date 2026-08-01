



#include "ActionStore.h"
#include "ControlNode.h"
#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "PrivateMirrorStore.h"
#include "PublicMirrorRuntime.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkReply>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardPaths>
#include <QSaveFile>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>

using namespace forkmesh::ui;

namespace {

QFrame *controlCard(const QString &heading, QVBoxLayout **bodyOut)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("leaderboardCard"));
    card->setFrameShape(QFrame::StyledPanel);
    auto *body = new QVBoxLayout(card);
    body->setContentsMargins(16, 14, 16, 14);
    body->setSpacing(10);
    auto *title = new QLabel(heading);
    title->setObjectName(QStringLiteral("sectionLabel"));
    body->addWidget(title);
    if (bodyOut)
        *bodyOut = body;
    return card;
}

QLabel *controlHint(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("mutedLabel"));
    label->setWordWrap(true);
    return label;
}

QString publicRelayOrigin(const QString &configured)
{
    QUrl url(configured.trimmed());
    if (!url.isValid() || url.host().isEmpty())
        return QStringLiteral("https://forkmesh.com");
    if (url.scheme() == QLatin1String("wss"))
        url.setScheme(QStringLiteral("https"));
    else if (url.scheme() == QLatin1String("ws"))
        url.setScheme(QStringLiteral("http"));
    url.setUserInfo(QString());
    url.setPath(QString());
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString(QUrl::RemoveUserInfo | QUrl::StripTrailingSlash);
}

QString repoControlKey(const RepositoryRecord &repo)
{
    return repo.owner + QLatin1Char('/') + repo.name;
}

QString directGatewayRoot()
{
    return QStandardPaths::writableLocation(
               QStandardPaths::AppDataLocation) +
           QStringLiteral("/mirror-gateway");
}

QString directGatewayConfigPath()
{
    return QDir(directGatewayRoot())
        .filePath(QStringLiteral("config.json"));
}

QString directGatewayManifestPath()
{
    return QDir(directGatewayRoot())
        .filePath(QStringLiteral("forkmesh-mirror.json"));
}

QString directGatewayConnectorTokenPath()
{
    return QDir(directGatewayRoot())
        .filePath(QStringLiteral("connector.token"));
}

QString managedCloudflaredPath()
{
    return QDir(directGatewayRoot())
        .filePath(
#ifdef Q_OS_WIN
            QStringLiteral("bin/cloudflared.exe")
#else
            QStringLiteral("bin/cloudflared")
#endif
        );
}

bool prepareOwnerDirectory(const QString &path, QString *error)
{
    QDir directory(path);
    if ((!directory.exists() &&
         !QDir().mkpath(directory.absolutePath())) ||
        QFileInfo(directory.absolutePath()).isSymLink() ||
        !QFileInfo(directory.absolutePath()).isDir()) {
        if (error)
            *error = QStringLiteral(
                "Could not create the owner-only mirror-gateway directory.");
        return false;
    }
    QFile permissions(directory.absolutePath());
    if (!permissions.setPermissions(
            QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            QFileDevice::ExeOwner)) {
        if (error)
            *error = QStringLiteral(
                "Could not restrict the mirror-gateway directory.");
        return false;
    }
    return true;
}




QByteArray readOwnerFileIfPresent(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool writeOwnerJson(const QString &path, const QJsonObject &object,
                    QString *error)
{
    if (!prepareOwnerDirectory(QFileInfo(path).absolutePath(), error))
        return false;
    const QFileInfo existing(path);
    if (existing.exists() &&
        (existing.isSymLink() || !existing.isFile())) {
        if (error)
            *error = QStringLiteral(
                "The mirror-gateway configuration path is unsafe.");
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner |
                             QFileDevice::WriteOwner)) {
        file.cancelWriting();
        if (error)
            *error = QStringLiteral(
                "Could not create the mirror-gateway configuration.");
        return false;
    }
    const QByteArray bytes =
        QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral(
                "Could not commit the mirror-gateway configuration.");
        return false;
    }
    return true;
}







bool writeEnvAssignments(const QString &path,
                         const QMap<QString, QString> &values,
                         QString *error, bool *changed = nullptr)
{
    if (changed)
        *changed = false;
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const QFileInfo info(path);
    QString contents;
    if (info.exists()) {
        if (info.isSymLink() || !info.isFile())
            return fail(QStringLiteral("%1 is not a regular file.").arg(path));


        if (info.size() > 512 * 1024)
            return fail(QStringLiteral("%1 is unexpectedly large.").arg(path));
        QFile existing(path);
        if (!existing.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("Could not read %1.").arg(path));
        contents = QString::fromUtf8(existing.readAll());
    }
    const QString before = contents;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        contents = forkmesh::control::updatedEnvAssignment(contents, it.key(),
                                                           it.value());
    }
    if (info.exists() && contents == before)
        return true;
    if (changed)
        *changed = true;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner |
                             QFileDevice::WriteOwner)) {
        file.cancelWriting();
        return fail(QStringLiteral("Could not write %1.").arg(path));
    }
    const QByteArray bytes = contents.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail(QStringLiteral("Could not commit %1.").arg(path));
    return true;
}

bool validRouterPublicKey(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9_-]{43}$"));
    if (!pattern.match(value).hasMatch())
        return false;
    return QByteArray::fromBase64(
               value.toLatin1(),
               QByteArray::Base64UrlEncoding |
                   QByteArray::AbortOnBase64DecodingErrors)
               .size() == 32;
}

QString normalizedHttpsOrigin(const QString &hostname)
{
    const QString host = hostname.trimmed().toLower();
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(host);
    return url.isValid() && !url.host().isEmpty()
               ? url.toString(QUrl::StripTrailingSlash)
               : QString();
}

bool controlMirrorReady(const RepositoryRecord &repo)
{
    if (!repo.isPrivate) {
        if (!PublicMirrorRuntime::isArchiveId(repo.publicArchiveId))
            return false;
        const QString root =
            QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation) +
            QStringLiteral("/public-mirror-archives");
        return PublicMirrorRuntime::readMetadata(
                   root, repo.publicArchiveId, nullptr)
            .isValid();
    }
    if (!PrivateMirrorStore::isOpaqueId(repo.privateReplicaId))
        return false;
    const QString root =
        QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) +
        QStringLiteral("/private-replicas");
    PrivateMirrorStore::Metadata metadata;
    return PrivateMirrorStore::inspectReplica(
        root, repo.privateReplicaId, &metadata, nullptr);
}

QString controlMirrorReadyKey(const RepositoryRecord &repo)
{
    if (repo.isPrivate) {
        return PrivateMirrorStore::isOpaqueId(repo.privateReplicaId)
                   ? QStringLiteral("private:") + repo.privateReplicaId
                   : QString();
    }
    return PublicMirrorRuntime::isArchiveId(repo.publicArchiveId)
               ? QStringLiteral("public:") + repo.publicArchiveId
               : QString();
}

}

QWidget *MainWindow::buildControlNodeSection()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("controlNodeSection"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 24);
    outer->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Control node"));
    title->setObjectName(QStringLiteral("sectionTitle"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title);
    header->addStretch();
    outer->addLayout(header);
    outer->addWidget(controlHint(
        QStringLiteral(
            "This desktop is the local control plane. Repository bytes, identity "
            "keys, wallet ownership, Cloudflare credentials, and host credentials "
            "remain on devices you control; the relay receives only signed public "
            "metadata and ordinary repository traffic.")));




    auto *tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("controlNodeTabs"));
    tabs->setDocumentMode(true);
    m_controlNodeTabs = tabs;
    const auto addTab = [tabs](QWidget *card, const QString &name) {
        auto *body = new QWidget;
        auto *bodyCol = new QVBoxLayout(body);
        bodyCol->setContentsMargins(2, 12, 2, 12);
        bodyCol->setSpacing(14);
        bodyCol->addWidget(card);
        bodyCol->addStretch();
        auto *scroll = new QScrollArea;
        scroll->setObjectName(QStringLiteral("controlNodeTabScroll"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(body);
        tabs->addTab(scroll, name);
    };


    QVBoxLayout *serviceCol = nullptr;
    QFrame *serviceCard =
        controlCard(QStringLiteral("Local mirror services"), &serviceCol);
    m_controlNodeStatus = new QLabel;
    m_controlNodeStatus->setObjectName(QStringLiteral("controlNodeStatus"));
    m_controlNodeStatus->setWordWrap(true);
    serviceCol->addWidget(m_controlNodeStatus);
    m_controlNodeHealth = new QLabel;
    m_controlNodeHealth->setObjectName(QStringLiteral("mutedLabel"));
    m_controlNodeHealth->setWordWrap(true);
    serviceCol->addWidget(m_controlNodeHealth);
    auto *serviceButtons = new QHBoxLayout;
    auto *startButton =
        new QPushButton(QStringLiteral("Start mirror services"));
    startButton->setObjectName(QStringLiteral("controlStartMirrorsButton"));
    startButton->setCursor(Qt::PointingHandCursor);
    setOcticon(startButton, QStringLiteral("broadcast"), 14);
    connect(startButton, &QPushButton::clicked, this,
            &MainWindow::startControlNodeServing);
    serviceButtons->addWidget(startButton);
    auto *stopButton =
        new QPushButton(QStringLiteral("Stop mirror services"));
    stopButton->setObjectName(QStringLiteral("controlStopMirrorsButton"));
    stopButton->setCursor(Qt::PointingHandCursor);
    setOcticon(stopButton, QStringLiteral("stop"), 14);
    connect(stopButton, &QPushButton::clicked, this,
            &MainWindow::stopControlNodeServing);
    serviceButtons->addWidget(stopButton);
    auto *syncButton = new QPushButton(QStringLiteral("Sync all mirrors"));
    syncButton->setObjectName(QStringLiteral("controlSyncMirrorsButton"));
    syncButton->setCursor(Qt::PointingHandCursor);
    setOcticon(syncButton, QStringLiteral("sync"), 14);
    connect(syncButton, &QPushButton::clicked, this,
            &MainWindow::syncControlNodeMirrors);
    serviceButtons->addWidget(syncButton);
    auto *healthButton = new QPushButton(QStringLiteral("Run health check"));
    healthButton->setObjectName(QStringLiteral("controlHealthButton"));
    healthButton->setCursor(Qt::PointingHandCursor);
    setOcticon(healthButton, QStringLiteral("check-circle"), 14);
    connect(healthButton, &QPushButton::clicked, this,
            &MainWindow::runControlNodeHealthCheck);
    serviceButtons->addWidget(healthButton);
    auto *logsButton = new QPushButton(QStringLiteral("Open full local log"));
    logsButton->setObjectName(QStringLiteral("controlLogsButton"));
    logsButton->setCursor(Qt::PointingHandCursor);
    setOcticon(logsButton, QStringLiteral("terminal"), 14);
    connect(logsButton, &QPushButton::clicked, this,
            [this] { showSection(4); });
    serviceButtons->addWidget(logsButton);
    serviceButtons->addStretch();
    serviceCol->addLayout(serviceButtons);
    addTab(serviceCard, QStringLiteral("Mirror services"));


    QVBoxLayout *permissionCol = nullptr;
    QFrame *permissionCard =
        controlCard(QStringLiteral("Repository permissions"), &permissionCol);
    permissionCol->addWidget(controlHint(
        QStringLiteral(
            "Control what this machine serves and which local workflows may run. "
            "Only an owner-capable identity can change network serving or "
            "visibility. Private repositories remain absent from public discovery.")));
    m_controlPermissionsTable = new QTableWidget(0, 7);
    m_controlPermissionsTable->setObjectName(
        QStringLiteral("controlPermissionsTable"));
    m_controlPermissionsTable->setHorizontalHeaderLabels(
        {QStringLiteral("Repository"), QStringLiteral("Mirror"),
         QStringLiteral("Private"), QStringLiteral("Serve"),
         QStringLiteral("Run actions"), QStringLiteral("Secret scan"),
         QStringLiteral("Last sync")});
    m_controlPermissionsTable->verticalHeader()->setVisible(false);
    m_controlPermissionsTable->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    m_controlPermissionsTable->setShowGrid(false);
    m_controlPermissionsTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    for (int column = 1; column < 7; ++column) {
        m_controlPermissionsTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    }
    connect(m_controlPermissionsTable, &QTableWidget::itemChanged, this,
            &MainWindow::updateControlRepositoryPermission);
    permissionCol->addWidget(m_controlPermissionsTable);
    addTab(permissionCard, QStringLiteral("Repository permissions"));


    QVBoxLayout *identityCol = nullptr;
    QFrame *identityCard =
        controlCard(QStringLiteral("Local keys and wallet connection"), &identityCol);
    identityCol->addWidget(controlHint(
        QStringLiteral(
            "The Ed25519 identity key signs node and mirror manifests locally. "
            "Backups are passphrase-encrypted. ForkMesh never asks for a wallet "
            "private key, seed phrase, or recovery phrase: only a public Solana "
            "payout address can be connected.")));
    m_controlIdentityStatus = new QLabel;
    m_controlIdentityStatus->setObjectName(
        QStringLiteral("controlIdentityStatus"));
    m_controlIdentityStatus->setWordWrap(true);
    m_controlIdentityStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    identityCol->addWidget(m_controlIdentityStatus);
    auto *backupButton =
        new QPushButton(QStringLiteral("Back up or import identity key"));
    backupButton->setObjectName(QStringLiteral("controlIdentityBackupButton"));
    backupButton->setCursor(Qt::PointingHandCursor);
    setOcticon(backupButton, QStringLiteral("key"), 14);
    connect(backupButton, &QPushButton::clicked, this,
            &MainWindow::backUpIdentityKey);
    identityCol->addWidget(backupButton, 0, Qt::AlignLeft);

    auto *walletRow = new QHBoxLayout;
    m_controlWalletEdit = new QLineEdit;
    m_controlWalletEdit->setObjectName(QStringLiteral("controlWalletAddress"));
    m_controlWalletEdit->setPlaceholderText(
        QStringLiteral("Solana public address (never a private key)"));
    m_controlWalletEdit->setMaxLength(64);
    walletRow->addWidget(m_controlWalletEdit, 1);
    auto *walletSave = new QPushButton(QStringLiteral("Connect address"));
    walletSave->setObjectName(QStringLiteral("controlWalletConnectButton"));
    walletSave->setCursor(Qt::PointingHandCursor);
    connect(walletSave, &QPushButton::clicked, this,
            &MainWindow::saveControlWalletAddress);
    walletRow->addWidget(walletSave);
    auto *walletClear = new QPushButton(QStringLiteral("Disconnect"));
    walletClear->setObjectName(QStringLiteral("controlWalletDisconnectButton"));
    walletClear->setCursor(Qt::PointingHandCursor);
    connect(walletClear, &QPushButton::clicked, this, [this] {
        if (m_controlWalletEdit)
            m_controlWalletEdit->clear();
        saveControlWalletAddress();
    });
    walletRow->addWidget(walletClear);
    identityCol->addLayout(walletRow);
    m_controlWalletStatus = new QLabel;
    m_controlWalletStatus->setObjectName(QStringLiteral("mutedLabel"));
    m_controlWalletStatus->setWordWrap(true);
    identityCol->addWidget(m_controlWalletStatus);
    identityCol->addWidget(controlHint(
        QStringLiteral(
            "Fund states stay separate: User-owned funds remain in the "
            "self-custodial wallet; the Community-funded reward pool is a "
            "distinct public on-chain address; Pending rewards are uncompleted "
            "ledger allocations; Completed on-chain transfers include a "
            "confirmed transaction signature. ForkMesh never stores a user's "
            "wallet private key.")));
    addTab(identityCard, QStringLiteral("Keys and wallet"));


    addTab(buildRewardPoolControlCard(), QStringLiteral("Reward pool"));


    QVBoxLayout *cloudflareCol = nullptr;
    QFrame *cloudflareCard =
        controlCard(QStringLiteral("Cloudflare relay deployment"), &cloudflareCol);
    cloudflareCol->addWidget(controlHint(
        QStringLiteral(
            "For a token scoped to exactly one account and active zone, leave the "
            "advanced topology fields empty: one click discovers the zone, derives "
            "separate relay and mirror hostnames, creates/reuses D1, deploys the "
            "Worker, configures proxied DNS and a Tunnel, and starts the local "
            "gateway. Broader tokens require an explicit account and zone so "
            "ForkMesh never guesses. The repository-pinned tools write only "
            "encrypted public-mirror archives. The API "
            "token is held in memory for this run, passed through the child "
            "environment, removed from displayed output, and never saved in "
            "settings, argv, logs, generated Worker configuration, D1, or Worker "
            "secrets. A Tunnel connector credential is stored owner-only on this "
            "device for service restarts; the local identity signs the public "
            "mirror manifest.")));
    auto *cloudflareForm = new QFormLayout;
    cloudflareForm->setLabelAlignment(Qt::AlignRight);
    QSettings settings;
    m_cloudflareHostnameEdit = new QLineEdit(
        settings.value(QStringLiteral("control/cloudflareHostname")).toString());
    m_cloudflareHostnameEdit->setObjectName(
        QStringLiteral("cloudflareHostname"));
    m_cloudflareHostnameEdit->setPlaceholderText(
        QStringLiteral("optional: auto-derive forkmesh.<zone>"));
    cloudflareForm->addRow(QStringLiteral("Relay hostname"),
                           m_cloudflareHostnameEdit);
    m_cloudflareMirrorHostnameEdit = new QLineEdit(
        settings
            .value(QStringLiteral(
                "control/cloudflareMirrorHostname"))
            .toString());
    m_cloudflareMirrorHostnameEdit->setObjectName(
        QStringLiteral("cloudflareMirrorHostname"));
    m_cloudflareMirrorHostnameEdit->setPlaceholderText(
        QStringLiteral("optional: auto-derive mirror.<zone>"));
    cloudflareForm->addRow(
        QStringLiteral("Mirror endpoint"),
        m_cloudflareMirrorHostnameEdit);
    m_cloudflareZoneEdit = new QLineEdit(
        settings.value(QStringLiteral("control/cloudflareZone")).toString());
    m_cloudflareZoneEdit->setObjectName(QStringLiteral("cloudflareZone"));
    m_cloudflareZoneEdit->setPlaceholderText(
        QStringLiteral("optional when token sees one active zone"));
    cloudflareForm->addRow(QStringLiteral("Cloudflare zone"),
                           m_cloudflareZoneEdit);
    m_cloudflareAccountEdit = new QLineEdit(
        settings.value(QStringLiteral("control/cloudflareAccount")).toString());
    m_cloudflareAccountEdit->setObjectName(
        QStringLiteral("cloudflareAccount"));
    m_cloudflareAccountEdit->setPlaceholderText(
        QStringLiteral("optional when the token has one account"));
    cloudflareForm->addRow(QStringLiteral("Account ID"),
                           m_cloudflareAccountEdit);
    m_cloudflareNodeNameEdit = new QLineEdit(
        settings.value(QStringLiteral("control/cloudflareNodeName")).toString());
    m_cloudflareNodeNameEdit->setObjectName(
        QStringLiteral("cloudflareNodeName"));
    m_cloudflareNodeNameEdit->setPlaceholderText(
        QStringLiteral("derived from hostname"));
    cloudflareForm->addRow(QStringLiteral("Node name"),
                           m_cloudflareNodeNameEdit);
    m_cloudflareRelayLabelEdit = new QLineEdit(
        settings.value(QStringLiteral("control/cloudflareRelayLabel")).toString());
    m_cloudflareRelayLabelEdit->setObjectName(
        QStringLiteral("cloudflareRelayLabel"));
    m_cloudflareRelayLabelEdit->setPlaceholderText(
        QStringLiteral("optional display label"));
    cloudflareForm->addRow(QStringLiteral("Relay label"),
                           m_cloudflareRelayLabelEdit);
    QString upstream =
        settings.value(QStringLiteral("control/cloudflareMainRelay")).toString();
    if (upstream.isEmpty() && !m_servers.isEmpty())
        upstream = publicRelayOrigin(m_servers.at(m_activeServer).url);
    m_cloudflareMainRelayEdit = new QLineEdit(upstream);
    m_cloudflareMainRelayEdit->setObjectName(
        QStringLiteral("cloudflareMainRelay"));
    m_cloudflareMainRelayEdit->setPlaceholderText(
        QStringLiteral("empty for a standalone main relay"));
    cloudflareForm->addRow(QStringLiteral("Upstream relay"),
                           m_cloudflareMainRelayEdit);
    m_cloudflareTokenEdit = new QLineEdit;
    m_cloudflareTokenEdit->setObjectName(QStringLiteral("cloudflareApiToken"));
    m_cloudflareTokenEdit->setEchoMode(QLineEdit::Password);
    m_cloudflareTokenEdit->setPlaceholderText(
        QStringLiteral("session-only Cloudflare API token"));
    m_cloudflareTokenEdit->setClearButtonEnabled(true);
    cloudflareForm->addRow(QStringLiteral("API token"),
                           m_cloudflareTokenEdit);
    m_cloudflareVpsHostEdit = new QLineEdit;
    m_cloudflareVpsHostEdit->setObjectName(
        QStringLiteral("cloudflareFirstMirrorHost"));
    m_cloudflareVpsHostEdit->setPlaceholderText(
        QStringLiteral("optional VPS hostname or address"));
    cloudflareForm->addRow(QStringLiteral("First mirror VPS"),
                           m_cloudflareVpsHostEdit);
    m_cloudflareVpsUserEdit = new QLineEdit;
    m_cloudflareVpsUserEdit->setObjectName(
        QStringLiteral("cloudflareFirstMirrorUser"));
    m_cloudflareVpsUserEdit->setPlaceholderText(
        QStringLiteral("dedicated non-root SSH user"));
    cloudflareForm->addRow(QStringLiteral("VPS SSH user"),
                           m_cloudflareVpsUserEdit);
    m_cloudflareVpsPasswordEdit = new QLineEdit;
    m_cloudflareVpsPasswordEdit->setObjectName(
        QStringLiteral("cloudflareFirstMirrorPassword"));
    m_cloudflareVpsPasswordEdit->setEchoMode(QLineEdit::Password);
    m_cloudflareVpsPasswordEdit->setPlaceholderText(
        QStringLiteral("optional; local SSH key / agent preferred"));
    m_cloudflareVpsPasswordEdit->setToolTip(
        QStringLiteral(
            "Session-only. Supplied to local SSH tooling through environment/"
            "stdin, never argv, settings, D1, or the Worker."));
    cloudflareForm->addRow(QStringLiteral("VPS SSH password"),
                           m_cloudflareVpsPasswordEdit);
    cloudflareCol->addLayout(cloudflareForm);

    m_cloudflareConnectCheck =
        new QCheckBox(QStringLiteral("Add and connect to the relay after deployment"));
    m_cloudflareConnectCheck->setChecked(true);
    cloudflareCol->addWidget(m_cloudflareConnectCheck);
    m_cloudflareInstallVpsCheck = new QCheckBox(
        QStringLiteral(
            "Install ForkMesh on the first mirror VPS after deployment"));
    m_cloudflareInstallVpsCheck->setChecked(false);
    m_cloudflareInstallVpsCheck->setToolTip(
        QStringLiteral(
            "Adds the host locally, runs the checksum-verified installer over "
            "SSH, and starts the headless node against the newly selected "
            "relay. Leave the VPS fields empty to keep the mirror on this "
            "desktop only."));
    cloudflareCol->addWidget(m_cloudflareInstallVpsCheck);
    auto *deployRow = new QHBoxLayout;
    m_cloudflareDryRunButton =
        new QPushButton(QStringLiteral("Validate Worker + mirror"));
    m_cloudflareDryRunButton->setObjectName(
        QStringLiteral("cloudflareDryRunButton"));
    m_cloudflareDryRunButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_cloudflareDryRunButton, QStringLiteral("shield-check"), 14);
    connect(m_cloudflareDryRunButton, &QPushButton::clicked, this,
            [this] { runCloudflareBootstrap(true); });
    deployRow->addWidget(m_cloudflareDryRunButton);
    m_cloudflareDeployButton =
        new QPushButton(QStringLiteral("Deploy Worker + mirror"));
    m_cloudflareDeployButton->setObjectName(
        QStringLiteral("cloudflareDeployButton"));
    m_cloudflareDeployButton->setProperty("buttonSize", "primary");
    m_cloudflareDeployButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_cloudflareDeployButton, QStringLiteral("rocket"), 14);
    connect(m_cloudflareDeployButton, &QPushButton::clicked, this,
            [this] { runCloudflareBootstrap(false); });
    deployRow->addWidget(m_cloudflareDeployButton);
    m_cloudflareCancelButton = new QPushButton(QStringLiteral("Cancel"));
    m_cloudflareCancelButton->setObjectName(
        QStringLiteral("cloudflareCancelButton"));
    m_cloudflareCancelButton->setCursor(Qt::PointingHandCursor);
    m_cloudflareCancelButton->setEnabled(false);
    connect(m_cloudflareCancelButton, &QPushButton::clicked, this,
            &MainWindow::cancelCloudflareBootstrap);
    deployRow->addWidget(m_cloudflareCancelButton);
    deployRow->addStretch();
    cloudflareCol->addLayout(deployRow);

    m_controlNodeOutput = new QPlainTextEdit;
    m_controlNodeOutput->setObjectName(
        QStringLiteral("controlNodeDeploymentOutput"));
    m_controlNodeOutput->setReadOnly(true);
    m_controlNodeOutput->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_controlNodeOutput->setMinimumHeight(150);
    m_controlNodeOutput->document()->setMaximumBlockCount(1000);
    m_controlNodeOutput->setPlaceholderText(
        QStringLiteral("Local deployment output appears here; credentials are redacted."));
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_controlNodeOutput->setFont(mono);
    cloudflareCol->addWidget(m_controlNodeOutput);
    m_controlCloudflareTabIndex = tabs->count();
    addTab(cloudflareCard, QStringLiteral("Cloudflare relay"));


    addTab(buildCloudflareTokenCard(), QStringLiteral("API token"));


    addTab(buildSiteDeployCard(), QStringLiteral("Site deployment"));


    QVBoxLayout *hostsCol = nullptr;
    QFrame *hostsCard =
        controlCard(QStringLiteral("Connected hosts"), &hostsCol);
    hostsCol->addWidget(controlHint(
        QStringLiteral(
            "Add remote machines, deploy the current ForkMesh binary, update "
            "nodes, inspect their logs, and manage SSH connection details in "
            "Hosts. SSH passwords stay on this desktop and are supplied through "
            "the local process environment/stdin, never command-line arguments "
            "or Worker storage.")));
    m_controlHostsStatus = new QLabel;
    m_controlHostsStatus->setObjectName(QStringLiteral("controlHostsStatus"));
    hostsCol->addWidget(m_controlHostsStatus);
    auto *hostButtons = new QHBoxLayout;
    auto *manageHosts = new QPushButton(QStringLiteral("Connect or manage hosts"));
    manageHosts->setObjectName(QStringLiteral("controlManageHostsButton"));
    manageHosts->setCursor(Qt::PointingHandCursor);
    setOcticon(manageHosts, QStringLiteral("server"), 14);
    connect(manageHosts, &QPushButton::clicked, this,
            [this] { showSection(7); });
    hostButtons->addWidget(manageHosts);
    auto *deployHosts =
        new QPushButton(QStringLiteral("Deploy current binary to saved hosts"));
    deployHosts->setObjectName(QStringLiteral("controlDeployHostsButton"));
    deployHosts->setCursor(Qt::PointingHandCursor);
    setOcticon(deployHosts, QStringLiteral("upload"), 14);
    connect(deployHosts, &QPushButton::clicked, this,
            &MainWindow::deploySavedHostsFromControl);
    hostButtons->addWidget(deployHosts);
    hostButtons->addStretch();
    hostsCol->addLayout(hostButtons);
    addTab(hostsCard, QStringLiteral("Connected hosts"));

    outer->addWidget(tabs, 1);

    m_controlNodeRefreshTimer = new QTimer(page);




    m_controlNodeRefreshTimer->setInterval(10000);
    connect(m_controlNodeRefreshTimer, &QTimer::timeout, this, [this] {
        if (m_sectionStack &&
            m_sectionStack->currentIndex() == kControlNodeSectionIndex) {
            refreshControlNode();
        }
    });
    m_controlNodeRefreshTimer->start();
    ensureDirectMirrorRegistrationTimer(page);
    return page;
}

void MainWindow::ensureDirectMirrorRegistrationTimer(QObject *parent)
{
    if (m_directMirrorRegistrationTimer)
        return;
    m_directMirrorRegistrationTimer = new QTimer(parent);
    m_directMirrorRegistrationTimer->setInterval(5 * 60 * 1000);
    connect(m_directMirrorRegistrationTimer, &QTimer::timeout, this,
            [this] {
                checkDirectMirrorGatewayHealth();
                registerDirectMirrorEndpoint();
            });
    m_directMirrorRegistrationTimer->start();
}

void MainWindow::openCloudflareSetupFromSystemLink(const QString &target)
{
    showSection(kControlNodeSectionIndex);


    if (m_controlNodeTabs && m_controlCloudflareTabIndex >= 0)
        m_controlNodeTabs->setCurrentIndex(m_controlCloudflareTabIndex);
    const QUrl url(target);
    if (url.isValid() && url.scheme() == QLatin1String("forkmesh") &&
        url.host() == QLatin1String("control") &&
        url.path() == QLatin1String("/cloudflare") &&
        url.userInfo().isEmpty() && url.fragment().isEmpty()) {
        const QUrlQuery query(url);
        const auto bounded = [&query](const QString &name, qsizetype maximum) {
            const QString value =
                query.queryItemValue(name, QUrl::FullyDecoded).trimmed();
            return value.size() <= maximum && !value.contains(QChar(u'\0')) &&
                           !value.contains(QLatin1Char('\n')) &&
                           !value.contains(QLatin1Char('\r'))
                       ? value
                       : QString();
        };
        if (bounded(QStringLiteral("mode"), 16) ==
            QLatin1String("vultr")) {
            const QString token =
                QApplication::clipboard()->text().trimmed();
            const QString node =
                bounded(QStringLiteral("node"), 63).toLower();
            static const QRegularExpression tokenPattern(
                QStringLiteral("^[A-Za-z0-9]{20,128}$"));
            static const QRegularExpression nodePattern(
                QStringLiteral("^(?:[a-z][a-z0-9-]{0,62})?$"));
            if (tokenPattern.match(token).hasMatch() &&
                nodePattern.match(node).hasMatch()) {
                if (m_vultrApiKeyEdit)
                    m_vultrApiKeyEdit->setText(token);
                if (m_vultrNameEdit && !node.isEmpty())
                    m_vultrNameEdit->setText(node);
                if (QApplication::clipboard()->text() == token)
                    QApplication::clipboard()->clear();
                QTimer::singleShot(0, this, [this] {
                    createVultrMirrorFromForm();
                });
            } else if (m_vultrStatus) {
                m_vultrStatus->setText(QStringLiteral(
                    "The World launch handoff did not contain a valid Vultr "
                    "token. Return to the LAUNCH MIRROR button and try again."));
            }
            return;
        }
        const auto dns = [&bounded](const QString &name) {
            const QString value = bounded(name, 253).toLower();
            static const QRegularExpression pattern(
                QStringLiteral(
                    "^(?=.{4,253}$)(?:[a-z0-9](?:[a-z0-9-]{0,61}"
                    "[a-z0-9])?\\.)+[a-z](?:[a-z0-9-]{0,61}"
                    "[a-z0-9])?$"));
            return pattern.match(value).hasMatch() ? value : QString();
        };
        const auto assign = [](QLineEdit *edit, const QString &value) {
            if (edit && !value.isEmpty())
                edit->setText(value);
        };
        assign(m_cloudflareHostnameEdit, dns(QStringLiteral("hostname")));
        assign(m_cloudflareMirrorHostnameEdit, dns(QStringLiteral("mirror")));
        assign(m_cloudflareZoneEdit, dns(QStringLiteral("zone")));
        const QString account = bounded(QStringLiteral("account"), 128);
        static const QRegularExpression accountPattern(
            QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
        if (accountPattern.match(account).hasMatch())
            assign(m_cloudflareAccountEdit, account);
        const QString node = bounded(QStringLiteral("node"), 63).toLower();
        static const QRegularExpression nodePattern(
            QStringLiteral("^[a-z][a-z0-9-]{0,62}$"));
        if (nodePattern.match(node).hasMatch())
            assign(m_cloudflareNodeNameEdit, node);
        assign(m_cloudflareRelayLabelEdit,
               bounded(QStringLiteral("label"), 80));
        const QString upstream = bounded(QStringLiteral("upstream"), 300);
        const QUrl upstreamUrl(upstream);
        if (upstreamUrl.isValid() &&
            upstreamUrl.scheme() == QLatin1String("https") &&
            !upstreamUrl.host().isEmpty() && upstreamUrl.userInfo().isEmpty())
            assign(m_cloudflareMainRelayEdit, upstream);
        const QString vpsHost = bounded(QStringLiteral("vpsHost"), 253);
        static const QRegularExpression vpsHostPattern(
            QStringLiteral(
                "^(?:[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?|"
                "(?:[0-9]{1,3}\\.){3}[0-9]{1,3})$"));
        if (vpsHostPattern.match(vpsHost).hasMatch()) {
            assign(m_cloudflareVpsHostEdit, vpsHost);
            if (m_cloudflareInstallVpsCheck)
                m_cloudflareInstallVpsCheck->setChecked(true);
        }
        const QString vpsUser = bounded(QStringLiteral("vpsUser"), 64);
        static const QRegularExpression vpsUserPattern(
            QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,63}$"));
        if (vpsUserPattern.match(vpsUser).hasMatch())
            assign(m_cloudflareVpsUserEdit, vpsUser);
    }
    if (m_cloudflareTokenEdit)
        m_cloudflareTokenEdit->setFocus(Qt::OtherFocusReason);
}

void MainWindow::installCloudflareFirstMirror()
{
    if (!m_cloudflareInstallVpsAfterDeploy)
        return;
    m_cloudflareInstallVpsAfterDeploy = false;
    const QString host =
        m_cloudflareVpsHostEdit
            ? m_cloudflareVpsHostEdit->text().trimmed()
            : QString();
    const QString user =
        m_cloudflareVpsUserEdit
            ? m_cloudflareVpsUserEdit->text().trimmed()
            : QString();
    const QString password =
        m_cloudflareVpsPasswordEdit
            ? m_cloudflareVpsPasswordEdit->text()
            : QString();
    const QString node =
        m_cloudflareNodeNameEdit
            ? m_cloudflareNodeNameEdit->text().trimmed()
            : QString();
    if (host.isEmpty() || user.isEmpty() || node.isEmpty()) {
        appendControlNodeOutput(
            QStringLiteral(
                "First mirror VPS install skipped because its local SSH "
                "fields are incomplete.\n"));
        return;
    }
    if (m_hostIpEdit)
        m_hostIpEdit->setText(host);
    if (m_hostUserEdit)
        m_hostUserEdit->setText(user);
    if (m_hostPassEdit)
        m_hostPassEdit->setText(password);
    if (m_hostNameEdit)
        m_hostNameEdit->setText(node);
    if (m_cloudflareVpsPasswordEdit)
        m_cloudflareVpsPasswordEdit->clear();
    addHostFromForm();
    appendControlNodeOutput(
        QStringLiteral(
            "Starting the checksum-verified ForkMesh install on first mirror "
            "%1@%2. SSH credentials remain in local process memory only.\n")
            .arg(user, host));
    runHostInstall(false, [this, node](bool ok) {
        appendControlNodeOutput(
            ok
                ? QStringLiteral(
                      "First mirror %1 installed. Its headless node will "
                      "register signed health and mirror availability with "
                      "the selected relay.\n")
                      .arg(node)
                : QStringLiteral(
                      "First mirror %1 did not install; review the local Hosts "
                      "log and retry without redeploying the Worker.\n")
                      .arg(node));
    });
}

void MainWindow::refreshControlNode()
{
    refreshControlMirrorReadiness();

    int tracked = 0;
    int mirrors = 0;
    int missing = 0;
    int checking = 0;
    int serving = 0;
    QHash<QString, int> readiness;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly)
            continue;
        ++tracked;
        const QString key = controlMirrorReadyKey(repo);
        const auto cached = m_controlMirrorReadyCache.constFind(key);
        const int state =
            key.isEmpty() ? 0
                          : (cached == m_controlMirrorReadyCache.constEnd()
                                 ? -1
                                 : (*cached ? 1 : 0));
        readiness.insert(repoControlKey(repo), state);
        if (state > 0) {
            ++mirrors;
        } else if (state < 0) {
            ++checking;
        } else {
            ++missing;
        }
        if (repo.publishToNetwork)
            ++serving;
    }
    if (m_controlNodeStatus) {
        const QString state = m_nodeOffline
                                  ? QStringLiteral("Stopped")
                                  : QStringLiteral("Running");
        const bool gatewayRunning =
            m_mirrorGatewayProcess &&
            m_mirrorGatewayProcess->state() != QProcess::NotRunning;
        const bool tunnelRunning =
            m_cloudflaredProcess &&
            m_cloudflaredProcess->state() != QProcess::NotRunning;
        m_controlNodeStatus->setText(
            QStringLiteral(
                "<b>%1</b> · HTTPS gateway %2 · Tunnel %3 · "
                "repository signals use bounded HTTPS sync · "
                "%4/%5 encrypted mirror(s) · "
                "%6 configured to serve · %7 sync job(s)%8")
                .arg(state)
                .arg(gatewayRunning
                         ? (m_directMirrorGatewayHealthy
                                ? QStringLiteral("healthy")
                                : QStringLiteral("starting"))
                         : QStringLiteral("stopped"))
                .arg(tunnelRunning ? QStringLiteral("connected")
                                   : QStringLiteral("stopped"))
                .arg(mirrors)
                .arg(tracked)
                .arg(serving)
                .arg(m_syncingRepos.size())
                .arg(checking > 0
                         ? QStringLiteral(" · %1 readiness check(s) running")
                               .arg(checking)
                         : QString()));
        m_controlNodeStatus->setTextFormat(Qt::RichText);
    }
    if (m_controlNodeHealth) {
        m_controlNodeHealth->setText(
            QStringLiteral("Relay: %1 · encrypted mirrors unavailable: %2 · "
                           "endpoint registration: %3 · last checked %4")
                .arg(m_backend ? QStringLiteral("connected")
                               : QStringLiteral("offline"))
                .arg(missing)
                .arg(m_directMirrorEndpointRegistered
                         ? QStringLiteral("accepted")
                         : QStringLiteral("pending/offline"))
                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
    }

    if (m_controlIdentityStatus) {
        const bool valid =
            m_profileIdentity.isValid();
        m_controlIdentityStatus->setText(
            valid
                ? QStringLiteral(
                      "Identity key: loaded locally · public node ID: %1 · "
                      "private key is owner-readable on this device only.")
                      .arg(m_profileIdentity.publicKey())
                : QStringLiteral(
                      "Identity key: not loaded yet. Run the health check or "
                      "connect this node to load/create the owner-only local key."));
    }
    const QString wallet = savedSolanaAddress().trimmed();
    if (m_controlWalletEdit && !m_controlWalletEdit->hasFocus() &&
        m_controlWalletEdit->text() != wallet) {
        m_controlWalletEdit->setText(wallet);
    }
    if (m_controlWalletStatus) {
        m_controlWalletStatus->setText(
            wallet.isEmpty()
                ? QStringLiteral(
                      "No payout address connected. Funds and keys are never "
                      "created or held by ForkMesh.")
                : QStringLiteral(
                      "Connected public payout address. Non-custodial: signing "
                      "and private wallet keys remain in your external wallet."));
    }
    refreshRewardPoolControls();

    const QJsonArray hosts =
        QJsonDocument::fromJson(
            QSettings().value(kHostsSetting).toString().toUtf8())
            .array();
    if (m_controlHostsStatus) {
        m_controlHostsStatus->setText(
            hosts.isEmpty()
                ? QStringLiteral("No remote hosts saved.")
                : QStringLiteral("%1 remote host(s) saved on this desktop.")
                      .arg(hosts.size()));
    }

    if (!m_controlPermissionsTable || m_controlRefreshingPermissions)
        return;
    QString permissionsSignature;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly)
            continue;
        permissionsSignature +=
            repoControlKey(repo) + QLatin1Char('|') +
            QString::number(readiness.value(repoControlKey(repo), -1)) +
            QLatin1Char('|') + QString::number(repo.isPrivate) +
            QLatin1Char('|') + QString::number(repo.publishToNetwork) +
            QLatin1Char('|') + QString::number(repo.actionsEnabled) +
            QLatin1Char('|') + QString::number(repo.secretScanningEnabled) +
            QLatin1Char('|') + QString::number(repo.lastSyncMs) +
            QLatin1Char('\n');
    }
    if (permissionsSignature == m_controlPermissionsSignature)
        return;
    m_controlPermissionsSignature = permissionsSignature;
    m_controlRefreshingPermissions = true;
    m_controlPermissionsTable->setRowCount(0);
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly)
            continue;
        const int row = m_controlPermissionsTable->rowCount();
        m_controlPermissionsTable->insertRow(row);
        const QString key = repoControlKey(repo);
        auto *name = new QTableWidgetItem(key);
        name->setData(Qt::UserRole, key);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        m_controlPermissionsTable->setItem(row, 0, name);
        const int mirrorState = readiness.value(key, -1);
        auto *mirror = new QTableWidgetItem(
            mirrorState > 0
                ? QStringLiteral("Ready")
                : (mirrorState < 0 ? QStringLiteral("Checking…")
                                   : QStringLiteral("Missing")));
        mirror->setFlags(mirror->flags() & ~Qt::ItemIsEditable);
        mirror->setToolTip(
            mirrorState > 0
                ? QStringLiteral(
                      "Authenticated encrypted mirror storage is ready. "
                      "Plaintext exists only in owner-only runtime storage.")
                : (mirrorState < 0
                       ? QStringLiteral(
                             "Authenticating encrypted mirror storage on a "
                             "background worker.")
                       : QStringLiteral(
                             "No authenticated encrypted mirror archive is "
                             "ready.")));
        m_controlPermissionsTable->setItem(row, 1, mirror);

        auto addToggle = [this, row, &key](int column, const QString &permission,
                                           bool checked, bool enabled,
                                           const QString &tooltip) {
            auto *item = new QTableWidgetItem;
            item->setData(Qt::UserRole, key);
            item->setData(Qt::UserRole + 1, permission);
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
                                  Qt::ItemIsEnabled;
            if (!enabled)
                flags &= ~Qt::ItemIsEnabled;
            item->setFlags(flags);
            item->setToolTip(tooltip);
            m_controlPermissionsTable->setItem(row, column, item);
        };
        const bool owner =
            hasOwnerSigningCapability(catalogOwner(repo));
        addToggle(2, QStringLiteral("private"), repo.isPrivate, owner,
                  QStringLiteral("Hide from public discovery and require authorized access"));
        addToggle(3, QStringLiteral("serve"), repo.publishToNetwork, owner,
                  QStringLiteral("Serve this mirror through the active relay"));
        addToggle(4, QStringLiteral("actions"), repo.actionsEnabled, true,
                  QStringLiteral("Allow trusted local .forkmesh workflows to run"));
        addToggle(5, QStringLiteral("secret-scan"),
                  repo.secretScanningEnabled, true,
                  QStringLiteral("Block pushes when high-confidence secrets are detected"));
        auto *last = new QTableWidgetItem(
            repo.lastSyncMs > 0
                ? QDateTime::fromMSecsSinceEpoch(repo.lastSyncMs)
                      .toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                : QString::fromUtf8("\xE2\x80\x94"));
        last->setFlags(last->flags() & ~Qt::ItemIsEditable);
        m_controlPermissionsTable->setItem(row, 6, last);
    }
    m_controlRefreshingPermissions = false;
}

void MainWindow::refreshControlMirrorReadiness()
{
    if (m_controlMirrorProbeInFlight)
        return;

    struct Probe {
        QString key;
        RepositoryRecord repo;
    };
    QList<Probe> probes;
    bool hasUnknown = false;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly)
            continue;
        const QString key = controlMirrorReadyKey(repo);
        if (key.isEmpty())
            continue;
        probes.append({key, repo});
        hasUnknown = hasUnknown || !m_controlMirrorReadyCache.contains(key);
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    constexpr qint64 kReadinessRefreshMs = 5 * 60 * 1000;
    if (!hasUnknown && m_controlMirrorProbeCompletedAtMs > 0 &&
        now - m_controlMirrorProbeCompletedAtMs < kReadinessRefreshMs) {
        return;
    }
    if (probes.isEmpty()) {
        m_controlMirrorReadyCache.clear();
        m_controlMirrorProbeCompletedAtMs = now;
        return;
    }

    m_controlMirrorProbeInFlight = true;
    auto result = std::make_shared<QHash<QString, bool>>();
    QThread *worker = QThread::create([probes, result] {
        const forkmesh::BackgroundScope activity(
            QStringLiteral("mirrors"),
            QStringLiteral("authenticate %1 encrypted mirror(s)")
                .arg(probes.size()),
            forkmesh::ActionTelemetry::Execution::Worker);
        for (const Probe &probe : probes)
            result->insert(probe.key, controlMirrorReady(probe.repo));
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, result] {
        m_controlMirrorProbeInFlight = false;
        m_controlMirrorProbeCompletedAtMs =
            QDateTime::currentMSecsSinceEpoch();
        if (m_controlMirrorReadyCache != *result) {
            m_controlMirrorReadyCache = *result;
            m_controlPermissionsSignature.clear();
        }
        refreshControlNode();
    });
    worker->start();
}

void MainWindow::runControlNodeHealthCheck()
{
    QStringList results;
    if (m_profileIdentity.isValid() || m_profileIdentity.load()) {
        results << QStringLiteral("identity signature key OK");
    } else {
        results << QStringLiteral("identity key unavailable");
    }
    results << (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty()
                    ? QStringLiteral("git missing")
                    : QStringLiteral("git OK"));
    QString ageError;
    results << (PublicMirrorRuntime::toolingAvailable(
                    PublicMirrorRuntime::Tools(), &ageError)
                    ? QStringLiteral("age encryption OK")
                    : QStringLiteral("age encryption unavailable"));
    results <<
        (forkmesh::control::findCloudflareBootstrapScript(
             QStringLiteral(FORKMESH_SOURCE_DIR),
             QCoreApplication::applicationDirPath())
                 .isEmpty()
             ? QStringLiteral("Cloudflare bootstrap source missing")
             : QStringLiteral("Cloudflare bootstrap OK"));
    results <<
        (forkmesh::control::findCloudflareTunnelBootstrapScript(
             QStringLiteral(FORKMESH_SOURCE_DIR),
             QCoreApplication::applicationDirPath())
                 .isEmpty()
             ? QStringLiteral("Tunnel bootstrap missing")
             : QStringLiteral("Tunnel bootstrap OK"));
    results <<
        (forkmesh::control::findMirrorGatewayScript(
             QStringLiteral(FORKMESH_SOURCE_DIR),
             QCoreApplication::applicationDirPath())
                 .isEmpty()
             ? QStringLiteral("mirror gateway missing")
             : QStringLiteral("mirror gateway OK"));
    results <<
        (forkmesh::control::findCloudflaredInstallerScript(
             QStringLiteral(FORKMESH_SOURCE_DIR),
             QCoreApplication::applicationDirPath())
                 .isEmpty()
             ? QStringLiteral("verified cloudflared installer missing")
             : QStringLiteral("verified cloudflared installer OK"));
    refreshControlMirrorReadiness();
    int unhealthy = 0;
    int checking = 0;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly)
            continue;
        const QString key = controlMirrorReadyKey(repo);
        const auto cached = m_controlMirrorReadyCache.constFind(key);
        if (!key.isEmpty() &&
            cached == m_controlMirrorReadyCache.constEnd()) {
            ++checking;
        } else if (key.isEmpty() ||
                   cached == m_controlMirrorReadyCache.constEnd() ||
                   !*cached) {
            ++unhealthy;
        }
    }
    results << QStringLiteral("%1 missing mirror path(s)").arg(unhealthy);
    if (checking > 0) {
        results << QStringLiteral("%1 mirror check(s) running in background")
                       .arg(checking);
    }
    results << (m_backend ? QStringLiteral("relay connected")
                          : QStringLiteral("relay offline"));
    checkDirectMirrorGatewayHealth();
    if (m_controlNodeHealth)
        m_controlNodeHealth->setText(results.join(QStringLiteral(" · ")));
    appendControlNodeOutput(
        QStringLiteral("Health check: %1\n").arg(results.join(QStringLiteral("; "))));
    logSystem(QStringLiteral(
        "Control node: local health check completed (%1 issue(s)).")
                  .arg(unhealthy));
    refreshControlNode();
}

void MainWindow::startControlNodeServing()
{
    if (m_nodeOffline)
        setNodeOffline(false);
    else
        startRepoHosts();
    startDirectMirrorServices();
    appendControlNodeOutput(
        QStringLiteral(
            "Direct HTTPS mirror services and repository update channels "
            "requested.\n"));
    logSystem(QStringLiteral(
        "Control node: repository update channels requested."));
    refreshControlNode();
}

void MainWindow::maybeAutoStartDirectMirrorServices()
{
    QSettings settings;
    if (!settings
             .value(QStringLiteral("control/autoStartMirrorServices"), true)
             .toBool())
        return;



    if (m_nodeOffline)
        return;
    const QString hostname =
        settings.value(QStringLiteral("control/cloudflareMirrorHostname"))
            .toString()
            .trimmed()
            .toLower();
    if (hostname.isEmpty())
        return;



    const QFileInfo tokenInfo(directGatewayConnectorTokenPath());
    const auto forbiddenPermissions =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup |
        QFileDevice::ExeGroup | QFileDevice::ReadOther |
        QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (!tokenInfo.isFile() || tokenInfo.isSymLink() ||
        (tokenInfo.permissions() & forbiddenPermissions))
        return;
    if ((m_mirrorGatewayProcess &&
         m_mirrorGatewayProcess->state() != QProcess::NotRunning) ||
        (m_cloudflaredProcess &&
         m_cloudflaredProcess->state() != QProcess::NotRunning))
        return;
    logSystem(QStringLiteral(
                  "Control node: auto-starting direct HTTPS mirror services "
                  "for %1.")
                  .arg(hostname));
    ensureDirectMirrorRegistrationTimer(this);
    startDirectMirrorServices();
}

void MainWindow::stopControlNodeServing()
{
    stopDirectMirrorServices();
    if (!m_nodeOffline)
        setNodeOffline(true);
    else
        stopRepoHosts();
    appendControlNodeOutput(
        QStringLiteral(
            "Direct HTTPS gateway, Tunnel connector, and repository update "
            "channels stopped. Encrypted archives remain local.\n"));
    logSystem(QStringLiteral(
        "Control node: repository update channels stopped."));
    refreshControlNode();
}

void MainWindow::syncControlNodeMirrors()
{
    appendControlNodeOutput(
        QStringLiteral("Sync requested for every tracked local mirror.\n"));
    logSystem(QStringLiteral("Control node: syncing all mirrors."));
    autoSyncMirrors();
    refreshControlNode();
}

void MainWindow::updateControlRepositoryPermission(QTableWidgetItem *item)
{
    if (!item || m_controlRefreshingPermissions)
        return;
    const QString key = item->data(Qt::UserRole).toString();
    const QString permission = item->data(Qt::UserRole + 1).toString();
    if (key.isEmpty() || permission.isEmpty())
        return;
    int index = -1;
    for (int i = 0; i < m_repositories.size(); ++i) {
        if (repoControlKey(m_repositories.at(i)) == key) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;
    RepositoryRecord &repo = m_repositories[index];
    const bool enabled = item->checkState() == Qt::Checked;
    if ((permission == QLatin1String("private") ||
         permission == QLatin1String("serve")) &&
        !hasOwnerSigningCapability(catalogOwner(repo))) {
        flashMessage(
            QStringLiteral("Only the repository owner's signing identity can "
                           "change that permission."),
            true);
        QTimer::singleShot(0, this, &MainWindow::refreshControlNode);
        return;
    }

    QString action;
    bool visibilityChanged = false;
    if (permission == QLatin1String("private")) {
        if (repo.isPrivate == enabled)
            return;
        repo.isPrivate = enabled;
        visibilityChanged = true;
        action = enabled ? QStringLiteral("visibility set to private")
                         : QStringLiteral("visibility set to public");
    } else if (permission == QLatin1String("serve")) {
        if (repo.publishToNetwork == enabled)
            return;
        repo.publishToNetwork = enabled;
        action = enabled ? QStringLiteral("network serving enabled")
                         : QStringLiteral("network serving disabled");
    } else if (permission == QLatin1String("actions")) {
        if (repo.actionsEnabled == enabled)
            return;
        repo.actionsEnabled = enabled;
        action = enabled ? QStringLiteral("local actions enabled")
                         : QStringLiteral("local actions disabled");
    } else if (permission == QLatin1String("secret-scan")) {
        if (repo.secretScanningEnabled == enabled)
            return;
        repo.secretScanningEnabled = enabled;
        action = enabled ? QStringLiteral("push secret scanning enabled")
                         : QStringLiteral("push secret scanning disabled");
    } else {
        return;
    }

    saveRepositories();
    if (visibilityChanged) {



        QString ignoredGatewayError;
        rebuildDirectMirrorGatewayConfiguration(
            &ignoredGatewayError, true);
        if (enabled)
            syncPrivateRepository(index,  false);
        else
            syncRepository(index,  false);
    } else if (permission == QLatin1String("serve") &&
               repo.publishToNetwork) {
        if (repo.isPrivate)
            syncPrivateRepository(index,  false);
        else
            syncRepository(index,  false);
    } else if (permission == QLatin1String("serve") &&
               !repo.publishToNetwork) {
        QString gatewayError;
        if (!rebuildDirectMirrorGatewayConfiguration(
                &gatewayError, true)) {
            logSystem(
                QStringLiteral(
                    "Control node: could not refresh the direct gateway "
                    "after disabling a mirror: %1")
                    .arg(gatewayError));
        }
    }
    startRepoHosts();
    logSystem(QStringLiteral("Control node: %1 for %2.").arg(action, key));
    appendControlNodeOutput(
        QStringLiteral("Repository %1: %2.\n").arg(key, action));
    refreshRepositoryList();
    QTimer::singleShot(0, this, &MainWindow::refreshControlNode);
}

void MainWindow::saveControlWalletAddress()
{
    const QString address =
        m_controlWalletEdit ? m_controlWalletEdit->text().trimmed() : QString();
    if (!address.isEmpty() &&
        !forkmesh::control::isValidSolanaPublicAddress(address)) {
        if (m_controlWalletStatus) {
            m_controlWalletStatus->setText(
                QStringLiteral(
                    "Enter one valid Solana public address. Private keys, seed "
                    "phrases, and recovery phrases are never accepted."));
        }
        flashMessage(QStringLiteral("That is not a valid Solana public address."),
                     true);
        return;
    }
    saveSolanaAddress(address);
    if (m_settingsSolanaEdit)
        m_settingsSolanaEdit->setText(address);
    updateSolanaNotice();
    updateWalletVerifyNotice();
    updateNavSolanaBalance();
    logSystem(address.isEmpty()
                  ? QStringLiteral("Control node: public payout address disconnected.")
                  : QStringLiteral("Control node: public payout address updated."));
    appendControlNodeOutput(
        address.isEmpty()
            ? QStringLiteral("Public payout address disconnected.\n")
            : QStringLiteral(
                  "Public payout address connected; no wallet key was stored.\n"));
    refreshControlNode();
}

bool MainWindow::rebuildDirectMirrorGatewayConfiguration(
    QString *error, bool restartRunningGateway)
{
    if ((!m_profileIdentity.isValid() && !m_profileIdentity.load()) ||
        !m_profileIdentity.isValid()) {
        if (error)
            *error = QStringLiteral(
                "The local node identity is unavailable.");
        return false;
    }
    QSettings settings;
    const QString node =
        settings.value(QStringLiteral("control/cloudflareNodeName"))
            .toString()
            .trimmed()
            .toLower();
    const QString hostname =
        (m_directMirrorHostname.isEmpty()
             ? settings
                   .value(QStringLiteral(
                       "control/cloudflareMirrorHostname"))
                   .toString()
             : m_directMirrorHostname)
            .trimmed()
            .toLower();
    const QString routerKey =
        (m_directMirrorRouterPublicKey.isEmpty()
             ? settings
                   .value(QStringLiteral(
                       "control/directMirrorRouterPublicKey"))
                   .toString()
             : m_directMirrorRouterPublicKey)
            .trimmed();
    const QString origin = normalizedHttpsOrigin(hostname);
    static const QRegularExpression nodePattern(
        QStringLiteral("^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    if (!nodePattern.match(node).hasMatch() || origin.isEmpty() ||
        !validRouterPublicKey(routerKey)) {
        if (error) {
            *error = QStringLiteral(
                "The mirror hostname, node name, or Worker router public key "
                "is not configured.");
        }
        return false;
    }
    const QString gatewayScript =
        forkmesh::control::findMirrorGatewayScript(
            QStringLiteral(FORKMESH_SOURCE_DIR),
            QCoreApplication::applicationDirPath());
    if (gatewayScript.isEmpty()) {
        if (error)
            *error = QStringLiteral(
                "The repository-pinned mirror gateway is unavailable.");
        return false;
    }

    const QString archiveRoot =
        QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) +
        QStringLiteral("/public-mirror-archives");
    const QString app = QCoreApplication::applicationFilePath();
    QJsonArray repositories;
    static const QStringList operations{
        QStringLiteral("blob"), QStringLiteral("blobs"),
        QStringLiteral("branches"), QStringLiteral("commit"),
        QStringLiteral("compare"),
        QStringLiteral("git-info-refs"),
        QStringLiteral("git-upload-pack"),
        QStringLiteral("history"), QStringLiteral("raw"),
        QStringLiteral("release-blob"), QStringLiteral("search"),
        QStringLiteral("sizes"), QStringLiteral("stats"),
        QStringLiteral("tree")};
    for (const RepositoryRecord &repo :
         std::as_const(m_repositories)) {
        if (repo.previewOnly || repo.isPrivate ||
            !repo.publishToNetwork)
            continue;
        const PublicMirrorRuntime::Metadata metadata =
            PublicMirrorRuntime::readMetadata(
                archiveRoot, repo.publicArchiveId, nullptr);



        if (!metadata.isValid())
            continue;
        QJsonArray operationArray;
        for (const QString &operation : operations)
            operationArray.append(operation);
        QJsonObject repository{
            {QStringLiteral("name"),
             repoSegment(repo.name,
                         QStringLiteral("repository"))},
            {QStringLiteral("visibility"),
             QStringLiteral("public")},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("integrity"),
             QJsonObject{
                 {QStringLiteral("expectedRefsSha256"),
                  metadata.expectedRefsSha256}}},
            {QStringLiteral("operations"), operationArray},
            {QStringLiteral("encryptedArchive"),
             QJsonObject{
                 {QStringLiteral("scheme"),
                  QStringLiteral("age-encrypted-tar-v1")},
                 {QStringLiteral("ciphertextPath"),
                  PublicMirrorRuntime::ciphertextPath(
                      archiveRoot, metadata.archiveId)},
                 {QStringLiteral("ciphertextSha256"),
                  metadata.ciphertextSha256},
                 {QStringLiteral("keyReference"),
                  metadata.keyReference},
                 {QStringLiteral("materializeCommand"),
                  QJsonArray{app,
                             QStringLiteral(
                                 "--materialize-public-mirror")}}}},
        };
        const QStringList owners =
            forkmesh::control::directMirrorRepositoryOwners(
                repoSegment(repo.owner, QStringLiteral("owner")),
                repoSegment(catalogOwner(repo), QStringLiteral("owner")));
        for (const QString &owner : owners) {
            repository.insert(QStringLiteral("owner"), owner);
            repositories.append(repository);
        }
    }
    QJsonObject config{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("node"),
         QJsonObject{{QStringLiteral("name"), node},
                     {QStringLiteral("publicKey"),
                      m_profileIdentity.publicKey()}}},
        {QStringLiteral("routerPublicKey"), routerKey},
        {QStringLiteral("publicOrigin"), origin},
        {QStringLiteral("listen"),
         QJsonObject{{QStringLiteral("host"),
                      QStringLiteral("127.0.0.1")},
                     {QStringLiteral("port"), 8790}}},
        {QStringLiteral("manifestPath"),
         directGatewayManifestPath()},
        {QStringLiteral("requestVerifierCommand"),
         QJsonArray{app,
                    QStringLiteral("--verify-mirror-capability")}},
        {QStringLiteral("healthSignerCommand"),
         QJsonArray{app, QStringLiteral("--sign-mirror-health")}},
        {QStringLiteral("repositories"), repositories},
        {QStringLiteral("limits"),
         QJsonObject{
             {QStringLiteral("maxReleaseBytes"),
              double(2LL * 1024 * 1024 * 1024)},
             {QStringLiteral("maxPrivateReplicaBytes"),
              double(512LL * 1024 * 1024)}}},
    };
    const QString privateStore =
        QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) +
        QStringLiteral("/private-replicas");
    const QFileInfo privateStoreInfo(privateStore);
    if (privateStoreInfo.isDir() &&
        !privateStoreInfo.isSymLink()) {
        config.insert(QStringLiteral("privateReplicaStore"),
                      privateStoreInfo.absoluteFilePath());
    }







    const QByteArray previousConfig = readOwnerFileIfPresent(
        directGatewayConfigPath());
    if (!writeOwnerJson(directGatewayConfigPath(), config, error))
        return false;
    const bool configChanged =
        previousConfig !=
        readOwnerFileIfPresent(directGatewayConfigPath());

    m_directMirrorHostname = hostname;
    m_directMirrorRouterPublicKey = routerKey;
    settings.setValue(
        QStringLiteral("control/cloudflareMirrorHostname"), hostname);
    settings.setValue(
        QStringLiteral("control/directMirrorRouterPublicKey"), routerKey);

    if (restartRunningGateway && configChanged &&
        ((m_mirrorGatewayProcess &&
          m_mirrorGatewayProcess->state() !=
              QProcess::NotRunning) ||
         (m_cloudflaredProcess &&
          m_cloudflaredProcess->state() !=
              QProcess::NotRunning))) {
        stopDirectMirrorServices();
        QTimer::singleShot(
            750, this, &MainWindow::startDirectMirrorServices);
    }
    return true;
}

void MainWindow::startDirectMirrorServices()
{
    QSettings serviceSettings;
    m_directMirrorHostname =
        serviceSettings
            .value(QStringLiteral(
                "control/cloudflareMirrorHostname"))
            .toString()
            .trimmed()
            .toLower();
    m_directMirrorRouterPublicKey =
        serviceSettings
            .value(QStringLiteral(
                "control/directMirrorRouterPublicKey"))
            .toString()
            .trimmed();
    QString rebuildError;
    if (!rebuildDirectMirrorGatewayConfiguration(
            &rebuildError, false)) {
        appendControlNodeOutput(
            QStringLiteral(
                "Direct HTTPS gateway configuration is not ready: %1\n")
                .arg(rebuildError));
        return;
    }
    const QString configPath = directGatewayConfigPath();
    const QString manifestPath = directGatewayManifestPath();
    if (!QFileInfo(configPath).isFile() ||
        !QFileInfo(manifestPath).isFile()) {
        appendControlNodeOutput(
            QStringLiteral(
                "Direct HTTPS mirror configuration/manifest is unavailable; "
                "deploy the mirror endpoint first.\n"));
        return;
    }
    QString python =
        QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty())
        python =
            QStandardPaths::findExecutable(QStringLiteral("python"));
    const QString gatewayScript =
        forkmesh::control::findMirrorGatewayScript(
            QStringLiteral(FORKMESH_SOURCE_DIR),
            QCoreApplication::applicationDirPath());
    if ((!m_mirrorGatewayProcess ||
         m_mirrorGatewayProcess->state() ==
             QProcess::NotRunning) &&
        !python.isEmpty() && !gatewayScript.isEmpty()) {
        auto *gateway = new QProcess(this);
        m_mirrorGatewayProcess = gateway;
        gateway->setProcessChannelMode(QProcess::MergedChannels);
        connect(gateway, &QProcess::readyReadStandardOutput, this,
                [this, gateway] {
                    const QString output =
                        QString::fromUtf8(
                            gateway->readAllStandardOutput());
                    appendControlNodeOutput(output);
                    if (output.contains(
                            QStringLiteral("\"event\":\"gateway_started\""))) {
                        m_directMirrorGatewayHealthy = true;
                        checkDirectMirrorGatewayHealth();
                        // The loopback gateway can be ready before the Tunnel
                        // connector has an edge route. Retry the signed
                        // registration during that short convergence window;
                        // a pending repository proof is not yet success.
                        for (const int delayMs : {1500, 5000, 15000, 60000}) {
                            QTimer::singleShot(delayMs, this, [this] {
                                if (m_directMirrorEndpointRegistered)
                                    return;
                                checkDirectMirrorGatewayHealth();
                                QTimer::singleShot(
                                    750, this,
                                    &MainWindow::registerDirectMirrorEndpoint);
                            });
                        }
                    }
                });
        connect(
            gateway, &QProcess::finished, this,
            [this, gateway](int exitCode, QProcess::ExitStatus) {
                if (m_mirrorGatewayProcess == gateway)
                    m_mirrorGatewayProcess = nullptr;
                m_directMirrorGatewayHealthy = false;
                m_directMirrorEndpointRegistered = false;
                appendControlNodeOutput(
                    QStringLiteral(
                        "Mirror gateway stopped (exit %1).\n")
                        .arg(exitCode));
                gateway->deleteLater();
                refreshControlNode();
            });
        gateway->start(
            python,
            {gatewayScript, QStringLiteral("--config"),
             configPath});
    }

    if (m_cloudflaredProcess &&
        m_cloudflaredProcess->state() !=
            QProcess::NotRunning) {
        refreshControlNode();
        return;
    }
    const QFileInfo tokenInfo(
        directGatewayConnectorTokenPath());
    const auto forbiddenPermissions =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup |
        QFileDevice::ExeGroup | QFileDevice::ReadOther |
        QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (!tokenInfo.isFile() || tokenInfo.isSymLink() ||
        (tokenInfo.permissions() & forbiddenPermissions)) {
        appendControlNodeOutput(
            QStringLiteral(
                "Local gateway started without the Tunnel connector because "
                "an owner-only connector token has not been provisioned.\n"));
        refreshControlNode();
        return;
    }
    QString cloudflared =
        QStandardPaths::findExecutable(
            QStringLiteral("cloudflared"));
    if (cloudflared.isEmpty() &&
        m_managedCloudflaredVerified &&
        QFileInfo(managedCloudflaredPath()).isFile() &&
        QFileInfo(managedCloudflaredPath()).isExecutable()) {
        cloudflared = managedCloudflaredPath();
    }
    if (cloudflared.isEmpty()) {
        if (m_cloudflaredInstallProcess &&
            m_cloudflaredInstallProcess->state() !=
                QProcess::NotRunning) {
            appendControlNodeOutput(
                QStringLiteral(
                    "The SHA-256-pinned cloudflared connector is being "
                    "verified locally.\n"));
            return;
        }
        const QString installer =
            forkmesh::control::findCloudflaredInstallerScript(
                QStringLiteral(FORKMESH_SOURCE_DIR),
                QCoreApplication::applicationDirPath());
        QString python =
            QStandardPaths::findExecutable(
                QStringLiteral("python3"));
        if (python.isEmpty()) {
            python =
                QStandardPaths::findExecutable(
                    QStringLiteral("python"));
        }
        QString directoryError;
        if (installer.isEmpty() || python.isEmpty() ||
            !prepareOwnerDirectory(
                QFileInfo(managedCloudflaredPath())
                    .absolutePath(),
                &directoryError)) {
            appendControlNodeOutput(
                QStringLiteral(
                    "The Tunnel connector could not be installed safely: %1\n")
                    .arg(
                        !directoryError.isEmpty()
                            ? directoryError
                            : QStringLiteral(
                                  "the bundled verified installer or Python 3 "
                                  "is unavailable")));
            refreshControlNode();
            return;
        }
        auto *installProcess = new QProcess(this);
        m_cloudflaredInstallProcess = installProcess;
        installProcess->setProcessChannelMode(
            QProcess::MergedChannels);
        installProcess->setProcessEnvironment(
            QProcessEnvironment::systemEnvironment());
        appendControlNodeOutput(
            QStringLiteral(
                "Downloading or verifying ForkMesh's exact SHA-256-pinned "
                "cloudflared 2026.7.2 connector. No shell pipeline or "
                "administrator install is used.\n"));
        connect(
            installProcess, &QProcess::finished, this,
            [this, installProcess](
                int exitCode, QProcess::ExitStatus status) {
                QByteArray output =
                    installProcess->readAllStandardOutput();
                if (output.size() > 16 * 1024)
                    output = output.left(16 * 1024);
                appendControlNodeOutput(
                    forkmesh::control::redactProcessOutput(
                        QString::fromUtf8(output)));
                const bool ok =
                    status == QProcess::NormalExit &&
                    exitCode == 0 &&
                    QFileInfo(managedCloudflaredPath())
                        .isFile() &&
                    QFileInfo(managedCloudflaredPath())
                        .isExecutable();
                m_managedCloudflaredVerified = ok;
                if (m_cloudflaredInstallProcess ==
                    installProcess) {
                    m_cloudflaredInstallProcess = nullptr;
                }
                installProcess->setProcessEnvironment(
                    QProcessEnvironment());
                installProcess->deleteLater();
                if (ok) {
                    appendControlNodeOutput(
                        QStringLiteral(
                            "Verified managed cloudflared connector is ready; "
                            "starting the Tunnel.\n"));
                    startDirectMirrorServices();
                } else {
                    appendControlNodeOutput(
                        QStringLiteral(
                            "Verified cloudflared installation failed (exit "
                            "%1); the local gateway remains loopback-only.\n")
                            .arg(exitCode));
                    refreshControlNode();
                }
            });
        installProcess->start(
            python,
            {installer, QStringLiteral("--destination"),
             managedCloudflaredPath(),
             QStringLiteral("--json-stdout")});
        return;
    }
    QFile tokenFile(tokenInfo.absoluteFilePath());
    if (!tokenFile.open(QIODevice::ReadOnly) ||
        tokenInfo.size() <= 0 || tokenInfo.size() > 16 * 1024) {
        appendControlNodeOutput(
            QStringLiteral(
                "Tunnel connector credential could not be read safely.\n"));
        refreshControlNode();
        return;
    }
    QByteArray connectorToken = tokenFile.readAll().trimmed();
    tokenFile.close();
    if (connectorToken.isEmpty() ||
        connectorToken.contains('\n') ||
        connectorToken.contains('\r')) {
        connectorToken.fill('\0');
        connectorToken.clear();
        appendControlNodeOutput(
            QStringLiteral(
                "Tunnel connector credential is invalid.\n"));
        refreshControlNode();
        return;
    }
    auto *tunnel = new QProcess(this);
    m_cloudflaredProcess = tunnel;
    QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();
    for (const QString &key :
         {QStringLiteral("CLOUDFLARE_API_TOKEN"),
          QStringLiteral("CF_API_TOKEN"),
          QStringLiteral("CLOUDFLARE_TOKEN"),
          QStringLiteral("CF_TOKEN")}) {
        environment.remove(key);
    }
    environment.insert(
        QStringLiteral("TUNNEL_TOKEN"),
        QString::fromUtf8(connectorToken));
    tunnel->setProcessEnvironment(environment);
    environment = QProcessEnvironment();
    tunnel->setStandardOutputFile(QProcess::nullDevice());
    tunnel->setStandardErrorFile(QProcess::nullDevice());
    connect(tunnel, &QProcess::started, this,
            [this, tunnel] {
                tunnel->setProcessEnvironment(
                    QProcessEnvironment());
                appendControlNodeOutput(
                    QStringLiteral(
                        "Cloudflare Tunnel connector started from an "
                        "owner-only local credential.\n"));
                refreshControlNode();
            });
    connect(
        tunnel, &QProcess::finished, this,
        [this, tunnel](int exitCode, QProcess::ExitStatus) {
            if (m_cloudflaredProcess == tunnel)
                m_cloudflaredProcess = nullptr;
            m_directMirrorEndpointRegistered = false;
            appendControlNodeOutput(
                QStringLiteral(
                    "Cloudflare Tunnel connector stopped (exit %1).\n")
                    .arg(exitCode));
            tunnel->deleteLater();
            refreshControlNode();
        });
    tunnel->start(
        cloudflared,
        {QStringLiteral("tunnel"), QStringLiteral("--no-autoupdate"),
         // QUIC's large UDP receive buffers compete directly with encrypted
         // repository materialization on small mirrors. HTTP/2 keeps the
         // connector stable under the same end-to-end Tunnel security model.
         QStringLiteral("--protocol"), QStringLiteral("http2"),
         QStringLiteral("run")});
    connectorToken.fill('\0');
    connectorToken.clear();
    refreshControlNode();
}

void MainWindow::stopDirectMirrorServices()
{
    auto stop = [this](QProcess *process,
                       const QString &label) {
        if (!process ||
            process->state() == QProcess::NotRunning)
            return;
        appendControlNodeOutput(
            label + QStringLiteral(" stop requested.\n"));
        process->terminate();
        QPointer<QProcess> guarded(process);
        QTimer::singleShot(5000, this, [guarded] {
            if (guarded &&
                guarded->state() != QProcess::NotRunning)
                guarded->kill();
        });
    };
    stop(m_cloudflaredProcess,
         QStringLiteral("Cloudflare Tunnel connector"));
    stop(m_mirrorGatewayProcess,
         QStringLiteral("Mirror gateway"));
    m_directMirrorGatewayHealthy = false;
    m_directMirrorEndpointRegistered = false;
    refreshControlNode();
}

void MainWindow::checkDirectMirrorGatewayHealth()
{
    if (!m_networkAccess || !m_mirrorGatewayProcess ||
        m_mirrorGatewayProcess->state() ==
            QProcess::NotRunning)
        return;
    QNetworkRequest request(
        QUrl(QStringLiteral("http://127.0.0.1:8790/health")));
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] {
                const int status =
                    reply
                        ->attribute(
                            QNetworkRequest::
                                HttpStatusCodeAttribute)
                        .toInt();
                const QJsonObject object =
                    QJsonDocument::fromJson(
                        reply->readAll())
                        .object();
                m_directMirrorGatewayHealthy =
                    reply->error() ==
                        QNetworkReply::NoError &&
                    status == 200 &&
                    object.value(QStringLiteral("ok"))
                        .toBool() &&
                    object
                            .value(QStringLiteral("transport"))
                            .toString() ==
                        QLatin1String("direct-https");
                reply->deleteLater();
                refreshControlNode();
            });
}

void MainWindow::registerDirectMirrorEndpoint()
{









    const QFileInfo connectorTokenInfo(directGatewayConnectorTokenPath());
    const bool connectorProvisioned =
        (m_cloudflaredProcess &&
         m_cloudflaredProcess->state() != QProcess::NotRunning) ||
        (connectorTokenInfo.isFile() && !connectorTokenInfo.isSymLink());
    if (!m_networkAccess || !m_directMirrorGatewayHealthy ||
        !connectorProvisioned ||
        (!m_profileIdentity.isValid() &&
         !m_profileIdentity.load()))
        return;
    const QString node =
        QSettings()
            .value(QStringLiteral(
                "control/cloudflareNodeName"))
            .toString()
            .trimmed()
            .toLower();
    const QString workerHost =
        QSettings()
            .value(QStringLiteral(
                "control/cloudflareHostname"))
            .toString()
            .trimmed()
            .toLower();
    const QString mirrorHost =
        QSettings()
            .value(QStringLiteral(
                "control/cloudflareMirrorHostname"))
            .toString()
            .trimmed()
            .toLower();
    const QString baseUrl =
        normalizedHttpsOrigin(mirrorHost);
    const qint64 issuedAt =
        QDateTime::currentMSecsSinceEpoch();
    QString error;
    const QByteArray canonical =
        forkmesh::control::
            httpsMirrorRegistrationSigningPayload(
                node, baseUrl,
                m_profileIdentity.publicKey(),
                issuedAt, &error);
    const QString signature =
        canonical.isEmpty()
            ? QString()
            : m_profileIdentity.signData(canonical);
    if (workerHost.isEmpty() || signature.isEmpty())
        return;
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(workerHost);
    url.setPath(QStringLiteral("/api/mirrors/https"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    const QJsonObject payload{
        {QStringLiteral("node"), node},
        {QStringLiteral("baseUrl"), baseUrl},
        {QStringLiteral("publicKey"),
         m_profileIdentity.publicKey()},
        {QStringLiteral("issuedAt"), double(issuedAt)},
        {QStringLiteral("signature"), signature},
    };
    QNetworkReply *reply = m_networkAccess->post(
        request,
        QJsonDocument(payload).toJson(
            QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, node, baseUrl] {
                const int status =
                    reply
                        ->attribute(
                            QNetworkRequest::
                                HttpStatusCodeAttribute)
                        .toInt();
                const QJsonObject object =
                    QJsonDocument::fromJson(
                        reply->readAll())
                        .object();
                const bool ok =
                    reply->error() ==
                        QNetworkReply::NoError &&
                    status >= 200 && status < 300 &&
                    object.value(QStringLiteral("ok"))
                        .toBool() &&
                    object.value(QStringLiteral("node"))
                            .toString() ==
                        node &&
                    object.value(QStringLiteral("baseUrl"))
                            .toString() ==
                        baseUrl &&
                    object.value(QStringLiteral("health"))
                            .toString() ==
                        QLatin1String("active") &&
                    object
                            .value(QStringLiteral(
                                "routerPublicKey"))
                            .toString() ==
                        m_directMirrorRouterPublicKey;
                reply->deleteLater();
                m_directMirrorEndpointRegistered = ok;
                if (ok) {
                    QSettings().setValue(
                        QStringLiteral(
                            "control/directMirrorRegisteredAt"),
                        QDateTime::currentMSecsSinceEpoch());
                    appendControlNodeOutput(
                        QStringLiteral(
                            "Signed direct HTTPS endpoint registration "
                            "accepted; health verification is pending/active "
                            "at the Worker.\n"));
                }
                refreshControlNode();
            });
}

void MainWindow::provisionDirectMirrorEndpoint(bool dryRun)
{
    auto releaseDeploymentUi = [this] {
        m_cloudflareInstallVpsAfterDeploy = false;
        if (m_cloudflareDryRunButton)
            m_cloudflareDryRunButton->setEnabled(true);
        if (m_cloudflareDeployButton)
            m_cloudflareDeployButton->setEnabled(true);
        if (m_cloudflareCancelButton)
            m_cloudflareCancelButton->setEnabled(false);
        if (m_cloudflareTokenEdit)
            m_cloudflareTokenEdit->setPlaceholderText(
                QStringLiteral("session-only Cloudflare API token"));
        m_cloudflareActiveSecret.fill(QChar(u'\0'));
        m_cloudflareActiveSecret.clear();
    };
    if (m_cloudflareTunnelBootstrapProcess &&
        m_cloudflareTunnelBootstrapProcess->state() !=
            QProcess::NotRunning)
        return;
    if (!m_networkAccess && !dryRun) {
        flashMessage(
            QStringLiteral(
                "Network access is unavailable for mirror router discovery."),
            true);
        releaseDeploymentUi();
        return;
    }
    const QString mirrorHostname =
        m_cloudflareMirrorHostnameEdit
            ? m_cloudflareMirrorHostnameEdit->text()
                  .trimmed()
                  .toLower()
            : QString();
    forkmesh::control::CloudflareBootstrapRequest request;
    request.hostname = mirrorHostname;
    request.zoneName =
        m_cloudflareZoneEdit
            ? m_cloudflareZoneEdit->text()
            : QString();
    request.accountId =
        m_cloudflareAccountEdit
            ? m_cloudflareAccountEdit->text()
            : QString();
    request.nodeName =
        m_cloudflareNodeNameEdit
            ? m_cloudflareNodeNameEdit->text()
            : QString();
    request.mainRelayUrl =
        m_cloudflareMainRelayEdit
            ? m_cloudflareMainRelayEdit->text()
            : QString();
    request.dryRun = dryRun;
    const QString validation =
        forkmesh::control::
            validateCloudflareBootstrapRequest(request);
    if (!validation.isEmpty() ||
        mirrorHostname == m_cloudflareDeployHostname) {
        flashMessage(
            validation.isEmpty()
                ? QStringLiteral(
                      "The Worker relay and direct mirror endpoint require "
                      "different hostnames.")
                : validation,
            true);
        releaseDeploymentUi();
        return;
    }
    const QString tunnelScript =
        forkmesh::control::
            findCloudflareTunnelBootstrapScript(
                QStringLiteral(FORKMESH_SOURCE_DIR),
                QCoreApplication::applicationDirPath());
    QString python =
        QStandardPaths::findExecutable(
            QStringLiteral("python3"));
    if (python.isEmpty())
        python = QStandardPaths::findExecutable(
            QStringLiteral("python"));
    if (tunnelScript.isEmpty() || python.isEmpty() ||
        m_cloudflareActiveSecret.isEmpty()) {
        flashMessage(
            QStringLiteral(
                "The Tunnel bootstrap, Python 3, or session API token is "
                "unavailable."),
            true);
        releaseDeploymentUi();
        return;
    }

    auto launchBootstrap =
        [this, request, mirrorHostname, tunnelScript, python,
         dryRun, releaseDeploymentUi](const QString &routerKey) {
            if (!dryRun) {
                m_directMirrorRouterPublicKey = routerKey;
                m_directMirrorHostname = mirrorHostname;
                QString configError;
                if (!rebuildDirectMirrorGatewayConfiguration(
                        &configError, false)) {
                    appendControlNodeOutput(
                        QStringLiteral(
                            "Direct mirror configuration failed: %1\n")
                            .arg(configError));
                    flashMessage(configError, true);
                    releaseDeploymentUi();
                    return;
                }
            }
            const auto command =
                forkmesh::control::
                    buildCloudflareTunnelBootstrapCommand(
                        request, mirrorHostname,
                        m_cloudflareActiveSecret,
                        tunnelScript, python,
                        QCoreApplication::applicationFilePath(),
                        m_profileIdentity.publicKey(),
                        directGatewayConfigPath(),
                        directGatewayManifestPath(),
                        directGatewayConnectorTokenPath());
            if (command.arguments.join(QChar(u'\0'))
                    .contains(
                        m_cloudflareActiveSecret)) {
                flashMessage(
                    QStringLiteral(
                        "Refusing an unsafe Tunnel command containing a "
                        "credential."),
                    true);
                releaseDeploymentUi();
                return;
            }
            auto *process = new QProcess(this);
            m_cloudflareTunnelBootstrapProcess = process;
            process->setProcessEnvironment(
                command.environment);
            process->setProcessChannelMode(
                QProcess::MergedChannels);
            connect(
                process,
                &QProcess::readyReadStandardOutput,
                this, [this, process] {
                    appendControlNodeOutput(
                        QString::fromUtf8(
                            process
                                ->readAllStandardOutput()));
                });
            connect(
                process, &QProcess::finished, this,
                [this, process, dryRun](
                    int exitCode,
                    QProcess::ExitStatus status) {
                    appendControlNodeOutput(
                        QString::fromUtf8(
                            process
                                ->readAllStandardOutput()));
                    const bool ok =
                        status ==
                            QProcess::NormalExit &&
                        exitCode == 0;
                    if (ok && !dryRun) {
                        appendControlNodeOutput(
                            QStringLiteral(
                                "Tunnel/DNS/manifest provisioning completed. "
                                "Starting local direct HTTPS services.\n"));
                        startDirectMirrorServices();
                        QTimer::singleShot(
                            2500, this,
                            &MainWindow::
                                registerDirectMirrorEndpoint);
                        if (m_cloudflareConnectAfterDeploy)
                            connectToDeployedRelay(
                                m_cloudflareDeployHostname);
                        installCloudflareFirstMirror();
                    } else if (ok) {
                        appendControlNodeOutput(
                            QStringLiteral(
                                "Worker and Tunnel validation completed.\n"));
                    } else {
                        m_cloudflareInstallVpsAfterDeploy = false;
                        appendControlNodeOutput(
                            QStringLiteral(
                                "Tunnel provisioning failed (exit %1).\n")
                                .arg(exitCode));
                        flashMessage(
                            QStringLiteral(
                                "Direct mirror Tunnel provisioning failed; "
                                "see the local output."),
                            true);
                    }
                    process->setProcessEnvironment(
                        QProcessEnvironment());
                    if (m_cloudflareTunnelBootstrapProcess ==
                        process) {
                        m_cloudflareTunnelBootstrapProcess =
                            nullptr;
                    }
                    process->deleteLater();
                    if (m_cloudflareDryRunButton)
                        m_cloudflareDryRunButton
                            ->setEnabled(true);
                    if (m_cloudflareDeployButton)
                        m_cloudflareDeployButton
                            ->setEnabled(true);
                    if (m_cloudflareCancelButton)
                        m_cloudflareCancelButton
                            ->setEnabled(false);
                    if (m_cloudflareTokenEdit)
                        m_cloudflareTokenEdit
                            ->setPlaceholderText(
                                QStringLiteral(
                                    "session-only Cloudflare API token"));
                    m_cloudflareActiveSecret.fill(
                        QChar(u'\0'));
                    m_cloudflareActiveSecret.clear();
                    refreshControlNode();
                });
            process->start(command.program,
                           command.arguments);
        };

    if (dryRun) {
        launchBootstrap(QString());
        return;
    }
    QUrl routerUrl;
    routerUrl.setScheme(QStringLiteral("https"));
    routerUrl.setHost(m_cloudflareDeployHostname);
    routerUrl.setPath(
        QStringLiteral("/api/mirrors/https"));
    QNetworkRequest routerRequest(routerUrl);
    routerRequest.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply =
        m_networkAccess->get(routerRequest);
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, releaseDeploymentUi,
         launchBootstrap = std::move(launchBootstrap)]() mutable {
            const int status =
                reply
                    ->attribute(
                        QNetworkRequest::
                            HttpStatusCodeAttribute)
                    .toInt();
            const QJsonObject object =
                QJsonDocument::fromJson(
                    reply->readAll())
                    .object();
            const QString routerKey =
                object
                    .value(QStringLiteral(
                        "routerPublicKey"))
                    .toString();
            const bool ok =
                reply->error() ==
                    QNetworkReply::NoError &&
                status == 200 &&
                object.value(QStringLiteral("ok"))
                    .toBool() &&
                validRouterPublicKey(routerKey);
            reply->deleteLater();
            if (!ok) {
                appendControlNodeOutput(
                    QStringLiteral(
                        "The deployed Worker has no valid mirror router "
                        "public key. Configure its dedicated "
                        "MIRROR_ROUTER_PUBLIC_KEY/signing seed before "
                        "registering endpoints.\n"));
                flashMessage(
                    QStringLiteral(
                        "Direct mirror routing is not enabled on the "
                        "deployed Worker."),
                    true);
                releaseDeploymentUi();
                return;
            }
            launchBootstrap(routerKey);
        });
}

void MainWindow::runCloudflareBootstrap(bool dryRun)
{
    if ((m_cloudflareBootstrapProcess &&
         m_cloudflareBootstrapProcess->state() != QProcess::NotRunning) ||
        (m_cloudflareTunnelBootstrapProcess &&
         m_cloudflareTunnelBootstrapProcess->state() !=
             QProcess::NotRunning)) {
        flashMessage(QStringLiteral("A Cloudflare deployment is already running."),
                     true);
        return;
    }
    forkmesh::control::CloudflareBootstrapRequest request;
    request.hostname =
        m_cloudflareHostnameEdit ? m_cloudflareHostnameEdit->text() : QString();
    request.zoneName =
        m_cloudflareZoneEdit ? m_cloudflareZoneEdit->text() : QString();
    request.accountId =
        m_cloudflareAccountEdit ? m_cloudflareAccountEdit->text() : QString();
    request.nodeName =
        m_cloudflareNodeNameEdit ? m_cloudflareNodeNameEdit->text() : QString();
    const QString mirrorHostname =
        m_cloudflareMirrorHostnameEdit
            ? m_cloudflareMirrorHostnameEdit->text().trimmed().toLower()
            : QString();
    const bool automaticTopology =
        request.hostname.trimmed().isEmpty() &&
        request.zoneName.trimmed().isEmpty() &&
        mirrorHostname.isEmpty();
    if (!automaticTopology && request.nodeName.trimmed().isEmpty()) {
        request.nodeName =
            mirrorHostname.section(QLatin1Char('.'), 0, 0);
        if (m_cloudflareNodeNameEdit)
            m_cloudflareNodeNameEdit->setText(request.nodeName);
    }
    request.relayLabel =
        m_cloudflareRelayLabelEdit ? m_cloudflareRelayLabelEdit->text() : QString();
    request.mainRelayUrl =
        m_cloudflareMainRelayEdit ? m_cloudflareMainRelayEdit->text() : QString();
    request.dryRun = dryRun;
    const QString requestError =
        forkmesh::control::validateCloudflareBootstrapRequest(
            request, automaticTopology);
    if (!requestError.isEmpty()) {
        flashMessage(requestError, true);
        return;
    }
    if (!automaticTopology) {
        forkmesh::control::CloudflareBootstrapRequest mirrorRequest =
            request;
        mirrorRequest.hostname = mirrorHostname;
        const QString mirrorRequestError =
            forkmesh::control::validateCloudflareBootstrapRequest(
                mirrorRequest);
        if (!mirrorRequestError.isEmpty() ||
            mirrorHostname ==
                request.hostname.trimmed().toLower()) {
            flashMessage(
                mirrorRequestError.isEmpty()
                    ? QStringLiteral(
                          "The relay hostname and direct mirror endpoint must "
                          "be different.")
                    : mirrorRequestError,
                true);
            return;
        }
    }
    const QString apiToken =
        m_cloudflareTokenEdit ? m_cloudflareTokenEdit->text().trimmed() : QString();
    if (apiToken.isEmpty()) {
        flashMessage(
            QStringLiteral("Enter a scoped Cloudflare API token for this run."),
            true);
        return;
    }
    const bool installFirstMirror =
        !dryRun && m_cloudflareInstallVpsCheck &&
        m_cloudflareInstallVpsCheck->isChecked();
    if (installFirstMirror &&
        ((!m_cloudflareVpsHostEdit ||
          m_cloudflareVpsHostEdit->text().trimmed().isEmpty()) ||
         (!m_cloudflareVpsUserEdit ||
          m_cloudflareVpsUserEdit->text().trimmed().isEmpty()))) {
        flashMessage(
            QStringLiteral(
                "Enter the first mirror VPS host and SSH user, or disable "
                "the post-deployment VPS install."),
            true);
        return;
    }
    if ((!m_profileIdentity.isValid() && !m_profileIdentity.load()) ||
        !m_profileIdentity.isValid()) {
        flashMessage(
            QStringLiteral(
                "The local identity key is unavailable, so ForkMesh cannot sign "
                "the mirror endpoint manifest."),
            true);
        return;
    }
    const QString script =
        forkmesh::control::findCloudflareBootstrapScript(
            QStringLiteral(FORKMESH_SOURCE_DIR),
            QCoreApplication::applicationDirPath());
    if (script.isEmpty()) {
        flashMessage(
            QStringLiteral(
                "The bundled Cloudflare bootstrap or its complete Worker "
                "source/static/migration resource tree could not be found."),
            true);
        return;
    }
    QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty())
        python = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (python.isEmpty()) {
        flashMessage(QStringLiteral("Python 3 is required for Cloudflare deployment."),
                     true);
        return;
    }

    const auto command =
        forkmesh::control::buildCloudflareBootstrapCommand(
            request, apiToken, script, python,
            QString(), QString());






    if (command.arguments.join(QChar(u'\0')).contains(apiToken)) {
        flashMessage(
            QStringLiteral("Refusing an unsafe deployment command containing a credential."),
            true);
        return;
    }

    QSettings settings;
    if (!automaticTopology) {
        settings.setValue(QStringLiteral("control/cloudflareHostname"),
                          request.hostname.trimmed().toLower());
        settings.setValue(
            QStringLiteral("control/cloudflareMirrorHostname"),
            mirrorHostname);
        settings.setValue(QStringLiteral("control/cloudflareZone"),
                          request.zoneName.trimmed().toLower());
        settings.setValue(QStringLiteral("control/cloudflareAccount"),
                          request.accountId.trimmed());
        settings.setValue(QStringLiteral("control/cloudflareNodeName"),
                          request.nodeName.trimmed());
        settings.setValue(QStringLiteral("control/cloudflareRelayLabel"),
                          request.relayLabel.trimmed());
        settings.setValue(QStringLiteral("control/cloudflareMainRelay"),
                          request.mainRelayUrl.trimmed());
    }

    m_cloudflareDeployHostname = request.hostname.trimmed().toLower();
    m_cloudflareConnectAfterDeploy =
        !dryRun && m_cloudflareConnectCheck &&
        m_cloudflareConnectCheck->isChecked();
    m_cloudflareInstallVpsAfterDeploy = installFirstMirror;
    m_cloudflareActiveSecret = apiToken;
    if (m_cloudflareTokenEdit) {
        m_cloudflareTokenEdit->clear();
        m_cloudflareTokenEdit->setPlaceholderText(
            QStringLiteral("token held in memory only while deployment runs"));
    }
    if (m_controlNodeOutput)
        m_controlNodeOutput->clear();
    appendControlNodeOutput(
        QStringLiteral("%1 Cloudflare deployment for %2.\n")
            .arg(dryRun ? QStringLiteral("Validating")
                        : QStringLiteral("Starting"),
                 automaticTopology
                     ? QStringLiteral(
                           "the single account/zone visible to this token")
                     : m_cloudflareDeployHostname));
    if (m_cloudflareDryRunButton)
        m_cloudflareDryRunButton->setEnabled(false);
    if (m_cloudflareDeployButton)
        m_cloudflareDeployButton->setEnabled(false);
    if (m_cloudflareCancelButton)
        m_cloudflareCancelButton->setEnabled(true);

    auto *process = new QProcess(this);
    m_cloudflareBootstrapProcess = process;
    process->setProperty("forkmeshBootstrapOutput", QByteArray());
    process->setProcessEnvironment(command.environment);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        const QByteArray chunk = process->readAllStandardOutput();
        QByteArray captured =
            process->property("forkmeshBootstrapOutput").toByteArray();
        if (captured.size() < 1024 * 1024)
            captured.append(chunk.left(1024 * 1024 - captured.size()));
        process->setProperty("forkmeshBootstrapOutput", captured);
        appendControlNodeOutput(QString::fromUtf8(chunk));
    });
    connect(process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    appendControlNodeOutput(
                        QStringLiteral(
                            "Deployment process could not start; verify Python "
                            "and the installed ForkMesh deployment resources.\n"));
                }
            });
    connect(process, &QProcess::finished, this,
            [this, process, dryRun, automaticTopology](int exitCode,
                                    QProcess::ExitStatus exitStatus) {
                const QByteArray trailing =
                    process->readAllStandardOutput();
                QByteArray captured =
                    process
                        ->property("forkmeshBootstrapOutput")
                        .toByteArray();
                if (captured.size() < 1024 * 1024)
                    captured.append(
                        trailing.left(
                            1024 * 1024 -
                            captured.size()));
                appendControlNodeOutput(
                    QString::fromUtf8(trailing));
                QString resultError;
                const QJsonObject result =
                    forkmesh::control::
                        parseCloudflareBootstrapResult(
                            captured, &resultError);
                bool ok =
                    exitStatus == QProcess::NormalExit &&
                    exitCode == 0 &&
                    !result.isEmpty();
                if (ok && automaticTopology) {
                    forkmesh::control::
                        CloudflareBootstrapRequest resolved;
                    resolved.hostname =
                        result
                            .value(QStringLiteral("hostname"))
                            .toString();
                    resolved.zoneName =
                        result
                            .value(QStringLiteral("zoneName"))
                            .toString();
                    resolved.accountId =
                        result
                            .value(QStringLiteral("accountId"))
                            .toString();
                    resolved.nodeName =
                        result
                            .value(QStringLiteral("nodeName"))
                            .toString();
                    resolved.relayLabel =
                        m_cloudflareRelayLabelEdit
                            ? m_cloudflareRelayLabelEdit
                                  ->text()
                            : QString();
                    resolved.mainRelayUrl =
                        m_cloudflareMainRelayEdit
                            ? m_cloudflareMainRelayEdit
                                  ->text()
                            : QString();
                    const QString directHostname =
                        result
                            .value(QStringLiteral(
                                "directMirrorHostname"))
                            .toString()
                            .trimmed()
                            .toLower();
                    auto directRequest = resolved;
                    directRequest.hostname =
                        directHostname;
                    const QString relayError =
                        forkmesh::control::
                            validateCloudflareBootstrapRequest(
                                resolved);
                    const QString directError =
                        forkmesh::control::
                            validateCloudflareBootstrapRequest(
                                directRequest);
                    if (!relayError.isEmpty() ||
                        !directError.isEmpty() ||
                        directHostname ==
                            resolved.hostname
                                .trimmed()
                                .toLower()) {
                        ok = false;
                        resultError = QStringLiteral(
                            "Automatic Cloudflare discovery returned "
                            "unsafe topology.");
                    } else {
                        if (m_cloudflareHostnameEdit)
                            m_cloudflareHostnameEdit
                                ->setText(
                                    resolved.hostname);
                        if (m_cloudflareMirrorHostnameEdit)
                            m_cloudflareMirrorHostnameEdit
                                ->setText(
                                    directHostname);
                        if (m_cloudflareZoneEdit)
                            m_cloudflareZoneEdit
                                ->setText(
                                    resolved.zoneName);
                        if (m_cloudflareAccountEdit)
                            m_cloudflareAccountEdit
                                ->setText(
                                    resolved.accountId);
                        if (m_cloudflareNodeNameEdit)
                            m_cloudflareNodeNameEdit
                                ->setText(
                                    resolved.nodeName);
                        QSettings settings;
                        settings.setValue(
                            QStringLiteral(
                                "control/cloudflareHostname"),
                            resolved.hostname);
                        settings.setValue(
                            QStringLiteral(
                                "control/cloudflareMirrorHostname"),
                            directHostname);
                        settings.setValue(
                            QStringLiteral(
                                "control/cloudflareZone"),
                            resolved.zoneName);
                        settings.setValue(
                            QStringLiteral(
                                "control/cloudflareAccount"),
                            resolved.accountId);
                        settings.setValue(
                            QStringLiteral(
                                "control/cloudflareNodeName"),
                            resolved.nodeName);
                        settings.setValue(
                            QStringLiteral(
                                "control/cloudflareRelayLabel"),
                            resolved.relayLabel.trimmed());
                        settings.setValue(
                            QStringLiteral(
                                "control/cloudflareMainRelay"),
                            resolved.mainRelayUrl.trimmed());
                        m_cloudflareDeployHostname =
                            resolved.hostname
                                .trimmed()
                                .toLower();
                        appendControlNodeOutput(
                            QStringLiteral(
                                "Discovered one unambiguous active zone; "
                                "using relay %1 and direct mirror %2.\n")
                                .arg(
                                    m_cloudflareDeployHostname,
                                    directHostname));
                    }
                }
                if (ok) {
                    appendControlNodeOutput(
                        dryRun
                            ? QStringLiteral(
                                  "Worker validation completed; validating "
                                  "the direct mirror Tunnel next.\n")
                            : QStringLiteral(
                                  "Worker deployment completed; provisioning "
                                  "the direct mirror Tunnel next.\n"));
                    logSystem(
                        QStringLiteral("Control node: Cloudflare %1 completed for %2.")
                            .arg(dryRun ? QStringLiteral("validation")
                                        : QStringLiteral("deployment"),
                                 m_cloudflareDeployHostname));
                } else {
                    m_cloudflareInstallVpsAfterDeploy = false;
                    if (!resultError.isEmpty()) {
                        appendControlNodeOutput(
                            QStringLiteral(
                                "Machine result rejected: %1\n")
                                .arg(resultError));
                    }
                    appendControlNodeOutput(
                        QStringLiteral("Deployment failed (exit %1). Review the "
                                       "redacted local output above.\n")
                            .arg(exitCode));
                    logSystem(
                        QStringLiteral("Control node: Cloudflare deployment failed "
                                       "for %1 (exit %2).")
                            .arg(m_cloudflareDeployHostname)
                            .arg(exitCode));
                    flashMessage(
                        QStringLiteral("Cloudflare deployment failed; see the local "
                                       "control-node output."),
                        true);
                }



                process->setProcessEnvironment(QProcessEnvironment());
                process->deleteLater();
                if (m_cloudflareBootstrapProcess == process)
                    m_cloudflareBootstrapProcess = nullptr;
                if (ok) {
                    provisionDirectMirrorEndpoint(dryRun);
                    refreshControlNode();
                    return;
                }
                if (m_cloudflareDryRunButton)
                    m_cloudflareDryRunButton->setEnabled(true);
                if (m_cloudflareDeployButton)
                    m_cloudflareDeployButton->setEnabled(true);
                if (m_cloudflareCancelButton)
                    m_cloudflareCancelButton->setEnabled(false);
                if (m_cloudflareTokenEdit)
                    m_cloudflareTokenEdit->setPlaceholderText(
                        QStringLiteral(
                            "session-only Cloudflare API token"));
                m_cloudflareActiveSecret.fill(QChar(u'\0'));
                m_cloudflareActiveSecret.clear();
                refreshControlNode();
            });
    process->start(command.program, command.arguments);
}

void MainWindow::cancelCloudflareBootstrap()
{
    QProcess *active =
        (m_cloudflareBootstrapProcess &&
         m_cloudflareBootstrapProcess->state() !=
             QProcess::NotRunning)
            ? m_cloudflareBootstrapProcess
            : ((m_cloudflareTunnelBootstrapProcess &&
                m_cloudflareTunnelBootstrapProcess->state() !=
                    QProcess::NotRunning)
                   ? m_cloudflareTunnelBootstrapProcess
                   : nullptr);
    if (!active)
        return;
    appendControlNodeOutput(
        QStringLiteral("Cancellation requested; waiting for local cleanup.\n"));
    active->terminate();
    QPointer<QProcess> process(active);
    QTimer::singleShot(5000, this, [process] {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void MainWindow::appendControlNodeOutput(const QString &text)
{
    if (!m_controlNodeOutput || text.isEmpty())
        return;
    const QString safe = forkmesh::control::redactProcessOutput(
        text, {m_cloudflareActiveSecret});
    m_controlNodeOutput->moveCursor(QTextCursor::End);
    m_controlNodeOutput->insertPlainText(safe);
    m_controlNodeOutput->moveCursor(QTextCursor::End);
    m_controlNodeOutput->ensureCursorVisible();
}

QWidget *MainWindow::buildSiteDeployCard()
{
    QVBoxLayout *col = nullptr;
    QFrame *card =
        controlCard(QStringLiteral("ForkMesh site deployment"), &col);
    col->addWidget(controlHint(
        QStringLiteral(
            "Runs this checkout's cloudflare_worker/deploy.sh: it builds the "
            "site, uploads the Worker and static assets, pushes the "
            ".env.production secrets, and then verifies the live origin is "
            "serving the new build. Credentials come from the deployment "
            "machine's own wrangler login and .env.production; nothing is read "
            "from or written to this page. Output streams below as the script "
            "runs.")));
    m_siteDeployStatus = new QLabel;
    m_siteDeployStatus->setObjectName(QStringLiteral("mutedLabel"));
    m_siteDeployStatus->setWordWrap(true);
    const QString deployScript = forkmesh::control::findSiteDeployScript(
        QStringLiteral(FORKMESH_SOURCE_DIR),
        QCoreApplication::applicationDirPath());
    m_siteDeployStatus->setText(
        deployScript.isEmpty()
            ? QStringLiteral(
                  "cloudflare_worker/deploy.sh was not found next to this "
                  "build's Worker bundle.")
            : QStringLiteral("Ready: %1").arg(deployScript));
    col->addWidget(m_siteDeployStatus);

    auto *buttons = new QHBoxLayout;
    m_siteDeployButton =
        new QPushButton(QStringLiteral("Deploy to Cloudflare"));
    m_siteDeployButton->setObjectName(QStringLiteral("siteDeployButton"));
    m_siteDeployButton->setProperty("buttonSize", "primary");
    m_siteDeployButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_siteDeployButton, QStringLiteral("rocket"), 14);
    connect(m_siteDeployButton, &QPushButton::clicked, this,
            &MainWindow::runSiteDeploy);
    buttons->addWidget(m_siteDeployButton);
    m_siteDeployCancelButton = new QPushButton(QStringLiteral("Cancel"));
    m_siteDeployCancelButton->setObjectName(
        QStringLiteral("siteDeployCancelButton"));
    m_siteDeployCancelButton->setCursor(Qt::PointingHandCursor);
    m_siteDeployCancelButton->setEnabled(false);
    connect(m_siteDeployCancelButton, &QPushButton::clicked, this,
            &MainWindow::cancelSiteDeploy);
    buttons->addWidget(m_siteDeployCancelButton);
    buttons->addStretch();
    col->addLayout(buttons);

    m_siteDeployOutput = new QPlainTextEdit;
    m_siteDeployOutput->setObjectName(QStringLiteral("siteDeployOutput"));
    m_siteDeployOutput->setReadOnly(true);
    m_siteDeployOutput->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_siteDeployOutput->setMinimumHeight(220);
    m_siteDeployOutput->document()->setMaximumBlockCount(5000);
    m_siteDeployOutput->setPlaceholderText(
        QStringLiteral("deploy.sh output appears here live; credentials are "
                       "redacted."));
    QFont deployMono(QStringLiteral("monospace"));
    deployMono.setStyleHint(QFont::Monospace);
    m_siteDeployOutput->setFont(deployMono);
    col->addWidget(m_siteDeployOutput);
    return card;
}

void MainWindow::runSiteDeploy()
{
    if (m_siteDeployProcess &&
        m_siteDeployProcess->state() != QProcess::NotRunning) {
        flashMessage(QStringLiteral("A site deployment is already running."),
                     true);
        return;
    }
    const QString script = forkmesh::control::findSiteDeployScript(
        QStringLiteral(FORKMESH_SOURCE_DIR),
        QCoreApplication::applicationDirPath());
    if (script.isEmpty()) {
        const QString missing = QStringLiteral(
            "cloudflare_worker/deploy.sh was not found next to this build's "
            "Worker bundle.");
        if (m_siteDeployStatus)
            m_siteDeployStatus->setText(missing);
        flashMessage(missing, true);
        return;
    }
    const QString bash = QStandardPaths::findExecutable(QStringLiteral("bash"));
    if (bash.isEmpty()) {
        flashMessage(
            QStringLiteral("bash is required to run cloudflare_worker/deploy.sh."),
            true);
        return;
    }
    if (m_siteDeployOutput)
        m_siteDeployOutput->clear();
    appendSiteDeployOutput(
        QStringLiteral("$ %1\n").arg(script));
    if (m_siteDeployStatus)
        m_siteDeployStatus->setText(QStringLiteral("Deploying…"));
    if (m_siteDeployButton)
        m_siteDeployButton->setEnabled(false);
    if (m_siteDeployCancelButton)
        m_siteDeployCancelButton->setEnabled(true);
    logSystem(QStringLiteral("Control node: site deployment started."));

    auto *process = new QProcess(this);
    m_siteDeployProcess = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(QFileInfo(script).absolutePath());
    QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();


    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    process->setProcessEnvironment(environment);
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process] {
                appendSiteDeployOutput(
                    QString::fromUtf8(process->readAllStandardOutput()));
            });
    connect(process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    appendSiteDeployOutput(
                        QStringLiteral(
                            "deploy.sh could not start; verify bash and the "
                            "Worker bundle on this machine.\n"));
                }
            });
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
                appendSiteDeployOutput(
                    QString::fromUtf8(process->readAllStandardOutput()));
                const bool ok =
                    status == QProcess::NormalExit && exitCode == 0;
                appendSiteDeployOutput(
                    ok ? QStringLiteral("\nDeployment finished successfully.\n")
                       : QStringLiteral("\nDeployment failed (exit %1).\n")
                             .arg(exitCode));
                if (m_siteDeployStatus)
                    m_siteDeployStatus->setText(
                        ok ? QStringLiteral("Last deployment succeeded.")
                           : QStringLiteral("Last deployment failed (exit %1).")
                                 .arg(exitCode));
                if (m_siteDeployButton)
                    m_siteDeployButton->setEnabled(true);
                if (m_siteDeployCancelButton)
                    m_siteDeployCancelButton->setEnabled(false);
                logSystem(ok
                              ? QStringLiteral(
                                    "Control node: site deployment succeeded.")
                              : QStringLiteral(
                                    "Control node: site deployment failed "
                                    "(exit %1).")
                                    .arg(exitCode));
                if (m_siteDeployProcess == process)
                    m_siteDeployProcess = nullptr;
                process->deleteLater();
            });
    process->start(bash, {script});
}

void MainWindow::cancelSiteDeploy()
{
    if (!m_siteDeployProcess ||
        m_siteDeployProcess->state() == QProcess::NotRunning) {
        return;
    }
    appendSiteDeployOutput(
        QStringLiteral("Cancellation requested; waiting for deploy.sh to "
                       "stop.\n"));
    m_siteDeployProcess->terminate();
    QPointer<QProcess> process(m_siteDeployProcess);
    QTimer::singleShot(5000, this, [process] {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void MainWindow::appendSiteDeployOutput(const QString &text)
{
    if (!m_siteDeployOutput || text.isEmpty())
        return;
    const QString safe = forkmesh::control::redactProcessOutput(
        text, {m_cloudflareActiveSecret});
    m_siteDeployOutput->moveCursor(QTextCursor::End);
    m_siteDeployOutput->insertPlainText(safe);
    m_siteDeployOutput->moveCursor(QTextCursor::End);
    m_siteDeployOutput->ensureCursorVisible();
}

QWidget *MainWindow::buildCloudflareTokenCard()
{
    QVBoxLayout *col = nullptr;
    QFrame *card =
        controlCard(QStringLiteral("Cloudflare API token"), &col);
    col->addWidget(controlHint(
        QStringLiteral(
            "Checks a Cloudflare API token against the exact permissions this "
            "checkout's deploy path needs. The token's own policy is read when "
            "it may read itself; otherwise each capability is confirmed with a "
            "read-only probe, which proves reach but never edit rights. "
            "Generating a replacement mints a token scoped to the table below "
            "and stores it in this device's CLOUDFLARE_API_TOKEN variable and in "
            "cloudflare_worker/.env.production — the two places deploys read it "
            "from. Token values are shown only as their last four characters "
            "and are redacted from the output below; the previous token stays "
            "valid until you delete it in the Cloudflare dashboard.")));

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    m_controlTokenEdit = new QLineEdit(
        forkmesh::control::cloudflareApiTokenFromVariables(
            ActionStore::variables()));
    m_controlTokenEdit->setObjectName(QStringLiteral("controlTokenValue"));
    m_controlTokenEdit->setEchoMode(QLineEdit::Password);
    m_controlTokenEdit->setClearButtonEnabled(true);
    m_controlTokenEdit->setPlaceholderText(
        QStringLiteral("empty: use this device's saved CLOUDFLARE_API_TOKEN"));
    form->addRow(QStringLiteral("API token"), m_controlTokenEdit);
    col->addLayout(form);

    auto *buttons = new QHBoxLayout;
    m_controlTokenTestButton =
        new QPushButton(QStringLiteral("Check token permissions"));
    m_controlTokenTestButton->setObjectName(
        QStringLiteral("controlTokenTestButton"));
    m_controlTokenTestButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_controlTokenTestButton, QStringLiteral("shield-check"), 14);
    connect(m_controlTokenTestButton, &QPushButton::clicked, this,
            &MainWindow::testCloudflareApiToken);
    buttons->addWidget(m_controlTokenTestButton);
    m_controlTokenGenerateButton =
        new QPushButton(QStringLiteral("Generate and install a new token"));
    m_controlTokenGenerateButton->setObjectName(
        QStringLiteral("controlTokenGenerateButton"));
    m_controlTokenGenerateButton->setProperty("buttonSize", "primary");
    m_controlTokenGenerateButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_controlTokenGenerateButton, QStringLiteral("key"), 14);
    connect(m_controlTokenGenerateButton, &QPushButton::clicked, this,
            &MainWindow::generateCloudflareApiToken);
    buttons->addWidget(m_controlTokenGenerateButton);
    buttons->addStretch();
    col->addLayout(buttons);

    m_controlTokenStatus = new QLabel(
        QStringLiteral("No token has been checked in this session."));
    m_controlTokenStatus->setObjectName(QStringLiteral("controlTokenStatus"));
    m_controlTokenStatus->setWordWrap(true);
    m_controlTokenStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    col->addWidget(m_controlTokenStatus);

    m_controlTokenTable = new QTableWidget(0, 5);
    m_controlTokenTable->setObjectName(
        QStringLiteral("controlTokenPermissionsTable"));
    m_controlTokenTable->setHorizontalHeaderLabels(
        {QStringLiteral("Cloudflare permission"), QStringLiteral("ForkMesh needs"),
         QStringLiteral("Token policy"), QStringLiteral("Live check"),
         QStringLiteral("Used for")});
    m_controlTokenTable->verticalHeader()->setVisible(false);
    m_controlTokenTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_controlTokenTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_controlTokenTable->setShowGrid(false);
    m_controlTokenTable->setWordWrap(true);
    for (int column = 0; column < 4; ++column) {
        m_controlTokenTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    }
    m_controlTokenTable->horizontalHeader()->setSectionResizeMode(
        4, QHeaderView::Stretch);


    m_controlTokenTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    col->addWidget(m_controlTokenTable);

    m_controlTokenTargets = new QLabel;
    m_controlTokenTargets->setObjectName(QStringLiteral("mutedLabel"));
    m_controlTokenTargets->setWordWrap(true);
    const QString envPath = forkmesh::control::siteDeployEnvFilePath(
        QStringLiteral(FORKMESH_SOURCE_DIR),
        QCoreApplication::applicationDirPath());
    m_controlTokenTargets->setText(
        envPath.isEmpty()
            ? QStringLiteral(
                  "A new token is written to this device's CLOUDFLARE_API_TOKEN "
                  "variable. cloudflare_worker/.env.production was not found "
                  "next to this build's Worker bundle, so no file is rewritten.")
            : QStringLiteral(
                  "A new token replaces CLOUDFLARE_API_TOKEN in this device's "
                  "variables and in %1.")
                  .arg(envPath));
    col->addWidget(m_controlTokenTargets);

    m_controlTokenOutput = new QPlainTextEdit;
    m_controlTokenOutput->setObjectName(QStringLiteral("controlTokenOutput"));
    m_controlTokenOutput->setReadOnly(true);
    m_controlTokenOutput->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_controlTokenOutput->setMinimumHeight(120);
    m_controlTokenOutput->document()->setMaximumBlockCount(500);
    m_controlTokenOutput->setPlaceholderText(
        QStringLiteral("Cloudflare API calls made by this tab appear here; "
                       "credentials are redacted."));
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_controlTokenOutput->setFont(mono);
    col->addWidget(m_controlTokenOutput);



    renderCloudflareTokenReport();
    return card;
}

QString MainWindow::resolvedCloudflareApiToken() const
{
    if (m_controlTokenEdit) {
        const QString typed = m_controlTokenEdit->text().trimmed();
        if (!typed.isEmpty())
            return typed;
    }
    if (m_cloudflareTokenEdit) {
        const QString deployToken = m_cloudflareTokenEdit->text().trimmed();
        if (!deployToken.isEmpty())
            return deployToken;
    }
    return forkmesh::control::cloudflareApiTokenFromVariables(
        ActionStore::variables());
}

void MainWindow::setCloudflareTokenBusy(bool busy)
{
    m_controlTokenBusy = busy;
    if (m_controlTokenTestButton)
        m_controlTokenTestButton->setEnabled(!busy);
    if (m_controlTokenGenerateButton)
        m_controlTokenGenerateButton->setEnabled(!busy);
}

void MainWindow::endCloudflareTokenRun()
{


    setCloudflareTokenBusy(false);
    m_controlTokenSecret.clear();
}

void MainWindow::appendCloudflareTokenOutput(const QString &text)
{
    if (!m_controlTokenOutput || text.isEmpty())
        return;
    const QString safe = forkmesh::control::redactProcessOutput(
        text, {m_controlTokenSecret, m_cloudflareActiveSecret});
    m_controlTokenOutput->moveCursor(QTextCursor::End);
    m_controlTokenOutput->insertPlainText(safe);
    m_controlTokenOutput->moveCursor(QTextCursor::End);
    m_controlTokenOutput->ensureCursorVisible();
}

void MainWindow::testCloudflareApiToken()
{
    if (m_controlTokenBusy)
        return;
    const QString token = resolvedCloudflareApiToken();
    if (token.isEmpty()) {
        if (m_controlTokenStatus)
            m_controlTokenStatus->setText(QStringLiteral(
                "No Cloudflare API token: paste one above, or save "
                "CLOUDFLARE_API_TOKEN in Settings > Secrets & Coves."));
        return;
    }
    if (!forkmesh::control::isPlausibleCloudflareApiToken(token)) {
        if (m_controlTokenStatus)
            m_controlTokenStatus->setText(QStringLiteral(
                "That value does not look like a Cloudflare API token (40 "
                "characters, no spaces). Nothing was sent."));
        return;
    }

    m_controlTokenSecret = token;
    m_controlTokenGrantedGroups.clear();
    m_controlTokenProbeResults.clear();
    m_controlTokenPolicyReadable = false;
    m_controlTokenUserResource.clear();
    m_controlTokenZoneId.clear();
    m_controlTokenAccountId =
        m_cloudflareAccountEdit ? m_cloudflareAccountEdit->text().trimmed()
                                : QString();
    if (m_controlTokenAccountId.isEmpty()) {
        m_controlTokenAccountId =
            forkmesh::control::cloudflareAccountIdFromVariables(
                ActionStore::variables());
    }
    setCloudflareTokenBusy(true);
    if (m_controlTokenOutput)
        m_controlTokenOutput->clear();
    const QString masked = forkmesh::control::maskedTokenSuffix(token);
    if (m_controlTokenStatus) {
        m_controlTokenStatus->setText(
            QStringLiteral("Checking token %1 against Cloudflare…").arg(masked));
    }
    appendCloudflareTokenOutput(
        QStringLiteral("GET /user/tokens/verify (token %1)\n").arg(masked));
    renderCloudflareTokenReport();

    cloudflareApiCall(
        token, QStringLiteral("/user/tokens/verify"),
        QByteArrayLiteral("GET"), {},
        [this, token, masked](const QJsonObject &response,
                              const QString &error) {
            if (!error.isEmpty()) {
                appendCloudflareTokenOutput(error + QLatin1Char('\n'));
                if (m_controlTokenStatus) {
                    m_controlTokenStatus->setText(
                        QStringLiteral("Token %1 was rejected by Cloudflare: %2")
                            .arg(masked, error));
                }
                endCloudflareTokenRun();
                return;
            }
            const QString status =
                forkmesh::control::cloudflareTokenVerifyStatus(response);
            appendCloudflareTokenOutput(
                QStringLiteral("Token status: %1\n")
                    .arg(status.isEmpty() ? QStringLiteral("unknown") : status));
            if (status != QLatin1String("active")) {
                if (m_controlTokenStatus) {
                    m_controlTokenStatus->setText(
                        QStringLiteral(
                            "Token %1 is %2, so no permission can be used. "
                            "Generate a replacement below or re-enable it in the "
                            "Cloudflare dashboard.")
                            .arg(masked, status.isEmpty()
                                             ? QStringLiteral("not active")
                                             : status));
                }
                endCloudflareTokenRun();
                return;
            }
            checkCloudflareTokenPolicies(
                token, forkmesh::control::cloudflareTokenVerifyId(response));
        });
}

void MainWindow::checkCloudflareTokenPolicies(const QString &token,
                                              const QString &tokenId)
{
    if (tokenId.isEmpty()) {
        appendCloudflareTokenOutput(QStringLiteral(
            "Cloudflare did not return this token's id, so its policy cannot be "
            "read; falling back to read-only probes.\n"));
        resolveCloudflareTokenTopology(token);
        return;
    }
    appendCloudflareTokenOutput(
        QStringLiteral("GET /user/tokens/%1\n").arg(tokenId));
    cloudflareApiCall(
        token, QStringLiteral("/user/tokens/") + tokenId,
        QByteArrayLiteral("GET"), {},
        [this, token](const QJsonObject &response, const QString &error) {
            if (error.isEmpty()) {
                m_controlTokenPolicyReadable = true;
                m_controlTokenGrantedGroups =
                    forkmesh::control::cloudflareTokenPermissionGroupNames(
                        response);
                m_controlTokenUserResource =
                    forkmesh::control::cloudflareTokenUserResourceKey(response);
                const QStringList accounts =
                    forkmesh::control::cloudflareTokenAccountIds(response);
                if (m_controlTokenAccountId.isEmpty() && accounts.size() == 1)
                    m_controlTokenAccountId = accounts.first();
                appendCloudflareTokenOutput(
                    QStringLiteral("Token policy grants: %1\n")
                        .arg(m_controlTokenGrantedGroups.isEmpty()
                                 ? QStringLiteral("(no permission group)")
                                 : m_controlTokenGrantedGroups.join(
                                       QStringLiteral(", "))));
            } else {
                appendCloudflareTokenOutput(
                    QStringLiteral(
                        "Token policy is not readable (%1); using read-only "
                        "probes instead.\n")
                        .arg(error));
            }
            resolveCloudflareTokenTopology(token);
        });
}

void MainWindow::resolveCloudflareTokenTopology(const QString &token)
{




    if (m_controlTokenAccountId.isEmpty()) {
        appendCloudflareTokenOutput(QStringLiteral("GET /accounts\n"));
        cloudflareApiCall(
            token, QStringLiteral("/accounts?per_page=2"),
            QByteArrayLiteral("GET"), {},
            [this, token](const QJsonObject &response, const QString &error) {
                const QJsonArray accounts =
                    response.value(QStringLiteral("result")).toArray();
                if (error.isEmpty() && accounts.size() == 1) {
                    m_controlTokenAccountId = accounts.first()
                                                  .toObject()
                                                  .value(QStringLiteral("id"))
                                                  .toString()
                                                  .trimmed();
                    appendCloudflareTokenOutput(
                        QStringLiteral("Account: %1\n")
                            .arg(m_controlTokenAccountId));
                } else if (error.isEmpty()) {
                    appendCloudflareTokenOutput(QStringLiteral(
                        "The token reaches %1 accounts; set Account ID on the "
                        "Cloudflare relay tab so checks are unambiguous.\n")
                            .arg(accounts.size()));
                } else {
                    appendCloudflareTokenOutput(
                        QStringLiteral("Accounts are not listable (%1).\n")
                            .arg(error));
                }


                if (m_controlTokenAccountId.isEmpty())
                    m_controlTokenAccountId = QStringLiteral("-");
                resolveCloudflareTokenTopology(token);
            });
        return;
    }

    if (m_controlTokenZoneId.isEmpty()) {
        QString zoneName = m_cloudflareZoneEdit
                               ? m_cloudflareZoneEdit->text().trimmed().toLower()
                               : QString();
        if (zoneName.isEmpty()) {
            zoneName = forkmesh::control::cloudflareZoneNameFromVariables(
                ActionStore::variables());
        }
        appendCloudflareTokenOutput(
            zoneName.isEmpty()
                ? QStringLiteral("GET /zones\n")
                : QStringLiteral("GET /zones?name=%1\n").arg(zoneName));
        const QString path =
            zoneName.isEmpty()
                ? QStringLiteral("/zones?per_page=2")
                : QStringLiteral("/zones?name=") +
                      QString::fromLatin1(
                          QUrl::toPercentEncoding(zoneName));
        cloudflareApiCall(
            token, path, QByteArrayLiteral("GET"), {},
            [this, token, zoneName](const QJsonObject &response,
                                    const QString &error) {
                const QJsonArray zones =
                    response.value(QStringLiteral("result")).toArray();
                if (error.isEmpty() && !zoneName.isEmpty()) {
                    m_controlTokenZoneId =
                        forkmesh::control::cloudflareZoneId(zones, zoneName);
                } else if (error.isEmpty() && zones.size() == 1) {
                    m_controlTokenZoneId = zones.first()
                                               .toObject()
                                               .value(QStringLiteral("id"))
                                               .toString()
                                               .trimmed();
                }
                if (!m_controlTokenZoneId.isEmpty()) {
                    appendCloudflareTokenOutput(
                        QStringLiteral("Zone: %1\n").arg(m_controlTokenZoneId));
                } else if (error.isEmpty()) {
                    appendCloudflareTokenOutput(QStringLiteral(
                        "No single zone matched; set Cloudflare zone on the "
                        "relay tab to check the DNS permission.\n"));
                } else {
                    appendCloudflareTokenOutput(
                        QStringLiteral("Zones are not listable (%1).\n")
                            .arg(error));
                }

                if (m_controlTokenZoneId.isEmpty())
                    m_controlTokenZoneId = QStringLiteral("-");
                resolveCloudflareTokenTopology(token);
            });
        return;
    }

    runCloudflareTokenProbe(token, 0);
}

void MainWindow::runCloudflareTokenProbe(const QString &token, int index)
{
    const QList<forkmesh::control::CloudflareTokenRequirement> requirements =
        forkmesh::control::cloudflareTokenRequirements();
    if (index >= requirements.size()) {
        endCloudflareTokenRun();
        renderCloudflareTokenReport();
        return;
    }
    const forkmesh::control::CloudflareTokenRequirement requirement =
        requirements.at(index);
    const QString path = forkmesh::control::cloudflareTokenProbePath(
        requirement, m_controlTokenAccountId, m_controlTokenZoneId);
    if (path.isEmpty()) {
        m_controlTokenProbeResults.insert(
            requirement.key,
            requirement.probePath.isEmpty()
                ? QStringLiteral("no read-only probe")
                : QStringLiteral("skipped: id unknown"));
        runCloudflareTokenProbe(token, index + 1);
        return;
    }
    appendCloudflareTokenOutput(QStringLiteral("GET %1\n").arg(path));
    cloudflareApiCall(
        token, path, QByteArrayLiteral("GET"), {},
        [this, token, index, requirement](const QJsonObject &,
                                          const QString &error) {
            m_controlTokenProbeResults.insert(
                requirement.key,
                error.isEmpty()
                    ? QStringLiteral("read access confirmed")
                    : QStringLiteral("denied"));
            if (!error.isEmpty())
                appendCloudflareTokenOutput(error + QLatin1Char('\n'));
            runCloudflareTokenProbe(token, index + 1);
        });
}

void MainWindow::renderCloudflareTokenReport()
{
    if (!m_controlTokenTable)
        return;
    const QList<forkmesh::control::CloudflareTokenRequirement> requirements =
        forkmesh::control::cloudflareTokenRequirements();
    m_controlTokenTable->setRowCount(requirements.size());
    QStringList missing;
    QStringList denied;
    int requiredCount = 0;
    int grantedCount = 0;
    for (int row = 0; row < requirements.size(); ++row) {
        const forkmesh::control::CloudflareTokenRequirement requirement =
            requirements.at(row);
        const bool granted = forkmesh::control::cloudflareTokenGrantsRequirement(
            requirement, m_controlTokenGrantedGroups);
        const QString probe = m_controlTokenProbeResults.value(requirement.key);
        if (requirement.required) {
            ++requiredCount;
            if (m_controlTokenPolicyReadable && granted)
                ++grantedCount;
            if (m_controlTokenPolicyReadable && !granted)
                missing.append(requirement.label);
            if (probe == QLatin1String("denied"))
                denied.append(requirement.label);
        }
        const auto cell = [](const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return item;
        };
        m_controlTokenTable->setItem(row, 0, cell(requirement.label));
        m_controlTokenTable->setItem(
            row, 1,
            cell(requirement.required ? QStringLiteral("Required")
                                      : QStringLiteral("Optional")));
        m_controlTokenTable->setItem(
            row, 2,
            cell(!m_controlTokenPolicyReadable
                     ? QStringLiteral("not readable")
                     : granted ? QStringLiteral("granted")
                               : QStringLiteral("missing")));
        m_controlTokenTable->setItem(
            row, 3, cell(probe.isEmpty() ? QStringLiteral("not checked") : probe));
        m_controlTokenTable->setItem(row, 4, cell(requirement.purpose));
    }
    m_controlTokenTable->resizeRowsToContents();
    int tableHeight = m_controlTokenTable->horizontalHeader()->height() + 6;
    for (int row = 0; row < m_controlTokenTable->rowCount(); ++row)
        tableHeight += m_controlTokenTable->rowHeight(row);
    m_controlTokenTable->setMinimumHeight(tableHeight);

    if (!m_controlTokenStatus || m_controlTokenBusy)
        return;
    if (m_controlTokenSecret.isEmpty() && m_controlTokenProbeResults.isEmpty() &&
        !m_controlTokenPolicyReadable) {
        return;
    }
    QStringList lines;
    if (m_controlTokenPolicyReadable) {
        lines << QStringLiteral("Token policy: %1 of %2 required permissions "
                                "granted.")
                     .arg(grantedCount)
                     .arg(requiredCount);
        if (!missing.isEmpty()) {
            lines << QStringLiteral("Missing: %1.")
                         .arg(missing.join(QStringLiteral(", ")));
        }
    } else {
        lines << QStringLiteral(
            "The token cannot read its own policy (that needs API Tokens: "
            "Read), so the live checks below are what ForkMesh can confirm.");
    }
    if (!denied.isEmpty()) {
        lines << QStringLiteral("Live checks denied: %1.")
                     .arg(denied.join(QStringLiteral(", ")));
    } else {
        lines << QStringLiteral(
            "Every probed capability answered; edit rights cannot be probed "
            "without writing, so a deploy is still the final proof.");
    }
    m_controlTokenStatus->setText(lines.join(QLatin1Char(' ')));
}

void MainWindow::generateCloudflareApiToken()
{
    if (m_controlTokenBusy)
        return;
    const QString token = resolvedCloudflareApiToken();
    if (token.isEmpty() ||
        !forkmesh::control::isPlausibleCloudflareApiToken(token)) {
        if (m_controlTokenStatus)
            m_controlTokenStatus->setText(QStringLiteral(
                "Minting a token needs a current Cloudflare token with API "
                "Tokens: Edit. Paste one above first."));
        return;
    }
    const QString account = m_controlTokenAccountId;
    const QString zone = m_controlTokenZoneId;
    static const QRegularExpression idPattern(
        QStringLiteral("^[A-Za-z0-9]{1,64}$"));
    if (!idPattern.match(account).hasMatch() ||
        !idPattern.match(zone).hasMatch()) {
        if (m_controlTokenStatus)
            m_controlTokenStatus->setText(QStringLiteral(
                "Run \"Check token permissions\" first (and set Account ID / "
                "Cloudflare zone on the relay tab if they stay unknown): a new "
                "token has to be scoped to one account and one zone."));
        return;
    }
    const QString envPath = forkmesh::control::siteDeployEnvFilePath(
        QStringLiteral(FORKMESH_SOURCE_DIR),
        QCoreApplication::applicationDirPath());
    if (QMessageBox::question(
            this, QStringLiteral("Generate a Cloudflare API token"),
            QStringLiteral(
                "Create a new Cloudflare API token on account %1, scoped to the "
                "permissions this page lists, and store it as this device's "
                "CLOUDFLARE_API_TOKEN variable%2?\n\nThe token you are using now "
                "stays valid until you delete it in the Cloudflare dashboard.")
                .arg(account,
                     envPath.isEmpty()
                         ? QString()
                         : QStringLiteral(" and in %1").arg(envPath)),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }

    m_controlTokenSecret = token;
    setCloudflareTokenBusy(true);
    appendCloudflareTokenOutput(
        QStringLiteral("GET /user/tokens/permission_groups\n"));


    cloudflareApiCall(
        token, QStringLiteral("/user/tokens/permission_groups"),
        QByteArrayLiteral("GET"), {},
        [this, token, account, zone](const QJsonObject &response,
                                     const QString &error) {
            if (!error.isEmpty()) {
                appendCloudflareTokenOutput(error + QLatin1Char('\n'));
                if (m_controlTokenStatus) {
                    m_controlTokenStatus->setText(QStringLiteral(
                        "Cloudflare would not list its permission groups (%1). "
                        "The current token needs API Tokens: Read and API "
                        "Tokens: Edit to mint a replacement.").arg(error));
                }
                endCloudflareTokenRun();
                return;
            }


            static const QRegularExpression unsafeName(
                QStringLiteral("[^A-Za-z0-9._-]"));
            QString node = machineNodeName();
            node.remove(unsafeName);
            if (node.isEmpty())
                node = QStringLiteral("node");
            const QString name =
                QStringLiteral("ForkMesh %1 %2")
                    .arg(node.left(48),
                         QDateTime::currentDateTimeUtc().toString(
                             QStringLiteral("yyyyMMdd-HHmmss")));
            QString payloadError;
            const QJsonObject payload =
                forkmesh::control::cloudflareTokenCreatePayload(
                    name, account, zone, m_controlTokenUserResource,
                    response.value(QStringLiteral("result")).toArray(),
                    &payloadError);
            if (payload.isEmpty()) {
                appendCloudflareTokenOutput(payloadError + QLatin1Char('\n'));
                if (m_controlTokenStatus)
                    m_controlTokenStatus->setText(payloadError);
                endCloudflareTokenRun();
                return;
            }
            appendCloudflareTokenOutput(
                QStringLiteral("POST /user/tokens (%1)\n").arg(name));
            cloudflareApiCall(
                token, QStringLiteral("/user/tokens"),
                QByteArrayLiteral("POST"), payload,
                [this](const QJsonObject &created, const QString &createError) {
                    if (!createError.isEmpty()) {
                        appendCloudflareTokenOutput(createError +
                                                    QLatin1Char('\n'));
                        if (m_controlTokenStatus) {
                            m_controlTokenStatus->setText(
                                QStringLiteral("Cloudflare refused to create the "
                                               "token: %1")
                                    .arg(createError));
                        }
                        endCloudflareTokenRun();
                        return;
                    }
                    const QString value =
                        forkmesh::control::cloudflareCreatedTokenValue(created);
                    if (value.isEmpty()) {
                        appendCloudflareTokenOutput(QStringLiteral(
                            "Cloudflare created a token but returned no usable "
                            "value; nothing was stored.\n"));
                        if (m_controlTokenStatus) {
                            m_controlTokenStatus->setText(QStringLiteral(
                                "Cloudflare created a token but returned no "
                                "usable value; nothing was stored."));
                        }
                        endCloudflareTokenRun();
                        return;
                    }
                    adoptRotatedCloudflareToken(value);
                });
        });
}

QStringList MainWindow::rememberVultrApiKey(const QString &apiKey,
                                            QString *error)
{
    if (error)
        error->clear();
    const QString key = apiKey.trimmed();


    if (key.isEmpty() || key == m_vultrRememberedKey ||
        key.contains(QLatin1Char('\n')) || key.contains(QLatin1Char('\r'))) {
        return {};
    }

    const QStringList names = forkmesh::control::vultrApiKeyVariableNames();
    const QString canonical = names.constFirst();
    QMap<QString, QString> variables = ActionStore::variables();
    bool changed = false;
    bool haveCanonical = false;
    for (auto it = variables.begin(); it != variables.end(); ++it) {
        const QString name = it.key().trimmed();
        if (!names.contains(name, Qt::CaseInsensitive))
            continue;




        if (name.compare(canonical, Qt::CaseInsensitive) == 0)
            haveCanonical = true;
        if (it.value().trimmed() != key) {
            it.value() = key;
            changed = true;
        }
    }
    if (!haveCanonical) {
        variables.insert(canonical, key);
        changed = true;
    }
    QStringList applied;
    if (changed) {
        ActionStore::setVariables(variables);
        reloadVariablesTable();
        applied << QStringLiteral("this device's %1 variable").arg(canonical);
    }

    const QString envPath = forkmesh::control::siteDeployEnvFilePath(
        QStringLiteral(FORKMESH_SOURCE_DIR),
        QCoreApplication::applicationDirPath());
    QString envError;
    bool retryable = false;
    bool envChanged = false;
    if (envPath.isEmpty()) {


        envError = QStringLiteral(
            "cloudflare_worker/.env.production was not found next to this "
            "build's Worker bundle, so no file was rewritten.");
    } else if (writeEnvAssignments(envPath, {{canonical, key}}, &envError,
                                   &envChanged)) {
        if (envChanged)
            applied << envPath;
    } else {
        retryable = true;
    }
    if (error)
        *error = envError;
    if (!retryable)
        m_vultrRememberedKey = key;
    return applied;
}

void MainWindow::adoptRotatedCloudflareToken(const QString &token)
{


    m_controlTokenSecret = token;
    const QString masked = forkmesh::control::maskedTokenSuffix(token);

    QMap<QString, QString> variables = ActionStore::variables();
    variables.insert(QStringLiteral("CLOUDFLARE_API_TOKEN"), token);
    if (!m_controlTokenAccountId.isEmpty() &&
        forkmesh::control::cloudflareAccountIdFromVariables(variables)
            .isEmpty()) {
        variables.insert(QStringLiteral("CLOUDFLARE_ACCOUNT_ID"),
                         m_controlTokenAccountId);
    }
    ActionStore::setVariables(variables);
    QStringList applied;
    applied << QStringLiteral("this device's CLOUDFLARE_API_TOKEN variable");

    const QString envPath = forkmesh::control::siteDeployEnvFilePath(
        QStringLiteral(FORKMESH_SOURCE_DIR),
        QCoreApplication::applicationDirPath());
    QString envError;
    if (envPath.isEmpty()) {
        envError = QStringLiteral(
            "cloudflare_worker/.env.production was not found next to this "
            "build's Worker bundle, so no file was rewritten.");
    } else {
        QMap<QString, QString> assignments;
        assignments.insert(QStringLiteral("CLOUDFLARE_API_TOKEN"), token);
        if (!m_controlTokenAccountId.isEmpty()) {
            assignments.insert(QStringLiteral("CLOUDFLARE_ACCOUNT_ID"),
                               m_controlTokenAccountId);
        }
        if (writeEnvAssignments(envPath, assignments, &envError))
            applied << envPath;
    }




    if (m_controlTokenEdit)
        m_controlTokenEdit->setText(token);
    if (m_cloudflareTokenEdit)
        m_cloudflareTokenEdit->setText(token);

    appendCloudflareTokenOutput(
        QStringLiteral("Created token %1; stored in %2.\n")
            .arg(masked, applied.join(QStringLiteral(" and "))));
    if (!envError.isEmpty())
        appendCloudflareTokenOutput(envError + QLatin1Char('\n'));
    logSystem(QStringLiteral(
                  "Control node: created a scoped Cloudflare API token (%1) and "
                  "replaced the stored deployment credential.")
                  .arg(masked));
    flashMessage(
        QStringLiteral("New Cloudflare API token %1 stored.").arg(masked),
        false);
    if (m_controlTokenStatus) {
        m_controlTokenStatus->setText(
            QStringLiteral("Created token %1 and replaced %2. %3Re-checking it "
                           "now…")
                .arg(masked, applied.join(QStringLiteral(" and ")),
                     envError.isEmpty() ? QString()
                                        : envError + QLatin1Char(' ')));
    }


    endCloudflareTokenRun();
    QTimer::singleShot(0, this, &MainWindow::testCloudflareApiToken);
}

QStringList MainWindow::rememberCloudflareApiToken(const QString &token,
                                                    QString *error)
{
    if (error)
        error->clear();
    const QString key = token.trimmed();
    if (key.isEmpty() || key.contains(QLatin1Char('\n')) ||
        key.contains(QLatin1Char('\r')))
        return {};

    QMap<QString, QString> variables = ActionStore::variables();
    QStringList applied;
    if (variables.value(QStringLiteral("CLOUDFLARE_API_TOKEN")) != key) {
        variables.insert(QStringLiteral("CLOUDFLARE_API_TOKEN"), key);
        ActionStore::setVariables(variables);
        reloadVariablesTable();
        applied << QStringLiteral(
            "this device's CLOUDFLARE_API_TOKEN variable");
    }

    const QString envPath = forkmesh::control::siteDeployEnvFilePath(
        QStringLiteral(FORKMESH_SOURCE_DIR),
        QCoreApplication::applicationDirPath());
    QString envError;
    bool envChanged = false;
    if (envPath.isEmpty()) {
        envError = QStringLiteral(
            "cloudflare_worker/.env.production was not found next to this "
            "build's Worker bundle, so no file was rewritten.");
    } else if (writeEnvAssignments(
                   envPath,
                   {{QStringLiteral("CLOUDFLARE_API_TOKEN"), key}},
                   &envError, &envChanged) &&
               envChanged) {
        applied << envPath;
    }
    if (error)
        *error = envError;
    return applied;
}

void MainWindow::connectToDeployedRelay(const QString &hostname)
{
    const QString host = hostname.trimmed().toLower();
    if (host.isEmpty())
        return;
    int index = -1;
    for (int i = 0; i < m_servers.size(); ++i) {
        if (serverHost(m_servers.at(i).url) == host) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        ServerConfig server;
        server.url = canonicalServerUrl(host);
        server.room = kDefaultRoomName;
        m_servers.append(server);
        index = m_servers.size() - 1;
        saveServers();
        fetchFavicon(index);
    }
    appendControlNodeOutput(
        QStringLiteral("Added %1 to this desktop's relay list.\n").arg(host));
    refreshRelaysTable();
    switchToServer(index);
}

void MainWindow::deploySavedHostsFromControl()
{
    const QJsonArray hosts =
        QJsonDocument::fromJson(
            QSettings().value(kHostsSetting).toString().toUtf8())
            .array();
    if (hosts.isEmpty()) {
        flashMessage(
            QStringLiteral(
                "Add a host in Network > Hosts before starting a fleet deployment."),
            true);
        showSection(7);
        return;
    }
    if (QMessageBox::question(
            this, QStringLiteral("Deploy to saved hosts"),
            QStringLiteral(
                "Upload and install this ForkMesh binary on %1 saved host(s)? "
                "Each host keeps its existing identity and mirrored data.")
                .arg(hosts.size()),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    showSection(7);
    QTimer::singleShot(0, this,
                       &MainWindow::runHostInstallAllFromBinary);
}
