// MainWindowSettings: MainWindow feature methods, split out of MainWindow.cpp.
// Settings, firewall, and diagnostics.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "QrCode.h"

#include <QClipboard>
#include <QDialog>
#include <QInputDialog>
#include "KebabHeaderView.h"
#include "ScreenAlignmentTarget.h"

#include <QDoubleSpinBox>

using namespace forkmesh::ui;

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
    m_settingsNameEdit->setPlaceholderText("Name");
    connect(m_settingsNameEdit, &QLineEdit::editingFinished, this,
            [this] { onProfileNameChanged(m_settingsNameEdit->text()); });

    m_settingsAvatarPreview = new QLabel("No\navatar");
    m_settingsAvatarPreview->setObjectName("avatarPreview");
    m_settingsAvatarPreview->setFixedSize(64, 64);
    m_settingsAvatarPreview->setAlignment(Qt::AlignCenter);
    auto *uploadButton = new QPushButton("Upload…");
    uploadButton->setObjectName("ghostButton");
    uploadButton->setCursor(Qt::PointingHandCursor);
    connect(uploadButton, &QPushButton::clicked, this, &MainWindow::chooseAvatar);
    auto *generateButton = new QPushButton("Generate");
    generateButton->setObjectName("ghostButton");
    generateButton->setCursor(Qt::PointingHandCursor);
    generateButton->setToolTip("Generate a fresh random face avatar");
    connect(generateButton, &QPushButton::clicked, this, [this] {
        const QByteArray png = forkMeshAvatarPng(
            QString::number(QRandomGenerator::global()->generate64()));
        setSettingsAvatar(png);
        onAvatarChosen(png);
    });
    auto *avatarRow = new QHBoxLayout;
    avatarRow->setSpacing(12);
    avatarRow->addWidget(m_settingsAvatarPreview);
    avatarRow->addWidget(uploadButton);
    avatarRow->addWidget(generateButton);
    avatarRow->addStretch();

    // #66: let the node's Solana donation/payout address be set right here in
    // Settings, not only during setup or from the profile panel.
    m_settingsSolanaEdit = new QLineEdit;
    m_settingsSolanaEdit->setMaxLength(64);
    m_settingsSolanaEdit->setPlaceholderText(
        "Solana address (for donations / payouts, optional)");
    m_settingsSolanaEdit->setText(savedSolanaAddress());
    connect(m_settingsSolanaEdit, &QLineEdit::editingFinished, this, [this] {
        const QString addr = m_settingsSolanaEdit->text().trimmed();
        m_settingsSolanaEdit->setText(addr);
        saveSolanaAddress(addr);
        if (m_solanaEdit && m_solanaEdit->text().trimmed() != addr)
            m_solanaEdit->setText(addr);
        // Share with peers on the next connect; refresh the sponsor banner now.
        updateSolanaNotice();
        updateHomeStats();
    });

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setSpacing(8);
    form->addRow("Name", m_settingsNameEdit);
    form->addRow("Solana", m_settingsSolanaEdit);
    m_settingsEmailLabel = new QLabel("Email");
    m_settingsEmailVerifiedBadge = new QLabel;
    m_settingsEmailVerifiedBadge->setObjectName("emailVerifiedBadge");
    m_settingsEmailVerifiedBadge->setAlignment(Qt::AlignCenter);
    m_settingsEmailVerifiedBadge->setSizePolicy(QSizePolicy::Fixed,
                                                QSizePolicy::Fixed);
    form->addRow(m_settingsEmailLabel, m_settingsEmailVerifiedBadge);
    refreshSettingsEmailVerifiedBadge();
    form->addRow("Avatar", avatarRow);

    // #368: identity key backup. The Ed25519 key under the app data dir is the
    // one thing that can't be regenerated — lose it and every signature,
    // catalog record and bounty binding is orphaned. Offer an encrypted export
    // (keyfile + QR) and import, and nag until the user has taken a backup.
    m_identityBackupNag = new QLabel;
    m_identityBackupNag->setObjectName("identityBackupNag");
    m_identityBackupNag->setWordWrap(true);
    m_identityBackupNag->setStyleSheet("color:#f0b429;");
    auto *backUpKeyButton = new QPushButton("Back up identity key…");
    backUpKeyButton->setObjectName("ghostButton");
    backUpKeyButton->setCursor(Qt::PointingHandCursor);
    backUpKeyButton->setToolTip(
        "Export, import, or show a QR of your passphrase-encrypted identity key. "
        "Without a backup, losing this machine loses your ForkMesh identity.");
    connect(backUpKeyButton, &QPushButton::clicked, this,
            &MainWindow::backUpIdentityKey);
    auto *backUpRow = new QHBoxLayout;
    backUpRow->addWidget(backUpKeyButton);
    backUpRow->addStretch();
    form->addRow("Identity key", backUpRow);
    form->addRow("", m_identityBackupNag);
    refreshIdentityBackupNag();

    auto *startupLabel = new QLabel("STARTUP");
    startupLabel->setObjectName("sectionLabel");
    m_autostartCheck = new QCheckBox("Launch ForkMesh at login");
    m_autostartCheck->setChecked(isAutostartEnabled());
    m_autostartCheck->setToolTip(
        "Start ForkMesh automatically when you log in to this computer.");
    connect(m_autostartCheck, &QCheckBox::toggled, this, [](bool enabled) {
        setAutostartEnabled(enabled);
    });

    // Auto-update (adhoc #120): quietly check for a new version and update,
    // rebuild and relaunch when one is found — the same flow as the manual
    // "Update, rebuild & restart" button below, just automatic. Off by default
    // on desktop so a personal machine never relaunches out from under you
    // unannounced; on by default for headless installs (seeded in main.cpp),
    // since an operator-run VM has no one around to click update.
    auto *autoUpdateCheck = new QCheckBox("Automatically update ForkMesh");
    autoUpdateCheck->setChecked(
        QSettings().value(kAutoUpdateSetting, false).toBool());
    autoUpdateCheck->setToolTip(
        "Check for a new version in the background and update, rebuild and "
        "relaunch automatically when one is found. Never interrupts a running "
        "agent — the update waits for it to finish. Off by default; on by "
        "default for headless installs.");
    connect(autoUpdateCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kAutoUpdateSetting, enabled);
    });

    // Default tab a repository opens on. Stored as the repo-detail tab index;
    // defaults to Agents (see defaultRepoTabIndex()).
    auto *defaultTabLabel = new QLabel("Open repositories on tab");
    auto *defaultTabCombo = new QComboBox;
    defaultTabCombo->addItem(QStringLiteral("Code"), 0);
    defaultTabCombo->addItem(QStringLiteral("Commits"), 1);
    defaultTabCombo->addItem(QStringLiteral("Issues"), 2);
    defaultTabCombo->addItem(QStringLiteral("Agents"), 3);
    defaultTabCombo->addItem(QStringLiteral("Pull requests"), 4);
    defaultTabCombo->addItem(QStringLiteral("Discussions"), 5);
    defaultTabCombo->setToolTip(
        "Which tab to show when you open a repository. Defaults to Agents.");
    {
        const int idx = defaultTabCombo->findData(defaultRepoTabIndex());
        defaultTabCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    connect(defaultTabCombo, &QComboBox::currentIndexChanged, this,
            [defaultTabCombo](int) {
                QSettings().setValue(kDefaultRepoTabSetting,
                                     defaultTabCombo->currentData().toInt());
            });

    auto *autoSwitchToAgentCheck =
        new QCheckBox("Switch to Agents tab when a new agent is created");
    autoSwitchToAgentCheck->setChecked(
        QSettings().value(kAutoSwitchToAgentSetting, true).toBool());
    autoSwitchToAgentCheck->setToolTip(
        "When you start an agent from a non-Agents tab, automatically navigate "
        "to the Agents tab and select the new session so you can watch it run.");
    connect(autoSwitchToAgentCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kAutoSwitchToAgentSetting, enabled);
    });

    // Screenshot: an inline calibration target for the region screenshot tool. It
    // shows a square with corner brackets and a centre crosshair right here in
    // Settings — grab it with the screenshot button and confirm the captured
    // pixels line up with the corners and the size shown. The screenshot itself is
    // queued as the next new-agent attachment.
    auto *screenshotLabel = new QLabel("SCREENSHOT");
    screenshotLabel->setObjectName("sectionLabel");
    auto *screenshotHint = new QLabel(
        "Grab this square with the screenshot tool to check the capture lines up "
        "with the corners \xE2\x80\x94 the shot attaches to a new agent.");
    screenshotHint->setObjectName("statusLine");
    screenshotHint->setWordWrap(true);
    auto *alignmentTarget = new ScreenAlignmentTarget;

    auto *notifyLabel = new QLabel("NOTIFICATIONS");
    notifyLabel->setObjectName("sectionLabel");
    auto *pushAlertCheck =
        new QCheckBox("Show a system alert when a push reaches a mirror");
    pushAlertCheck->setChecked(
        QSettings().value(kPushAlertSetting, false).toBool());
    pushAlertCheck->setToolTip(
        "Pop up a desktop notification with the repo, branch and commit "
        "whenever someone pushes to one of this node's mirrors.");
    connect(pushAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kPushAlertSetting, enabled);
    });
    auto *actionAlertCombo = new QComboBox;
    actionAlertCombo->addItem("Action alerts: all runs", QStringLiteral("all"));
    actionAlertCombo->addItem("Action alerts: failures only",
                              QStringLiteral("failed"));
    actionAlertCombo->addItem("Action alerts: off", QStringLiteral("none"));
    actionAlertCombo->setToolTip(
        "Desktop notifications for .forkmesh/ workflows: pop one for every run "
        "(start and finish), only when a run fails, or never. The in-app "
        "Notifications page logs every run regardless.");
    {
        const int idx = actionAlertCombo->findData(actionAlertMode());
        actionAlertCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    connect(actionAlertCombo, &QComboBox::currentIndexChanged, this,
            [actionAlertCombo](int) {
                QSettings().setValue(kActionAlertModeSetting,
                                     actionAlertCombo->currentData().toString());
            });
    auto *nodeConnectAlertCheck =
        new QCheckBox("Show a system alert when a node connects");
    nodeConnectAlertCheck->setChecked(
        QSettings().value(kNodeConnectAlertSetting, false).toBool());
    nodeConnectAlertCheck->setToolTip(
        "Pop up a desktop notification when another node comes online on this "
        "network.");
    connect(nodeConnectAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kNodeConnectAlertSetting, enabled);
    });
    auto *disbursementAlertCheck =
        new QCheckBox("Show a system alert when this node receives a disbursement");
    disbursementAlertCheck->setChecked(
        QSettings().value(kDisbursementAlertSetting, false).toBool());
    disbursementAlertCheck->setToolTip(
        "Pop up a desktop notification when this node's Solana wallet balance "
        "increases after a refresh.");
    connect(disbursementAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kDisbursementAlertSetting, enabled);
    });

    // The remaining alert categories. Each is off by default (notifyEnabled())
    // and re-enabled here, so a fresh install is silent until the user opts in.
    auto alertCheck = [this](const QString &label, const QString &key,
                             const QString &tip) {
        auto *box = new QCheckBox(label);
        box->setChecked(notifyEnabled(key));
        box->setToolTip(tip);
        connect(box, &QCheckBox::toggled, this,
                [key](bool on) { QSettings().setValue(key, on); });
        return box;
    };
    auto *chatMessageAlertCheck = alertCheck(
        "Show a system alert for new chat messages", kChatMessageAlertSetting,
        "Pop up a desktop notification when a chat message arrives while ForkMesh "
        "isn't the active window.");
    auto *mentionAlertCheck = alertCheck(
        "Show a system alert when you're @mentioned", kMentionAlertSetting,
        "Pop up a desktop notification when your node name is mentioned in chat or "
        "in an issue/pull request.");
    auto *issueAlertCheck = alertCheck(
        "Show a system alert for new issues", kIssueAlertSetting,
        "Pop up a desktop notification when another node files an issue on one of "
        "your repositories.");
    auto *pullAlertCheck = alertCheck(
        "Show a system alert for new pull requests", kPullAlertSetting,
        "Pop up a desktop notification when another node opens a pull request on "
        "one of your repositories.");
    auto *commentAlertCheck = alertCheck(
        "Show a system alert for new issue comments", kCommentAlertSetting,
        "Pop up a desktop notification when someone comments on one of your "
        "issues.");
    auto *mirrorUpdateAlertCheck = alertCheck(
        "Show a system alert when a mirror updates", kMirrorUpdateAlertSetting,
        "Pop up a desktop notification when a peer refreshes the mirror of a repo "
        "you also mirror.");
    auto *coveOpenAlertCheck = alertCheck(
        "Show a system alert when a cove is opened", kCoveOpenAlertSetting,
        "Pop up a desktop notification when someone opens an encrypted cove you "
        "created with notifications enabled.");
    auto *newUserAlertCheck = alertCheck(
        "Show a system alert when a new user joins", kNewUserAlertSetting,
        "Admin: pop up a desktop notification when a new user signs up and needs "
        "email verification.");

    auto *ideLabel = new QLabel("IDE INTEGRATION");
    ideLabel->setObjectName("sectionLabel");
    auto *ideIntegrationCheck = new QCheckBox(
        "Run issues in my IDE (VS Code / Codeium) with Claude Code or Codex");
    ideIntegrationCheck->setChecked(
        QSettings().value(kIdeIntegrationSetting, false).toBool());
    ideIntegrationCheck->setToolTip(
        "When the ForkMesh IDE extension is installed and running, each issue "
        "gets buttons to start the task with Claude Code or Codex in the IDE.");
    auto *ideStatus = new QLabel(this);
    ideStatus->setObjectName("statusLine");
    ideStatus->setWordWrap(true);
    // Refresh the detect-status line; reused on toggle and on a short poll so the
    // user sees the extension appear without reopening Settings.
    auto refreshIdeStatus = [this, ideStatus, ideIntegrationCheck] {
        if (!ideIntegrationCheck->isChecked()) {
            ideStatus->setText(
                "Off. Install the extension from the ide_extension/ folder, then "
                "enable this to connect.");
            return;
        }
        QString ideName;
        if (ideExtensionActive(&ideName))
            ideStatus->setText(QString::fromUtf8("\xE2\x97\x8F Connected to %1.")
                                   .arg(ideName));
        else
            ideStatus->setText(
                "Waiting for the IDE extension\xE2\x80\xA6 open your IDE with the "
                "ForkMesh extension installed.");
    };
    connect(ideIntegrationCheck, &QCheckBox::toggled, this,
            [this, refreshIdeStatus](bool enabled) {
                QSettings().setValue(kIdeIntegrationSetting, enabled);
                refreshIdeStatus();
                updateIssueIdeButtons();
            });
    // Poll while Settings is open so detection (and the issue buttons) update
    // live as the IDE/extension comes and goes.
    auto *idePoll = new QTimer(ideStatus);
    idePoll->setInterval(5000);
    connect(idePoll, &QTimer::timeout, this, [this, refreshIdeStatus] {
        refreshIdeStatus();
        updateIssueIdeButtons();
    });
    idePoll->start();
    refreshIdeStatus();

    // Voice input: download & build whisper.cpp for local, offline speech-to-text
    // so the prompt box can be dictated. Once installed a mic appears beside the
    // prompt (see updateVoiceInputButton()).
    auto *voiceLabel = new QLabel("VOICE INPUT");
    voiceLabel->setObjectName("sectionLabel");
    auto *voiceHint = new QLabel(
        "Install a local speech-to-text engine to speak your prompts. Everything "
        "runs on this machine \xE2\x80\x94 no audio leaves your computer. Whisper "
        "(whisper.cpp) builds with git, cmake and a C++ compiler; Parakeet (NVIDIA) "
        "uses a Python env (needs python3). Once installed a click-to-record mic "
        "appears next to the prompt box.");
    voiceHint->setObjectName("statusLine");
    voiceHint->setWordWrap(true);
    // Engine selector: whisper.cpp (default) or NVIDIA Parakeet. The model combo
    // beside it swaps to match the chosen engine.
    m_voiceEngineCombo = new QComboBox;
    m_voiceEngineCombo->addItem(QStringLiteral("Whisper (whisper.cpp)"),
                                QStringLiteral("whisper"));
    m_voiceEngineCombo->addItem(QStringLiteral("Parakeet (NVIDIA)"),
                                QStringLiteral("parakeet"));
    m_voiceEngineCombo->setToolTip(
        "Which speech-to-text engine transcribes your dictation. Both run locally; "
        "Parakeet can be more accurate but installs a Python environment.");
    {
        const int ei = m_voiceEngineCombo->findData(voiceEngine());
        m_voiceEngineCombo->setCurrentIndex(ei >= 0 ? ei : 0);
    }
    connect(m_voiceEngineCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
                QSettings().setValue(kVoiceEngineSetting,
                                     m_voiceEngineCombo->currentData().toString());
                refreshWhisperStatus();
                updateVoiceInputButton();
            });
    m_parakeetModelCombo = new QComboBox;
    m_parakeetModelCombo->addItem(QStringLiteral("Parakeet TDT 0.6B v2 (English)"),
                                  QStringLiteral("parakeet-tdt-0.6b-v2"));
    m_parakeetModelCombo->addItem(
        QStringLiteral("Parakeet TDT 0.6B v3 (multilingual)"),
        QStringLiteral("parakeet-tdt-0.6b-v3"));
    m_parakeetModelCombo->setToolTip(
        "Which Parakeet checkpoint to download for transcription.");
    {
        const int mi = m_parakeetModelCombo->findData(parakeetModelName());
        m_parakeetModelCombo->setCurrentIndex(mi >= 0 ? mi : 0);
    }
    connect(m_parakeetModelCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
                QSettings().setValue(kParakeetModelSetting,
                                     m_parakeetModelCombo->currentData().toString());
            });
    m_whisperModelCombo = new QComboBox;
    m_whisperModelCombo->addItem(QStringLiteral("Tiny (fastest, ~75 MB)"),
                                 QStringLiteral("tiny.en"));
    m_whisperModelCombo->addItem(QStringLiteral("Base (recommended, ~142 MB)"),
                                 QStringLiteral("base.en"));
    m_whisperModelCombo->addItem(QStringLiteral("Small (most accurate, ~466 MB)"),
                                 QStringLiteral("small.en"));
    m_whisperModelCombo->setToolTip(
        "Which Whisper model to download. Larger models are more accurate but "
        "slower to transcribe.");
    {
        const int mi = m_whisperModelCombo->findData(whisperModelName());
        m_whisperModelCombo->setCurrentIndex(mi >= 0 ? mi : 1);
    }
    m_whisperInstallButton = new QPushButton;
    m_whisperInstallButton->setObjectName("ghostButton");
    m_whisperInstallButton->setCursor(Qt::PointingHandCursor);
    connect(m_whisperInstallButton, &QPushButton::clicked, this,
            &MainWindow::installVoiceEngine);
    m_whisperStatusLabel = new QLabel(this);
    m_whisperStatusLabel->setObjectName("statusLine");
    m_whisperStatusLabel->setWordWrap(true);
    auto *voiceRow = new QHBoxLayout;
    voiceRow->setSpacing(8);
    voiceRow->addWidget(m_voiceEngineCombo);
    voiceRow->addWidget(m_whisperModelCombo);
    voiceRow->addWidget(m_parakeetModelCombo);
    voiceRow->addWidget(m_whisperInstallButton);
    voiceRow->addStretch();
    refreshWhisperStatus(); // toggles which model combo is shown

    // Microphone picker (adhoc #10): choose which input device the recorder
    // captures from. Populated from the installed recorder's device list; the
    // first entry is the system default. Persisted to kVoiceInputDeviceSetting,
    // which audioRecorderFor() reads when it starts a capture.
    m_voiceDeviceCombo = new QComboBox;
    m_voiceDeviceCombo->setToolTip(
        "Microphone the recorder captures from. \"System default\" follows your "
        "OS audio settings.");
    {
        const QString saved =
            QSettings().value(kVoiceInputDeviceSetting).toString();
        const auto devices = voiceInputDevices();
        for (const auto &d : devices)
            m_voiceDeviceCombo->addItem(d.first, d.second);
        const int di = m_voiceDeviceCombo->findData(saved);
        m_voiceDeviceCombo->setCurrentIndex(di >= 0 ? di : 0);
    }
    connect(m_voiceDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                QSettings().setValue(kVoiceInputDeviceSetting,
                                     m_voiceDeviceCombo->currentData().toString());
            });
    auto *voiceDeviceRow = new QHBoxLayout;
    voiceDeviceRow->setSpacing(8);
    auto *micLabel = new QLabel("Microphone");
    micLabel->setObjectName("statusLine");
    voiceDeviceRow->addWidget(micLabel);
    voiceDeviceRow->addWidget(m_voiceDeviceCombo, 1);
    voiceDeviceRow->addStretch();

    // Test-mic check (adhoc #14): record from the chosen device and show a live
    // level bar, so you can confirm the mic is actually captured (and watch the
    // level move as you speak) without going through whisper transcription. Works
    // even before whisper.cpp is installed — it only needs a recorder.
    m_voiceTestMicButton = new QPushButton("Test mic");
    m_voiceTestMicButton->setObjectName("ghostButton");
    m_voiceTestMicButton->setCursor(Qt::PointingHandCursor);
    m_voiceTestMicButton->setToolTip(
        "Record from the selected microphone and show its input level so you can "
        "confirm it's working. Click again to stop.");
    connect(m_voiceTestMicButton, &QPushButton::clicked, this,
            &MainWindow::toggleMicTest);
    m_voiceTestMeter = new QProgressBar;
    m_voiceTestMeter->setObjectName("voiceLevelMeter");
    m_voiceTestMeter->setRange(0, 100);
    m_voiceTestMeter->setValue(0);
    m_voiceTestMeter->setTextVisible(false);
    m_voiceTestMeter->setFixedHeight(14);
    m_voiceTestMeter->setToolTip("Live microphone input level");
    m_voiceTestMeter->setStyleSheet(
        "QProgressBar#voiceLevelMeter{border:1px solid #30363d;border-radius:3px;"
        "background:#0d1117;}"
        "QProgressBar#voiceLevelMeter::chunk{background:#3fb950;border-radius:2px;}");
    auto *voiceTestRow = new QHBoxLayout;
    voiceTestRow->setSpacing(8);
    voiceTestRow->addWidget(m_voiceTestMicButton);
    voiceTestRow->addWidget(m_voiceTestMeter, 1);

    auto *appearanceLabel = new QLabel("APPEARANCE");
    appearanceLabel->setObjectName("sectionLabel");
    m_themeCombo = new QComboBox;
    m_themeCombo->addItem("Follow system", "system");
    m_themeCombo->addItem("Dark", "dark");
    m_themeCombo->addItem("Light", "light");
    m_themeCombo->setToolTip("Choose the color theme, or follow the OS setting.");
    {
        const QString pref = QSettings().value(kThemeSetting, "system").toString();
        const int idx = m_themeCombo->findData(pref);
        m_themeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        QSettings().setValue(kThemeSetting, m_themeCombo->currentData().toString());
        applyTheme();
    });
    auto *showCurrencyCombo = new QComboBox;
    showCurrencyCombo->addItem("Show balance in SOL", QStringLiteral("sol"));
    showCurrencyCombo->addItem("Show balance in USD", QStringLiteral("usd"));
    showCurrencyCombo->addItem("Show balance in INR (\xE2\x82\xB9)",
                               QStringLiteral("inr"));
    showCurrencyCombo->setToolTip(
        "Currency for this node's top-bar balance (live SOL price for USD/INR). "
        "Also switchable by clicking the balance in the top bar.");
    {
        const int idx = showCurrencyCombo->findData(solanaDisplayCurrency());
        showCurrencyCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    connect(showCurrencyCombo, &QComboBox::currentIndexChanged, this, [this,
            showCurrencyCombo](int) {
        QSettings().setValue(kSolanaDisplayCurrencySetting,
                             showCurrencyCombo->currentData().toString());
        updateNavSolanaBalance();
    });
    auto *rebuildButtonCheck =
        new QCheckBox("Show a rebuild & restart button in the top bar");
    rebuildButtonCheck->setChecked(
        QSettings().value(kShowRebuildButtonSetting, false).toBool());
    rebuildButtonCheck->setToolTip(
        "Adds a small rebuild & restart button beside Leaderboards (under the "
        "avatar) for a fast local rebuild and relaunch. Off by default.");
    connect(rebuildButtonCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue(kShowRebuildButtonSetting, enabled);
        updateNavRebuildButton();
    });

    // Diagnostic: mirror every HTTP request the app makes into the network log
    // (verb + status + URL). Off by default; handy for confirming what the
    // background traffic actually is (adhoc #74).
    auto *verboseNetLogCheck =
        new QCheckBox("Log every network request in the network log");
    verboseNetLogCheck->setChecked(
        QSettings().value(kVerboseNetworkLogSetting, false).toBool());
    verboseNetLogCheck->setToolTip(
        "Record every HTTP request the app makes — method, status code and URL "
        "— in the network log, so you can see exactly what traffic is going "
        "out. Verbose; off by default.");
    connect(verboseNetLogCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kVerboseNetworkLogSetting, enabled);
    });

    // Per-PR bounties (issue #347): reward every merged pull request's author
    // with a fixed bounty, funded either per-merge (a QR) or from a pre-funded
    // inbuilt wallet. Off by default.
    auto *bountyLabel = new QLabel("PR BOUNTIES");
    bountyLabel->setObjectName("sectionLabel");
    auto *autoBountyCheck =
        new QCheckBox("Reward every merged pull request");
    autoBountyCheck->setChecked(
        QSettings().value(kAutoPrBountyEnabledSetting, false).toBool());
    autoBountyCheck->setToolTip(
        "When on, merging a pull request you own rewards its author with the "
        "bounty below. Off by default.");
    auto *bountyHint = new QLabel(
        "Only applies to repositories you own. Amounts reuse the SOL-priced "
        "bounty flow (minimum $1).");
    bountyHint->setObjectName("modeHint");
    bountyHint->setWordWrap(true);

    auto *bountyAmount = new QDoubleSpinBox;
    bountyAmount->setRange(1.0, 100000.0);
    bountyAmount->setDecimals(2);
    bountyAmount->setPrefix("$");
    bountyAmount->setValue(
        QSettings().value(kAutoPrBountyAmountSetting, 1.0).toDouble());
    bountyAmount->setToolTip(
        "Reward paid to each merged pull request's author (USD, priced to SOL "
        "at payout).");
    auto *bountyAmountLabel = new QLabel("Reward per PR");
    bountyAmountLabel->setObjectName("statusLine");
    auto *bountyAmountRow = new QHBoxLayout;
    bountyAmountRow->setSpacing(8);
    bountyAmountRow->addWidget(bountyAmountLabel);
    bountyAmountRow->addWidget(bountyAmount);
    bountyAmountRow->addStretch();

    auto *bountyModeCombo = new QComboBox;
    bountyModeCombo->addItem("Pay per PR (funding QR at each merge)",
                             QStringLiteral("perPr"));
    bountyModeCombo->addItem("Pay from the inbuilt bounty wallet",
                             QStringLiteral("wallet"));
    {
        const QString mode =
            QSettings().value(kAutoPrBountyModeSetting,
                              QStringLiteral("perPr")).toString();
        const int mi = bountyModeCombo->findData(mode);
        bountyModeCombo->setCurrentIndex(mi < 0 ? 0 : mi);
    }
    bountyModeCombo->setToolTip(
        "\"Pay per PR\" shows a funding QR each time you merge. \"Inbuilt wallet\" "
        "pays automatically from a wallet you pre-fund with a little SOL.");
    auto *fundWalletBtn = new QPushButton("Fund inbuilt wallet…");
    fundWalletBtn->setObjectName("ghostButton");
    fundWalletBtn->setCursor(Qt::PointingHandCursor);
    fundWalletBtn->setToolTip(
        "Show the inbuilt bounty wallet's deposit address and balance so you can "
        "top it up with SOL.");
    auto *bountyModeRow = new QHBoxLayout;
    bountyModeRow->setSpacing(8);
    bountyModeRow->addWidget(bountyModeCombo);
    bountyModeRow->addWidget(fundWalletBtn);
    bountyModeRow->addStretch();

    const auto syncBountyEnabled = [autoBountyCheck, bountyAmount,
                                    bountyModeCombo, fundWalletBtn] {
        const bool on = autoBountyCheck->isChecked();
        bountyAmount->setEnabled(on);
        bountyModeCombo->setEnabled(on);
        fundWalletBtn->setEnabled(
            on && bountyModeCombo->currentData().toString() ==
                      QLatin1String("wallet"));
    };
    connect(autoBountyCheck, &QCheckBox::toggled, this,
            [this, syncBountyEnabled](bool enabled) {
                QSettings().setValue(kAutoPrBountyEnabledSetting, enabled);
                syncBountyEnabled();
            });
    connect(bountyAmount, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [](double v) {
                QSettings().setValue(kAutoPrBountyAmountSetting, v);
            });
    connect(bountyModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, bountyModeCombo, syncBountyEnabled](int) {
                QSettings().setValue(kAutoPrBountyModeSetting,
                                     bountyModeCombo->currentData().toString());
                syncBountyEnabled();
            });
    connect(fundWalletBtn, &QPushButton::clicked, this,
            &MainWindow::showBountyWalletDialog);
    syncBountyEnabled();

    // Per-metric toggles for this node's host stats (CPU/RAM/disk) shown in the
    // Mirror nodes view. Off by default on the desktop so a personal machine
    // doesn't broadcast its load; headless installs are seeded on at startup so
    // hosts stay monitorable (adhoc #23). Re-sampled within ~10s of a change.
    auto *nodeStatsLabel = new QLabel("NODE STATS");
    nodeStatsLabel->setObjectName("sectionLabel");
    auto *nodeStatsHint = new QLabel(
        "Show this node's resource use to other nodes in the Mirror nodes view. "
        "Off by default; servers installed from the Hosts tab report all three.");
    nodeStatsHint->setObjectName("statusLine");
    nodeStatsHint->setWordWrap(true);
    struct NodeStatToggle {
        const char *label;
        const QString &key;
    };
    const NodeStatToggle nodeStatToggles[] = {
        {"Report CPU usage", TelemetrySettings::kReportCpu},
        {"Report memory usage", TelemetrySettings::kReportMemory},
        {"Report disk usage", TelemetrySettings::kReportDisk},
    };
    QList<QCheckBox *> nodeStatChecks;
    for (const NodeStatToggle &toggle : nodeStatToggles) {
        auto *check = new QCheckBox(QString::fromUtf8(toggle.label));
        check->setChecked(QSettings().value(toggle.key, false).toBool());
        const QString key = toggle.key;
        connect(check, &QCheckBox::toggled, this, [key](bool enabled) {
            QSettings().setValue(key, enabled);
        });
        nodeStatChecks.append(check);
    }

    // Opt-in crash/stall telemetry (issue #354). OFF by default: when on, the
    // previous session's crash summary and UI-stall records are uploaded to the
    // mainnode on startup so bugs reach a triage queue instead of dying in a
    // local log. Only the app version, OS, and an anonymized node hash are sent;
    // repo names and filesystem paths are scrubbed out client-side first.
    auto *telemetryCheck =
        new QCheckBox("Upload crash & UI-stall reports to help fix bugs");
    telemetryCheck->setChecked(
        QSettings().value(kUploadTelemetrySetting, false).toBool());
    telemetryCheck->setToolTip(
        "On startup, send the previous session's crash summary and UI-stall "
        "records to the mainnode so they reach a triage queue. Only the app "
        "version, OS, and an anonymized node hash go with them; repo names and "
        "file paths are scrubbed out first. Off by default.");
    connect(telemetryCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kUploadTelemetrySetting, enabled);
    });

    auto *agentsLabel = new QLabel("AGENTS");
    agentsLabel->setObjectName("sectionLabel");
    auto *agentsHint = new QLabel(
        "Stored locally. Command templates run in a temporary worktree with "
        "{promptFile}, {modelArg}, {contextWindow}, and {maxOutputTokens} available.");
    agentsHint->setObjectName("statusLine");
    agentsHint->setWordWrap(true);

    // Default agent: which provider the quick-add bar and issue-detail "Assign
    // agent" picker start on. Stored as the canonical provider id so the pickers
    // (built elsewhere) can seed themselves via selectDefaultAgentProvider().
    m_defaultAgentProviderCombo = new QComboBox;
    m_defaultAgentProviderCombo->addItem(QStringLiteral("OpenAI API"),
                                         QStringLiteral("openai"));
    m_defaultAgentProviderCombo->addItem(QStringLiteral("Claude API"),
                                         QStringLiteral("claude-api"));
    m_defaultAgentProviderCombo->addItem(QStringLiteral("Claude Code"),
                                         QStringLiteral("claude-code"));
    selectDefaultAgentProvider(m_defaultAgentProviderCombo);
    m_defaultAgentProviderCombo->setToolTip(
        "Provider pre-selected when you assign a coding agent to an issue.");
    connect(m_defaultAgentProviderCombo, &QComboBox::currentIndexChanged, this,
            [this] {
                const QString provider =
                    m_defaultAgentProviderCombo->currentData().toString();
                QSettings().setValue(kDefaultAgentProviderSetting, provider);
                // Keep the live pickers in step with the new default.
                selectDefaultAgentProvider(m_quickAddAgentProvider);
                selectDefaultAgentProvider(m_issueAgentProvider);
                selectDefaultAgentProvider(m_issuePrioritizeAgentCombo);
            });

    // When the watchdog catches the GUI thread freezing, hand the captured
    // backtrace to a coding agent so the freeze gets fixed without anyone filing
    // it by hand. On by default (adhoc #205).
    auto *autoStallAgentCheck =
        new QCheckBox("Auto-create an agent task to fix new UI stalls");
    autoStallAgentCheck->setChecked(
        QSettings().value(kAutoAgentOnStallSetting, true).toBool());
    autoStallAgentCheck->setToolTip(
        "When the app detects the GUI thread freezing, start a coding agent on "
        "the captured backtrace to fix the stall. Uses the default agent above. "
        "On by default; de-duped so one recurring freeze files a single task.");
    connect(autoStallAgentCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kAutoAgentOnStallSetting, enabled);
    });

    // When an idle agent's branch would conflict with base — the same check
    // that shows the "Fix conflicts with agent" button — automatically ask the
    // agent to merge and resolve it instead of waiting for a manual click.
    // On by default.
    auto *autoFixConflictsCheck =
        new QCheckBox("Auto-fix agent branch conflicts");
    autoFixConflictsCheck->setChecked(
        QSettings().value(kAutoFixAgentConflictsSetting, true).toBool());
    autoFixConflictsCheck->setToolTip(
        "When an idle agent's branch conflicts with the base branch, "
        "automatically ask the agent to merge and resolve the conflicts "
        "(the same action as the \"Fix conflicts with agent\" button). "
        "On by default; only attempted once per detected conflict.");
    connect(autoFixConflictsCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kAutoFixAgentConflictsSetting, enabled);
    });

    m_codexApiKeyEdit = new QLineEdit;
    m_codexApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_codexApiKeyEdit->setPlaceholderText("OPENAI_API_KEY");
    m_codexApiKeyEdit->setText(QSettings().value(kCodexApiKeySetting).toString().trimmed());
    connect(m_codexApiKeyEdit, &QLineEdit::editingFinished, this, [this] {
        const QString key = m_codexApiKeyEdit->text().trimmed();
        m_codexApiKeyEdit->setText(key);
        QSettings().setValue(kCodexApiKeySetting, key);
    });

    m_openAiAdminKeyEdit = new QLineEdit;
    m_openAiAdminKeyEdit->setEchoMode(QLineEdit::Password);
    m_openAiAdminKeyEdit->setPlaceholderText("Optional Admin API key for usage and costs");
    m_openAiAdminKeyEdit->setText(
        QSettings().value(kOpenAiAdminKeySetting).toString().trimmed());
    connect(m_openAiAdminKeyEdit, &QLineEdit::editingFinished, this, [this] {
        const QString key = m_openAiAdminKeyEdit->text().trimmed();
        m_openAiAdminKeyEdit->setText(key);
        QSettings().setValue(kOpenAiAdminKeySetting, key);
    });

    m_codexModelEdit = new QLineEdit;
    m_codexModelEdit->setPlaceholderText("Optional, e.g. gpt-5.1-codex");
    m_codexModelEdit->setText(QSettings().value(kCodexModelSetting).toString().trimmed());
    connect(m_codexModelEdit, &QLineEdit::editingFinished, this, [this] {
        const QString model = m_codexModelEdit->text().trimmed();
        m_codexModelEdit->setText(model);
        QSettings().setValue(kCodexModelSetting, model);
    });

    m_claudeApiKeyEdit = new QLineEdit;
    m_claudeApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_claudeApiKeyEdit->setPlaceholderText("ANTHROPIC_API_KEY");
    m_claudeApiKeyEdit->setText(QSettings().value(kClaudeApiKeySetting).toString().trimmed());
    connect(m_claudeApiKeyEdit, &QLineEdit::editingFinished, this, [this] {
        const QString key = m_claudeApiKeyEdit->text().trimmed();
        m_claudeApiKeyEdit->setText(key);
        QSettings().setValue(kClaudeApiKeySetting, key);
    });

    m_claudeAdminKeyEdit = new QLineEdit;
    m_claudeAdminKeyEdit->setEchoMode(QLineEdit::Password);
    m_claudeAdminKeyEdit->setPlaceholderText("sk-ant-admin01-... (for usage and costs)");
    m_claudeAdminKeyEdit->setText(
        QSettings().value(kClaudeAdminKeySetting).toString().trimmed());
    connect(m_claudeAdminKeyEdit, &QLineEdit::editingFinished, this, [this] {
        const QString key = m_claudeAdminKeyEdit->text().trimmed();
        m_claudeAdminKeyEdit->setText(key);
        QSettings().setValue(kClaudeAdminKeySetting, key);
    });

    m_codexCommandEdit = new QLineEdit;
    m_codexCommandEdit->setText(codexCommandSetting());
    connect(m_codexCommandEdit, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(kCodexCommandSetting, m_codexCommandEdit->text());
    });

    m_claudeCommandEdit = new QLineEdit;
    m_claudeCommandEdit->setText(claudeCommandSetting());
    connect(m_claudeCommandEdit, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(kClaudeCommandSetting, m_claudeCommandEdit->text());
    });

    m_agentContextEdit = new QLineEdit;
    m_agentContextEdit->setPlaceholderText("32000");
    m_agentContextEdit->setText(
        QSettings().value(kAgentContextSetting, 32000).toString());
    connect(m_agentContextEdit, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(kAgentContextSetting,
                             qMax(1000, m_agentContextEdit->text().toInt()));
    });

    m_agentMaxOutputEdit = new QLineEdit;
    m_agentMaxOutputEdit->setPlaceholderText("2000");
    m_agentMaxOutputEdit->setText(
        QSettings().value(kAgentMaxOutputSetting, 2000).toString());
    connect(m_agentMaxOutputEdit, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(kAgentMaxOutputSetting,
                             qMax(256, m_agentMaxOutputEdit->text().toInt()));
    });

    // Instruction preamble prepended to every agent prompt (the prompt that
    // drives Claude and the other providers). Stored verbatim; clearing the box
    // restores the built-in default on the next run.
    m_agentPromptPreambleEdit = new QPlainTextEdit;
    m_agentPromptPreambleEdit->setPlainText(agentPromptPreamble());
    m_agentPromptPreambleEdit->setMaximumHeight(140);
    m_agentPromptPreambleEdit->setPlaceholderText(
        "Instructions prepended to the agent prompt. Leave empty to use the "
        "built-in default.");
    m_agentPromptPreambleEdit->setToolTip(
        "Editable instruction preamble sent ahead of each issue's prompt to the "
        "Claude (and other) coding agents. Clear it to fall back to the default.");
    connect(m_agentPromptPreambleEdit, &QPlainTextEdit::textChanged, this, [this] {
        QSettings().setValue(kAgentPromptPreambleSetting,
                             m_agentPromptPreambleEdit->toPlainText());
    });

    // Instruction used by the Issues "Prioritize from README" button (issue
    // #286). The README and the open-issue list are appended after this text, so
    // it only governs how the default agent is asked to rank the backlog.
    // Clearing the box restores the built-in default on the next run.
    m_prioritizePromptEdit = new QPlainTextEdit;
    m_prioritizePromptEdit->setPlainText(prioritizePromptSetting());
    m_prioritizePromptEdit->setMaximumHeight(140);
    m_prioritizePromptEdit->setPlaceholderText(
        "Instruction for the issues 'Prioritize from README' button. Leave empty "
        "to use the built-in default.");
    m_prioritizePromptEdit->setToolTip(
        "Sent to the default agent (with the README and open issues appended) "
        "when you click 'Prioritize from README' on the Issues page. Clear it to "
        "fall back to the default.");
    connect(m_prioritizePromptEdit, &QPlainTextEdit::textChanged, this, [this] {
        QSettings().setValue(kPrioritizePromptSetting,
                             m_prioritizePromptEdit->toPlainText());
    });

    auto *agentForm = new QFormLayout;
    agentForm->setLabelAlignment(Qt::AlignLeft);
    agentForm->setSpacing(8);
    agentForm->addRow("Default agent", m_defaultAgentProviderCombo);
    agentForm->addRow("OpenAI API key", m_codexApiKeyEdit);
    agentForm->addRow("OpenAI Admin key", m_openAiAdminKeyEdit);
    agentForm->addRow("OpenAI model", m_codexModelEdit);
    agentForm->addRow("Claude API key", m_claudeApiKeyEdit);
    agentForm->addRow("Claude Admin key", m_claudeAdminKeyEdit);
    agentForm->addRow("OpenAI command", m_codexCommandEdit);
    agentForm->addRow("Claude command", m_claudeCommandEdit);
    agentForm->addRow("Context window", m_agentContextEdit);
    agentForm->addRow("Max output", m_agentMaxOutputEdit);
    agentForm->addRow("Agent prompt", m_agentPromptPreambleEdit);
    agentForm->addRow("Prioritize prompt", m_prioritizePromptEdit);

    // Agent API spend/usage stats: moved out of the Agent sessions list (adhoc
    // #118) so that tab only shows the session table and its toolbar.
    auto *usageLabel = new QLabel("USAGE & SPEND");
    usageLabel->setObjectName("sectionLabel");
    auto *usageHint = new QLabel(
        "Issue-assigned local OpenAI API and Claude API runs. Usage is estimated "
        "from prompt and transcript size.");
    usageHint->setObjectName("statusLine");
    usageHint->setWordWrap(true);
    m_agentOpenAiSpend = new QLabel("OpenAI spend this month: not yet refreshed");
    m_agentOpenAiSpend->setObjectName("channelTitle");
    m_agentOpenAiSpend->setWordWrap(true);
    m_agentOpenAiSpend->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_agentOpenAiCredit = new QLabel("OpenAI remaining credits: not yet refreshed");
    m_agentOpenAiCredit->setObjectName("statusLine");
    m_agentOpenAiCredit->setWordWrap(true);
    m_agentOpenAiCredit->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_agentApiKeyStatus = new QLabel("OpenAI usage refreshes automatically after each OpenAI API session.");
    m_agentApiKeyStatus->setObjectName("statusLine");
    m_agentApiKeyStatus->setWordWrap(true);
    m_agentApiKeyStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_agentClaudeSpend = new QLabel("Claude spend this month: not yet refreshed");
    m_agentClaudeSpend->setObjectName("channelTitle");
    m_agentClaudeSpend->setWordWrap(true);
    m_agentClaudeSpend->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_agentClaudeCredit = new QLabel("Claude remaining credits: not yet refreshed");
    m_agentClaudeCredit->setObjectName("statusLine");
    m_agentClaudeCredit->setWordWrap(true);
    m_agentClaudeCredit->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_agentClaudeStatus = new QLabel("Claude usage refreshes automatically after each Claude API session.");
    m_agentClaudeStatus->setObjectName("statusLine");
    m_agentClaudeStatus->setWordWrap(true);
    m_agentClaudeStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto makeStatsRefreshButton = [](const QString &toolTip) {
        auto *button = new QPushButton("Refresh");
        button->setObjectName("ghostButton");
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(toolTip);
        setOcticon(button, "sync", 16);
        return button;
    };
    m_agentTestApiKeyButton =
        makeStatsRefreshButton("Refresh OpenAI usage and spend");
    connect(m_agentTestApiKeyButton, &QPushButton::clicked, this,
            &MainWindow::testOpenAiAgentKey);
    addRefreshSpin(m_agentTestApiKeyButton);
    auto *claudeRefreshButton =
        makeStatsRefreshButton("Refresh Claude usage and spend");
    connect(claudeRefreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshClaudeSpend);
    addRefreshSpin(claudeRefreshButton);
    auto *openAiStatsRow = new QHBoxLayout;
    openAiStatsRow->setContentsMargins(0, 0, 0, 0);
    openAiStatsRow->setSpacing(8);
    openAiStatsRow->addWidget(m_agentOpenAiSpend, 1);
    openAiStatsRow->addWidget(m_agentTestApiKeyButton, 0, Qt::AlignTop);
    auto *claudeStatsRow = new QHBoxLayout;
    claudeStatsRow->setContentsMargins(0, 0, 0, 0);
    claudeStatsRow->setSpacing(8);
    claudeStatsRow->addWidget(m_agentClaudeSpend, 1);
    claudeStatsRow->addWidget(claudeRefreshButton, 0, Qt::AlignTop);
    m_agentTotalSpend = new QLabel("Total Agent API spend this month: not yet refreshed");
    m_agentTotalSpend->setObjectName("channelTitle");
    m_agentTotalSpend->setWordWrap(true);
    m_agentTotalSpend->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *usageText = new QVBoxLayout;
    usageText->setContentsMargins(0, 0, 0, 0);
    usageText->setSpacing(4);
    usageText->addLayout(openAiStatsRow);
    usageText->addWidget(m_agentOpenAiCredit);
    usageText->addWidget(m_agentApiKeyStatus);
    usageText->addLayout(claudeStatsRow);
    usageText->addWidget(m_agentClaudeCredit);
    usageText->addWidget(m_agentClaudeStatus);
    usageText->addWidget(m_agentTotalSpend);
    m_agentLimitsLabel = new QLabel;
    m_agentLimitsLabel->setObjectName("statusLine");
    m_agentLimitsLabel->setWordWrap(true);
    m_agentLimitsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    usageText->addWidget(m_agentLimitsLabel);

    // Issue #346: a headless node has no one watching its screen, so the only
    // way to know Claude Code can resume after running out is to be told.
    auto *emailOnRefillCheck =
        new QCheckBox("Email me when Claude Code credits refill after running out");
    emailOnRefillCheck->setChecked(
        QSettings().value(kEmailOnCreditsRefillSetting, false).toBool());
    emailOnRefillCheck->setToolTip(
        "When this node's 5-hour or weekly Claude Code usage window was maxed "
        "out and then resets, email the account on file (requires a verified "
        "email — see the profile section above).");
    connect(emailOnRefillCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kEmailOnCreditsRefillSetting, enabled);
    });
    usageText->addWidget(emailOnRefillCheck);
    // Issue #115: restore the last-known spend figures immediately so they are
    // visible on restart before any network refresh completes.
    applyCachedSpendLabels();
    refreshAgentLimitLabel();
    // Tick once a minute so the countdowns stay current while the tab is open.
    m_agentLimitsTimer = new QTimer(this);
    m_agentLimitsTimer->setInterval(60 * 1000);
    connect(m_agentLimitsTimer, &QTimer::timeout, this,
            &MainWindow::refreshAgentLimitLabel);
    m_agentLimitsTimer->start();

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

    auto *previewCacheLabel = new QLabel("PREVIEW CACHE");
    previewCacheLabel->setObjectName("sectionLabel");
    m_previewCacheRootEdit = new QLineEdit(repositoryPreviewRoot());
    m_previewCacheRootEdit->setReadOnly(true);
    m_previewCacheRootEdit->setToolTip(
        "Folder where temporary browse-only mirrors are stored before you "
        "choose to mirror or fork a repository.");
    auto *previewCacheChangeButton = new QPushButton("Change\xE2\x80\xA6");
    previewCacheChangeButton->setObjectName("ghostButton");
    previewCacheChangeButton->setCursor(Qt::PointingHandCursor);
    connect(previewCacheChangeButton, &QPushButton::clicked, this,
            &MainWindow::changePreviewCacheLocation);
    auto *previewCacheRow = new QHBoxLayout;
    previewCacheRow->setContentsMargins(0, 0, 0, 0);
    previewCacheRow->addWidget(m_previewCacheRootEdit, 1);
    previewCacheRow->addWidget(previewCacheChangeButton);

    // Auto-sync incoming issues (issue #193): when on, the periodic inbox poll
    // merges and commits issues filed on this node's repos as they arrive — but
    // only while the working tree is clean, so it never interleaves issue
    // commits with the owner's in-progress edits.
    auto *issuesSyncLabel = new QLabel("ISSUES");
    issuesSyncLabel->setObjectName("sectionLabel");
    auto *autoSyncIssuesCheck =
        new QCheckBox("Auto-sync new issues when the working tree is clean");
    autoSyncIssuesCheck->setChecked(
        QSettings().value(kAutoSyncIssuesSetting, true).toBool());
    autoSyncIssuesCheck->setToolTip(
        "Automatically merge and commit issues filed on your repositories as "
        "they arrive in the inbox — but only while the repo's working tree has "
        "no uncommitted changes, so issue commits never land on top of work in "
        "progress. When off, or while the tree is dirty, incoming issues wait "
        "in the inbox until you click \"Sync inbox\".");
    connect(autoSyncIssuesCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kAutoSyncIssuesSetting, enabled);
    });

    // Start a repository under this node: either spin up a brand-new empty repo
    // (git init) or adopt an existing local Git folder. Both then mirror + publish
    // under the account, exactly like the import flow below.
    auto *repositoriesLabel = new QLabel("REPOSITORIES");
    repositoriesLabel->setObjectName("sectionLabel");
    auto *repositoriesHint = new QLabel(
        "Create a new empty repository or add one that already lives on this "
        "computer. Either way it's mirrored locally and published under your "
        "node so others can discover and mirror it.");
    repositoriesHint->setObjectName("statusLine");
    repositoriesHint->setWordWrap(true);

    auto *newRepoButton = new QPushButton("New repository\xE2\x80\xA6");
    newRepoButton->setObjectName("primaryButton");
    newRepoButton->setCursor(Qt::PointingHandCursor);
    newRepoButton->setToolTip("Create a brand-new empty Git repository");
    setOcticon(newRepoButton, "repo", 16);
    connect(newRepoButton, &QPushButton::clicked, this,
            &MainWindow::createNewRepository);

    auto *addLocalRepoButton = new QPushButton("Add local repository\xE2\x80\xA6");
    addLocalRepoButton->setObjectName("ghostButton");
    addLocalRepoButton->setCursor(Qt::PointingHandCursor);
    addLocalRepoButton->setToolTip(
        "Pick an existing Git folder on this computer to mirror and publish");
    setOcticon(addLocalRepoButton, "file-directory", 16);
    connect(addLocalRepoButton, &QPushButton::clicked, this,
            &MainWindow::promptAddRepository);

    auto *repoButtonRow = new QHBoxLayout;
    repoButtonRow->setContentsMargins(0, 0, 0, 0);
    repoButtonRow->addWidget(newRepoButton);
    repoButtonRow->addWidget(addLocalRepoButton);
    repoButtonRow->addStretch();

    // Import a repo from GitHub/GitLab: clone it into a local working copy, then
    // mirror + publish it under this node like any local repo. An optional access
    // token per host authenticates the clone to dodge unauthenticated rate limits.
    auto *importLabel = new QLabel("IMPORT REPOSITORY");
    importLabel->setObjectName("sectionLabel");
    auto *importHint = new QLabel(
        "Clone a GitHub or GitLab repository into a local folder, then mirror "
        "and publish it under your node. Add an access token to avoid "
        "unauthenticated rate limits — tokens are stored locally only.");
    importHint->setObjectName("statusLine");
    importHint->setWordWrap(true);

    m_importUrlEdit = new QLineEdit;
    m_importUrlEdit->setPlaceholderText(
        "https://github.com/owner/repo  or  https://gitlab.com/group/repo");
    m_importButton = new QPushButton("Import");
    m_importButton->setObjectName("primaryButton");
    m_importButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_importButton, "download", 16);
    connect(m_importButton, &QPushButton::clicked, this,
            &MainWindow::importRemoteRepository);
    connect(m_importUrlEdit, &QLineEdit::returnPressed, this,
            &MainWindow::importRemoteRepository);
    auto *importRow = new QHBoxLayout;
    importRow->setContentsMargins(0, 0, 0, 0);
    importRow->addWidget(m_importUrlEdit, 1);
    importRow->addWidget(m_importButton);

    m_importStatus = new QLabel;
    m_importStatus->setObjectName("modeHint");
    m_importStatus->setWordWrap(true);
    m_importStatus->hide();

    auto *githubTokenEdit = new QLineEdit;
    githubTokenEdit->setEchoMode(QLineEdit::Password);
    githubTokenEdit->setPlaceholderText("GitHub personal access token (optional)");
    githubTokenEdit->setText(
        QSettings().value(kGithubTokenSetting).toString().trimmed());
    connect(githubTokenEdit, &QLineEdit::editingFinished, this, [githubTokenEdit] {
        const QString t = githubTokenEdit->text().trimmed();
        githubTokenEdit->setText(t);
        QSettings().setValue(kGithubTokenSetting, t);
    });
    auto *gitlabTokenEdit = new QLineEdit;
    gitlabTokenEdit->setEchoMode(QLineEdit::Password);
    gitlabTokenEdit->setPlaceholderText("GitLab personal access token (optional)");
    gitlabTokenEdit->setText(
        QSettings().value(kGitlabTokenSetting).toString().trimmed());
    connect(gitlabTokenEdit, &QLineEdit::editingFinished, this, [gitlabTokenEdit] {
        const QString t = gitlabTokenEdit->text().trimmed();
        gitlabTokenEdit->setText(t);
        QSettings().setValue(kGitlabTokenSetting, t);
    });
    auto *importTokenForm = new QFormLayout;
    importTokenForm->setLabelAlignment(Qt::AlignLeft);
    importTokenForm->setSpacing(8);
    importTokenForm->addRow("GitHub token", githubTokenEdit);
    importTokenForm->addRow("GitLab token", gitlabTokenEdit);

    // Variables / secrets shared by all action workflows on this node. Values
    // are injected into each run's environment (e.g. CLOUDFLARE_API_TOKEN) and
    // redacted from run logs.
    auto *varsLabel = new QLabel("VARIABLES / SECRETS");
    varsLabel->setObjectName("sectionLabel");
    auto *varsHint = new QLabel(
        "Injected into every action run's environment and redacted from logs. "
        "Add CLOUDFLARE_API_TOKEN here to let the deploy workflow authenticate.");
    varsHint->setObjectName("statusLine");
    varsHint->setWordWrap(true);

    m_varsTable = new QTableWidget(0, 2);
    installColumnHeaderMenu(m_varsTable); // 3-dots per-column menu (issue #318)
    m_varsTable->setHorizontalHeaderLabels({"Name", "Value"});
    m_varsTable->horizontalHeader()->setStretchLastSection(true);
    m_varsTable->verticalHeader()->setVisible(false);
    m_varsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_varsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_varsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_varsTable->setMaximumHeight(160);
    makeColumnsResizable(m_varsTable); // spreadsheet-style draggable columns (#263)

    auto *varAddButton = new QPushButton("Add\xE2\x80\xA6");
    auto *varEditButton = new QPushButton("Edit\xE2\x80\xA6");
    auto *varDeleteButton = new QPushButton("Delete");
    m_varsRevealButton = new QPushButton("Reveal");
    for (QPushButton *b :
         {varAddButton, varEditButton, varDeleteButton, m_varsRevealButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    m_varsRevealButton->setToolTip("Show or hide the secret values in clear text");
    connect(varAddButton, &QPushButton::clicked, this,
            [this] { addOrEditVariable(false); });
    connect(varEditButton, &QPushButton::clicked, this,
            [this] { addOrEditVariable(true); });
    connect(varDeleteButton, &QPushButton::clicked, this,
            &MainWindow::deleteSelectedVariable);
    // Double-clicking a row is the natural "edit this one" gesture.
    connect(m_varsTable, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { addOrEditVariable(true); });
    connect(m_varsRevealButton, &QPushButton::clicked, this,
            &MainWindow::toggleVariablesRevealed);
    // Click a revealed value to copy it to the clipboard. Masked rows do
    // nothing — there is nothing useful to copy while hidden.
    connect(m_varsTable, &QTableWidget::cellClicked, this,
            [this](int row, int column) {
                if (column != 1 || !m_varsRevealed)
                    return;
                QTableWidgetItem *item = m_varsTable->item(row, 1);
                if (!item)
                    return;
                const QString value = item->data(Qt::UserRole).toString();
                if (value.isEmpty())
                    return;
                QApplication::clipboard()->setText(value);
                const QString name = m_varsTable->item(row, 0)
                                         ? m_varsTable->item(row, 0)->text()
                                         : QString();
                logSystem(name.isEmpty()
                              ? QStringLiteral("Copied value to clipboard.")
                              : QStringLiteral("Copied %1 to clipboard.").arg(name));
            });
    auto *varButtonRow = new QHBoxLayout;
    varButtonRow->setContentsMargins(0, 0, 0, 0);
    varButtonRow->addWidget(varAddButton);
    varButtonRow->addWidget(varEditButton);
    varButtonRow->addWidget(varDeleteButton);
    varButtonRow->addWidget(m_varsRevealButton);
    varButtonRow->addStretch();

    auto *leaveButton = new QPushButton("Leave node");
    leaveButton->setObjectName("dangerButton");
    leaveButton->setCursor(Qt::PointingHandCursor);
    setOcticon(leaveButton, "sign-out", 16);
    connect(leaveButton, &QPushButton::clicked, this, [this] { leaveSession(); });

    // Rebuild & restart now lives here, next to Leave node, rather than in the
    // server rail.
    m_rebuildButton = new QPushButton("Rebuild & restart");
    m_rebuildButton->setObjectName("ghostButton");
    m_rebuildButton->setCursor(Qt::PointingHandCursor);
    m_rebuildButton->setToolTip("Pull the latest version, rebuild, and relaunch");
    setOcticon(m_rebuildButton, "sync", 16);
    connect(m_rebuildButton, &QPushButton::clicked, this,
            [this] { startRestartSpin(m_rebuildButton); quickRebuildRestart(); });

    // Log out clears the signed-in account so you can log back in (as the same
    // or a different account).
    auto *logoutButton = new QPushButton("Log out");
    logoutButton->setObjectName("ghostButton");
    logoutButton->setCursor(Qt::PointingHandCursor);
    setOcticon(logoutButton, "sign-out", 16);
    connect(logoutButton, &QPushButton::clicked, this, [this] { logout(); });

    m_rebuildStatus = new QLabel;
    m_rebuildStatus->setObjectName("modeHint");
    m_rebuildStatus->setWordWrap(true);
    m_rebuildStatus->hide();

    // Uninstall: erase every trace of ForkMesh from this computer and quit.
    // Kept at the far right of the footer, past the stretch, so it sits apart
    // from the everyday actions and is hard to hit by accident.
    auto *uninstallButton = new QPushButton("Uninstall ForkMesh");
    uninstallButton->setObjectName("dangerButton");
    uninstallButton->setCursor(Qt::PointingHandCursor);
    uninstallButton->setToolTip(
        "Permanently delete all ForkMesh data, settings, the desktop launcher "
        "and the program files from this computer, then quit.");
    setOcticon(uninstallButton, "trash", 16);
    connect(uninstallButton, &QPushButton::clicked, this,
            [this] { uninstallForkMesh(); });

    auto *footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->addWidget(leaveButton);
    footerRow->addWidget(m_rebuildButton);
    footerRow->addWidget(logoutButton);
    footerRow->addStretch();
    footerRow->addWidget(uninstallButton);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(10);
    layout->addWidget(title);

    // Group related settings under tabs rather than one long two-column scroll.
    // Each tab scrolls on its own; the title above and the account-action footer
    // below stay pinned so they're reachable from any tab.
    auto *tabs = new QTabWidget;
    tabs->setObjectName("settingsTabs");
    tabs->setDocumentMode(true);
    // Wrap a tab's content widget in a frameless, vertically-scrolling page.
    auto addTab = [tabs](QWidget *body, const QString &name) {
        auto *scroll = new QScrollArea;
        scroll->setObjectName("settingsTabScroll");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(body);
        tabs->addTab(scroll, name);
    };

    // General: identity, appearance and launch behaviour.
    auto *generalTab = new QWidget;
    auto *generalCol = new QVBoxLayout(generalTab);
    generalCol->setContentsMargins(2, 14, 2, 14);
    generalCol->setSpacing(10);
    generalCol->addWidget(profileLabel);
    generalCol->addLayout(form);
    generalCol->addSpacing(6);
    generalCol->addWidget(appearanceLabel);
    generalCol->addWidget(m_themeCombo, 0, Qt::AlignLeft);
    generalCol->addWidget(showCurrencyCombo, 0, Qt::AlignLeft);
    generalCol->addWidget(rebuildButtonCheck);
    generalCol->addWidget(verboseNetLogCheck);
    generalCol->addSpacing(6);
    generalCol->addWidget(bountyLabel);
    generalCol->addWidget(autoBountyCheck);
    generalCol->addWidget(bountyHint);
    generalCol->addLayout(bountyAmountRow);
    generalCol->addLayout(bountyModeRow);
    generalCol->addSpacing(6);
    generalCol->addWidget(nodeStatsLabel);
    generalCol->addWidget(nodeStatsHint);
    for (QCheckBox *check : std::as_const(nodeStatChecks))
        generalCol->addWidget(check);
    generalCol->addWidget(telemetryCheck);
    generalCol->addSpacing(6);
    generalCol->addWidget(startupLabel);
    generalCol->addWidget(m_autostartCheck);
    generalCol->addWidget(autoUpdateCheck);
    generalCol->addWidget(defaultTabLabel);
    generalCol->addWidget(defaultTabCombo, 0, Qt::AlignLeft);
    generalCol->addWidget(autoSwitchToAgentCheck);
    generalCol->addSpacing(6);
    generalCol->addWidget(screenshotLabel);
    generalCol->addWidget(screenshotHint);
    generalCol->addWidget(alignmentTarget, 0, Qt::AlignLeft);
    generalCol->addStretch();
    addTab(generalTab, "General");

    // Repositories: creating/importing repos and where mirrors live.
    auto *reposTab = new QWidget;
    auto *reposCol = new QVBoxLayout(reposTab);
    reposCol->setContentsMargins(2, 14, 2, 14);
    reposCol->setSpacing(10);
    reposCol->addWidget(repositoriesLabel);
    reposCol->addWidget(repositoriesHint);
    reposCol->addLayout(repoButtonRow);
    reposCol->addSpacing(6);
    reposCol->addWidget(importLabel);
    reposCol->addWidget(importHint);
    reposCol->addLayout(importRow);
    reposCol->addWidget(m_importStatus);
    reposCol->addLayout(importTokenForm);
    reposCol->addSpacing(6);
    reposCol->addWidget(storageLabel);
    reposCol->addLayout(mirrorRow);
    reposCol->addSpacing(6);
    reposCol->addWidget(previewCacheLabel);
    reposCol->addLayout(previewCacheRow);
    reposCol->addSpacing(6);
    reposCol->addWidget(issuesSyncLabel);
    reposCol->addWidget(autoSyncIssuesCheck);
    reposCol->addStretch();
    addTab(reposTab, "Repositories");

    // Notifications: every desktop-alert opt-in.
    auto *notifyTab = new QWidget;
    auto *notifyCol = new QVBoxLayout(notifyTab);
    notifyCol->setContentsMargins(2, 14, 2, 14);
    notifyCol->setSpacing(10);
    notifyCol->addWidget(notifyLabel);
    notifyCol->addWidget(pushAlertCheck);
    notifyCol->addWidget(actionAlertCombo, 0, Qt::AlignLeft);
    notifyCol->addWidget(nodeConnectAlertCheck);
    notifyCol->addWidget(disbursementAlertCheck);
    notifyCol->addWidget(chatMessageAlertCheck);
    notifyCol->addWidget(mentionAlertCheck);
    notifyCol->addWidget(issueAlertCheck);
    notifyCol->addWidget(pullAlertCheck);
    notifyCol->addWidget(commentAlertCheck);
    notifyCol->addWidget(mirrorUpdateAlertCheck);
    notifyCol->addWidget(coveOpenAlertCheck);
    notifyCol->addWidget(newUserAlertCheck);
    notifyCol->addStretch();
    addTab(notifyTab, "Notifications");

    // Agents & IDE: model keys/commands and editor integration.
    auto *agentsTab = new QWidget;
    auto *agentsCol = new QVBoxLayout(agentsTab);
    agentsCol->setContentsMargins(2, 14, 2, 14);
    agentsCol->setSpacing(10);
    agentsCol->addWidget(agentsLabel);
    agentsCol->addWidget(agentsHint);
    agentsCol->addLayout(agentForm);
    agentsCol->addWidget(autoStallAgentCheck);
    agentsCol->addWidget(autoFixConflictsCheck);
    agentsCol->addSpacing(6);
    agentsCol->addWidget(usageLabel);
    agentsCol->addWidget(usageHint);
    agentsCol->addLayout(usageText);
    agentsCol->addSpacing(6);
    agentsCol->addWidget(ideLabel);
    agentsCol->addWidget(ideIntegrationCheck);
    agentsCol->addWidget(ideStatus);
    agentsCol->addSpacing(6);
    agentsCol->addWidget(voiceLabel);
    agentsCol->addWidget(voiceHint);
    agentsCol->addLayout(voiceRow);
    agentsCol->addLayout(voiceDeviceRow);
    agentsCol->addLayout(voiceTestRow);
    agentsCol->addWidget(m_whisperStatusLabel);
    agentsCol->addStretch();
    addTab(agentsTab, "Agents & IDE");

    // Secrets & Coves: shared action variables and encrypted coves.
    auto *secretsTab = new QWidget;
    auto *secretsCol = new QVBoxLayout(secretsTab);
    secretsCol->setContentsMargins(2, 14, 2, 14);
    secretsCol->setSpacing(10);
    secretsCol->addWidget(varsLabel);
    secretsCol->addWidget(varsHint);
    secretsCol->addWidget(m_varsTable);
    secretsCol->addLayout(varButtonRow);
    secretsCol->addSpacing(6);
    secretsCol->addWidget(buildCoveGlobalSection());
    secretsCol->addStretch();
    addTab(secretsTab, "Secrets & Coves");

    // Security: private vulnerability reporting form.
    addTab(buildVulnReportTab(), "Security");

    // Data: where configuration data lives, per-directory breakdown, backup and
    // cleanup. Built in its own translation unit (MainWindowData.cpp).
    addTab(buildDataSection(), "Data");

    layout->addWidget(tabs, 1);
    layout->addWidget(m_rebuildStatus);
    layout->addLayout(footerRow);
    reloadVariablesTable();
    setSettingsAvatar(QByteArray()); // show the current/generated avatar
    return page;
}

void MainWindow::refreshIdentityBackupNag()
{
    if (!m_identityBackupNag)
        return;
    const bool backedUp = m_profileIdentity.hasBackedUp();
    m_identityBackupNag->setVisible(!backedUp);
    if (!backedUp)
        m_identityBackupNag->setText(
            "⚠ You haven't backed up your identity key yet. If you lose this "
            "machine, your ForkMesh identity is gone for good. Back it up now.");
}

void MainWindow::backUpIdentityKey()
{
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load()) {
        QMessageBox::warning(this, "Identity key",
                             "No identity key is loaded yet.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Back up identity key");
    auto *l = new QVBoxLayout(&dialog);

    auto *intro = new QLabel(
        "Your Ed25519 identity key signs everything you publish. Export it to a "
        "passphrase-encrypted keyfile (or scan the QR onto another device) and "
        "keep it somewhere safe. On a new machine, import it to keep your name, "
        "signatures and bounty bindings. Public key:\n" +
        m_profileIdentity.publicKey());
    intro->setWordWrap(true);
    intro->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(intro);

    auto *qrLabel = new QLabel;
    qrLabel->setAlignment(Qt::AlignCenter);
    l->addWidget(qrLabel);

    // Prompt for a passphrase and return the encrypted keyfile text, or {}.
    auto exportKeyfile = [this, &dialog]() -> QString {
        bool ok = false;
        const QString pass = QInputDialog::getText(
            &dialog, "Encrypt keyfile",
            "Choose a passphrase to encrypt the keyfile. You'll need it to "
            "restore the key — it cannot be recovered.",
            QLineEdit::Password, QString(), &ok);
        if (!ok || pass.isEmpty())
            return {};
        const QString keyfile = m_profileIdentity.exportEncryptedKeyfile(pass);
        if (keyfile.isEmpty())
            QMessageBox::warning(&dialog, "Export failed",
                                 "Could not encrypt the identity key.");
        return keyfile;
    };

    auto *saveBtn = new QPushButton("Save keyfile…");
    saveBtn->setObjectName("primaryButton");
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, &dialog, [this, &dialog, exportKeyfile] {
        const QString keyfile = exportKeyfile();
        if (keyfile.isEmpty())
            return;
        const QString path = QFileDialog::getSaveFileName(
            &dialog, "Save identity keyfile", "forkmesh-identity.keyfile.json",
            "ForkMesh keyfile (*.json)");
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(&dialog, "Save failed",
                                 "Could not write " + path);
            return;
        }
        f.write(keyfile.toUtf8());
        f.close();
        m_profileIdentity.markBackedUp();
        refreshIdentityBackupNag();
        QMessageBox::information(&dialog, "Backed up",
                                 "Identity key saved to " + path);
    });

    auto *qrBtn = new QPushButton("Show QR");
    qrBtn->setObjectName("ghostButton");
    qrBtn->setCursor(Qt::PointingHandCursor);
    connect(qrBtn, &QPushButton::clicked, &dialog,
            [this, &dialog, qrLabel, exportKeyfile] {
                const QString keyfile = exportKeyfile();
                if (keyfile.isEmpty())
                    return;
                const QImage qr = QrCode::encodeToImage(keyfile, 3, 3,
                                                        QrCode::Ecl::Low);
                if (qr.isNull()) {
                    QMessageBox::warning(
                        &dialog, "QR too large",
                        "The encrypted keyfile is too large for a QR code; use "
                        "\"Save keyfile\" instead.");
                    return;
                }
                qrLabel->setPixmap(QPixmap::fromImage(qr));
                m_profileIdentity.markBackedUp();
                refreshIdentityBackupNag();
            });

    auto *importBtn = new QPushButton("Import keyfile…");
    importBtn->setObjectName("ghostButton");
    importBtn->setCursor(Qt::PointingHandCursor);
    connect(importBtn, &QPushButton::clicked, &dialog, [this, &dialog] {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, "Import identity keyfile", QString(),
            "ForkMesh keyfile (*.json);;All files (*)");
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&dialog, "Import failed",
                                 "Could not read " + path);
            return;
        }
        const QString keyfile = QString::fromUtf8(f.readAll());
        f.close();
        const QString incoming = ForkMeshIdentity::keyfilePublicKey(keyfile);
        if (incoming.isEmpty()) {
            QMessageBox::warning(&dialog, "Import failed",
                                 "That file is not a ForkMesh identity keyfile.");
            return;
        }
        if (incoming != m_profileIdentity.publicKey() &&
            QMessageBox::question(
                &dialog, "Replace identity?",
                "This keyfile is a different identity than the one on this "
                "machine. Importing it replaces your current key. Continue?",
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        bool ok = false;
        const QString pass = QInputDialog::getText(
            &dialog, "Decrypt keyfile", "Enter the keyfile passphrase.",
            QLineEdit::Password, QString(), &ok);
        if (!ok || pass.isEmpty())
            return;
        if (!m_profileIdentity.importEncryptedKeyfile(keyfile, pass)) {
            QMessageBox::warning(&dialog, "Import failed",
                                 m_profileIdentity.errorString());
            return;
        }
        refreshIdentityBackupNag();
        QMessageBox::information(
            &dialog, "Identity imported",
            "Restored identity " + m_profileIdentity.shortPublicKey() +
                ". Restart ForkMesh so every panel picks up the new key.");
        dialog.accept();
    });

    auto *closeBtn = new QPushButton("Close");
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    auto *row = new QHBoxLayout;
    row->addWidget(saveBtn);
    row->addWidget(qrBtn);
    row->addWidget(importBtn);
    row->addStretch(1);
    row->addWidget(closeBtn);
    l->addLayout(row);

    dialog.exec();
}

