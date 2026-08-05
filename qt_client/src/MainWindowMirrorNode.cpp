#include "ForkMeshVersion.h"
#include "ControlNode.h"
#include "MainWindow.h"
#include "MainWindowInternal.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>

using namespace forkmesh::ui;

namespace {

QString managedMirrorNodeRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/mirror-node");
}

QString managedMirrorNodeConfigPath()
{
    return QDir(managedMirrorNodeRoot()).filePath(QStringLiteral("config.json"));
}

QString mirrorGatewayRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/mirror-gateway");
}

QString managedMirrorNodeBinary()
{
    QString binary =
        QStandardPaths::findExecutable(QStringLiteral("forkmesh-mirror-node"));
    if (!binary.isEmpty())
        return binary;
    const QString sibling = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("forkmesh-mirror-node"));
    return QFileInfo(sibling).isExecutable() ? sibling : QString();
}

bool writeOwnerJson(const QString &path, const QJsonObject &object, QString *error)
{
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory) || QFileInfo(directory).isSymLink()) {
        if (error)
            *error = QStringLiteral("Could not create the mirror-node settings directory.");
        return false;
    }
    QFile(directory).setPermissions(QFileDevice::ReadOwner |
                                    QFileDevice::WriteOwner |
                                    QFileDevice::ExeOwner);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not open the mirror-node configuration.");
        return false;
    }
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit()) {
        if (error)
            *error = QStringLiteral("Could not save the mirror-node configuration.");
        return false;
    }
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner |
                                        QFileDevice::WriteOwner)) {
        if (error)
            *error = QStringLiteral("Could not make the mirror-node configuration owner-only.");
        return false;
    }
    return true;
}

QString compactDuration(qint64 seconds)
{
    if (seconds >= 86400)
        return QStringLiteral("%1d %2h").arg(seconds / 86400).arg((seconds / 3600) % 24);
    if (seconds >= 3600)
        return QStringLiteral("%1h %2m").arg(seconds / 3600).arg((seconds / 60) % 60);
    return QStringLiteral("%1m %2s").arg(seconds / 60).arg(seconds % 60);
}

} // namespace

bool MainWindow::rebuildManagedMirrorNodeConfiguration(QString *error)
{
    if (!rebuildDirectMirrorGatewayConfiguration(error, false))
        return false;
    const QString binary = managedMirrorNodeBinary();
    if (binary.isEmpty()) {
        if (error)
            *error = QStringLiteral(
                "forkmesh-mirror-node is not installed. Re-run the ForkMesh installer.");
        return false;
    }
    QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty())
        python = QStandardPaths::findExecutable(QStringLiteral("python"));
    const QString gatewayScript = forkmesh::control::findMirrorGatewayScript(
        QStringLiteral(FORKMESH_SOURCE_DIR),
        QCoreApplication::applicationDirPath());
    QString cloudflared =
        QStandardPaths::findExecutable(QStringLiteral("cloudflared"));
    const QString managedCloudflared =
        QDir(mirrorGatewayRoot()).filePath(QStringLiteral("bin/cloudflared"));
    if (cloudflared.isEmpty() && QFileInfo(managedCloudflared).isExecutable())
        cloudflared = managedCloudflared;
    const QString token =
        QDir(mirrorGatewayRoot()).filePath(QStringLiteral("connector.token"));
    const QString identity =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/identity/ed25519.pem");
    if (python.isEmpty() || gatewayScript.isEmpty() || cloudflared.isEmpty() ||
        !QFileInfo(token).isFile() || !QFileInfo(identity).isFile()) {
        if (error)
            *error = QStringLiteral(
                "The gateway runtime, Tunnel connector, token, or identity is not ready.");
        return false;
    }

    QSettings settings;
    const QString node =
        settings.value(QStringLiteral("control/cloudflareNodeName"))
            .toString().trimmed().toLower();
    QJsonObject upstreams;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly || repo.isPrivate || !repo.publishToNetwork ||
            repo.mirrorPath.trimmed().isEmpty())
            continue;
        QString upstream = repo.cloneUrl.trimmed();
        if (upstream.isEmpty()) {
            QUrl clone = catalogApiUrl();
            clone.setPath(QStringLiteral("/%1/%2")
                              .arg(repoSegment(repo.owner, QStringLiteral("owner")),
                                   repoSegment(repo.name, QStringLiteral("repository"))));
            upstream = clone.toString();
        }
        const QJsonArray values{upstream};
        const QString name = repoSegment(repo.name, QStringLiteral("repository"));
        upstreams.insert(repoSegment(repo.owner, QStringLiteral("owner")) +
                             QLatin1Char('/') + name,
                         values);
        upstreams.insert(repoSegment(catalogOwner(repo), QStringLiteral("owner")) +
                             QLatin1Char('/') + name,
                         values);
    }
    const int syncSeconds = qBound(
        5, settings.value(QStringLiteral("control/mirrorNodeSyncSeconds"), 30).toInt(),
        86400);
    const QJsonObject config{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("listen"), QStringLiteral("127.0.0.1:8791")},
        {QStringLiteral("gatewayProbe"), QStringLiteral("127.0.0.1:8790")},
        {QStringLiteral("gatewayConfig"),
         QDir(mirrorGatewayRoot()).filePath(QStringLiteral("config.json"))},
        {QStringLiteral("gatewayScript"), gatewayScript},
        {QStringLiteral("python"), python},
        {QStringLiteral("cloudflared"), cloudflared},
        {QStringLiteral("connectorToken"), token},
        {QStringLiteral("identityKey"), identity},
        {QStringLiteral("catalogUrl"), catalogApiUrl().toString()},
        {QStringLiteral("publishOwner"), node},
        {QStringLiteral("version"), QStringLiteral(FORKMESH_VERSION)},
        {QStringLiteral("syncInterval"), QString::number(syncSeconds) + QStringLiteral("s")},
        {QStringLiteral("syncTimeout"), QStringLiteral("5m")},
        {QStringLiteral("upstreams"), upstreams},
    };
    return writeOwnerJson(managedMirrorNodeConfigPath(), config, error);
}

