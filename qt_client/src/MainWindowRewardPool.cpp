// First-instance-owner community reward-pool controls. The Worker creates
// auditable public transfer intents; this desktop verifies and signs one exact
// System Program transfer after explicit owner confirmation, broadcasts it
// directly to the locally configured Solana RPC, then reports finalized public
// chain state back to the Worker. Private key bytes never cross the encrypted
// vault/signer boundary.

#include "MainWindow.h"

#include "MainWindowInternal.h"
#include "RewardPoolSigner.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrlQuery>

#include <cmath>
#include <utility>

using namespace forkmesh::ui;

namespace {

const QString kRewardRpcSetting =
    QStringLiteral("control/rewardPool/publicRpc");
const QString kRewardNetworkSetting =
    QStringLiteral("control/rewardPool/network");
const QString kPendingIntentSetting =
    QStringLiteral("control/rewardPool/pendingIntent");
const QString kPendingSignatureSetting =
    QStringLiteral("control/rewardPool/pendingSignature");
const QString kPendingPoolSetting =
    QStringLiteral("control/rewardPool/pendingPool");
const QString kPendingRpcSetting =
    QStringLiteral("control/rewardPool/pendingRpc");
const QString kPendingNetworkSetting =
    QStringLiteral("control/rewardPool/pendingNetwork");
const QString kPendingSubmittedAtSetting =
    QStringLiteral("control/rewardPool/pendingSubmittedAt");

constexpr int kMaximumWorkerResponse = 1024 * 1024;
constexpr int kFinalizationPollLimit = 36;
constexpr int kFinalizationPollMs = 5000;

QFrame *rewardCard()
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("leaderboardCard"));
    card->setFrameShape(QFrame::StyledPanel);
    return card;
}

QLabel *rewardHint(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("mutedLabel"));
    label->setWordWrap(true);
    return label;
}

QString formatLamports(quint64 lamports)
{
    const quint64 whole = lamports / 1000000000ull;
    QString fraction =
        QString::number(lamports % 1000000000ull).rightJustified(
            9, QLatin1Char('0'));
    while (fraction.endsWith(QLatin1Char('0')) && fraction.size() > 1)
        fraction.chop(1);
    return QStringLiteral("%1.%2 SOL").arg(whole).arg(fraction);
}

void scrubEdit(QLineEdit *edit)
{
    if (!edit)
        return;
    QString overwrite(edit->text().size(), QChar(u'\0'));
    edit->setText(overwrite);
    edit->clear();
    forkmesh::rewards::secureErase(overwrite);
}

void hardenJsonRequest(QNetworkRequest &request)
{
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Cache-Control", "no-store");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(15000);
}

QByteArray boundedReplyBody(QNetworkReply *reply)
{
    if (!reply)
        return {};
    const QByteArray body = reply->read(kMaximumWorkerResponse + 1);
    return body.size() <= kMaximumWorkerResponse ? body : QByteArray();
}

QString publicNetworkFailure(QNetworkReply *reply, const QString &operation)
{
    if (!reply)
        return operation + QStringLiteral(" failed.");
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 404 || status == 405)
        return operation +
               QStringLiteral(
                   " is unavailable on this Worker; install the reward-intent API contract.");
    if (status == 401 || status == 403)
        return operation +
               QStringLiteral(
                   " was denied: this node identity is not authorized as the first-instance owner.");
    if (status > 0)
        return QStringLiteral("%1 failed (HTTP %2).").arg(operation).arg(status);
    return operation + QStringLiteral(" failed before an HTTP response.");
}

QString selectedIntentId(QTableWidget *table)
{
    if (!table)
        return {};
    const int row = table->currentRow();
    if (row < 0 || !table->item(row, 0))
        return {};
    return table->item(row, 0)->data(Qt::UserRole).toString();
}

QString jsonSlotString(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble()) {
        const double slot = value.toDouble(-1);
        if (slot >= 0 && slot <= 9007199254740991.0 &&
            std::floor(slot) == slot)
            return QString::number(quint64(slot));
    }
    return {};
}

} // namespace