QByteArray MainWindow::effectiveAvatar()
{
    if (!m_userAvatar.isEmpty())
        return m_userAvatar;
    QString seed = !m_accountName.isEmpty()
                       ? m_accountName
                       : (!m_userName.isEmpty() ? m_userName
                                                : m_profileIdentity.publicKey());
    if (seed.isEmpty())
        seed = QStringLiteral("forkmesh");
    return forkMeshAvatarPng(seed);
}

void MainWindow::updateAvatarButton()
{
    if (!m_avatarNavButton)
        return;
    const QPixmap pm = roundedAvatar(effectiveAvatar(), 34);
    if (!pm.isNull())
        m_avatarNavButton->setIcon(QIcon(pm));
    refreshIssueComposerAvatar();
}

void MainWindow::refreshIssueComposerAvatar()
{
    if (!m_issueComposerAvatar)
        return;
    // Show this node's avatar next to the comment composer so it's clear who is
    // about to post.
    const QPixmap pm = roundedAvatar(effectiveAvatar(), 36);
    if (pm.isNull()) {
        m_issueComposerAvatar->setPixmap(QPixmap());
        m_issueComposerAvatar->setText("FM");
    } else {
        m_issueComposerAvatar->setText(QString());
        m_issueComposerAvatar->setPixmap(pm);
    }
}