bool MainWindow::startManagedMirrorNodeServer()
{
    m_mirrorNodeStopRequested = false;
    if (m_mirrorNodeProcess &&
        m_mirrorNodeProcess->state() != QProcess::NotRunning)
        return true;
    // Older desktop releases launched the Python gateway and Tunnel connector
    // directly. Hand those loopback ports over before the Go supervisor starts;
    // its child supervision is now the single owner of both processes.
    if ((m_mirrorGatewayProcess &&
         m_mirrorGatewayProcess->state() != QProcess::NotRunning) ||
        (m_cloudflaredProcess &&
         m_cloudflaredProcess->state() != QProcess::NotRunning)) {
        stopDirectMirrorServices();
        QTimer::singleShot(900, this, [this] {
            startManagedMirrorNodeServer();
        });
        return true;
    }
    QString error;
    if (!rebuildManagedMirrorNodeConfiguration(&error)) {
        flashMessage(error, true);
        return false;
    }
    const QString binary = managedMirrorNodeBinary();
    auto *process = new QProcess(this);
    m_mirrorNodeProcess = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        appendControlNodeOutput(
            QStringLiteral("mirror-node: ") +
            QString::fromUtf8(process->readAllStandardOutput()));
    });
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus) {
                if (m_mirrorNodeProcess == process)
                    m_mirrorNodeProcess = nullptr;
                appendControlNodeOutput(
                    QStringLiteral("Go mirror-node stopped (exit %1).\n").arg(exitCode));
                process->deleteLater();
                refreshMirrorNodeCompanion();
                if (!m_mirrorNodeStopRequested && !m_nodeOffline &&
                    QSettings().value(
                                   QStringLiteral("control/autoStartMirrorServices"),
                                   true)
                        .toBool()) {
                    QTimer::singleShot(5000, this, [this] {
                        if (!m_mirrorNodeStopRequested && !m_nodeOffline)
                            startManagedMirrorNodeServer();
                    });
                }
            });
    process->start(binary,
                   {QStringLiteral("--config"), managedMirrorNodeConfigPath()});
    if (!process->waitForStarted(3000)) {
        const QString message = process->errorString();
        process->deleteLater();
        m_mirrorNodeProcess = nullptr;
        flashMessage(QStringLiteral("Could not start mirror-node: %1").arg(message),
                     true);
        return false;
    }
    appendControlNodeOutput(QStringLiteral("Go mirror-node supervisor started.\n"));
    QTimer::singleShot(750, this, &MainWindow::refreshMirrorNodeCompanion);
    return true;
}