QWidget *MainWindow::buildRewardPoolControlCard()
{
    QFrame *card = rewardCard();
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("Community reward-pool signer"));
    title->setObjectName(QStringLiteral("sectionLabel"));
    layout->addWidget(title);
    layout->addWidget(rewardHint(
        QStringLiteral(
            "First-instance owner only. Import an existing Solana pool key into "
            "this desktop's passphrase-encrypted, owner-permission-only vault. "
            "ForkMesh never generates the key, uploads it to a Worker or D1, "
            "writes it to logs, or holds community members' funds. Only the "
            "public pool address and public on-chain transaction state leave "
            "this device. Rewards are community incentives, not investments, "
            "and visual fountain effects are not transactions.")));

    m_rewardPoolVaultStatus = new QLabel;
    m_rewardPoolVaultStatus->setObjectName(
        QStringLiteral("rewardPoolVaultStatus"));
    m_rewardPoolVaultStatus->setWordWrap(true);
    layout->addWidget(m_rewardPoolVaultStatus);

    auto *addressRow = new QHBoxLayout;
    auto *addressTitle = new QLabel(QStringLiteral("Public pool address"));
    addressTitle->setObjectName(QStringLiteral("mutedLabel"));
    addressRow->addWidget(addressTitle);
    m_rewardPoolAddress = new QLabel(QString::fromUtf8("\xE2\x80\x94"));
    m_rewardPoolAddress->setObjectName(
        QStringLiteral("rewardPoolPublicAddress"));
    m_rewardPoolAddress->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_rewardPoolAddress->setWordWrap(true);
    addressRow->addWidget(m_rewardPoolAddress, 1);
    auto *importButton =
        new QPushButton(QStringLiteral("Import existing pool key"));
    importButton->setObjectName(QStringLiteral("rewardPoolImportButton"));
    importButton->setCursor(Qt::PointingHandCursor);
    setOcticon(importButton, QStringLiteral("key"), 14);
    connect(importButton, &QPushButton::clicked, this,
            &MainWindow::importRewardPoolKey);
    addressRow->addWidget(importButton);
    layout->addLayout(addressRow);

    auto *configuration = new QFormLayout;
    configuration->setLabelAlignment(Qt::AlignRight);
    const QSettings settings;
    m_rewardPoolNetworkCombo = new QComboBox;
    m_rewardPoolNetworkCombo->setObjectName(
        QStringLiteral("rewardPoolNetwork"));
    m_rewardPoolNetworkCombo->addItem(QStringLiteral("Mainnet beta"),
                                      QStringLiteral("mainnet-beta"));
    m_rewardPoolNetworkCombo->addItem(QStringLiteral("Devnet (development only)"),
                                      QStringLiteral("devnet"));
    m_rewardPoolNetworkCombo->addItem(QStringLiteral("Testnet (development only)"),
                                      QStringLiteral("testnet"));
    const QString savedNetwork = settings.value(kRewardNetworkSetting).toString();
    const int savedIndex =
        m_rewardPoolNetworkCombo->findData(savedNetwork);
    m_rewardPoolNetworkCombo->setCurrentIndex(savedIndex >= 0 ? savedIndex : 0);
    configuration->addRow(QStringLiteral("Expected network"),
                          m_rewardPoolNetworkCombo);

    m_rewardPoolRpcEdit = new QLineEdit(
        settings.value(kRewardRpcSetting).toString());
    m_rewardPoolRpcEdit->setObjectName(QStringLiteral("rewardPoolRpc"));
    m_rewardPoolRpcEdit->setPlaceholderText(
        QStringLiteral("https://api.mainnet-beta.solana.com (no API key)"));
    m_rewardPoolRpcEdit->setClearButtonEnabled(true);
    configuration->addRow(QStringLiteral("Public Solana RPC"),
                          m_rewardPoolRpcEdit);
    layout->addLayout(configuration);

    auto *configurationRow = new QHBoxLayout;
    auto *saveConfiguration =
        new QPushButton(QStringLiteral("Save public RPC configuration"));
    saveConfiguration->setObjectName(
        QStringLiteral("rewardPoolSaveRpcButton"));
    saveConfiguration->setCursor(Qt::PointingHandCursor);
    setOcticon(saveConfiguration, QStringLiteral("shield-check"), 14);
    connect(saveConfiguration, &QPushButton::clicked, this,
            &MainWindow::saveRewardPoolRpcConfiguration);
    configurationRow->addWidget(saveConfiguration);
    configurationRow->addWidget(rewardHint(
        QStringLiteral(
            "Configuration is deliberately blank on first use. Credential- or "
            "API-key-bearing RPC URLs are rejected instead of stored.")));
    layout->addLayout(configurationRow);

    m_rewardPoolIntentsTable = new QTableWidget(0, 6);
    m_rewardPoolIntentsTable->setObjectName(
        QStringLiteral("rewardPoolIntentsTable"));
    m_rewardPoolIntentsTable->setHorizontalHeaderLabels(
        {QStringLiteral("Intent"), QStringLiteral("Destination"),
         QStringLiteral("Amount"), QStringLiteral("Network"),
         QStringLiteral("Expires"), QStringLiteral("Selected node")});
    m_rewardPoolIntentsTable->verticalHeader()->setVisible(false);
    m_rewardPoolIntentsTable->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    m_rewardPoolIntentsTable->setSelectionMode(
        QAbstractItemView::SingleSelection);
    m_rewardPoolIntentsTable->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    m_rewardPoolIntentsTable->setShowGrid(false);
    m_rewardPoolIntentsTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_rewardPoolIntentsTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    for (int column = 2; column < 6; ++column) {
        m_rewardPoolIntentsTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    }
    connect(m_rewardPoolIntentsTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::refreshRewardPoolControls);
    layout->addWidget(m_rewardPoolIntentsTable);

    auto *actions = new QHBoxLayout;
    m_rewardPoolFetchButton =
        new QPushButton(QStringLiteral("Fetch pending chain intents"));
    m_rewardPoolFetchButton->setObjectName(
        QStringLiteral("rewardPoolFetchButton"));
    m_rewardPoolFetchButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_rewardPoolFetchButton, QStringLiteral("sync"), 14);
    connect(m_rewardPoolFetchButton, &QPushButton::clicked, this,
            &MainWindow::fetchRewardPoolIntents);
    actions->addWidget(m_rewardPoolFetchButton);
    m_rewardPoolSignButton =
        new QPushButton(QStringLiteral("Review, sign and submit selected"));
    m_rewardPoolSignButton->setObjectName(
        QStringLiteral("rewardPoolSignButton"));
    m_rewardPoolSignButton->setProperty("buttonSize", "primary");
    m_rewardPoolSignButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_rewardPoolSignButton, QStringLiteral("credit-card"), 14);
    connect(m_rewardPoolSignButton, &QPushButton::clicked, this,
            &MainWindow::reviewAndSignRewardIntent);
    actions->addWidget(m_rewardPoolSignButton);
    m_rewardPoolReconcileButton =
        new QPushButton(QStringLiteral("Reconcile last submission"));
    m_rewardPoolReconcileButton->setObjectName(
        QStringLiteral("rewardPoolReconcileButton"));
    m_rewardPoolReconcileButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_rewardPoolReconcileButton,
               QStringLiteral("check-circle"), 14);
    connect(m_rewardPoolReconcileButton, &QPushButton::clicked, this,
            [this] {
                if (m_rewardPoolFinalizeTimer)
                    m_rewardPoolFinalizeTimer->stop();
                pollRewardPoolFinalization(0);
            });
    actions->addWidget(m_rewardPoolReconcileButton);
    actions->addStretch();
    layout->addLayout(actions);

    m_rewardPoolStatus = new QLabel(
        QStringLiteral(
            "No chain operation has run. Import a key and explicitly save an "
            "RPC/network configuration before fetching intents."));
    m_rewardPoolStatus->setObjectName(QStringLiteral("rewardPoolStatus"));
    m_rewardPoolStatus->setWordWrap(true);
    layout->addWidget(m_rewardPoolStatus);

    m_rewardPoolFinalizeTimer = new QTimer(card);
    m_rewardPoolFinalizeTimer->setSingleShot(true);
    connect(m_rewardPoolFinalizeTimer, &QTimer::timeout, this, [this] {
        pollRewardPoolFinalization(
            m_rewardPoolFinalizeTimer->property("attempt").toInt());
    });
    refreshRewardPoolControls();
    return card;
}