void MainWindow::setSettingsAvatar(const QByteArray &pngData)
{
    if (!m_settingsAvatarPreview)
        return;
    // Fall back to the deterministic generated avatar when none is set.
    const QByteArray data = pngData.isEmpty() ? effectiveAvatar() : pngData;
    QPixmap pixmap;
    if (!pixmap.loadFromData(data))
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
    beginRestartLog();
    showUpdateLog();
    logRestart(QStringLiteral("quick rebuild & restart started"));
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;
    const QString clientDir = updateClientDir();
    if (!QDir(clientDir).exists("CMakeLists.txt")) {
        stopRefreshSpin();
        stopRestartSpin();
        QMessageBox::information(
            this, "Rebuild & restart",
            "No local source checkout to rebuild from. Use Quick update on the "
            "start screen instead.");
        return;
    }
    if (m_rebuildButton)
        m_rebuildButton->setEnabled(false);
    // Dev iteration loop: build unoptimized (Debug => -O0) so the 20k-line
    // MainWindow.cpp compiles in ~13s instead of ~42s at -O2. The user-facing
    // Quick update path stays Release. Note: alternating between this and a
    // Release update reconfigures the shared build dir and forces one full
    // rebuild on the switch.
    buildAndRelaunch(clientDir, QString(), QString(), QStringLiteral("Debug"));
}