void MainWindow::stopManagedMirrorNodeServer()
{
    m_mirrorNodeStopRequested = true;
    if (!m_mirrorNodeProcess ||
        m_mirrorNodeProcess->state() == QProcess::NotRunning)
        return;
    m_mirrorNodeProcess->terminate();
    QPointer<QProcess> process(m_mirrorNodeProcess);
    QTimer::singleShot(5000, this, [process] {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void MainWindow::requestManagedMirrorNodeSync()
{
    if (!m_networkAccess)
        return;
    QNetworkRequest request(
        QUrl(QStringLiteral("http://127.0.0.1:8791/v1/control/sync")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(request, QByteArrayLiteral("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int status = reply->attribute(
                                    QNetworkRequest::HttpStatusCodeAttribute)
                               .toInt();
        reply->deleteLater();
        flashMessage(status == 202 ? QStringLiteral("Mirror-node sync requested.")
                                   : QStringLiteral("Mirror-node is not reachable."),
                     status != 202);
        QTimer::singleShot(500, this, &MainWindow::refreshMirrorNodeCompanion);
    });
}

void MainWindow::showMirrorNodeCompanion()
{
    if (!m_mirrorNodeCompanion) {
        auto *dialog = new QDialog(this, Qt::Window | Qt::Tool);
        m_mirrorNodeCompanion = dialog;
        dialog->setWindowTitle(QStringLiteral("ForkMesh mirror node"));
        dialog->setWindowIcon(windowIcon());
        dialog->resize(440, 430);
        dialog->setMinimumSize(390, 360);
        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);

        auto *headingRow = new QHBoxLayout;
        m_mirrorNodeCompanionStatus = new QLabel;
        m_mirrorNodeCompanionStatus->setObjectName(QStringLiteral("sectionLabel"));
        headingRow->addWidget(m_mirrorNodeCompanionStatus, 1);
        auto *start = new QPushButton(QStringLiteral("Start"), dialog);
        start->setObjectName(QStringLiteral("primaryButton"));
        setOcticon(start, "rocket", 13);
        connect(start, &QPushButton::clicked, this, [this] {
            startManagedMirrorNodeServer();
        });
        auto *stop = new QPushButton(QStringLiteral("Stop"), dialog);
        stop->setObjectName(QStringLiteral("ghostButton"));
        setOcticon(stop, "stop", 13);
        connect(stop, &QPushButton::clicked, this,
                &MainWindow::stopManagedMirrorNodeServer);
        auto *sync = new QPushButton(QStringLiteral("Sync"), dialog);
        sync->setObjectName(QStringLiteral("ghostButton"));
        setOcticon(sync, "sync", 13);
        connect(sync, &QPushButton::clicked, this,
                &MainWindow::requestManagedMirrorNodeSync);
        headingRow->addWidget(start);
        headingRow->addWidget(stop);
        headingRow->addWidget(sync);
        layout->addLayout(headingRow);

        auto *tabs = new QTabWidget(dialog);
        auto *statsPage = new QWidget(tabs);
        auto *statsLayout = new QVBoxLayout(statsPage);
        m_mirrorNodeCompanionStats = new QLabel(statsPage);
        m_mirrorNodeCompanionStats->setWordWrap(true);
        m_mirrorNodeCompanionStats->setTextFormat(Qt::RichText);
        m_mirrorNodeCompanionStats->setAlignment(Qt::AlignTop);
        statsLayout->addWidget(m_mirrorNodeCompanionStats, 1);
        tabs->addTab(statsPage, themedOcticon("graph", QColor("#8b949e"), 14),
                     QStringLiteral("Stats"));

        auto *accountPage = new QWidget(tabs);
        auto *accountLayout = new QVBoxLayout(accountPage);
        accountLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        m_mirrorNodeCompanionAccount = new QLabel(accountPage);
        m_mirrorNodeCompanionAccount->setWordWrap(true);
        m_mirrorNodeCompanionAccount->setAlignment(Qt::AlignCenter);
        m_mirrorNodeCompanionBalance = new QLabel(accountPage);
        m_mirrorNodeCompanionBalance->setObjectName(QStringLiteral("channelTitle"));
        m_mirrorNodeCompanionBalance->setAlignment(Qt::AlignCenter);
        m_mirrorNodeCompanionQr = new QLabel(accountPage);
        m_mirrorNodeCompanionQr->setAlignment(Qt::AlignCenter);
        auto *connectAccount = new QPushButton(QStringLiteral("Connect account"), accountPage);
        connectAccount->setObjectName(QStringLiteral("mirrorNodeConnectAccount"));
        connectAccount->setProperty("buttonStyle", QStringLiteral("primary"));
        connectAccount->setStyleSheet(QStringLiteral("font-weight:600;"));
        setOcticon(connectAccount, "sign-in", 14);
        connect(connectAccount, &QPushButton::clicked, this,
                &MainWindow::openLinkNodeInBrowser);
        auto *rewards = new QPushButton(QStringLiteral("Reward settings"), accountPage);
        rewards->setObjectName(QStringLiteral("mirrorNodeRewardSettings"));
        setOcticon(rewards, "credit-card", 14);
        connect(rewards, &QPushButton::clicked, this, [this] {
            promptSetSolanaAddress();
            refreshMirrorNodeCompanion();
        });
        m_mirrorNodeCompanionLogout =
            new QPushButton(QStringLiteral("Log out"), accountPage);
        m_mirrorNodeCompanionLogout->setObjectName(QStringLiteral("ghostButton"));
        setOcticon(m_mirrorNodeCompanionLogout, "sign-out", 14);
        connect(m_mirrorNodeCompanionLogout, &QPushButton::clicked, this, [this] {
            logout();
            refreshMirrorNodeCompanion();
        });
        accountLayout->addWidget(m_mirrorNodeCompanionAccount);
        accountLayout->addWidget(m_mirrorNodeCompanionBalance);
        accountLayout->addWidget(m_mirrorNodeCompanionQr);
        accountLayout->addWidget(connectAccount, 0, Qt::AlignHCenter);
        accountLayout->addWidget(rewards, 0, Qt::AlignHCenter);
        accountLayout->addWidget(m_mirrorNodeCompanionLogout, 0, Qt::AlignHCenter);
        tabs->addTab(accountPage, themedOcticon("person", QColor("#8b949e"), 14),
                     QStringLiteral("Account"));

        auto *infoPage = new QWidget(tabs);
        auto *infoLayout = new QVBoxLayout(infoPage);
        auto *info = new QLabel(
            QStringLiteral("The Go supervisor owns mirror sync, the local HTTPS "
                           "gateway, and the Tunnel connector. The Qt client only "
                           "manages it through its loopback control API."),
            infoPage);
        info->setWordWrap(true);
        infoLayout->addWidget(info);
        auto *autoStart = new QCheckBox(QStringLiteral("Start mirror node automatically"),
                                        infoPage);
        autoStart->setChecked(QSettings().value(
            QStringLiteral("control/autoStartMirrorServices"), true).toBool());
        connect(autoStart, &QCheckBox::toggled, this, [](bool checked) {
            QSettings().setValue(QStringLiteral("control/autoStartMirrorServices"),
                                 checked);
        });
        infoLayout->addWidget(autoStart);
        auto *intervalRow = new QHBoxLayout;
        intervalRow->addWidget(new QLabel(QStringLiteral("Sync interval"), infoPage));
        auto *interval = new QSpinBox(infoPage);
        interval->setRange(5, 86400);
        interval->setSuffix(QStringLiteral(" seconds"));
        interval->setValue(QSettings().value(
            QStringLiteral("control/mirrorNodeSyncSeconds"), 30).toInt());
        connect(interval, qOverload<int>(&QSpinBox::valueChanged), this,
                [](int value) {
                    QSettings().setValue(
                        QStringLiteral("control/mirrorNodeSyncSeconds"), value);
                });
        intervalRow->addWidget(interval, 1);
        infoLayout->addLayout(intervalRow);
        auto *path = new QLabel(managedMirrorNodeConfigPath(), infoPage);
        path->setObjectName(QStringLiteral("mutedLabel"));
        path->setWordWrap(true);
        infoLayout->addWidget(path);
        infoLayout->addStretch();
        tabs->addTab(infoPage, themedOcticon("gear", QColor("#8b949e"), 14),
                     QStringLiteral("Info & settings"));
        layout->addWidget(tabs, 1);

        m_mirrorNodeCompanionTimer = new QTimer(dialog);
        m_mirrorNodeCompanionTimer->setInterval(2000);
        connect(m_mirrorNodeCompanionTimer, &QTimer::timeout, this,
                &MainWindow::refreshMirrorNodeCompanion);
        m_mirrorNodeCompanionTimer->start();
    }
    m_mirrorNodeCompanion->show();
    m_mirrorNodeCompanion->raise();
    m_mirrorNodeCompanion->activateWindow();
    refreshMirrorNodeCompanion();
}

void MainWindow::refreshMirrorNodeCompanion()
{
    if (!m_mirrorNodeCompanion)
        return;
    const QString account = accountOwner();
    const QString solana = savedSolanaAddress();
    const bool enrolled = hasOwnerSigningCapability(account) && !solana.isEmpty();
    m_mirrorNodeCompanionAccount->setText(
        enrolled ? QStringLiteral("Connected as <b>%1</b><br>Reward payout is configured.")
                       .arg(account.toHtmlEscaped())
                 : QStringLiteral("Connect this node to your ForkMesh account, then "
                                  "add a public Solana payout address to join the node "
                                  "reward program."));
    m_mirrorNodeCompanionAccount->setTextFormat(Qt::RichText);
    m_mirrorNodeCompanionLogout->setVisible(!m_accountName.trimmed().isEmpty());
    if (auto *connectAccount = m_mirrorNodeCompanion->findChild<QPushButton *>(
            QStringLiteral("mirrorNodeConnectAccount")))
        connectAccount->setVisible(!enrolled);
    if (auto *rewardSettings = m_mirrorNodeCompanion->findChild<QPushButton *>(
            QStringLiteral("mirrorNodeRewardSettings")))
        rewardSettings->setVisible(!enrolled);
    m_mirrorNodeCompanionBalance->setVisible(enrolled);
    m_mirrorNodeCompanionBalance->setText(
        enrolled && m_navSolanaLamports >= 0
            ? QStringLiteral("%1 SOL")
                  .arg(QString::number(double(m_navSolanaLamports) / 1000000000.0,
                                       'f', 4))
            : (enrolled ? QStringLiteral("SOL balance —") : QString()));
    QUrl enrollUrl;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 qrGeneratedAt =
        m_mirrorNodeCompanionQr->property("generatedAtMs").toLongLong();
    if (!enrolled && nowMs - qrGeneratedAt < 60000) {
        enrollUrl = QUrl(m_mirrorNodeCompanionQr->property("enrollmentUrl")
                             .toString());
    } else if (!enrolled && hasOwnerSigningCapability(account) &&
               m_profileIdentity.isValid()) {
        enrollUrl = catalogApiUrl();
        enrollUrl.setPath(QStringLiteral("/dashboard"));
        QUrlQuery query;
        const QString timestamp =
            QString::number(nowMs);
        const QByteArray canonical =
            ("forkmesh-link-grant-v1\n" + account + "\n" + timestamp).toUtf8();
        query.addQueryItem(QStringLiteral("link_node"), account);
        query.addQueryItem(QStringLiteral("link_ts"), timestamp);
        query.addQueryItem(QStringLiteral("link_sig"),
                           m_profileIdentity.signData(canonical));
        query.addQueryItem(QStringLiteral("reward_settings"), QStringLiteral("1"));
        enrollUrl.setQuery(query);
        m_mirrorNodeCompanionQr->setProperty("generatedAtMs", nowMs);
        m_mirrorNodeCompanionQr->setProperty("enrollmentUrl",
                                             enrollUrl.toString());
    } else {
        m_mirrorNodeCompanionQr->setProperty("generatedAtMs", 0);
        m_mirrorNodeCompanionQr->setProperty("enrollmentUrl", QString());
    }
    if (!enrollUrl.isEmpty() &&
        m_mirrorNodeCompanionQr->property("renderedUrl").toString() !=
            enrollUrl.toString()) {
        const QImage qr = QrCode::encodeToImage(enrollUrl.toString(), 4, 3);
        if (!qr.isNull()) {
            m_mirrorNodeCompanionQr->setPixmap(QPixmap::fromImage(qr));
            m_mirrorNodeCompanionQr->setProperty("renderedUrl",
                                                 enrollUrl.toString());
        }
    }
    m_mirrorNodeCompanionQr->setVisible(!enrolled && !enrollUrl.isEmpty());

    if (!m_networkAccess)
        return;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:8791/v1/status")));
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int httpStatus = reply->attribute(
                                        QNetworkRequest::HttpStatusCodeAttribute)
                                   .toInt();
        const QJsonObject status = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        if (!m_mirrorNodeCompanion)
            return;
        if (httpStatus != 200 || status.isEmpty()) {
            m_mirrorNodeCompanionStatus->setText(
                QStringLiteral("<span style='color:#f85149'>●</span> Mirror node stopped"));
            m_mirrorNodeCompanionStatus->setTextFormat(Qt::RichText);
            m_mirrorNodeCompanionStats->setText(
                QStringLiteral("The local Go mirror-node API is not reachable on "
                               "127.0.0.1:8791."));
            return;
        }
        const bool ready = status.value(QStringLiteral("ready")).toBool();
        m_mirrorNodeCompanionStatus->setText(
            QStringLiteral("<span style='color:%1'>●</span> %2")
                .arg(ready ? QStringLiteral("#3fb950") : QStringLiteral("#d29922"),
                     ready ? QStringLiteral("Mirror node ready")
                           : QStringLiteral("Mirror node starting")));
        m_mirrorNodeCompanionStatus->setTextFormat(Qt::RichText);
        const QJsonObject runtime = status.value(QStringLiteral("runtime")).toObject();
        const QJsonObject processes = status.value(QStringLiteral("processes")).toObject();
        const QJsonObject repositories = status.value(QStringLiteral("repositories")).toObject();
        QStringList processNames;
        for (auto it = processes.begin(); it != processes.end(); ++it) {
            processNames << QStringLiteral("%1 %2")
                                .arg(it.value().toBool() ? QStringLiteral("✓")
                                                        : QStringLiteral("×"),
                                     it.key());
        }
        const QString error = status.value(QStringLiteral("lastSyncError")).toString();
        m_mirrorNodeCompanionStats->setText(
            QStringLiteral(
                "<b>%1</b> · v%2<br><br>"
                "%3<br><br>"
                "<b>Uptime</b> %4<br>"
                "<b>Repositories</b> %5<br>"
                "<b>Sync cycles</b> %6<br>"
                "<b>Go heap</b> %7<br>"
                "<b>Goroutines</b> %8<br>"
                "<b>Last sync</b> %9%10")
                .arg(status.value(QStringLiteral("node")).toString().toHtmlEscaped(),
                     status.value(QStringLiteral("version")).toString().toHtmlEscaped(),
                     processNames.join(QStringLiteral(" &nbsp; ")).toHtmlEscaped(),
                     compactDuration(status.value(QStringLiteral("uptimeSeconds")).toInteger()),
                     QString::number(repositories.size()),
                     QString::number(status.value(QStringLiteral("syncCount")).toInteger()),
                     formatByteSize(runtime.value(QStringLiteral("heapBytes")).toInteger()),
                     QString::number(runtime.value(QStringLiteral("goroutines")).toInt()),
                     status.value(QStringLiteral("lastSyncAt")).toString().toHtmlEscaped(),
                     error.isEmpty()
                         ? QString()
                         : QStringLiteral("<br><span style='color:#f85149'>%1</span>")
                               .arg(error.toHtmlEscaped())));
    });
}