void MainWindow::refreshRewardPoolControls()
{
    QString vaultError;
    const QString vaultPath = forkmesh::rewards::defaultPoolVaultPath();
    const QString poolAddress =
        vaultPath.isEmpty()
            ? QString()
            : forkmesh::rewards::poolVaultPublicAddress(vaultPath, &vaultError);
    const bool vaultReady = !poolAddress.isEmpty();
    if (m_rewardPoolAddress) {
        m_rewardPoolAddress->setText(
            vaultReady ? poolAddress : QString::fromUtf8("\xE2\x80\x94"));
    }
    if (m_rewardPoolVaultStatus) {
        m_rewardPoolVaultStatus->setText(
            vaultReady
                ? QStringLiteral(
                      "Encrypted local vault ready. The clear value below is "
                      "only the public pool address; unlocking and signing "
                      "require the owner's passphrase every time.")
                : QStringLiteral(
                      "No secure local pool-key vault is configured. %1")
                      .arg(vaultError.isEmpty()
                               ? QStringLiteral(
                                     "ForkMesh will not generate a custody key.")
                               : vaultError));
    }

    const QSettings settings;
    const QString savedRpc = settings.value(kRewardRpcSetting).toString();
    const QString savedNetwork =
        settings.value(kRewardNetworkSetting).toString();
    QUrl rpc;
    QString rpcError;
    const bool rpcReady =
        !savedNetwork.isEmpty() &&
        forkmesh::rewards::validatePublicRpcUrl(savedRpc, &rpc, &rpcError);
    const bool widgetsMatchSaved =
        m_rewardPoolRpcEdit && m_rewardPoolNetworkCombo &&
        m_rewardPoolRpcEdit->text().trimmed() == savedRpc &&
        m_rewardPoolNetworkCombo->currentData().toString() == savedNetwork;
    const bool identityReady =
        m_profileIdentity.isValid();
    const bool pending =
        !settings.value(kPendingSignatureSetting).toString().isEmpty();
    const QString selected = selectedIntentId(m_rewardPoolIntentsTable);
    const bool selectedUnsigned =
        !selected.isEmpty() &&
        m_rewardPoolIntents.value(selected)
                .value(QStringLiteral("status"))
                .toString() == QLatin1String("pending_signature");
    if (m_rewardPoolFetchButton) {
        m_rewardPoolFetchButton->setEnabled(
            !m_rewardPoolBusy && vaultReady && rpcReady &&
            widgetsMatchSaved && identityReady);
    }
    if (m_rewardPoolSignButton) {
        m_rewardPoolSignButton->setEnabled(
            !m_rewardPoolBusy && vaultReady && rpcReady &&
            widgetsMatchSaved && identityReady && !pending &&
            selectedUnsigned);
    }
    if (m_rewardPoolReconcileButton)
        m_rewardPoolReconcileButton->setEnabled(!m_rewardPoolBusy && pending);
}