void MainWindow::rebuildAndRelaunch()
{
    beginRestartLog();
    showUpdateLog();
    logRestart(QStringLiteral("clean rebuild & restart started"));
    if (m_settingsNameEdit)
        saveProfileName(m_settingsNameEdit->text());
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;
    m_rebuildButton->setEnabled(false);

    const QString clientDir = updateClientDir();
    if (!QDir(clientDir).exists("CMakeLists.txt")) {
        setUpdateStatus("No local source checkout to rebuild from. Use Quick "
                        "update on the start screen instead.",
                        true);
        stopRestartSpin();
        m_rebuildButton->setEnabled(true);
        return;
    }
    // Clear the build cache for a clean from-scratch rebuild, then relaunch.
    setUpdateStatus("Clearing build cache...");
    QDir(clientDir + "/build").removeRecursively();
    buildAndRelaunch(clientDir);
}

void MainWindow::maybeAutoUpdate()
{
    if (!QSettings().value(kAutoUpdateSetting, false).toBool())
        return;
    if (m_autoUpdateChecking)
        return; // a check from an earlier tick is still in flight
    if (m_rebuildButton && !m_rebuildButton->isEnabled())
        return; // a rebuild (manual or auto) is already running
    if (anyAgentRunning())
        return; // never yank an in-progress agent session out from under itself

    const QString clientDir = updateClientDir();
    if (!QDir(clientDir).exists("CMakeLists.txt"))
        return; // no local checkout yet; first install goes through the manual/headless flow

    QString upstream;
    if (!gitOutput(clientDir,
                   {"rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"},
                   &upstream) ||
        upstream.isEmpty())
        return; // no upstream branch configured to compare against

    // Fetch quietly in the background (no blocking wait — this can take a while
    // on a slow connection) and only fall through to the visible update flow
    // when it actually finds a new tagged release, so a node never rebuilds and
    // relaunches for every ordinary commit landing on main — only when a real
    // release is cut (adhoc #50).
    m_autoUpdateChecking = true;
    auto *fetch = new QProcess(this);
    fetch->setWorkingDirectory(clientDir);
    auto *timeoutGuard = new QTimer(fetch);
    timeoutGuard->setSingleShot(true);
    connect(timeoutGuard, &QTimer::timeout, fetch, &QProcess::kill);
    timeoutGuard->start(45000);

    auto finish = [this, fetch] {
        m_autoUpdateChecking = false;
        fetch->deleteLater();
    };
    connect(fetch, &QProcess::errorOccurred, this,
            [finish](QProcess::ProcessError) { finish(); });
    connect(fetch, &QProcess::finished, this,
            [this, clientDir, finish](int exitCode, QProcess::ExitStatus status) {
                finish();
                if (status != QProcess::NormalExit || exitCode != 0)
                    return; // offline, or the remote is unreachable right now; retry next tick

                // Newest release tag, same "sort=-creatordate" idiom the Releases
                // tab uses to pick the current release. No tags at all means this
                // checkout predates tagged releases; leave it alone rather than
                // updating on ordinary commits.
                QString latestTag;
                if (!gitOutput(clientDir,
                               {"for-each-ref", "--sort=-creatordate", "--count=1",
                                "refs/tags", "--format=%(refname:short)"},
                               &latestTag) ||
                    latestTag.trimmed().isEmpty())
                    return;
                latestTag = latestTag.trimmed();

                QString tagCommit;
                if (!gitOutput(clientDir, {"rev-list", "-n1", latestTag}, &tagCommit) ||
                    tagCommit.isEmpty())
                    return;

                // Already on (or ahead of) the latest release: nothing to do,
                // even if unreleased commits have since landed on main.
                if (gitOutput(clientDir,
                              {"merge-base", "--is-ancestor", tagCommit, "HEAD"},
                              nullptr))
                    return;

                // Re-check: the fetch may have taken a while, so the gating
                // conditions could have changed while it was in flight.
                if ((m_rebuildButton && !m_rebuildButton->isEnabled()) ||
                    anyAgentRunning())
                    return;

                logSystem(QStringLiteral("Auto-update: release %1 is available; "
                                         "updating in the background.")
                             .arg(latestTag));
                updateRebuildRestart();
            });
    fetch->start(QStringLiteral("git"), {"fetch", "--quiet", "--tags"});
}

void MainWindow::attachBackend(ChatBackend *backend)
{
    if (m_backend)
        leaveSession();
    m_backend = backend;
    // Force refreshRepositoryList to re-push the mirror adverts to this backend even
    // if the repo signature hasn't changed since the last one (adhoc #83 skip cache).
    m_mirrorAdvertSig.clear();

    connect(backend, &ChatBackend::messageArrived, this, &MainWindow::onMessage);
    connect(backend, &ChatBackend::reactionChanged, this, &MainWindow::onReaction);
    connect(backend, &ChatBackend::messageEdited, this, &MainWindow::onMessageEdited);
    connect(backend, &ChatBackend::messageDeleted, this, &MainWindow::onMessageDeleted);
    connect(backend, &ChatBackend::adminDeleteRequested, this,
            &MainWindow::onAdminDeleteRequested);
    connect(backend, &ChatBackend::avatarChanged, this, &MainWindow::onAvatar);
    connect(backend, &ChatBackend::typingChanged, this, &MainWindow::onTypingChanged);
    connect(backend, &ChatBackend::systemMessage, this, &MainWindow::logSystem);
    connect(backend, &ChatBackend::channelsChanged, this, &MainWindow::setChannels);
    connect(backend, &ChatBackend::privateChannelJoined, this,
            [this](const QString &channel) {
                m_privateChannels.insert(channel);
                persistPrivateChannels();
                refreshChannelList();
            });
    connect(backend, &ChatBackend::rosterChanged, this, &MainWindow::setRoster);
    connect(backend, &ChatBackend::mirrorUpdated, this, &MainWindow::onPeerMirrorUpdated);
    connect(backend, &ChatBackend::coveOpened, this, &MainWindow::onCoveOpened);
    connect(backend, &ChatBackend::statusChanged, this, [this](const QString &status) {
        const QString summary = status.section(" · ", 0, 0);
        m_statusLine->setText(summary);
        logSystem("Status: " + status);
        updateConnectionStatus();
    });
    connect(backend, &ChatBackend::firewallBlocking, this, &MainWindow::showFirewallBanner);
    connect(backend, &ChatBackend::firewallHealthy, this, &MainWindow::hideFirewallBanner);
    connect(backend, &ChatBackend::fatalError, this, [this](const QString &message) {
        leaveSession();
        m_setupError->setText(message);
        m_setupError->show();
    });
    // Let a headless console attach its live event feed to this backend.
    emit backendAttached(backend);
}

// --- Headless / CLI support -------------------------------------------------
// Read-only views and control entry points used by HeadlessConsole when the app
// runs with no display. Everything routes through the same logic the GUI uses.

bool MainWindow::headlessConnected() const
{
    return m_backend != nullptr && m_connectedAtMs > 0;
}

QString MainWindow::headlessNodeName() const
{
    if (!m_userName.isEmpty())
        return m_userName;
    return QSettings().value(kAccountNameSetting).toString().trimmed();
}

void MainWindow::headlessStart(const QString &name, const QString &solana)
{
    // Equivalent to typing a name and pressing the GUI connect button: fill the
    // (offscreen) setup widgets and run the normal startSession path. The Ed25519
    // identity auto-generates on first load, so a fresh VM needs only a name.
    const QString trimmed = name.trimmed().toLower();
    if (m_nameEdit)
        m_nameEdit->setText(trimmed);
    if (m_solanaEdit && !solana.trimmed().isEmpty())
        m_solanaEdit->setText(solana.trimmed());
    if (m_serverUrlEdit && m_serverUrlEdit->text().trimmed().isEmpty())
        m_serverUrlEdit->setText(serverHostDisplay(kDefaultServerUrl));
    startSession();
}

void MainWindow::headlessSyncNow()
{
    autoSyncMirrors();
    pollOwnedInboxes();
}

void MainWindow::headlessUpdateRestart()
{
    // Same code path as the GUI "Update, rebuild & restart" button. The update log
    // dialog it opens is invisible under the offscreen platform, but every phase is
    // also echoed to the terminal by logRestart()/qInfo(), so a headless operator
    // sees the full progress. On success the process relaunches itself and quits.
    updateRebuildRestart();
}