void MainWindow::importRewardPoolKey()
{
    const QString vaultPath = forkmesh::rewards::defaultPoolVaultPath();
    if (vaultPath.isEmpty()) {
        QMessageBox::critical(
            this, QStringLiteral("Community reward-pool key"),
            QStringLiteral(
                "A secure application-data location is unavailable. No key was imported."));
        return;
    }
    if (QFileInfo::exists(vaultPath) &&
        QMessageBox::warning(
            this, QStringLiteral("Replace encrypted pool-key vault?"),
            QStringLiteral(
                "A community reward-pool key is already configured on this "
                "desktop. Replacing it can make the existing pool impossible "
                "to sign from here. Continue only if you have independently "
                "backed up the old key."),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Import existing Solana pool key"));
    dialog.resize(620, 280);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(rewardHint(
        QStringLiteral(
            "Paste an existing Solana 64-byte keypair (CLI JSON array or "
            "base58), or load a local keypair file. ForkMesh does not generate "
            "a key, transmit it, or delete the source file. The imported seed "
            "is immediately encrypted with scrypt + AES-256-GCM.")));
    auto *form = new QFormLayout;
    auto *keyEdit = new QLineEdit;
    keyEdit->setObjectName(QStringLiteral("rewardPoolPrivateKeyInput"));
    keyEdit->setEchoMode(QLineEdit::Password);
    keyEdit->setMaxLength(4096);
    keyEdit->setPlaceholderText(
        QStringLiteral("64-byte keypair; never logged or uploaded"));
    form->addRow(QStringLiteral("Existing private key"), keyEdit);
    auto *passphraseEdit = new QLineEdit;
    passphraseEdit->setEchoMode(QLineEdit::Password);
    passphraseEdit->setMaxLength(1024);
    passphraseEdit->setPlaceholderText(
        QStringLiteral("at least 12 characters; not stored"));
    form->addRow(QStringLiteral("Vault passphrase"), passphraseEdit);
    auto *repeatEdit = new QLineEdit;
    repeatEdit->setEchoMode(QLineEdit::Password);
    repeatEdit->setMaxLength(1024);
    form->addRow(QStringLiteral("Repeat passphrase"), repeatEdit);
    layout->addLayout(form);
    auto *loadFile =
        new QPushButton(QStringLiteral("Load Solana keypair file…"));
    loadFile->setCursor(Qt::PointingHandCursor);
    connect(loadFile, &QPushButton::clicked, &dialog, [this, keyEdit] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Select an existing Solana keypair"),
            QString(), QStringLiteral("JSON keypair (*.json);;All files (*)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
            file.size() > 4096) {
            QMessageBox::warning(
                this, QStringLiteral("Solana keypair"),
                QStringLiteral("The selected keypair file is unreadable or too large."));
            return;
        }
        QByteArray bytes = file.read(4097);
        keyEdit->setText(QString::fromUtf8(bytes).trimmed());
        forkmesh::rewards::secureErase(bytes);
    });
    layout->addWidget(loadFile, 0, Qt::AlignLeft);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText(
        QStringLiteral("Inspect existing key"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        scrubEdit(keyEdit);
        scrubEdit(passphraseEdit);
        scrubEdit(repeatEdit);
        return;
    }

    QString keyText = keyEdit->text();
    QByteArray keyMaterial = keyText.toUtf8();
    QString passphrase = passphraseEdit->text();
    QString repeated = repeatEdit->text();
    scrubEdit(keyEdit);
    scrubEdit(passphraseEdit);
    scrubEdit(repeatEdit);
    forkmesh::rewards::secureErase(keyText);
    if (passphrase != repeated || passphrase.size() < 12) {
        forkmesh::rewards::secureErase(keyMaterial);
        forkmesh::rewards::secureErase(passphrase);
        forkmesh::rewards::secureErase(repeated);
        QMessageBox::warning(
            this, QStringLiteral("Community reward-pool key"),
            QStringLiteral(
                "The passphrases did not match, or were shorter than 12 characters."));
        return;
    }
    forkmesh::rewards::secureErase(repeated);

    QString inspectError;
    const QString address =
        forkmesh::rewards::poolAddressForKeyMaterial(keyMaterial,
                                                     &inspectError);
    if (address.isEmpty()) {
        forkmesh::rewards::secureErase(keyMaterial);
        forkmesh::rewards::secureErase(passphrase);
        QMessageBox::warning(this,
                             QStringLiteral("Community reward-pool key"),
                             inspectError);
        return;
    }
    if (QMessageBox::question(
            this, QStringLiteral("Confirm public pool address"),
            QStringLiteral(
                "The imported key controls this public Solana address:\n\n%1\n\n"
                "Confirm that this is the intended community reward pool. "
                "ForkMesh will store only its encrypted private seed and this "
                "public address on the local device.")
                .arg(address),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        forkmesh::rewards::secureErase(keyMaterial);
        forkmesh::rewards::secureErase(passphrase);
        return;
    }

    QString imported;
    QString importError;
    const bool ok = forkmesh::rewards::importPoolKey(
        keyMaterial, passphrase, vaultPath, &imported, &importError);
    forkmesh::rewards::secureErase(keyMaterial);
    forkmesh::rewards::secureErase(passphrase);
    if (!ok) {
        QMessageBox::critical(this,
                              QStringLiteral("Community reward-pool key"),
                              importError);
        return;
    }
    if (m_rewardPoolStatus) {
        m_rewardPoolStatus->setText(
            QStringLiteral(
                "Existing pool key imported into the encrypted local vault. "
                "Only public address %1 is displayed or shared.")
                .arg(imported));
    }
    logSystem(
        QStringLiteral(
            "Reward pool: encrypted local signer vault configured for public address %1.")
            .arg(imported));
    refreshRewardPoolControls();
}

void MainWindow::saveRewardPoolRpcConfiguration()
{
    const QString rpcText =
        m_rewardPoolRpcEdit ? m_rewardPoolRpcEdit->text().trimmed()
                            : QString();
    const QString network =
        m_rewardPoolNetworkCombo
            ? m_rewardPoolNetworkCombo->currentData().toString()
            : QString();
    QUrl normalized;
    QString error;
    if ((network != QLatin1String("devnet") &&
         network != QLatin1String("testnet") &&
         network != QLatin1String("mainnet-beta")) ||
        !forkmesh::rewards::validatePublicRpcUrl(rpcText, &normalized,
                                                 &error)) {
        if (m_rewardPoolStatus)
            m_rewardPoolStatus->setText(error);
        refreshRewardPoolControls();
        return;
    }
    QSettings settings;
    settings.setValue(kRewardRpcSetting,
                      normalized.toString(QUrl::StripTrailingSlash));
    settings.setValue(kRewardNetworkSetting, network);
    if (m_rewardPoolRpcEdit)
        m_rewardPoolRpcEdit->setText(
            normalized.toString(QUrl::StripTrailingSlash));
    if (m_rewardPoolStatus) {
        m_rewardPoolStatus->setText(
            QStringLiteral(
                "%1 public RPC configuration saved locally. No RPC API key or "
                "credential was accepted.")
                .arg(network));
    }
    logSystem(
        QStringLiteral("Reward pool: public %1 RPC configuration updated.")
            .arg(network));
    refreshRewardPoolControls();
}

QUrl MainWindow::rewardPoolWorkerEndpoint(const QString &path) const
{
    if ((path != QLatin1String("/api/rewards/signing-jobs") &&
         path != QLatin1String("/api/rewards/pool")) ||
        m_servers.isEmpty() || m_activeServer < 0 ||
        m_activeServer >= m_servers.size())
        return {};
    QUrl url(canonicalServerUrl(m_servers.at(m_activeServer).url));
    if (!url.isValid() || url.host().isEmpty())
        return {};
    if (url.scheme() == QLatin1String("wss"))
        url.setScheme(QStringLiteral("https"));
    else if (url.scheme() == QLatin1String("ws"))
        url.setScheme(QStringLiteral("http"));
    if (url.scheme() != QLatin1String("https"))
        return {};
    url.setUserInfo(QString());
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void MainWindow::fetchRewardPoolIntents()
{
    if (m_rewardPoolBusy)
        return;
    QString vaultError;
    const QString poolAddress =
        forkmesh::rewards::poolVaultPublicAddress(
            forkmesh::rewards::defaultPoolVaultPath(), &vaultError);
    QSettings settings;
    const QString network =
        settings.value(kRewardNetworkSetting).toString();
    const QString savedRpc = settings.value(kRewardRpcSetting).toString();
    QUrl rpc;
    QString rpcError;
    if (poolAddress.isEmpty() ||
        !forkmesh::rewards::validatePublicRpcUrl(savedRpc, &rpc,
                                                 &rpcError) ||
        !m_rewardPoolRpcEdit || !m_rewardPoolNetworkCombo ||
        m_rewardPoolRpcEdit->text().trimmed() != savedRpc ||
        m_rewardPoolNetworkCombo->currentData().toString() != network) {
        if (m_rewardPoolStatus) {
            m_rewardPoolStatus->setText(
                QStringLiteral(
                    "Fail closed: configure the encrypted pool vault and save "
                    "the current public RPC/network settings before fetching."));
        }
        refreshRewardPoolControls();
        return;
    }
    const QString signer = accountOwner().trimmed().toLower();
    if (signer.isEmpty() || !hasOwnerSigningCapability(signer) ||
        (!m_profileIdentity.isValid() && !m_profileIdentity.load()) ||
        !m_profileIdentity.isValid()) {
        if (m_rewardPoolStatus) {
            m_rewardPoolStatus->setText(
                QStringLiteral(
                    "Fail closed: sign in with the configured first-instance "
                    "owner account and its local identity key."));
        }
        return;
    }
    QUrl endpoint = rewardPoolWorkerEndpoint(
        QStringLiteral("/api/rewards/signing-jobs"));
    const QString timestamp =
        QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        forkmesh::rewards::signingJobsFetchCanonical(signer, timestamp);
    const QString signature = m_profileIdentity.signData(canonical);
    if (endpoint.isEmpty() || canonical.isEmpty() || signature.isEmpty() ||
        !m_networkAccess) {
        if (m_rewardPoolStatus) {
            m_rewardPoolStatus->setText(
                QStringLiteral(
                    "Fail closed: an HTTPS Worker relay and authorized local "
                    "first-owner identity are required."));
        }
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("signer"), signer);
    query.addQueryItem(QStringLiteral("ts"), timestamp);
    query.addQueryItem(QStringLiteral("sig"), signature);
    endpoint.setQuery(query);

    QNetworkRequest request(endpoint);
    hardenJsonRequest(request);
    m_rewardPoolBusy = true;
    refreshRewardPoolControls();
    if (m_rewardPoolStatus)
        m_rewardPoolStatus->setText(
            QStringLiteral("Fetching signed-owner pending intent list…"));
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, poolAddress, network] {
        const QByteArray body = boundedReplyBody(reply);
        const bool networkOk =
            reply->error() == QNetworkReply::NoError;
        const QString failure =
            publicNetworkFailure(reply,
                                 QStringLiteral("Reward-intent fetch"));
        reply->deleteLater();
        m_rewardPoolBusy = false;
        if (!networkOk || body.isEmpty()) {
            if (m_rewardPoolStatus)
                m_rewardPoolStatus->setText(failure);
            refreshRewardPoolControls();
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(body, &parseError);
        const QJsonObject response = document.object();
        const QJsonArray intents =
            response.value(QStringLiteral("jobs")).toArray();
        if (parseError.error != QJsonParseError::NoError ||
            !response.value(QStringLiteral("ok")).toBool(false) ||
            response.value(QStringLiteral("custody")).toString() !=
                QLatin1String("external-local-signer")) {
            if (m_rewardPoolStatus) {
                m_rewardPoolStatus->setText(
                    QStringLiteral(
                        "The Worker returned an invalid external-signer job envelope; nothing was signed."));
            }
            refreshRewardPoolControls();
            return;
        }

        m_rewardPoolIntents.clear();
        if (m_rewardPoolIntentsTable)
            m_rewardPoolIntentsTable->setRowCount(0);
        int rejected = 0;
        for (const QJsonValue &value : intents) {
            if (!value.isObject()) {
                ++rejected;
                continue;
            }
            const QJsonObject object = value.toObject();
            forkmesh::rewards::RewardSigningJob intent;
            QString validationError;
            if (!forkmesh::rewards::parseRewardSigningJob(
                    object, poolAddress, network,
                    QDateTime::currentDateTimeUtc(), &intent,
                    &validationError)) {
                ++rejected;
                continue;
            }
            if (m_rewardPoolIntents.contains(intent.intentId)) {
                ++rejected;
                continue;
            }
            m_rewardPoolIntents.insert(intent.intentId, object);
            if (!m_rewardPoolIntentsTable)
                continue;
            const int row = m_rewardPoolIntentsTable->rowCount();
            m_rewardPoolIntentsTable->insertRow(row);
            auto *id = new QTableWidgetItem(
                QStringLiteral("%1 · %2")
                    .arg(intent.intentId.left(12), intent.status));
            id->setData(Qt::UserRole, intent.intentId);
            id->setToolTip(intent.intentId);
            m_rewardPoolIntentsTable->setItem(row, 0, id);
            QStringList destinations;
            for (const forkmesh::rewards::RewardTransfer &transfer :
                 std::as_const(intent.transfers)) {
                destinations.append(transfer.destinationWallet);
            }
            auto *destination =
                new QTableWidgetItem(
                    destinations.size() == 1
                        ? destinations.first()
                        : QStringLiteral("%1 reviewed recipients")
                              .arg(destinations.size()));
            destination->setToolTip(destinations.join(QLatin1Char('\n')));
            m_rewardPoolIntentsTable->setItem(row, 1, destination);
            m_rewardPoolIntentsTable->setItem(
                row, 2,
                new QTableWidgetItem(
                    formatLamports(intent.totalLamports)));
            m_rewardPoolIntentsTable->setItem(
                row, 3, new QTableWidgetItem(intent.network));
            m_rewardPoolIntentsTable->setItem(
                row, 4,
                new QTableWidgetItem(
                    QDateTime::fromMSecsSinceEpoch(intent.expiresAtMs)
                        .toLocalTime()
                        .toString(
                        QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
            QStringList nodes;
            for (const forkmesh::rewards::RewardTransfer &transfer :
                 std::as_const(intent.transfers)) {
                if (!transfer.nodeId.isEmpty())
                    nodes.append(transfer.nodeId);
            }
            m_rewardPoolIntentsTable->setItem(
                row, 5,
                new QTableWidgetItem(
                    nodes.isEmpty()
                        ? QString::fromUtf8("\xE2\x80\x94")
                        : nodes.join(QStringLiteral(", "))));
        }
        if (m_rewardPoolStatus) {
            m_rewardPoolStatus->setText(
                QStringLiteral(
                    "%1 locally verified pending intent(s); %2 malformed, "
                    "expired, mismatched, or unsupported intent(s) rejected.")
                    .arg(m_rewardPoolIntents.size())
                    .arg(rejected));
        }
        if (m_rewardPoolIntentsTable &&
            m_rewardPoolIntentsTable->rowCount() > 0)
            m_rewardPoolIntentsTable->selectRow(0);
        refreshRewardPoolControls();
    });
}

void MainWindow::reviewAndSignRewardIntent()
{
    if (m_rewardPoolBusy)
        return;
    const QString intentId =
        selectedIntentId(m_rewardPoolIntentsTable);
    if (intentId.isEmpty() || !m_rewardPoolIntents.contains(intentId))
        return;
    const QSettings settings;
    const QString network =
        settings.value(kRewardNetworkSetting).toString();
    QUrl rpc;
    QString error;
    if (!forkmesh::rewards::validatePublicRpcUrl(
            settings.value(kRewardRpcSetting).toString(), &rpc, &error)) {
        if (m_rewardPoolStatus)
            m_rewardPoolStatus->setText(error);
        return;
    }
    const QString vaultPath =
        forkmesh::rewards::defaultPoolVaultPath();
    const QString poolAddress =
        forkmesh::rewards::poolVaultPublicAddress(vaultPath, &error);
    forkmesh::rewards::RewardSigningJob intent;
    const QJsonObject object = m_rewardPoolIntents.value(intentId);
    if (poolAddress.isEmpty() ||
        !forkmesh::rewards::parseRewardSigningJob(
            object, poolAddress, network, QDateTime::currentDateTimeUtc(),
            &intent, &error)) {
        if (m_rewardPoolStatus)
            m_rewardPoolStatus->setText(
                QStringLiteral("Intent re-validation failed: %1").arg(error));
        return;
    }
    if (intent.status != QLatin1String("pending_signature")) {
        if (m_rewardPoolStatus)
            m_rewardPoolStatus->setText(
                QStringLiteral("That Worker job is already submitted."));
        return;
    }

    QStringList recipients;
    for (const forkmesh::rewards::RewardTransfer &transfer :
         std::as_const(intent.transfers)) {
        recipients.append(
            QStringLiteral("%1 — %2")
                .arg(transfer.destinationWallet,
                     formatLamports(transfer.lamports)));
    }
    QString recipientPreview;
    // Escape each row without escaping the intentional line breaks.
    QStringList escapedRows;
    for (const QString &row : recipients.mid(0, 5))
        escapedRows.append(row.toHtmlEscaped());
    recipientPreview = escapedRows.join(QStringLiteral("<br>"));
    if (recipients.size() > 5) {
        recipientPreview +=
            QStringLiteral("<br>…and %1 more")
                .arg(recipients.size() - 5);
    }

    QMessageBox confirmation(this);
    confirmation.setIcon(QMessageBox::Warning);
    confirmation.setWindowTitle(
        QStringLiteral("Confirm community reward transfer"));
    confirmation.setText(
        QStringLiteral(
            "<b>Explicit owner approval required</b><br><br>"
            "Transfer a total of <b>%1</b> from public pool<br><code>%2</code>"
            "<br>through %3 exact System Program transfer(s):<br>%4<br><br>"
            "Network: %5<br>Policy: %6<br>Intent: %7<br>Expires: %8<br><br>"
            "A normal Solana network fee is additional. No other program or "
            "instruction is allowed. The transaction is submitted "
            "directly to your configured public RPC. Visual fountain effects "
            "are separate from this transaction.")
            .arg(formatLamports(intent.totalLamports).toHtmlEscaped(),
                 intent.sourceWallet.toHtmlEscaped(),
                 QString::number(intent.transfers.size()),
                 recipientPreview,
                 intent.network.toHtmlEscaped(),
                 intent.policy.toHtmlEscaped(),
                 intent.intentId.toHtmlEscaped(),
                 QDateTime::fromMSecsSinceEpoch(intent.expiresAtMs)
                     .toLocalTime()
                     .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss t"))
                     .toHtmlEscaped()));
    confirmation.setTextFormat(Qt::RichText);
    QPushButton *approve = confirmation.addButton(
        QStringLiteral("Confirm, sign and submit"), QMessageBox::AcceptRole);
    confirmation.addButton(QMessageBox::Cancel);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    confirmation.exec();
    if (confirmation.clickedButton() != approve)
        return;
    prepareRewardPoolTransaction(object);
}

void MainWindow::prepareRewardPoolTransaction(const QJsonObject &job)
{
    if (m_rewardPoolBusy)
        return;
    const QSettings settings;
    const QString network =
        settings.value(kRewardNetworkSetting).toString();
    QUrl rpc;
    QString error;
    if (!forkmesh::rewards::validatePublicRpcUrl(
            settings.value(kRewardRpcSetting).toString(), &rpc, &error)) {
        if (m_rewardPoolStatus)
            m_rewardPoolStatus->setText(error);
        return;
    }
    const QString vaultPath =
        forkmesh::rewards::defaultPoolVaultPath();
    const QString poolAddress =
        forkmesh::rewards::poolVaultPublicAddress(vaultPath, &error);
    forkmesh::rewards::RewardSigningJob parsedJob;
    if (poolAddress.isEmpty() ||
        !forkmesh::rewards::parseRewardSigningJob(
            job, poolAddress, network, QDateTime::currentDateTimeUtc(),
            &parsedJob, &error) ||
        parsedJob.status != QLatin1String("pending_signature")) {
        if (m_rewardPoolStatus)
            m_rewardPoolStatus->setText(
                QStringLiteral("Job re-validation failed: %1").arg(error));
        return;
    }

    QNetworkRequest request(rpc);
    hardenJsonRequest(request);
    const QJsonObject payload{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), QStringLiteral("forkmesh-reward-blockhash")},
        {QStringLiteral("method"), QStringLiteral("getLatestBlockhash")},
        {QStringLiteral("params"),
         QJsonArray{QJsonObject{
             {QStringLiteral("commitment"), QStringLiteral("finalized")}}}}};
    m_rewardPoolBusy = true;
    refreshRewardPoolControls();
    if (m_rewardPoolStatus) {
        m_rewardPoolStatus->setText(
            QStringLiteral(
                "Owner approved the public plan; fetching a recent Solana "
                "blockhash before the one-use local vault unlock…"));
    }
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, job, network, poolAddress, vaultPath] {
        const QByteArray body = boundedReplyBody(reply);
        const bool networkOk =
            reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        m_rewardPoolBusy = false;
        const QJsonObject response =
            QJsonDocument::fromJson(body).object();
        const QJsonObject value =
            response.value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("value"))
                .toObject();
        const QString blockhash =
            value.value(QStringLiteral("blockhash")).toString();
        const QString heightText =
            jsonSlotString(
                value.value(QStringLiteral("lastValidBlockHeight")));
        bool heightOk = false;
        const quint64 lastValidBlockHeight =
            heightText.toULongLong(&heightOk);
        QString error;
        QJsonObject transactionIntent;
        if (!networkOk ||
            !response.value(QStringLiteral("error")).isUndefined() ||
            !heightOk ||
            !forkmesh::rewards::buildRewardTransactionIntent(
                job, poolAddress, network, blockhash,
                lastValidBlockHeight, QDateTime::currentDateTimeUtc(),
                &transactionIntent, &error)) {
            if (m_rewardPoolStatus) {
                m_rewardPoolStatus->setText(
                    error.isEmpty()
                        ? QStringLiteral(
                              "The Solana RPC did not return a usable recent blockhash; nothing was signed.")
                        : error);
            }
            refreshRewardPoolControls();
            return;
        }

        QInputDialog unlock(this);
        unlock.setWindowTitle(
            QStringLiteral("Unlock local pool-key vault"));
        unlock.setLabelText(
            QStringLiteral(
                "Enter the local vault passphrase. It is used only in memory "
                "for this one approved transaction and is never saved or transmitted."));
        unlock.setTextEchoMode(QLineEdit::Password);
        unlock.setInputMode(QInputDialog::TextInput);
        if (unlock.exec() != QDialog::Accepted)
            return;
        QString passphrase = unlock.textValue();
        unlock.setTextValue(
            QString(passphrase.size(), QChar(u'\0')));
        unlock.setTextValue(QString());
        if (passphrase.isEmpty())
            return;

        forkmesh::rewards::SignedRewardTransaction signedTransaction;
        const bool signedOk =
            forkmesh::rewards::signRewardTransferIntent(
                transactionIntent, network, passphrase, vaultPath,
                QDateTime::currentDateTimeUtc(), &signedTransaction,
                &error);
        forkmesh::rewards::secureErase(passphrase);
        if (!signedOk) {
            if (m_rewardPoolStatus)
                m_rewardPoolStatus->setText(
                    QStringLiteral("Local signing failed: %1").arg(error));
            return;
        }
        if (signedTransaction.intentId !=
                transactionIntent.value(
                                     QStringLiteral("intentId"))
                    .toString() ||
            signedTransaction.signerPublicKey != poolAddress ||
            !forkmesh::rewards::isValidSolanaSignature(
                signedTransaction.chainSignature)) {
            if (m_rewardPoolStatus) {
                m_rewardPoolStatus->setText(
                    QStringLiteral(
                        "The local signer failed its post-signature checks; nothing was submitted."));
            }
            return;
        }
        submitRewardPoolTransaction(
            transactionIntent,
            signedTransaction.signedTransactionBase64,
            signedTransaction.chainSignature);
    });
}

void MainWindow::submitRewardPoolTransaction(
    const QJsonObject &intent, const QString &signedTransactionBase64,
    const QString &chainSignature)
{
    if (m_rewardPoolBusy)
        return;
    const QSettings settings;
    QUrl rpc;
    QString error;
    const QString network =
        settings.value(kRewardNetworkSetting).toString();
    if (!forkmesh::rewards::validatePublicRpcUrl(
            settings.value(kRewardRpcSetting).toString(), &rpc, &error) ||
        intent.value(QStringLiteral("network")).toString() != network ||
        !forkmesh::rewards::isValidSolanaSignature(chainSignature)) {
        if (m_rewardPoolStatus) {
            m_rewardPoolStatus->setText(
                QStringLiteral(
                    "Fail closed: the local RPC/network configuration changed before submission."));
        }
        return;
    }
    const QString intentId =
        intent.value(QStringLiteral("intentId")).toString();
    const QString poolAddress =
        intent.value(QStringLiteral("sourceWallet")).toString();
    QNetworkRequest request(rpc);
    hardenJsonRequest(request);
    const QJsonObject rpcPayload{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), QStringLiteral("forkmesh-reward-submit")},
        {QStringLiteral("method"), QStringLiteral("sendTransaction")},
        {QStringLiteral("params"),
         QJsonArray{
             signedTransactionBase64,
             QJsonObject{
                 {QStringLiteral("encoding"), QStringLiteral("base64")},
                 {QStringLiteral("skipPreflight"), false},
                 {QStringLiteral("preflightCommitment"),
                  QStringLiteral("finalized")},
                 {QStringLiteral("maxRetries"), 3}}}}};
    m_rewardPoolBusy = true;
    refreshRewardPoolControls();
    if (m_rewardPoolStatus) {
        m_rewardPoolStatus->setText(
            QStringLiteral(
                "Locally signed; submitting the public transaction directly "
                "to the configured Solana RPC…"));
    }
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(rpcPayload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, intentId, poolAddress, network, rpc,
             chainSignature] {
        const QByteArray body = boundedReplyBody(reply);
        const bool networkOk =
            reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        m_rewardPoolBusy = false;
        const QJsonObject response =
            QJsonDocument::fromJson(body).object();
        const QString returnedSignature =
            response.value(QStringLiteral("result")).toString();
        if (!networkOk || body.isEmpty() ||
            !response.value(QStringLiteral("error")).isUndefined() ||
            returnedSignature != chainSignature) {
            if (m_rewardPoolStatus) {
                const QJsonObject rpcError =
                    response.value(QStringLiteral("error")).toObject();
                const QString code =
                    rpcError.value(QStringLiteral("code")).toVariant().toString();
                m_rewardPoolStatus->setText(
                    code.isEmpty()
                        ? QStringLiteral(
                              "Solana RPC submission failed; no completed "
                              "transfer was recorded.")
                        : QStringLiteral(
                              "Solana RPC rejected the transaction (public "
                              "error code %1).")
                              .arg(code));
            }
            refreshRewardPoolControls();
            return;
        }
        QSettings settings;
        settings.setValue(kPendingIntentSetting, intentId);
        settings.setValue(kPendingSignatureSetting, chainSignature);
        settings.setValue(kPendingPoolSetting, poolAddress);
        settings.setValue(
            kPendingRpcSetting,
            rpc.toString(QUrl::StripTrailingSlash));
        settings.setValue(kPendingNetworkSetting, network);
        settings.setValue(
            kPendingSubmittedAtSetting,
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        if (m_rewardPoolStatus) {
            m_rewardPoolStatus->setText(
                QStringLiteral(
                    "Transaction submitted (%1…). Waiting for finalized "
                    "on-chain status before Worker reconciliation.")
                    .arg(chainSignature.left(12)));
        }
        logSystem(
            QStringLiteral(
                "Reward pool: public transaction %1 submitted for intent %2.")
                .arg(chainSignature.left(16), intentId.left(16)));
        refreshRewardPoolControls();
        submitRewardSignatureReceipt(intentId, chainSignature, false);
    });
}