QStringList MainWindow::headlessStatusLines() const
{
    QStringList lines;
    lines << QStringLiteral("ForkMesh v" FORKMESH_VERSION "  (headless)");
    const QString node = headlessNodeName();
    lines << QStringLiteral("Node:      %1")
                 .arg(node.isEmpty()
                          ? QStringLiteral("(not set — run: setup <name>)")
                          : node);
    if (!m_accountName.isEmpty())
        lines << QStringLiteral("Account:   %1 (%2, %3)")
                     .arg(m_accountName,
                          m_accountAuthenticated
                              ? QStringLiteral("authenticated")
                              : QStringLiteral("unauthenticated"),
                          m_accountTier.isEmpty() ? QStringLiteral("free")
                                                  : m_accountTier);
    if (headlessConnected()) {
        const qint64 secs =
            (QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs) / 1000;
        lines << QStringLiteral("Connected: yes  (uptime %1s)").arg(secs);
        if (m_statusLine && !m_statusLine->text().isEmpty())
            lines << QStringLiteral("Status:    %1").arg(m_statusLine->text());
    } else {
        lines << QStringLiteral("Connected: no  (run: setup <name>)");
    }
    int online = 0;
    for (const MemberInfo &m : m_homeRoster)
        if (m.online)
            ++online;
    lines << QStringLiteral("Peers:     %1 online / %2 known")
                 .arg(online)
                 .arg(m_homeRoster.size());
    lines << QStringLiteral("Repos:     %1").arg(m_repositories.size());
    lines << QStringLiteral("Serving:   %1 live host(s)").arg(m_repoHosts.size());

    // Aggregate the collaboration totals across every permanent repo this node
    // holds, so an operator can see at a glance how much is on the node without
    // opening the GUI: pull requests, discussions, branches and commits.
    int pulls = 0, discussions = 0, branches = 0, commits = 0;
    for (const RepositoryRecord &r : m_repositories) {
        if (r.previewOnly)
            continue;
        pulls += PullStore(r.localPath, r.mirrorPath, &m_profileIdentity, m_userName)
                     .loadAll()
                     .size();
        discussions +=
            DiscussionStore(r.localPath, r.mirrorPath, &m_profileIdentity, m_userName)
                .loadAll()
                .size();
        const QString dir =
            (!r.localPath.isEmpty() && QDir(r.localPath).exists())
                ? r.localPath
                : (!r.mirrorPath.isEmpty() && QDir(r.mirrorPath).exists()
                       ? r.mirrorPath
                       : QString());
        if (dir.isEmpty())
            continue;
        QByteArray out;
        if (runGitCapture(dir, {"for-each-ref", "--format=%(refname)", "refs/heads"},
                          &out, nullptr))
            branches += QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts).size();
        out.clear();
        if (runGitCapture(dir, {"rev-list", "--count", "HEAD"}, &out, nullptr))
            commits += QString::fromUtf8(out).trimmed().toInt();
    }
    lines << QString::fromUtf8("Content:   %1 pulls \xC2\xB7 %2 discussions \xC2\xB7 "
                              "%3 branches \xC2\xB7 %4 commits")
                 .arg(pulls)
                 .arg(discussions)
                 .arg(branches)
                 .arg(commits);

    lines << QStringLiteral("Load:      %1").arg(headlessResourceLine());
    return lines;
}

QStringList MainWindow::headlessRosterLines() const
{
    QStringList lines;
    if (m_homeRoster.isEmpty()) {
        lines << QStringLiteral("(no peers)");
        return lines;
    }
    for (const MemberInfo &m : m_homeRoster) {
        QString line =
            QStringLiteral("%1 %2").arg(m.online ? QStringLiteral("●")
                                                 : QStringLiteral("○"),
                                        m.name.isEmpty() ? m.id : m.name);
        if (m.self)
            line += QStringLiteral(" (you)");
        if (!m.platform.isEmpty())
            line += QStringLiteral("  [%1 %2]").arg(m.platform, m.version);
        if (!m.mirrors.isEmpty())
            line += QStringLiteral("  mirrors:%1").arg(m.mirrors.size());
        lines << line;
    }
    return lines;
}

QStringList MainWindow::headlessRepoLines() const
{
    QStringList lines;
    if (m_repositories.isEmpty()) {
        lines << QStringLiteral("(no repositories)");
        return lines;
    }
    for (const RepositoryRecord &r : m_repositories) {
        QString line = QStringLiteral("%1/%2").arg(r.owner, r.name);
        QStringList flags;
        if (r.publishToNetwork)
            flags << QStringLiteral("published");
        if (r.isPrivate)
            flags << QStringLiteral("private");
        if (r.previewOnly)
            flags << QStringLiteral("preview");
        if (!r.mirrorPath.isEmpty())
            flags << QStringLiteral("mirror");
        if (!flags.isEmpty())
            line += QStringLiteral("  [%1]").arg(flags.join(QStringLiteral(", ")));
        lines << line;
    }
    return lines;
}

QString MainWindow::headlessResourceLine() const
{
    const double cpu = SystemStats::cpuPercent();
    const QString cpuText = cpu < 0.0
                                ? QStringLiteral("cpu n/a")
                                : QStringLiteral("cpu %1%").arg(cpu, 0, 'f', 1);
    const qint64 rss = SystemStats::residentBytes();
    QString memText = QStringLiteral("mem %1").arg(SystemStats::formatBytes(rss));
    const qint64 total = SystemStats::totalMemoryBytes();
    if (rss > 0 && total > 0)
        memText += QStringLiteral(" (%1% of %2)")
                       .arg(100.0 * double(rss) / double(total), 0, 'f', 1)
                       .arg(SystemStats::formatBytes(total));
    return QStringLiteral("%1  %2").arg(cpuText, memText);
}

QStringList MainWindow::headlessMirrorLines() const
{
    QStringList lines;
    // Lead with this node's CPU / memory so an operator watching a durable
    // headless daemon can see how much the node is consuming while it serves its
    // mirrors (issue #287).
    lines << QStringLiteral("node load: %1").arg(headlessResourceLine());
    for (const RepositoryRecord &r : m_repositories) {
        if (r.previewOnly || r.mirrorPath.isEmpty())
            continue;
        QString line = QStringLiteral("%1/%2").arg(r.owner, r.name);
        if (r.lastSyncMs > 0)
            line += QStringLiteral("  synced %1")
                        .arg(QDateTime::fromMSecsSinceEpoch(r.lastSyncMs)
                                 .toString(Qt::ISODate));
        lines << line;
    }
    if (lines.size() == 1)
        lines << QStringLiteral("(no mirrors)");
    return lines;
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
    updateConnectionStatus();
    m_stack->setCurrentIndex(0);
    m_userName.clear();

    m_homeRoster.clear();
    refreshRepositoryList(); // clears node online status from the repos panel
    updateHomeStats();
}

// ------------------------------------------------------------------ firewall

void MainWindow::showFirewallBanner(const QString &displayCommand,
                                    const QString &privilegedCommand)
{
    m_firewallPrivilegedCommand = privilegedCommand;
    m_firewallBannerLabel->setText(
        "A firewall on this computer may be blocking peers from "
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
                        "Firewall opened. Peers should connect "
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

// ------------------------------------------------------------- voice input

// Reflect whether the selected voice engine is installed (or installing) on the
// Settings button + status line, and show the model combo that matches the engine.
// Safe to call even when the Settings widgets don't exist.
void MainWindow::refreshWhisperStatus()
{
    const bool parakeet = voiceEngine() == QStringLiteral("parakeet");
    const bool installing =
        (m_whisperInstallProc &&
         m_whisperInstallProc->state() != QProcess::NotRunning) ||
        (m_parakeetInstallProc &&
         m_parakeetInstallProc->state() != QProcess::NotRunning);
    const bool ready = voiceInputReady();
    // Only the active engine's model combo is relevant.
    if (m_whisperModelCombo)
        m_whisperModelCombo->setVisible(!parakeet);
    if (m_parakeetModelCombo)
        m_parakeetModelCombo->setVisible(parakeet);
    if (m_whisperInstallButton) {
        m_whisperInstallButton->setEnabled(!installing);
        m_whisperInstallButton->setText(
            installing ? QStringLiteral("Installing\xE2\x80\xA6")
                       : ready ? QStringLiteral("Reinstall")
                               : QStringLiteral("Download & install"));
    }
    if (m_voiceEngineCombo)
        m_voiceEngineCombo->setEnabled(!installing);
    if (m_whisperModelCombo)
        m_whisperModelCombo->setEnabled(!installing);
    if (m_parakeetModelCombo)
        m_parakeetModelCombo->setEnabled(!installing);
    if (m_whisperStatusLabel && !installing) {
        if (ready && parakeet)
            m_whisperStatusLabel->setText(
                QString::fromUtf8("\xE2\x97\x8F Installed (Parakeet). A mic now sits "
                                  "next to the prompt box."));
        else if (ready)
            m_whisperStatusLabel->setText(
                QString::fromUtf8("\xE2\x97\x8F Installed (%1 model). A mic now sits "
                                  "next to the prompt box.")
                    .arg(whisperModelName()));
        else
            m_whisperStatusLabel->setText("Not installed.");
    }
}

// Settings "Test mic" (adhoc #14): start/stop a self-contained recording from the
// chosen microphone and drive the level bar from the growing capture, so the user
// can confirm the mic is actually being hooked into and watch the level move as
// they speak. Independent of whisper.cpp — it only needs a recorder.
void MainWindow::toggleMicTest()
{
    if (!m_voiceTestMicButton)
        return;

    // Already testing: stop. The recorder finalizes on SIGTERM; cleanup runs from
    // its finished handler (or here if it never started).
    if (m_voiceTestRecording) {
        stopMicTest();
        return;
    }

    m_voiceTestWavPath =
        QDir(QDir::tempPath())
            .filePath(QStringLiteral("forkmesh-mictest-%1.wav")
                          .arg(QDateTime::currentMSecsSinceEpoch()));
    const AudioRecorderCommand rec = audioRecorderFor(m_voiceTestWavPath);
    if (rec.program.isEmpty()) {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        logSystem("Mic test needs ffmpeg to capture the microphone. Install it "
                  "and make sure it's on your PATH.");
#else
        logSystem("Mic test needs a recorder. Install one of: arecord "
                  "(alsa-utils), parecord (pulseaudio-utils) or ffmpeg.");
#endif
        return;
    }

    auto *proc = new QProcess(this);
    m_voiceTestProc = proc;
    connect(proc, &QProcess::finished, this,
            [this, proc](int, QProcess::ExitStatus) {
                const QString err =
                    QString::fromUtf8(proc->readAllStandardError()).trimmed();
                const bool captured = QFileInfo::exists(m_voiceTestWavPath) &&
                                      QFileInfo(m_voiceTestWavPath).size() >= 1024;
                if (m_voiceTestProc == proc)
                    stopMicTest();
                proc->deleteLater();
                // The recorder never opened the device (no WAV / header only):
                // surface why so the user knows the mic isn't being hooked into.
                if (!captured && !err.isEmpty())
                    logSystem("Mic test: capture failed \xE2\x80\x94 " +
                              err.right(200));
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart || m_voiceTestProc != proc)
                    return;
                stopMicTest();
                proc->deleteLater();
                logSystem("Mic test: could not start the recorder.");
            });

    proc->start(rec.program, rec.args);
    if (!proc->waitForStarted(3000))
        return; // errorOccurred handles cleanup

    m_voiceTestRecording = true;
    m_voiceTestPos = 0;
    if (!m_voiceTestTimer) {
        m_voiceTestTimer = new QTimer(this);
        m_voiceTestTimer->setInterval(80);
        connect(m_voiceTestTimer, &QTimer::timeout, this,
                &MainWindow::updateMicTestMeter);
    }
    if (m_voiceTestMeter)
        m_voiceTestMeter->setValue(0);
    m_voiceTestTimer->start();
    m_voiceTestMicButton->setText("Stop test");
}

// Drive the test-mic level bar from the freshly-captured tail of the WAV. Mirrors
// updateVoiceLevelMeter(): attack fast, release slow, square-rooted so ordinary
// speech moves the bar visibly.
void MainWindow::updateMicTestMeter()
{
    if (!m_voiceTestMeter)
        return;
    const double peak = wavLevelSince(m_voiceTestWavPath, &m_voiceTestPos);
    const int cur = m_voiceTestMeter->value();
    int next;
    if (peak < 0.0) {
        next = qMax(0, cur - 14);
    } else {
        const int target = int(qBound(0.0, qSqrt(peak) * 135.0, 100.0));
        next = target >= cur ? target : qMax(target, cur - 14);
    }
    if (next != cur)
        m_voiceTestMeter->setValue(next);
}

// Stop the test recording, reset the bar, and clean up the temp WAV.
void MainWindow::stopMicTest()
{
    m_voiceTestRecording = false;
    if (m_voiceTestTimer)
        m_voiceTestTimer->stop();
    if (m_voiceTestProc && m_voiceTestProc->state() != QProcess::NotRunning) {
        // Detach the finished handler's path before terminating so it doesn't
        // re-enter stopMicTest() while we're already tearing down.
        QProcess *p = m_voiceTestProc;
        m_voiceTestProc = nullptr;
        p->terminate();
    } else {
        m_voiceTestProc = nullptr;
    }
    if (m_voiceTestMeter)
        m_voiceTestMeter->setValue(0);
    if (m_voiceTestMicButton)
        m_voiceTestMicButton->setText("Test mic");
    if (!m_voiceTestWavPath.isEmpty())
        QFile::remove(m_voiceTestWavPath);
}

// Clone (or update), build, and fetch a model for whisper.cpp, streaming the
// build log to the live network log. On success the mic button appears next to
// the prompt. Runs as a detached-from-Settings QProcess so closing Settings
// doesn't abort the build.
void MainWindow::installWhisperCpp()
{
    if (m_whisperInstallProc &&
        m_whisperInstallProc->state() != QProcess::NotRunning)
        return; // already running

    const QString dir = whisperDir();
    const QString model = m_whisperModelCombo
                              ? m_whisperModelCombo->currentData().toString()
                              : whisperModelName();
    // Persist the choices now so detection (whisperDir/whisperModelName) lines up
    // with what this build produces.
    QSettings().setValue(kWhisperDirSetting, dir);
    QSettings().setValue(kWhisperModelSetting, model);

    // One self-contained build script. Idempotent: re-running pulls the latest,
    // rebuilds, and re-fetches the model only if missing.
    static const char *kScript = R"sh(
set -e
DIR="$1"; MODEL="$2"
mkdir -p "$DIR"
if [ -d "$DIR/.git" ]; then
  echo "Updating whisper.cpp in $DIR"
  git -C "$DIR" pull --ff-only || true
else
  echo "Cloning whisper.cpp into $DIR"
  git clone --depth 1 https://github.com/ggerganov/whisper.cpp "$DIR"
fi
cd "$DIR"
echo "Building whisper.cpp (this can take a few minutes)"
cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build --config Release -j
if [ ! -f "models/ggml-$MODEL.bin" ]; then
  echo "Downloading model $MODEL"
  sh ./models/download-ggml-model.sh "$MODEL"
fi
echo "whisper.cpp ready"
)sh";

    auto *proc = new QProcess(this);
    m_whisperInstallProc = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    logSystem("Installing whisper.cpp for voice input\xE2\x80\xA6");
    refreshWhisperStatus();

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (m_whisperStatusLabel)
                m_whisperStatusLabel->setText(trimmed.right(160));
        }
    });
    connect(proc, &QProcess::finished, this,
            [this, proc, model](int exitCode, QProcess::ExitStatus status) {
                proc->deleteLater();
                if (m_whisperInstallProc == proc)
                    m_whisperInstallProc = nullptr;
                const bool ok = exitCode == 0 &&
                                status == QProcess::NormalExit && whisperInstalled();
                if (ok) {
                    logSystem("whisper.cpp installed \xE2\x80\x94 voice input ready.");
                    if (m_whisperStatusLabel)
                        m_whisperStatusLabel->setText(
                            QString::fromUtf8("\xE2\x97\x8F Installed (%1 model). A "
                                              "mic now sits next to the prompt box.")
                                .arg(model));
                } else {
                    logSystem("whisper.cpp install failed (exit " +
                              QString::number(exitCode) +
                              "). Check that git, cmake and a C++ compiler are "
                              "installed.");
                    if (m_whisperStatusLabel)
                        m_whisperStatusLabel->setText(
                            "Install failed. Ensure git, cmake and a C++ compiler "
                            "are installed, then try again.");
                }
                refreshWhisperStatus();
                updateVoiceInputButton();
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc](QProcess::ProcessError err) {
                // finished() handles every error except FailedToStart (where the
                // process never ran and finished is not emitted).
                if (err != QProcess::FailedToStart || m_whisperInstallProc != proc)
                    return;
                m_whisperInstallProc = nullptr;
                proc->deleteLater();
                logSystem("Could not start the whisper.cpp build (sh not found?).");
                if (m_whisperStatusLabel)
                    m_whisperStatusLabel->setText("Could not start the build process.");
                refreshWhisperStatus();
            });

    proc->start(QStringLiteral("sh"),
                {QStringLiteral("-c"), QString::fromLatin1(kScript),
                 QStringLiteral("sh"), dir, model});
}

// Install whichever engine the Settings selector points at. Keeps the single
// "Download & install" button wired to the right provisioner.
void MainWindow::installVoiceEngine()
{
    if (voiceEngine() == QStringLiteral("parakeet"))
        installParakeet();
    else
        installWhisperCpp();
}

// Provision NVIDIA Parakeet for local dictation: build a self-contained Python
// venv and install a Parakeet runner into it (parakeet-mlx on Apple Silicon, NeMo
// elsewhere), then drop a transcribe.py wrapper the mic shells out to. Streams the
// pip log to the network log; runs detached from Settings like installWhisperCpp.
void MainWindow::installParakeet()
{
    if (m_parakeetInstallProc &&
        m_parakeetInstallProc->state() != QProcess::NotRunning)
        return; // already running

    const QString dir = parakeetDir();
    const QString model = m_parakeetModelCombo
                              ? m_parakeetModelCombo->currentData().toString()
                              : parakeetModelName();
    QSettings().setValue(kVoiceEngineSetting, QStringLiteral("parakeet"));
    QSettings().setValue(kParakeetModelSetting, model);

    // The wrapper the mic runs per clip: it loads Parakeet (MLX where available,
    // else NeMo) and writes the transcript to the output path. Written from here so
    // detection (parakeetInstalled()) and transcription both find it. Kept ASCII so
    // it embeds cleanly as a raw C++ string.
    static const char *kTranscribePy = R"py(
import sys

def main():
    if len(sys.argv) < 3:
        sys.exit("usage: transcribe.py <wav> <out.txt> [model]")
    wav, out = sys.argv[1], sys.argv[2]
    model = sys.argv[3] if len(sys.argv) > 3 else "parakeet-tdt-0.6b-v2"

    try:
        from parakeet_mlx import from_pretrained
        have_mlx = True
    except Exception:
        have_mlx = False

    if have_mlx:
        repo = model if "/" in model else "mlx-community/" + model
        m = from_pretrained(repo)
        text = (getattr(m.transcribe(wav), "text", "") or "").strip()
    else:
        import nemo.collections.asr as nemo_asr
        repo = model if "/" in model else "nvidia/" + model
        m = nemo_asr.models.ASRModel.from_pretrained(repo)
        res = m.transcribe([wav])
        first = res[0] if res else ""
        text = (getattr(first, "text", first) or "").strip()

    with open(out, "w", encoding="utf-8") as f:
        f.write(text)

if __name__ == "__main__":
    main()
)py";

    QDir().mkpath(dir);
    {
        QFile f(parakeetScriptPath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(kTranscribePy);
    }

    // Build the venv and install the runner. macOS gets parakeet-mlx (fast, local);
    // other platforms get NeMo's ASR stack. Idempotent: re-running upgrades.
    static const char *kScript = R"sh(
set -e
DIR="$1"
mkdir -p "$DIR"
echo "Creating Python environment in $DIR"
python3 -m venv "$DIR/venv"
PY="$DIR/venv/bin/python"
"$PY" -m pip install --upgrade pip >/dev/null
case "$(uname -s)" in
  Darwin)
    echo "Installing parakeet-mlx (this can take a few minutes)"
    "$PY" -m pip install -U parakeet-mlx ;;
  *)
    echo "Installing NeMo ASR (this can take several minutes)"
    "$PY" -m pip install -U "nemo_toolkit[asr]" ;;
esac
echo "Parakeet ready"
)sh";

    auto *proc = new QProcess(this);
    m_parakeetInstallProc = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    logSystem("Installing Parakeet for voice input\xE2\x80\xA6");
    refreshWhisperStatus();

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (m_whisperStatusLabel)
                m_whisperStatusLabel->setText(trimmed.right(160));
        }
    });
    connect(proc, &QProcess::finished, this,
            [this, proc](int exitCode, QProcess::ExitStatus status) {
                proc->deleteLater();
                if (m_parakeetInstallProc == proc)
                    m_parakeetInstallProc = nullptr;
                const bool ok = exitCode == 0 &&
                                status == QProcess::NormalExit && parakeetInstalled();
                if (ok) {
                    logSystem("Parakeet installed \xE2\x80\x94 voice input ready.");
                    if (m_whisperStatusLabel)
                        m_whisperStatusLabel->setText(QString::fromUtf8(
                            "\xE2\x97\x8F Installed (Parakeet). A mic now sits next "
                            "to the prompt box."));
                } else {
                    logSystem("Parakeet install failed (exit " +
                              QString::number(exitCode) +
                              "). Check that python3 (with venv + pip) is installed.");
                    if (m_whisperStatusLabel)
                        m_whisperStatusLabel->setText(
                            "Install failed. Ensure python3 with venv and pip are "
                            "installed, then try again.");
                }
                refreshWhisperStatus();
                updateVoiceInputButton();
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart || m_parakeetInstallProc != proc)
                    return;
                m_parakeetInstallProc = nullptr;
                proc->deleteLater();
                logSystem("Could not start the Parakeet install (sh not found?).");
                if (m_whisperStatusLabel)
                    m_whisperStatusLabel->setText("Could not start the install process.");
                refreshWhisperStatus();
            });

    proc->start(QStringLiteral("sh"),
                {QStringLiteral("-c"), QString::fromLatin1(kScript),
                 QStringLiteral("sh"), dir});
}

// -------------------------------------------------------------- diagnostics

namespace {
// Classify a network-log message into a colored, single-word category badge so
// the log reads at a glance. Failures always win (red); otherwise notable
// keywords decide the accent. Returns the accent color (hex) and badge text.
struct NetworkLogStyle {
    QString accent;
    QString badge;
};

NetworkLogStyle networkLogStyleFor(const QString &message)
{
    const QString lower = message.toLower();
    // Errors / failures take precedence over any category.
    if (lower.contains("fail") || lower.contains("error") ||
        lower.contains("could not") || lower.contains("couldn't") ||
        lower.contains("no live") || lower.contains("denied") ||
        lower.contains("blocks ") || lower.contains("unable")) {
        return {QStringLiteral("#f85149"), QStringLiteral("ERROR")};
    }
    // Each entry: substring to look for (lower-case) -> {accent, badge}. First
    // match wins, so order from most specific to most general.
    struct Rule {
        const char *needle;
        const char *accent;
        const char *badge;
    };
    static const Rule rules[] = {
        // App start/stop/rebuild-restart markers — keep above "fork" so
        // "ForkMesh" in the start line doesn't get tagged FORK.
        {"session started", "#f2cc60", "SESSION"},
        {"session ended", "#f2cc60", "SESSION"},
        {"quick update started", "#f2cc60", "SESSION"},
        {"rebuild & restart started", "#f2cc60", "SESSION"},
        {"restarting now", "#f2cc60", "SESSION"},
        {"pull request", "#3fb950", "PULL"},
        {"pull #", "#3fb950", "PULL"},
        {"merged", "#a371f7", "MERGE"},
        {"bounty", "#d29922", "BOUNTY"},
        {"escrow", "#d29922", "BOUNTY"},
        {"solana", "#d29922", "WALLET"},
        {"funded", "#d29922", "BOUNTY"},
        {"mirror", "#39c5cf", "MIRROR"},
        {"sync", "#39c5cf", "SYNC"},
        {"fork", "#3fb950", "FORK"},
        {"publish", "#58a6ff", "PUBLISH"},
        {"push", "#58a6ff", "GIT"},
        {"git:", "#58a6ff", "GIT"},
        {"commit", "#58a6ff", "GIT"},
        {"patch", "#58a6ff", "GIT"},
        {"issue", "#bc8cff", "ISSUE"},
        {"admin", "#db6d28", "ADMIN"},
        {"identity", "#79c0ff", "IDENTITY"},
        {"encryption", "#79c0ff", "CRYPTO"},
        {"connected", "#3fb950", "NODE"},
        {"peer", "#3fb950", "NODE"},
        {"node", "#3fb950", "NODE"},
        {"copied", "#8b949e", "CLIP"},
        {"saved", "#3fb950", "SAVE"},
    };
    for (const Rule &r : rules) {
        if (lower.contains(QLatin1String(r.needle)))
            return {QString::fromLatin1(r.accent), QString::fromLatin1(r.badge)};
    }
    return {QStringLiteral("#6e7681"), QStringLiteral("INFO")};
}
} // namespace

void MainWindow::appendNetworkLogLine(const QString &storedLine)
{
    if (!m_settingsLog)
        return;

    // The badge accents below read on either canvas, but the timestamp, day
    // divider and message body need per-theme greys/text so the log isn't grey
    // text washed out on the light (#ffffff) background. Dark keeps its lighter
    // ink on the near-black canvas; light uses GitHub's near-black body text.
    const bool dark = currentThemeIsDark();
    const QString messageColor =
        dark ? QStringLiteral("#adbac7") : QStringLiteral("#1f2328");
    const QString timeColor =
        dark ? QStringLiteral("#6e7681") : QStringLiteral("#656d76");
    const QString dividerLabelColor =
        dark ? QStringLiteral("#8b949e") : QStringLiteral("#656d76");
    const QString dividerDashColor =
        dark ? QStringLiteral("#484f58") : QStringLiteral("#afb8c1");

    // Stored format: "yyyy-MM-dd HH:mm:ss  message". Parse leniently so any
    // legacy/odd line still renders (as a plain message with no timestamp).
    QString date, time, message = storedLine;
    if (storedLine.size() >= 21 && storedLine.at(10) == QLatin1Char(' ')) {
        date = storedLine.left(10);
        time = storedLine.mid(11, 8);
        message = storedLine.mid(21);
    }

    // Day divider whenever the calendar date changes from the previous line.
    if (!date.isEmpty() && date != m_lastLogRenderDate) {
        m_lastLogRenderDate = date;
        const QString pretty =
            QDate::fromString(date, QStringLiteral("yyyy-MM-dd"))
                .toString(QStringLiteral("dddd, d MMMM yyyy"));
        m_settingsLog->appendHtml(
            QString::fromUtf8(
                "<span style='color:%1'>"
                "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80&nbsp;</span>"
                "<span style='color:%2; font-weight:600'>%3</span>"
                "<span style='color:%4'>&nbsp;"
                "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80</span>")
                .arg(dividerDashColor, dividerLabelColor,
                     (pretty.isEmpty() ? date : pretty).toHtmlEscaped(),
                     dividerDashColor));
    }

    const NetworkLogStyle style = networkLogStyleFor(message);
    QString html;
    if (!time.isEmpty())
        html += QStringLiteral("<span style='color:%1'>%2</span>&nbsp;&nbsp;")
                    .arg(timeColor, time);
    html += QStringLiteral(
                "<span style='color:%1; font-weight:700'>%2</span>&nbsp;&nbsp;"
                "<span style='color:%3'>%4</span>")
                .arg(style.accent,
                     style.badge.leftJustified(7).toHtmlEscaped(),
                     messageColor,
                     message.toHtmlEscaped());
    m_settingsLog->appendHtml(html);
}

QString MainWindow::logBadgeFor(const QString &storedLine) const
{
    // Stored format: "yyyy-MM-dd HH:mm:ss  message" — classify by the message.
    const QString message =
        (storedLine.size() >= 21 && storedLine.at(10) == QLatin1Char(' '))
            ? storedLine.mid(21)
            : storedLine;
    return networkLogStyleFor(message).badge;
}