void MainWindow::submitRewardSignatureReceipt(
    const QString &intentId, const QString &chainSignature,
    bool finalizationObserved)
{
    if (m_rewardPoolBusy)
        return;
    const QString signer = accountOwner().trimmed().toLower();
    const QString timestamp =
        QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        forkmesh::rewards::signingJobSubmitCanonical(
            signer, intentId, chainSignature, timestamp);
    const QString ownerSignature =
        m_profileIdentity.isValid()
            ? m_profileIdentity.signData(canonical)
            : QString();
    const QUrl endpoint = rewardPoolWorkerEndpoint(
        QStringLiteral("/api/rewards/signing-jobs"));
    if (canonical.isEmpty() || ownerSignature.isEmpty() ||
        endpoint.isEmpty() || !m_networkAccess ||
        !hasOwnerSigningCapability(signer)) {
        if (m_rewardPoolStatus) {
            m_rewardPoolStatus->setText(
                QStringLiteral(
                    "The public transaction was submitted, but the configured "
                    "first-owner identity cannot send its Worker receipt. The "
                    "public signature was retained for retry."));
        }
        refreshRewardPoolControls();
        return;
    }
    const QJsonObject payload{
        {QStringLiteral("signer"), signer},
        {QStringLiteral("intentId"), intentId},
        {QStringLiteral("transactionSignature"), chainSignature},
        {QStringLiteral("ts"), timestamp},
        {QStringLiteral("sig"), ownerSignature}};
    QNetworkRequest request(endpoint);
    hardenJsonRequest(request);
    m_rewardPoolBusy = true;
    refreshRewardPoolControls();
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, intentId, chainSignature,
             finalizationObserved] {
        const QByteArray body = boundedReplyBody(reply);
        const int httpStatus =
            reply->attribute(
                     QNetworkRequest::HttpStatusCodeAttribute)
                .toInt();
        const bool networkOk =
            reply->error() == QNetworkReply::NoError;
        const QString failure =
            publicNetworkFailure(
                reply, QStringLiteral("Reward signature receipt"));
        reply->deleteLater();
        m_rewardPoolBusy = false;
        const QJsonObject response =
            QJsonDocument::fromJson(body).object();
        const QJsonObject job =
            response.value(QStringLiteral("job")).toObject();
        const QString jobStatus =
            job.value(QStringLiteral("status")).toString();
        const bool completed =
            networkOk &&
            response.value(QStringLiteral("ok")).toBool(false) &&
            job.value(QStringLiteral("intentId")).toString() ==
                intentId &&
            job.value(QStringLiteral("transactionSignature")).toString() ==
                chainSignature &&
            jobStatus == QLatin1String("completed");
        const bool pending =
            networkOk &&
            response.value(QStringLiteral("ok")).toBool(false) &&
            (httpStatus == 202 ||
             response.value(QStringLiteral("status")).toString() ==
                 QLatin1String("submitted"));
        const QString workerError =
            response.value(QStringLiteral("error")).toString();
        const bool terminalFailure =
            finalizationObserved &&
            (workerError == QLatin1String(
                                "transaction_plan_mismatch") ||
             workerError == QLatin1String(
                                "intent_already_completed"));
        if (completed || terminalFailure) {
            QSettings settings;
            settings.remove(kPendingIntentSetting);
            settings.remove(kPendingSignatureSetting);
            settings.remove(kPendingPoolSetting);
            settings.remove(kPendingRpcSetting);
            settings.remove(kPendingNetworkSetting);
            settings.remove(kPendingSubmittedAtSetting);
            m_rewardPoolIntents.remove(intentId);
            if (m_rewardPoolIntentsTable) {
                for (int row =
                         m_rewardPoolIntentsTable->rowCount() - 1;
                     row >= 0; --row) {
                    if (m_rewardPoolIntentsTable->item(row, 0) &&
                        m_rewardPoolIntentsTable->item(row, 0)
                                ->data(Qt::UserRole)
                                .toString() == intentId) {
                        m_rewardPoolIntentsTable->removeRow(row);
                    }
                }
            }
            if (m_rewardPoolStatus) {
                m_rewardPoolStatus->setText(
                    completed
                        ? QStringLiteral(
                              "Completed on chain and independently reconciled "
                              "by the Worker (signature %1…).")
                              .arg(chainSignature.left(12))
                        : QStringLiteral(
                              "The finalized transaction did not match the "
                              "Worker plan and was recorded as rejected."));
            }
            logSystem(
                QStringLiteral(
                    "Reward pool: intent %1 reached terminal Worker status %2.")
                    .arg(intentId.left(16),
                         completed ? QStringLiteral("completed")
                                   : QStringLiteral("rejected")));
            refreshRewardPoolControls();
            return;
        }
        if (pending) {
            if (m_rewardPoolStatus) {
                m_rewardPoolStatus->setText(
                    QStringLiteral(
                        "Worker recorded the public signature and is awaiting "
                        "finality; the desktop is reconciling it independently."));
            }
            refreshRewardPoolControls();
            pollRewardPoolFinalization(0);
            return;
        }
        if (m_rewardPoolStatus)
            m_rewardPoolStatus->setText(failure);
        refreshRewardPoolControls();
    });
}

void MainWindow::pollRewardPoolFinalization(int attempt)
{
    if (m_rewardPoolBusy)
        return;
    QSettings settings;
    const QString intentId =
        settings.value(kPendingIntentSetting).toString();
    const QString chainSignature =
        settings.value(kPendingSignatureSetting).toString();
    const QString poolAddress =
        settings.value(kPendingPoolSetting).toString();
    const QString network =
        settings.value(kPendingNetworkSetting).toString();
    QUrl rpc;
    QString error;
    if (intentId.isEmpty() || poolAddress.isEmpty() ||
        !forkmesh::rewards::isValidSolanaSignature(chainSignature) ||
        !forkmesh::rewards::validatePublicRpcUrl(
            settings.value(kPendingRpcSetting).toString(), &rpc, &error) ||
        (network != QLatin1String("devnet") &&
         network != QLatin1String("testnet") &&
         network != QLatin1String("mainnet-beta"))) {
        if (m_rewardPoolStatus) {
            m_rewardPoolStatus->setText(
                QStringLiteral(
                    "No valid public submitted-transaction record is available to reconcile."));
        }
        refreshRewardPoolControls();
        return;
    }
    if (attempt >= kFinalizationPollLimit) {
        if (m_rewardPoolStatus) {
            m_rewardPoolStatus->setText(
                QStringLiteral(
                    "The transaction remains submitted but was not finalized "
                    "during this polling window. Use Reconcile last submission "
                    "later; no key or signed transaction was persisted."));
        }
        refreshRewardPoolControls();
        return;
    }

    QNetworkRequest request(rpc);
    hardenJsonRequest(request);
    const QJsonObject payload{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), QStringLiteral("forkmesh-reward-status")},
        {QStringLiteral("method"), QStringLiteral("getSignatureStatuses")},
        {QStringLiteral("params"),
         QJsonArray{
             QJsonArray{chainSignature},
             QJsonObject{{QStringLiteral("searchTransactionHistory"), true}}}}};
    m_rewardPoolBusy = true;
    refreshRewardPoolControls();
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, attempt, intentId, chainSignature] {
        const QByteArray body = boundedReplyBody(reply);
        const bool networkOk =
            reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        m_rewardPoolBusy = false;
        const QJsonObject response =
            QJsonDocument::fromJson(body).object();
        const QJsonArray values =
            response.value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("value"))
                .toArray();
        const QJsonValue statusValue =
            values.isEmpty() ? QJsonValue() : values.at(0);
        if (networkOk && response.value(QStringLiteral("error")).isUndefined() &&
            statusValue.isObject()) {
            const QJsonObject status = statusValue.toObject();
            const QString confirmation =
                status.value(QStringLiteral("confirmationStatus")).toString();
            const QString slot =
                jsonSlotString(status.value(QStringLiteral("slot")));
            if (confirmation == QLatin1String("finalized") &&
                !slot.isEmpty()) {
                const bool failed =
                    !status.value(QStringLiteral("err")).isNull() &&
                    !status.value(QStringLiteral("err")).isUndefined();
                reconcileRewardPoolIntent(
                    intentId, chainSignature,
                    failed ? QStringLiteral("failed")
                           : QStringLiteral("finalized"),
                    slot);
                return;
            }
        }
        if (m_rewardPoolStatus) {
            m_rewardPoolStatus->setText(
                QStringLiteral(
                    "Transaction %1… is not finalized yet (check %2/%3).")
                    .arg(chainSignature.left(12))
                    .arg(attempt + 1)
                    .arg(kFinalizationPollLimit));
        }
        refreshRewardPoolControls();
        if (m_rewardPoolFinalizeTimer) {
            m_rewardPoolFinalizeTimer->setProperty("attempt", attempt + 1);
            m_rewardPoolFinalizeTimer->start(kFinalizationPollMs);
        }
    });
}

void MainWindow::reconcileRewardPoolIntent(
    const QString &intentId, const QString &chainSignature,
    const QString &observedStatus, const QString &slot)
{
    if (m_rewardPoolStatus) {
        m_rewardPoolStatus->setText(
            observedStatus == QLatin1String("finalized")
                ? QStringLiteral(
                      "Finalized chain status observed at slot %1; asking the "
                      "Worker to independently verify the exact public transfer plan…")
                      .arg(slot)
                : QStringLiteral(
                      "A finalized on-chain failure was observed at slot %1; "
                      "asking the Worker to record the terminal result…")
                      .arg(slot));
    }
    submitRewardSignatureReceipt(intentId, chainSignature, true);
}