void MainWindow::rebuildLogFilterButtons()
{
    if (!m_logFilterRow)
        return;
    // Tear down the previous chips (and their exclusive group).
    QLayoutItem *item = nullptr;
    while ((item = m_logFilterRow->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    if (m_logFilterGroup)
        m_logFilterGroup->deleteLater();
    m_logFilterGroup = new QButtonGroup(this);
    m_logFilterGroup->setExclusive(true);

    auto addChip = [this](const QString &label, const QString &category) {
        auto *chip = new QPushButton(label);
        chip->setObjectName("logFilterChip");
        chip->setCheckable(true);
        chip->setChecked(m_logFilter == category);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setToolTip(category.isEmpty()
                             ? QStringLiteral("Show every event")
                             : QStringLiteral("Show only %1 events").arg(label));
        m_logFilterGroup->addButton(chip);
        m_logFilterRow->addWidget(chip);
        connect(chip, &QPushButton::clicked, this, [this, category] {
            m_logFilter = category;
            rebuildNetworkLogView();
        });
    };

    addChip(QStringLiteral("All"), QString());
    // Show present categories in a stable, readable order.
    static const char *order[] = {
        "SESSION", "NODE",   "FORK",  "MIRROR",   "SYNC",  "GIT",
        "PUBLISH", "PULL",   "MERGE", "ISSUE",    "BOUNTY", "WALLET",
        "CRYPTO",  "IDENTITY", "ADMIN", "SAVE",   "CLIP",  "ERROR",
        "INFO",
    };
    for (const char *b : order) {
        const QString badge = QString::fromLatin1(b);
        if (m_logFilterCategories.contains(badge))
            addChip(badge, badge);
    }
    m_logFilterRow->addStretch();
}

#ifdef FORKMESH_WINDOW_TESTS
void MainWindow::testResetNetworkLog()
{
    m_networkLog.clear();
    m_networkLogDiskLines = 0;
    QFile::remove(networkLogPath());
    rebuildNetworkLogView();
}
#endif

void MainWindow::rebuildNetworkLogView()
{
    if (!m_settingsLog)
        return;
    m_settingsLog->clear();
    m_lastLogRenderDate.clear();
    for (const QString &line : std::as_const(m_networkLog)) {
        if (!m_logFilter.isEmpty() && logBadgeFor(line) != m_logFilter)
            continue;
        appendNetworkLogLine(line);
    }
}

QString MainWindow::networkLogPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/network_log.txt";
}

void MainWindow::loadNetworkLog()
{
    const QString path = networkLogPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(f.readAll())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    f.close();
    m_networkLogDiskLines = lines.size();
    m_networkLog = lines;
    while (m_networkLog.size() > kNetworkLogLimit)
        m_networkLog.removeFirst();
}

void MainWindow::saveNetworkLog()
{
    const QString path = networkLogPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    if (!m_networkLog.isEmpty()) {
        f.write(m_networkLog.join(QLatin1Char('\n')).toUtf8());
        f.write("\n");
    }
    f.close();
    m_networkLogDiskLines = m_networkLog.size();
}

void MainWindow::logSystem(const QString &text)
{
    // Some callers (e.g. flashMessage("") to dismiss the toast) pass empty or
    // whitespace-only text; skip those instead of leaving a blank log entry.
    if (text.trimmed().isEmpty())
        return;

    const QString time =
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString plain = text;
    plain.replace(QChar(0x2014), QLatin1Char('-'));
    plain.replace(QChar(0x2026), QStringLiteral("..."));
    const QString line = time + "  " + plain;
    m_networkLog.append(line);
    while (m_networkLog.size() > kNetworkLogLimit)
        m_networkLog.removeFirst();

    // A category we haven't seen yet earns its own quick-filter chip.
    const QString badge = networkLogStyleFor(plain).badge;
    if (!m_logFilterCategories.contains(badge)) {
        m_logFilterCategories.insert(badge);
        rebuildLogFilterButtons(); // no-ops until the log section is built
    }
    // Only render the line if it passes the active filter.
    if (m_logFilter.isEmpty() || m_logFilter == badge)
        appendNetworkLogLine(line);

    // Mirror the newest event onto the always-on footer log line so the latest
    // activity is visible at the bottom of the app even when the Log tab is closed.
    // Pass the full dated line (not just the message) so the bottom strip shows the
    // same timestamped log line as the Log view.
    setFooterUpdateLine(line);

    // Persist incrementally so the history survives a restart (even an unclean
    // one). Periodically rewrite the file to trim it back to the in-memory cap.
    QFile lf(networkLogPath());
    if (lf.open(QIODevice::Append | QIODevice::Text)) {
        lf.write(line.toUtf8());
        lf.write("\n");
        lf.close();
        if (++m_networkLogDiskLines > kNetworkLogLimit * 2)
            saveNetworkLog();
    }
}

// Toast pill caps the inline message at this many characters; longer text is
// elided to one line and revealed in full via the Expand button.
static constexpr int kToastMaxChars = 100;

// Auto-dismiss windows for the top toast. Every toast counts down visibly so the
// notification area never flashes a message away unannounced. Success
// confirmations clear quickly; errors linger far longer (but still show a
// countdown) so a failure can be read and copied before it fades — its full text
// is also preserved in the network log regardless.
static constexpr int kToastSuccessSeconds = 5;
static constexpr int kToastErrorSeconds = 20;

// Cap on how many error toasts can back up in m_topMessageQueue; a runaway
// retry loop firing errors faster than they can be read shouldn't grow this
// without bound. The oldest queued message is dropped once the cap is hit.
static constexpr int kToastQueueLimit = 20;

// (Re)paint the toast from m_topMessageRaw, honoring the expand/collapse state.
// A long message shows as an elided one-liner so it can never widen the window;
// expanding it wraps the full text so the toast grows in place (no modal).
void MainWindow::renderTopMessage()
{
    if (!m_topMessage)
        return;
    // Green for success, red for failure; compact pill in the centre of the bar.
    const QString fg = m_topMessageError ? "#f85149" : "#3fb950";
    const QString glyph = m_topMessageError ? QString::fromUtf8("\xE2\x9C\x95")  // ✕
                                            : QString::fromUtf8("\xE2\x9C\x93"); // ✓
    // The inline toast always stays a single elided one-liner; expanding never
    // wraps or grows it. The full text is revealed in the floating overlay below
    // instead, so it can't widen the window or push the layout around.
    QString display = m_topMessageRaw;
    if (m_topMessageElided)
        display = display.left(kToastMaxChars - 1).trimmed()
                  + QString::fromUtf8("\xE2\x80\xA6"); // …
    m_topMessage->setWordWrap(false);
    // The base HTML carries the message; auto-dismissing successes append a
    // ticking countdown suffix on top of it (see renderTopMessageCountdown).
    // When a click target is set, the message text itself becomes an underlined
    // link (routed by the m_topMessage linkActivated handler) so e.g. an "agent is
    // waiting for you" toast is clickable straight through to that agent.
    QString body = display.toHtmlEscaped();
    if (!m_topMessageHref.isEmpty())
        body = QStringLiteral(
                   "<a href='%1' style='color:%2;text-decoration:underline'>%3</a>")
                   .arg(m_topMessageHref.toHtmlEscaped(), fg, body);
    m_topMessageBaseHtml = QStringLiteral("<span style='color:%1'>%2 %3</span>")
                               .arg(fg, glyph, body);
    m_topMessage->setText(m_topMessageBaseHtml);
    // The expand toggle's glyph tracks the state: chevron-down to reveal more,
    // chevron-up to collapse back to the one-liner.
    if (m_topMessageExpand) {
        setOcticon(m_topMessageExpand,
                   m_topMessageExpanded ? "chevron-up" : "chevron-down", 14);
        m_topMessageExpand->setToolTip(m_topMessageExpanded
                                           ? QStringLiteral("Collapse the message")
                                           : QStringLiteral("Show the full message"));
    }
    // Float the full, wrapped message on top of the layout when expanded; hide
    // the panel again when collapsed.
    if (m_topMessageOverlay && m_topMessageOverlayText) {
        if (m_topMessageExpanded) {
            m_topMessageOverlayText->setText(
                QStringLiteral("<span style='color:%1'>%2 %3</span>")
                    .arg(fg, glyph, m_topMessageRaw.toHtmlEscaped()));
            positionTopMessageOverlay();
            m_topMessageOverlay->show();
            m_topMessageOverlay->raise();
        } else {
            m_topMessageOverlay->hide();
        }
    }
}

// Size the floating expanded-toast panel to its content (capped to a readable
// width) and anchor it just under the inline toast, centred on it but clamped to
// stay inside the window. Called on expand and on window resize.
void MainWindow::positionTopMessageOverlay()
{
    if (!m_topMessageOverlay || !m_topMessage)
        return;
    const int margin = 16;
    const int w = qMin(620, qMax(240, width() - 2 * margin));
    m_topMessageOverlay->setFixedWidth(w);
    int h = m_topMessageOverlay->heightForWidth(w);
    if (h <= 0)
        h = m_topMessageOverlay->sizeHint().height();
    m_topMessageOverlay->setFixedHeight(h);
    // Anchor just below the inline toast, horizontally centred on it.
    const QPoint anchor = m_topMessage->mapTo(this, QPoint(0, m_topMessage->height()));
    int x = anchor.x() + m_topMessage->width() / 2 - w / 2;
    x = qBound(margin, x, width() - w - margin);
    m_topMessageOverlay->move(x, anchor.y() + 6);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Keep the floating expanded-toast panel anchored to the (re-centred) toast.
    if (m_topMessageOverlay && m_topMessageOverlay->isVisible())
        positionTopMessageOverlay();
}

void MainWindow::flashMessage(const QString &text, bool error,
                              const QString &clickHref)
{
    // A real result supersedes any in-flight progress pill (showLoadStatus).
    m_loadStatusShowing = false;
    // Carry an optional click target so the whole toast can act as a link (e.g. an
    // "agent is waiting for you" toast jumps to that agent). Cleared by default so
    // an ordinary toast is never left clickable from a previous message.
    m_topMessageHref = clickHref;
    // Always keep a copy in the network log for history.
    logSystem(text);
    if (!m_topMessage)
        return;

    const QString trimmed = text.simplified();
    if (trimmed.isEmpty()) {
        dismissTopMessage();
        return;
    }
    // A second error arriving while one is already counting down would otherwise
    // instantly replace it, so a burst of quick failures (retries, batched
    // errors) could flash by unread. Queue it instead; advanceTopMessageQueue
    // shows it with its own full countdown once the current toast finishes.
    if (error && m_topMessage->isVisible() && m_topMessageError &&
        m_topMessageTimer && m_topMessageTimer->isActive()) {
        m_topMessageQueue.append(trimmed);
        while (m_topMessageQueue.size() > kToastQueueLimit)
            m_topMessageQueue.removeFirst();
        renderTopMessageCountdown(); // repaint the "(+N more)" suffix
        return;
    }
    // A generic toast supersedes the integrity-pin warning (it'll be re-shown on the
    // next refreshRepoPinBanner if still stale), so this is no longer the pin toast.
    m_pinWarningActive = false;
    m_topMessageError = error;
    m_topMessageRaw = trimmed;
    // Keep the pill compact: a long message (a multi-line git error, say) must not
    // stretch the top bar and drag the whole window wide. Show an elided one-liner;
    // the full text is preserved in m_topMessageRaw and is revealed inline by the
    // Expand button (see renderTopMessage) or copied via Copy.
    m_topMessageElided = trimmed.size() > kToastMaxChars;
    m_topMessageExpanded = false; // every new message starts collapsed
    renderTopMessage();
    m_topMessage->show();

    if (!m_topMessageTimer) {
        // Ticks once a second so the countdown is visible; when the count runs out
        // it dismisses the whole toast (label plus any Copy / ✕ / Expand
        // affordances) rather than firing a single timeout.
        m_topMessageTimer = new QTimer(this);
        connect(m_topMessageTimer, &QTimer::timeout, this, [this] {
            if (!m_topMessage)
                return;
            if (--m_topMessageSecondsLeft <= 0) {
                advanceTopMessageQueue();
                return;
            }
            renderTopMessageCountdown();
        });
    }
    // Every toast counts down visibly so the notification area never flashes a
    // message away without the user knowing how long it stayed (or what it was).
    // Errors keep their Copy / ✕ buttons and get a much longer window, so a
    // failure stays readable and grabbable for a bug report before it fades; its
    // full text also remains in the network log (logSystem above) regardless.
    if (error) {
        if (m_topMessageCopy)
            m_topMessageCopy->show();
        if (m_topMessageClose)
            m_topMessageClose->show();
        m_topMessageSecondsLeft = kToastErrorSeconds;
    } else {
        if (m_topMessageCopy)
            m_topMessageCopy->hide();
        if (m_topMessageClose)
            m_topMessageClose->hide();
        m_topMessageSecondsLeft = kToastSuccessSeconds;
    }
    renderTopMessageCountdown();
    m_topMessageTimer->start(1000);
    // The Expand affordance appears only when the message was truncated, so the
    // user can read it in full inline instead of via a popup.
    if (m_topMessageExpand)
        m_topMessageExpand->setVisible(m_topMessageElided);
}

// Repaint the toast as its base message plus a dimmed "· Ns" countdown suffix,
// reflecting how many seconds remain before an auto-dismissing toast fades.
void MainWindow::renderTopMessageCountdown()
{
    if (!m_topMessage)
        return;
    // "·" is a byte-escaped glyph, so it must go through fromUtf8 (QStringLiteral
    // would mangle the multibyte sequence).
    const QString suffix =
        QString::fromUtf8(" <span style='color:#6e7681'>\xC2\xB7 %1s</span>")
            .arg(m_topMessageSecondsLeft);
    // Tell the user more errors are waiting behind this one, so a fading toast
    // doesn't feel like it silently dropped the rest of a quick burst.
    QString queuedSuffix;
    if (m_topMessageError && !m_topMessageQueue.isEmpty())
        queuedSuffix = QStringLiteral(" <span style='color:#6e7681'>(+%1 more)</span>")
                           .arg(m_topMessageQueue.size());
    m_topMessage->setText(m_topMessageBaseHtml + suffix + queuedSuffix);
}

// Hide the top toast and its error affordances (Expand / Copy / dismiss). This
// is a hard reset: any errors still waiting behind the current one are dropped
// too (their full text remains in the network log regardless).
void MainWindow::dismissTopMessage()
{
    m_loadStatusShowing = false;
    m_pinWarningActive = false;
    m_topMessageExpanded = false;
    m_topMessageHref.clear(); // the next toast opts back in to clickability if it wants it
    m_topMessageQueue.clear();
    if (m_topMessageTimer)
        m_topMessageTimer->stop(); // don't keep ticking the countdown on a hidden toast
    if (m_topMessage) {
        m_topMessage->hide();
        m_topMessage->setWordWrap(false); // back to a one-liner for the next toast
    }
    if (m_topMessageOverlay)
        m_topMessageOverlay->hide(); // drop the floating expanded panel with the toast
    if (m_topMessageExpand)
        m_topMessageExpand->hide();
    if (m_topMessageCopy)
        m_topMessageCopy->hide();
    if (m_topMessageClose)
        m_topMessageClose->hide();
}

// Show the next queued error (its own full countdown, per flashMessage), or
// fully dismiss the toast if nothing is waiting. Called when the current
// toast's countdown runs out or the user dismisses it early.
void MainWindow::advanceTopMessageQueue()
{
    if (m_topMessageQueue.isEmpty()) {
        dismissTopMessage();
        return;
    }
    const QString next = m_topMessageQueue.takeFirst();
    flashMessage(next, /*error=*/true);
}

void MainWindow::notifyIfInactive(const QString &title, const QString &body)
{
    if (isActiveWindow())
        return;

    QApplication::alert(this, 0);
    QString cleanBody = body.simplified();
    if (cleanBody.size() > 180)
        cleanBody = cleanBody.left(177) + "...";
    postNotification(title, cleanBody);
}

// ----------------------------------------------------------------- security tab

QWidget *MainWindow::buildVulnReportTab()
{
    auto *body = new QWidget;
    auto *col = new QVBoxLayout(body);
    col->setContentsMargins(2, 14, 2, 14);
    col->setSpacing(10);

    auto *headLabel = new QLabel("REPORT A VULNERABILITY");
    headLabel->setObjectName("sectionLabel");

    auto *hint = new QLabel(
        "Submit a private, encrypted vulnerability report directly to the ForkMesh "
        "security team. Your report is stored encrypted and never published. "
        "Please do not file security issues in the public issue tracker.");
    hint->setObjectName("statusLine");
    hint->setWordWrap(true);

    m_vulnComponentCombo = new QComboBox;
    m_vulnComponentCombo->addItem("Identity & signing",      QStringLiteral("identity"));
    m_vulnComponentCombo->addItem("Relay / Durable Objects", QStringLiteral("relay"));
    m_vulnComponentCombo->addItem("Mirroring",               QStringLiteral("mirroring"));
    m_vulnComponentCombo->addItem("Encrypted chat",          QStringLiteral("chat"));
    m_vulnComponentCombo->addItem("Donations / Solana",      QStringLiteral("donations"));
    m_vulnComponentCombo->addItem("Website / dashboard",     QStringLiteral("website"));
    m_vulnComponentCombo->addItem("Desktop client",          QStringLiteral("client"));
    m_vulnComponentCombo->addItem("Other",                   QStringLiteral("other"));
    m_vulnComponentCombo->setToolTip("Which part of ForkMesh is affected.");

    m_vulnTitleEdit = new QLineEdit;
    m_vulnTitleEdit->setMaxLength(200);
    m_vulnTitleEdit->setPlaceholderText("Short title, e.g. \"RCE via crafted git pack\"");

    m_vulnBodyEdit = new QPlainTextEdit;
    m_vulnBodyEdit->setPlaceholderText(
        "Describe the vulnerability: what it is, how to reproduce it, and its "
        "potential impact. Proof-of-concept code or steps are very helpful.");
    m_vulnBodyEdit->setMinimumHeight(160);
    m_vulnBodyEdit->setMaximumHeight(300);

    m_vulnContactEdit = new QLineEdit;
    m_vulnContactEdit->setMaxLength(254);
    m_vulnContactEdit->setPlaceholderText(
        "Optional contact \xe2\x80\x94 email or handle for follow-up");

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setSpacing(8);
    form->addRow("Component",          m_vulnComponentCombo);
    form->addRow("Title",              m_vulnTitleEdit);
    form->addRow("Description",        m_vulnBodyEdit);
    form->addRow("Contact (optional)", m_vulnContactEdit);

    m_vulnSubmitButton = new QPushButton("Submit report");
    m_vulnSubmitButton->setObjectName("primaryButton");
    m_vulnSubmitButton->setCursor(Qt::PointingHandCursor);
    m_vulnSubmitButton->setToolTip(
        "Send an encrypted vulnerability report to the ForkMesh security team.");
    connect(m_vulnSubmitButton, &QPushButton::clicked, this,
            &MainWindow::submitVulnerabilityReport);

    m_vulnStatusLabel = new QLabel;
    m_vulnStatusLabel->setObjectName("modeHint");
    m_vulnStatusLabel->setWordWrap(true);
    m_vulnStatusLabel->hide();

    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->addWidget(m_vulnSubmitButton);
    btnRow->addStretch();

    col->addWidget(headLabel);
    col->addWidget(hint);
    col->addSpacing(4);
    col->addLayout(form);
    col->addLayout(btnRow);
    col->addWidget(m_vulnStatusLabel);
    col->addStretch();
    return body;
}

void MainWindow::submitVulnerabilityReport()
{
    if (!m_vulnTitleEdit || !m_vulnBodyEdit || !m_vulnComponentCombo
        || !m_vulnSubmitButton || !m_vulnStatusLabel)
        return;

    const QString title     = m_vulnTitleEdit->text().trimmed();
    const QString body      = m_vulnBodyEdit->toPlainText().trimmed();
    const QString component = m_vulnComponentCombo->currentData().toString();
    const QString contact   = m_vulnContactEdit ? m_vulnContactEdit->text().trimmed()
                                                : QString();

    auto setStatus = [this](const QString &msg, bool bad) {
        m_vulnStatusLabel->setText(msg);
        m_vulnStatusLabel->setProperty("bad", bad);
        m_vulnStatusLabel->style()->unpolish(m_vulnStatusLabel);
        m_vulnStatusLabel->style()->polish(m_vulnStatusLabel);
        m_vulnStatusLabel->show();
    };

    if (title.isEmpty()) {
        setStatus("Please enter a title for the vulnerability.", true);
        return;
    }
    if (body.isEmpty()) {
        setStatus("Please describe the vulnerability in the Description field.", true);
        return;
    }

    m_vulnSubmitButton->setEnabled(false);
    m_vulnSubmitButton->setText(QString::fromUtf8("Sending\xe2\x80\xa6"));
    m_vulnStatusLabel->hide();

    const QJsonObject payload{
        {QStringLiteral("title"),     title},
        {QStringLiteral("body"),      body},
        {QStringLiteral("component"), component},
        {QStringLiteral("contact"),   contact},
    };
    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/security/report"));
    url.setQuery(QString());
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, setStatus] {
        reply->deleteLater();
        m_vulnSubmitButton->setEnabled(true);
        m_vulnSubmitButton->setText("Submit report");
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200 || status == 201) {
            setStatus(
                "Your report has been submitted. Thank you for helping keep "
                "ForkMesh secure. We\xe2\x80\x99ll follow up if you provided "
                "contact details.",
                false);
            m_vulnTitleEdit->clear();
            m_vulnBodyEdit->clear();
            m_vulnContactEdit->clear();
        } else {
            setStatus("Could not submit the report. Please check your connection "
                      "and try again, or email security@forkmesh.com directly.",
                      true);
        }
    });
}

